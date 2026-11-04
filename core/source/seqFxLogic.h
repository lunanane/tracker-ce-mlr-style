// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Parameters of the v0.2.4 FX additions (plan 007), their mappings and the sidecar file seqfx.bin. No Polyend includes,
// host-testable (core/seqtest/test_fx.cpp).
//
//   F1  ping-pong delay filter = bandpass. Centre = the stock project value delayFilterFrequency (100 .. 20000 Hz); the top of the
//       range (>= kOpenHz) is "open": the filter is out of the signal path, exactly the stock "no filtering" setting. Width =
//       bwTwelfths (in 1/12 octave, 4 .. 36 = 1/3 .. 3 octaves, default 12 = 1 octave), saved in the sidecar.
//   F2  reverb highpass (low cut) on the reverb input. hpIdx 0 = off, 1 .. 72 = 30 Hz .. 1.8 kHz in semitone steps. Saved in the
//       sidecar.
//   F3  reverb type: 0 = Stock (the Polyend reverb, unchanged), 1 = Plinky (ring reverb ported from the Plinky firmware).
//       Saved in the sidecar; a project without the file (every project made before v0.2.4) is Stock.
//
// The stock project struct (strMtValues) is NOT changed: the new values live in the sidecar, like the clip modes.
#pragma once
#include "ring_reverb.h"
#include "seqFxDsp.h"
#include "seqSidecarLogic.h"
#include <stddef.h>
#include <stdint.h>

namespace seqfx {

// ---------------------------------------------------------------- F1: delay bandpass
enum : uint8_t { kBwMin = 4, kBwMax = 36, kBwDefault = 12 };      // 1/12 octave
constexpr uint16_t kCenterMinHz = 100, kCenterMaxHz = 20000;        // = DELAY_FILTER_FREQ_MIN / MAX (mtStructs.h)
constexpr uint16_t kOpenHz = 19000;                                 // centre >= this: filter bypassed

inline bool centerOpen(uint16_t hz) { return hz >= kOpenHz; }
inline uint8_t clampBw(int v) { return (uint8_t)(v < kBwMin ? kBwMin : (v > kBwMax ? kBwMax : v)); }

// Bandwidth (octaves) -> Q of the 2-pole bandpass: Q = sqrt(2^bw) / (2^bw - 1)
inline float bwToQ(float octaves)
{
    float r = exp2f(octaves);
    return sqrtf(r) / (r - 1.0f);
}
inline float bwOctaves(uint8_t twelfths) { return (float)clampBw(twelfths) / 12.0f; }

// Jog wheel: one step = one semitone (12 steps per octave), clamped to the project range. A step always moves.
inline uint16_t jogCenter(uint16_t hz, int steps)
{
    if (hz < kCenterMinHz)
        hz = kCenterMinHz;
    float f = (float)hz * exp2f((float)steps / 12.0f);
    int v = (int)(f + 0.5f);
    if (steps > 0 && v <= (int)hz)
        v = hz + 1;
    if (steps < 0 && v >= (int)hz)
        v = hz - 1;
    if (v < (int)kCenterMinHz)
        v = kCenterMinHz;
    if (v > (int)kCenterMaxHz)
        v = kCenterMaxHz;
    return (uint16_t)v;
}

// Shift + jog: one step = 1/12 octave of width
inline uint8_t jogWidth(uint8_t twelfths, int steps) { return clampBw((int)clampBw(twelfths) + steps); }

// ---------------------------------------------------------------- F2: reverb highpass
enum : uint8_t { kHpMax = 72 };
inline uint8_t clampHp(int v) { return (uint8_t)(v < 0 ? 0 : (v > kHpMax ? kHpMax : v)); }
inline float hpHz(uint8_t idx) { return idx == 0 ? 0.0f : 30.0f * exp2f((float)(clampHp(idx) - 1) / 12.0f); }
inline uint8_t jogHp(uint8_t idx, int steps) { return clampHp((int)clampHp(idx) + steps); }
constexpr float kHpQ = 0.7071f; // 12 dB / octave Butterworth

// ---------------------------------------------------------------- F3: reverb type and its Plinky-ring mapping
enum ReverbType : uint8_t { RT_STOCK = 0, RT_PLINKY = 1, RT_COUNT = 2 };
inline const char* reverbTypeName(uint8_t t) { return t == RT_PLINKY ? "Plinky" : "Stock"; }

// stock size (decay, 0..1)      -> fade: per-pass loop gain = fade / 256 (the same curve as the original time control)
inline int ringFade(float size)
{
    float t = size < 0.0f ? 0.0f : (size > 1.0f ? 1.0f : size);
    float f = 1.0f - t;
    f *= f;
    f *= f;
    return (int)(250.0f * (1.0f - f));
}
// stock damp (0..1, more = darker) -> one-pole coefficient of the two loop filters at 44.1 kHz.
// 0 = 18 kHz (the original is about 15 kHz), 0.5 = 6.4 kHz, 1 = 400 Hz
inline float ringColor(float damp)
{
    float d = damp < 0.0f ? 0.0f : (damp > 1.0f ? 1.0f : damp);
    float q = 1.0f - d;
    float fc = 18000.0f * q * sqrtf(q);
    if (fc < 400.0f)
        fc = 400.0f;
    return 1.0f - expf(-2.0f * kPi * fc / kFs);
}
// stock diffusion (0 .. 0.85, default 0.68) -> all-pass gain in Q15. The default 0.68 gives 0.5, the gain of the original.
inline int32_t ringApGain(float diffusion)
{
    float d = diffusion < 0.0f ? 0.0f : (diffusion > 0.85f ? 0.85f : diffusion);
    return (int32_t)(d * (0.5f / 0.68f) * 32768.0f + 0.5f);
}
// stock predelay (0..1) -> samples (the stereo pre-delay line of the ring reverb)
constexpr int kRingPredelayMax = (int)ringverb::kPreSize - 1; // 4095 (93 ms); 1023 on the Tracker Plus / Mini
inline int ringPredelay(float pre)
{
    float t = pre < 0.0f ? 0.0f : (pre > 1.0f ? 1.0f : pre);
    return (int)(t * (float)kRingPredelayMax);
}

// The reverb type switch, one step per audio block: fade the old type out (one block), keep the return silent while the memory of
// the new type is cleared in pieces, fade the new type in (one block). A request that changes meanwhile restarts the clearing.
struct TypeSwitch
{
    enum Phase : uint8_t { PH_RUN = 0, PH_FADE_OUT, PH_CLEAR, PH_FADE_IN };
    enum Act : uint8_t { ACT_RUN = 0, ACT_FADE_OUT, ACT_CLEAR, ACT_FADE_IN };
    uint8_t active = RT_STOCK;
    uint8_t phase = PH_RUN;
    uint8_t clearType = RT_STOCK;
    uint32_t clearOffset = 0;

