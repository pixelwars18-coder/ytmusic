#include "engine.hpp"
#include "../../source/net.hpp"
#include "resolve.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <random>
#include <sys/stat.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

using namespace std::chrono_literals;

void bglog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

struct Engine::Session {
    BgTrack t;
    double startAt = 0;
    std::string cache;   // file being filled (or the downloaded file itself)
    bool local = false;
    std::atomic<bool> cancel{false};
    std::atomic<double> seekTo{-1};
    std::atomic<int> state{0};   // 0 loading, 1 playing, 2 paused, 3 ended, 4 error
    std::atomic<double> pos{0}, dur{0};
    std::string err;
    std::mutex m;
    std::condition_variable cv;
    int64_t written = 0, total = -1;
    bool fetchDone = false, fetchFailed = false;
    std::string fetchErr;
    FILE *rf = nullptr;    // decoder's read handle
    int64_t readPos = 0;
    std::thread main, fetcher;
};

Engine::Engine(Sink *s, std::string r) : sink(s), root(std::move(r)) {}

Engine::~Engine() {
    stopSession();
    if (ahead.joinable()) ahead.join();
}

static int64_t now() { return int64_t(time(nullptr)); }

// ------------------------------------------------------------- commands
void Engine::load(std::vector<BgTrack> q, int i, double pos, bool paused) {
    {
        std::lock_guard<std::mutex> l(mu);
        queue = std::move(q);
        nextPick = -1;
        errorStreak = 0;
    }
    if (i < 0 || i >= int(queue.size())) { stop(); return; }
    start(i, pos, paused);
}

void Engine::setQueue(std::vector<BgTrack> q, int i) {
    std::lock_guard<std::mutex> l(mu);
    // keep resolved URLs we already have
    for (auto &t : q)
        for (auto &o : queue)
            if (o.vid == t.vid && t.url.empty() && !o.url.empty()) { t.url = o.url; t.ua = o.ua; t.len = o.len; t.exp = o.exp; }
    queue = std::move(q);
    index = (i >= 0 && i < int(queue.size())) ? i : -1;
    nextPick = -1;
}

void Engine::setPaused(bool p) {
    std::shared_ptr<Session> s;
    { std::lock_guard<std::mutex> l(mu); s = cur; }
    if (!s) {
        if (!p && index >= 0) start(index, 0, false);   // resume after stop
        return;
    }
    const int st = s->state.load();
    if (p && st == 1) { s->state = 2; sink->setPaused(true); }
    else if (!p && st == 2) { s->state = 1; sink->setPaused(false); }
}

void Engine::togglePause() {
    std::shared_ptr<Session> s;
    { std::lock_guard<std::mutex> l(mu); s = cur; }
    setPaused(!(s && s->state.load() == 2) && s != nullptr);
}

void Engine::next() {
    const int j = pickNext(false);
    if (j >= 0) start(j, 0, false);
}

void Engine::prev() {
    std::shared_ptr<Session> s;
    int i;
    { std::lock_guard<std::mutex> l(mu); s = cur; i = index; }
    if ((s && s->pos.load() > 5) || i <= 0) { if (s) s->seekTo = 0; else if (i >= 0) start(i, 0, false); }
    else start(i - 1, 0, false);
}

void Engine::seek(double sec) {
    std::lock_guard<std::mutex> l(mu);
    if (cur) cur->seekTo = std::max(0.0, sec);
}

void Engine::setVolume(int v) {
    vol = std::clamp(v, 0, 100);
    const float f = vol / 100.f;
    sink->setVolume(f * f);   // perceptual curve
}

void Engine::setMode(bool sh, int rep) {
    std::lock_guard<std::mutex> l(mu);
    shuffle = sh;
    repeat = std::clamp(rep, 0, 2);
    nextPick = -1;
}

void Engine::stop() {
    stopSession();
    std::lock_guard<std::mutex> l(mu);
    index = -1;
}

int Engine::pickNext(bool automatic) {
    std::lock_guard<std::mutex> l(mu);
    const int n = int(queue.size());
    if (n == 0) return -1;
    if (automatic && repeat == 2) return index;
    if (shuffle && n > 1) {
        if (nextPick >= 0 && nextPick < n && nextPick != index) return nextPick;
        static std::mt19937 rng{std::random_device{}()};
        int j;
        do { j = int(rng() % unsigned(n)); } while (j == index);
        nextPick = j;
        return j;
    }
    if (index + 1 < n) return index + 1;
    if (repeat == 1 || !automatic) return 0;
    return -1;
}

