// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Master amplitude view (plan 013 M, redesigned in plan 014 B, plan 015 V): one level contour per track plus the master, on a time axis
// that is almost linear for the last seconds and compresses towards the left edge. New data enters at the right edge. This header holds
//   - the dB conversion by table (no log per sample),
//   - Cascade: the history of one lane. The audio side delivers ONE PEAK PER AUDIO BLOCK (2.9 ms). kGroup = 8 blocks (23 ms) form the
//     finest bucket (average of the block peaks, maximum kept); the newest kLinCap (192 = 4.4 s) finest buckets are kept as they are (a
//     ring); older ones merge pairwise into an exponential histogram (kPerLevel buckets per span 16, 32, 64, ... blocks, the oldest
//     bucket absorbs the rest). Constant memory (kMaxBuckets) and O(1) work per block; no per-sample work anywhere,
//   - the time axis (age -> x): x = X1 - W * u, age(u) = H * (c * u + (1 - c) * u^6): a blend of a straight line and a power curve. With
//     the default the right half of the width shows the last 4 s almost linearly, the compression builds up gradually and the left edge
//     is the full history H = 120 s. The jog wheel picks one of seven "right half = 2 / 3 / 4 / 6 / 10 / 20 / 60 s" scales,
//   - the layout over the full height of the screen between the title bar and the strip, and the drawing over a tiny context (the same code
//     runs on the FT81x display list in interface/masterParams/ampView.cpp and in the host tests). No Polyend includes.
//
// Drawing: every audible track is two thin contour lines (the dim line of the bucket maxima = peaks, in front of it the line of the
// averages), the master one neutral line. The lines are RESAMPLED at M columns of equal pixel width (M follows the display-list budget and
// the number of audible lanes: 16 .. 200 points per line), so the resolution on the screen does not depend on the bucket structure.
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
    kLinCap = 192,         // finest buckets kept as they are (4.4 s)
    kPerLevel = 4,         // buckets per coarser span
    kLevels = 12,          // level 0 = kGroup blocks, level l = kGroup << l; the top level absorbs everything older
    kCoarseMax = kPerLevel * (kLevels - 1) + 3,
    kMaxBuckets = kLinCap + kCoarseMax,
    kHorizonMs = 120000,   // the left edge of the time axis
    kX0 = 56,              // plot area: the full height between the stock title bar (28) and the strip (425)
    kX1 = 716,
    kY0 = 36,              // 0 dB
    kY1 = 418,             // the floor
    kPanelY0 = 30,
    kPanelY1 = 424,
    kFloorDb = -48,        // the bottom edge: typical levels (-36 .. 0 dB) use three quarters of the height
    kSilent = -96 * 64,    // dB x 64 of a zero level
    kMaxCols = 200,        // points per line, at most (3.3 px apart)
    kMinCols = 16,
    kScales = 7,
    kDefaultScale = 2,     // right half = 4 s
    kDefaultBudget = 5400  // bytes of display list for the view (8172 - 400 margin - title bar 600 - strip / labels 1720)
};
constexpr float kBlockMs = 128000.0f / 44100.0f; // 2.9025 ms
static const uint8_t kHalfSpanS[kScales] = { 2, 3, 4, 6, 10, 20, 60 };

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
    uint16_t avg;  // weighted average of the merged block peaks
    uint16_t mx;   // maximum of the merged block peaks
    uint32_t span; // blocks covered
};

// History of one lane. Index 0 = newest. at(i).span never decreases with i; the sum of the spans is the number of blocks pushed
// (minus the 0..7 of the unfinished finest bucket). Levels: the finest kLinCap buckets are a ring; the coarser ones (spans 16 ... ) keep at
// most kPerLevel buckets per level, a fifth makes the two oldest of the level merge into one of the next level.
class Cascade
{
  public:
    Cascade() { reset(); }
    void reset()
    {
        rn_ = rh_ = cn_ = 0;
        pn_ = 0;
        ps_ = 0;
        pm_ = 0;
    }
    int count() const { return rn_ + cn_; }
    const Bucket& at(int i) const { return i < rn_ ? r_[(rh_ + i) % kRing] : c_[i - rn_]; }
    // one audio block peak (0 .. 65535)
    void push(uint16_t v)
    {
        ps_ += v;
        if (v > pm_) pm_ = v;
        if (++pn_ < kGroup) return;
        Bucket b;
        b.avg = (uint16_t)((ps_ + kGroup / 2) / kGroup);
        b.mx = pm_;
        b.span = kGroup;
        pn_ = 0;
        ps_ = 0;
        pm_ = 0;
        rh_ = (uint16_t)((rh_ + kRing - 1) % kRing);
        r_[rh_] = b;
        rn_++;
        if (rn_ > kLinCap) // the two oldest finest buckets become one bucket of level 1
        {
            const Bucket older = at(rn_ - 1), newer = at(rn_ - 2);
            rn_ = (uint16_t)(rn_ - 2);
            addCoarse(merged(newer, older));
        }
    }

