#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Audio engine: a fetcher fills an in-memory buffer (YouTube's audio is a few MB per song), an ffmpeg
// decoder reads from it through a custom AVIO and feeds SDL's audio queue. Network sources are fetched in
// 1 MB ranges (one big request gets throttled), local files are read straight from the SD card.
class Player {
public:
    enum State { Idle, Loading, Playing, Paused, Ended, Error };
    struct Session;   // one opened song (fetcher + decoder threads)

    bool init();        // opens the audio device
    void shutdown();

    void openUrl(const std::string &url, int64_t contentLength, const std::string &userAgent, double startAt = 0);
    void openFile(const std::string &path, double startAt = 0);
    void stop();
    void setPaused(bool p);
    void togglePause() { setPaused(state() == Playing); }
    void seek(double sec);
    void setVolume(int v);     // 0..100
    int volume() const { return vol.load(); }

    State state() const { return st.load(); }
    double position() const;
    double duration() const { return dur.load(); }
    double buffered() const;   // 0..1 of the file fetched
    std::string error() const;
    bool finished() { return st.load() == Ended; }

private:
    void startSession(std::shared_ptr<Session> s);
    void fetchLoop(std::shared_ptr<Session> s);
    void decodeLoop(std::shared_ptr<Session> s);

    unsigned dev = 0;
    int rate = 48000;
    std::shared_ptr<Session> cur;
    mutable std::mutex mu;
    std::atomic<State> st{Idle};
    std::atomic<double> dur{0}, posBase{0};
    std::atomic<int> vol{80};
    std::string err;
    std::atomic<int64_t> queuedBytesAtPos{0};
};
