// Audio output for the sysmodule through the console's audio renderer (works alongside games).
#ifdef __SWITCH__
#include "sink.hpp"
#include <switch.h>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <mutex>

class AudrenSink : public Sink {
public:
    bool open() override {
        static const AudioRendererConfig cfg = {
            .output_rate = AudioRendererOutputRate_48kHz,
            .num_voices = 2,
            .num_effects = 0,
            .num_sinks = 1,
            .num_mix_objs = 1,
            .num_mix_buffers = 2,
        };
        if (R_FAILED(audrenInitialize(&cfg))) return false;
        if (R_FAILED(audrvCreate(&drv, &cfg, 2))) { audrenExit(); return false; }
        poolSize = (kBufs * kFrames * 4 + (AUDREN_MEMPOOL_ALIGNMENT - 1)) & ~(AUDREN_MEMPOOL_ALIGNMENT - 1);
        pool = static_cast<int16_t *>(aligned_alloc(AUDREN_MEMPOOL_ALIGNMENT, poolSize));
        if (!pool) return false;
        memset(pool, 0, poolSize);
        armDCacheFlush(pool, poolSize);
        const int mp = audrvMemPoolAdd(&drv, pool, poolSize);
        audrvMemPoolAttach(&drv, mp);
        static const u8 channels[2] = {0, 1};
        audrvDeviceSinkAdd(&drv, AUDREN_DEFAULT_DEVICE_NAME, 2, channels);
        audrvUpdate(&drv);
        audrenStartAudioRenderer();
        audrvVoiceInit(&drv, 0, 2, PcmFormat_Int16, kRate);
        audrvVoiceSetDestinationMix(&drv, 0, AUDREN_FINAL_MIX_ID);
        audrvVoiceSetMixFactor(&drv, 0, 1.0f, 0, 0);
        audrvVoiceSetMixFactor(&drv, 0, 0.0f, 0, 1);
        audrvVoiceSetMixFactor(&drv, 0, 0.0f, 1, 0);
        audrvVoiceSetMixFactor(&drv, 0, 1.0f, 1, 1);
        audrvVoiceSetVolume(&drv, 0, volume);
        audrvVoiceStart(&drv, 0);
        for (int i = 0; i < kBufs; ++i) {
            memset(&wb[i], 0, sizeof wb[i]);
            wb[i].data_pcm16 = pool + i * kFrames * 2;
            wb[i].size = kFrames * 4;
            wb[i].start_sample_offset = 0;
            wb[i].end_sample_offset = kFrames;
            wb[i].state = AudioDriverWaveBufState_Done;
        }
        audrvUpdate(&drv);
        ok = true;
        return true;
    }

    void close() override {
        if (!ok) return;
        std::lock_guard<std::mutex> l(mu);
        audrvVoiceStop(&drv, 0);
        audrvUpdate(&drv);
        audrvClose(&drv);
        audrenExit();
        free(pool);
        ok = false;
    }

    int queuedFrames() override {
        if (!ok) return 0;
        std::lock_guard<std::mutex> l(mu);
        audrvUpdate(&drv);
        int n = 0;
        for (int i = 0; i < kBufs; ++i)
            if (wb[i].state == AudioDriverWaveBufState_Queued || wb[i].state == AudioDriverWaveBufState_Playing)
                n += wb[i].end_sample_offset - wb[i].start_sample_offset;
        return n + fill;   // + the partly filled buffer not submitted yet
    }

    // Copies into the current wave buffer; submits it when full. Blocks (briefly) when all buffers are in flight.
    void write(const int16_t *pcm, int frames) override {
        while (frames > 0 && ok) {
            int slot = -1;
            {
                std::lock_guard<std::mutex> l(mu);
                audrvUpdate(&drv);
                if (curBuf >= 0) slot = curBuf;
                else
                    for (int i = 0; i < kBufs; ++i)
                        if (wb[i].state == AudioDriverWaveBufState_Free || wb[i].state == AudioDriverWaveBufState_Done) { slot = i; curBuf = i; fill = 0; break; }
                if (slot >= 0) {
                    const int n = std::min(frames, kFrames - fill);
                    memcpy(wb[slot].data_pcm16 + fill * 2, pcm, size_t(n) * 4);
                    fill += n;
                    pcm += n * 2;
                    frames -= n;
                    if (fill == kFrames) submit();
                }
            }
            if (slot < 0) svcSleepThread(5'000'000);
        }
    }

    void clear() override {
        if (!ok) return;
        std::lock_guard<std::mutex> l(mu);
        audrvVoiceStop(&drv, 0);
        audrvUpdate(&drv);
        for (int i = 0; i < kBufs; ++i) wb[i].state = AudioDriverWaveBufState_Done;
        curBuf = -1;
        fill = 0;
        audrvVoiceStart(&drv, 0);
        audrvUpdate(&drv);
    }

    void setPaused(bool p) override {
        if (!ok) return;
        std::lock_guard<std::mutex> l(mu);
        audrvVoiceSetPaused(&drv, 0, p);
        audrvUpdate(&drv);
    }

    void setVolume(float v) override {
        volume = v;
        if (!ok) return;
        std::lock_guard<std::mutex> l(mu);
        audrvVoiceSetVolume(&drv, 0, v);
        audrvUpdate(&drv);
    }

    // a partly filled buffer is submitted when the decoder pauses for breath (end of song)
    void flush() override {
        std::lock_guard<std::mutex> l(mu);
        if (curBuf >= 0 && fill > 0) submit();
    }

private:
    void submit() {   // mu held
        AudioDriverWaveBuf &b = wb[curBuf];
        b.end_sample_offset = fill;
        armDCacheFlush(b.data_pcm16, size_t(fill) * 4);
        audrvVoiceAddWaveBuf(&drv, 0, &b);
        if (!audrvVoiceIsPlaying(&drv, 0)) audrvVoiceStart(&drv, 0);
        audrvUpdate(&drv);
        curBuf = -1;
        fill = 0;
    }

    static constexpr int kBufs = 6, kFrames = 2048;   // 6 x 43 ms
    AudioDriver drv{};
    AudioDriverWaveBuf wb[kBufs];
    int16_t *pool = nullptr;
    size_t poolSize = 0;
    int curBuf = -1, fill = 0;
    float volume = 0.64f;
    bool ok = false;
    std::mutex mu;
};

Sink *makeAudrenSink() { return new AudrenSink(); }
#endif
