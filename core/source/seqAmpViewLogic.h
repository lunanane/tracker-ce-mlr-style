// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Master amplitude view (plan 013 M, redesigned in plan 014 B): one level line per track plus the master, on an EXPONENTIAL time axis.
// New data enters at the right edge; the further back in time, the more it is merged and compressed towards the left axis (the idea of
// the "data streams rushing in and being exponentially slowed down" visualiser). This header holds
//   - the dB conversion by table (no log per sample),
//   - Cascade: a fixed bucket cascade per lane (exponential histogram): the newest value enters bucket 0; per span (1, 2, 4, ...
//     blocks) at most two buckets exist, a third makes the two oldest merge into one bucket of the next span (average, the maximum is
//     kept). Constant memory (kMaxBuckets) and constant work per block; no linear history,
//   - xOfAge: x = f(log age), the layout, and the drawing over a tiny context (the same code runs on the FT81x display list in
//     interface/masterParams/ampView.cpp and in the host tests). No Polyend includes.
//
// Look (decision, plan 014 B): the tracks are OVERLAID in one dB plot (honest axis: 0 dB at the top, -60 dB at the bottom; a lane per
// track would need offsets that bend the dB scale), each track a thin line in a tracker key colour or its variant (seqpal::trackColor)
// with a dimmer line of the bucket maxima behind it (peaks), the master a thin neutral line; silent lanes are not drawn. Plain dark
// ground, faint grid, header boxes (pattern / position, BPM, title) like the reference, time marks 1 s / 5 s / 15 s / 60 s / 2 min.
#ifndef SEQ_AMP_VIEW_LOGIC_H_
#define SEQ_AMP_VIEW_LOGIC_H_

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "seqPalette.h"

namespace ampview
{
enum
{
    kTracks = 8,           // sample tracks (the only ones with audio of their own)
    kLanes = kTracks + 1,  // + the master (lane 8)
    kColMs = 40,           // one block per 40 ms (25 per second)
    kLevels = 12,          // spans 1 .. 2048 blocks (82 s); the oldest bucket absorbs everything older
    kMaxBuckets = 2 * kLevels + 1,
    kHorizonMs = 120000,   // the left edge of the time axis
    kT0Ms = 800,           // time constant of the log axis: x ~ ln(1 + age / T0)
    kX0 = 56,
    kX1 = 716,
    kY0 = 78,
    kY1 = 392,
    kPanelY0 = 30,
    kPanelY1 = 424,
    kFloorDb = -60,        // the bottom edge
    kSilent = -96 * 64     // dB x 64 of a zero level
};

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
    uint16_t avg;  // weighted average of the merged values
    uint16_t mx;   // maximum of the merged values
    uint32_t span; // blocks covered
};

// Fixed bucket cascade of one lane (exponential histogram). Index 0 = newest. Spans never decrease with the index; every span below
// the top level has one or two buckets. The sum of the spans equals the number of pushes.
class Cascade
{
  public:
    Cascade() { reset(); }
    void reset() { n_ = 0; }
    int count() const { return n_; }
    const Bucket& at(int i) const { return b_[i]; }
    void push(uint16_t v)
    {
        for (int i = n_; i > 0; i--) b_[i] = b_[i - 1]; // b_ holds kMaxBuckets + 1 entries: room for the transient one
        b_[0].avg = v;
        b_[0].mx = v;
        b_[0].span = 1;
        n_++;
        int i = 0;
        while (i + 2 < n_)
        {
            if (b_[i].span == b_[i + 1].span && b_[i + 1].span == b_[i + 2].span && b_[i].span < (uint32_t)kTopSpan)
            {
                merge(i + 1, i + 2); // the two oldest of the three
                i++;                 // the merged bucket may now be the third of the next span
            }
            else
                i++;
        }
        if (n_ > kMaxBuckets) merge(n_ - 2, n_ - 1); // top level: the oldest bucket absorbs
    }

  private:
    enum { kTopSpan = 1 << (kLevels - 1) };
    void merge(int a, int b) // b into a (b older), b removed
    {
        const uint32_t sa = b_[a].span, sb = b_[b].span;
        b_[a].avg = (uint16_t)(((uint64_t)b_[a].avg * sa + (uint64_t)b_[b].avg * sb + (sa + sb) / 2) / (sa + sb));
        if (b_[b].mx > b_[a].mx) b_[a].mx = b_[b].mx;
        b_[a].span = sa + sb;
        for (int i = b; i + 1 < n_; i++) b_[i] = b_[i + 1];
        n_--;
    }
    Bucket b_[kMaxBuckets + 1];
    uint8_t n_;
};

