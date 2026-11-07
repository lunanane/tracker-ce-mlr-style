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
// averages), the master one neutral line. The lines are RESAMPLED at 200 columns of equal pixel width; the value of a column is read at
// the EXACT age of its x (and of the history that the audio side delivered up to now), from the tier whose buckets are at most half a
// column wide, so the contour moves smoothly. v0.4.3: the columns are smoothed in amplitude (a triangle of +-220 ms at the finest level;
// the peak line is held first and stays an upper contour), converted to dB, then thinned to the display-list budget by importance with
// ONE threshold for all lines (straight parts cost two points, busy parts keep their detail, the resolution drops evenly; when many lines
// share a small budget the next smoothing level makes the curves simpler instead of angular; the ends of every strip are always kept, so a
// line always reaches the y axis once the history covers the total age), and drawn with sub-pixel precision (1/16 px) as antialiased
// strips. The budget is counted exactly in display-list bytes against what the other shown controls leave of the 8172 byte frame
// (frameBudget, corrected by what the device measured): nothing is ever dropped.
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
    kMaxCols = 200,        // dense columns per line (3.3 px apart); the drawn points are thinned from them
    kMinCols = 16,         // the coarsest columns (only when even the strip ends of 200 columns do not fit)
    kScales = 4,
    kDefaultScale = 2,     // linear 8 s, total 30 s
    kDefaultBudget = 5400, // bytes of display list for the view (8172 - 400 margin - title bar 600 - strip / labels 1720)
    kFrameBytes = 8172,    // display.cpp step3: the shown controls are appended while the sum stays below this
    kFrameMargin = 400,
    kMinBudget = 2600,     // the view's budget never below this (it would not be readable) ...
    kMaxBudget = 6800,     // ... nor above what the two display stages take for sure (2 x (3840 - 12) - the largest strip 808)
    kLineSafety = 24,      // bytes kept free when the lines are fitted (rounding of the counting contexts)
    kLineSlots = kTracks * 2 + 1, // drawn lines: maxima + averages of the tracks, the master's averages
    kSub = 16,             // sub-pixel units per pixel of the drawn lines (VERTEX_FORMAT(4))
    kRankMax = 63,         // importance of a point that is always drawn (the ends of a strip); inner points 0 .. 62
    kRankMask = 0x3F,
    kStart = 0x40,         // the column starts a strip
    kEnd = 0x80,           // the column ends a strip
    kMaxRadius = 8,        // smoothing radius in columns, at most
    kManyLines = 9,        // more lines than this (five tracks or more): kDenseMany columns
    kMaxBk = 32,           // buckets of one column read in one go (denseAll)
    kPruneDev = 8,         // 1/16 px: a Douglas-Peucker segment that deviates less than half a pixel is not split further
    kDenseMany = 133,      // 5 px
    kLevels = 4,           // smoothing levels (more smoothing when many lines share a small budget)
    kThrSmooth = 22,       // importance threshold above which the lines get the next smoothing level (about 2.7 px of deviation dropped)
    kThrRelax = 15,        // ... and below which the previous level is tried again (about 1.2 px)
    kRelaxHold = 50        // frames before a lower level is tried again after it did not fit
};
// smoothing radius in time per level: the averages (a box of this radius twice = a triangle of twice the radius), the maxima (held twice
// the radius, then the same boxes). Level 0: the averages +-220 ms, the maxima held +-110 ms: every beat a smooth hump.
static const uint16_t kAvgMs[kLevels] = { 110, 110, 220, 330 };
static const uint16_t kPeakMs[kLevels] = { 55, 110, 220, 330 };
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
    const int e = 31 - __builtin_clz(peak); // the highest set bit
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
    static int shift(int t) { return 3 + kTierShift * t; } // log2 of span(t) (kGroup = 8): shifts instead of divisions
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
            Cell& c = cell_[base(t) + (((n_ >> shift(t)) - 1) & (cap(t) - 1))];
            c.avg = (uint16_t)((sum_[t] + s / 2) >> shift(t));
            c.mx = mx_[t];
            sum_[t] = 0;
            mx_[t] = 0;
        }
    }
    // Bucket j of tier t (absolute index, covers blocks j*S .. (j+1)*S - 1). The newest one (j = blocks / S) is the unfinished bucket.
    // false: not held (older than the ring, or no block in it yet).
    bool get(int t, uint32_t j, Bucket& b) const
    {
        const uint32_t s = span(t), f = n_ >> shift(t);
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
        const uint32_t f = n_ >> shift(t);
        return f > cap(t) ? f - cap(t) : 0;
    }
    // age in blocks that tier t covers completely (the start of its oldest held bucket, counted back from now)
    uint32_t reach(int t) const { return n_ - oldestHeld(t) * span(t); }
    // bucket of tier t that holds the block of the given age (0 = the newest block); a must be < blocks()
    uint32_t indexAt(int t, uint32_t a) const { return (n_ - 1 - a) >> shift(t); }

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

// a column of a frame: the ages of its left / right edge and centre (blocks) and the tier it reads
struct Col
{
    float aL, aR, ac; // ages in blocks
    float ms;         // width in ms
    int32_t t;        // tier
};

