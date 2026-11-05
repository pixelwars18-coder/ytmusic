#include "app.hpp"
#include <algorithm>
#include <cmath>
#include <random>
#include <ctime>

namespace {
constexpr float W = 1280, H = 720;
// Console home-menu layout: top bar, a row of square tiles, the round section buttons, a hint bar.
constexpr float kTopH = 84;
constexpr float kTileY = 158, kTile = 256, kGap = 22, kTileX0 = 64;
constexpr float kTabY = 478, kTabH = 96, kTabBtn = 70, kTabStep = 100;
constexpr float kHintY = 652;
constexpr float kListX = 64, kListW = W - 128, kListY = 112, kRowH = 66, kListH = 440 - 112;   // Settings list
constexpr int kMenuVisible = 9;
const char *kViewNames[] = {"Search", "Queue", "Liked", "Playlists", "Downloads", "Settings"};
const char *kViewIcons[] = {"search", "queue", "heart", "list", "download", "gear"};
std::mt19937 &rng() { static std::mt19937 r{std::random_device{}()}; return r; }
std::string songs(size_t n) { return std::to_string(n) + (n == 1 ? " song" : " songs"); }
float tabX(int i) { return W / 2 - (int(5) * kTabStep) / 2 + i * kTabStep; }   // centre of section button i
bool fresh(const YT::Stream &s) { return s.ok() && (s.expires == 0 || s.expires - nowSecs() > 600); }
}

bool App::init(SDL_Renderer *r, const std::vector<Gfx::FontBlob> &fonts) {
    if (!g.init(r, fonts)) return false;
    thumbs.start(r);
    mkdirs(Paths::root());
    lib.load();
    setDarkTheme(lib.dark);
    player.init();
    player.setVolume(lib.volume);
    dl.start();
    if (!lib.queue.empty()) view = View::Queue;
    tabSel = int(view);
    return true;
}

void App::shutdown() {
    quitting = true;
    lib.volume = player.volume();
    lib.save();
    player.shutdown();
    dl.stop();
    thumbs.stop();
    for (auto &j : jobs) if (j.th.joinable()) j.th.join();
    g.shutdown();
}

void App::async(std::function<void()> job) {
    // finished threads are joined lazily (never blocks: only ones that are done)
    for (auto it = jobs.begin(); it != jobs.end();) {
        if (it->done->load()) { it->th.join(); it = jobs.erase(it); }
        else ++it;
    }
    auto done = std::make_shared<std::atomic<bool>>(false);
    jobs.push_back({std::thread([job = std::move(job), done] { job(); done->store(true); }), done});
}

void App::post(std::function<void()> fn) {
    std::lock_guard<std::mutex> l(postMu);
    posted.push_back(std::move(fn));
}

void App::toast(const std::string &s) {
    toastText = s;
    toastT = 3.0f;
}

// ------------------------------------------------------------------ lists
const std::vector<Track> *App::currentTracks() {
    switch (view) {
    case View::Search: return results.empty() ? nullptr : &results;
    case View::Queue: return &lib.queue;
    case View::Liked: return &lib.liked;
    case View::Playlists: if (Playlist *p = lib.find(openPlaylist)) return &p->tracks; return nullptr;
    case View::Downloads: tmpList = dl.list(); return &tmpList;
    default: return nullptr;
    }
}

std::vector<App::Row> App::rows() {
    std::vector<Row> v;
    if (view == View::Settings) {
        auto act = [&](const std::string &l, const std::string &s, const std::string &id) { Row r; r.kind = Row::Action; r.label = l; r.sub = s; r.id = id; v.push_back(r); };
        act("Import library backup", "Reads " + Paths::backup() + " (export it from the PC or Android app)", "import");
        act("Export library backup", "Writes your likes and playlists to " + Paths::backup(), "export");
        act("Clear recent searches", std::to_string(lib.history.size()) + " saved", "clearhist");
        act("Shuffle: " + std::string(lib.shuffle ? "on" : "off"), "Play the queue in random order", "shuffle");
        act(std::string("Repeat: ") + (lib.repeat == 0 ? "off" : lib.repeat == 1 ? "all" : "one"), "What happens at the end of the queue", "repeat");
        act(std::string("Theme: ") + (lib.dark ? "Basic Dark" : "Basic Light"), "Switch between the dark and light look", "theme");
        act("Controls", "A play · X like · Y search · \xE2\x88\x92 menu · + play/pause (hold: exit) · ZL/ZR prev/next · R-stick seek/volume", "help");
        act("Exit", "Saves and closes YT Music", "exit");
        return v;
    }
    if (view == View::Search && results.empty()) {
        for (auto &h : lib.history) { Row r; r.kind = Row::Query; r.label = h; v.push_back(r); }
        return v;
    }
    if (view == View::Playlists && openPlaylist.empty()) {
        Row n; n.kind = Row::NewPlaylist; n.label = "New playlist"; n.sub = "Create an empty playlist"; v.push_back(n);
        for (size_t i = 0; i < lib.playlists.size(); ++i) {
            const Playlist &p = lib.playlists[i];
            Row r; r.kind = Row::PlaylistRow; r.label = p.name; r.id = p.id; r.index = int(i);
            r.sub = std::to_string(p.tracks.size()) + (p.tracks.size() == 1 ? " song" : " songs");
            if (!p.tracks.empty()) r.t = p.tracks.front();
            v.push_back(r);
        }
        return v;
    }
    if (const std::vector<Track> *l = currentTracks())
        for (size_t i = 0; i < l->size(); ++i) { Row r; r.t = (*l)[i]; r.index = int(i); v.push_back(r); }
    return v;
}

// ---------------------------------------------------------------- search
void App::askSearch() {
    std::string q;
    if (!keyboard || !keyboard("Search YouTube or paste a link", "", q)) return;
    runSearch(q);
}

