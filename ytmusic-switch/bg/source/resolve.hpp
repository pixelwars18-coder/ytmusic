#pragma once
#include <cstdint>
#include <string>

// Stream URL lookup for the background service. Same Innertube clients as the app, but the response is
// read with a streaming (SAX) parser: a sysmodule has a few MB of memory, not enough for a JSON tree.
namespace BgResolve {
struct Result {
    std::string url, mime, userAgent, error, visitor;
    int itag = 0;
    int64_t contentLength = 0, expires = 0;
    bool ok() const { return !url.empty(); }
};
// clientsPath: optional clients.json override (same format as the app's)
Result resolve(const std::string &vid, const std::string &visitor, const std::string &clientsPath);
int64_t expiryOf(const std::string &url);
}
