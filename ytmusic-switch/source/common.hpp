#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "ext/json.hpp"

using json = nlohmann::json;

struct Track {
    std::string vid, title, artist;
    double duration = 0;
    bool operator==(const Track &o) const { return vid == o.vid; }
    json toJson() const { return {{"vid", vid}, {"title", title}, {"artist", artist}, {"duration", duration}}; }
    static Track fromJson(const json &j) {
        Track t;
        if (!j.is_object()) return t;
        t.vid = j.value("vid", std::string());
        t.title = j.value("title", std::string());
        t.artist = j.value("artist", std::string());
        if (j.contains("duration") && j["duration"].is_number()) t.duration = j["duration"].get<double>();
        return t;
    }
};

namespace Paths {
#ifdef __SWITCH__
inline std::string root() { return "sdmc:/switch/ytmusic"; }
#else
std::string root();   // host tests: ./ytmusic-data
#endif
inline std::string state() { return root() + "/state.json"; }
inline std::string downloads() { return root() + "/downloads"; }
inline std::string clients() { return root() + "/clients.json"; }
inline std::string backup() { return root() + "/ytmusic-backup.json"; }
inline std::string log() { return root() + "/log.txt"; }
}

void logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
std::string fmtTime(double s);
bool mkdirs(const std::string &path);
bool fileExists(const std::string &path);
int64_t nowSecs();