void App::runSearch(const std::string &q0) {
    std::string q = q0;
    while (!q.empty() && q.back() == ' ') q.pop_back();
    while (!q.empty() && q.front() == ' ') q.erase(q.begin());
    if (q.empty()) return;
    view = View::Search;
    const std::string vid = YT::videoIdFromText(q);
    if (!vid.empty() && q.find("http") != std::string::npos) {   // pasted link: play it right away
        toast("Opening link\xE2\x80\xA6");
        const int gen = ++loadGen;
        async([this, vid, gen] {
            YT::Stream s = YT::resolve(vid);
            post([this, s, gen] {
                if (gen != loadGen) return;
                if (!s.ok()) { toast("Couldn't open: " + s.error); return; }
                urlCache[s.meta.vid] = s;
                playTracks({s.meta}, 0, false);
            });
        });
        return;
    }
    lib.addHistory(q);
    query = q;
    results.clear();
    continuation.clear();
    searchStatus = "Searching\xE2\x80\xA6";
    searching = true;
    sel[int(View::Search)] = 0;
    scroll[int(View::Search)] = 0;
    const int gen = ++searchGen;
    async([this, q, gen] {
        YT::SearchPage pg = YT::search(q);
        post([this, pg, gen] {
            if (gen != searchGen) return;
            searching = false;
            results = pg.tracks;
            continuation = pg.continuation;
            searchStatus = results.empty() ? (pg.error.empty() ? "No results" : pg.error) : "";
        });
    });
}

void App::loadMore() {
    if (continuation.empty() || loadingMore || searching) return;
    loadingMore = true;
    const std::string c = continuation;
    const int gen = searchGen;
    async([this, c, gen] {
        YT::SearchPage pg = YT::searchMore(c);
        post([this, pg, gen] {
            loadingMore = false;
            if (gen != searchGen) return;
            for (auto &t : pg.tracks)
                if (std::find(results.begin(), results.end(), t) == results.end()) results.push_back(t);
            continuation = pg.continuation;
        });
    });
}

// -------------------------------------------------------------- playback
void App::playTracks(const std::vector<Track> &list, int index, bool shuffle) {
    if (list.empty()) return;
    lib.queue = list;
    if (shuffle) {
        lib.shuffle = true;
        index = int(rng()() % list.size());
    }
    lib.markDirty();
    playIndex(std::clamp(index, 0, int(list.size()) - 1));
}

void App::playIndex(int i) {
    if (i < 0 || i >= int(lib.queue.size())) return;
    lib.current = i;
    nextPick = -1;
    lib.markDirty();
    const Track t = lib.queue[size_t(i)];
    const int gen = ++loadGen;
    const std::string local = dl.pathFor(t.vid);
    if (!local.empty()) { player.openFile(local); loadingTrack = false; return; }
    auto it = urlCache.find(t.vid);
    if (it != urlCache.end() && fresh(it->second)) {
        player.openUrl(it->second.url, it->second.contentLength, it->second.userAgent);
        loadingTrack = false;
        return;
    }
    player.stop();
    loadingTrack = true;
    async([this, t, gen] {
        YT::Stream s = YT::resolve(t.vid);
        post([this, s, t, gen] {
            if (gen != loadGen) return;
            loadingTrack = false;
            if (!s.ok()) {
                toast("Can't play \"" + t.title.substr(0, 40) + "\": " + s.error.substr(0, 60));
                if (++errorStreak < 5 && int(lib.queue.size()) > 1) next(true);
                return;
            }
            urlCache[t.vid] = s;
            player.openUrl(s.url, s.contentLength, s.userAgent);
        });
    });
}

int App::pickNext(bool automatic) {
    const int n = int(lib.queue.size());
    if (n == 0) return -1;
    if (automatic && lib.repeat == 2) return lib.current;
    if (lib.shuffle && n > 1) {
        if (nextPick >= 0 && nextPick < n && nextPick != lib.current) return nextPick;
        int j;
        do { j = int(rng()() % unsigned(n)); } while (j == lib.current);
        return j;
    }
    if (lib.current + 1 < n) return lib.current + 1;
    if (lib.repeat == 1 || !automatic) return 0;
    return -1;
}

void App::next(bool automatic) {
    const int j = pickNext(automatic);
    if (j < 0) { player.stop(); return; }
    playIndex(j);
}

void App::prev() {
    if (player.position() > 5 || lib.current <= 0) player.seek(0);
    else playIndex(lib.current - 1);
}

void App::preloadNext() {
    if (lib.queue.empty() || !preloading.empty()) return;
    nextPick = pickNext(true);
    if (nextPick < 0 || nextPick >= int(lib.queue.size())) return;
    const std::string vid = lib.queue[size_t(nextPick)].vid;
    auto it = urlCache.find(vid);
    if ((it != urlCache.end() && fresh(it->second)) || dl.isDownloaded(vid)) return;
    preloading = vid;
    async([this, vid] {
        YT::Stream s = YT::resolve(vid);
        post([this, s, vid] {
            preloading.clear();
            if (s.ok()) urlCache[vid] = s;
        });
    });
}

void App::enqueue(const Track &t, bool asNext) {
    if (lib.current < 0 || lib.queue.empty()) { lib.queue.push_back(t); playIndex(int(lib.queue.size()) - 1); return; }
    if (asNext) lib.queue.insert(lib.queue.begin() + lib.current + 1, t);
    else lib.queue.push_back(t);
    nextPick = -1;
    lib.markDirty();
    toast(asNext ? "Playing next" : "Added to the queue");
}

void App::startRadio(const Track &t) {
    toast("Starting radio\xE2\x80\xA6");
    async([this, t] {
        auto list = YT::radio(t.vid);
        post([this, list, t] {
            std::vector<Track> all{t};
            for (auto &x : list) if (x.vid != t.vid) all.push_back(x);
            if (all.size() < 2) { toast("No similar songs found"); return; }
            playTracks(all, 0, false);
            view = View::Queue;
            sel[int(View::Queue)] = 0;
        });
    });
}

