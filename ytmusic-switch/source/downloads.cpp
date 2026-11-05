#include "downloads.hpp"
#include "net.hpp"
#include "yt.hpp"
#include <algorithm>
#include <cstdio>
#include <fstream>

static std::string indexPath() { return Paths::downloads() + "/index.json"; }

void Downloads::start() {
    mkdirs(Paths::downloads());
    loadIndex();
    th = std::thread([this] { worker(); });
}

void Downloads::stop() {
    quit = true;
    cancelCur = true;
    cv.notify_all();
    if (th.joinable()) th.join();
}

void Downloads::loadIndex() {
    std::ifstream f(indexPath());
    if (!f) return;
    try {
        json j = json::parse(f);
        for (auto &e : j) {
            Entry en{Track::fromJson(e), e.value("file", std::string()), e.value("at", int64_t(0))};
            if (!en.t.vid.empty() && fileExists(Paths::downloads() + "/" + en.file)) done[en.t.vid] = en;
        }
    } catch (...) {}
}

void Downloads::saveIndex() {   // called with mu held
    json a = json::array();
    for (auto &[vid, e] : done) {
        json j = e.t.toJson();
        j["file"] = e.file;
        j["at"] = e.at;
        a.push_back(j);
    }
    const std::string tmp = indexPath() + ".tmp";
    { std::ofstream f(tmp, std::ios::trunc); f << a.dump(); }
    ::remove(indexPath().c_str());
    rename(tmp.c_str(), indexPath().c_str());
}

int Downloads::add(const std::vector<Track> &tracks) {
    std::lock_guard<std::mutex> l(mu);
    int n = 0;
    for (auto &t : tracks) {
        if (t.vid.empty() || done.count(t.vid) || running == t.vid) continue;
        if (std::any_of(queue.begin(), queue.end(), [&](const Track &q) { return q.vid == t.vid; })) continue;
        queue.push_back(t);
        ++n;
    }
    cv.notify_all();
    return n;
}

void Downloads::cancel(const std::string &vid) {
    std::lock_guard<std::mutex> l(mu);
    queue.erase(std::remove_if(queue.begin(), queue.end(), [&](const Track &t) { return t.vid == vid; }), queue.end());
    if (running == vid) cancelCur = true;
}

bool Downloads::remove(const std::string &vid) {
    std::lock_guard<std::mutex> l(mu);
    auto it = done.find(vid);
    if (it == done.end()) return false;
    ::remove((Paths::downloads() + "/" + it->second.file).c_str());
    done.erase(it);
    saveIndex();
    return true;
}

std::string Downloads::pathFor(const std::string &vid) const {
    std::lock_guard<std::mutex> l(mu);
    auto it = done.find(vid);
    return it == done.end() ? std::string() : Paths::downloads() + "/" + it->second.file;
}
bool Downloads::isDownloaded(const std::string &vid) const { std::lock_guard<std::mutex> l(mu); return done.count(vid) > 0; }
bool Downloads::isRunning(const std::string &vid) const { std::lock_guard<std::mutex> l(mu); return running == vid; }
bool Downloads::isActive(const std::string &vid) const {
    std::lock_guard<std::mutex> l(mu);
    return running == vid || std::any_of(queue.begin(), queue.end(), [&](const Track &t) { return t.vid == vid; });
}
double Downloads::progress(const std::string &vid) const { std::lock_guard<std::mutex> l(mu); return running == vid ? prog : 0; }
int Downloads::pending() const { std::lock_guard<std::mutex> l(mu); return int(queue.size()) + (running.empty() ? 0 : 1); }

std::vector<Track> Downloads::list() const {
    std::lock_guard<std::mutex> l(mu);
    std::vector<const Entry *> v;
    for (auto &[k, e] : done) v.push_back(&e);
    std::sort(v.begin(), v.end(), [](const Entry *a, const Entry *b) { return a->at > b->at; });
    std::vector<Track> out;
    for (auto *e : v) out.push_back(e->t);
    return out;
}

