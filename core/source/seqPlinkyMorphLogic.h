// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// PolySynth snapshots per pattern with a timed linear morph (plan 012). No Polyend includes, host-testable (core/seqtest/test_plinky.cpp).
//
//  - Store:      one snapshot (all parameters but the channel) per pattern 1..255, "inherit" when a pattern has none. 5.4 KB, lives in
//                EXTERNAL_RAM on the device. Slot 0 is not used; `base` = the values of the old plinky.bin / the last save.
//  - Morph:      linear move of the continuous parameters from the current values to a target over the morph time. Stepped parameters
//                (voices, interval, LFO target) switch at the start. A parameter turned by hand leaves the morph (hand wins).
//  - Controller: what happens on a pattern change / clip launch / edit; the glue only feeds it the engine values and applies the result.
//  - File:       plinky.bin = the old version 1 frame first (a v0.3.x mod reads it unchanged and ignores what follows), then an optional
//                trailer frame "PLVM": morph time + the snapshots (only patterns that have one).
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "PlinkyVoice/plinky_params.h"

namespace plinkymorph {

using namespace plinkyvoice;

enum { kPatterns = 256, kHalfSecDefault = 12 /* 6 s */, kHalfSecMax = 60 /* 30 s */, kMsPerHalfSec = 500 };

// P_CHANNEL never takes part; stepped ones switch at the start of a morph
inline bool isStepped(int p) { return p == P_VOICES || p == P_INTERVAL || p == P_LFO_DEST; }

// ---------------------------------------------------------------- snapshots
struct Store
{
    uint8_t bits[kPatterns / 8];
    uint8_t v[kPatterns][P_COUNT];
    uint8_t base[P_COUNT];
    Store() { clear(); }
    void clear()
    {
        memset(bits, 0, sizeof(bits));
        for (int i = 0; i < P_COUNT; i++)
            base[i] = paramInfo(i).def;
    }
    static bool validPattern(int p) { return p >= 1 && p < kPatterns; }
    bool has(int p) const { return validPattern(p) && (bits[p >> 3] >> (p & 7)) & 1u; }
    const uint8_t* get(int p) const { return has(p) ? v[p] : nullptr; }
    int count() const
    {
        int n = 0;
        for (int p = 1; p < kPatterns; p++)
            n += has(p) ? 1 : 0;
        return n;
    }
    void set(int p, const uint8_t* vals)
    {
        if (!validPattern(p))
            return;
        for (int i = 0; i < P_COUNT; i++)
        {
            const ParamInfo& pi = paramInfo(i);
            v[p][i] = (vals[i] >= pi.min && vals[i] <= pi.max) ? vals[i] : pi.def;
        }
        bits[p >> 3] = (uint8_t)(bits[p >> 3] | (1u << (p & 7)));
    }
    void remove(int p)
    {
        if (validPattern(p))
            bits[p >> 3] = (uint8_t)(bits[p >> 3] & ~(1u << (p & 7)));
    }
    // A hand edit: the pattern's snapshot gets the value; a pattern without one starts from `cur` (all current values), so
    // the first edit makes the sound of the moment the pattern's own. false when nothing changed.
    bool write(int p, int param, int value, const uint8_t* cur)
    {
        if (!validPattern(p) || param < 0 || param >= P_COUNT || param == P_CHANNEL)
            return false;
        const uint8_t nv = clampParam(param, value);
        if (!has(p))
        {
            set(p, cur);
            v[p][param] = nv;
            return true;
        }
        if (v[p][param] == nv)
            return false;
        v[p][param] = nv;
        return true;
    }
    // The values pattern p would have by inheriting: the nearest lower pattern with a snapshot, else the base.
    const uint8_t* inherited(int p) const
    {
        for (int q = p - 1; q >= 1; q--)
            if (has(q))
                return v[q];
        return base;
    }
    // "differs from the inherited value" (the mark on the parameter)
    bool differs(int p, int param) const
    {
        return has(p) && param > 0 && param < P_COUNT && v[p][param] != inherited(p)[param];
    }
    uint32_t marks(int p) const
    {
        uint32_t m = 0;
        if (!has(p))
            return 0;
        const uint8_t* in = inherited(p);
        for (int i = 1; i < P_COUNT; i++)
            if (v[p][i] != in[i])
                m |= 1u << i;
        return m;
    }
};

// ---------------------------------------------------------------- the morph
struct Morph
{
    bool running = false;
    uint8_t pattern = 0; // the target pattern (title)
    uint32_t t0 = 0, dur = 0;
    uint32_t held = 0; // parameters taken out by hand
    uint8_t start[P_COUNT];
    uint8_t target[P_COUNT];
    Morph() { memset(start, 0, sizeof(start)); memset(target, 0, sizeof(target)); }