// ------------------------------------------------------------------ menus
void App::openAddToPlaylist(const std::vector<Track> &tracks) {
    menu.clear();
    menuTitle = "Add to playlist";
    menu.push_back({"New playlist\xE2\x80\xA6", [this, tracks] {
        std::string name;
        if (keyboard && keyboard("Playlist name", "", name)) {
            lib.createPlaylist(name, tracks);
            toast("Playlist created");
        }
    }});
    for (auto &p : lib.playlists) {
        const std::string id = p.id, name = p.name;
        menu.push_back({name, [this, id, name, tracks] {
            const int n = lib.addToPlaylist(id, tracks);
            toast(n ? "Added to \"" + name + "\"" : "Already in \"" + name + "\"");
        }});
    }
    menuSel = 0;
    menuTop = 0;
}

void App::openMenu(const Row *r) {
    menu.clear();
    menuSel = menuTop = 0;
    const std::vector<Track> *list = currentTracks();
    if (r && r->kind == Row::Song) {
        const Track t = r->t;
        const int idx = r->index;
        menuTitle = t.title;
        menu.push_back({"Play next", [this, t] { enqueue(t, true); }});
        menu.push_back({"Add to queue", [this, t] { enqueue(t, false); }});
        menu.push_back({"Start radio", [this, t] { startRadio(t); }});
        menu.push_back({"Add to playlist\xE2\x80\xA6", [this, t] { openAddToPlaylist({t}); }});
        menu.push_back({lib.isLiked(t.vid) ? "Remove from liked" : "Add to liked", [this, t] { lib.toggleLike(t); }});
        if (dl.isActive(t.vid)) menu.push_back({"Cancel download", [this, t] { dl.cancel(t.vid); toast("Download cancelled"); }});
        else if (dl.isDownloaded(t.vid)) menu.push_back({"Delete download", [this, t] { dl.remove(t.vid); toast("Download deleted"); }, true});
        else menu.push_back({"Download to SD card", [this, t] { dl.add({t}); toast("Downloading\xE2\x80\xA6"); }});
        if (view == View::Queue)
            menu.push_back({"Remove from queue", [this, idx] {
                if (idx < 0 || idx >= int(lib.queue.size())) return;
                lib.queue.erase(lib.queue.begin() + idx);
                if (idx < lib.current) --lib.current;
                else if (idx == lib.current) { player.stop(); lib.current = -1; }
                nextPick = -1;
                lib.markDirty();
            }, true});
        if (view == View::Playlists && !openPlaylist.empty())
            menu.push_back({"Remove from this playlist", [this, idx] {
                if (Playlist *p = lib.find(openPlaylist)) if (idx >= 0 && idx < int(p->tracks.size())) { p->tracks.erase(p->tracks.begin() + idx); lib.markDirty(); }
            }, true});
    } else if (r && r->kind == Row::PlaylistRow) {
        const std::string id = r->id;
        menuTitle = r->label;
        menu.push_back({"Play", [this, id] { if (Playlist *p = lib.find(id)) playTracks(p->tracks, 0, false); }});
        menu.push_back({"Shuffle play", [this, id] { if (Playlist *p = lib.find(id)) playTracks(p->tracks, 0, true); }});
        menu.push_back({"Download all", [this, id] { if (Playlist *p = lib.find(id)) { int n = dl.add(p->tracks); toast("Downloading " + std::to_string(n) + " songs"); } }});
        menu.push_back({"Rename", [this, id] {
            Playlist *p = lib.find(id);
            std::string name;
            if (p && keyboard && keyboard("Playlist name", p->name, name) && !name.empty()) { p->name = name; lib.markDirty(); }
        }});
        menu.push_back({"Delete playlist", [this, id] { lib.deletePlaylist(id); toast("Playlist deleted"); }, true});
    } else if (r && r->kind == Row::Query) {
        const std::string q = r->label;
        menuTitle = q;
        menu.push_back({"Search", [this, q] { runSearch(q); }});
        menu.push_back({"Remove from history", [this, q] {
            lib.history.erase(std::remove(lib.history.begin(), lib.history.end(), q), lib.history.end());
            lib.markDirty();
        }, true});
    } else {
        menuTitle = kViewNames[int(view)];
    }
    // list-wide actions
    if (list && !list->empty() && (!r || r->kind == Row::Song)) {
        const std::vector<Track> all = *list;
        menu.push_back({"Play all", [this, all] { playTracks(all, 0, false); }});
        menu.push_back({"Shuffle all", [this, all] { playTracks(all, 0, true); }});
        if (view != View::Queue) menu.push_back({"Add all to queue", [this, all] { for (auto &t : all) lib.queue.push_back(t); lib.markDirty(); toast("Added " + std::to_string(all.size()) + " songs"); }});
        if (view != View::Downloads) menu.push_back({"Download all", [this, all] { int n = dl.add(all); toast(n ? "Downloading " + std::to_string(n) + " songs" : "Already downloaded"); }});
        if (view == View::Liked || view == View::Queue) menu.push_back({"Save as playlist\xE2\x80\xA6", [this, all] { openAddToPlaylist(all); }});
        if (view == View::Queue) menu.push_back({"Clear queue", [this] { player.stop(); lib.queue.clear(); lib.current = -1; lib.markDirty(); }, true});
    }
    if (menu.empty()) menuTitle.clear();
}

void App::activate(const Row &r) {
    switch (r.kind) {
    case Row::Query: runSearch(r.label); return;
    case Row::NewPlaylist: {
        std::string name;
        if (keyboard && keyboard("Playlist name", "", name)) { lib.createPlaylist(name); toast("Playlist created"); }
        return;
    }
    case Row::PlaylistRow:
        playlistsSel = sel[int(View::Playlists)];
        openPlaylist = r.id;
        sel[int(View::Playlists)] = 0;
        scroll[int(View::Playlists)] = 0;
        return;
    case Row::Action:
        if (r.id == "import") { bool ok; toast(lib.importBackup(Paths::backup(), &ok)); }
        else if (r.id == "export") { const std::string e = lib.exportBackup(Paths::backup()); toast(e.empty() ? "Saved " + Paths::backup() : e); }
        else if (r.id == "clearhist") { lib.history.clear(); lib.markDirty(); toast("Search history cleared"); }
        else if (r.id == "shuffle") { lib.shuffle = !lib.shuffle; nextPick = -1; lib.markDirty(); }
        else if (r.id == "repeat") { lib.repeat = (lib.repeat + 1) % 3; lib.markDirty(); }
        else if (r.id == "exit") quitting = true;
        else if (r.id == "theme") { lib.dark = !lib.dark; setDarkTheme(lib.dark); lib.markDirty(); }
        return;
    case Row::Song:
        if (view == View::Queue) playIndex(r.index);
        else if (const std::vector<Track> *l = currentTracks()) playTracks(*l, r.index, false);
        return;
    }
}