EngineStatus Engine::status() {
    EngineStatus st;
    std::lock_guard<std::mutex> l(mu);
    st.vol = vol;
    st.index = index;
    if (index >= 0 && index < int(queue.size())) st.vid = queue[size_t(index)].vid;
    if (!cur) { st.state = index >= 0 ? "paused" : "idle"; return st; }
    static const char *names[] = {"loading", "playing", "paused", "playing", "error"};
    st.state = names[std::clamp(cur->state.load(), 0, 4)];
    st.pos = cur->pos.load();
    st.dur = cur->dur.load() > 0 ? cur->dur.load() : cur->t.duration;
    st.err = cur->err;
    return st;
}

void Engine::tick() {
    std::shared_ptr<Session> s;
    { std::lock_guard<std::mutex> l(mu); s = cur; }
    if (!s) return;
    const int st = s->state.load();
    if (st == 3) {   // ended
        errorStreak = 0;
        const int j = pickNext(true);
        if (j >= 0) start(j, 0, false);
        else stop();
    } else if (st == 4) {
        bglog("error on %s: %s", s->t.vid.c_str(), s->err.c_str());
        // a stale URL: drop it and retry once from scratch
        {
            std::lock_guard<std::mutex> l(mu);
            if (index >= 0 && index < int(queue.size())) queue[size_t(index)].url.clear();
        }
        if (++errorStreak < 4) {
            const int j = pickNext(true);
            if (j >= 0) start(j, 0, false);
            else stop();
        } else stop();
    } else if (st == 1 && s->pos.load() > 4) {
        errorStreak = 0;
        lookAhead();
    }
}

// Resolve the next song's URL while this one plays, so the switch is quick.
void Engine::lookAhead() {
    if (aheadBusy) return;
    const int j = pickNext(true);
    std::string vid;
    {
        std::lock_guard<std::mutex> l(mu);
        if (j < 0 || j >= int(queue.size())) return;
        const BgTrack &t = queue[size_t(j)];
        if (!t.file.empty() || (!t.url.empty() && t.exp - now() > 900)) return;
        vid = t.vid;
    }
    if (vid == aheadVid) return;
    if (ahead.joinable()) ahead.join();
    aheadVid = vid;
    aheadBusy = true;
    ahead = std::thread([this, vid] {
        BgResolve::Result r = BgResolve::resolve(vid, visitor, root + "/clients.json");
        if (r.ok()) {
            std::lock_guard<std::mutex> l(mu);
            for (auto &t : queue)
                if (t.vid == vid) { t.url = r.url; t.ua = r.userAgent; t.len = r.contentLength; t.exp = r.expires; }
        }
        aheadBusy = false;
    });
}

// ------------------------------------------------------------- sessions
void Engine::stopSession() {
    std::shared_ptr<Session> s;
    { std::lock_guard<std::mutex> l(mu); s = cur; cur.reset(); }
    if (!s) return;
    s->cancel = true;
    s->cv.notify_all();
    if (s->fetcher.joinable()) s->fetcher.join();
    if (s->main.joinable()) s->main.join();
    sink->clear();
    sink->setPaused(false);
    if (!s->local) remove(s->cache.c_str());
}

void Engine::start(int i, double pos, bool paused) {
    stopSession();
    auto s = std::make_shared<Session>();
    {
        std::lock_guard<std::mutex> l(mu);
        if (i < 0 || i >= int(queue.size())) return;
        index = i;
        if (nextPick == i) nextPick = -1;
        s->t = queue[size_t(i)];
        cur = s;
    }
    s->startAt = pos;
    s->pos = pos;
    struct stat stt;
    if (!s->t.file.empty() && stat(s->t.file.c_str(), &stt) == 0) {
        s->local = true;
        s->cache = s->t.file;
    } else {
        s->cache = root + "/bg/cache" + std::to_string(flip ^= 1) + ".part";
    }
    if (paused) { s->state = 2; sink->setPaused(true); }
    s->main = std::thread([this, s] { runSession(s); });
}

void Engine::runSession(std::shared_ptr<Session> s) {
    if (s->local) {
        struct stat stt;
        stat(s->cache.c_str(), &stt);
        std::lock_guard<std::mutex> l(s->m);
        s->written = s->total = stt.st_size;
        s->fetchDone = true;
    } else {
        if (s->t.url.empty() || s->t.exp - now() < 600) {
            BgResolve::Result r = BgResolve::resolve(s->t.vid, visitor, root + "/clients.json");
            if (s->cancel) return;
            if (!r.ok()) { s->err = r.error; s->state = 4; return; }
            if (!r.visitor.empty() && visitor.empty()) visitor = r.visitor;
            s->t.url = r.url;
            s->t.ua = r.userAgent;
            s->t.len = r.contentLength;
            s->t.exp = r.expires;
            std::lock_guard<std::mutex> l(mu);
            for (auto &t : queue)
                if (t.vid == s->t.vid) { t.url = r.url; t.ua = r.userAgent; t.len = r.contentLength; t.exp = r.expires; }
        }
        { std::lock_guard<std::mutex> l(s->m); s->total = s->t.len > 0 ? s->t.len : -1; }
        s->fetcher = std::thread([this, s] { fetchLoop(s); });
    }
    decodeLoop(s);
}

