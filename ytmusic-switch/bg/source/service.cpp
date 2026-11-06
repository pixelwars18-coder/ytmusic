#include "service.hpp"
#include "../../shared/bgproto.hpp"
#include "../../source/ext/json.hpp"
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

using json = nlohmann::json;
void bglog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

BgService::BgService(Engine &e, std::string r) : engine(e), root(std::move(r)) {
    // carry on from the last command a previous run handled; the app may already have queued new ones
    std::ifstream f(BgProto::status(root));
    if (f) {
        try { lastSeq = json::parse(f).value("seq", 0LL); } catch (...) {}
    }
    writeStatus(true);
}

static std::vector<BgTrack> tracksFrom(const json &a) {
    std::vector<BgTrack> v;
    if (!a.is_array()) return v;
    for (auto &j : a) {
        BgTrack t;
        t.vid = j.value("vid", std::string());
        t.title = j.value("title", std::string());
        t.artist = j.value("artist", std::string());
        t.file = j.value("file", std::string());
        t.url = j.value("url", std::string());
        t.ua = j.value("ua", std::string());
        if (j.contains("duration") && j["duration"].is_number()) t.duration = j["duration"].get<double>();
        t.len = j.value("len", int64_t(0));
        t.exp = j.value("exp", int64_t(0));
        if (!t.vid.empty()) v.push_back(t);
    }
    return v;
}

void BgService::apply(const std::string &text) {
    json root_;
    try { root_ = json::parse(text); } catch (...) { return; }   // half-written: next poll
    for (auto &c : root_.value("cmds", json::array())) {
        const long long seq = c.value("seq", 0LL);
        if (seq <= lastSeq) continue;
        lastSeq = seq;
        if (c.value("t", int64_t(0)) < int64_t(time(nullptr)) - 30) continue;   // stale (left over from an old session)
        const std::string cmd = c.value("cmd", std::string());
        lastPing = int64_t(time(nullptr));
        if (c.contains("visitor") && !c["visitor"].get<std::string>().empty()) engine.visitor = c["visitor"].get<std::string>();
        if (c.contains("volume")) engine.setVolume(c["volume"].get<int>());
        if (c.contains("shuffle")) engine.setMode(c["shuffle"].get<bool>(), c.value("repeat", 0));
        if (cmd == "load") engine.load(tracksFrom(c["queue"]), c.value("index", 0), c.value("pos", 0.0), c.value("paused", false));
        else if (cmd == "queue") engine.setQueue(tracksFrom(c["queue"]), c.value("index", -1));
        else if (cmd == "pause") engine.setPaused(true);
        else if (cmd == "resume") engine.setPaused(false);
        else if (cmd == "toggle") engine.togglePause();
        else if (cmd == "next") engine.next();
        else if (cmd == "prev") engine.prev();
        else if (cmd == "seek") engine.seek(c.value("pos", 0.0));
        else if (cmd == "stop") engine.stop();
        else if (cmd == "quit") { engine.stop(); quit = true; }
        if (!cmd.empty() && cmd != "ping") bglog("cmd %lld %s", seq, cmd.c_str());
    }
    writeStatus(true);
}

void BgService::poll() {
    std::ifstream f(BgProto::ctl(root));
    if (f) {
        std::stringstream ss;
        ss << f.rdbuf();
        std::string text = ss.str();
        if (!text.empty() && text != lastCtl) {
            lastCtl = text;
            apply(text);
        }
    }
    engine.tick();
    writeStatus(false);
}

// Written every second while the app is talking to us, otherwise only on changes and every 20 s (SD wear).
void BgService::writeStatus(bool force) {
    const int64_t now = int64_t(time(nullptr));
    const EngineStatus st = engine.status();
    const std::string key = st.state + "|" + st.vid + "|" + std::to_string(st.index) + "|" + st.err;
    const bool appActive = now - lastPing < 6;
    if (!force && key == lastStatusKey && now - lastWrite < (appActive ? 1 : 20)) return;
    lastStatusKey = key;
    lastWrite = now;
    json j{{"t", now}, {"seq", lastSeq}, {"state", st.state}, {"vid", st.vid}, {"index", st.index}, {"pos", st.pos},
           {"dur", st.dur}, {"vol", st.vol}, {"err", st.err}};
    const std::string path = BgProto::status(root), tmp = path + ".tmp";
    {
        std::ofstream o(tmp, std::ios::trunc);
        if (!o) return;
        o << j.dump();
    }
    std::remove(path.c_str());
    std::rename(tmp.c_str(), path.c_str());
}
