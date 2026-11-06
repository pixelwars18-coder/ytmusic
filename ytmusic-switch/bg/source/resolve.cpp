#include "resolve.hpp"
#include "../../source/ext/json.hpp"
#include "../../source/net.hpp"
#include <fstream>
#include <vector>

using json = nlohmann::json;

namespace BgResolve {

struct Client { std::string name; json ctx; int id; };

static std::vector<Client> clients(const std::string &path) {
    std::ifstream f(path);
    if (f) {
        try {
            json j = json::parse(f);
            std::vector<Client> v;
            for (auto &c : j)
                if (c.contains("context") && c.contains("id")) v.push_back({c.value("name", std::string("custom")), c["context"], c["id"].get<int>()});
            if (!v.empty()) return v;
        } catch (...) {}
    }
    return {
        {"visionos",
         {{"clientName", "VISIONOS"}, {"clientVersion", "1.02"}, {"deviceMake", "Apple"}, {"deviceModel", "RealityDevice17,1"},
          {"userAgent", "Mozilla/5.0 (Macintosh; Intel Mac OS X 15_7_3) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/26.0 Safari/605.1.15"},
          {"osName", "visionOS"}, {"osVersion", "26.5.23O471"}, {"hl", "en"}},
         101},
        {"android_vr",
         {{"clientName", "ANDROID_VR"}, {"clientVersion", "1.65.10"}, {"deviceMake", "Oculus"}, {"deviceModel", "Quest 3"},
          {"androidSdkVersion", 32},
          {"userAgent", "com.google.android.apps.youtube.vr.oculus/1.65.10 (Linux; U; Android 12L; eureka-user Build/SQ3A.220605.009.A1) gzip"},
          {"osName", "Android"}, {"osVersion", "12L"}, {"hl", "en"}},
         28},
    };
}

int64_t expiryOf(const std::string &url) {
    const size_t p = url.find("expire=");
    if (p == std::string::npos) return 0;
    return std::strtoll(url.c_str() + p + 7, nullptr, 10);
}

// Pulls only what we need out of the /player response while it streams through the parser.
// `path` holds the keys from the root down; array elements add a "[]" marker.
struct Sax : nlohmann::json_sax<json> {
    struct Fmt { int itag = 0; std::string url, mime; int64_t len = 0; int64_t bitrate = 0; bool drc = false; };
    std::vector<std::string> path;
    std::vector<bool> objPushed;
    std::vector<int> arrPushed;
    std::string status, reason, visitor, pendingKey;
    std::vector<Fmt> fmts;
    Fmt curFmt;

    bool inFmt() const { return path.size() == 3 && path[0] == "streamingData" && path[1] == "adaptiveFormats" && path[2] == "[]"; }

