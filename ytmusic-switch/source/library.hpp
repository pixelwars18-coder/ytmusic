#pragma once
#include "common.hpp"
#include <set>

struct Playlist {
    std::string id, name, source;
    std::vector<Track> tracks;
};

// Likes, playlists, queue and search history, kept in sdmc:/switch/ytmusic/state.json.
// The backup format is the same as the desktop and Android apps' "Export library".
class Library {
public:
    std::vector<Track> liked, queue;
    std::vector<Playlist> playlists;
    std::vector<std::string> history;
    int current = -1;
    int volume = 80;
    bool shuffle = false;
    int repeat = 0;   // 0 off, 1 all, 2 one
    bool dark = true; // theme
    bool bg = false;  // background playback (sysmodule)

    void load();
    void save();
    void markDirty() { dirty = true; }
    void saveIfDirty() { if (dirty) save(); }

    bool isLiked(const std::string &vid) const { return likedSet.count(vid) > 0; }
    void toggleLike(const Track &t);
    void addHistory(const std::string &q);
    std::string createPlaylist(const std::string &name, const std::vector<Track> &tracks = {});
    int addToPlaylist(const std::string &id, const std::vector<Track> &tracks);   // returns how many were new
    void deletePlaylist(const std::string &id);
    Playlist *find(const std::string &id);

    // merges a backup file; returns a summary or an error
    std::string importBackup(const std::string &path, bool *ok);
    std::string exportBackup(const std::string &path) const;

private:
    void rebuild();
    std::set<std::string> likedSet;
    bool dirty = false;
};
