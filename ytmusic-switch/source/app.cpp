#include "app.hpp"
#include <algorithm>
#include <cmath>
#include <random>

namespace {
constexpr float W = 1280, H = 720;
constexpr float kSideW = 252, kCX = 276, kCW = W - kCX - 24;
constexpr float kListY = 116, kRowH = 80, kBarY = 610, kBarH = 94;
constexpr float kListH = kBarY - 14 - kListY;
constexpr int kMenuVisible = 10;
const char *kViewNames[] = {"Search", "Queue", "Liked", "Playlists", "Downloads", "Settings"};
const char *kViewIcons[] = {"search", "queue", "heart", "list", "download", "gear"};
std::mt19937 &rng() { static std::mt19937 r{std::random_device{}()}; return r; }
std::string songs(size_t n) { return std::to_string(n) + (n == 1 ? " song" : " songs"); }
bool fresh(const YT::Stream &s) { return s.ok() && (s.expires == 0 || s.expires - nowSecs() > 600); }
}

bool App::init(SDL_Renderer *r, const std::vector<Gfx::FontBlob> &fonts) {
    if (!g.init(r, fonts)) return false;
    thumbs.start(r);
    mkdirs(Paths::root());
    lib.load();
    player.init();
    player.setVolume(lib.volume);
    dl.start();
    if (!lib.queue.empty()) view = View::Queue;
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
}