    // what to do with this block. ACT_RUN / ACT_FADE_OUT / ACT_FADE_IN: process `active` (faded out / in for the two ramps).
    // ACT_CLEAR: the block is silent, clear a piece of the memory of `clearType` and call clearFinished() once all of it is clean.
    Act step(uint8_t requested)
    {
        if (phase == PH_RUN && requested != active)
            phase = PH_FADE_OUT;
        switch (phase)
        {
            case PH_FADE_OUT:
                phase = PH_CLEAR;
                clearType = requested;
                clearOffset = 0;
                return ACT_FADE_OUT;
            case PH_CLEAR:
                if (clearType != requested)
                {
                    clearType = requested;
                    clearOffset = 0;
                }
                return ACT_CLEAR;
            case PH_FADE_IN: phase = PH_RUN; return ACT_FADE_IN;
            default: return ACT_RUN;
        }
    }
    void clearFinished()
    {
        active = clearType;
        phase = PH_FADE_IN;
    }
    // hard reset (boot, clearReverb): straight to the requested type, the caller cleared the memory itself
    void jumpTo(uint8_t type)
    {
        active = type;
        phase = PH_RUN;
        clearOffset = 0;
    }
};

// A piece of up to `step` bytes of up to three memory segments (lens[0..n-1] bytes each, one after the other), advancing `offset`.
// Returns the number of bytes to clear at segment *seg + *start; 0 once everything is done.
inline uint32_t nextClearPiece(const uint32_t* lens, int n, uint32_t& offset, uint32_t step, int* seg, uint32_t* start)
{
    uint32_t base = 0;
    for (int s = 0; s < n; s++)
    {
        if (offset < base + lens[s])
        {
            uint32_t room = base + lens[s] - offset;
            uint32_t m = room < step ? room : step;
            *seg = s;
            *start = offset - base;
            offset += m;
            return m;
        }
        base += lens[s];
    }
    return 0;
}
inline uint32_t clearTotal(const uint32_t* lens, int n)
{
    uint32_t t = 0;
    for (int s = 0; s < n; s++)
        t += lens[s];
    return t;
}

// ---------------------------------------------------------------- sidecar seqfx.bin
constexpr char kMagic[4] = { 'S', 'Q', 'F', 'X' };
enum { kVersion = 1, kPayload = 8, kFileMax = kPayload + seqsidecar::kOverhead };

struct Params
{
    uint8_t bw = kBwDefault; // delay bandpass width, 1/12 octave
    uint8_t hp = 0;          // reverb highpass index, 0 = off
    uint8_t type = RT_STOCK; // reverb type

    bool operator==(const Params& o) const { return bw == o.bw && hp == o.hp && type == o.type; }
    bool isDefault() const { return bw == kBwDefault && hp == 0 && type == RT_STOCK; }
    uint16_t hash() const
    {
        uint8_t b[3] = { bw, hp, type };
        return seqsidecar::crc16(b, 3);
    }

    // frame -> buf (>= kFileMax bytes); 0 = buffer too small
    size_t save(uint8_t* buf, size_t cap) const
    {
        if (cap < (size_t)kFileMax)
            return 0;
        uint8_t* p = buf + seqsidecar::kHeader;
        p[0] = bw;
        p[1] = hp;
        p[2] = type;
        for (int i = 3; i < kPayload; i++)
            p[i] = 0; // reserved
        return seqsidecar::frameWrite(buf, cap, kMagic, kVersion, p, kPayload);
    }
    // false: missing / bad frame / short payload (nothing changed). *newer: written by a newer mod version (leave the file alone).
    // A value outside its range falls back to its own default (a hand-edited or future file never produces a wild setting);
    // a longer payload of a later version is read as far as it is known.
    bool load(const uint8_t* buf, size_t len, bool* newer = nullptr)
    {
        const uint8_t* p = nullptr;
        size_t n = 0;
        uint8_t v = 0;
        seqsidecar::FrameStatus st = seqsidecar::frameRead(buf, len, kMagic, kVersion, &p, &n, &v);
        if (newer)
            *newer = st == seqsidecar::FRAME_NEWER;
        if (st != seqsidecar::FRAME_OK || n < 3)
            return false;
        bw = (p[0] >= kBwMin && p[0] <= kBwMax) ? p[0] : (uint8_t)kBwDefault;
        hp = p[1] <= kHpMax ? p[1] : (uint8_t)0;
        type = p[2] < RT_COUNT ? p[2] : (uint8_t)RT_STOCK;
        return true;
    }
};

}  // namespace seqfx