void Engine::fetchLoop(std::shared_ptr<Session> s) {
    FILE *f = fopen(s->cache.c_str(), "wb");
    auto finish = [&](bool failed, const std::string &e) {
        std::lock_guard<std::mutex> l(s->m);
        s->fetchDone = true;
        s->fetchFailed = failed;
        s->fetchErr = e;
        if (!failed) s->total = s->written;
        s->cv.notify_all();
    };
    if (!f) { finish(true, "can't write the cache file"); return; }
    constexpr int64_t kChunk = 512 * 1024;
    int retries = 0;
    while (!s->cancel) {
        int64_t have, total;
        { std::lock_guard<std::mutex> l(s->m); have = s->written; total = s->total; }
        if (total > 0 && have >= total) break;
        const int64_t end = (total > 0 ? std::min(total, have + kChunk) : have + kChunk) - 1;
        int64_t got = 0;
        std::string e;
        const long code = Net::stream(s->t.url + "&range=" + std::to_string(have) + "-" + std::to_string(end),
                                      [&](const char *p, size_t n) {
                                          if (fwrite(p, 1, n, f) != n) return false;
                                          got += int64_t(n);
                                          if (got % (64 * 1024) < int64_t(n)) {   // make data visible to the reader regularly
                                              fflush(f);
                                              std::lock_guard<std::mutex> l(s->m);
                                              s->written = have + got;
                                              s->cv.notify_all();
                                          }
                                          return true;
                                      },
                                      &s->cancel, &e, {{"User-Agent", s->t.ua}});
        fflush(f);
        { std::lock_guard<std::mutex> l(s->m); s->written = have + got; s->cv.notify_all(); }
        if (s->cancel) break;
        if (code == 200 || code == 206) {
            retries = 0;
            if (total <= 0 && got < kChunk) break;
            continue;
        }
        if (++retries > 4) {
            fclose(f);
            finish(true, code ? "stream refused (HTTP " + std::to_string(code) + ")" : "network: " + e);
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500 * retries));
    }
    fclose(f);
    finish(false, {});
}

// ---- AVIO over the (growing) cache file
static int avRead(void *op, uint8_t *buf, int size) {
    auto *s = static_cast<Engine::Session *>(op);
    std::unique_lock<std::mutex> l(s->m);
    s->cv.wait_for(l, 30s, [&] { return s->cancel.load() || s->readPos < s->written || s->fetchDone; });
    if (s->cancel) return AVERROR_EXIT;
    const int64_t avail = s->written - s->readPos;
    if (avail <= 0) return AVERROR_EOF;
    const int n = int(std::min<int64_t>(avail, size));
    const int64_t at = s->readPos;
    l.unlock();
    fseek(s->rf, long(at), SEEK_SET);
    const size_t got = fread(buf, 1, size_t(n), s->rf);
    l.lock();
    if (got == 0) return AVERROR_EOF;
    s->readPos = at + int64_t(got);
    return int(got);
}

static int64_t avSeek(void *op, int64_t off, int whence) {
    auto *s = static_cast<Engine::Session *>(op);
    std::unique_lock<std::mutex> l(s->m);
    if (whence & AVSEEK_SIZE) return s->total;
    int64_t np;
    switch (whence & ~AVSEEK_FORCE) {
    case SEEK_SET: np = off; break;
    case SEEK_CUR: np = s->readPos + off; break;
    case SEEK_END: if (s->total < 0) return -1; np = s->total + off; break;
    default: return -1;
    }
    if (np < 0 || (s->total >= 0 && np > s->total)) return -1;
    s->cv.wait(l, [&] { return s->cancel.load() || np <= s->written || s->fetchDone; });
    if (s->cancel || np > s->written) return -1;
    s->readPos = np;
    return np;
}