std::vector<std::string> Downloads::takeMessages() {
    std::lock_guard<std::mutex> l(mu);
    std::vector<std::string> m;
    m.swap(msgs);
    return m;
}

void Downloads::worker() {
    int okCount = 0, failCount = 0;
    while (!quit) {
        Track t;
        {
            std::unique_lock<std::mutex> l(mu);
            cv.wait(l, [&] { return quit || !queue.empty(); });
            if (quit) break;
            t = queue.front();
            queue.pop_front();
            running = t.vid;
            prog = 0;
            cancelCur = false;
        }
        const bool ok = fetch(t);
        std::lock_guard<std::mutex> l(mu);
        running.clear();
        if (ok) ++okCount;
        else if (!cancelCur) ++failCount;
        if (queue.empty() && okCount > 0) {
            char b[160];
            if (okCount == 1 && failCount == 0) snprintf(b, sizeof b, "Downloaded \"%.100s\"", t.title.c_str());
            else snprintf(b, sizeof b, "Downloaded %d song%s%s", okCount, okCount == 1 ? "" : "s", failCount ? " (some failed)" : "");
            msgs.push_back(b);
            okCount = failCount = 0;
        } else if (queue.empty()) failCount = 0;
    }
}

bool Downloads::fetch(const Track &t) {
    YT::Stream s = YT::resolve(t.vid, true);
    if (!s.ok()) {
        std::lock_guard<std::mutex> l(mu);
        msgs.push_back("Download failed: " + s.error.substr(0, 80));
        return false;
    }
    const std::string ext = s.mime.find("webm") != std::string::npos ? "webm" : "m4a";
    const std::string file = t.vid + "." + ext;
    const std::string part = Paths::downloads() + "/" + file + ".part";
    FILE *f = fopen(part.c_str(), "wb");
    if (!f) {
        std::lock_guard<std::mutex> l(mu);
        msgs.push_back("Can't write to the SD card");
        return false;
    }
    std::vector<char> buf;
    buf.reserve(512 * 1024);
    int64_t have = 0;
    const int64_t total = s.contentLength;
    const std::string ua = s.userAgent;
    bool failed = false;
    int retries = 0;
    constexpr int64_t kChunk = 1 << 20;
    while (!cancelCur && !quit) {
        if (total > 0 && have >= total) break;
        const int64_t end = (total > 0 ? std::min(total, have + kChunk) : have + kChunk) - 1;
        int64_t got = 0;
        std::string err;
        const long code = Net::stream(s.url + "&range=" + std::to_string(have) + "-" + std::to_string(end),
                                      [&](const char *p, size_t n) {
                                          if (fwrite(p, 1, n, f) != n) return false;
                                          got += int64_t(n);
                                          if (total > 0) { std::lock_guard<std::mutex> l(mu); prog = double(have + got) / double(total); }
                                          return true;
                                      },
                                      &cancelCur, &err, {{"User-Agent", ua}});
        have += got;
        if (code == 200 || code == 206) {
            retries = 0;
            if (total <= 0 && got < kChunk) break;
            continue;
        }
        if (cancelCur || quit || ++retries > 4) { failed = true; break; }
    }
    fclose(f);
    if (failed || cancelCur || quit || (total > 0 && have < total)) {
        ::remove(part.c_str());
        if (!cancelCur && !quit) { std::lock_guard<std::mutex> l(mu); msgs.push_back("Download failed: " + t.title.substr(0, 60)); }
        return false;
    }
    const std::string final = Paths::downloads() + "/" + file;
    ::remove(final.c_str());
    rename(part.c_str(), final.c_str());
    std::lock_guard<std::mutex> l(mu);
    Track meta = t;
    if (meta.title.empty()) meta = s.meta;
    done[t.vid] = {meta, file, nowSecs()};
    saveIndex();
    return true;
}
