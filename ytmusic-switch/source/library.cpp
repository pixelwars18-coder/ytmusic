#include "library.hpp"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

static std::vector<Track> tracksFrom(const json &a) {
    std::vector<Track> v;
    if (!a.is_array()) return v;
    for (auto &x : a) {
        Track t = Track::fromJson(x);
        if (!t.vid.empty()) v.push_back(t);
    }
    return v;
}
static json tracksTo(const std::vector<Track> &v) {
    json a = json::array();
    for (auto &t : v) a.push_back(t.toJson());
    return a;
}

static bool readJson(const std::string &path, json &out) {
    std::ifstream f(path);
    if (!f) return false;
    try { out = json::parse(f); return true; } catch (...) { return false; }
}

void Library::load() {
    json o;
    if (!readJson(Paths::state(), o) || !o.is_object()) return;
    liked = tracksFrom(o.value("liked", json::array()));
    queue = tracksFrom(o.value("queue", json::array()));
    for (auto &p : o.value("playlists", json::array())) {
        Playlist pl;
        pl.id = p.value("id", std::string());
        pl.name = p.value("name", std::string("Playlist"));
        pl.source = p.value("source", std::string());
        pl.tracks = tracksFrom(p.value("tracks", json::array()));
        if (!pl.id.empty()) playlists.push_back(pl);
    }
    for (auto &h : o.value("history", json::array())) if (h.is_string()) history.push_back(h.get<std::string>());
    current = o.value("current", -1);
    if (current >= int(queue.size())) current = -1;
    volume = std::clamp(o.value("volume", 80), 0, 100);
    shuffle = o.value("shuffle", false);
    repeat = std::clamp(o.value("repeat", 0), 0, 2);
    rebuild();
}

void Library::save() {
    json o;
    o["liked"] = tracksTo(liked);
    o["queue"] = tracksTo(queue);
    json pa = json::array();
    for (auto &p : playlists) pa.push_back({{"id", p.id}, {"name", p.name}, {"source", p.source}, {"tracks", tracksTo(p.tracks)}});
    o["playlists"] = pa;
    o["history"] = history;
    o["current"] = current;
    o["volume"] = volume;
    o["shuffle"] = shuffle;
    o["repeat"] = repeat;
    mkdirs(Paths::root());
    const std::string tmp = Paths::state() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) return;
        f << o.dump();
        if (!f.good()) return;
    }
    remove(Paths::state().c_str());   // FAT32 rename won't replace an existing file
    rename(tmp.c_str(), Paths::state().c_str());
    dirty = false;
}

void Library::rebuild() {
    likedSet.clear();
    for (auto &t : liked) likedSet.insert(t.vid);
}

void Library::toggleLike(const Track &t) {
    if (t.vid.empty()) return;
    if (isLiked(t.vid)) liked.erase(std::remove(liked.begin(), liked.end(), t), liked.end());
    else liked.insert(liked.begin(), t);
    rebuild();
    dirty = true;
}

void Library::addHistory(const std::string &q) {
    history.erase(std::remove(history.begin(), history.end(), q), history.end());
    history.insert(history.begin(), q);
    if (history.size() > 12) history.resize(12);
    dirty = true;
}

std::string Library::createPlaylist(const std::string &name, const std::vector<Track> &tracks) {
    Playlist p;
    char id[32];
    snprintf(id, sizeof id, "sw%llx", (unsigned long long)(nowSecs() * 1000 + playlists.size()));
    p.id = id;
    p.name = name.empty() ? "New playlist" : name;
    p.tracks = tracks;
    playlists.insert(playlists.begin(), p);
    dirty = true;
    return p.id;
}

Playlist *Library::find(const std::string &id) {
    for (auto &p : playlists) if (p.id == id) return &p;
    return nullptr;
}

int Library::addToPlaylist(const std::string &id, const std::vector<Track> &add) {
    Playlist *p = find(id);
    if (!p) return 0;
    int n = 0;
    for (auto &t : add)
        if (std::find(p->tracks.begin(), p->tracks.end(), t) == p->tracks.end()) { p->tracks.push_back(t); ++n; }
    dirty = true;
    return n;
}

void Library::deletePlaylist(const std::string &id) {
    playlists.erase(std::remove_if(playlists.begin(), playlists.end(), [&](const Playlist &p) { return p.id == id; }), playlists.end());
    dirty = true;
}

std::string Library::importBackup(const std::string &path, bool *ok) {
    *ok = false;
    json o;
    if (!readJson(path, o)) return "Couldn't read " + path;
    if (o.value("app", std::string()) != "ytmusic") return "That file isn't a YT Music backup";
    int nLiked = 0, nPl = 0, nTr = 0;
    for (auto &t : tracksFrom(o.value("liked", json::array())))
        if (!isLiked(t.vid)) { liked.push_back(t); likedSet.insert(t.vid); ++nLiked; }
    for (auto &h : o.value("history", json::array()))
        if (h.is_string() && std::find(history.begin(), history.end(), h.get<std::string>()) == history.end()) history.push_back(h.get<std::string>());
    if (history.size() > 12) history.resize(12);
    for (auto &p : o.value("playlists", json::array())) {
        const std::string id = p.value("id", std::string());
        auto tr = tracksFrom(p.value("tracks", json::array()));
        if (find(id)) nTr += addToPlaylist(id, tr);
        else {
            char gen[32];
            snprintf(gen, sizeof gen, "im%llx%zu", (unsigned long long)nowSecs(), playlists.size());
            Playlist np{id.empty() ? std::string(gen) : id, p.value("name", std::string("Playlist")), p.value("source", std::string()), tr};
            playlists.push_back(np);
            ++nPl;
            nTr += int(tr.size());
        }
    }
    dirty = true;
    *ok = true;
    std::ostringstream s;
    s << "Imported " << nLiked << " liked songs, " << nPl << " playlists, " << nTr << " playlist songs";
    return s.str();
}

std::string Library::exportBackup(const std::string &path) const {
    json root{{"app", "ytmusic"}, {"version", 1}};
    root["liked"] = tracksTo(liked);
    root["history"] = history;
    json pa = json::array();
    for (auto &p : playlists) pa.push_back({{"id", p.id}, {"name", p.name}, {"source", p.source}, {"tracks", tracksTo(p.tracks)}});
    root["playlists"] = pa;
    std::ofstream f(path, std::ios::trunc);
    if (!f) return "Can't write " + path;
    f << root.dump(2);
    return f.good() ? std::string() : "Write failed";
}
