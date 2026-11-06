// Sequencer tracks mod (plan 016 B) - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Warp settings of the sample instruments: the loop length in bars, the auto-guess, the stretch ratio and the sidecar file
// seqwarp.bin. No Polyend includes, host-testable (core/seqtest/test_warp.cpp).
//
//   Per sample instrument: Warp off / on, and "Bars" = the length in bars of the played region of the sample (start .. end
//   point of the instrument). Bars 0 = Auto: the guess at the project tempo of the note, the sample length rounded to the
//   nearest of 1 / 2 / 4 / 8 / 16 bars. The stretch ratio (source frames per output frame) is
//        ratio = region seconds / (bars * 4 * 60 / BPM) = K * BPM,   K = region frames / (bars * 240 * sample rate)
//   so a tempo change only scales the ratio.
//
//   Sidecar "seqwarp.bin" (frame of seqSidecarLogic.h, magic "SWRP", version 1): payload = 2 bytes per instrument (on 0/1,
//   bars 0..32). The stock project struct is not changed; a project without the file has Warp off everywhere.
#pragma once
#include "seqSidecarLogic.h"
#include <math.h>
#include <stddef.h>
#include <stdint.h>

namespace seqwarp {

enum : uint8_t { kCount = 48, kBarsMax = 32, kMaxVoices = 2 };
constexpr float kRate = 44117.64706f; // AUDIO_SAMPLE_RATE_EXACT
constexpr float kRatioMin = 0.25f, kRatioMax = 4.0f;

// stream manager constants the warp read window relies on (static_assert'ed against VoiceStreamManager in play_memory_warp.cpp)
constexpr int32_t kRingFrames = 16384, kLookbehind = 512, kLoopWrapFrames = 4096;

// bars of a region of `frames` at `bpm` before rounding
inline float barsExact(float frames, float bpm)
{
    return frames / kRate * bpm / 240.0f;
}

// nearest of 1 / 2 / 4 / 8 / 16 (in the octave domain: 3 bars is nearer to 4 than to 2, 2.8 is nearer to 2)
inline uint8_t guessBars(float frames, float bpm)
{
    float e = barsExact(frames, bpm);
    if (!(e > 1.0f))
        return 1;
    uint8_t best = 1;
    float bestD = 1e9f;
    for (uint8_t c = 1; c <= 16; c = (uint8_t)(c * 2))
    {
        float d = fabsf(log2f(e) - log2f((float)c));
        if (d < bestD)
        {
            bestD = d;
            best = c;
        }
    }
    return best;
}

inline uint8_t clampBars(int v) { return (uint8_t)(v < 0 ? 0 : (v > kBarsMax ? kBarsMax : v)); }

// the bars a note really uses: the setting, or the guess for 0 (Auto)
inline uint8_t resolveBars(uint8_t setting, float frames, float bpm)
{
    return setting ? clampBars(setting) : guessBars(frames, bpm);
}

// K of the ratio (see above); bars >= 1
inline float ratioK(float frames, uint8_t bars)
{
    return frames / ((float)(bars ? bars : 1) * 240.0f * kRate);
}
inline float ratioFor(float k, float bpm)
{
    float r = k * bpm;
    return r < kRatioMin ? kRatioMin : (r > kRatioMax ? kRatioMax : r);
}

// Jog / arrows on the Bars value: Auto(0), 1, 2, 3 ... 32. A step always moves, clamped at both ends.
inline uint8_t stepBars(uint8_t bars, int steps) { return clampBars((int)clampBars(bars) + steps); }

// frames a grain can reach from the stream playhead (the older grain): the stream ring and the loop-wrap cache must cover it
inline int32_t grainSpan(int32_t hop, float pitch)
{
    return (int32_t)(2.0f * (float)hop * pitch) + 3;
}

struct Item
{
    uint8_t on = 0;   // Warp
    uint8_t bars = 0; // 0 = Auto
};

inline const char* magic() { return "SWRP"; }
enum : uint8_t { kVersion = 1 };
enum : size_t { kPayload = (size_t)kCount * 2, kFileMax = (size_t)kPayload + seqsidecar::kOverhead };

struct Params
{
    Item it[kCount];

    bool operator==(const Params& o) const
    {
        for (int i = 0; i < kCount; i++)
            if (it[i].on != o.it[i].on || it[i].bars != o.it[i].bars)
                return false;
        return true;
    }
    bool isDefault() const
    {
        for (int i = 0; i < kCount; i++)
            if (it[i].on || it[i].bars)
                return false;
        return true;
    }
    void raw(uint8_t* p) const
    {
        for (int i = 0; i < kCount; i++)
        {
            p[2 * i] = it[i].on ? 1 : 0;
            p[2 * i + 1] = it[i].bars;
        }
    }
    uint16_t hash() const
    {
        uint8_t b[kPayload];
        raw(b);
        return seqsidecar::crc16(b, kPayload);
    }
    // frame -> buf (>= kFileMax); 0 = buffer too small
    size_t save(uint8_t* buf, size_t cap) const
    {
        if (cap < (size_t)kFileMax)
            return 0;
        uint8_t* p = buf + seqsidecar::kHeader;
        raw(p);
        return seqsidecar::frameWrite(buf, cap, magic(), kVersion, p, kPayload);
    }
    // false: missing / bad frame (nothing changed). *newer: written by a newer mod version (leave the file alone). A short
    // payload leaves the remaining instruments off; a longer one (later version) is read as far as it is known; a value out
    // of range falls back to its own default.
    bool load(const uint8_t* buf, size_t len, bool* newer = nullptr)
    {
        const uint8_t* p = nullptr;
        size_t n = 0;
        uint8_t v = 0;
        seqsidecar::FrameStatus st = seqsidecar::frameRead(buf, len, magic(), kVersion, &p, &n, &v);
        if (newer)
            *newer = st == seqsidecar::FRAME_NEWER;
        if (st != seqsidecar::FRAME_OK)
            return false;
        for (int i = 0; i < kCount; i++)
        {
            it[i].on = 0;
            it[i].bars = 0;
            if ((size_t)(2 * i + 1) < n)
            {
                it[i].on = p[2 * i] == 1 ? 1 : 0;
                it[i].bars = p[2 * i + 1] <= kBarsMax ? p[2 * i + 1] : (uint8_t)0;
            }
        }
        return true;
    }
};

}  // namespace seqwarp
