#include "common.hpp"
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <sys/stat.h>

#ifndef __SWITCH__
std::string Paths::root() { return "./ytmusic-data"; }
#endif

void logf(const char *fmt, ...) {
    static std::mutex mu;
    std::lock_guard<std::mutex> l(mu);
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    printf("%s\n", buf);
    static FILE *f = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        mkdirs(Paths::root());
        f = fopen(Paths::log().c_str(), "w");
    }
    if (f) { fprintf(f, "[%lld] %s\n", (long long)time(nullptr), buf); fflush(f); }
}

std::string fmtTime(double s) {
    if (!(s >= 0)) s = 0;
    const int t = int(s), h = t / 3600, m = (t % 3600) / 60, sec = t % 60;
    char b[32];
    if (h > 0) snprintf(b, sizeof b, "%d:%02d:%02d", h, m, sec);
    else snprintf(b, sizeof b, "%d:%02d", m, sec);
    return b;
}

bool mkdirs(const std::string &path) {
    for (size_t i = 1; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            const std::string p = path.substr(0, i);
            if (p.empty() || p.back() == ':') continue;   // "sdmc:"
            mkdir(p.c_str(), 0777);
        }
    }
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

bool fileExists(const std::string &path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

int64_t nowSecs() { return int64_t(time(nullptr)); }
