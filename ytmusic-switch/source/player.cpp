#include "player.hpp"
#include "common.hpp"
#include "net.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
}

using namespace std::chrono_literals;

struct Player::Session {
    // source
    std::string url, path, ua;
    bool isFile = false;
    double startAt = 0;
    // buffer (filled sequentially from byte 0)
    std::mutex m;
    std::condition_variable cv;
    std::vector<uint8_t> data;
    int64_t total = -1;        // known size, -1 unknown
    bool fetchDone = false, fetchFailed = false;
    std::string fetchErr;
    int64_t readPos = 0;       // AVIO cursor
    // control
    std::atomic<bool> cancel{false};
    std::atomic<double> seekTo{-1};
    std::thread fetcher, decoder;
};

bool Player::init() {
    SDL_AudioSpec want{}, have{};
    want.freq = 48000;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 2048;
    dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!dev) { logf("SDL_OpenAudioDevice: %s", SDL_GetError()); return false; }
    rate = have.freq;
    SDL_PauseAudioDevice(dev, 0);
    return true;
}

void Player::shutdown() {
    stop();
    if (dev) SDL_CloseAudioDevice(dev);
    dev = 0;
}

void Player::stop() {
    std::shared_ptr<Session> s;
    { std::lock_guard<std::mutex> l(mu); s = cur; cur.reset(); }
    if (s) {
        s->cancel = true;
        s->cv.notify_all();
        if (s->fetcher.joinable()) s->fetcher.join();
        if (s->decoder.joinable()) s->decoder.join();
    }
    if (dev) SDL_ClearQueuedAudio(dev);
    st = Idle;
    posBase = 0;
    dur = 0;
}

void Player::openUrl(const std::string &url, int64_t len, const std::string &ua, double startAt) {
    stop();
    auto s = std::make_shared<Session>();
    s->url = url;
    s->ua = ua;
    s->total = len > 0 ? len : -1;
    s->startAt = startAt;
    startSession(s);
}

void Player::openFile(const std::string &path, double startAt) {
    stop();
    auto s = std::make_shared<Session>();
    s->path = path;
    s->isFile = true;
    s->startAt = startAt;
    startSession(s);
}

void Player::startSession(std::shared_ptr<Session> s) {
    { std::lock_guard<std::mutex> l(mu); cur = s; err.clear(); }
    st = Loading;
    posBase = s->startAt;
    if (s->total > 0) s->data.reserve(size_t(s->total));
    s->fetcher = std::thread([this, s] { fetchLoop(s); });
    s->decoder = std::thread([this, s] { decodeLoop(s); });
}

void Player::fetchLoop(std::shared_ptr<Session> s) {
    auto append = [s](const char *p, size_t n) {
        std::lock_guard<std::mutex> l(s->m);
        s->data.insert(s->data.end(), reinterpret_cast<const uint8_t *>(p), reinterpret_cast<const uint8_t *>(p) + n);
        s->cv.notify_all();
        return !s->cancel.load();
    };
    auto finish = [s](bool failed, const std::string &e) {
        std::lock_guard<std::mutex> l(s->m);
        s->fetchDone = true;
        s->fetchFailed = failed;
        s->fetchErr = e;
        if (!failed) s->total = int64_t(s->data.size());
        s->cv.notify_all();
    };

    if (s->isFile) {
        FILE *f = fopen(s->path.c_str(), "rb");
        if (!f) { finish(true, "Can't open the downloaded file"); return; }
        fseek(f, 0, SEEK_END);
        const long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        { std::lock_guard<std::mutex> l(s->m); s->total = sz; s->data.reserve(size_t(sz)); }
        std::vector<char> buf(256 * 1024);
        size_t n;
        while (!s->cancel && (n = fread(buf.data(), 1, buf.size(), f)) > 0) append(buf.data(), n);
        fclose(f);
        finish(false, {});
        return;
    }

    constexpr int64_t kChunk = 1 << 20;
    int retries = 0;
    while (!s->cancel) {
        int64_t have;
        int64_t total;
        { std::lock_guard<std::mutex> l(s->m); have = int64_t(s->data.size()); total = s->total; }
        if (total > 0 && have >= total) break;
        const int64_t end = total > 0 ? std::min(total, have + kChunk) - 1 : have + kChunk - 1;
        std::string e;
        const size_t before = size_t(have);
        const long code = Net::stream(s->url + "&range=" + std::to_string(have) + "-" + std::to_string(end), append, &s->cancel, &e,
                                      {{"User-Agent", s->ua}});
        if (s->cancel) return;
        size_t after;
        { std::lock_guard<std::mutex> l(s->m); after = s->data.size(); }
        if (code == 200 || code == 206) {
            retries = 0;
            if (total <= 0 && after - before < size_t(kChunk)) break;   // unknown size and a short chunk: that was the end
            continue;
        }
        if (++retries > 4) {
            finish(true, code ? "Stream refused (HTTP " + std::to_string(code) + ")" : "Network error: " + e);
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(400 * retries));
    }
    finish(false, {});
}

