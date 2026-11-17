#pragma once
#include <atomic>
#include <functional>
#include <map>
#include <string>

namespace Net {
void init();
void cleanup();
// Keep up to `maxIdle` curl handles (with their open connections) between requests instead of connecting
// afresh every time. A TLS handshake takes the Switch hundreds of milliseconds, and a song start is three
// requests, a page of covers a dozen. 0 (the default) turns it off: every request opens its own connection.
void setKeepAlive(int maxIdle);

struct Response {
    long status = 0;
    std::string body;
    std::string error;   // curl error text, empty on transport success
    bool ok() const { return error.empty() && status >= 200 && status < 300; }
};

using Headers = std::map<std::string, std::string>;

Response get(const std::string &url, const Headers &h = {}, long timeoutSec = 20);
Response post(const std::string &url, const std::string &body, const Headers &h = {}, long timeoutSec = 20);

// Streams a GET into `sink` (return false to abort). `cancel` aborts from another thread.
// Returns the HTTP status (0 on transport error, error text in *err).
long stream(const std::string &url, const std::function<bool(const char *, size_t)> &sink,
            const std::atomic<bool> *cancel = nullptr, std::string *err = nullptr, const Headers &h = {});

std::string urlEncode(const std::string &s);
}
