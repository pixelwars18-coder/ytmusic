#include "net.hpp"
#include <curl/curl.h>
#include <cstring>

namespace Net {

void init() { curl_global_init(CURL_GLOBAL_DEFAULT); }
void cleanup() { curl_global_cleanup(); }

static size_t toString(char *p, size_t s, size_t n, void *ud) {
    static_cast<std::string *>(ud)->append(p, s * n);
    return s * n;
}

static curl_slist *mkHeaders(const Headers &h) {
    curl_slist *l = nullptr;
    for (const auto &[k, v] : h) l = curl_slist_append(l, (k + ": " + v).c_str());
    return l;
}

static void common(CURL *c, const std::string &url, long timeoutSec) {
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");   // gzip: Innertube JSON is big
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    if (timeoutSec > 0) curl_easy_setopt(c, CURLOPT_TIMEOUT, timeoutSec);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
#ifdef __SWITCH__
    curl_easy_setopt(c, CURLOPT_BUFFERSIZE, 128 * 1024L);
#endif
}

static Response perform(const std::string &url, const std::string *body, const Headers &h, long timeoutSec) {
    Response r;
    CURL *c = curl_easy_init();
    if (!c) { r.error = "curl init failed"; return r; }
    common(c, url, timeoutSec);
    curl_slist *hl = mkHeaders(h);
    if (hl) curl_easy_setopt(c, CURLOPT_HTTPHEADER, hl);
    if (body) {
        curl_easy_setopt(c, CURLOPT_POST, 1L);
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, body->data());
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, long(body->size()));
    }
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, toString);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &r.body);
    const CURLcode rc = curl_easy_perform(c);
    if (rc != CURLE_OK) r.error = curl_easy_strerror(rc);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r.status);
    curl_slist_free_all(hl);
    curl_easy_cleanup(c);
    return r;
}

Response get(const std::string &url, const Headers &h, long t) { return perform(url, nullptr, h, t); }
Response post(const std::string &url, const std::string &body, const Headers &h, long t) { return perform(url, &body, h, t); }

struct StreamCtx {
    const std::function<bool(const char *, size_t)> *sink;
    const std::atomic<bool> *cancel;
};

static size_t toSink(char *p, size_t s, size_t n, void *ud) {
    auto *ctx = static_cast<StreamCtx *>(ud);
    if (ctx->cancel && ctx->cancel->load()) return 0;
    return (*ctx->sink)(p, s * n) ? s * n : 0;
}

static int progressCb(void *ud, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto *ctx = static_cast<StreamCtx *>(ud);
    return (ctx->cancel && ctx->cancel->load()) ? 1 : 0;
}

long stream(const std::string &url, const std::function<bool(const char *, size_t)> &sink, const std::atomic<bool> *cancel,
            std::string *err, const Headers &h) {
    CURL *c = curl_easy_init();
    if (!c) { if (err) *err = "curl init failed"; return 0; }
    common(c, url, 0);
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, nullptr);   // media: no gzip
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);     // stalled for 20 s: give up (caller retries)
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 20L);
    curl_slist *hl = mkHeaders(h);
    if (hl) curl_easy_setopt(c, CURLOPT_HTTPHEADER, hl);
    StreamCtx ctx{&sink, cancel};
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, toSink);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, progressCb);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &ctx);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    const CURLcode rc = curl_easy_perform(c);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    if (rc != CURLE_OK) {
        if (err) *err = curl_easy_strerror(rc);
        if (rc != CURLE_WRITE_ERROR && rc != CURLE_ABORTED_BY_CALLBACK) status = 0;
    }
    curl_slist_free_all(hl);
    curl_easy_cleanup(c);
    return status;
}

std::string urlEncode(const std::string &s) {
    static const char *hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char ch : s) {
        if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') o += char(ch);
        else { o += '%'; o += hex[ch >> 4]; o += hex[ch & 15]; }
    }
    return o;
}

} // namespace Net
