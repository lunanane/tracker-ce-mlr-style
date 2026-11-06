// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Master amplitude view (plan 013 M, redesigned in plan 014 B, plan 015 V, time axis of v0.4.1): one level contour per track plus the master.
// New data enters at the right edge and scrolls to the left. This header holds
//   - the dB conversion by table (no log per sample),
//   - Cascade: the history of one lane. The audio side delivers ONE PEAK PER AUDIO BLOCK (2.9 ms). Three tiers of buckets with
//     ABSOLUTE alignment (bucket j of a tier covers the blocks j*S .. (j+1)*S - 1): 8 blocks (23 ms, 512 buckets = 11.9 s), 32 blocks
//     (93 ms, 256 buckets = 23.8 s), 128 blocks (372 ms, 256 buckets = 95 s). Every tier keeps its own ring (average and maximum of the
//     block peaks), so a bucket never changes after it is finished; the unfinished newest bucket of a tier is read from its accumulator.
//     The oldest tier holds more than the largest time scale (60 s). Constant memory (4 KB per lane) and O(1) work per block,
//   - the time axis (age -> x): the right 75 % of the width is LINEAR in time (newest at the right edge, every column moves at the same
//     speed), the left 25 % compresses with a continuous slope (a parabola in x) up to the total age at the left axis, where the lines are
//     CUT (nothing older is drawn). The jog wheel picks one of four scales: linear 2 / 4 / 8 / 16 s, total 8 / 15 / 30 / 60 s (default 8 / 30),
//   - the layout over the full height of the screen between the title bar and the strip, and the drawing over a tiny context (the same code
//     runs on the FT81x display list in interface/masterParams/ampView.cpp and in the host tests). No Polyend includes.
//
// Drawing: every audible track is two thin contour lines (the dim line of the bucket maxima = peaks, in front of it the line of the
// averages), the master one neutral line. The lines are RESAMPLED at M columns of equal pixel width (M follows the display-list budget and
// the number of audible lanes: 16 .. 200 points per line); the value of a column is read at the EXACT age of its x (and of the history
// that the audio side delivered up to now), from the tier whose buckets are at most half a column wide, so the contour moves smoothly.
#ifndef SEQ_AMP_VIEW_LOGIC_H_
#define SEQ_AMP_VIEW_LOGIC_H_

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "seqPalette.h"

