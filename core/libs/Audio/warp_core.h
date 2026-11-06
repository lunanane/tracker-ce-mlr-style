// Sequencer tracks mod (plan 016 B) - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Warp: time-stretch of a sample voice to the project tempo, pitch kept. Pure DSP, no Polyend includes, host-tested
// (core/seqtest/test_warp.cpp). The firmware glue is play_memory_warp.cpp.
//
// Algorithm: two overlapping grains (Hann cross-fade, constant overlap-add, hop = half a grain). A new grain starts every `hop`
// output frames at the TIMELINE position; the timeline moves `ratio` source frames per output frame (ratio = sample duration /
// target duration, so ratio > 1 shortens the sound). Inside a grain the source is read with the pitch increment (tune), so the
// pitch is independent of the tempo. The first hop of a note plays the source unchanged (both grains start at the same frame),
// so a note starts with its original transient. Grain = 2 * hop = 512 .. 1536 frames (11.6 .. 34.8 ms at 44.1 kHz).
//
// WSOLA-lite: a new grain does not start exactly at the timeline but at the best matching phase within +-192 frames (4.4 ms):
// the start whose waveform correlates best with the natural continuation of the grain that is fading out. Without it a steady
// tone is detuned by up to +-29 Hz (the hop rate / 2) whenever the stretch ratio is not 1; with it the join is phase-coherent.
// The search is hierarchical (coarse 1:8 decimated, then 2, then 1 frame), 32 points per correlation, ~500 source reads per hop.
// Loop modes follow the stock playback: Forward loop wraps loop2 -> loop1; Backward loop plays forward to loop2, then backward
// and wraps loop1 -> loop2; Ping-pong reflects at both ends; One-shot ends at the end point. A grain that crosses a loop edge is
// read through the same wrap / reflect, so the join is as continuous as the loop itself. The source reads go through a reader
// functor (resident memory or the streamed ring), so the same code serves both.
//
// Known limits (v1): transients are smeared (a grain start moves by up to +-4.4 ms to align the phase); stretch ratios far from 1 (below 0.5 or above 2) skip or repeat material; reverse (R step FX) plays un-warped.
#pragma once
#include <math.h>
#include <stdint.h>

