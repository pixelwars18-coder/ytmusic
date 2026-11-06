#pragma once
#include "engine.hpp"
#include <string>

// Reads the app's commands from ctl.json and reports status.json (see shared/bgproto.hpp).
class Service {
public:
    Service(Engine &e, std::string root);
    void poll();          // call every ~100 ms
    bool quitRequested() const { return quit; }
private:
    void apply(const std::string &ctlText);
    void writeStatus(bool force);
    Engine &engine;
    std::string root, lastCtl, lastStatusKey;
    long long lastSeq = 0;
    int64_t lastPing = 0, lastWrite = 0;
    bool quit = false;
};