namespace ampview
{
enum
{
    kTracks = 8,           // sample tracks (the only ones with audio of their own)
    kLanes = kTracks + 1,  // + the master (lane 8)
    kGroup = 8,            // blocks per finest bucket (23.2 ms)
    kTiers = 3,            // bucket spans kGroup, 4 x kGroup, 16 x kGroup blocks
    kTierShift = 2,        // span ratio 4
    kRing0 = 512,          // buckets per tier (powers of two)
    kRing1 = 256,
    kRing2 = 256,
    kRingCells = kRing0 + kRing1 + kRing2,
    kX0 = 56,              // plot area: the full height between the stock title bar (28) and the strip (425); x = kX0 is the y axis
    kX1 = 716,
    kLinPx = (kX1 - kX0) * 3 / 4, // the linear part: the right 75 % of the width (495 px)
    kY0 = 36,              // 0 dB
    kY1 = 418,             // the floor
    kPanelY0 = 30,
    kPanelY1 = 424,
    kFloorDb = -48,        // the bottom edge: typical levels (-36 .. 0 dB) use three quarters of the height
    kSilent = -96 * 64,    // dB x 64 of a zero level
    kMaxCols = 200,        // points per line, at most (3.3 px apart)
    kMinCols = 16,
    kScales = 4,
    kDefaultScale = 2,     // linear 8 s, total 30 s
    kDefaultBudget = 5400  // bytes of display list for the view (8172 - 400 margin - title bar 600 - strip / labels 1720)
};
constexpr float kBlockMs = 128000.0f / 44100.0f; // 2.9025 ms
// time scales (jog wheel): the age at the 75 % point (end of the linear part) and the age at the left axis (the line is cut there)
static const uint16_t kLinMs[kScales] = { 2000, 4000, 8000, 16000 };
static const uint32_t kTotalMs[kScales] = { 8000, 15000, 30000, 60000 };
constexpr uint32_t kMaxTotalMs = 60000;

// 1/64 dB of a level (0 .. 32768 = full scale; larger values clamp to +6 dB). Table of 6.02 dB * log2(1 + i / 64) * 64, linear
// interpolation: error < 0.06 dB. Called per bucket, never per sample.
inline int dbx64(uint32_t peak)
{
    static const int16_t kTab[65] = { 0,   9,   17,  25,  34,  42,  50,  58,  65,  73,  81,  88,  96,  103, 110, 117, 124,
                                      131, 138, 144, 151, 158, 164, 171, 177, 183, 189, 196, 202, 208, 214, 220, 225, 231,
                                      237, 242, 248, 254, 259, 264, 270, 275, 280, 286, 291, 296, 301, 306, 311, 316, 321,
                                      326, 330, 335, 340, 345, 349, 354, 359, 363, 368, 372, 376, 381, 385 };
    if (peak == 0) return kSilent;
    if (peak > 65535u) peak = 65535u;
    int e = 0;
    while ((peak >> e) > 1u) e++;
    const uint32_t norm = peak << (15 - e); // 32768 .. 65535
    const uint32_t off = norm - 32768u;
    const int idx = (int)(off >> 9), fr = (int)(off & 511u);
    const int t = kTab[idx] + ((kTab[idx + 1] - kTab[idx]) * fr >> 9);
    const int db = ((e - 15) * 3852) / 10 + t;
    return db < kSilent ? kSilent : db;
}

// height 0..1 of a level in dB (0 dB = 1, kFloorDb and below = 0)
inline float heightOf(float db)
{
    const float h = (db - (float)kFloorDb) / (float)(-kFloorDb);
    return h < 0 ? 0 : (h > 1 ? 1 : h);
}
inline int yOf(float db) { return (int)(kY1 - heightOf(db) * (kY1 - kY0) + 0.5f); }

// RMS meter value (0..1, -1 = the meter is not armed yet) to the 0..32768 level scale
inline uint16_t levelOfRms(float rms)
{
    if (!(rms > 0.0f)) return 0;
    const float v = rms * 32768.0f + 0.5f;
    return v >= 65535.0f ? (uint16_t)65535 : (uint16_t)v;
}

struct Bucket
{
    uint16_t avg;  // average of the block peaks in it
    uint16_t mx;   // maximum of the block peaks in it
    uint32_t span; // blocks covered (the unfinished newest bucket of a tier: fewer than the tier's span)
};

// History of one lane: kTiers rings of buckets with absolute alignment (see the header). Nothing is merged: a finished bucket never
// changes. n_ counts the pushed blocks (uint32: 140 hours; the view is re-armed whenever the Master module is entered).
class Cascade
{
  public:
    Cascade() { reset(); }
    void reset()
    {
        n_ = 0;
        for (int t = 0; t < kTiers; t++)
        {
            sum_[t] = 0;
            mx_[t] = 0;
        }
    }
    uint32_t blocks() const { return n_; }
    static uint32_t span(int t) { return (uint32_t)kGroup << (kTierShift * t); }
    static uint32_t cap(int t) { return t == 0 ? (uint32_t)kRing0 : (t == 1 ? (uint32_t)kRing1 : (uint32_t)kRing2); }
    // one audio block peak (0 .. 65535)
    void push(uint16_t v)
    {
        for (int t = 0; t < kTiers; t++)
        {
            sum_[t] += v;
            if (v > mx_[t]) mx_[t] = v;
        }
        n_++;
        for (int t = 0; t < kTiers; t++)
        {
            const uint32_t s = span(t);
            if (n_ == 0 || (n_ & (s - 1)) != 0) continue;
            Cell& c = cell_[base(t) + (((n_ / s) - 1) & (cap(t) - 1))];
            c.avg = (uint16_t)((sum_[t] + s / 2) / s);
            c.mx = mx_[t];
            sum_[t] = 0;
            mx_[t] = 0;
        }
    }
    // Bucket j of tier t (absolute index, covers blocks j*S .. (j+1)*S - 1). The newest one (j = blocks / S) is the unfinished bucket.
    // false: not held (older than the ring, or no block in it yet).
    bool get(int t, uint32_t j, Bucket& b) const
    {
        const uint32_t s = span(t), f = n_ / s;
        if (j == f)
        {
            const uint32_t c = n_ - f * s;
            if (!c) return false;
            b.avg = (uint16_t)((sum_[t] + c / 2) / c);
            b.mx = mx_[t];
            b.span = c;
            return true;
        }
        if (j > f || f - j > cap(t)) return false;
        const Cell& c = cell_[base(t) + (j & (cap(t) - 1))];
        b.avg = c.avg;
        b.mx = c.mx;
        b.span = s;
        return true;
    }
    uint32_t oldestHeld(int t) const // first finished bucket still in the ring
    {
        const uint32_t f = n_ / span(t);
        return f > cap(t) ? f - cap(t) : 0;
    }
    // age in blocks that tier t covers completely (the start of its oldest held bucket, counted back from now)
    uint32_t reach(int t) const { return n_ - oldestHeld(t) * span(t); }
    // bucket of tier t that holds the block of the given age (0 = the newest block); a must be < blocks()
    uint32_t indexAt(int t, uint32_t a) const { return (n_ - 1 - a) / span(t); }

