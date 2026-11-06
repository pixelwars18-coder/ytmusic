#pragma once
#include "sink.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct BgTrack {
    std::string vid, title, artist, file;   // file: downloaded copy on the SD card, if any
    double duration = 0;
    std::string url, ua;                    // pre-resolved stream (optional)
    int64_t len = 0, exp = 0;
};

struct EngineStatus {
    std::string state = "idle";   // idle loading playing paused error
    std::string vid, err;
    int index = -1;
    double pos = 0, dur = 0;
    int vol = 80;
};

// The background player: owns the queue, resolves and fetches each song into a cache file on the SD card
// (no big RAM buffers), decodes it with a minimal ffmpeg and plays through a Sink. Moves on by itself when
// a song ends, so music keeps going with the app closed.
class Engine {
public:
    Engine(Sink *sink, std::string root);   // root: sdmc:/switch/ytmusic (cache + clients.json live there)
    ~Engine();

    void load(std::vector<BgTrack> q, int index, double pos, bool paused);
    void setQueue(std::vector<BgTrack> q, int index);   // edit without interrupting the current song
    void setPaused(bool p);
    void togglePause();
    void next();
    void prev();
    void seek(double sec);
    void setVolume(int v);
    void setMode(bool shuffle, int repeat);
    void stop();
    void tick();          // call ~10x a second: auto-advance, errors, look-ahead
    EngineStatus status();
    std::string visitor;  // X-Goog-Visitor-Id from the app

    struct Session;
private:
    void start(int index, double pos, bool paused);
    void stopSession();
    void runSession(std::shared_ptr<Session> s);
    void fetchLoop(std::shared_ptr<Session> s);
    void decodeLoop(std::shared_ptr<Session> s);
    int pickNext(bool automatic);
    void lookAhead();

    Sink *sink;
    std::string root;
    std::mutex mu;
    std::vector<BgTrack> queue;
    int index = -1, repeat = 0, vol = 80, nextPick = -1, errorStreak = 0;
    bool shuffle = false;
    std::shared_ptr<Session> cur;
    int flip = 0;
    std::thread ahead;
    std::atomic<bool> aheadBusy{false};
    std::string aheadVid;
};