void App::ensureVisible() {
    const int v = int(view);
    const float y = sel[v] * kRowH;
    if (y < scroll[v]) scroll[v] = y;
    if (y + kRowH > scroll[v] + kListH) scroll[v] = y + kRowH - kListH;
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

    // ---------------- menu overlay
    if (!menu.empty()) {
        if (in.down & (B_B | B_MINUS)) { menu.clear(); return !quitting; }
        if (in.down & B_UP || (in.down & B_DOWN)) menuSel = std::clamp(menuSel + ((in.down & B_DOWN) ? 1 : -1), 0, int(menu.size()) - 1);
        const int vis = std::min(int(menu.size()), kMenuVisible);
        if (menuSel < menuTop) menuTop = menuSel;
        if (menuSel >= menuTop + vis) menuTop = menuSel - vis + 1;
        const float mh = 64 + vis * 52.f;
        const float mx = (W - 560) / 2, my = (H - mh) / 2;
        bool fire = (in.down & B_A) != 0;
        if (in.touching && !touchWas) {
            const int i = menuTop + int((in.ty - my - 60) / 52);
            if (in.tx >= mx && in.tx <= mx + 560 && in.ty > my + 60 && i >= 0 && i < int(menu.size())) { menuSel = i; fire = true; }
            else if (in.tx < mx || in.tx > mx + 560 || in.ty < my || in.ty > my + mh) menu.clear();
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

    // ---------------- navigation with key repeat
    uint32_t dir = 0;
    if ((in.held & B_UP) || in.ly > 0.5f) dir = B_UP;
    else if ((in.held & B_DOWN) || in.ly < -0.5f) dir = B_DOWN;
    if (dir) {
        if (dir != lastDir) { repeatT = 0.38f; lastDir = dir; s += dir == B_DOWN ? 1 : -1; }
        else if ((repeatT -= in.dt) <= 0) { repeatT = 0.065f; s += dir == B_DOWN ? 1 : -1; }
        s = std::clamp(s, 0, std::max(0, int(rs.size()) - 1));
        ensureVisible();
    } else lastDir = 0;
    if (in.down & B_RIGHT) { s = std::min(int(rs.size()) - 1, s + 6); s = std::max(s, 0); ensureVisible(); }
    if (in.down & B_LEFT) { s = std::max(0, s - 6); ensureVisible(); }

    if (in.down & B_L) switchView(-1);
    if (in.down & B_R) switchView(1);
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
        if (volAccum > 0.08f) { volAccum = 0; player.setVolume(player.volume() + (in.ry > 0 ? 2 : -2)); }
    } else volAccum = 0.08f;

    if (in.down & B_B) {
        if (view == View::Playlists && !openPlaylist.empty()) { openPlaylist.clear(); sel[v] = playlistsSel; ensureVisible(); }
        else if (view == View::Search && !results.empty()) { results.clear(); searchStatus.clear(); ++searchGen; searching = false; }
    }
    rs = rows();
    s = std::clamp(s, 0, std::max(0, int(rs.size()) - 1));
    if (!rs.empty()) {
        const Row &r = rs[size_t(s)];
        if (in.down & B_A) activate(r);
        else if ((in.down & B_X) && (r.kind == Row::Song)) { lib.toggleLike(r.t); toast(lib.isLiked(r.t.vid) ? "Added to liked" : "Removed from liked"); }
        else if (in.down & B_MINUS) openMenu(&r);
    } else if (in.down & B_MINUS) openMenu(nullptr);

    // ---------------- touch
    if (in.touching && !touchWas) { touchStartX = in.tx; touchStartY = touchLastY = in.ty; dragging = false; }
    if (in.touching && touchWas) {
        if (std::fabs(in.ty - touchStartY) > 14 && touchStartX > kCX && touchStartY > kListY && touchStartY < kBarY) dragging = true;
        if (dragging) {
            scroll[v] = std::clamp(scroll[v] - (in.ty - touchLastY), 0.f, std::max(0.f, rs.size() * kRowH - kListH));
            touchLastY = in.ty;
        }
    }
    if (!in.touching && touchWas && !dragging) {   // tap
        const float x = touchStartX, y = touchStartY;
        if (x < kSideW && y > 96 && y < 96 + int(View::Count) * 58) view = View(int((y - 96) / 58));
        else if (x > kCX && y > kListY && y < kBarY - 14) {
            const int i = int((y - kListY + scroll[v]) / kRowH);
            if (i >= 0 && i < int(rs.size())) {
                if (i == s) activate(rs[size_t(i)]);
                else s = i;
            }
        } else if (y > kBarY) {
            const float cx = W / 2 + 40;
            if (std::fabs(x - cx) < 34) player.togglePause();
            else if (std::fabs(x - (cx - 72)) < 26) prev();
            else if (std::fabs(x - (cx + 72)) < 26) next(false);
            else if (y > kBarY + kBarH - 24 && x > kCX && x < W - 220 && player.duration() > 0)
                player.seek((x - kCX) / (W - 220 - kCX) * player.duration());
        } else if (x > kCX + kCW - 160 && y < kListY - 10) askSearch();
    }
    touchWas = in.touching;

    // infinite scroll for search results
    if (view == View::Search && !results.empty() && s >= int(results.size()) - 6) loadMore();

    // smooth scroll toward selection when not dragging is handled by ensureVisible (instant)
    g.endFrame();
    return !quitting;
}

// ---------------------------------------------------------------- drawing
void App::render() {
    SDL_SetRenderDrawColor(g.r, Col::surface.r, Col::surface.g, Col::surface.b, 255);
    SDL_RenderClear(g.r);
    drawSidebar();
    drawHeader();
    drawList();
    drawPlayerBar();
    drawToast();
    drawMenu();
}

void App::drawSidebar() {
    g.fillRect(0, 0, kSideW, H, Col::alpha(Col::surfaceVariant, 160));
    g.fillRect(kSideW - 1, 0, 1, H, Col::outline);
    g.fillCircle(42, 48, 18, Col::primary);
    g.icon("play", 43, 48, 16, Col::onPrimary);
    g.text("YT Music", 70, 34, 24, Col::onSurface, 0, Align::Left, true);
    for (int i = 0; i < int(View::Count); ++i) {
        const float y = 96 + i * 58;
        const bool on = int(view) == i;
        if (on) g.fillRound(14, y, kSideW - 28, 48, 24, Col::secondary);
        const SDL_Color fg = on ? Col::onPrimary : Col::onSurface;
        g.icon(kViewIcons[i], 44, y + 24, 20, fg);
        g.text(kViewNames[i], 68, y + 12, 19, fg, 0, Align::Left, on);
        std::string count;
        if (i == int(View::Queue) && !lib.queue.empty()) count = std::to_string(lib.queue.size());
        if (i == int(View::Liked) && !lib.liked.empty()) count = std::to_string(lib.liked.size());
        if (i == int(View::Playlists) && !lib.playlists.empty()) count = std::to_string(lib.playlists.size());
        if (i == int(View::Downloads)) { const int p = dl.pending(); if (p) count = "\xE2\x86\x93" + std::to_string(p); }
        if (!count.empty()) g.text(count, kSideW - 30, y + 14, 15, on ? Col::onPrimary : Col::onSurfaceVariant, 0, Align::Right);
    }
    float hy = H - 168;
    g.badge("L", 24, hy); g.badge("R", 54, hy);
    g.text("switch tabs", 88, hy + 2, 15, Col::onSurfaceVariant);
    hy += 32;
    g.badge("Y", 24, hy);
    g.text("search", 58, hy + 2, 15, Col::onSurfaceVariant);
    hy += 32;
    g.badge("\xE2\x88\x92", 24, hy);
    g.text("more options", 58, hy + 2, 15, Col::onSurfaceVariant);
}

void App::drawHeader() {
    std::string title = kViewNames[int(view)], sub;
    if (view == View::Search) {
        if (!query.empty() && !results.empty()) { title = query; sub = songs(results.size()) + (continuation.empty() ? "" : " \xC2\xB7 scroll for more"); }
        else if (results.empty()) sub = lib.history.empty() ? "Press Y to search" : "Recent searches";
    } else if (view == View::Queue) sub = lib.queue.empty() ? "Nothing queued" : songs(lib.queue.size()) + (lib.shuffle ? " \xC2\xB7 shuffle" : "") + (lib.repeat == 1 ? " \xC2\xB7 repeat all" : lib.repeat == 2 ? " \xC2\xB7 repeat one" : "");
    else if (view == View::Liked) sub = songs(lib.liked.size());
    else if (view == View::Playlists) {
        if (Playlist *p = lib.find(openPlaylist)) { title = p->name; sub = songs(p->tracks.size()) + " \xC2\xB7 B to go back"; }
        else sub = std::to_string(lib.playlists.size()) + " playlists";
    } else if (view == View::Downloads) {
        const int p = dl.pending();
        sub = std::to_string(dl.list().size()) + " on SD card" + (p ? " \xC2\xB7 " + std::to_string(p) + " downloading" : "") + " \xC2\xB7 play offline";
    } else if (view == View::Settings) sub = "Library is stored in " + Paths::root();
    g.text(title, kCX, 26, 30, Col::onSurface, int(kCW - 200), Align::Left, true);
    g.text(sub, kCX, 70, 16, Col::onSurfaceVariant, int(kCW - 200));
    // search pill
    const float px = kCX + kCW - 170;
    g.fillRound(px, 30, 170, 44, 22, Col::surfaceVariant);
    g.strokeRound(px, 30, 170, 44, 22, 1, Col::outline);
    g.icon("search", px + 26, 52, 18, Col::onSurfaceVariant);
    g.text("Search", px + 46, 41, 17, Col::onSurfaceVariant);
    g.badge("Y", px + 128, 40, Col::outline, Col::onSurface);
}

void App::drawRowSong(const Row &r, float y, bool selected) {
    const float x = kCX;
    const bool playing = lib.current >= 0 && lib.current < int(lib.queue.size()) && lib.queue[size_t(lib.current)].vid == r.t.vid &&
                         (view != View::Queue || r.index == lib.current);
    SDL_Texture *tx = thumbs.get(r.t.vid);
    g.fillRound(x + 10, y + 8, 112, 63, 10, Col::surfaceVariant);
    if (tx) g.image(tx, x + 10, y + 8, 112, 63, 10);
    else g.icon("music", x + 66, y + 40, 22, Col::outline);
    if (playing) {
        g.fillRound(x + 10, y + 8, 112, 63, 10, SDL_Color{8, 8, 32, 170});
        g.icon(player.state() == Player::Paused ? "play" : "music", x + 66, y + 40, 22, Col::primary);
    }
    // download badge on the thumbnail
    if (dl.isDownloaded(r.t.vid)) {
        g.fillCircle(x + 110, y + 60, 10, Col::primary);
        g.icon("check", x + 110, y + 60, 12, Col::onPrimary);
    } else if (dl.isActive(r.t.vid)) {
        g.fillCircle(x + 110, y + 60, 11, SDL_Color{7, 7, 34, 230});
        const double p = dl.progress(r.t.vid);
        g.fillRound(x + 99, y + 72 - 4, float(22 * std::max(0.08, p)), 3, 1.5f, Col::primary);
        g.icon("download", x + 110, y + 59, 11, Col::onSurface);
    }
    const float tx0 = x + 138, tw = kCW - 138 - 150;
    g.text(r.t.title, tx0, y + 14, 20, playing ? Col::primary : Col::onSurface, int(tw), Align::Left, selected);
    g.text(r.t.artist, tx0, y + 44, 16, Col::onSurfaceVariant, int(tw));
    g.text(fmtTime(r.t.duration), x + kCW - 24, y + 28, 16, Col::onSurfaceVariant, 0, Align::Right);
    if (lib.isLiked(r.t.vid)) g.icon("heart", x + kCW - 100, y + 40, 18, Col::primary);
}

void App::drawList() {
    auto rs = rows();
    const int v = int(view);
    SDL_Rect clip{int(kCX - 4), int(kListY), int(kCW + 8), int(kListH)};
    SDL_RenderSetClipRect(g.r, &clip);
    if (rs.empty()) {
        std::string msg;
        if (view == View::Search) msg = searching ? searchStatus : (searchStatus.empty() ? "Press Y to search YouTube" : searchStatus);
        else if (view == View::Queue) msg = "The queue is empty. Play something from Search or Liked.";
        else if (view == View::Liked) msg = "No liked songs yet. Press X on a song to like it.";
        else if (view == View::Playlists) msg = "This playlist is empty. Use \xE2\x88\x92 on a song \xE2\x86\x92 Add to playlist.";
        else if (view == View::Downloads) msg = "Nothing downloaded yet. Use \xE2\x88\x92 on a song \xE2\x86\x92 Download.";
        if (searching) {
            const float t = SDL_GetTicks() / 1000.f;
            for (int i = 0; i < 3; ++i) g.fillCircle(kCX + kCW / 2 - 24 + i * 24, kListY + kListH / 2 - 40, 6, Col::alpha(Col::primary, Uint8(120 + 135 * (0.5f + 0.5f * std::sin(t * 6 - i)))));
        }
        g.text(msg, kCX + kCW / 2, kListY + kListH / 2 - 10, 19, Col::onSurfaceVariant, int(kCW - 80), Align::Center);
        SDL_RenderSetClipRect(g.r, nullptr);
        return;
    }
    const int first = std::max(0, int(scroll[v] / kRowH));
    for (int i = first; i < int(rs.size()); ++i) {
        const float y = kListY + i * kRowH - scroll[v];
        if (y > kListY + kListH) break;
        const Row &r = rs[size_t(i)];
        const bool selected = i == sel[v];
        if (selected) {
            g.fillRound(kCX, y + 2, kCW, kRowH - 4, 18, Col::surfaceVariant);
            g.strokeRound(kCX, y + 2, kCW, kRowH - 4, 18, 2, Col::alpha(Col::secondary, 200));
        }
        switch (r.kind) {
        case Row::Song: drawRowSong(r, y, selected); break;
        case Row::Query:
            g.fillCircle(kCX + 40, y + 40, 22, Col::surfaceVariant);
            g.icon("search", kCX + 40, y + 40, 18, Col::onSurfaceVariant);
            g.text(r.label, kCX + 80, y + 28, 20, Col::onSurface, int(kCW - 120), Align::Left, selected);
            break;
        case Row::NewPlaylist:
            g.fillRound(kCX + 10, y + 8, 112, 63, 10, Col::outline);
            g.text("+", kCX + 66, y + 18, 30, Col::onSurface, 0, Align::Center, true);
            g.text(r.label, kCX + 138, y + 14, 20, Col::onSurface, 0, Align::Left, selected);
            g.text(r.sub, kCX + 138, y + 44, 16, Col::onSurfaceVariant);
            break;
        case Row::PlaylistRow: {
            g.fillRound(kCX + 10, y + 8, 112, 63, 10, Col::surfaceVariant);
            if (SDL_Texture *t = thumbs.get(r.t.vid)) g.image(t, kCX + 10, y + 8, 112, 63, 10);
            else g.icon("list", kCX + 66, y + 40, 22, Col::outline);
            g.text(r.label, kCX + 138, y + 14, 20, Col::onSurface, int(kCW - 200), Align::Left, selected);
            g.text(r.sub, kCX + 138, y + 44, 16, Col::onSurfaceVariant);
            break;
        }
        case Row::Action:
            g.text(r.label, kCX + 24, y + 14, 20, r.id == "exit" ? Col::error : Col::onSurface, int(kCW - 48), Align::Left, selected);
            g.text(r.sub, kCX + 24, y + 44, 15, Col::onSurfaceVariant, int(kCW - 48));
            break;
        }
    }
    if (view == View::Search && (loadingMore || !continuation.empty())) {
        const float y = kListY + rs.size() * kRowH - scroll[v];
        if (y < kListY + kListH) g.text(loadingMore ? "Loading more\xE2\x80\xA6" : "", kCX + kCW / 2, y + 20, 16, Col::onSurfaceVariant, 0, Align::Center);
    }
    // scrollbar
    const float content = rs.size() * kRowH;
    if (content > kListH) {
        const float bh = std::max(40.f, kListH * kListH / content);
        const float by = kListY + (kListH - bh) * (scroll[v] / (content - kListH));
        g.fillRound(kCX + kCW + 6, by, 5, bh, 2.5f, Col::outline);
    }
    SDL_RenderSetClipRect(g.r, nullptr);
}

void App::drawPlayerBar() {
    const float x = kCX - 8, y = kBarY, w = W - x - 16;
    g.fillRound(x, y, w, kBarH, 22, Col::surfaceVariant);
    g.strokeRound(x, y, w, kBarH, 22, 1, Col::outline);
    const Track *t = (lib.current >= 0 && lib.current < int(lib.queue.size())) ? &lib.queue[size_t(lib.current)] : nullptr;
    g.fillRound(x + 14, y + 14, 66, 66, 14, Col::surface);
    if (t) {
        if (SDL_Texture *tx = thumbs.get(t->vid)) g.image(tx, x + 14, y + 14, 66, 66, 14);
        else g.icon("music", x + 47, y + 47, 24, Col::outline);
    } else g.icon("music", x + 47, y + 47, 24, Col::outline);
    const Player::State st = player.state();
    const float tw = (W / 2 + 40 - 72 - 30) - (x + 94);   // up to the transport buttons
    g.text(t ? t->title : "Nothing playing", x + 94, y + 18, 18, Col::onSurface, int(tw), Align::Left, true);
    std::string sub = t ? t->artist : "Pick a song and press A";
    if (loadingTrack || st == Player::Loading) sub = "Loading\xE2\x80\xA6";
    g.text(sub, x + 94, y + 44, 15, (loadingTrack || st == Player::Loading) ? Col::tertiary : Col::onSurfaceVariant, int(tw));

    // transport
    const float cx = W / 2 + 40, cy = y + 38;
    g.icon("prev", cx - 72, cy, 22, Col::onSurface);
    g.fillCircle(cx, cy, 26, Col::primary);
    g.icon(st == Player::Playing ? "pause" : "play", cx + (st == Player::Playing ? 0 : 2), cy, 22, Col::onPrimary);
    g.icon("next", cx + 72, cy, 22, Col::onSurface);
    g.text("ZL", cx - 72, cy + 18, 11, Col::onSurfaceVariant, 0, Align::Center, true);
    g.text("+", cx, cy + 26, 11, Col::onSurfaceVariant, 0, Align::Center, true);
    g.text("ZR", cx + 72, cy + 18, 11, Col::onSurfaceVariant, 0, Align::Center, true);
    if (lib.shuffle) g.icon("shuffle", cx - 136, cy, 18, Col::primary);
    if (lib.repeat) { g.icon("repeat", cx + 136, cy, 18, Col::primary); if (lib.repeat == 2) g.text("1", cx + 136, cy - 7, 11, Col::primary, 0, Align::Center, true); }

    // volume
    const float vx = x + w - 180;
    g.text("Vol", vx, y + 26, 14, Col::onSurfaceVariant);
    g.fillRound(vx + 36, y + 33, 110, 6, 3, Col::outline);
    g.fillRound(vx + 36, y + 33, 110 * player.volume() / 100.f, 6, 3, Col::tertiary);
    g.text(std::to_string(player.volume()), vx + 154, y + 26, 14, Col::onSurfaceVariant);
    g.text("R-stick \xE2\x86\x95 vol \xC2\xB7 \xE2\x86\x94 seek", vx, y + 48, 12, Col::onSurfaceVariant);

    // progress
    const double d = player.duration() > 0 ? player.duration() : (t ? t->duration : 0);
    const double pos = st == Player::Idle ? 0 : player.position();
    const float bx = x + 94, bw = w - 94 - 200, by = y + kBarH - 18;
    g.text(fmtTime(pos), bx, by - 8, 12, Col::onSurfaceVariant);
    g.text(fmtTime(d), bx + bw, by - 8, 12, Col::onSurfaceVariant, 0, Align::Right);
    const float px0 = bx + 40, pw = bw - 80;
    g.fillRound(px0, by - 1, pw, 4, 2, Col::outline);
    if (st != Player::Idle) g.fillRound(px0, by - 1, float(pw * std::clamp(player.buffered(), 0.0, 1.0)), 4, 2, Col::alpha(Col::secondary, 90));
    if (d > 0) {
        const float f = float(std::clamp(pos / d, 0.0, 1.0));
        g.fillRound(px0, by - 1, pw * f, 4, 2, Col::primary);
        g.fillCircle(px0 + pw * f, by + 1, 6, Col::primary);
    }
}

void App::drawMenu() {
    if (menu.empty()) return;
    g.fillRect(0, 0, W, H, SDL_Color{3, 3, 16, 170});
    const int vis = std::min(int(menu.size()), kMenuVisible);
    const float mh = 64 + vis * 52.f;
    const float mx = (W - 560) / 2, my = (H - mh) / 2;
    g.fillRound(mx, my, 560, mh, 24, Col::surfaceVariant);
    g.strokeRound(mx, my, 560, mh, 24, 1, Col::outline);
    g.text(menuTitle, mx + 28, my + 20, 18, Col::secondary, 500, Align::Left, true);
    if (int(menu.size()) > vis) {   // scroll hint
        g.text(std::to_string(menuSel + 1) + "/" + std::to_string(menu.size()), mx + 532, my + 22, 14, Col::onSurfaceVariant, 0, Align::Right);
        const float bh = vis * 52.f * vis / menu.size(), by = my + 60 + (vis * 52.f - bh) * menuTop / float(menu.size() - vis);
        g.fillRound(mx + 550, by, 4, bh, 2, Col::outline);
    }
    for (int i = menuTop; i < menuTop + vis; ++i) {
        const float iy = my + 60 + (i - menuTop) * 52;
        const bool on = i == menuSel;
        if (on) g.fillRound(mx + 12, iy, 536, 48, 14, Col::outline);
        g.text(menu[size_t(i)].label, mx + 30, iy + 13, 19, menu[size_t(i)].danger ? Col::error : Col::onSurface, 480, Align::Left, on);
    }
}

void App::drawToast() {
    if (toastT <= 0 || toastText.empty()) return;
    const int tw = std::min(760, g.measure(toastText, 17) + 48);
    const float x = kCX + (kCW - tw) / 2, y = kBarY - 60;
    const Uint8 a = Uint8(255 * std::min(1.f, toastT * 3));
    g.fillRound(x, y, float(tw), 42, 21, Col::alpha(Col::surfaceVariant, a));
    g.strokeRound(x, y, float(tw), 42, 21, 1, Col::alpha(Col::primary, a));
    g.text(toastText, x + tw / 2.f, y + 11, 17, Col::alpha(Col::onSurface, a), tw - 32, Align::Center);
}
