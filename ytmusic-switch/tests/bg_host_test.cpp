// Drives the background service (engine + control files) on Linux against the real YouTube, the same way
// the app does on the console: append commands to ctl.json, read status.json.
#include "../bg/source/engine.hpp"
#include "../bg/source/service.hpp"
#include "../shared/bgproto.hpp"
#include "../source/ext/json.hpp"
#include "../source/net.hpp"
#include <SDL2/SDL.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <functional>
#include <sstream>
#include <sys/stat.h>
#include <thread>
using json = nlohmann::json;
using namespace std::chrono_literals;

void bglog(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    printf("  [bg] ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

struct SdlSink : Sink {
    SDL_AudioDeviceID dev = 0;
    float vol = 1;
    bool open() override {
        SDL_AudioSpec want{}, have{};
        want.freq = kRate; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 2048;
        dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        if (dev) SDL_PauseAudioDevice(dev, 0);
        return dev != 0;
    }
    int queuedFrames() override { return int(SDL_GetQueuedAudioSize(dev) / 4); }
    void write(const int16_t *pcm, int frames) override {
        std::vector<int16_t> b(pcm, pcm + frames * 2);
        for (auto &x : b) x = int16_t(x * vol);
        SDL_QueueAudio(dev, b.data(), Uint32(frames * 4));
    }
    void clear() override { SDL_ClearQueuedAudio(dev); }
    void setPaused(bool p) override { SDL_PauseAudioDevice(dev, p); }
    void setVolume(float v) override { vol = v; }
};

static const std::string kRoot = "./bgdata";
static json cmds = json::array();
static long long seq = 0;

static void send(json c) {
    c["seq"] = ++seq;
    cmds.push_back(c);
    std::ofstream(BgProto::ctl(kRoot) + ".tmp") << json{{"cmds", cmds}}.dump();
    std::remove(BgProto::ctl(kRoot).c_str());
    std::rename((BgProto::ctl(kRoot) + ".tmp").c_str(), BgProto::ctl(kRoot).c_str());
}

static json status() {
    std::ifstream f(BgProto::status(kRoot));
    try { return json::parse(f); } catch (...) { return json::object(); }
}

static bool waitFor(int secs, const std::function<bool(const json &)> &pred) {
    for (int i = 0; i < secs * 10; ++i) {
        if (pred(status())) return true;
        std::this_thread::sleep_for(100ms);
    }
    return false;
}

int main() {
    int fails = 0;
    auto check = [&](bool ok, const char *what) { printf("%s %s | %s\n", ok ? "PASS" : "FAIL", what, status().dump().c_str()); if (!ok) ++fails; };
    mkdir(kRoot.c_str(), 0777);
    mkdir(BgProto::dir(kRoot).c_str(), 0777);
    std::remove(BgProto::ctl(kRoot).c_str());
    SDL_Init(SDL_INIT_AUDIO);
    Net::init();
    SdlSink sink;
    sink.open();
    Engine engine(&sink, kRoot);
    Service service(engine, kRoot);
    std::atomic<bool> quit{false};
    std::thread loop([&] { while (!quit) { service.poll(); std::this_thread::sleep_for(100ms); } });

    json q = json::array({{{"vid", "jNQXAC9IVRw"}, {"title", "Me at the zoo"}, {"artist", "jawed"}, {"duration", 19}},
                          {{"vid", "dQw4w9WgXcQ"}, {"title", "Never Gonna Give You Up"}, {"artist", "Rick Astley"}, {"duration", 213}}});
    auto t0 = std::chrono::steady_clock::now();
    send({{"cmd", "load"}, {"queue", q}, {"index", 1}, {"pos", 0}, {"volume", 70}, {"shuffle", false}, {"repeat", 0}});
    const bool playing = waitFor(30, [](const json &s) { return s.value("state", "") == "playing" && s.value("pos", 0.0) > 0.5; });
    printf("start took %lld ms\n", (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
    check(playing, "load + play (resolve, fetch to file, decode)");
    std::this_thread::sleep_for(2s);
    check(status().value("pos", 0.0) > 2, "position advances");
    send({{"cmd", "pause"}});
    check(waitFor(3, [](const json &s) { return s.value("state", "") == "paused"; }), "pause");
    const double p0 = status().value("pos", 0.0);
    std::this_thread::sleep_for(1500ms);
    check(std::fabs(status().value("pos", 0.0) - p0) < 0.3, "paused position holds");
    send({{"cmd", "toggle"}});
    check(waitFor(3, [](const json &s) { return s.value("state", "") == "playing"; }), "toggle resumes");
    send({{"cmd", "seek"}, {"pos", 120}});
    check(waitFor(5, [](const json &s) { double p = s.value("pos", 0.0); return p > 119 && p < 130; }), "seek to 2:00");
    send({{"cmd", "prev"}});   // > 5 s in: restarts the song
    check(waitFor(5, [](const json &s) { return s.value("pos", 99.0) < 5 && s.value("index", -1) == 1; }), "prev restarts song");
    // auto-advance: repeat all, short video near its end
    send({{"cmd", "load"}, {"queue", q}, {"index", 0}, {"pos", 14}, {"shuffle", false}, {"repeat", 1}});
    check(waitFor(30, [](const json &s) { return s.value("index", -1) == 0 && s.value("state", "") == "playing"; }), "load song 0 at 0:14");
    check(waitFor(30, [](const json &s) { return s.value("index", -1) == 1 && s.value("state", "") == "playing"; }), "auto-advance to next song");
    send({{"cmd", "next"}});   // repeat all: wraps to 0
    check(waitFor(20, [](const json &s) { return s.value("index", -1) == 0 && s.value("state", "") == "playing"; }), "next wraps (repeat all)");
    send({{"cmd", "queue"}, {"queue", json::array({q[1], q[0]})}, {"index", 1}});
    check(waitFor(3, [](const json &s) { return s.value("index", -1) == 1 && s.value("vid", "") == "jNQXAC9IVRw" && s.value("state", "") == "playing"; }), "queue edit keeps playing");
    send({{"cmd", "quit"}});
    check(waitFor(3, [&](const json &s) { return service.quitRequested(); }), "quit");
    quit = true;
    loop.join();
    printf("%s: %d failure(s)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
