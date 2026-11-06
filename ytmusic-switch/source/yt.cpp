#include "yt.hpp"
#include "net.hpp"
#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>

namespace YT {

static const char *kApi = "https://www.youtube.com/youtubei/v1/";
static const char *kWebVersion = "2.20260708.00.00";

static std::mutex gMu;
static std::string gVisitor;   // X-Goog-Visitor-Id, learnt from the first response

struct Client {
    std::string name;   // label for logs
    json context;       // INNERTUBE_CONTEXT.client
    int id = 0;         // X-YouTube-Client-Name
};

// From yt-dlp 2026.10.26: clients that return direct URLs without a JS player or PO token.
static std::vector<Client> defaultClients() {
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

// clients.json: [{"name":"x","id":101,"context":{...client...}}, ...]
static std::vector<Client> clients() {
    std::ifstream f(Paths::clients());
    if (f) {
        try {
            json j = json::parse(f);
            std::vector<Client> v;
            for (auto &c : j)
                if (c.contains("context") && c.contains("id")) v.push_back({c.value("name", std::string("custom")), c["context"], c["id"].get<int>()});
            if (!v.empty()) return v;
        } catch (...) { logf("clients.json: parse error, using built-in clients"); }
    }
    return defaultClients();
}

static std::string text(const json &j) {
    if (!j.is_object()) return {};
    if (j.contains("simpleText")) return j["simpleText"].get<std::string>();
    if (j.contains("runs") && j["runs"].is_array()) {
        std::string s;
        for (auto &r : j["runs"]) s += r.value("text", std::string());
        return s;
    }
    if (j.contains("content") && j["content"].is_string()) return j["content"].get<std::string>();
    return {};
}

static double parseLength(const std::string &s) {   // "1:02:03" / "3:45"
    double t = 0;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, ':')) {
        try { t = t * 60 + std::stod(part); } catch (...) { return 0; }
    }
    return t;
}

static void rememberVisitor(const json &j) {
    if (!j.contains("responseContext")) return;
    const std::string v = j["responseContext"].value("visitorData", std::string());
    if (!v.empty()) { std::lock_guard<std::mutex> l(gMu); gVisitor = v; }
}

static std::string visitor() { std::lock_guard<std::mutex> l(gMu); return gVisitor; }
std::string visitorId() { return visitor(); }

static Net::Response call(const std::string &ep, json body, const json &clientCtx, int clientId) {
    body["context"]["client"] = clientCtx;
    Net::Headers h{{"Content-Type", "application/json"},
                   {"X-YouTube-Client-Name", std::to_string(clientId)},
                   {"X-YouTube-Client-Version", clientCtx.value("clientVersion", std::string())},
                   {"Origin", "https://www.youtube.com"}};
    if (clientCtx.contains("userAgent")) h["User-Agent"] = clientCtx["userAgent"].get<std::string>();
    else h["User-Agent"] = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/140.0.0.0 Safari/537.36";
    const std::string v = visitor();
    if (!v.empty()) h["X-Goog-Visitor-Id"] = v;
    return Net::post(std::string(kApi) + ep + "?prettyPrint=false", body.dump(), h);
}

static json webClient() { return {{"clientName", "WEB"}, {"clientVersion", kWebVersion}, {"hl", "en"}, {"gl", "US"}}; }

// Walks the whole response: search results are nested deeply and YouTube reshuffles the wrappers often.
static void collect(const json &j, SearchPage &out) {
    if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) {
            const std::string &k = it.key();
            const json &v = it.value();
            if ((k == "videoRenderer" || k == "compactVideoRenderer" || k == "playlistPanelVideoRenderer") && v.is_object() && v.contains("videoId")) {
                Track t;
                t.vid = v["videoId"].get<std::string>();
                t.title = text(v.value("title", json::object()));
                for (const char *by : {"ownerText", "longBylineText", "shortBylineText"}) {
                    t.artist = text(v.value(by, json::object()));
                    if (!t.artist.empty()) break;
                }
                t.duration = parseLength(text(v.value("lengthText", json::object())));
                // live streams / premieres have no length: skip, they don't play as music
                if (t.duration > 0 && !t.title.empty()) out.tracks.push_back(t);
                continue;
            }
            if (k == "continuationCommand" && v.is_object() && v.contains("token") && out.continuation.empty())
                out.continuation = v["token"].get<std::string>();
            collect(v, out);
        }
    } else if (j.is_array()) {
        for (auto &e : j) collect(e, out);
    }
}

static SearchPage runSearch(const json &body) {
    SearchPage page;
    Net::Response r = call("search", body, webClient(), 1);
    if (!r.ok()) {
        page.error = r.error.empty() ? "YouTube said HTTP " + std::to_string(r.status) : r.error;
        return page;
    }
    try {
        json j = json::parse(r.body);
        rememberVisitor(j);
        collect(j, page);
    } catch (const std::exception &e) {
        page.error = std::string("Bad search response: ") + e.what();
    }
    if (page.tracks.empty() && page.error.empty()) page.error = "No results";
    return page;
}

SearchPage search(const std::string &q) {
    // params "EgIQAQ==": results filter "Type: Video"
    return runSearch({{"query", q}, {"params", "EgIQAQ%3D%3D"}});
}