    void stop() { running = false; held = 0; }
    // Moves `vals` (the current values, in / out) towards `tgt`. Stepped parameters, and everything when durMs is 0, switch now.
    // Returns the bit mask of the parameters whose value changed at once. A running morph is replaced: the new one starts from vals.
    uint32_t begin(uint8_t* vals, const uint8_t* tgt, uint8_t pat, uint32_t now, uint32_t durMs)
    {
        uint32_t changed = 0;
        bool any = false;
        held = 0;
        for (int i = 1; i < P_COUNT; i++)
        {
            if (tgt[i] == vals[i])
                continue;
            if (durMs == 0 || isStepped(i))
            {
                vals[i] = tgt[i];
                changed |= 1u << i;
            }
            else
            {
                start[i] = vals[i];
                target[i] = tgt[i];
                any = true;
            }
        }
        for (int i = 1; i < P_COUNT; i++) // parameters that do not move
            if (tgt[i] == vals[i] || isStepped(i) || durMs == 0)
                start[i] = target[i] = vals[i];
        running = any;
        pattern = pat;
        t0 = now;
        dur = durMs;
        return changed;
    }
    // A hand edit takes the parameter out of the morph.
    void hold(int p)
    {
        if (p > 0 && p < P_COUNT)
            held |= 1u << p;
    }
    // The values at `now` (wrap-safe time). Returns the mask of parameters whose value changed.
    uint32_t step(uint8_t* vals, uint32_t now)
    {
        if (!running)
            return 0;
        uint32_t el = now - t0;
        const bool done = el >= dur;
        uint32_t changed = 0;
        for (int i = 1; i < P_COUNT; i++)
        {
            if (start[i] == target[i] || ((held >> i) & 1u))
                continue;
            uint8_t nv;
            if (done)
                nv = target[i];
            else
            {
                const float f = (float)el / (float)dur;
                nv = (uint8_t)(int)floorf((float)start[i] + ((float)target[i] - (float)start[i]) * f + 0.5f);
            }
            if (nv != vals[i])
            {
                vals[i] = nv;
                changed |= 1u << i;
            }
        }
        if (done)
        {
            running = false;
            held = 0;
        }
        return changed;
    }
    uint32_t remainMs(uint32_t now) const
    {
        if (!running)
            return 0;
        const uint32_t el = now - t0;
        return el >= dur ? 0 : dur - el;
    }
};

// ---------------------------------------------------------------- what to do on which event
// The pattern a clip launch morphs to: the clip's source pattern when the track plays the PolySynth; 0 = nothing.
inline uint8_t clipTarget(bool trackUsesVoice, int clipPattern)
{
    return (trackUsesVoice && Store::validPattern(clipPattern)) ? (uint8_t)clipPattern : 0;
}

struct Controller
{
    Store store;
    Morph morph;
    uint8_t halfSec = kHalfSecDefault;
    uint8_t pattern = 0; // the pattern the engine values follow
    bool synced = false;

