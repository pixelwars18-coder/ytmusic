#include "bgremote.hpp"
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#ifdef __SWITCH__
#include <switch.h>
#endif

static std::string readAll(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string BgRemote::start() {
    mkdirs(BgProto::dir(Paths::root()));
    // continue numbering after whatever the service has already seen
    try {
        json s = json::parse(readAll(BgProto::status(Paths::root())));
        seq = std::max(seq, s.value("seq", 0LL));
    } catch (...) {}
    try {
        json c = json::parse(readAll(BgProto::ctl(Paths::root())));
        for (auto &x : c.value("cmds", json::array())) seq = std::max(seq, x.value("seq", 0LL));
    } catch (...) {}
#ifdef __SWITCH__
    // the service ships inside this .nro (romfs:/bg/exefs.nsp); copy it where Atmosphère looks for it
    const std::string dir = std::string("sdmc:/atmosphere/contents/") + BgProto::kProgramIdStr;
    const std::string dst = dir + "/exefs.nsp";
    const std::string bundled = readAll("romfs:/bg/exefs.nsp");
    if (bundled.empty()) return "The background service is missing from this build";
    if (readAll(dst) != bundled) {
        if (running()) stop();   // replace an older version
        mkdirs(dir);
        std::ofstream o(dst, std::ios::binary | std::ios::trunc);
        o.write(bundled.data(), std::streamsize(bundled.size()));
        if (!o.good()) return "Couldn't write to the SD card";
    }
    if (running()) return {};
    if (R_FAILED(pmshellInitialize())) return "Can't start services (pm:shell unavailable)";
    NcmProgramLocation loc{};
    loc.program_id = BgProto::kProgramId;
    loc.storageID = NcmStorageId_None;
    u64 pid = 0;
    const Result rc = pmshellLaunchProgram(0, &loc, &pid);
    pmshellExit();
    if (R_FAILED(rc)) {
        char b[96];
        snprintf(b, sizeof b, "Couldn't start the background service (error 0x%x)", rc);
        return b;
    }
    return {};
#else
    return {};   // Linux tests run the service themselves
#endif
}

void BgRemote::stop() {
    send({{"cmd", "quit"}});
#ifdef __SWITCH__
    for (int i = 0; i < 20 && running(); ++i) svcSleepThread(50'000'000);
    if (running() && R_SUCCEEDED(pmshellInitialize())) {
        pmshellTerminateProgram(BgProto::kProgramId);
        pmshellExit();
    }
#endif
    st = Status{};
}

bool BgRemote::running() {
#ifdef __SWITCH__
    u64 pid = 0;
    if (R_FAILED(pmdmntInitialize())) return alive();
    const bool r = R_SUCCEEDED(pmdmntGetProcessId(&pid, BgProto::kProgramId)) && pid != 0;
    pmdmntExit();
    return r;
#else
    return alive();
#endif
}

bool BgRemote::alive() const { return st.ok && nowSecs() - st.t < BgProto::kAliveSecs; }

void BgRemote::send(json c) {
    c["seq"] = ++seq;
    c["t"] = nowSecs();   // the service ignores stale commands
    cmds.push_back(c);
    if (cmds.size() > 40) cmds.erase(cmds.begin(), cmds.begin() + long(cmds.size() - 40));
    const std::string path = BgProto::ctl(Paths::root()), tmp = path + ".tmp";
    {
        std::ofstream o(tmp, std::ios::trunc);
        o << json{{"cmds", cmds}}.dump();
    }
    std::remove(path.c_str());
    std::rename(tmp.c_str(), path.c_str());
}

void BgRemote::poll(uint32_t nowMs) {
    const std::string text = readAll(BgProto::status(Paths::root()));
    if (text.empty() || text == lastStatusText) return;
    try {
        json j = json::parse(text);
        lastStatusText = text;
        st.state = j.value("state", std::string("idle"));
        st.vid = j.value("vid", std::string());
        st.err = j.value("err", std::string());
        st.index = j.value("index", -1);
        st.vol = j.value("vol", 80);
        st.pos = j.value("pos", 0.0);
        st.dur = j.value("dur", 0.0);
        st.t = j.value("t", int64_t(0));
        st.receivedMs = nowMs;
        st.ok = true;
    } catch (...) {}   // caught it mid-write: next poll
}

double BgRemote::position(uint32_t nowMs) const {
    if (st.state != "playing") return st.pos;
    const double p = st.pos + (nowMs - st.receivedMs) / 1000.0;
    return st.dur > 0 ? std::min(p, st.dur) : p;
}
