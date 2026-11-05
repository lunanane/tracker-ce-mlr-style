// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Master amplitude view (plan 013 M): a scrolling dB history of the master output. The audio side only keeps the peak of the final
// left / right blocks (AudioEffectLimiter::takePeak, one pass over 128 samples, only while the view is open); this header holds the
// main-loop ring of columns, the dB conversion by table (no log), the decimation, the layout and the drawing over a tiny context (the
// same code runs on the FT81x display list in interface/masterParams/ampView.cpp and in the host tests). No Polyend includes.
//
// Look (decision): L above the centre line, R below it (mirrored), each a thin envelope line of the column peaks, x = time scrolling
// right to left (~5 s), y = dB on a logarithmic axis: 0 dB at the top / bottom edge, -60 dB at the centre line. Faint grid lines at 0 /
// -6 / -12 / -24 / -48 dB, L in the Note key colour, R in the FX1 key colour, plain dark ground, no fills.
#ifndef SEQ_AMP_VIEW_LOGIC_H_
#define SEQ_AMP_VIEW_LOGIC_H_

#include <stdint.h>
#include <stdio.h>

#include "seqPalette.h"

namespace ampview
{
enum
{
    kCols = 125,   // columns kept: 125 * 40 ms = 5 s
    kColMs = 40,   // one column per 40 ms (25 per second)
    kMaxPts = 96,  // points per line after decimation (bucket maximum)
    kX0 = 56,
    kX1 = 716,
    kY0 = 60,
    kY1 = 404,
    kCy = (kY0 + kY1) / 2,
    kPanelY0 = 30,
    kPanelY1 = 424,
    kFloorDb = -60,        // the centre line
    kSilent = -96 * 64     // dB x 64 of a zero peak
};

// 1/64 dB of a block peak (0 .. 32768 = full scale; larger values clamp to +6 dB). Table of 6.02 dB * log2(1 + i / 64) * 64, linear
// interpolation: error < 0.06 dB. Called per column, never per sample.
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

class Follower
{
  public:
    Follower() { reset(); }
    void reset()
    {
        for (int i = 0; i < kCols; i++) l_[i] = r_[i] = 0;
        head_ = 0;
        lastMs_ = 0;
        started_ = false;
    }
    // true when the next column is due (25 per second); the caller then pushes the peaks it took from the audio side
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
    void push(uint32_t nowMs, uint16_t peakL, uint16_t peakR)
    {
        l_[head_] = peakL;
        r_[head_] = peakR;
        head_ = (uint8_t)((head_ + 1) % kCols);
        lastMs_ += kColMs;
        if (nowMs - lastMs_ >= 4 * kColMs) lastMs_ = nowMs; // far behind (view was not serviced): do not burst
    }
    // age 0 = newest
    uint16_t left(int age) const { return l_[(head_ + kCols - 1 - age) % kCols]; }
    uint16_t right(int age) const { return r_[(head_ + kCols - 1 - age) % kCols]; }

  private:
    uint16_t l_[kCols], r_[kCols];
    uint8_t head_;
    bool started_;
    uint32_t lastMs_;
};

// The columns of one channel oldest first, reduced to at most maxPts values by the maximum of each bucket (a peak must not vanish).
inline int decimate(const Follower& f, bool right, uint16_t* out, int maxPts)
{
    const int n = maxPts < kCols ? maxPts : kCols;
    for (int i = 0; i < n; i++)
    {
        const int a = i * kCols / n, b = (i + 1) * kCols / n; // oldest-first indices a .. b - 1
        uint16_t mx = 0;
        for (int k = a; k < (b > a ? b : a + 1); k++)
        {
            const int age = kCols - 1 - k;
            const uint16_t v = right ? f.right(age) : f.left(age);
            if (v > mx) mx = v;
        }
        out[i] = mx;
    }
    return n;
}

inline int yUp(float db) { return (int)(kCy - heightOf(db) * (kCy - kY0) + 0.5f); }
inline int yDown(float db) { return (int)(kCy + heightOf(db) * (kY1 - kCy) + 0.5f); }

// C as in pitchtrace::draw
template <class C> void draw(C& c, const Follower& f, const char* title, const char* hint)
{
    static const int kLines[5] = { 0, -6, -12, -24, -48 };
    char buf[16];
    c.color(seqpal::kBg);
    c.rect(0, kPanelY0, 800, kPanelY1);
    c.textPhase();

    c.width(16);
    int16_t seg[11 * 4];
    int n = 0;
    for (int i = 1; i < 5; i++) // faint grid (0 dB is the edge line below)
    {
        const int yu = yUp((float)kLines[i]), yd = yDown((float)kLines[i]);
        const int16_t s[8] = { kX0, (int16_t)yu, kX1, (int16_t)yu, kX0, (int16_t)yd, kX1, (int16_t)yd };
        for (int k = 0; k < 8; k++) seg[n * 4 + k] = s[k];
        n += 2;
    }
    c.color(seqpal::kGrid);
    c.lines(seg, n);
    n = 0;
    const int16_t strong[12] = { kX0, kY0, kX1, kY0, kX0, kCy, kX1, kCy, kX0, kY1, kX1, kY1 }; // 0 dB edges and the centre line
    for (int k = 0; k < 12; k++) seg[k] = strong[k];
    c.color(seqpal::kGridHi);
    c.lines(seg, 3);

    for (int i = 0; i < 5; i++) // dB labels at the left, both halves
    {
        snprintf(buf, sizeof buf, "%d", kLines[i]);
        c.color(seqpal::dim(seqpal::kText));
        c.text(kX0 - 6, i == 0 ? kY0 + 7 : yUp((float)kLines[i]), 1, 2 | 4, buf);
        c.text(kX0 - 6, i == 0 ? kY1 - 7 : yDown((float)kLines[i]), 1, 2 | 4, buf);
    }

    uint16_t pk[kMaxPts];
    int16_t xy[kMaxPts * 2];
    for (int ch = 0; ch < 2; ch++)
    {
        const int cnt = decimate(f, ch == 1, pk, kMaxPts);
        for (int i = 0; i < cnt; i++)
        {
            const float db = dbx64(pk[i]) * (1.0f / 64.0f);
            xy[i * 2] = (int16_t)(kX0 + i * (kX1 - kX0) / (cnt - 1));
            xy[i * 2 + 1] = (int16_t)(ch == 0 ? yUp(db) : yDown(db));
        }
        c.color(ch == 0 ? seqpal::kNote : seqpal::kFx1);
        c.width(20);
        c.strip(xy, cnt);
    }

    // current level at the right edge (the newest column)
    for (int ch = 0; ch < 2; ch++)
    {
        const float db = dbx64(ch == 0 ? f.left(0) : f.right(0)) * (1.0f / 64.0f);
        if (db <= -90.0f)
            snprintf(buf, sizeof buf, "%c  -inf", ch == 0 ? 'L' : 'R');
        else
            snprintf(buf, sizeof buf, "%c %d dB", ch == 0 ? 'L' : 'R', (int)(db < 0 ? db - 0.5f : db + 0.5f));
        c.color(ch == 0 ? seqpal::kNote : seqpal::kFx1);
        c.text(kX1 + 8, ch == 0 ? kCy - 20 : kCy + 20, 3, 2, buf);
    }

    c.color(seqpal::kTitle);
    c.text(12, 42, 3, 2, title);
    c.color(seqpal::kText);
    c.text(788, 42, 1, 2 | 4, hint);
    c.finish();
}

} // namespace ampview

#endif