    uint32_t durMs() const { return (uint32_t)halfSec * kMsPerHalfSec; }
    // project (re)loaded: the next onPattern applies the pattern's snapshot at once, no morph
    void resync()
    {
        synced = false;
        morph.stop();
    }
    // The playing pattern (or the shown one when stopped) as the main loop sees it. vals = engine values in / out. Returns the changed mask.
    uint32_t onPattern(int pat, bool playing, uint8_t* vals, uint32_t now)
    {
        if (!Store::validPattern(pat))
            return 0;
        if (!synced)
        {
            synced = true;
            pattern = (uint8_t)pat;
            morph.stop();
            const uint8_t* s = store.get(pat);
            return s ? morph.begin(vals, s, (uint8_t)pat, now, 0) : 0;
        }
        if (pat == pattern)
            return 0;
        pattern = (uint8_t)pat;
        const uint8_t* s = store.get(pat);
        if (!s)
            return 0; // inherits: nothing changes (a running morph goes on)
        return morph.begin(vals, s, (uint8_t)pat, now, playing ? durMs() : 0);
    }
    // A clip was launched on a track: the sound of the clip's source pattern (last launch wins)
    uint32_t onClip(int clipPattern, bool trackUsesVoice, bool playing, uint8_t* vals, uint32_t now)
    {
        const uint8_t t = clipTarget(trackUsesVoice, clipPattern);
        const uint8_t* s = t ? store.get(t) : nullptr;
        if (!s)
            return 0;
        return morph.begin(vals, s, t, now, playing ? durMs() : 0);
    }
    uint32_t tick(uint8_t* vals, uint32_t now) { return morph.step(vals, now); }
    // A hand edit of `param` (already applied to the engine): out of the morph, into the snapshot of pattern `pat`.
    // Returns true when the snapshots changed (project dirty).
    bool edit(int pat, int param, int value, const uint8_t* cur)
    {
        morph.hold(param);
        uint8_t from[P_COUNT];
        memcpy(from, cur, P_COUNT);
        if (morph.running) // a new snapshot starts from where the morph is going, not from the values on the way
            for (int i = 1; i < P_COUNT; i++)
                if (!((morph.held >> i) & 1u) && morph.start[i] != morph.target[i])
                    from[i] = morph.target[i];
        return store.write(pat, param, value, from);
    }
};

// ---------------------------------------------------------------- display texts
// "Morph 3.2s > P12"
inline void titleText(char* out, size_t n, uint32_t remainMs, int pattern)
{
    const uint32_t tenths = (remainMs + 99) / 100;
    snprintf(out, n, "Morph %u.%us > P%d", (unsigned)(tenths / 10), (unsigned)(tenths % 10), pattern);
}
inline void timeText(int halfSec, char* out, size_t n)
{
    if (halfSec <= 0)
        snprintf(out, n, "Jump");
    else
        snprintf(out, n, "%d.%d s", halfSec / 2, (halfSec & 1) ? 5 : 0);
}

// ---------------------------------------------------------------- plinky.bin
constexpr char kTrailerMagic[4] = { 'P', 'L', 'V', 'M' };
enum { kTrailerVersion = 1, kEntry = 1 + (P_COUNT - 1), kTrailerMax = 2 + (kPatterns - 1) * kEntry };
enum { kFileMaxAll = seqsidecar::kOverhead + kPayload + seqsidecar::kOverhead + kTrailerMax };

inline bool needsTrailer(const Store& st, int halfSec) { return st.count() > 0 || halfSec != kHalfSecDefault; }

// Frame 1 (version 1, exactly what the older mods write: `cur` = the values dialled in) + the trailer when there is something to say.
inline size_t saveFile(uint8_t* buf, size_t cap, const Params& cur, const Store& st, int halfSec)
{
    size_t n = cur.save(buf, cap);
    if (!n || !needsTrailer(st, halfSec))
        return n;
    if (cap < n + seqsidecar::kOverhead + kTrailerMax)
        return 0;
    uint8_t* p = buf + n + seqsidecar::kHeader;
    uint8_t cnt = 0;
    size_t o = 2;
    for (int pat = 1; pat < kPatterns; pat++)
    {
        if (!st.has(pat))
            continue;
        p[o++] = (uint8_t)pat;
        for (int i = 1; i < P_COUNT; i++)
            p[o++] = st.v[pat][i];
        cnt++;
    }
    p[0] = (uint8_t)(halfSec < 0 ? 0 : (halfSec > kHalfSecMax ? kHalfSecMax : halfSec));
    p[1] = cnt;
    size_t t = seqsidecar::frameWrite(buf + n, cap - n, kTrailerMagic, kTrailerVersion, p, o);
    return t ? n + t : 0;
}

// false: frame 1 missing / bad / short (nothing changed); *newer: a later mod version wrote it (leave the file alone).
// An old file (frame 1 only) gives the base values and no snapshots. A bad trailer is ignored (the base stays); a newer trailer
// version sets *newer and is read as far as it is known: nothing, so the file is left alone.
inline bool loadFile(const uint8_t* buf, size_t len, Params& base, Store& st, uint8_t& halfSec, bool* newer = nullptr)
{
    if (newer)
        *newer = false;
    Params b;
    if (!b.load(buf, len, newer))
        return false;
    base = b;
    st.clear();
    memcpy(st.base, b.v, P_COUNT);
    halfSec = kHalfSecDefault;
    const size_t n1 = seqsidecar::kHeader + ((size_t)buf[6] | ((size_t)buf[7] << 8)) + seqsidecar::kCrc;
    if (len < n1 + seqsidecar::kOverhead)
        return true;
    const uint8_t* p = nullptr;
    size_t pn = 0;
    uint8_t ver = 0;
    const seqsidecar::FrameStatus fs = seqsidecar::frameRead(buf + n1, len - n1, kTrailerMagic, kTrailerVersion, &p, &pn, &ver);
    if (fs == seqsidecar::FRAME_NEWER)
    {
        if (newer)
            *newer = true;
        return true;
    }
    if (fs != seqsidecar::FRAME_OK || pn < 2)
        return true;
    halfSec = p[0] > kHalfSecMax ? (uint8_t)kHalfSecDefault : p[0];
    const size_t cnt = p[1];
    for (size_t e = 0; e < cnt && 2 + (e + 1) * kEntry <= pn; e++)
    {
        const uint8_t* q = p + 2 + e * kEntry;
        uint8_t vals[P_COUNT];
        vals[P_CHANNEL] = paramInfo(P_CHANNEL).def;
        memcpy(vals + 1, q + 1, P_COUNT - 1);
        st.set(q[0], vals); // pattern 0 / out of range values are dropped / defaulted by set()
    }
    return true;
}

} // namespace plinkymorph