// ---- AVIO over the session buffer
static int avRead(void *op, uint8_t *buf, int size) {
    auto *s = static_cast<Player::Session *>(op);
    std::unique_lock<std::mutex> l(s->m);
    s->cv.wait(l, [&] { return s->cancel.load() || s->readPos < int64_t(s->data.size()) || s->fetchDone; });
    if (s->cancel) return AVERROR_EXIT;
    const int64_t avail = int64_t(s->data.size()) - s->readPos;
    if (avail <= 0) return AVERROR_EOF;
    const int n = int(std::min<int64_t>(avail, size));
    memcpy(buf, s->data.data() + s->readPos, size_t(n));
    s->readPos += n;
    return n;
}

static int64_t avSeek(void *op, int64_t off, int whence) {
    auto *s = static_cast<Player::Session *>(op);
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
    // MP4 "moov" can sit at the end: seeking there means waiting for the fetcher to get that far
    s->cv.wait(l, [&] { return s->cancel.load() || np <= int64_t(s->data.size()) || s->fetchDone; });
    if (s->cancel || np > int64_t(s->data.size())) return -1;
    s->readPos = np;
    return np;
}

void Player::decodeLoop(std::shared_ptr<Session> s) {
    auto fail = [this, s](const std::string &e) {
        if (s->cancel) return;
        { std::lock_guard<std::mutex> l(mu); err = e; }
        st = Error;
        logf("player: %s", e.c_str());
    };

    const int ioSize = 64 * 1024;
    auto *ioBuf = static_cast<uint8_t *>(av_malloc(ioSize));
    AVIOContext *io = avio_alloc_context(ioBuf, ioSize, 0, s.get(), avRead, nullptr, avSeek);
    AVFormatContext *fmt = avformat_alloc_context();
    fmt->pb = io;
    fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    AVCodecContext *cc = nullptr;
    SwrContext *swr = nullptr;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    int aidx = -1;
    std::vector<int16_t> out;

    auto cleanup = [&] {
        av_frame_free(&frame);
        av_packet_free(&pkt);
        swr_free(&swr);
        avcodec_free_context(&cc);
        avformat_close_input(&fmt);   // also frees fmt
        if (io) { av_freep(&io->buffer); avio_context_free(&io); }
    };

    if (avformat_open_input(&fmt, nullptr, nullptr, nullptr) < 0) {
        std::string why;
        { std::lock_guard<std::mutex> l(s->m); why = s->fetchFailed ? s->fetchErr : "Unsupported audio format"; }
        fail(why);
        cleanup();
        return;
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) { fail("Can't read the audio stream"); cleanup(); return; }
    const AVCodec *codec = nullptr;
    aidx = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (aidx < 0 || !codec) { fail("No playable audio in this file"); cleanup(); return; }
    AVStream *as = fmt->streams[aidx];
    cc = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(cc, as->codecpar);
    if (avcodec_open2(cc, codec, nullptr) < 0) { fail("Audio decoder failed"); cleanup(); return; }

    AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
    swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_S16, rate, &cc->ch_layout, cc->sample_fmt, cc->sample_rate, 0, nullptr);
    if (!swr || swr_init(swr) < 0) { fail("Resampler failed"); cleanup(); return; }

    double d = fmt->duration > 0 ? double(fmt->duration) / AV_TIME_BASE : 0;
    if (d <= 0 && as->duration > 0) d = as->duration * av_q2d(as->time_base);
    dur = d;

    const double tb = av_q2d(as->time_base);
    const int bytesPerSec = rate * 2 * 2;
    double lastPts = s->startAt;

    double skipUntil = -1;   // drop audio before a seek target (seeks land on the previous keyframe)
    auto doSeek = [&](double t) {
        const int64_t ts = int64_t(t / tb);
        av_seek_frame(fmt, aidx, ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(cc);
        SDL_ClearQueuedAudio(dev);
        lastPts = t;
        posBase = t;
        skipUntil = t;
    };
    if (s->startAt > 0.5) doSeek(s->startAt);

    bool eof = false;
    while (!s->cancel) {
        const double sk = s->seekTo.exchange(-1);
        if (sk >= 0) { doSeek(sk); eof = false; if (st == Ended) st = Playing; }

        // keep ~0.6 s queued; position = last decoded pts minus what is still queued
        const Uint32 queued = SDL_GetQueuedAudioSize(dev);
        if (skipUntil < 0) posBase = std::max(0.0, lastPts - double(queued) / bytesPerSec);
        if (st == Paused || queued > Uint32(bytesPerSec * 0.6)) {
            std::this_thread::sleep_for(10ms);
            continue;
        }
        if (eof) {
            if (queued == 0) { st = Ended; std::this_thread::sleep_for(20ms); }
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
            avcodec_send_packet(cc, nullptr);   // drain
            eof = true;
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
                if (lastPts < skipUntil) continue;
                skipUntil = -1;
            }
            const int maxOut = swr_get_out_samples(swr, frame->nb_samples);
            out.resize(size_t(maxOut) * 2);
            uint8_t *op = reinterpret_cast<uint8_t *>(out.data());
            const int got = swr_convert(swr, &op, maxOut, const_cast<const uint8_t **>(frame->extended_data), frame->nb_samples);
            if (got > 0) {
                const int v = vol.load();
                if (v != 100) {
                    const int mul = v * v * 256 / 10000;   // perceptual-ish curve, fixed point /256
                    for (int i = 0; i < got * 2; ++i) out[size_t(i)] = int16_t((int(out[size_t(i)]) * mul) >> 8);
                }
                SDL_QueueAudio(dev, out.data(), Uint32(got) * 4);
            }
            if (st == Loading) st = Playing;
        }
    }
    cleanup();
}

void Player::setPaused(bool p) {
    const State s = st.load();
    if (p && s == Playing) { st = Paused; SDL_PauseAudioDevice(dev, 1); }
    else if (!p && s == Paused) { st = Playing; SDL_PauseAudioDevice(dev, 0); }
}

void Player::seek(double sec) {
    std::lock_guard<std::mutex> l(mu);
    if (cur) cur->seekTo = std::max(0.0, sec);
}

void Player::setVolume(int v) { vol = std::clamp(v, 0, 100); }

double Player::position() const { return posBase.load(); }

double Player::buffered() const {
    std::lock_guard<std::mutex> l(mu);
    if (!cur) return 0;
    std::lock_guard<std::mutex> l2(cur->m);
    if (cur->total <= 0) return cur->fetchDone ? 1.0 : 0.0;
    return double(cur->data.size()) / double(cur->total);
}

std::string Player::error() const {
    std::lock_guard<std::mutex> l(mu);
    return err;
}
