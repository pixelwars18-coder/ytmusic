#pragma once
#include "common.hpp"
#include <mutex>

// Minimal YouTube Innertube client (what yt-dlp does, without Python):
//  - search: WEB client, "videos" filter, with continuation for "load more"
//  - stream URLs: a client that gets plain https URLs (no signature/JS player, no PO token).
// The client list can be overridden with sdmc:/switch/ytmusic/clients.json when YouTube changes something.
namespace YT {

struct SearchPage {
    std::vector<Track> tracks;
    std::string continuation;   // empty: no more pages
    std::string error;
};
SearchPage search(const std::string &query);
SearchPage searchMore(const std::string &continuation);

struct Stream {
    std::string url, mime, client, error, userAgent;   // fetch the URL with the same User-Agent
    int itag = 0;
    int64_t contentLength = 0;   // bytes, 0 if unknown
    int64_t expires = 0;         // epoch seconds
    double duration = 0;
    Track meta;                  // title/artist from the player response
    bool ok() const { return !url.empty(); }
};
Stream resolve(const std::string &vid, bool verify = true);

// "Start radio": YouTube's Mix for a song (the watch-next playlist "RD<vid>").
std::vector<Track> radio(const std::string &vid);

// Extracts a video id from a pasted link or returns empty.
std::string videoIdFromText(const std::string &text);

std::string thumbUrl(const std::string &vid, bool large = false);   // 480x360, large: 640x480 (16:9 picture letterboxed)
}