void App::switchView(int dir) {
    int v = (int(view) + dir + int(View::Count)) % int(View::Count);
    view = View(v);
    tabSel = v;
}

// keeps the selection on screen: vertical list (Settings) or the horizontal tile row
void App::ensureVisible() {
    const int v = int(view);
    if (view == View::Settings) {
        const float y = sel[v] * kRowH;
        if (y < scroll[v]) scroll[v] = y;
        if (y + kRowH > scroll[v] + kListH) scroll[v] = y + kRowH - kListH;
        if (scroll[v] < 0) scroll[v] = 0;
        return;
    }
    const float step = kTile + kGap;
    const float x = sel[v] * step;
    const float visible = W - 2 * kTileX0;
    // keep one tile of context on each side, like the home menu
    if (x - step < scroll[v]) scroll[v] = std::max(0.f, x - step);
    if (x + step * 2 > scroll[v] + visible) scroll[v] = x + step * 2 - visible;
    if (scroll[v] < 0) scroll[v] = 0;
}

// ------------------------------------------------------------------ frame
bool App::frame(const Input &in) {
    {   // results from background jobs
        std::vector<std::function<void()>> todo;
        { std::lock_guard<std::mutex> l(postMu); todo.swap(posted); }
        for (auto &f : todo) f();
    }
    thumbs.pump();
    for (auto &m : dl.takeMessages()) toast(m);
    if (toastT > 0) toastT -= in.dt;
    clock += in.dt;

    // player housekeeping
    const Player::State ps = player.state();
    if (ps == Player::Ended) next(true);
    else if (ps == Player::Error) {
        toast("Playback error: " + player.error());
        player.stop();
        if (lib.current >= 0) urlCache.erase(lib.queue[size_t(lib.current)].vid);
        if (++errorStreak < 5) next(true);
    } else if (ps == Player::Playing) {
        if (player.position() > 3) { errorStreak = 0; preloadNext(); }
    }
    saveT += in.dt;
    if (saveT > 5) { saveT = 0; lib.volume = player.volume(); lib.saveIfDirty(); }

    auto rs = rows();
    const int v = int(view);
    int &s = sel[v];
    s = std::clamp(s, 0, std::max(0, int(rs.size()) - 1));
    const bool listView = view == View::Settings;

    // ---------------- menu overlay
    if (!menu.empty()) {
        if (in.down & (B_B | B_MINUS)) { menu.clear(); return !quitting; }
        if (in.down & B_UP || (in.down & B_DOWN)) menuSel = std::clamp(menuSel + ((in.down & B_DOWN) ? 1 : -1), 0, int(menu.size()) - 1);
        const int vis = std::min(int(menu.size()), kMenuVisible);
        if (menuSel < menuTop) menuTop = menuSel;
        if (menuSel >= menuTop + vis) menuTop = menuSel - vis + 1;
        const float mh = 84 + vis * 54.f, mw = 620;
        const float mx = (W - mw) / 2, my = (H - mh) / 2;
        bool fire = (in.down & B_A) != 0;
        if (in.touching && !touchWas) {
            const int i = menuTop + int((in.ty - my - 72) / 54);
            if (in.tx >= mx && in.tx <= mx + mw && in.ty > my + 72 && i >= 0 && i < int(menu.size())) { menuSel = i; fire = true; }
            else if (in.tx < mx || in.tx > mx + mw || in.ty < my || in.ty > my + mh) menu.clear();
        }
        touchWas = in.touching;
        if (fire && menuSel < int(menu.size())) {
            auto fn = menu[size_t(menuSel)].fn;
            menu.clear();
            menuTop = 0;
            fn();   // may open a new menu (add to playlist)
        }
        return !quitting;
    }

    // ---------------- directional input with key repeat (d-pad or left stick)
    uint32_t dir = 0;
    if ((in.held & B_UP) || in.ly > 0.55f) dir = B_UP;
    else if ((in.held & B_DOWN) || in.ly < -0.55f) dir = B_DOWN;
    else if ((in.held & B_LEFT) || in.lx < -0.55f) dir = B_LEFT;
    else if ((in.held & B_RIGHT) || in.lx > 0.55f) dir = B_RIGHT;
    uint32_t step = 0;   // a direction "press" this frame (first press + repeats)
    if (dir) {
        if (dir != lastDir) { repeatT = 0.36f; lastDir = dir; step = dir; }
        else if ((repeatT -= in.dt) <= 0) { repeatT = 0.075f; step = dir; }
    } else lastDir = 0;

    const int focusAtStart = focus;   // a button that moves focus shouldn't also act on the new target
    if (focus == 1) {   // section buttons
        if (step == B_LEFT && tabSel > 0) { --tabSel; view = View(tabSel); }
        if (step == B_RIGHT && tabSel < int(View::Count) - 1) { ++tabSel; view = View(tabSel); }
        if (step == B_UP || (in.down & (B_A | B_B))) focus = 0;
    } else if (listView) {
        if (step == B_UP) s = std::max(0, s - 1);
        if (step == B_DOWN) { if (s + 1 >= int(rs.size())) { focus = 1; tabSel = v; } else ++s; }
        ensureVisible();
    } else {
        if (step == B_LEFT) s = std::max(0, s - 1);
        if (step == B_RIGHT) s = std::min(std::max(0, int(rs.size()) - 1), s + 1);
        if (step == B_DOWN) { focus = 1; tabSel = v; }
        if (step & (B_LEFT | B_RIGHT)) ensureVisible();
    }

    if (in.down & B_L) { switchView(-1); }
    if (in.down & B_R) { switchView(1); }
    if (in.down & B_Y) askSearch();
    if (in.down & B_ZL) prev();
    if (in.down & B_ZR) next(false);
    if (in.held & B_PLUS) {
        plusHeld += in.dt;
        if (plusHeld > 1.2f) quitting = true;
    } else {
        if (plusHeld > 0 && plusHeld < 1.2f) {   // released: play/pause
            if (player.state() == Player::Idle && lib.current >= 0) playIndex(lib.current);
            else if (player.state() == Player::Idle && !lib.queue.empty()) playIndex(0);
            else player.togglePause();
        }
        plusHeld = 0;
    }
    // right stick: seek (x) and volume (y)
    if (std::fabs(in.rx) > 0.5f) {
        seekAccum += in.dt;
        if (seekAccum > 0.18f) { seekAccum = 0; player.seek(player.position() + (in.rx > 0 ? 10 : -10)); }
    } else seekAccum = 0.18f;
    if (std::fabs(in.ry) > 0.5f) {
        volAccum += in.dt;
        if (volAccum > 0.08f) { volAccum = 0; player.setVolume(player.volume() + (in.ry > 0 ? 2 : -2)); toastVol = 1.2f; }
    } else volAccum = 0.08f;
    if (toastVol > 0) toastVol -= in.dt;

    if (focusAtStart == 0 && (in.down & B_B)) {
        if (view == View::Playlists && !openPlaylist.empty()) { openPlaylist.clear(); sel[v] = playlistsSel; ensureVisible(); }
        else if (view == View::Search && !results.empty()) { results.clear(); searchStatus.clear(); ++searchGen; searching = false; }
    }
    rs = rows();
    s = std::clamp(s, 0, std::max(0, int(rs.size()) - 1));
    if (focusAtStart == 0) {
        if (!rs.empty()) {
            const Row &r = rs[size_t(s)];
            if (in.down & B_A) activate(r);
            else if ((in.down & B_X) && (r.kind == Row::Song)) { lib.toggleLike(r.t); toast(lib.isLiked(r.t.vid) ? "Added to liked" : "Removed from liked"); }
            else if (in.down & B_MINUS) openMenu(&r);
        } else if (in.down & B_MINUS) openMenu(nullptr);
    } else if (in.down & B_MINUS) openMenu(nullptr);

    // ---------------- touch: tap to select / tap again to open, drag the tile row
    if (in.touching && !touchWas) { touchStartX = touchLastX = in.tx; touchStartY = touchLastY = in.ty; dragging = false; }
    if (in.touching && touchWas) {
        const bool inRow = listView ? (touchStartY > kListY && touchStartY < kListY + kListH) : (touchStartY > kTileY - 30 && touchStartY < kTileY + kTile + 40);
        if (inRow && (listView ? std::fabs(in.ty - touchStartY) : std::fabs(in.tx - touchStartX)) > 14) dragging = true;
        if (dragging) {
            if (listView) scroll[v] = std::clamp(scroll[v] - (in.ty - touchLastY), 0.f, std::max(0.f, rs.size() * kRowH - kListH));
            else scroll[v] = std::clamp(scroll[v] - (in.tx - touchLastX), 0.f, std::max(0.f, rs.size() * (kTile + kGap) - (W - 2 * kTileX0)));
            scrollShown[v] = scroll[v];
            touchLastX = in.tx;
            touchLastY = in.ty;
        }
    }
    if (!in.touching && touchWas && !dragging) {   // tap
        const float x = touchStartX, y = touchStartY;
        int hitTab = -1;
        for (int i = 0; i < int(View::Count); ++i)
            if (std::hypot(x - tabX(i), y - (kTabY + kTabH / 2)) < kTabBtn / 2 + 6) hitTab = i;
        if (hitTab >= 0) { view = View(hitTab); tabSel = hitTab; focus = 0; }
        else if (listView && y > kListY && y < kListY + kListH && x > kListX && x < kListX + kListW) {
            const int i = int((y - kListY + scroll[v]) / kRowH);
            if (i >= 0 && i < int(rs.size())) { focus = 0; if (i == s) activate(rs[size_t(i)]); else s = i; }
        } else if (!listView && y > kTileY && y < kTileY + kTile) {
            const int i = int((x - kTileX0 + scrollShown[v]) / (kTile + kGap));
            if (i >= 0 && i < int(rs.size())) { focus = 0; if (i == s) activate(rs[size_t(i)]); else s = i; }
        } else if (y > kHintY) {
            if (x < 520) player.togglePause();
            else if (x > W - 170) askSearch();
        }
    }
    touchWas = in.touching;

    // infinite scroll for search results
    if (view == View::Search && !results.empty() && s >= int(results.size()) - 5) loadMore();

    // smooth horizontal scrolling
    for (int i = 0; i < int(View::Count); ++i) {
        const float d = scroll[i] - scrollShown[i];
        scrollShown[i] = std::fabs(d) < 0.5f ? scroll[i] : scrollShown[i] + d * std::min(1.f, in.dt * 14);
    }
    g.endFrame();
    return !quitting;
}