    bool null() override { pendingKey.clear(); return true; }
    bool boolean(bool b) override {
        if (inFmt() && pendingKey == "isDrc") curFmt.drc = b;
        pendingKey.clear();
        return true;
    }
    bool number_integer(number_integer_t v) override { return number_unsigned(number_unsigned_t(v)); }
    bool number_unsigned(number_unsigned_t v) override {
        if (inFmt()) {
            if (pendingKey == "itag") curFmt.itag = int(v);
            else if (pendingKey == "bitrate") curFmt.bitrate = int64_t(v);
        }
        pendingKey.clear();
        return true;
    }
    bool number_float(number_float_t, const string_t &) override { pendingKey.clear(); return true; }
    bool string(string_t &s) override {
        if (path.size() == 1) {
            if (path[0] == "playabilityStatus" && pendingKey == "status") status = s;
            else if (path[0] == "playabilityStatus" && pendingKey == "reason") reason = s;
            else if (path[0] == "responseContext" && pendingKey == "visitorData") visitor = s;
        } else if (inFmt()) {
            if (pendingKey == "url") curFmt.url = s;
            else if (pendingKey == "mimeType") curFmt.mime = s;
            else if (pendingKey == "contentLength") curFmt.len = std::strtoll(s.c_str(), nullptr, 10);
        }
        pendingKey.clear();
        return true;
    }
    bool binary(binary_t &) override { pendingKey.clear(); return true; }
    bool key(string_t &k) override { pendingKey = k; return true; }
    bool start_object(std::size_t) override {
        // a keyed object adds its key; the root and array elements add nothing (the "[]" is already there)
        const bool push = !pendingKey.empty();
        if (push) path.push_back(pendingKey);
        objPushed.push_back(push);
        pendingKey.clear();
        if (inFmt()) curFmt = Fmt{};
        return true;
    }
    bool end_object() override {
        if (inFmt() && !curFmt.url.empty() && curFmt.mime.rfind("audio/", 0) == 0) fmts.push_back(curFmt);
        const bool push = !objPushed.empty() && objPushed.back();
        if (!objPushed.empty()) objPushed.pop_back();
        if (push && !path.empty()) path.pop_back();
        return true;
    }
    bool start_array(std::size_t) override {
        int n = 1;
        if (!pendingKey.empty()) { path.push_back(pendingKey); n = 2; }
        path.push_back("[]");
        arrPushed.push_back(n);
        pendingKey.clear();
        return true;
    }
    bool end_array() override {
        int n = arrPushed.empty() ? 1 : arrPushed.back();
        if (!arrPushed.empty()) arrPushed.pop_back();
        while (n-- > 0 && !path.empty()) path.pop_back();
        return true;
    }
    bool parse_error(std::size_t, const std::string &, const nlohmann::detail::exception &) override { return false; }
};

Result resolve(const std::string &vid, const std::string &visitor, const std::string &clientsPath) {
    Result best;
    std::string lastErr = "no client worked";
    for (const Client &c : clients(clientsPath)) {
        json body = {{"videoId", vid},
                     {"context", {{"client", c.ctx}}},
                     {"playbackContext", {{"contentPlaybackContext", {{"html5Preference", "HTML5_PREF_WANTS"}}}}},
                     {"contentCheckOk", true},
                     {"racyCheckOk", true}};
        Net::Headers h{{"Content-Type", "application/json"},
                       {"X-YouTube-Client-Name", std::to_string(c.id)},
                       {"X-YouTube-Client-Version", c.ctx.value("clientVersion", std::string())},
                       {"Origin", "https://www.youtube.com"},
                       {"User-Agent", c.ctx.value("userAgent", std::string())}};
        if (!visitor.empty()) h["X-Goog-Visitor-Id"] = visitor;
        Net::Response r = Net::post("https://www.youtube.com/youtubei/v1/player?prettyPrint=false", body.dump(), h);
        if (!r.ok()) { lastErr = c.name + ": HTTP " + std::to_string(r.status) + " " + r.error; continue; }
        Sax sax;
        json::sax_parse(r.body, &sax);
        r.body.clear();
        r.body.shrink_to_fit();
        if (sax.status != "OK") { lastErr = sax.reason.empty() ? (sax.status.empty() ? "not playable" : sax.status) : sax.reason; continue; }
        // Opus/WebM first (the service's decoder set is small), then AAC/M4A
        const Sax::Fmt *pick = nullptr;
        long long score = -1;
        for (const auto &f : sax.fmts) {
            long long s = (f.itag == 251 ? 3000000000LL : 0) + (f.mime.find("webm") != std::string::npos ? 1000000000LL : 0) +
                          (f.itag == 140 ? 500000000LL : 0) + f.bitrate - (f.drc ? 2000000000LL : 0);
            if (s > score) { score = s; pick = &f; }
        }
        if (!pick) { lastErr = c.name + ": no audio URL"; continue; }
        best.url = pick->url;
        best.mime = pick->mime;
        best.itag = pick->itag;
        best.contentLength = pick->len;
        best.expires = expiryOf(pick->url);
        best.userAgent = c.ctx.value("userAgent", std::string());
        best.visitor = sax.visitor;
        return best;
    }
    best.error = lastErr;
    return best;
}

} // namespace BgResolve