// The lines of one frame (prepareLines): computed when stage 0 draws, taken as they are by stage 1 (the follower does not change in between)
struct LineCache
{
    bool valid;
    uint32_t key[4];  // blocks, scale | budget, audible lanes, bytes of the rest of the view
    int16_t cols;     // dense columns of this frame
    int16_t first;    // the first (oldest) column inside the history
    uint8_t thr;      // points of importance >= thr are drawn
    uint8_t count;    // lines
    uint8_t level;    // smoothing level of this frame (kept for the next frame)
    uint8_t relaxHold;
    uint8_t lane[kLineSlots], pass[kLineSlots];
    int strips, points;
    int bytes;        // display list of the whole view (what the device appends to the frame)
    Col col[kMaxCols];
    float db[kMaxCols], tmp[kMaxCols], pre[kMaxCols + 1], raw[kMaxCols];
    uint8_t rad[2][kMaxCols]; // smoothing radii of the maxima / averages
    int16_t y[kLineSlots][kMaxCols];  // 1/16 px; -1 = not on a strip
    uint8_t rf[kLineSlots][kMaxCols]; // importance (0 .. 63) | kStart | kEnd
    int16_t stk[3 * (kMaxCols + 2)];
    mutable int16_t out[(kMaxCols + 1) * 2];
};