// ---------------------------------------------------------------- drawing
void App::render() {
    const Theme &t = TH();
    SDL_SetRenderDrawColor(g.r, t.bg.r, t.bg.g, t.bg.b, 255);
    SDL_RenderClear(g.r);
    drawTopBar();
    if (view == View::Settings) drawList();
    else drawTiles();
    drawTabs();
    drawHints();
    drawToast();
    drawMenu();
}

void App::drawTopBar() {
    const Theme &t = TH();
    // "user" badge: the app's own round icon
    g.fillCircle(66, 46, 27, t.accent);
    g.icon("music", 66, 46, 26, t.onAccent);
    std::string title = kViewNames[int(view)], sub;
    if (view == View::Search) {
        if (!query.empty() && !results.empty()) { title = "\xE2\x80\x9C" + query + "\xE2\x80\x9D"; sub = songs(results.size()) + (continuation.empty() ? "" : "+"); }
        else sub = lib.history.empty() ? "Press Y to search YouTube" : "Recent searches";
    } else if (view == View::Queue) sub = lib.queue.empty() ? "Nothing queued" : songs(lib.queue.size()) + (lib.shuffle ? " \xC2\xB7 shuffle" : "") + (lib.repeat == 1 ? " \xC2\xB7 repeat all" : lib.repeat == 2 ? " \xC2\xB7 repeat one" : "");
    else if (view == View::Liked) sub = songs(lib.liked.size());
    else if (view == View::Playlists) {
        if (Playlist *p = lib.find(openPlaylist)) { title = p->name; sub = songs(p->tracks.size()) + " \xC2\xB7 B: all playlists"; }
        else sub = std::to_string(lib.playlists.size()) + (lib.playlists.size() == 1 ? " playlist" : " playlists");
    } else if (view == View::Downloads) {
        const int p = dl.pending();
        sub = std::to_string(dl.list().size()) + " on the SD card" + (p ? " \xC2\xB7 " + std::to_string(p) + " downloading" : "");
    } else sub = "YT Music \xC2\xB7 data in " + Paths::root();
    g.text(title, 108, 24, 22, t.text, 640, Align::Left, true);
    g.text(sub, 108, 54, 15, t.sub, 640);

    // clock + battery, right aligned
    float x = W - 56;
    const int pct = battery ? battery() : -1;
    if (pct >= 0) {
        const float bw = 34, bh = 17, by = 38;
        g.strokeRound(x - bw, by, bw, bh, 4, 2, t.text);
        g.fillRound(x + 1, by + 5, 3, bh - 10, 1, t.text);
        const SDL_Color fill = pct <= 15 ? t.danger : t.text;
        g.fillRound(x - bw + 4, by + 4, (bw - 8) * pct / 100.f, bh - 8, 2, fill);
        x -= bw + 10;
        x -= g.text(std::to_string(pct) + "%", x, 36, 17, t.text, 0, Align::Right) + 18;
    }
    const time_t now = time(nullptr);
    struct tm lt;
    localtime_r(&now, &lt);
    char clk[16];
    snprintf(clk, sizeof clk, "%d:%02d", lt.tm_hour, lt.tm_min);
    g.text(clk, x, 33, 22, t.text, 0, Align::Right, true);
}

