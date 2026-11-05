#pragma once
#include "common.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

// Saves songs to sdmc:/switch/ytmusic/downloads/<videoId>.m4a, one at a time in the background.
// index.json there keeps the title/artist so the Downloads tab can list them offline.
class Downloads {
public:
    void start();
    void stop();

    int add(const std::vector<Track> &tracks);   // newly queued count
    void cancel(const std::string &vid);
    bool remove(const std::string &vid);

    std::string pathFor(const std::string &vid) const;   // empty if not downloaded
    bool isDownloaded(const std::string &vid) const;
    bool isActive(const std::string &vid) const;
    bool isRunning(const std::string &vid) const;
    double progress(const std::string &vid) const;
    std::vector<Track> list() const;                     // downloaded, newest first
    int pending() const;

    // messages for the UI toast (taken once)
    std::vector<std::string> takeMessages();

private:
    void worker();
    bool fetch(const Track &t);
    void saveIndex();
    void loadIndex();

    struct Entry { Track t; std::string file; int64_t at = 0; };
    mutable std::mutex mu;
    std::condition_variable cv;
    std::map<std::string, Entry> done;
    std::deque<Track> queue;
    std::string running;
    double prog = 0;
    std::atomic<bool> quit{false}, cancelCur{false};
    std::thread th;
    std::vector<std::string> msgs;
};