void Engine::decodeLoop(std::shared_ptr<Session> s) {
    auto fail = [s](const std::string &e) {
        if (s->cancel) return;
        s->err = e;
        s->state = 4;
    };
    {   // wait for the first bytes, then open a read handle
        std::unique_lock<std::mutex> l(s->m);
        s->cv.wait(l, [&] { return s->cancel.load() || s->written > 0 || s->fetchDone; });
        if (s->cancel) return;
        if (s->written == 0) { fail(s->fetchFailed ? s->fetchErr : "empty stream"); return; }
    }
    s->rf = fopen(s->cache.c_str(), "rb");
    if (!s->rf) { fail("can't read the cache file"); return; }

    const int ioSize = 32 * 1024;
    auto *ioBuf = static_cast<uint8_t *>(av_malloc(ioSize));
    AVIOContext *io = avio_alloc_context(ioBuf, ioSize, 0, s.get(), avRead, nullptr, avSeek);
    AVFormatContext *fmt = avformat_alloc_context();
    fmt->pb = io;
    fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    fmt->probesize = 64 * 1024;
    AVCodecContext *cc = nullptr;
    SwrContext *swr = nullptr;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    std::vector<int16_t> out;
    auto cleanup = [&] {
        av_frame_free(&frame);
        av_packet_free(&pkt);
        swr_free(&swr);
        avcodec_free_context(&cc);
        avformat_close_input(&fmt);
        if (io) { av_freep(&io->buffer); avio_context_free(&io); }
        if (s->rf) { fclose(s->rf); s->rf = nullptr; }
    };

    if (avformat_open_input(&fmt, nullptr, nullptr, nullptr) < 0) { fail(s->fetchFailed ? s->fetchErr : "unsupported audio format"); cleanup(); return; }
    if (avformat_find_stream_info(fmt, nullptr) < 0) { fail("can't read the audio stream"); cleanup(); return; }
    const AVCodec *codec = nullptr;
    const int aidx = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (aidx < 0 || !codec) { fail("no playable audio"); cleanup(); return; }
    AVStream *as = fmt->streams[aidx];
    cc = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(cc, as->codecpar);
    cc->thread_count = 1;
    if (avcodec_open2(cc, codec, nullptr) < 0) { fail("decoder failed"); cleanup(); return; }
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_S16, Sink::kRate, &cc->ch_layout, cc->sample_fmt, cc->sample_rate, 0, nullptr);
    if (!swr || swr_init(swr) < 0) { fail("resampler failed"); cleanup(); return; }

    double d = fmt->duration > 0 ? double(fmt->duration) / AV_TIME_BASE : 0;
    if (d <= 0 && as->duration > 0) d = as->duration * av_q2d(as->time_base);
    s->dur = d;
    const double tb = av_q2d(as->time_base);
    double lastPts = s->startAt;

    double skipUntil = -1;   // after a seek: drop audio before the target (seeks land on the previous keyframe)
    auto doSeek = [&](double t) {
        av_seek_frame(fmt, aidx, int64_t(t / tb), AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(cc);
        sink->clear();
        lastPts = t;
        s->pos = t;
        skipUntil = t;
    };
    if (s->startAt > 0.5) doSeek(s->startAt);

    bool eof = false, needFlush = false;
    const int maxQueued = Sink::kRate / 2;   // ~0.5 s ahead
    while (!s->cancel) {
        const double sk = s->seekTo.exchange(-1);
        if (sk >= 0) { doSeek(sk); eof = false; if (s->state == 3) s->state = 1; }
        const int queued = sink->queuedFrames();
        if (s->state != 0 && skipUntil < 0) s->pos = std::max(0.0, lastPts - double(queued) / Sink::kRate);
        if (s->state == 2 || queued > maxQueued) { std::this_thread::sleep_for(10ms); continue; }
        if (eof) {
            if (needFlush && queued > 0) { sink->flush(); needFlush = false; }
            if (queued == 0) { s->state = 3; std::this_thread::sleep_for(30ms); }
            else std::this_thread::sleep_for(10ms);
            continue;
        }
        const int r = av_read_frame(fmt, pkt);
        if (r < 0) {
            if (s->cancel) break;
            bool failed;
            std::string why;
            { std::lock_guard<std::mutex> l(s->m); failed = s->fetchFailed; why = s->fetchErr; }
            if (failed) { fail(why); break; }
            avcodec_send_packet(cc, nullptr);
            eof = true;
            needFlush = true;
        } else if (pkt->stream_index != aidx) {
            av_packet_unref(pkt);
            continue;
        } else {
            avcodec_send_packet(cc, pkt);
            av_packet_unref(pkt);
        }
        while (avcodec_receive_frame(cc, frame) == 0) {
            if (frame->pts != AV_NOPTS_VALUE) lastPts = frame->pts * tb + double(frame->nb_samples) / cc->sample_rate;
            else lastPts += double(frame->nb_samples) / cc->sample_rate;
            if (skipUntil >= 0) {
                if (lastPts < skipUntil) continue;   // whole frame before the target
                skipUntil = -1;
            }
            const int maxOut = swr_get_out_samples(swr, frame->nb_samples);
            out.resize(size_t(maxOut) * 2);
            uint8_t *op = reinterpret_cast<uint8_t *>(out.data());
            const int got = swr_convert(swr, &op, maxOut, const_cast<const uint8_t **>(frame->extended_data), frame->nb_samples);
            if (got > 0) sink->write(out.data(), got);
            if (s->state == 0) s->state = 1;
        }
    }
    cleanup();
}