  private:
    enum { kRing = kLinCap + 2 };
    static int levelOf(uint32_t span)
    {
        int l = 0;
        uint32_t s = span / kGroup;
        while (s > 1 && l < kLevels - 1)
        {
            s >>= 1;
            l++;
        }
        return l;
    }
    static Bucket merged(const Bucket& a, const Bucket& b) // a newer, b older
    {
        Bucket m;
        const uint64_t sa = a.span, sb = b.span;
        m.avg = (uint16_t)(((uint64_t)a.avg * sa + (uint64_t)b.avg * sb + (sa + sb) / 2) / (sa + sb));
        m.mx = a.mx > b.mx ? a.mx : b.mx;
        m.span = (uint32_t)(sa + sb);
        return m;
    }
    void addCoarse(const Bucket& m)
    {
        for (int i = cn_; i > 0; i--) c_[i] = c_[i - 1];
        c_[0] = m;
        cn_++;
        int i = 0;
        for (int l = 1; l < kLevels; l++)
        {
            const int s = i;
            while (i < cn_ && levelOf(c_[i].span) == l) i++;
            const int cnt = i - s;
            if (cnt > kPerLevel)
            {
                c_[s + cnt - 2] = merged(c_[s + cnt - 2], c_[s + cnt - 1]);
                for (int k = s + cnt - 1; k + 1 < cn_; k++) c_[k] = c_[k + 1];
                cn_--;
                i = s + cnt - 2; // the merged bucket is the newest of the next level
            }
        }
    }
    Bucket r_[kRing];
    Bucket c_[kCoarseMax + 2];
    uint16_t rn_, rh_, cn_;
    uint8_t pn_;
    uint32_t ps_;
    uint16_t pm_;
};