  private:
    struct Cell
    {
        uint16_t avg, mx;
    };
    static uint32_t base(int t) { return t == 0 ? 0u : (t == 1 ? (uint32_t)kRing0 : (uint32_t)(kRing0 + kRing1)); }
    Cell cell_[kRingCells];
    uint32_t sum_[kTiers];
    uint16_t mx_[kTiers];
    uint32_t n_;
};

// ---- time axis ------------------------------------------------------------------------------------------------------------------
// p = distance from the right edge in pixels (0 .. W). p <= kLinPx: age = a * p (linear, a = linear age / 495 px). Beyond: with d = p - kLinPx
// age = lin + a * d + b * d^2 (same slope a at the 75 % point), b chosen so that the left edge (p = W) is the total age of the scale.
inline float tailB(int scale)
{
    const float D = (float)(kX1 - kX0 - kLinPx);
    const float a = (float)kLinMs[scale] / (float)kLinPx;
    return ((float)kTotalMs[scale] - (float)kLinMs[scale] - a * D) / (D * D);
}
// u = 0 (now, right edge) .. 1 (left edge) -> age in ms
inline float ageMsOfU(float u, int scale)
{
    const float p = u * (float)(kX1 - kX0), a = (float)kLinMs[scale] / (float)kLinPx;
    if (p <= (float)kLinPx) return a * p;
    const float d = p - (float)kLinPx;
    return (float)kLinMs[scale] + a * d + tailB(scale) * d * d;
}
// x of a point of the given age (ms, 0 = newest = right edge). Monotone, continuous slope; the total age and anything older sit at the left axis.
inline float xOfAge(float ageMs, int scale = kDefaultScale)
{
    if (ageMs <= 0.0f) return (float)kX1;
    if (ageMs >= (float)kTotalMs[scale]) return (float)kX0;
    const float a = (float)kLinMs[scale] / (float)kLinPx;
    float p;
    if (ageMs <= (float)kLinMs[scale])
        p = ageMs / a;
    else
    {
        const float r = ageMs - (float)kLinMs[scale], b = tailB(scale);
        const float d = b < 1e-9f ? r / a : (sqrtf(a * a + 4.0f * b * r) - a) / (2.0f * b);
        p = (float)kLinPx + d;
    }
    return (float)kX1 - p;
}

// header values (set by the entry point from the sequencer)
struct Info
{
    uint8_t playing = 0;
    uint8_t pattern = 0; // 1-based
    uint8_t pos = 0;     // 1-based step
    uint8_t len = 0;
    uint16_t bpm10 = 0;  // BPM x 10
};

class Follower
{
  public:
    Follower() { reset(); }
    void reset()
    {
        for (int i = 0; i < kLanes; i++) lane_[i].reset();
        blocks_ = 0;
    }
    // one audio block for all lanes (kLanes peaks)
    void push(const uint16_t* levels)
    {
        for (int i = 0; i < kLanes; i++) lane_[i].push(levels[i]);
        blocks_++;
    }
    uint32_t blocks() const { return blocks_; }
    const Cascade& lane(int i) const { return lane_[i]; }
    uint16_t newest(int i) const
    {
        const Cascade& c = lane_[i];
        Bucket b;
        return c.blocks() && c.get(0, c.indexAt(0, 0), b) ? b.avg : (uint16_t)0;
    }
    // the jog wheel: one step per call towards a longer (+) / shorter (-) time scale
    void jog(int delta)
    {
        const int s = scale + (delta > 0 ? 1 : (delta < 0 ? -1 : 0));
        scale = (uint8_t)(s < 0 ? 0 : (s >= kScales ? kScales - 1 : s));
    }
    Info info;
    uint8_t scale = kDefaultScale;
    uint16_t budget = kDefaultBudget; // bytes of display list the drawing may use (the entry point lowers it when other controls need room)

