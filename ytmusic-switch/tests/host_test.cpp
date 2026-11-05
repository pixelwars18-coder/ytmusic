// Runs the Switch app's core (YouTube client, audio engine, downloads) on Linux, against the real YouTube.
// CI uses it to check the parts that can't be tested without a console.
#include "../source/yt.hpp"
#include "../source/net.hpp"
#include "../source/player.hpp"
#include "../source/downloads.hpp"
#include <SDL2/SDL.h>
#include <chrono>
#include <cstdio>
#include <thread>
using namespace std::chrono_literals;

static const char *stName(Player::State s) {
    static const char *n[] = {"Idle", "Loading", "Playing", "Paused", "Ended", "Error"};
    return n[s];
}

static bool waitPlaying(Player &p, int secs) {
    for (int i = 0; i < secs * 10; ++i) {
        if (p.state() == Player::Playing) return true;
        if (p.state() == Player::Error) return false;
        std::this_thread::sleep_for(100ms);
    }
    return false;
}

int main() {
    int fails = 0;
    auto check = [&](bool ok, const char *what) { printf("%s %s\n", ok ? "PASS" : "FAIL", what); if (!ok) ++fails; };
    Net::init();
    SDL_Init(SDL_INIT_AUDIO);

    auto t0 = std::chrono::steady_clock::now();
    auto ms = [&] { return (long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count(); };

    YT::SearchPage pg = YT::search("rick astley never gonna give you up");
    printf("search: %zu results, err='%s', cont=%d, %ld ms\n", pg.tracks.size(), pg.error.c_str(), !pg.continuation.empty(), ms());
    for (size_t i = 0; i < pg.tracks.size() && i < 3; ++i)
        printf("  %s | %s | %s | %s\n", pg.tracks[i].vid.c_str(), pg.tracks[i].title.c_str(), pg.tracks[i].artist.c_str(), fmtTime(pg.tracks[i].duration).c_str());
    check(pg.tracks.size() >= 5, "search returns songs");
    if (!pg.continuation.empty()) {
        YT::SearchPage more = YT::searchMore(pg.continuation);
        printf("more: %zu results err='%s'\n", more.tracks.size(), more.error.c_str());
        check(!more.tracks.empty(), "load more");
    }
    const std::string vid = pg.tracks.empty() ? "dQw4w9WgXcQ" : pg.tracks[0].vid;

    t0 = std::chrono::steady_clock::now();
    YT::Stream s = YT::resolve(vid);
    printf("resolve %s: client=%s itag=%d mime=%s len=%lld exp=%lld err='%s' %ld ms\n", vid.c_str(), s.client.c_str(), s.itag, s.mime.c_str(),
           (long long)s.contentLength, (long long)s.expires, s.error.c_str(), ms());
    check(s.ok(), "resolve stream URL");

    Player p;
    check(p.init(), "audio device");
    if (s.ok()) {
        t0 = std::chrono::steady_clock::now();
        p.openUrl(s.url, s.contentLength, s.userAgent);
        const bool playing = waitPlaying(p, 30);
        printf("stream start: %s after %ld ms (err '%s') dur=%.1f\n", stName(p.state()), ms(), p.error().c_str(), p.duration());
        check(playing, "stream plays");
        std::this_thread::sleep_for(3s);
        printf("pos after 3s: %.2f buffered %.2f\n", p.position(), p.buffered());
        check(p.position() > 1.5, "position advances");
        p.seek(60);
        std::this_thread::sleep_for(2s);
        printf("pos after seek 60: %.2f\n", p.position());
        check(p.position() > 59 && p.position() < 70, "seek");
        p.setPaused(true);
        const double pp = p.position();
        std::this_thread::sleep_for(1s);
        check(p.state() == Player::Paused && std::abs(p.position() - pp) < 0.3, "pause holds position");
        p.stop();
    }

    YT::SearchPage r;
    auto rad = YT::radio(vid);
    printf("radio: %zu tracks\n", rad.size());
    check(rad.size() >= 5, "radio mix");

    Downloads d;
    d.start();
    Track tr = pg.tracks.empty() ? Track{vid, "t", "a", 0} : pg.tracks[0];   // CI IPs get bot checks on some videos
    d.add({tr});
    t0 = std::chrono::steady_clock::now();
    while (d.pending() > 0 && ms() < 90000) std::this_thread::sleep_for(200ms);
    for (auto &m : d.takeMessages()) printf("dl msg: %s\n", m.c_str());
    const std::string path = d.pathFor(tr.vid);
    FILE *f = path.empty() ? nullptr : fopen(path.c_str(), "rb");
    long sz = 0;
    if (f) { fseek(f, 0, SEEK_END); sz = ftell(f); fclose(f); }
    printf("download: %s %ld bytes in %ld ms\n", path.c_str(), sz, ms());
    check(sz > 100000, "download to file");
    if (sz > 0) {
        p.openFile(path);
        check(waitPlaying(p, 10), "local file plays");
        std::this_thread::sleep_for(1500ms);
        printf("local pos %.2f dur %.1f\n", p.position(), p.duration());
        p.stop();
    }
    d.stop();
    p.shutdown();
    printf("%s: %d failure(s)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