// ---- time axis ------------------------------------------------------------------------------------------------------------------
// Share of the straight line in age(u) = H * (c * u + (1 - c) * u^6) so that age(0.5) is the chosen span of the right half.
inline float linShare(int scale)
{
    const float s = (float)kHalfSpanS[scale] * 1000.0f / (float)kHorizonMs;
    float c = (s - 1.0f / 64.0f) / (0.5f - 1.0f / 64.0f);
    return c < 0 ? 0 : (c > 1 ? 1 : c);
}
// u = 0 (now, right edge) .. 1 (left edge) -> age in ms
inline float ageMsOfU(float u, int scale)
{
    const float c = linShare(scale), u2 = u * u;
    return (float)kHorizonMs * (c * u + (1.0f - c) * u2 * u2 * u2);
}
inline float uOfAge(float ageMs, int scale)
{
    float lo = 0.0f, hi = 1.0f;
    for (int i = 0; i < 24; i++)
    {
        const float mid = 0.5f * (lo + hi);
        if (ageMsOfU(mid, scale) < ageMs) lo = mid;
        else hi = mid;
    }
    return 0.5f * (lo + hi);
}
// x of a point of the given age (ms, 0 = newest = right edge). Monotone; the px per ms shrink with the age (compression); ages beyond the
// horizon sit at the left edge.
inline float xOfAge(float ageMs, int scale = kDefaultScale)
{
    if (ageMs <= 0.0f) return (float)kX1;
    if (ageMs >= (float)kHorizonMs) return (float)kX0;
    return (float)kX1 - (float)(kX1 - kX0) * uOfAge(ageMs, scale);
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
    uint16_t newest(int i) const { return lane_[i].count() ? lane_[i].at(0).avg : (uint16_t)0; }
    // the jog wheel: one step per call towards a longer (+) / shorter (-) almost linear part
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

// Start block (age) of every bucket, newest first; st[n] = total. Returns n. All lanes advance together, so one lane gives the geometry.
inline int geometry(const Cascade& c, uint32_t* st)
{
    uint32_t t = 0;
    const int n = c.count();
    for (int i = 0; i < n; i++)
    {
        st[i] = t;
        t += c.at(i).span;
    }
    st[n] = t;
    return n;
}

// Does a lane have anything above the floor (silent lanes are not drawn)
inline bool audible(const Cascade& c)
{
    for (int i = 0; i < c.count(); i++)
        if (dbx64(c.at(i).mx) > kFloorDb * 64) return true;
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
// the oldest column to the newest at the right edge; returns the number of points (columns older than the history are left out).
inline int resample(const Cascade& cs, const uint32_t* st, int n, int cols, int which, int scale, int16_t* o)
{
    int16_t dbv[kMaxBuckets];
    for (int k = 0; k < n; k++) dbv[k] = (int16_t)dbx64(which ? cs.at(k).avg : cs.at(k).mx);
    const float bpms = 1.0f / kBlockMs;
    const float du = 1.0f / (float)(cols - 1);
    int cnt = 0, k = 0;
    float dbs[kMaxCols];
    int16_t xs[kMaxCols];
    for (int j = cols - 1; j >= 0; j--) // newest first
    {
        const float u = (float)(cols - 1 - j) * du; // j = cols - 1 -> u = 0 (now)
        const float uL = u - 0.5f * du < 0 ? 0.0f : u - 0.5f * du, uR = u + 0.5f * du > 1 ? 1.0f : u + 0.5f * du;
        const float aL = ageMsOfU(uL, scale) * bpms, aR = ageMsOfU(uR, scale) * bpms, ac = ageMsOfU(u, scale) * bpms;
        if (aL >= (float)st[n]) break; // older than the history
        while (k + 1 < n && (float)st[k + 1] <= aL) k++;
        int k1 = k;
        while (k1 + 1 < n && (float)st[k1 + 1] < aR) k1++;
        float db;
        if (k1 > k) // several buckets in this column: the average (weighted by the overlap) or the maximum of them
        {
            float sum = 0, wsum = 0;
            uint32_t mx = 0;
            for (int q = k; q <= k1; q++)
            {
                const float lo = (float)st[q] > aL ? (float)st[q] : aL, hi = (float)st[q + 1] < aR ? (float)st[q + 1] : aR;
                const float w = hi > lo ? hi - lo : 0.0f;
                const uint32_t v = which ? cs.at(q).avg : cs.at(q).mx;
                sum += w * (float)v;
                wsum += w;
                if (v > mx) mx = v;
            }
            db = (float)dbx64(which ? (uint32_t)(wsum > 0 ? sum / wsum + 0.5f : 0.0f) : mx);
        }
        else // inside one bucket: linear between the centres of it and its neighbour (smooth contour, no steps)
        {
            const float c0 = 0.5f * ((float)st[k] + (float)st[k + 1]);
            const int nb = ac < c0 ? k - 1 : k + 1;
            db = (float)dbv[k];
            if (nb >= 0 && nb < n)
            {
                const float c1 = 0.5f * ((float)st[nb] + (float)st[nb + 1]);
                float t = (ac - c0) / (c1 - c0);
                t = t < 0 ? 0 : (t > 1 ? 1 : t);
                db += ((float)dbv[nb] - db) * t;
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

static const int kMarkMs[5] = { 1000, 5000, 15000, 60000, 120000 };
static const char* const kMarkText[5] = { "1s", "5s", "15s", "60s", "2m" };
static const int kDbLines[5] = { 0, -12, -24, -36, -48 };

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
    int16_t seg[10 * 4];
    int n = 0;
    for (int i = 1; i < 5; i++)
    {
        const int16_t y = (int16_t)yOf((float)kDbLines[i]);
        const int16_t s[4] = { kX0, y, kX1, y };
        for (int k = 0; k < 4; k++) seg[n * 4 + k] = s[k];
        n++;
    }
    for (int i = 0; i < 5; i++)
    {
        const int16_t x = (int16_t)(xOfAge((float)kMarkMs[i], sc) + 0.5f);
        const int16_t s[4] = { x, kY0, x, kY1 };
        for (int k = 0; k < 4; k++) seg[n * 4 + k] = s[k];
        n++;
    }
    c.color(seqpal::kGrid);
    c.lines(seg, n);
    const int16_t edge[8] = { kX0, kY0, kX1, kY0, kX1, kY0, kX1, kY1 }; // 0 dB line and the "now" edge
    c.color(seqpal::kGridHi);
    c.lines(edge, 2);

    // scale labels, inside the plot at the bottom (no footer band)
    c.color(seqpal::dim(seqpal::kText));
    for (int i = 0; i < 5; i++)
    {
        snprintf(buf, sizeof buf, "%d", kDbLines[i]);
        c.text(kX0 - 6, i == 0 ? kY0 + 7 : (i == 4 ? kY1 - 7 : yOf((float)kDbLines[i])), 1, 2 | 4, buf);
    }
    for (int i = 0; i < 5; i++)
        c.text((int)(xOfAge((float)kMarkMs[i], sc) + 0.5f), kY1 - 22, 1, 1 | 2, kMarkText[i]);
    c.text(kX1 - 4, kY1 - 22, 1, 4 | 2, "now");

    // the lines: resampled at M columns; maxima dim behind (pass 0), averages on top (pass 1); the master neutral and without a second line
    uint32_t st[kMaxBuckets + 1];
    const int nb = geometry(f.lane(0), st);
    int lines = 0, lanes = 0;
    bool aud[kLanes];
    for (int ln = 0; ln < kLanes; ln++)
    {
        aud[ln] = nb >= 2 && audible(f.lane(ln));
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
            const int cnt = resample(f.lane(ln), st, nb, cols, pass, sc, o);
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
    snprintf(buf, sizeof buf, "Jog: time scale, right half = %d s", (int)kHalfSpanS[sc]);
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