// The row of square tiles (songs, playlists or recent searches)
void App::drawTiles() {
    const Theme &t = TH();
    auto rs = rows();
    const int v = int(view);
    if (rs.empty()) {
        std::string msg;
        if (view == View::Search) msg = searching ? "Searching\xE2\x80\xA6" : (searchStatus.empty() ? "Press Y to search YouTube, or paste a link" : searchStatus);
        else if (view == View::Queue) msg = "The queue is empty. Play something from Search or Liked.";
        else if (view == View::Liked) msg = "No liked songs yet. Press X on a song to like it.";
        else if (view == View::Playlists) msg = "This playlist is empty. Press \xE2\x88\x92 on a song \xE2\x86\x92 Add to playlist.";
        else if (view == View::Downloads) msg = "Nothing downloaded yet. Press \xE2\x88\x92 on a song \xE2\x86\x92 Download.";
        g.fillRound(kTileX0, kTileY, W - 2 * kTileX0, kTile, 28, t.panel);
        if (searching)
            for (int i = 0; i < 3; ++i)
                g.fillCircle(W / 2 - 26 + i * 26, kTileY + kTile / 2 - 30, 7, Col::alpha(t.accent, Uint8(110 + 145 * (0.5f + 0.5f * std::sin(clock * 6 - i)))));
        g.text(msg, W / 2, kTileY + kTile / 2 - 4, 20, t.sub, int(W - 2 * kTileX0 - 80), Align::Center);
        return;
    }
    const float step = kTile + kGap;
    const int sv = sel[v];
    g.clip(0, kTileY - 70, W, kTile + 140);
    const int first = std::max(0, int((scrollShown[v]) / step) - 1);
    for (int i = first; i < int(rs.size()); ++i) {
        const float x = kTileX0 + i * step - scrollShown[v];
        if (x > W) break;
        if (x + kTile < 0) continue;
        const Row &r = rs[size_t(i)];
        const bool on = i == sv;
        g.shadow(x, kTileY, kTile, kTile, 26, 10, darkTheme() ? 90 : 40);
        g.fillRound(x, kTileY, kTile, kTile, 26, t.panel);
        std::string vid = r.t.vid;
        if (SDL_Texture *tx = vid.empty() ? nullptr : thumbs.get(vid)) g.image(tx, x, kTileY, kTile, kTile, 26);
        else if (r.kind == Row::Query) {
            g.fillRound(x, kTileY, kTile, kTile, 26, t.panelHi);
            g.icon("search", x + kTile / 2, kTileY + 92, 56, t.accent);
            g.text(r.label, x + kTile / 2, kTileY + 158, 20, t.text, int(kTile - 30), Align::Center, true);
        } else if (r.kind == Row::NewPlaylist) {
            g.fillRound(x, kTileY, kTile, kTile, 26, t.panelHi);
            g.fillCircle(x + kTile / 2, kTileY + 108, 42, t.accent);
            g.text("+", x + kTile / 2, kTileY + 80, 52, t.onAccent, 0, Align::Center, true);
            g.text("New playlist", x + kTile / 2, kTileY + 170, 19, t.text, 0, Align::Center, true);
        } else {
            g.icon(r.kind == Row::PlaylistRow ? "list" : "music", x + kTile / 2, kTileY + kTile / 2, 60, t.line);
        }
        if (r.kind == Row::PlaylistRow) {   // name band along the bottom of playlist tiles
            g.fillRound(x, kTileY + kTile - 64, kTile, 64, 26, SDL_Color{0, 0, 0, 150});
            g.fillRect(x, kTileY + kTile - 64, kTile, 30, SDL_Color{0, 0, 0, 150});
            g.text(r.label, x + 18, kTileY + kTile - 56, 19, SDL_Color{255, 255, 255, 255}, int(kTile - 36), Align::Left, true);
            g.text(r.sub, x + 18, kTileY + kTile - 30, 14, SDL_Color{220, 220, 228, 255});
        }
        if (r.kind == Row::Song) {
            const bool playing = lib.current >= 0 && lib.current < int(lib.queue.size()) && lib.queue[size_t(lib.current)].vid == r.t.vid &&
                                 (view != View::Queue || r.index == lib.current);
            if (playing) {   // equaliser bars on the playing song
                g.fillRound(x, kTileY, kTile, kTile, 26, SDL_Color{0, 0, 0, 120});
                const bool moving = player.state() == Player::Playing;
                for (int b = 0; b < 4; ++b) {
                    const float hgt = moving ? 22 + 30 * (0.5f + 0.5f * std::sin(clock * (5 + b * 1.7f) + b)) : 18;
                    g.fillRound(x + kTile / 2 - 34 + b * 18, kTileY + kTile / 2 + 26 - hgt, 12, hgt, 4, t.accent);
                }
            }
            float bx = x + kTile - 16;
            if (dl.isDownloaded(r.t.vid)) {
                g.fillCircle(bx - 16, kTileY + 30, 17, t.accent);
                g.icon("download", bx - 16, kTileY + 30, 16, t.onAccent);
                bx -= 42;
            } else if (dl.isActive(r.t.vid)) {
                g.fillCircle(bx - 16, kTileY + 30, 17, SDL_Color{0, 0, 0, 170});
                g.icon("download", bx - 16, kTileY + 30, 16, SDL_Color{255, 255, 255, 255});
                g.fillRound(x + 14, kTileY + kTile - 16, (kTile - 28) * float(std::max(0.04, dl.progress(r.t.vid))), 6, 3, t.accent);
                bx -= 42;
            }
            if (lib.isLiked(r.t.vid)) {
                g.fillCircle(bx - 16, kTileY + 30, 17, SDL_Color{0, 0, 0, 150});
                g.icon("heart", bx - 16, kTileY + 31, 17, SDL_Color{255, 110, 140, 255});
            }
            g.fillRound(x + 12, kTileY + kTile - 38, float(g.measure(fmtTime(r.t.duration), 14, true) + 16), 26, 13, SDL_Color{0, 0, 0, 160});
            g.text(fmtTime(r.t.duration), x + 20, kTileY + kTile - 34, 14, SDL_Color{255, 255, 255, 255}, 0, Align::Left, true);
        }
        if (on && focus == 0) g.rainbowRing(x - 7, kTileY - 7, kTile + 14, kTile + 14, 32, 5, clock);
    }
    g.unclip();

    // the selected item's name above the row, details below (like the home menu)
    if (sv < int(rs.size())) {
        const Row &r = rs[size_t(sv)];
        const float x = std::clamp(kTileX0 + sv * step - scrollShown[v], kTileX0, W - kTileX0 - 600);
        const std::string name = r.kind == Row::Song ? r.t.title : r.label;
        g.text(name, x, kTileY - 48, 24, focus == 0 ? t.accent : t.sub, int(W - x - kTileX0), Align::Left, true);
        std::string det;
        if (r.kind == Row::Song) det = r.t.artist + "  \xC2\xB7  " + fmtTime(r.t.duration);
        else if (r.kind == Row::PlaylistRow || r.kind == Row::NewPlaylist) det = r.sub;
        else if (r.kind == Row::Query) det = "Search again";
        g.text(det, x, kTileY + kTile + 16, 17, t.sub, int(W - x - kTileX0));
        if (view == View::Search && loadingMore) g.text("Loading more\xE2\x80\xA6", W - kTileX0, kTileY + kTile + 16, 15, t.sub, 0, Align::Right);
        else if (rs.size() > 1) g.text(std::to_string(sv + 1) + " / " + std::to_string(rs.size()) + (continuation.empty() || view != View::Search ? "" : "+"),
                                       W - kTileX0, kTileY + kTile + 16, 15, t.sub, 0, Align::Right);
    }
}