// x of a point of the given age (ms, 0 = newest = right edge): x = X1 - W * ln(1 + age / T0) / ln(1 + H / T0). Monotone; the px per ms
// shrink with the age (compression); ages beyond the horizon sit at the left edge.
inline float xOfAge(float ageMs)
{
    static const float kDen = logf(1.0f + (float)kHorizonMs / (float)kT0Ms);
    if (ageMs <= 0.0f) return (float)kX1;
    if (ageMs >= (float)kHorizonMs) return (float)kX0;
    return (float)kX1 - (float)(kX1 - kX0) * logf(1.0f + ageMs / (float)kT0Ms) / kDen;
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
        lastMs_ = 0;
        started_ = false;
    }
    // true when the next block is due (25 per second); the caller then pushes the levels it took from the audio side
    bool due(uint32_t nowMs)
    {
        if (!started_)
        {
            started_ = true;
            lastMs_ = nowMs;
            return false;
        }
        return nowMs - lastMs_ >= kColMs;
    }
    void push(uint32_t nowMs, const uint16_t* levels) // kLanes values
    {
        for (int i = 0; i < kLanes; i++) lane_[i].push(levels[i]);
        lastMs_ += kColMs;
        if (nowMs - lastMs_ >= 4 * kColMs) lastMs_ = nowMs; // far behind (view was not serviced): do not burst
    }
    const Cascade& lane(int i) const { return lane_[i]; }
    uint16_t newest(int i) const { return lane_[i].count() ? lane_[i].at(0).avg : (uint16_t)0; }
    Info info;

  private:
    Cascade lane_[kLanes];
    bool started_;
    uint32_t lastMs_;
};

// Geometry of the cascade (shared by all lanes, they advance together): x of every bucket centre (age of its middle, the newest
// value at the right edge), newest first. The first bucket past the horizon is kept as the last point at the left edge, the rest dropped.
inline int geometry(const Cascade& c, int16_t* xs)
{
    uint32_t start = 0;
    int cnt = 0;
    for (int i = 0; i < c.count(); i++)
    {
        const uint32_t sp = c.at(i).span;
        const float age = ((float)start + (float)(sp - 1) * 0.5f) * (float)kColMs;
        xs[cnt++] = (int16_t)(xOfAge(age) + 0.5f);
        start += sp;
        if (age >= (float)kHorizonMs) break;
    }
    return cnt;
}

// Does a lane have anything above the floor (silent lanes are not drawn)
inline bool audible(const Cascade& c)
{
    for (int i = 0; i < c.count(); i++)
        if (dbx64(c.at(i).mx) > kFloorDb * 64) return true;
    return false;
}

static const int kMarkMs[5] = { 1000, 5000, 15000, 60000, 120000 };
static const char* const kMarkText[5] = { "1s", "5s", "15s", "60s", "2m" };
static const int kDbLines[6] = { 0, -12, -24, -36, -48, -60 };