SearchPage searchMore(const std::string &cont) { return runSearch({{"continuation", cont}}); }

static int64_t expiryOf(const std::string &url) {
    static const std::regex re("[?&]expire=(\\d+)");
    std::smatch m;
    if (std::regex_search(url, m, re)) return std::stoll(m[1].str());
    return 0;
}

// Make sure the URL actually serves bytes (a client can hand out URLs that 403).
static bool verifyUrl(const std::string &url, const std::string &ua) {
    std::string got;
    std::string err;
    std::atomic<bool> cancel{false};
    const long st = Net::stream(url + "&range=0-2047", [&](const char *p, size_t n) { got.append(p, n); return got.size() < 4096; },
                                &cancel, &err, {{"User-Agent", ua}});
    return (st == 200 || st == 206) && !got.empty();
}

Stream resolve(const std::string &vid, bool verify) {
    Stream best;
    std::string lastErr = "no client worked";
    if (visitor().empty()) search("music");   // learn a visitor id first (some clients refuse without one)
    for (const Client &c : clients()) {
        json body = {{"videoId", vid},
                     {"playbackContext", {{"contentPlaybackContext", {{"html5Preference", "HTML5_PREF_WANTS"}}}}},
                     {"contentCheckOk", true},
                     {"racyCheckOk", true}};
        Net::Response r = call("player", body, c.context, c.id);
        if (!r.ok()) { lastErr = c.name + ": HTTP " + std::to_string(r.status) + " " + r.error; continue; }
        json j;
        try { j = json::parse(r.body); } catch (...) { lastErr = c.name + ": bad JSON"; continue; }
        rememberVisitor(j);
        const json ps = j.value("playabilityStatus", json::object());
        const std::string status = ps.value("status", std::string());
        if (status != "OK") {
            lastErr = ps.value("reason", status.empty() ? std::string("not playable") : status);
            logf("player %s %s: %s", c.name.c_str(), vid.c_str(), lastErr.c_str());
            continue;
        }
        const json sd = j.value("streamingData", json::object());
        // pick: AAC/M4A 128k (itag 140) first, then the best other audio-only format with a plain URL
        const json *pick = nullptr;
        int pickScore = -1;
        if (sd.contains("adaptiveFormats"))
            for (const json &f : sd["adaptiveFormats"]) {
                const std::string mime = f.value("mimeType", std::string());
                if (mime.rfind("audio/", 0) != 0 || !f.contains("url")) continue;
                const int itag = f.value("itag", 0);
                const int br = f.value("bitrate", 0);
                const bool drc = f.value("isDrc", false);
                int score = (itag == 140 ? 1000000000 : 0) + (mime.find("mp4") != std::string::npos ? 500000000 : 0) + br - (drc ? 400000000 : 0);
                if (score > pickScore) { pickScore = score; pick = &f; }
            }
        if (!pick) { lastErr = c.name + ": no audio URL"; continue; }
        Stream s;
        s.url = (*pick)["url"].get<std::string>();
        s.mime = pick->value("mimeType", std::string());
        s.itag = pick->value("itag", 0);
        s.client = c.name;
        s.userAgent = c.context.value("userAgent", std::string());
        try { s.contentLength = std::stoll(pick->value("contentLength", std::string("0"))); } catch (...) {}
        try { s.duration = std::stod(pick->value("approxDurationMs", std::string("0"))) / 1000.0; } catch (...) {}
        s.expires = expiryOf(s.url);
        const json vd = j.value("videoDetails", json::object());
        s.meta.vid = vid;
        s.meta.title = vd.value("title", std::string());
        s.meta.artist = vd.value("author", std::string());
        try { s.meta.duration = std::stod(vd.value("lengthSeconds", std::string("0"))); } catch (...) {}
        if (verify && !verifyUrl(s.url, c.context.value("userAgent", std::string()))) {
            lastErr = c.name + ": stream URL refused";
            logf("%s", lastErr.c_str());
            continue;
        }
        return s;
    }
    best.error = lastErr;
    return best;
}

std::vector<Track> radio(const std::string &vid) {
    SearchPage page;
    Net::Response r = call("next", {{"videoId", vid}, {"playlistId", "RD" + vid}}, webClient(), 1);
    if (!r.ok()) return {};
    try { collect(json::parse(r.body), page); } catch (...) {}
    std::vector<Track> out;
    for (auto &t : page.tracks) {
        bool dup = false;
        for (auto &o : out) dup |= o.vid == t.vid;
        if (!dup) out.push_back(t);
    }
    return out;
}

std::string videoIdFromText(const std::string &s) {
    static const std::regex re("(?:v=|youtu\\.be/|shorts/|embed/)([A-Za-z0-9_-]{11})");
    std::smatch m;
    if (std::regex_search(s, m, re)) return m[1].str();
    static const std::regex bare("^[A-Za-z0-9_-]{11}$");
    if (std::regex_match(s, bare)) return s;
    return {};
}

std::string thumbUrl(const std::string &vid, bool large) { return "https://i.ytimg.com/vi/" + vid + (large ? "/sddefault.jpg" : "/hqdefault.jpg"); }

} // namespace YT