class Follower
{
  public:
    Follower() { reset(); }
    Follower(const Follower& o) { *this = o; }
    Follower& operator=(const Follower& o) // the history and the settings; the drawing scratch is not copied
    {
        memcpy(lane_, o.lane_, sizeof lane_);
        blocks_ = o.blocks_;
        info = o.info;
        scale = o.scale;
        budget = o.budget;
        cache.valid = false;
        cache.level = 0;
        cache.relaxHold = 0;
        return *this;
    }
    void reset()
    {
        for (int i = 0; i < kLanes; i++) lane_[i].reset();
        blocks_ = 0;
        cache.valid = false;
        cache.level = 0;
        cache.relaxHold = 0;
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
    uint16_t budget = kDefaultBudget; // bytes of display list the drawing may use (the entry point: frameBudget of what the others leave)
    mutable LineCache cache;          // scratch of the drawing (EXTERNAL_RAM with the follower on the device)

  private:
    Cascade lane_[kLanes];
    uint32_t blocks_;
};

// the smallest peak level that lies above the floor (dbx64 is monotone)
inline uint32_t floorPeak()
{
    uint32_t lo = 1, hi = 32768; // dbx64(hi) > floor >= dbx64(lo)
    while (hi - lo > 1)
    {
        const uint32_t m = (lo + hi) / 2;
        if (dbx64(m) > kFloorDb * 64) hi = m;
        else lo = m;
    }
    return hi;
}
// Does a lane have anything above the floor (silent lanes are not drawn): looked up in the oldest tier, which holds the whole displayed range
inline bool audible(const Cascade& c, uint32_t fp = floorPeak())
{
    const int t = kTiers - 1;
    const uint32_t f = c.blocks() >> Cascade::shift(t);
    Bucket b;
    for (uint32_t j = c.oldestHeld(t); j <= f; j++)
        if (c.get(t, j, b) && b.mx >= fp) return true;
    return false;
}

// ---- the lines (v0.4.3) ---------------------------------------------------------------------------------------------------------
// Every lane is first sampled at `cols` columns of equal width (dense: 200 = 3.3 px, 133 = 5 px with five tracks or more), smoothed
// (smoothLine, levels kAvgMs / kPeakMs), then THINNED to what the display list can take: each point gets an importance (how far it lies from the line between its neighbours of the coarser level, Douglas-Peucker),
// and ONE threshold for all lines keeps the most important points that fit the budget. Straight or quiet parts cost two points, busy
// parts keep their detail, and the resolution drops evenly over all lines when the budget is small; nothing is cut off, the two ends
// of every strip (the oldest column at the y axis, the newest at the right edge) are always kept. The points are drawn with
// sub-pixel precision (1/16 px, VERTEX_FORMAT(4)) as antialiased line strips, so slopes look smooth.

// Columns of a frame (left = oldest = 0 .. cols - 1 = newest at the right edge). Returns the first column inside the history (cols = none).
// The tier: the coarsest one whose buckets are at most half a column wide, a coarser one where the finer ring does not reach back. All
// lanes are pushed together, so the columns (and what the tiers hold) are the same for every lane.
inline int buildCols(const Cascade& cs, int cols, int scale, Col* m)
{
    const uint32_t P = cs.blocks();
    const float bpms = 1.0f / kBlockMs;
    const float du = 1.0f / (float)(cols - 1);
    int first = cols;
    for (int j = cols - 1; j >= 0; j--) // newest first; the first column older than the history ends the line
    {
        const float u = (float)(cols - 1 - j) * du; // j = cols - 1 -> u = 0 (now)
        const float uL = u - 0.5f * du < 0 ? 0.0f : u - 0.5f * du, uR = u + 0.5f * du > 1 ? 1.0f : u + 0.5f * du;
        const float aL = ageMsOfU(uL, scale) * bpms, aR = ageMsOfU(uR, scale) * bpms, ac = ageMsOfU(u, scale) * bpms;
        if (!(ac < (float)P)) break; // older than the history
        const float colSpan = aR - aL;
        int t = 0;
        while (t + 1 < kTiers && 2.0f * (float)Cascade::span(t + 1) <= colSpan) t++;
        while (t + 1 < kTiers && aR > (float)cs.reach(t)) t++;
        const uint32_t jn = cs.indexAt(t, (uint32_t)aL);
        uint32_t jo = cs.indexAt(t, aR >= (float)(P - 1) ? P - 1 : (uint32_t)aR);
        if (jo < cs.oldestHeld(t)) jo = cs.oldestHeld(t);
        if (jn < jo) break; // older than the oldest tier holds
        Bucket b;
        if (jn == jo && !cs.get(t, jn, b)) break;
        m[j].aL = aL;
        m[j].aR = aR;
        m[j].ac = ac;
        m[j].ms = (aR - aL) * kBlockMs;
        m[j].t = t;
        first = j;
    }
    return first;
}

// Level (0 .. 65535, the block peak scale) of one lane in one column (which 0 = maxima, 1 = averages): the average (weighted by the overlap)
// or the maximum of the buckets in the column; inside one bucket linear between the centres of it and its neighbour (smooth contour, no
// steps). v0.4.3: in amplitude, like the buckets themselves (the lines are smoothed in amplitude and converted to dB after that, so a
// column of the compressed part, which averages long buckets, and a column of the linear part read the same way)
inline float colLevel(const Cascade& cs, const Col& m, int which)
{
    const uint32_t P = cs.blocks();
    const int t = m.t;
    const uint32_t S = Cascade::span(t);
    const uint32_t jn = cs.indexAt(t, (uint32_t)m.aL);
    uint32_t jo = cs.indexAt(t, m.aR >= (float)(P - 1) ? P - 1 : (uint32_t)m.aR);
    if (jo < cs.oldestHeld(t)) jo = cs.oldestHeld(t);
    Bucket b;
    if (jn > jo)
    {
        float sum = 0, wsum = 0;
        uint32_t mx = 0;
        for (uint32_t q = jo; q <= jn; q++)
        {
            if (!cs.get(t, q, b)) continue;
            const float e = (float)(P - q * S), s = e - (float)b.span;
            const float lo = s > m.aL ? s : m.aL, hi = e < m.aR ? e : m.aR;
            const float w = hi > lo ? hi - lo : 0.0f;
            const uint32_t v = which ? b.avg : b.mx;
            sum += w * (float)v;
            wsum += w;
            if (v > mx) mx = v;
        }
        return which ? (wsum > 0 ? sum / wsum : 0.0f) : (float)mx;
    }
    if (!cs.get(t, jn, b)) return 0.0f;
    const float e = (float)(P - jn * S), c0 = e - 0.5f * (float)b.span;
    float lv = (float)(which ? b.avg : b.mx);
    const uint32_t nj = m.ac < c0 ? jn + 1 : jn - 1; // newer / older neighbour
    Bucket nb;
    if ((m.ac < c0 || jn > 0) && cs.get(t, nj, nb))
    {
        const float ne = (float)(P - nj * S), c1 = ne - 0.5f * (float)nb.span;
        float tt = c1 != c0 ? (m.ac - c0) / (c1 - c0) : 0.0f;
        tt = tt < 0 ? 0 : (tt > 1 ? 1 : tt);
        lv += ((float)(which ? nb.avg : nb.mx) - lv) * tt;
    }
    return lv;
}

// dB x 64 of a smoothed level, clipped 6 dB below the floor
inline float lineDb(float level)
{
    const float lowest = (float)((kFloorDb - 6) * 64), d = (float)dbx64((uint32_t)(level + 0.5f));
    return d < lowest ? lowest : d;
}

// smoothing radii (columns) of the n columns m[0 .. n - 1] for a radius of `ms` milliseconds: at least rmin (1: a wide column of the
// compressed part still gets its neighbours, no aliasing zigzag), at most kMaxRadius; neighbours differ by one at
// most, so both ends of the smoothing windows move monotonically from column to column
inline void radii(const Col* m, int n, float ms, int rmin, uint8_t* rad)
{
    for (int i = 0; i < n; i++)
    {
        const int r = (int)(ms / m[i].ms + 0.5f);
        rad[i] = (uint8_t)(r < rmin ? rmin : (r > kMaxRadius ? kMaxRadius : r));
    }
    for (int i = 1; i < n; i++)
        if (rad[i] > rad[i - 1] + 1) rad[i] = (uint8_t)(rad[i - 1] + 1);
    for (int i = n - 2; i >= 0; i--)
        if (rad[i] > rad[i + 1] + 1) rad[i] = (uint8_t)(rad[i + 1] + 1);
}

// box mean over the window i - rad[i] .. i + rad[i] (cut at the ends, not padded)
inline void boxMean(float* v, float* out, float* pre, const uint8_t* rad, int n)
{
    pre[0] = 0;
    for (int i = 0; i < n; i++) pre[i + 1] = pre[i] + v[i];
    for (int i = 0; i < n; i++)
    {
        const int r = rad[i], lo = i - r < 0 ? 0 : i - r, hi = i + r > n - 1 ? n - 1 : i + r;
        out[i] = (pre[hi + 1] - pre[lo]) / (float)(hi - lo + 1);
    }
    for (int i = 0; i < n; i++) v[i] = out[i];
}

// Smoothing of one line (n values, dB x 64; radii from radii()). Averages: the box mean twice (a triangle): every value stays between the
// smallest and the largest value within 2 r, a monotone line stays monotone (the windows' ends move monotonically). Maxima: first the largest
// value within 2 r (held), then the same two box means, at least the raw value: never below the raw maximum (the peak line stays an upper
// contour, a hit is never shaved off), never above the largest value within 4 r. Smooth humps instead of teeth.
inline void smoothLine(float* v, float* tmp, float* pre, float* raw, const uint8_t* rad, int n, int which)
{
    if (n < 2) return;
    if (which == 0)
    {
        for (int i = 0; i < n; i++)
        {
            const int r = 2 * rad[i], lo = i - r < 0 ? 0 : i - r, hi = i + r > n - 1 ? n - 1 : i + r;
            float mx = v[lo];
            for (int k = lo + 1; k <= hi; k++) mx = v[k] > mx ? v[k] : mx;
            tmp[i] = mx;
        }
        for (int i = 0; i < n; i++)
        {
            raw[i] = v[i];
            v[i] = tmp[i];
        }
    }
    boxMean(v, tmp, pre, rad, n);
    boxMean(v, tmp, pre, rad, n);
    if (which == 0)
        for (int i = 0; i < n; i++) v[i] = v[i] < raw[i] ? raw[i] : v[i];
}

// the values of one lane over the columns first .. cols - 1, smoothed with the radii rad (n = cols - first), in dB x 64 clipped 6 dB below
// the floor
inline void denseLine(const Cascade& cs, const Col* m, int first, int cols, int which, const uint8_t* rad, float* db, float* tmp, float* pre,
                      float* raw)
{
    for (int j = first; j < cols; j++) db[j - first] = colLevel(cs, m[j], which);
    smoothLine(db, tmp, pre, raw, rad, cols - first, which);
    for (int k = 0; k < cols - first; k++) db[k] = lineDb(db[k]);
}

// The values of ALL lines in one pass over the columns (the buckets of a column, their weights and the interpolation are the same for every
// lane, they are worked out once): the level of line i in column j, halved and rounded (0 .. 32767), into L.y[i][j]; the same values as
// colLevel() of each lane (host test). The smoothing and the dB follow per line.
inline int16_t halfLevel(float lv) { return (int16_t)((int)(lv + 1.0f) >> 1); } // 0 .. 32767
inline void denseAll(const Follower& f, LineCache& L, int first, int cols)
{
    const Cascade& c0 = f.lane(0);
    const uint32_t P = c0.blocks();
    for (int j = first; j < cols; j++)
    {
        const Col& m = L.col[j];
        const int t = m.t;
        const uint32_t S = Cascade::span(t);
        const uint32_t jn = c0.indexAt(t, (uint32_t)m.aL);
        uint32_t jo = c0.indexAt(t, m.aR >= (float)(P - 1) ? P - 1 : (uint32_t)m.aR);
        if (jo < c0.oldestHeld(t)) jo = c0.oldestHeld(t);
        Bucket b = { 0, 0, 0 };
        if (jn > jo && jn - jo < (uint32_t)kMaxBk)
        {
            float w[kMaxBk];
            bool held[kMaxBk];
            const int nq = (int)(jn - jo + 1);
            for (int k = 0; k < nq; k++)
            {
                const uint32_t q = jo + (uint32_t)k;
                held[k] = c0.get(t, q, b);
                if (!held[k]) continue;
                const float e = (float)(P - q * S), s0 = e - (float)b.span;
                const float lo = s0 > m.aL ? s0 : m.aL, hi = e < m.aR ? e : m.aR;
                w[k] = hi > lo ? hi - lo : 0.0f;
            }
            for (int i = 0; i < L.count; i++)
            {
                const Cascade& cs = f.lane(L.lane[i]);
                const int which = L.pass[i];
                float sum = 0, wsum = 0;
                uint32_t mx = 0;
                for (int k = 0; k < nq; k++)
                {
                    if (!held[k]) continue;
                    cs.get(t, jo + (uint32_t)k, b);
                    const uint32_t v = which ? b.avg : b.mx;
                    sum += w[k] * (float)v;
                    wsum += w[k];
                    if (v > mx) mx = v;
                }
                L.y[i][j] = halfLevel(which ? (wsum > 0 ? sum / wsum : 0.0f) : (float)mx);
            }
        }
        else if (jn > jo) // more buckets than kMaxBk in one column (only the coarsest fallback columns): lane by lane
        {
            for (int i = 0; i < L.count; i++) L.y[i][j] = halfLevel(colLevel(f.lane(L.lane[i]), m, L.pass[i]));
        }
        else
        {
            if (!c0.get(t, jn, b)) // (buildCols does not let such a column through)
            {
                for (int i = 0; i < L.count; i++) L.y[i][j] = 0;
                continue;
            }
            const float e = (float)(P - jn * S), cc = e - 0.5f * (float)b.span;
            const uint32_t nj = m.ac < cc ? jn + 1 : jn - 1; // newer / older neighbour
            Bucket nb = { 0, 0, 0 };
            const bool hasN = (m.ac < cc || jn > 0) && c0.get(t, nj, nb);
            float tt = 0;
            if (hasN)
            {
                const float ne = (float)(P - nj * S), c1 = ne - 0.5f * (float)nb.span;
                tt = c1 != cc ? (m.ac - cc) / (c1 - cc) : 0.0f;
                tt = tt < 0 ? 0 : (tt > 1 ? 1 : tt);
            }
            for (int i = 0; i < L.count; i++)
            {
                const Cascade& cs = f.lane(L.lane[i]);
                const int which = L.pass[i];
                cs.get(t, jn, b);
                float lv = (float)(which ? b.avg : b.mx);
                if (hasN)
                {
                    cs.get(t, nj, nb);
                    lv += ((float)(which ? nb.avg : nb.mx) - lv) * tt;
                }
                L.y[i][j] = halfLevel(lv);
            }
        }
    }
}

// x of a column in 1/16 px (exact ends: column 0 = the y axis, cols - 1 = the right edge)
inline int x16Of(int j, int cols) { return kX0 * kSub + ((kX1 - kX0) * kSub * j + (cols - 1) / 2) / (cols - 1); }
inline float yOfF(float db) { return (float)kY1 - heightOf(db) * (float)(kY1 - kY0); }

// strip flags of column k of a line of n values (dB x 64): -1 = not drawn. Strips are the runs above the floor, each with the column
// before and after it at the floor (a run rises from / falls to the floor; no long joins across silence); a floor column between two
// runs ends the one (kEnd) and starts the other (kStart)
inline bool above(const float* db, int n, int i) { return i >= 0 && i < n && db[i] > (float)(kFloorDb * 64); }
inline int stripFlags(const float* db, int n, int k)
{
    const bool in = above(db, n, k), inL = above(db, n, k - 1), inR = above(db, n, k + 1);
    if (!(in || inL || inR)) return -1;
    const bool drawnL = k > 0 && (inL || in || above(db, n, k - 2));
    const bool drawnR = k + 1 < n && (inR || in || above(db, n, k + 2));
    int fl = 0;
    if (!drawnL || (!in && inR)) fl |= kStart;
    if (!drawnR || (!in && inL)) fl |= kEnd;
    return fl;
}

// importance of a point (vertical distance to the coarser line in 1/16 px): 0 .. 62, about 6 log2(1 + dev / 4), monotone
inline int rankOf(int dev16)
{
    const uint32_t v = 1u + (uint32_t)(dev16 < 0 ? 0 : dev16) / 4u;
    const int e = 31 - __builtin_clz(v);
    const int frac = e >= 3 ? (int)((v >> (e - 3)) & 7u) : (int)((v << (3 - e)) & 7u);
    const int q = 6 * (e * 8 + frac) / 8;
    return q > kRankMax - 1 ? kRankMax - 1 : q;
}

// Douglas-Peucker ranks of the inner points of one strip (columns a .. b of y, 1/16 px): a point's rank is its distance to the line between
// the two points that split it off, never above the rank of that split (so a kept point always has its two neighbours of the coarser level)
inline void rankStrip(const int16_t* y, uint8_t* rf, int a, int b, int16_t* stk, float prune = (float)kPruneDev)
{
    int sp = 0;
    stk[sp++] = (int16_t)a;
    stk[sp++] = (int16_t)b;
    stk[sp++] = (int16_t)(kRankMax - 1);
    while (sp)
    {
        const int pr = stk[--sp], r = stk[--sp], l = stk[--sp];
        if (r - l < 2) continue;
        int m = l + 1;
        float best = -1;
        const float yl = (float)y[l], k = (float)(y[r] - y[l]) / (float)(r - l);
        for (int i = l + 1; i < r; i++)
        {
            const float d = fabsf((float)y[i] - (yl + k * (float)(i - l)));
            if (d > best)
            {
                best = d;
                m = i;
            }
        }
        int q = rankOf((int)(best + 0.5f));
        if (q > pr) q = pr;
        if (best < prune) // nothing in this segment deviates by more: all its points get this rank (counted so), no deeper split
        {
            for (int i = l + 1; i < r; i++) rf[i] = (uint8_t)((rf[i] & ~kRankMask) | q);
            continue;
        }
        rf[m] = (uint8_t)((rf[m] & ~kRankMask) | q);
        stk[sp++] = (int16_t)l;
        stk[sp++] = (int16_t)m;
        stk[sp++] = (int16_t)q;
        stk[sp++] = (int16_t)m;
        stk[sp++] = (int16_t)r;
        stk[sp++] = (int16_t)q;
    }
}

// display list bytes of a text as the co-processor writes it (CMD_TEXT: a few setup words, then a cell and a vertex per character; an
// upper bound, the device measures what it really took and corrects the budget, see frameBudget)
inline int dlTextBytes(const char* s) { return 16 + 8 * (int)strlen(s); }

// counts the display list bytes of a drawing (the same sizes as traceControl.cpp, texts by dlTextBytes)
struct CountCtx
{
    int bytes = 0;
    void color(uint32_t) { bytes += 4; }
    void width(int) { bytes += 4; }
    void rect(int, int, int, int) { bytes += 20; }
    void lines(const int16_t*, int n) { bytes += 8 + 8 * n; }
    void strip(const int16_t*, int n) { bytes += 8 + 4 * n; }
    void subpixel(bool) { bytes += 4; }
    void textPhase() { bytes += 4; }
    void text(int, int, int, int, const char* s) { bytes += dlTextBytes(s); }
    void finish() {}
};

// The frame (display.cpp step3 appends the shown controls while the sum stays under 8172 bytes; a control that does not fit is not drawn at
// all): what the view may use next to the others. corr: how much the view's last display list was larger (> 0) or smaller (< 0) than its
// own estimate, measured on the device (texts: the estimate is an upper bound, so usually < 0 and given back to the lines).
inline int frameBudget(int others, int corr)
{
    const int room = kFrameBytes - kFrameMargin - others - corr;
    return room < kMinBudget ? kMinBudget : (room > kMaxBudget ? kMaxBudget : room);
}

// The lines of a frame at one smoothing level: dense values, strips, ranks; then the lowest threshold that fits `room` (thr = kRankMax keeps
// the strip ends only; -1: not even those fit). Falls back to coarser columns when even the strip ends of the finer ones do not fit.
// hint: the threshold of the last frame at the same level and columns (-1: none). Segments are not split below the deviation of a few ranks
// under it (they would not be drawn anyway; if the threshold does drop that far their points are kept or dropped together, counted exactly).
inline int computeLines(const Follower& f, LineCache& L, int room, int level, int hint = -1)
{
    float prune = (float)kPruneDev;
    if (hint > 6)
    {
        const float d = 4.0f * (exp2f((float)(hint - 6) / 6.0f) - 1.0f); // the deviation of rank hint - 6 (rankOf inverted)
        prune = d > prune ? d : prune;
    }
    int cols = L.count > kManyLines ? (int)kDenseMany : (int)kMaxCols; // many lines share the budget: 5 px columns are plenty (less CPU)
    for (;;)
    {
        L.cols = (int16_t)cols;
        L.first = (int16_t)buildCols(f.lane(0), cols, f.scale, L.col);
        const int first = L.first, n = cols - first;
        if (n > 0)
        {
            radii(L.col + first, n, (float)kPeakMs[level], 1, L.rad[0]);
            radii(L.col + first, n, (float)kAvgMs[level], 1, L.rad[1]);
        }
        int hist[kRankMax];
        for (int q = 0; q < kRankMax; q++) hist[q] = 0;
        int ends = 0, strips = 0;
        if (n > 0) denseAll(f, L, first, cols);
        for (int i = 0; i < L.count; i++)
        {
            int16_t* y = L.y[i];
            uint8_t* rf = L.rf[i];
            for (int k = 0; k < n; k++) L.db[k] = 2.0f * (float)y[first + k];
            for (int j = 0; j < cols; j++)
            {
                y[j] = -1;
                rf[j] = 0;
            }
            if (n <= 0) continue;
            smoothLine(L.db, L.tmp, L.pre, L.raw, L.rad[L.pass[i]], n, L.pass[i]);
            for (int k = 0; k < n; k++) L.db[k] = lineDb(L.db[k]);
            for (int k = 0; k < n; k++)
            {
                const int fl = stripFlags(L.db, n, k);
                if (fl < 0) continue;
                const int j = first + k;
                y[j] = (int16_t)(above(L.db, n, k) ? (int)(yOfF(L.db[k] * (1.0f / 64.0f)) * (float)kSub + 0.5f) : kY1 * kSub);
                rf[j] = (uint8_t)fl;
            }
            // ranks per strip (a floor column between two runs ends the one and starts the next; a single point is no strip)
            int s = -1;
            for (int j = first; j < cols; j++)
            {
                if (y[j] < 0) continue;
                if ((rf[j] & kEnd) && s >= 0 && s < j)
                {
                    rankStrip(y, rf, s, j, L.stk, prune);
                    rf[s] |= kRankMax;
                    rf[j] |= kRankMax;
                    ends += 2;
                    strips++;
                    for (int q = s + 1; q < j; q++) hist[rf[q] & kRankMask]++;
                    s = -1;
                }
                if (rf[j] & kStart) s = j;
            }
        }
        int thr = -1, cum = ends;
        for (int q = kRankMax; q >= 0; q--)
        {
            if (q < kRankMax) cum += hist[q];
            if (strips * 8 + 4 * cum > room) break;
            thr = q;
        }
        if (thr >= 0 || cols <= kMinCols)
        {
            L.thr = (uint8_t)(thr < 0 ? kRankMax : thr);
            L.strips = strips;
            int pts = ends;
            for (int q = L.thr; q < kRankMax; q++) pts += hist[q];
            L.points = pts;
            return thr;
        }
        cols = cols / 2 < kMinCols ? kMinCols : cols / 2; // even the strip ends do not fit (very many breaks): coarser columns
    }
}

// Lines of a frame: computed once per frame (both display stages draw the same data) or taken from the cache. The smoothing level follows
// the budget: when the threshold that fits drops more than about 2.7 px of detail, the next level is used (the curves get simpler instead
// of angular); the level of the last frame is the start, a lower one is tried again when the lines fit easily (hysteresis, kRelaxHold).
inline const LineCache& prepareLines(const Follower& f, const bool* aud, int fixedBytes)
{
    LineCache& L = f.cache;
    uint32_t mask = 0;
    for (int ln = 0; ln < kLanes; ln++) mask |= aud[ln] ? 1u << ln : 0u;
    const int budget = f.budget > kMaxBudget ? (int)kMaxBudget : (int)f.budget;
    const uint32_t key[4] = { f.blocks(), (uint32_t)f.scale | ((uint32_t)budget << 8), mask, (uint32_t)fixedBytes };
    if (L.valid && memcmp(L.key, key, sizeof key) == 0) return L;
    memcpy(L.key, key, sizeof key);
    L.valid = true;
    L.count = 0;
    for (int pass = 0; pass < 2; pass++)
        for (int ln = 0; ln < kLanes; ln++)
            if (aud[ln] && !(ln == kTracks && pass == 0))
            {
                L.lane[L.count] = (uint8_t)ln;
                L.pass[L.count] = (uint8_t)pass;
                L.count++;
            }
    const int prevThr = L.thr, prevCols = L.cols; // of the last frame
    L.points = 0;
    L.strips = 0;
    L.thr = kRankMax;
    L.cols = kMaxCols;
    L.first = kMaxCols;
    if (L.level >= kLevels) L.level = 0;
    if (!L.count)
    {
        L.bytes = fixedBytes;
        return L;
    }
    // what the lines may take: the budget less the rest of the view, a safety margin, the sub-pixel switches and colour + width per line
    const int room = budget - fixedBytes - kLineSafety - 8 - 8 * L.count;
    int level = L.level;
    const bool tryLower = level > 0 && L.relaxHold == 0 && prevThr < kThrRelax;
    if (L.relaxHold) L.relaxHold--;
    if (tryLower) level--;
    int thr = computeLines(f, L, room, level, level == L.level && prevCols == (L.count > kManyLines ? (int)kDenseMany : (int)kMaxCols) ? prevThr : -1);
    while ((thr < 0 || thr > kThrSmooth) && level + 1 < kLevels)
    {
        L.relaxHold = kRelaxHold; // stay at the higher level for a while before a lower one is tried again
        level++;
        thr = computeLines(f, L, room, level);
    }
    L.level = (uint8_t)level;
    L.bytes = fixedBytes + 8 + 8 * L.count + 8 * L.strips + 4 * L.points;
    return L;
}

// One line over M columns for the tests and tools (pixels): (x, y) pairs from the oldest column to the newest at the right edge, y = -1
// marks a break (the level fell below the floor). Every column is a point (no thinning). Returns the number of entries.
inline int resample(const Cascade& cs, int cols, int which, int scale, int16_t* o)
{
    Col m[kMaxCols];
    float db[kMaxCols], tmp[kMaxCols], pre[kMaxCols + 1], raw[kMaxCols];
    uint8_t rad[kMaxCols];
    const int first = buildCols(cs, cols, scale, m);
    if (first >= cols) return 0;
    radii(m + first, cols - first, (float)(which ? kAvgMs[0] : kPeakMs[0]), 1, rad);
    denseLine(cs, m, first, cols, which, rad, db, tmp, pre, raw);
    const int n = cols - first;
    int w = 0;
    for (int k = 0; k < n; k++)
    {
        const int fl = stripFlags(db, n, k);
        if (fl < 0) continue;
        const bool in = above(db, n, k);
        const int16_t x = (int16_t)(kX0 + ((kX1 - kX0) * (first + k) + (cols - 1) / 2) / (cols - 1));
        const int16_t y = (int16_t)(in ? yOf(db[k] * (1.0f / 64.0f)) : kY1);
        o[w * 2] = x;
        o[w * 2 + 1] = y;
        w++;
        if ((fl & kEnd) && !in) // a run fell to the floor: break; a floor column between two runs starts the next one again
        {
            o[w * 2] = x;
            o[w * 2 + 1] = -1;
            w++;
            if (fl & kStart)
            {
                o[w * 2] = x;
                o[w * 2 + 1] = y;
                w++;
            }
        }
    }
    return w;
}

static const int kMarkMs[7] = { 1000, 2000, 4000, 8000, 15000, 30000, 60000 };
static const char* const kMarkText[7] = { "1s", "2s", "4s", "8s", "15s", "30s", "60s" };
static const int kDbLines[5] = { 0, -12, -24, -36, -48 };
static const char* const kDbText[5] = { "0", "-12", "-24", "-36", "-48" };
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

// the panel, the grid and the scale labels
template <class C> void drawBack(C& c, const Follower& f)
{
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
        c.text(kX0 - 6, i == 0 ? kY0 + 7 : (i == 4 ? kY1 - 7 : yOf((float)kDbLines[i])), 1, 2 | 4, kDbText[i]);
    for (int i = 0; i < marks; i++)
        c.text(mX[i] < kX0 + 20 ? kX0 + 20 : mX[i], kY1 - 22, 1, 1 | 2, kMarkText[mIdx[i]]); // not on top of the -48 label
    c.text(kX1 - 4, kY1 - 22, 1, 4 | 2, "now");
}

// the lines of the cache: maxima dim behind (pass 0), averages on top (pass 1), the master neutral and without a maxima line; sub-pixel
template <class C> void drawLines(C& c, const LineCache& L)
{
    if (!L.count) return;
    c.subpixel(true);
    int16_t* o = L.out;
    for (int i = 0; i < L.count; i++)
    {
        const int ln = L.lane[i], pass = L.pass[i];
        const uint32_t col = ln == kTracks ? seqpal::kTitle : seqpal::trackColor(ln);
        c.color(pass == 0 ? seqpal::faint(col) : col);
        c.width(pass == 0 ? 10 : (ln == kTracks ? 12 : 16));
        const int16_t* y = L.y[i];
        const uint8_t* rf = L.rf[i];
        int n = 0;
        bool open = false;
        for (int j = L.first; j < L.cols; j++)
        {
            if (y[j] < 0) continue;
            const bool keep = (rf[j] & kRankMask) >= L.thr;
            if (open && keep)
            {
                o[n * 2] = (int16_t)x16Of(j, L.cols);
                o[n * 2 + 1] = y[j];
                n++;
            }
            if (open && (rf[j] & kEnd))
            {
                if (n >= 2) c.strip(o, n);
                open = false;
            }
            if (rf[j] & kStart)
            {
                open = true;
                o[0] = (int16_t)x16Of(j, L.cols);
                o[1] = y[j];
                n = 1;
            }
        }
    }
    c.subpixel(false);
}

// the formatted texts of a frame (once per draw: the counting pass and the drawing use them)
struct Labels
{
    char lvl[kLanes][12];
    char jog[48];
    char pat[16];
    char bpm[16];
};
inline void makeLabels(const Follower& f, const bool* aud, Labels& lb)
{
    const int sc = f.scale;
    for (int ln = 0; ln < kLanes; ln++)
    {
        if (!aud[ln]) continue;
        const float db = dbx64(f.newest(ln)) * (1.0f / 64.0f);
        const char tag = ln == kTracks ? 'M' : (char)('1' + ln);
        if (db <= (float)kFloorDb)
            snprintf(lb.lvl[ln], sizeof lb.lvl[ln], "%c  --", tag);
        else
            snprintf(lb.lvl[ln], sizeof lb.lvl[ln], "%c %d", tag, (int)(db < 0 ? db - 0.5f : db + 0.5f));
    }
    snprintf(lb.jog, sizeof lb.jog, "Jog: zoom %d s / %d s", (int)(kLinMs[sc] / 1000), (int)(kTotalMs[sc] / 1000));
    const Info& in = f.info;
    if (in.playing)
        snprintf(lb.pat, sizeof lb.pat, "P%02d %02d/%02d", in.pattern, in.pos, in.len);
    else
        snprintf(lb.pat, sizeof lb.pat, "P%02d stop", in.pattern);
    snprintf(lb.bpm, sizeof lb.bpm, "BPM %d.%d", in.bpm10 / 10, in.bpm10 % 10);
}

// current levels at the right edge (the audible tracks and M) and the header (title and the jog label at the top left; pattern / BPM / hint
// in the free corner of the right column)
template <class C> void drawTexts(C& c, const bool* aud, const Labels& lb, const char* title, const char* hint)
{
    for (int ln = 0; ln < kLanes; ln++)
    {
        if (!aud[ln]) continue;
        const uint32_t tc = ln == kTracks ? seqpal::kTitle : seqpal::trackColor(ln);
        c.color(strstr(lb.lvl[ln], "--") ? seqpal::dim(tc) : tc); // a silent track is off: its label is dimmed
        c.text(kX1 + 8, kY0 + 14 + ln * 28, 3, 2, lb.lvl[ln]);
    }
    c.color(seqpal::kTitle);
    c.text(kX0 + 8, kY0 + 12, 1, 2, title);
    c.color(seqpal::kText);
    c.text(kX0 + 150, kY0 + 12, 1, 2, lb.jog);
    c.color(seqpal::kTitle);
    c.text(kX1 + 8, kY1 - 62, 1, 2, lb.pat);
    c.text(kX1 + 8, kY1 - 42, 1, 2, lb.bpm);
    if (hint[0]) // the screen button under the panel says Close already
    {
        c.color(seqpal::kText);
        c.text(kX1 + 8, kY1 - 22, 1, 2, hint);
    }
}

// C as in pitchtrace::draw (color, width, rect, lines, strip, textPhase, text, finish) plus subpixel(on): the strips between subpixel(true)
// and subpixel(false) are in 1/16 px (VERTEX_FORMAT(4) on the FT81x)
template <class C> void draw(C& c, const Follower& f, const char* title, const char* hint)
{
    const bool history = f.lane(0).blocks() >= 2 * (uint32_t)kGroup;
    bool aud[kLanes];
    const uint32_t fp = floorPeak();
    for (int ln = 0; ln < kLanes; ln++) aud[ln] = history && audible(f.lane(ln), fp);
    Labels lb;
    makeLabels(f, aud, lb);
    CountCtx k; // the rest of the view, counted exactly: the lines take what is left of the budget
    drawBack(k, f);
    drawTexts(k, aud, lb, title, hint);
    const LineCache& L = prepareLines(f, aud, k.bytes);
    drawBack(c, f);
    drawLines(c, L);
    drawTexts(c, aud, lb, title, hint);
    c.finish();
}

} // namespace ampview

#endif
