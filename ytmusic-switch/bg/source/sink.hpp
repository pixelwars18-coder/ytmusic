#pragma once
#include <cstdint>

// Where decoded audio goes: the console's audio renderer in the sysmodule, SDL in the Linux test.
// Always 48 kHz, stereo, signed 16-bit.
struct Sink {
    virtual ~Sink() = default;
    virtual bool open() = 0;
    virtual void close() {}
    virtual int queuedFrames() = 0;                          // frames written but not played yet
    virtual void write(const int16_t *pcm, int frames) = 0;  // may block briefly; caller keeps the queue short
    virtual void clear() = 0;                                // drop everything queued (seek / stop)
    virtual void setPaused(bool p) = 0;
    virtual void setVolume(float v) = 0;                     // 0..1
    virtual void flush() {}                                  // submit a partly filled buffer (end of song)
    static constexpr int kRate = 48000;
};