  private:
    Cascade lane_[kLanes];
    uint32_t blocks_;
};

// Does a lane have anything above the floor (silent lanes are not drawn): looked up in the oldest tier, which holds the whole displayed range
inline bool audible(const Cascade& c)
{
    const int t = kTiers - 1;
    const uint32_t f = c.blocks() / Cascade::span(t);
    Bucket b;
    for (uint32_t j = c.oldestHeld(t); j <= f; j++)
        if (c.get(t, j, b) && dbx64(b.mx) > kFloorDb * 64) return true;
    return false;
}

// Columns of the resampled lines: how many points per line fit the budget (lines = number of strips, lanes = audible lanes: their labels)
constexpr int kBaseBytes = 800; // measured: grid, labels, header without the lines and the lane labels
inline int colsFor(int budget, int lines, int lanes)
{
    if (lines < 1) return kMaxCols;
    const int m = (budget - kBaseBytes - lanes * 24 - lines * 40) / (4 * lines); // 40: colour, width and a few strip breaks per line
    return m < kMinCols ? kMinCols : (m > kMaxCols ? kMaxCols : m);
}

// One line of one lane over M columns (left .. right x = kX0 .. kX1, equal spacing). which 0 = maxima, 1 = averages. Writes (x, y) pairs from
// the oldest column to the newest at the right edge; returns the number of points. A column is drawn when its exact age (from the time axis)
// lies inside the history, so the line starts where the history starts and ends exactly at the left axis (x = kX0) at the latest.
inline int resample(const Cascade& cs, int cols, int which, int scale, int16_t* o)
{
    const uint32_t P = cs.blocks();
    const float bpms = 1.0f / kBlockMs;
    const float du = 1.0f / (float)(cols - 1);
    int cnt = 0;
    float dbs[kMaxCols];
    int16_t xs[kMaxCols];
    for (int j = cols - 1; j >= 0; j--) // newest first
    {
        const float u = (float)(cols - 1 - j) * du; // j = cols - 1 -> u = 0 (now)
        const float uL = u - 0.5f * du < 0 ? 0.0f : u - 0.5f * du, uR = u + 0.5f * du > 1 ? 1.0f : u + 0.5f * du;
        const float aL = ageMsOfU(uL, scale) * bpms, aR = ageMsOfU(uR, scale) * bpms, ac = ageMsOfU(u, scale) * bpms;
        if (!(ac < (float)P)) break; // older than the history
        // the tier: the coarsest one whose buckets are at most half a column wide, a coarser one where the finer ring does not reach back
        const float colSpan = aR - aL;
        int t = 0;
        while (t + 1 < kTiers && 2.0f * (float)Cascade::span(t + 1) <= colSpan) t++;
        while (t + 1 < kTiers && aR > (float)cs.reach(t)) t++;
        const uint32_t S = Cascade::span(t);
        const uint32_t jn = cs.indexAt(t, (uint32_t)aL);
        uint32_t jo = cs.indexAt(t, aR >= (float)(P - 1) ? P - 1 : (uint32_t)aR);
        const uint32_t jmin = cs.oldestHeld(t);
        if (jo < jmin) jo = jmin;
        if (jn < jo) break; // older than the oldest tier holds
        Bucket b;
        float db;
        if (jn > jo) // several buckets in this column: the average (weighted by the overlap) or the maximum of them
        {
            float sum = 0, wsum = 0;
            uint32_t mx = 0;
            for (uint32_t q = jo; q <= jn; q++)
            {
                if (!cs.get(t, q, b)) continue;
                const float e = (float)(P - q * S), s = e - (float)b.span;
                const float lo = s > aL ? s : aL, hi = e < aR ? e : aR;
                const float w = hi > lo ? hi - lo : 0.0f;
                const uint32_t v = which ? b.avg : b.mx;
                sum += w * (float)v;
                wsum += w;
                if (v > mx) mx = v;
            }
            db = (float)dbx64(which ? (uint32_t)(wsum > 0 ? sum / wsum + 0.5f : 0.0f) : mx);
        }
        else // inside one bucket: linear between the centres of it and its neighbour (smooth contour, no steps)
        {
            if (!cs.get(t, jn, b)) break;
            const float e = (float)(P - jn * S), c0 = e - 0.5f * (float)b.span;
            db = (float)dbx64(which ? b.avg : b.mx);
            const uint32_t nj = ac < c0 ? jn + 1 : jn - 1; // newer / older neighbour
            Bucket nb;
            if ((ac < c0 || jn > 0) && cs.get(t, nj, nb))
            {
                const float ne = (float)(P - nj * S), c1 = ne - 0.5f * (float)nb.span;
                float tt = c1 != c0 ? (ac - c0) / (c1 - c0) : 0.0f;
                tt = tt < 0 ? 0 : (tt > 1 ? 1 : tt);
                db += ((float)dbx64(which ? nb.avg : nb.mx) - db) * tt;
            }
        }
        const float lowest = (float)((kFloorDb - 6) * 64);
        dbs[cnt] = db < lowest ? lowest : db;
        xs[cnt] = (int16_t)(kX0 + ((kX1 - kX0) * j + (cols - 1) / 2) / (cols - 1));
        cnt++;
    }
    // cnt columns, newest first. The averages are smoothed over about three to five columns in the right half (a rhythm reads as a stream, not a scribble)
    if (which == 1)
    {
        float tmp[kMaxCols];
        for (int i = 0; i < cnt; i++)
        {
            const bool right = xs[i] > (kX0 + kX1) / 2;
            tmp[i] = right && i > 1 && i + 2 < cnt ? (dbs[i - 2] + 2 * dbs[i - 1] + 3 * dbs[i] + 2 * dbs[i + 1] + dbs[i + 2]) * (1.0f / 9.0f)
                                                        : (right && i > 0 && i + 1 < cnt ? 0.25f * dbs[i - 1] + 0.5f * dbs[i] + 0.25f * dbs[i + 1] : dbs[i]);
        }
        for (int i = 0; i < cnt; i++) dbs[i] = tmp[i];
    }
    // oldest first; where the level is below the floor the line is clipped to it and BROKEN (y = -1 marks the break): no long straight
    // joins across silence
    int w = 0;
    bool prevIn = false;
    for (int i = cnt - 1; i >= 0; i--)
    {
        const bool in = dbs[i] > (float)(kFloorDb * 64);
        if (in || prevIn)
        {
            o[w * 2] = xs[i];
            o[w * 2 + 1] = (int16_t)yOf(in ? dbs[i] * (1.0f / 64.0f) : (float)kFloorDb);
            w++;
            if (!in)
            {
                o[w * 2] = xs[i];
                o[w * 2 + 1] = -1;
                w++;
            }
        }
        prevIn = in;
    }
    return w;
}

static const int kMarkMs[7] = { 1000, 2000, 4000, 8000, 15000, 30000, 60000 };
static const char* const kMarkText[7] = { "1s", "2s", "4s", "8s", "15s", "30s", "60s" };
static const int kDbLines[5] = { 0, -12, -24, -36, -48 };
constexpr int kMarkGap = 40; // px between two time marks (and to the right edge)

// the time marks of a scale: candidates up to the total age, thinned so that labels do not touch; the total age (the left axis) is always one
inline int timeMarks(int scale, int* idx, int* xs)
{
    int n = 0, lastX = kX1;
    for (int i = 0; i < 7 && (uint32_t)kMarkMs[i] <= kTotalMs[scale]; i++)
    {
        const int x = (int)(xOfAge((float)kMarkMs[i], scale) + 0.5f);
        const bool total = (uint32_t)kMarkMs[i] == kTotalMs[scale];
        if (!total && (x > lastX - kMarkGap || x < kX0 + kMarkGap)) continue;
        if (total && n && x > lastX - kMarkGap) n--; // the left axis label wins over a neighbour that is too close
        idx[n] = i;
        xs[n] = x;
        n++;
        lastX = x;
    }
    return n;
}

// C as in pitchtrace::draw (color, width, rect, lines, strip, textPhase, text, finish)
template <class C> void draw(C& c, const Follower& f, const char* title, const char* hint)
{
    char buf[48];
    const int sc = f.scale;
    c.color(seqpal::kBg);
    c.rect(0, kPanelY0, 800, kPanelY1);
    c.textPhase();

    // grid: horizontal dB lines, vertical time marks (the marks follow the chosen time scale)
    c.width(16);
    int16_t seg[12 * 4];
    int n = 0;
    for (int i = 1; i < 5; i++)
    {
        const int16_t y = (int16_t)yOf((float)kDbLines[i]);
        const int16_t s[4] = { kX0, y, kX1, y };
        for (int k = 0; k < 4; k++) seg[n * 4 + k] = s[k];
        n++;
    }
    int mIdx[7], mX[7];
    const int marks = timeMarks(sc, mIdx, mX);
    for (int i = 0; i < marks; i++)
    {
        const int16_t x = (int16_t)mX[i];
        const int16_t s[4] = { x, kY0, x, kY1 };
        for (int k = 0; k < 4; k++) seg[n * 4 + k] = s[k];
        n++;
    }
    c.color(seqpal::kGrid);
    c.lines(seg, n);
    const int16_t edge[12] = { kX0, kY0, kX1, kY0, kX1, kY0, kX1, kY1, kX0, kY0, kX0, kY1 }; // 0 dB line, the "now" edge and the y axis (the cut)
    c.color(seqpal::kGridHi);
    c.lines(edge, 3);

    // scale labels, inside the plot at the bottom (no footer band)
    c.color(seqpal::dim(seqpal::kText));
    for (int i = 0; i < 5; i++)
    {
        snprintf(buf, sizeof buf, "%d", kDbLines[i]);
        c.text(kX0 - 6, i == 0 ? kY0 + 7 : (i == 4 ? kY1 - 7 : yOf((float)kDbLines[i])), 1, 2 | 4, buf);
    }
    for (int i = 0; i < marks; i++)
        c.text(mX[i], kY1 - 22, 1, 1 | 2, kMarkText[mIdx[i]]);
    c.text(kX1 - 4, kY1 - 22, 1, 4 | 2, "now");

    // the lines: resampled at M columns; maxima dim behind (pass 0), averages on top (pass 1); the master neutral and without a second line
    const bool history = f.lane(0).blocks() >= 2 * (uint32_t)kGroup;
    int lines = 0, lanes = 0;
    bool aud[kLanes];
    for (int ln = 0; ln < kLanes; ln++)
    {
        aud[ln] = history && audible(f.lane(ln));
        if (aud[ln])
        {
            lanes++;
            lines += ln == kTracks ? 1 : 2;
        }
    }
    const int cols = colsFor(f.budget, lines, lanes);
    int16_t o[(2 * kMaxCols + 2) * 2]; // worst case: every other column is a break
    for (int pass = 0; pass < 2; pass++)
    {
        for (int ln = 0; ln < kLanes; ln++)
        {
            if (!aud[ln] || (ln == kTracks && pass == 0)) continue;
            const int cnt = resample(f.lane(ln), cols, pass, sc, o);
            const uint32_t col = ln == kTracks ? seqpal::kTitle : seqpal::trackColor(ln);
            c.color(pass == 0 ? seqpal::faint(col) : col);
            c.width(pass == 0 ? 10 : (ln == kTracks ? 12 : 16));
            int s = 0; // strips between the breaks (y = -1)
            for (int i = 0; i <= cnt; i++)
                if (i == cnt || o[i * 2 + 1] < 0)
                {
                    if (i - s >= 2) c.strip(o + s * 2, i - s);
                    s = i + 1;
                }
        }
    }

    // current levels at the right edge: the audible tracks and M
    for (int ln = 0; ln < kLanes; ln++)
    {
        if (!aud[ln]) continue;
        const int y = kY0 + 14 + ln * 28;
        const float db = dbx64(f.newest(ln)) * (1.0f / 64.0f);
        const char tag = ln == kTracks ? 'M' : (char)('1' + ln);
        if (db <= (float)kFloorDb)
            snprintf(buf, sizeof buf, "%c  --", tag);
        else
            snprintf(buf, sizeof buf, "%c %d", tag, (int)(db < 0 ? db - 0.5f : db + 0.5f));
        c.color(ln == kTracks ? seqpal::kTitle : seqpal::trackColor(ln));
        c.text(kX1 + 8, y, 3, 2, buf);
    }

    // header, inside the plot: title and the jog label at the top left; pattern / BPM / hint in the free corner of the right column
    c.color(seqpal::kTitle);
    c.text(kX0 + 8, kY0 + 12, 1, 2, title);
    snprintf(buf, sizeof buf, "Jog: time scale, linear %d s / total %d s", (int)(kLinMs[sc] / 1000), (int)(kTotalMs[sc] / 1000));
    c.color(seqpal::kText);
    c.text(kX0 + 150, kY0 + 12, 1, 2, buf);
    const Info& in = f.info;
    if (in.playing)
        snprintf(buf, sizeof buf, "P%02d %02d/%02d", in.pattern, in.pos, in.len);
    else
        snprintf(buf, sizeof buf, "P%02d stop", in.pattern);
    c.color(seqpal::kTitle);
    c.text(kX1 + 8, kY1 - 62, 1, 2, buf);
    snprintf(buf, sizeof buf, "BPM %d.%d", in.bpm10 / 10, in.bpm10 % 10);
    c.text(kX1 + 8, kY1 - 42, 1, 2, buf);
    c.color(seqpal::kText);
    c.text(kX1 + 8, kY1 - 22, 1, 2, hint);
    c.finish();
}

} // namespace ampview

#endif