namespace warp {

enum Mode : uint8_t { kOnce = 0, kLoopFwd = 1, kLoopBwd = 2, kPingPong = 3 };

enum : int32_t {
    kHopMax = 768, // 34.8 ms grains
    kHopMin = 96,  // shortest hop, for high pitches: keeps the grain span (2 * hop * pitch) at 2000 frames or less (the stream loop-wrap cache holds 4096)
    kWinSteps = 256,
    kWinOne = 16384 // Q14
};

// rising half of the Hann window, Q14 (0 .. 16384), 257 points. Built once (cosf), internal RAM 514 bytes.
inline uint16_t* windowTable()
{
    static uint16_t t[kWinSteps + 1];
    return t;
}
inline void initWindow()
{
    uint16_t* t = windowTable();
    if (t[kWinSteps] == kWinOne)
        return;
    for (int i = 0; i <= kWinSteps; i++)
    {
        float c = 0.5f - 0.5f * cosf(3.14159265358979f * (float)i / (float)kWinSteps);
        int v = (int)(c * (float)kWinOne + 0.5f);
        t[i] = (uint16_t)(v < 0 ? 0 : (v > (int)kWinOne ? (int)kWinOne : v));
    }
    t[0] = 0;
    t[kWinSteps] = kWinOne;
}

struct Grain
{
    int32_t idx;   // source frame (relative to the sample start of the voice)
    uint32_t frac; // 0 .. 65535
    int8_t dir;    // +1 / -1: direction of the read
    uint8_t on;
};

struct Voice
{
    uint8_t active;
    uint8_t mode;
    int8_t tdir;     // direction of the timeline
    uint8_t finishing;
    uint8_t newest;  // index of the newer grain
    uint8_t align;   // 1 = WSOLA-lite phase alignment of the grain starts (0 only for comparison in the tests)
    int16_t shift;   // the last alignment shift in frames (diagnostics)
    int32_t endRel;  // sample frames played (end point - start point)
    int32_t l1, l2;  // loop (relative); l2 exclusive
    int32_t loopLen;
    int32_t hop;
    int32_t hopLeft;
    uint32_t ph, phInc; // window position, 16.16 over 0 .. 256
    double tl;          // timeline: start of the next grain
    float ratio;        // current source frames per output frame
    Grain g[2];
};

inline int32_t hopForPitch(float pitch)
{
    if (!(pitch > 0.05f))
        pitch = 0.05f;
    int32_t h = (int32_t)(1000.0f / pitch);
    return h < kHopMin ? kHopMin : (h > kHopMax ? kHopMax : h);
}

// start of a note. endRel / l1 / l2 in frames from the voice start; startRel = first source frame (0 = the voice start).
inline void start(Voice& v, uint8_t mode, int32_t endRel, int32_t l1, int32_t l2, float ratio, float pitch)
{
    initWindow();
    if (endRel < 2)
        endRel = 2;
    if (l2 > endRel)
        l2 = endRel;
    if (mode != kOnce && (l2 - l1) < 2)
        mode = kOnce;
    v.active = 1;
    v.mode = mode;
    v.tdir = 1;
    v.finishing = 0;
    v.endRel = endRel;
    v.l1 = l1 < 0 ? 0 : l1;
    v.l2 = l2;
    v.loopLen = l2 - v.l1;
    v.hop = hopForPitch(pitch);
    v.hopLeft = v.hop;
    v.ph = 0;
    v.phInc = ((uint32_t)kWinSteps << 16) / (uint32_t)v.hop;
    v.ratio = ratio;
    v.tl = (double)v.hop * (double)ratio;
    v.newest = 1;
    v.align = 1;
    v.shift = 0;
    v.g[0].idx = 0;
    v.g[0].frac = 0;
    v.g[0].dir = 1;
    v.g[0].on = 1;
    v.g[1] = v.g[0];
}

// tempo change: the ratio follows with a one-pole (one call per audio block: ~30 ms time constant)
inline void follow(Voice& v, float target)
{
    float d = target - v.ratio;
    if (d > -0.0005f && d < 0.0005f)
        v.ratio = target;
    else
        v.ratio += d * 0.1f;
}

// frame mapping of a grain read (loop wrap / reflect). -1 = silence (before the start)
inline int32_t mapIdx(const Voice& v, int32_t idx, int dir)
{
    if (v.mode == kOnce) // past the end the last frame is held: a grain that runs out fades smoothly, no step
        return idx < 0 ? -1 : (idx >= v.endRel ? v.endRel - 1 : idx);
    if (idx >= v.l2)
    {
        if (v.mode == kLoopFwd)
            idx = v.l1 + (idx - v.l1) % v.loopLen;
        else
        {
            idx = 2 * v.l2 - 2 - idx;
            if (idx < v.l1)
                idx = v.l1;
        }
    }
    else if (idx < v.l1 && dir < 0)
    {
        if (v.mode == kLoopBwd)
            idx = v.l2 - 1 - (v.l1 - 1 - idx) % v.loopLen;
        else if (v.mode == kPingPong)
        {
            idx = 2 * v.l1 - idx;
            if (idx >= v.l2)
                idx = v.l2 - 1;
        }
    }
    if (idx < 0)
        idx = 0;
    if (idx >= v.endRel)
        idx = v.endRel - 1;
    return idx;
}

// ---- phase alignment (WSOLA-lite) ----
enum : int32_t { kSearch = 192, kDec = 8, kPoints = 32 };

// shared scratch of the coarse stage (the voices render one after the other in the audio interrupt; never re-entered)
inline int32_t* searchBuf()
{
    static int32_t b[(2 * kSearch + 1024) / kDec + 8];
    return b;
}

template <class Rd>
inline int32_t at12(const Voice& v, Rd& rd, int32_t pos)
{
    int32_t m = mapIdx(v, pos, 1);
    return m < 0 ? 0 : (rd(m) >> 4);
}

// score of a candidate start: dot^2 / energy (only positive correlations count)
template <class Rd>
__attribute__((noinline)) inline float candScore(const Voice& v, Rd& rd, const int32_t* ref, int M, int sstride, int32_t pos)
{
    int32_t dot = 0, en = 0;
    for (int j = 0; j < M; j++)
    {
        int32_t c = at12(v, rd, pos + j * sstride);
        dot += ref[j] * c;
        en += c * c;
    }
    return dot > 0 ? (float)dot * (float)dot / (float)(en + 1) : 0.0f;
}

// best start near S for a grain that must continue the signal of a grain now at P (both read forward at `pitch`)
template <class Rd>
__attribute__((noinline)) inline int32_t alignStart(const Voice& v, Rd& rd, int32_t S, int32_t P, float pitch)
{
    int sstride = ((int)(16.0f * pitch + 4.0f)) & ~7;
    if (sstride < 8)
        sstride = 8;
    int M = 1024 / sstride;
    M = M > (int)kPoints ? (int)kPoints : (M < 8 ? 8 : M);
    int32_t ref[kPoints];
    for (int j = 0; j < M; j++)
        ref[j] = at12(v, rd, P + j * sstride);
    // coarse: candidates every kDec frames, from a decimated copy of the source around S
    const int32_t base = S - kSearch;
    const int step = sstride / kDec;
    const int nb = (2 * kSearch + M * sstride) / kDec + 1;
    int32_t* buf = searchBuf();
    for (int i = 0; i < nb; i++)
        buf[i] = at12(v, rd, base + i * kDec);
    float best = 0.0f;
    int bestLag = -1;
    for (int lag = 0; lag <= 2 * kSearch / kDec; lag++)
    {
        int32_t dot = 0, en = 0;
        for (int j = 0; j < M; j++)
        {
            int32_t c = buf[lag + j * step];
            dot += ref[j] * c;
            en += c * c;
        }
        if (dot > 0)
        {
            float sc = (float)dot * (float)dot / (float)(en + 1);
            if (sc > best)
            {
                best = sc;
                bestLag = lag;
            }
        }
    }
    if (bestLag < 0)
        return S; // no positive correlation anywhere (noise, silence): keep the timeline
    int32_t c0 = base + bestLag * kDec;
    // refine: +-kDec in steps of 2, then +-1
    int32_t cBest = c0;
    float sBest = candScore(v, rd, ref, M, sstride, c0);
    for (int32_t d = -kDec; d <= kDec; d += 2)
    {
        if (d == 0)
            continue;
        float sc = candScore(v, rd, ref, M, sstride, c0 + d);
        if (sc > sBest)
        {
            sBest = sc;
            cBest = c0 + d;
        }
    }
    const int32_t c2 = cBest;
    for (int32_t d = -1; d <= 1; d += 2)
    {
        float sc = candScore(v, rd, ref, M, sstride, c2 + d);
        if (sc > sBest)
        {
            sBest = sc;
            cBest = c2 + d;
        }
    }
    return cBest;
}

// start a new grain at the timeline and move the timeline on by one hop
template <class Rd>
inline void spawn(Voice& v, Rd& rd, float pitch)
{
    Grain& n = v.g[v.newest ^ 1];
    if (v.finishing)
        n.on = 0;
    else
    {
        double t = v.tl;
        if (t < 0.0)
            t = 0.0;
        n.idx = (int32_t)t;
        n.frac = (uint32_t)((t - (double)n.idx) * 65536.0);
        v.shift = 0;
        // phase alignment against the grain that is fading out; skipped when both read the same frames anyway (ratio == pitch)
        const Grain& cur = v.g[v.newest];
        const float dr = v.ratio - pitch;
        if (v.align && cur.on && cur.dir > 0 && v.tdir > 0 && (dr > 0.003f || dr < -0.003f))
        {
            int32_t S = alignStart(v, rd, n.idx, cur.idx, pitch);
            if (S < 0)
                S = 0;
            v.shift = (int16_t)(S - n.idx);
            n.idx = S;
        }
        n.dir = v.tdir;
        n.on = 1;
    }
    v.newest ^= 1;
    if (!v.g[0].on && !v.g[1].on)
    {
        v.active = 0;
        return;
    }
    if (v.finishing)
        return;
    double step = (double)v.hop * (double)v.ratio;
    if (v.tdir < 0)
        step = -step;
    v.tl += step;
    switch (v.mode)
    {
        case kOnce:
            if (v.tl >= (double)v.endRel)
                v.finishing = 1;
            break;
        case kLoopFwd:
            while (v.tl >= (double)v.l2)
                v.tl -= (double)v.loopLen;
            break;
        default: // kLoopBwd, kPingPong
            if (v.tdir > 0 && v.tl >= (double)v.l2)
            {
                v.tdir = -1;
                v.tl = 2.0 * (double)v.l2 - 1.0 - v.tl;
            }
            else if (v.tdir < 0 && v.tl < (double)v.l1)
            {
                if (v.mode == kPingPong)
                {
                    v.tdir = 1;
                    v.tl = 2.0 * (double)v.l1 - v.tl;
                }
                else
                {
                    v.tl += (double)v.loopLen;
                }
            }
            if (v.tl < 0.0)
                v.tl = 0.0;
            if (v.tl > (double)(v.endRel - 1))
                v.tl = (double)(v.endRel - 1);
            break;
    }
}

// Where the stream manager should look: the lowest frame the voice will still read while the timeline runs forward (the highest
// while it runs backward): the two grains' positions, and the start of the NEXT grain (timeline minus the alignment search
// range: the search reads back that far). In a forward loop a position that has wrapped counts as one loop length further on,
// so the playhead does not jump back to the loop start while a grain is still reading the end of the loop.
inline int32_t playhead(const Voice& v)
{
    const Grain& o = v.g[v.newest ^ 1];
    const Grain& n = v.g[v.newest];
    int32_t ref = o.on ? o.idx : (n.on ? n.idx : 0);
    int32_t p = ref;
    auto unwrap = [&](int32_t x) { return (v.mode == kLoopFwd && ref - x > v.loopLen / 2) ? x + v.loopLen : x; };
    int32_t nx = n.on ? unwrap(n.idx) : ref;
    int32_t tl = unwrap((int32_t)v.tl);
    if (v.tdir > 0)
    {
        p = ref < nx ? ref : nx;
        if (!v.finishing)
        {
            int32_t t = tl - (v.align ? (int32_t)kSearch : 0);
            if (t < p)
                p = t;
        }
    }
    else
    {
        p = ref > nx ? ref : nx;
        if (!v.finishing && tl > p)
            p = tl;
    }
    if (p < 0)
        p = 0;
    if (p > v.endRel - 1)
        p = v.endRel - 1;
    return p;
}

// Renders n frames (n <= any) into out. rd(frame) returns the source sample of a frame relative to the voice start, valid for
// 0 <= frame < endRel. pitch = read increment of a grain (1.0 = unchanged). Returns the frames rendered before the voice ended
// (the rest of out is zero).
template <class Rd>
inline int render(Voice& v, int16_t* out, int n, float pitch, Rd& rd)
{
    if (!v.active)
    {
        for (int i = 0; i < n; i++)
            out[i] = 0;
        return 0;
    }
    if (!(pitch > 0.001f))
        pitch = 0.001f;
    const int32_t incI = (int32_t)pitch;
    const uint32_t incF = (uint32_t)((pitch - (float)incI) * 65536.0f);
    const uint16_t* wt = windowTable();
    // grain reads in [lo, hi) need no mapping (idx + 1 inside as well); [0] forward grains, [1] backward grains
    int32_t lo[2], hi[2];
    if (v.mode == kOnce)
    {
        lo[0] = lo[1] = 0;
        hi[0] = hi[1] = v.endRel - 1;
    }
    else
    {
        lo[0] = 0;
        hi[0] = hi[1] = v.l2 - 1;
        lo[1] = (v.mode == kLoopFwd) ? 0 : v.l1;
    }
    int i = 0;
    for (; i < n; i++)
    {
        if (v.hopLeft <= 0)
        {
            spawn(v, rd, pitch);
            v.hopLeft = v.hop;
            v.ph = 0;
            if (!v.active)
                break;
        }
        int32_t s[2];
        for (int k = 0; k < 2; k++)
        {
            Grain& g = v.g[k];
            if (!g.on)
            {
                s[k] = 0;
                continue;
            }
            const int bw = g.dir < 0 ? 1 : 0;
            int32_t a, b;
            if (g.idx >= lo[bw] && g.idx < hi[bw])
            {
                a = rd(g.idx);
                b = rd(g.idx + 1);
            }
            else
            {
                int32_t m0 = mapIdx(v, g.idx, g.dir);
                int32_t m1 = mapIdx(v, g.idx + 1, g.dir);
                a = m0 < 0 ? 0 : rd(m0);
                b = m1 < 0 ? 0 : rd(m1);
            }
            s[k] = a + (((b - a) * (int32_t)(g.frac >> 1)) >> 15);
            if (g.dir > 0)
            {
                uint32_t f = g.frac + incF;
                g.idx += incI + (int32_t)(f >> 16);
                g.frac = f & 0xFFFF;
            }
            else
            {
                g.idx -= incI;
                if (g.frac < incF)
                {
                    g.idx -= 1;
                    g.frac = g.frac + 65536u - incF;
                }
                else
                    g.frac -= incF;
            }
        }
        const uint32_t wi = v.ph >> 16;
        const int32_t w0 = wt[wi];
        const int32_t w = w0 + ((((int32_t)wt[wi + 1] - w0) * (int32_t)((v.ph >> 8) & 0xFF)) >> 8);
        v.ph += v.phInc;
        const int32_t older = s[v.newest ^ 1], newer = s[v.newest];
        int32_t o = older + (((newer - older) * w) >> 14);
        out[i] = (int16_t)(o > 32767 ? 32767 : (o < -32768 ? -32768 : o));
        v.hopLeft--;
    }
    const int done = i;
    for (; i < n; i++)
        out[i] = 0;
    return done;
}

}  // namespace warp