// C as in pitchtrace::draw (color, width, rect, lines, strip, textPhase, text, finish)
template <class C> void draw(C& c, const Follower& f, const char* title, const char* hint)
{
    char buf[40];
    c.color(seqpal::kBg);
    c.rect(0, kPanelY0, 800, kPanelY1);
    c.textPhase();

    // grid: horizontal dB lines, vertical time marks
    c.width(16);
    int16_t seg[11 * 4];
    int n = 0;
    for (int i = 1; i < 6; i++)
    {
        const int16_t y = (int16_t)yOf((float)kDbLines[i]);
        const int16_t s[4] = { kX0, y, kX1, y };
        for (int k = 0; k < 4; k++) seg[n * 4 + k] = s[k];
        n++;
    }
    for (int i = 0; i < 5; i++)
    {
        const int16_t x = (int16_t)(xOfAge((float)kMarkMs[i]) + 0.5f);
        const int16_t s[4] = { x, kY0, x, kY1 };
        for (int k = 0; k < 4; k++) seg[n * 4 + k] = s[k];
        n++;
    }
    c.color(seqpal::kGrid);
    c.lines(seg, n);
    const int16_t edge[8] = { kX0, kY0, kX1, kY0, kX1, kY0, kX1, kY1 }; // 0 dB line and the "now" edge
    c.color(seqpal::kGridHi);
    c.lines(edge, 2);

    // header boxes: pattern / position, BPM, title
    const int16_t boxes[12 * 4] = { kX0, 36,  200, 36,  200, 36, 200, 62, kX0, 62,  200, 62,  kX0, 36,  kX0, 62,
                                    208, 36,  352, 36,  352, 36, 352, 62, 208, 62,  352, 62,  208, 36,  208, 62,
                                    360, 36,  640, 36,  640, 36, 640, 62, 360, 62,  640, 62,  360, 36,  360, 62 };
    c.color(seqpal::kGridHi);
    c.lines(boxes, 12);

    // scale labels
    c.color(seqpal::dim(seqpal::kText));
    for (int i = 0; i < 6; i++)
    {
        snprintf(buf, sizeof buf, "%d", kDbLines[i]);
        c.text(kX0 - 6, i == 0 ? kY0 + 7 : (i == 5 ? kY1 - 7 : yOf((float)kDbLines[i])), 1, 2 | 4, buf);
    }
    for (int i = 0; i < 5; i++)
        c.text((int)(xOfAge((float)kMarkMs[i]) + 0.5f), kY1 + 14, 1, 1 | 2, kMarkText[i]);
    c.text(kX1, kY1 + 14, 1, 1 | 2, "now");

    // lines: the maxima dim behind (pass 0), the averages on top (pass 1); the master neutral and without a second line
    int16_t xs[kMaxBuckets];
    int16_t o[kMaxBuckets * 2];
    const int nb = geometry(f.lane(0), xs);
    for (int pass = 0; pass < 2; pass++)
    {
        for (int ln = 0; ln < kLanes; ln++)
        {
            const Cascade& cs = f.lane(ln);
            if (nb < 2 || !audible(cs)) continue;
            if (ln == kTracks && pass == 0) continue;
            for (int i = 0; i < nb; i++) // oldest first
            {
                const int j = nb - 1 - i;
                const float db = dbx64(pass == 0 ? cs.at(j).mx : cs.at(j).avg) * (1.0f / 64.0f);
                o[i * 2] = xs[j];
                o[i * 2 + 1] = (int16_t)yOf(db);
            }
            const uint32_t col = ln == kTracks ? seqpal::kTitle : seqpal::trackColor(ln);
            c.color(pass == 0 ? seqpal::dim(col) : col);
            c.width(pass == 0 ? 12 : (ln == kTracks ? 12 : 20));
            c.strip(o, nb);
        }
    }

    // current levels at the right edge, tracks 1..8 and M
    for (int ln = 0; ln < kLanes; ln++)
    {
        const int y = kY0 + 12 + ln * 34;
        const float db = dbx64(f.newest(ln)) * (1.0f / 64.0f);
        const char tag = ln == kTracks ? 'M' : (char)('1' + ln);
        if (db <= (float)kFloorDb)
            snprintf(buf, sizeof buf, "%c  --", tag);
        else
            snprintf(buf, sizeof buf, "%c %d", tag, (int)(db < 0 ? db - 0.5f : db + 0.5f));
        c.color(ln == kTracks ? seqpal::kTitle : seqpal::trackColor(ln));
        c.text(kX1 + 8, y, 3, 2, buf);
    }

    // header texts
    const Info& in = f.info;
    if (in.playing)
        snprintf(buf, sizeof buf, "P%02d  %02d/%02d", in.pattern, in.pos, in.len);
    else
        snprintf(buf, sizeof buf, "P%02d  stop", in.pattern);
    c.color(seqpal::kTitle);
    c.text(kX0 + 8, 49, 3, 2, buf);
    snprintf(buf, sizeof buf, "BPM %d.%d", in.bpm10 / 10, in.bpm10 % 10);
    c.text(216, 49, 3, 2, buf);
    c.text(368, 49, 3, 2, title);
    c.color(seqpal::kText);
    c.text(788, 49, 1, 2 | 4, hint);
    c.finish();
}

} // namespace ampview

#endif