// Settings: a console-style list of options
void App::drawList() {
    const Theme &t = TH();
    auto rs = rows();
    const int v = int(view);
    g.fillRound(kListX - 10, kListY - 10, kListW + 20, kListH + 20, 26, t.panel);
    g.clip(kListX - 10, kListY - 6, kListW + 20, kListH + 12);
    for (int i = 0; i < int(rs.size()); ++i) {
        const float y = kListY + i * kRowH - scrollShown[v];
        if (y > kListY + kListH) break;
        if (y + kRowH < kListY) continue;
        const Row &r = rs[size_t(i)];
        const bool on = i == sel[v] && focus == 0;
        if (i > 0) g.fillRect(kListX + 20, y, kListW - 40, 1, t.line);
        if (on) {
            g.fillRound(kListX + 4, y + 4, kListW - 8, kRowH - 8, 16, t.panelHi);
            g.rainbowRing(kListX + 2, y + 2, kListW - 4, kRowH - 4, 18, 4, clock);
        }
        g.text(r.label, kListX + 30, y + 10, 20, r.id == "exit" ? t.danger : t.text, int(kListW - 60), Align::Left, on);
        g.text(r.sub, kListX + 30, y + 38, 14, t.sub, int(kListW - 60));
    }
    g.unclip();
}

