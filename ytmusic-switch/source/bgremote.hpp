#pragma once
#include "common.hpp"
#include "../shared/bgproto.hpp"
#include <cstdint>

// App side of background playback: installs and starts the ytmusic-bg sysmodule, sends it commands
// (ctl.json) and reads what it's doing (status.json).
class BgRemote {
public:
    struct Status {
        std::string state = "idle", vid, err;
        int index = -1, vol = 80;
        double pos = 0, dur = 0;
        int64_t t = 0;
        uint32_t receivedMs = 0;   // when this status arrived (for smooth position)
        bool ok = false;
    };

    std::string start();     // install (if needed) + launch; returns an error or ""
    void stop();             // ask it to quit and terminate it
    bool running();          // process exists (console) / status is fresh (Linux)
    bool alive() const;      // status.json recently written
    void send(json cmd);     // appends to ctl.json (keeps the last 40 commands)
    void poll(uint32_t nowMs);
    const Status &status() const { return st; }
    double position(uint32_t nowMs) const;

private:
    std::string lastStatusText;
    Status st;
    json cmds = json::array();
    long long seq = 0;
};
