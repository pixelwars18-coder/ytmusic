#pragma once
#include "common.hpp"
#include "downloads.hpp"
#include "gfx.hpp"
#include "library.hpp"
#include "player.hpp"
#include "yt.hpp"
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <memory>
#include <atomic>

enum Btn : uint32_t {
    B_A = 1 << 0, B_B = 1 << 1, B_X = 1 << 2, B_Y = 1 << 3, B_L = 1 << 4, B_R = 1 << 5, B_ZL = 1 << 6, B_ZR = 1 << 7,
    B_PLUS = 1 << 8, B_MINUS = 1 << 9, B_UP = 1 << 10, B_DOWN = 1 << 11, B_LEFT = 1 << 12, B_RIGHT = 1 << 13,
};

struct Input {
    uint32_t down = 0, held = 0;
    float lx = 0, ly = 0, rx = 0, ry = 0;   // sticks, -1..1 (y up = positive)
    bool touching = false;
    float tx = 0, ty = 0;                   // 1280x720 coordinates
    float dt = 1 / 60.f;
};

class App {
public:
    bool init(SDL_Renderer *r, const std::vector<Gfx::FontBlob> &fonts);
    bool frame(const Input &in);   // false: quit
    void render();
    void shutdown();
    // platform hook: on-screen keyboard. Returns false if cancelled.
    std::function<bool(const std::string &guide, const std::string &initial, std::string &out)> keyboard;

    // dev/test hooks
    void testSearch(const std::string &q) { runSearch(q); }
    void testView(int v) { view = View(v); }
    void testPlay(const Track &t) { playTracks({t}, 0, false); }
    int testState() const { return int(player.state()); }
    double testPos() const { return player.position(); }

private:
    enum class View { Search, Queue, Liked, Playlists, Downloads, Settings, Count };
    struct Row {
        enum Kind { Song, Query, PlaylistRow, NewPlaylist, Action } kind = Song;
        Track t;
        std::string label, sub, id;
        int index = -1;   // index in the underlying list
    };
    struct MenuItem { std::string label; std::function<void()> fn; bool danger = false; };

    // async
    void async(std::function<void()> job);
    void post(std::function<void()> fn);
    std::mutex postMu;
    std::vector<std::function<void()>> posted;
    struct Job { std::thread th; std::shared_ptr<std::atomic<bool>> done; };
    std::vector<Job> jobs;

    // logic
    std::vector<Row> rows();
    const std::vector<Track> *currentTracks();
    void activate(const Row &r);
    void openMenu(const Row *r);
    void openAddToPlaylist(const std::vector<Track> &tracks);
    void runSearch(const std::string &q);
    void loadMore();
    void askSearch();
    void playTracks(const std::vector<Track> &list, int index, bool shuffle);
    void playIndex(int i);
    void next(bool automatic);
    void prev();
    int pickNext(bool automatic);
    void startRadio(const Track &t);
    void enqueue(const Track &t, bool next);
    void preloadNext();
    void toast(const std::string &s);
    void switchView(int dir);
    void ensureVisible();

    // drawing
    void drawSidebar();
    void drawHeader();
    void drawList();
    void drawPlayerBar();
    void drawMenu();
    void drawToast();
    void drawRowSong(const Row &r, float y, bool sel);

    Gfx g;
    Thumbs thumbs;
    Player player;
    Library lib;
    Downloads dl;

    View view = View::Search;
    int sel[int(View::Count)] = {};
    float scroll[int(View::Count)] = {};
    std::string openPlaylist;   // Playlists view: opened playlist id
    int playlistsSel = 0;       // selection to return to

    // search
    std::vector<Track> results;
    std::string query, continuation, searchStatus;
    bool searching = false, loadingMore = false;
    int searchGen = 0;

    // playback
    int loadGen = 0;
    int errorStreak = 0;
    int nextPick = -1;
    bool loadingTrack = false;
    struct Cached { YT::Stream s; };
    std::map<std::string, YT::Stream> urlCache;
    std::string preloading;

    // ui
    std::vector<MenuItem> menu;
    std::string menuTitle;
    int menuSel = 0, menuTop = 0;
    std::string toastText;
    float toastT = 0;
    float repeatT = 0;
    uint32_t lastDir = 0;
    float plusHeld = 0;
    bool touchWas = false, dragging = false;
    float touchStartX = 0, touchStartY = 0, touchLastY = 0;
    float seekAccum = 0, volAccum = 0;
    float saveT = 0;
    std::vector<Track> tmpList;   // backing store for computed lists (downloads)
    bool quitting = false;
};