// Round section buttons in a rounded tray, label under the focused one
void App::drawTabs() {
    const Theme &t = TH();
    const float trayW = (int(View::Count) - 1) * kTabStep + kTabBtn + 48;
    g.fillRound(W / 2 - trayW / 2, kTabY, trayW, kTabH, kTabH / 2, t.panel);
    for (int i = 0; i < int(View::Count); ++i) {
        const float cx = tabX(i), cy = kTabY + kTabH / 2;
        const bool current = int(view) == i;
        g.fillCircle(cx, cy, kTabBtn / 2, current ? t.accent : t.panelHi);
        g.icon(kViewIcons[i], cx, cy, 28, current ? t.onAccent : t.text);
        if (focus == 1 && i == tabSel) {
            g.rainbowRing(cx - kTabBtn / 2 - 6, cy - kTabBtn / 2 - 6, kTabBtn + 12, kTabBtn + 12, kTabBtn / 2 + 6, 4, clock);
            g.text(kViewNames[i], cx, kTabY + kTabH + 10, 17, t.accent, 0, Align::Center, true);
        }
        std::string count;
        if (i == int(View::Queue) && !lib.queue.empty()) count = std::to_string(lib.queue.size());
        if (i == int(View::Downloads) && dl.pending()) count = std::to_string(dl.pending());
        if (!count.empty()) {
            const float bw = float(std::max(22, g.measure(count, 12, true) + 12));
            g.fillRound(cx + 16, cy - 36, bw, 22, 11, t.danger);
            g.text(count, cx + 16 + bw / 2, cy - 33, 12, SDL_Color{255, 255, 255, 255}, 0, Align::Center, true);
        }
    }
}

// Bottom bar: now playing on the left, button guide on the right (separated by a hairline, like the system menus)
void App::drawHints() {
    const Theme &t = TH();
    g.fillRect(28, kHintY, W - 56, 1, t.line);
    const Track *tr = (lib.current >= 0 && lib.current < int(lib.queue.size())) ? &lib.queue[size_t(lib.current)] : nullptr;
    const Player::State st = player.state();
    const float y = kHintY + 12;
    g.fillRound(36, y, 44, 44, 10, t.panel);
    if (tr) { if (SDL_Texture *tx = thumbs.get(tr->vid)) g.image(tx, 36, y, 44, 44, 10); }
    else g.icon("music", 58, y + 22, 18, t.sub);
    const float tx0 = 92, tw = 400;
    g.text(tr ? tr->title : "Nothing playing", tx0, y + 1, 16, t.text, int(tw), Align::Left, true);
    std::string sub;
    if (loadingTrack || st == Player::Loading) sub = "Loading\xE2\x80\xA6";
    else if (tr) sub = (st == Player::Paused ? "Paused \xC2\xB7 " : "") + fmtTime(player.position()) + " / " + fmtTime(player.duration() > 0 ? player.duration() : tr->duration);
    else sub = "Pick a song and press A";
    if (toastVol > 0) sub = "Volume " + std::to_string(player.volume());
    g.text(sub, tx0, y + 24, 13, (loadingTrack || st == Player::Loading) ? t.accent : t.sub, int(tw));
    // thin progress line under the hairline
    const double d = player.duration() > 0 ? player.duration() : (tr ? tr->duration : 0);
    if (tr && d > 0 && st != Player::Idle) g.fillRect(28, kHintY - 1, float((W - 56) * std::clamp(player.position() / d, 0.0, 1.0)), 3, t.accent);

    // button guide, right to left
    float x = W - 40;
    auto hint = [&](const std::string &btn, const std::string &label) {
        x -= g.measure(label, 17) ;
        g.text(label, x, y + 10, 17, t.text);
        x -= 8;
        const int bw = std::max(26, g.measure(btn, 14, true) + 14);
        x -= float(bw);
        g.badge(btn, x, y + 9, t.text, t.bg);
        x -= 26;
    };
    if (!menu.empty()) { hint("B", "Close"); hint("A", "OK"); return; }
    if (focus == 1) { hint("B", "Back"); hint("A", "Open"); hint("Y", "Search"); return; }
    hint("+", st == Player::Playing ? "Pause" : "Resume");
    hint("Y", "Search");
    hint("\xE2\x88\x92", "Options");
    auto rs = rows();
    const int s = sel[int(view)];
    if (!rs.empty() && s < int(rs.size())) {
        const Row &r = rs[size_t(s)];
        if (r.kind == Row::Song) hint("X", lib.isLiked(r.t.vid) ? "Unlike" : "Like");
        hint("A", r.kind == Row::Song ? "Play" : r.kind == Row::Action ? "Select" : "Open");
    }
}

void App::drawMenu() {
    if (menu.empty()) return;
    const Theme &t = TH();
    g.fillRect(0, 0, W, H, t.dim);
    const int vis = std::min(int(menu.size()), kMenuVisible);
    const float mh = 84 + vis * 54.f, mw = 620;
    const float mx = (W - mw) / 2, my = (H - mh) / 2;
    g.shadow(mx, my, mw, mh, 28, 16, 120);
    g.fillRound(mx, my, mw, mh, 28, t.panel);
    g.text(menuTitle, mx + mw / 2, my + 22, 19, t.text, int(mw - 60), Align::Center, true);
    g.fillRect(mx + 24, my + 64, mw - 48, 1, t.line);
    if (int(menu.size()) > vis) {
        const float bh = vis * 54.f * vis / menu.size(), by = my + 72 + (vis * 54.f - bh) * menuTop / float(menu.size() - vis);
        g.fillRound(mx + mw - 14, by, 4, bh, 2, t.line);
    }
    for (int i = menuTop; i < menuTop + vis; ++i) {
        const float iy = my + 72 + (i - menuTop) * 54;
        const bool on = i == menuSel;
        if (on) {
            g.fillRound(mx + 18, iy + 2, mw - 36, 48, 14, t.panelHi);
            g.rainbowRing(mx + 16, iy, mw - 32, 52, 16, 4, clock);
        }
        g.text(menu[size_t(i)].label, mx + mw / 2, iy + 13, 19, menu[size_t(i)].danger ? t.danger : t.text, int(mw - 80), Align::Center, on);
    }
}

// Notification pill, top right under the clock
void App::drawToast() {
    if (toastT <= 0 || toastText.empty()) return;
    const Theme &t = TH();
    const int tw = std::min(700, g.measure(toastText, 16) + 48);
    const float slide = std::min(1.f, (3.0f - toastT) * 6) * std::min(1.f, toastT * 4);
    const float x = W - 40 - tw * slide, y = 92;
    g.shadow(x, y, float(tw), 46, 23, 8, 80);
    g.fillRound(x, y, float(tw), 46, 23, t.panelHi);
    g.text(toastText, x + 24, y + 13, 16, t.text, tw - 40);
}
