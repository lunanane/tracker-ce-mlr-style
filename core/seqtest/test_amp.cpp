// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Host tests of the amplitude view (plan 014 B, plan 015 V, v0.4.1 time axis): dB table, block peak cascade (three tiers of absolutely
// aligned buckets), the time axis (right 75 % linear in time, the left 25 % compressing with a continuous slope, cut at the y axis, jog
// scales), smooth scrolling, per-track lanes and colours, the resampled contour lines, layout over the full height and the display-list budget.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "seqAmpViewLogic.h"
#include "seqPalette.h"

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                \
    do                                                          \
    {                                                           \
        g_checks++;                                             \
        if (!(c))                                               \
        {                                                       \
            g_fail++;                                           \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
        }                                                       \
    } while (0)

// display-list recording context: bytes like the FT81x list (command 4, vertex 4, text 12 + padded string)
struct Ctx
{
    int bytes = 16; // the control's own blend function calls
    int minX = 9999, maxX = -9999, minY = 9999, maxY = -9999;
    int pminY = 9999, pmaxY = -9999;
    int textMinX = 9999, textMaxX = -9999, textMinY = 9999, textMaxY = -9999;
    int strips = 0, maxStripPts = 0, texts = 0;
    std::vector<std::vector<int>> stripX, stripY;
    std::vector<uint32_t> stripColor;
    std::vector<int> stripWidth;
    uint32_t col = 0;
    int w = 0;
    void vtx(int x, int y, bool line)
    {
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
        if (line)
        {
            if (y < pminY) pminY = y;
            if (y > pmaxY) pmaxY = y;
        }
    }
    void color(uint32_t rgb) { bytes += 4; col = rgb; }
    void width(int v) { bytes += 4; w = v; }
    void rect(int x0, int y0, int x1, int y1) { bytes += 16; vtx(x0, y0, false); vtx(x1, y1, false); }
    void lines(const int16_t* s, int n)
    {
        bytes += 8 + 8 * n;
        for (int i = 0; i < n; i++) { vtx(s[i * 4], s[i * 4 + 1], true); vtx(s[i * 4 + 2], s[i * 4 + 3], true); }
    }
    void strip(const int16_t* xy, int n)
    {
        bytes += 8 + 4 * n;
        strips++;
        if (n > maxStripPts) maxStripPts = n;
        std::vector<int> xs, ys;
        for (int i = 0; i < n; i++) { vtx(xy[i * 2], xy[i * 2 + 1], true); xs.push_back(xy[i * 2]); ys.push_back(xy[i * 2 + 1]); }
        stripX.push_back(xs);
        stripY.push_back(ys);
        stripColor.push_back(col);
        stripWidth.push_back(w);
    }
    void textPhase() { bytes += 8; }
    void text(int x, int y, int font, int opt, const char* s)
    {
        (void)font; (void)opt;
        bytes += 12 + ((int)strlen(s) + 4) / 4 * 4;
        texts++;
        if (x < textMinX) textMinX = x;
        if (x > textMaxX) textMaxX = x;
        if (y < textMinY) textMinY = y;
        if (y > textMaxY) textMaxY = y;
    }
    void finish() {}
};

using namespace ampview;

static void test_db_table()
{
    double worst = 0;
    for (uint32_t p = 1; p <= 32768; p += (p < 4096 ? 1 : 37))
    {
        const double exact = 20.0 * log10((double)p / 32768.0);
        const double err = fabs(dbx64(p) / 64.0 - exact);
        if (err > worst) worst = err;
    }
    printf("  dB table, worst error %.3f dB\n", worst);
    CHECK(worst < 0.08);
    CHECK(dbx64(32768) == 0);
    CHECK(dbx64(0) == kSilent);
    CHECK(abs(dbx64(16384) - (-386)) <= 2);
    CHECK(dbx64(100000) > 0 && dbx64(100000) < 6 * 64 + 8);
    CHECK(heightOf(0) == 1.0f && heightOf(kFloorDb) == 0.0f && heightOf(-200) == 0.0f && heightOf(10) == 1.0f);
    CHECK(yOf(0) == kY0 && yOf(kFloorDb) == kY1 && yOf(-6) > yOf(0) && yOf(-48) > yOf(-24));
    // v0.3.7: the floor is -48 dB, so typical levels (-36 .. 0 dB) use three quarters of the full height
    CHECK(kFloorDb == -48);
    CHECK((double)(yOf(-36) - yOf(0)) / (kY1 - kY0) < 0.80 && (double)(yOf(-36) - yOf(0)) / (kY1 - kY0) > 0.70);
    // RMS meter value to the level scale
    CHECK(levelOfRms(-1.0f) == 0 && levelOfRms(0.0f) == 0 && levelOfRms(1.0f) == 32768 && levelOfRms(0.5f) == 16384);
    CHECK(levelOfRms(5.0f) == 65535);
}

static void test_cascade()
{
    Cascade c;
    CHECK(c.blocks() == 0);
    Bucket b;
    CHECK(!c.get(0, 0, b)); // nothing yet
    // spans of the tiers, ring sizes (powers of two), memory
    CHECK(Cascade::span(0) == (uint32_t)kGroup && Cascade::span(1) == 4u * kGroup && Cascade::span(2) == 16u * kGroup);
    for (int tt = 0; tt < kTiers; tt++) CHECK((Cascade::cap(tt) & (Cascade::cap(tt) - 1)) == 0);
    printf("  cascade: %u bytes per lane, %u bytes for 9 lanes; rings reach %.1f s / %.1f s / %.1f s\n", (unsigned)sizeof(Cascade),
           (unsigned)(sizeof(Cascade) * kLanes), Cascade::cap(0) * Cascade::span(0) * kBlockMs / 1000.0, Cascade::cap(1) * Cascade::span(1) * kBlockMs / 1000.0,
           Cascade::cap(2) * Cascade::span(2) * kBlockMs / 1000.0);
    CHECK(sizeof(Cascade) <= 4200); // v0.4.0: 1960 B for 120 s of exponential history; now 60 s at full resolution
    // the history covers the largest total age (60 s) at the display resolution, and the linear part of every scale with the finest tier or the next
    CHECK((double)Cascade::cap(kTiers - 1) * Cascade::span(kTiers - 1) * kBlockMs >= (double)kMaxTotalMs);
    CHECK((double)Cascade::cap(0) * Cascade::span(0) * kBlockMs >= 11000.0 && (double)Cascade::cap(1) * Cascade::span(1) * kBlockMs >= 23000.0);

    // fresh: the unfinished bucket is readable (partial), a finished one is final
    for (int i = 0; i < kGroup - 1; i++) c.push(100);
    CHECK(c.get(0, 0, b) && b.span == (uint32_t)kGroup - 1 && b.avg == 100 && b.mx == 100);
    c.push(100);
    CHECK(c.get(0, 0, b) && b.span == (uint32_t)kGroup && b.avg == 100 && !c.get(0, 1, b));
    for (int i = 0; i < kGroup; i++) c.push((uint16_t)(i == 3 ? 900 : 50));
    CHECK(c.get(0, 1, b) && b.mx == 900 && b.avg == (900 + 7 * 50 + 4) / 8 && c.get(0, 0, b) && b.avg == 100);
    // the coarser tiers average / maximise the same block peaks
    CHECK(c.get(1, 0, b) && b.span == 16 && b.mx == 900 && b.avg == (8 * 100 + 900 + 7 * 50 + 8) / 16);

    // every held bucket of every tier equals the brute force average / maximum of ITS blocks (absolute alignment, nothing merged)
    Cascade d;
    std::vector<uint16_t> src;
    unsigned rnd = 99;
    const uint32_t N = 100000;
    for (uint32_t n = 0; n < N; n++)
    {
        rnd = rnd * 1664525u + 1013904223u;
        const uint16_t v = (uint16_t)((rnd >> 16) % 3000 + (n % 5000 < 200 ? 20000 : 0));
        src.push_back(v);
        d.push(v);
    }
    bool exact = true;
    int held[kTiers] = { 0, 0, 0 };
    for (int tt = 0; tt < kTiers; tt++)
    {
        const uint32_t S = Cascade::span(tt), F = N / S;
        for (uint32_t j = d.oldestHeld(tt); j <= F; j++)
        {
            Bucket g;
            const bool ok = d.get(tt, j, g);
            if (j == F && N == F * S) { exact = exact && !ok; continue; } // no partial bucket
            if (!ok) { exact = false; continue; }
            held[tt]++;
            uint64_t sum = 0;
            uint16_t mx = 0;
            const uint32_t cnt = j == F ? N - F * S : S;
            for (uint32_t k = 0; k < cnt; k++) { sum += src[j * S + k]; if (src[j * S + k] > mx) mx = src[j * S + k]; }
            exact = exact && g.span == cnt && g.mx == mx && g.avg == (uint16_t)((sum + cnt / 2) / cnt);
        }
        Bucket gone;
        exact = exact && !d.get(tt, d.oldestHeld(tt) - 1, gone) && !d.get(tt, F + 5, gone); // older than the ring / in the future
    }
    CHECK(exact);
    CHECK(held[0] >= (int)Cascade::cap(0) && held[1] >= (int)Cascade::cap(1) && held[2] >= (int)Cascade::cap(2)); // the rings are full
    // reach(): the age that the tier covers completely
    for (int tt = 0; tt < kTiers; tt++) CHECK(d.reach(tt) >= (Cascade::cap(tt) - 1) * Cascade::span(tt) && d.reach(tt) <= (Cascade::cap(tt) + 1) * Cascade::span(tt));
    CHECK(d.reach(0) < d.reach(1) && d.reach(1) < d.reach(2));
    // indexAt: the block of age 0 is the newest one
    CHECK(d.indexAt(0, 0) == (N - 1) / Cascade::span(0) && d.indexAt(2, 0) == (N - 1) / Cascade::span(2));

    // constant input: every bucket of every tier is that value
    Cascade k;
    for (int i = 0; i < 50000; i++) k.push(12345);
    bool same = true;
    for (int tt = 0; tt < kTiers; tt++)
        for (uint32_t j = k.oldestHeld(tt); j <= 50000 / Cascade::span(tt); j++)
        {
            Bucket g;
            if (k.get(tt, j, g)) same = same && g.avg == 12345 && g.mx == 12345;
        }
    CHECK(same);

    // a peak survives in the bucket that covers it, as long as the tier holds it: 87 s old = only the coarsest tier
    Cascade p;
    for (int i = 0; i < 40000; i++) p.push((uint16_t)(i == 10000 ? 30000 : 10));
    Bucket g;
    CHECK(p.get(2, 10000 / Cascade::span(2), g) && g.mx == 30000 && g.avg > 10 && g.avg < 400);
    CHECK(!p.get(0, 10000 / Cascade::span(0), g) && !p.get(1, 10000 / Cascade::span(1), g));
    // old data beyond the largest ring is gone (no growth)
    Cascade q;
    for (uint32_t i = 0; i < 300000; i++) q.push(7);
    CHECK(!q.get(2, 100, g) && q.get(2, 300000 / Cascade::span(2) - 1, g));
}

static void test_age_to_x()
{
    const float W = (float)(kX1 - kX0);
    CHECK(kLinPx == 495 && kDefaultScale == 2 && kLinMs[kDefaultScale] == 8000 && kTotalMs[kDefaultScale] == 30000); // owner: ~8 s linear, ~30 s max
    for (int sc = 0; sc < kScales; sc++)
    {
        const float lin = (float)kLinMs[sc], tot = (float)kTotalMs[sc], a = lin / (float)kLinPx; // ms per px of the linear part
        CHECK(xOfAge(0, sc) == (float)kX1 && xOfAge(-5, sc) == (float)kX1);
        // cut at the y axis: the total age is exactly the left axis, anything older sits there too (and is never drawn)
        CHECK(xOfAge(tot, sc) == (float)kX0 && xOfAge(1e9f, sc) == (float)kX0);
        CHECK(fabsf(ageMsOfU(1.0f, sc) - tot) < 1.0f && ageMsOfU(0.0f, sc) == 0.0f);
        // monotone: older = further left (strictly)
        float prev = xOfAge(0, sc);
        bool mono = true;
        for (int ag = 40; ag < (int)tot; ag += 40)
        {
            const float x = xOfAge((float)ag, sc);
            if (!(x < prev)) mono = false;
            prev = x;
        }
        CHECK(mono);
        // the right 75 % are LINEAR in time: x = X1 - age / a within a hundredth of a pixel, every column moves at the same speed
        bool linear = true;
        for (float ag = 0; ag <= lin; ag += lin / 97.0f) linear = linear && fabsf(xOfAge(ag, sc) - ((float)kX1 - ag / a)) < 0.02f;
        CHECK(linear);
        CHECK(fabsf(xOfAge(lin, sc) - (float)(kX1 - kLinPx)) < 0.02f); // the linear part ends at 75 % of the distance from the right edge
        bool linU = true;
        for (float u = 0; u <= 0.75f; u += 0.01f) linU = linU && fabsf(ageMsOfU(u, sc) - a * u * W) < 0.05f;
        CHECK(linU);
        // continuous slope at the 75 % point (no kink), then the compression grows smoothly (ms per px never shrinks)
        const float p0 = (float)kLinPx / W, eps = 0.02f / W;
        const float sl = (ageMsOfU(p0, sc) - ageMsOfU(p0 - eps, sc)) / 0.02f, sr = (ageMsOfU(p0 + eps, sc) - ageMsOfU(p0, sc)) / 0.02f;
        CHECK(fabsf(sl - a) < 0.02f * a && fabsf(sr - a) < 0.03f * a && fabsf(sr / sl - 1.0f) < 0.03f);
        float prevSlope = 0;
        bool grow = true;
        for (int px = 0; px + 1 <= (int)W; px++)
        {
            const float s = ageMsOfU((float)(px + 1) / W, sc) - ageMsOfU((float)px / W, sc);
            if (s < prevSlope - 0.01f) grow = false;
            prevSlope = s;
        }
        CHECK(grow);
        // xOfAge inverts ageMsOfU (labels and lines agree)
        bool inv = true;
        for (float u = 0.01f; u < 0.995f; u += 0.03f) inv = inv && fabsf(xOfAge(ageMsOfU(u, sc), sc) - ((float)kX1 - u * W)) < 0.06f;
        CHECK(inv);
        // time marks: in order inside the plot, apart from each other and from the right edge, the last one on the left axis
        int mi[7], mx[7];
        const int nm = timeMarks(sc, mi, mx);
        bool order = nm >= 2 && mx[nm - 1] == kX0 && (uint32_t)kMarkMs[mi[nm - 1]] == kTotalMs[sc];
        int last = kX1;
        for (int i = 0; i < nm; i++)
        {
            if (mx[i] > last - kMarkGap + 1 || mx[i] < kX0) order = false;
            last = mx[i];
        }
        CHECK(order);
    }
    // the jog scales are ordered: a longer linear part / total = slower scrolling
    for (int sc = 0; sc + 1 < kScales; sc++)
    {
        CHECK(xOfAge(5000, sc) < xOfAge(5000, sc + 1) && kLinMs[sc] < kLinMs[sc + 1] && kTotalMs[sc] < kTotalMs[sc + 1]);
        CHECK(kTotalMs[sc] >= kLinMs[sc] + (kLinMs[sc] / 3)); // the tail needs room (b >= 0)
    }
    CHECK(kTotalMs[kScales - 1] == kMaxTotalMs);
    // default: 8 s take 75 %, 30 s the whole width; the last 22 s are compressed into the left quarter
    CHECK(fabsf(xOfAge(8000) - (kX1 - 0.75f * W)) < 0.05f && xOfAge(30000) == (float)kX0 && xOfAge(15000) > kX0 + 0.06f * W);
    CHECK(xOfAge(0) - xOfAge(1000) > 0.08f * W && xOfAge(0) - xOfAge(1000) < 0.11f * W); // 1 s = 61.9 px, the same for every 1 s of the linear part
    CHECK(fabsf((xOfAge(0) - xOfAge(1000)) - (xOfAge(6000) - xOfAge(7000))) < 0.05f);

    // geometry of a full cascade: the resampled line has as many points as columns, newest at the right edge, oldest EXACTLY at the y axis
    Cascade c;
    for (int i = 0; i < 100000; i++) c.push(1000); // -30 dB, 290 s
    int16_t o[(2 * kMaxCols + 2) * 2];
    for (int sc = 0; sc < kScales; sc++)
        for (int cols : { 16, 60, 120, 200 })
        {
            const int cnt = resample(c, cols, 1, sc, o);
            CHECK(cnt == cols && o[0] == kX0 && o[(cnt - 1) * 2] == kX1);
            bool xs = true;
            for (int i = 1; i < cnt; i++) xs = xs && o[i * 2] > o[(i - 1) * 2];
            CHECK(xs);
            for (int i = 0; i < cnt; i++) CHECK(o[i * 2 + 1] >= kY0 && o[i * 2 + 1] <= kY1 && o[i * 2] >= kX0 && o[i * 2] <= kX1);
        }
    // a history shorter than the total age: the line starts where the history starts (at its exact x), not earlier
    Cascade yng;
    for (int i = 0; i < (int)(15000 / kBlockMs); i++) yng.push(1000);
    const int cy = resample(yng, 200, 1, kDefaultScale, o);
    const float xh = xOfAge(15000.0f);
    CHECK(cy > 100 && cy < 200 && o[0] >= xh - 1 && o[0] <= xh + 4.0f);
    // the maxima line: the same
    CHECK(resample(yng, 200, 0, kDefaultScale, o) == cy);
}

// smooth scrolling (v0.4.1): a pulse moves left at the constant speed of the linear part, block by block, no jumps
static void test_scroll()
{
    for (int which = 0; which < 2; which++)
    {
        Cascade c;
        for (int i = 0; i < 6000; i++) c.push(0);
        for (int i = 0; i < 300; i++) c.push(20000); // a 0.87 s pulse
        const float a = (float)kLinMs[kDefaultScale] / (float)kLinPx; // ms per px
        int16_t o[(2 * kMaxCols + 2) * 2];
        float firstEdge = -1, prevEdge = -1, maxStep = 0, lastEdge = 0;
        const int steps = 600; // 1.7 s
        for (int s = 0; s < steps; s++)
        {
            const int cnt = resample(c, 200, which, kDefaultScale, o);
            // the leading (newer) edge of the pulse: where the line falls through the middle of its height, scanning from the right edge
            const int ymid = (yOf(-3.7f) + kY1) / 2;
            float edge = -1;
            for (int i = cnt - 1; i > 0; i--)
                if (o[i * 2 + 1] > ymid && o[(i - 1) * 2 + 1] <= ymid && o[i * 2 + 1] < kY1 + 1)
                {
                    const float f = (float)(ymid - o[(i - 1) * 2 + 1]) / (float)(o[i * 2 + 1] - o[(i - 1) * 2 + 1]);
                    edge = o[(i - 1) * 2] + f * (o[i * 2] - o[(i - 1) * 2]);
                    break;
                }
            if (edge >= 0)
            {
                if (firstEdge < 0) firstEdge = edge;
                else if (edge - prevEdge > maxStep) maxStep = edge - prevEdge;
                if (prevEdge >= 0 && edge > prevEdge + 0.8f) maxStep = 99;
                prevEdge = edge;
                lastEdge = edge;
            }
            c.push(0);
        }
        // expected: 600 blocks x 2.9025 ms / (16.16 ms per px) = 107.8 px to the left
        const float moved = firstEdge - lastEdge, expect = steps * kBlockMs / a;
        printf("  scroll (%s): the pulse edge moves %.1f px in %d blocks (expected %.1f), largest step to the right %.2f px\n", which ? "averages" : "maxima", moved,
               steps, expect, maxStep);
        CHECK(firstEdge > 0 && fabsf(moved - expect) < 0.04f * expect + 2.0f);
        CHECK(maxStep < 0.8f); // never jumps back
    }
}

static void test_follower_and_colours()
{
    Follower f;
    uint16_t lv[kLanes];
    for (int i = 0; i < kLanes; i++) lv[i] = (uint16_t)(100 * (i + 1));
    for (int n = 0; n < kGroup; n++) f.push(lv);
    CHECK(f.blocks() == (uint32_t)kGroup);
    for (int i = 0; i < kLanes; i++) CHECK(f.newest(i) == 100 * (i + 1));
    // lane k only ever sees its own values (per-track mapping)
    Follower g;
    for (int n = 0; n < 400; n++)
    {
        for (int i = 0; i < kLanes; i++) lv[i] = (uint16_t)(i == 3 ? 20000 : 0);
        g.push(lv);
    }
    for (int i = 0; i < kLanes; i++) CHECK(audible(g.lane(i)) == (i == 3));
    // the jog wheel steps the time scale and stops at both ends
    Follower j;
    CHECK(j.scale == kDefaultScale);
    j.jog(1);
    CHECK(j.scale == kDefaultScale + 1);
    for (int i = 0; i < 20; i++) j.jog(5);
    CHECK(j.scale == kScales - 1);
    j.jog(1);
    CHECK(j.scale == kScales - 1);
    for (int i = 0; i < 20; i++) j.jog(-1);
    CHECK(j.scale == 0);
    j.jog(0);
    CHECK(j.scale == 0);

    // colours: tracks 1-4 are the key colours, 5-8 their lighter variants, all eight distinct; 9-16 dimmer variants
    CHECK(seqpal::trackColor(0) == seqpal::kNote && seqpal::trackColor(1) == seqpal::kInstr);
    CHECK(seqpal::trackColor(2) == seqpal::kFx1 && seqpal::trackColor(3) == seqpal::kFx2);
    for (int a = 0; a < 16; a++)
        for (int b = a + 1; b < 16; b++) CHECK(seqpal::trackColor(a) != seqpal::trackColor(b));
    for (int t = 0; t < 4; t++) CHECK(seqpal::trackColor(t + 4) != seqpal::trackColor(t));
}

// block peaks of eight tracks (track t: a hit every (t + 2) * 250 ms with a decay) and the master; seconds 0 .. secs
static Follower busyFollower(double secs, double gain)
{
    Follower f;
    const int blocks = (int)(secs * 1000.0 / kBlockMs);
    uint16_t lv[kLanes];
    unsigned rnd = 12345;
    for (int n = 0; n < blocks; n++)
    {
        const double sec = n * kBlockMs / 1000.0;
        double mix = 0;
        for (int t = 0; t < kTracks; t++)
        {
            rnd = rnd * 1664525u + 1013904223u;
            const double period = (t + 2) * 0.25;
            const double ph = fmod(sec, period) / period;
            const double env = exp(-ph * (3.0 + t)) * (0.55 + 0.04 * t);
            const double v = gain * env * 32768.0 * (0.85 + 0.15 * ((rnd >> 16) & 255) / 255.0);
            lv[t] = (uint16_t)(v > 32768 ? 32768 : v);
            mix += v;
        }
        lv[kTracks] = (uint16_t)(mix * 0.5 > 32768 ? 32768 : mix * 0.5);
        f.push(lv);
    }
    f.info.playing = 1;
    f.info.pattern = 12;
    f.info.pos = 33;
    f.info.len = 64;
    f.info.bpm10 = 1275;
    return f;
}

static bool stripsInPlot(const Ctx& c)
{
    for (size_t s = 0; s < c.stripY.size(); s++)
        for (size_t i = 0; i < c.stripY[s].size(); i++)
            if (c.stripY[s][i] < kY0 || c.stripY[s][i] > kY1 || c.stripX[s][i] < kX0 || c.stripX[s][i] > kX1) return false;
    return true;
}

static void test_layout_and_budget()
{
    Follower busy = busyFollower(130, 0.45); // all eight tracks and the master, more than two minutes
    Ctx c;
    ampview::draw(c, busy, "Track levels, dB", "Amp view: close");
    printf("  amp view, 8 loud tracks + master: %d bytes (budget %d), %d strips (max %d points), %d texts\n", c.bytes, (int)kDefaultBudget,
           c.strips, c.maxStripPts, c.texts);
    CHECK(c.bytes <= kDefaultBudget);
    CHECK(c.bytes <= 8172 - 400 - 600 - 1720); // the frame model: title bar + strip / labels + the view with a margin of 400 B
    // per track a peak line and an average line, the master one line; a line is BROKEN where the level falls below the floor (v0.3.7), so a
    // lane may be several strips, but never fewer than one per line
    CHECK(c.strips >= 8 * 2 + 1);
    CHECK(c.maxStripPts >= 40 && c.maxStripPts <= kMaxCols); // v0.3.6 had at most 17 points per line
    CHECK(c.minX >= 0 && c.maxX <= 800);
    CHECK(c.minY >= kPanelY0 && c.maxY <= kPanelY1);
    CHECK(c.pminY >= kY0 && c.pmaxY <= kY1); // grid lines and data lines inside the plot, which uses the full height
    CHECK(stripsInPlot(c));
    CHECK(c.textMinX >= 0 && c.textMaxX <= 800 && c.textMinY >= kPanelY0 && c.textMaxY <= kPanelY1);
    CHECK(kY1 - kY0 >= 380 && kPanelY0 <= 30 && kPanelY1 >= 424); // between the stock title bar and the strip: no header / footer bands
    bool xok = true;
    for (size_t s = 0; s < c.stripX.size(); s++)
    {
        for (size_t i = 1; i < c.stripX[s].size(); i++) xok = xok && c.stripX[s][i] > c.stripX[s][i - 1];
        xok = xok && c.stripX[s].size() >= 2;
    }
    CHECK(xok);
    // every colour of the palette is present; the dim peak lines come before the averages, the master last
    int firstFaint[8], firstAvg[8], lastMaster = -1, firstMaster = 99999, widthAvg = 0, widthMaster = 0;
    for (int tr = 0; tr < 8; tr++)
    {
        firstFaint[tr] = firstAvg[tr] = -1;
        for (size_t s = 0; s < c.stripColor.size(); s++)
        {
            if (firstFaint[tr] < 0 && c.stripColor[s] == seqpal::faint(seqpal::trackColor(tr))) firstFaint[tr] = (int)s;
            if (firstAvg[tr] < 0 && c.stripColor[s] == seqpal::trackColor(tr)) { firstAvg[tr] = (int)s; widthAvg = c.stripWidth[s]; }
        }
        CHECK(firstFaint[tr] >= 0 && firstAvg[tr] > firstFaint[tr]);
        if (tr) CHECK(firstFaint[tr] > firstFaint[tr - 1] && firstAvg[tr] > firstAvg[tr - 1]);
    }
    for (size_t s = 0; s < c.stripColor.size(); s++)
        if (c.stripColor[s] == seqpal::kTitle) { lastMaster = (int)s; if ((int)s < firstMaster) firstMaster = (int)s; widthMaster = c.stripWidth[s]; }
    CHECK(lastMaster == (int)c.stripColor.size() - 1 && firstMaster > firstAvg[7]);
    CHECK(widthMaster <= widthAvg); // the master thinner
    // the newest values are at the right edge for at least one strip of every colour group
    int maxX = 0;
    for (size_t s = 0; s < c.stripX.size(); s++) if (c.stripX[s].back() > maxX) maxX = c.stripX[s].back();
    CHECK(maxX == kX1);
    // the lines use the height: typical loud material reaches the upper part of the plot, quiet parts the lower
    int top = 9999, bot = -9999;
    for (size_t s = 0; s < c.stripY.size(); s++)
        for (int y : c.stripY[s]) { if (y < top) top = y; if (y > bot) bot = y; }
    CHECK(top < kY0 + (kY1 - kY0) / 3 && bot > kY0 + (kY1 - kY0) / 2);

    // the display list adapts to the budget it is given (other controls under the view): a smaller budget = fewer points, never above it
    Follower tight = busy;
    tight.budget = 3600;
    Ctx t;
    ampview::draw(t, tight, "t", "h");
    CHECK(t.bytes <= 3600 && t.maxStripPts >= kMinCols && t.maxStripPts < c.maxStripPts);
    // a bigger one = more points up to the cap
    Follower roomy = busy;
    roomy.budget = 8000;
    Ctx r;
    ampview::draw(r, roomy, "t", "h");
    CHECK(r.maxStripPts > c.maxStripPts && r.maxStripPts <= kMaxCols);

    // few tracks: many more points per line
    Follower f3;
    {
        uint16_t lv[kLanes];
        for (int n = 0; n < 20000; n++)
        {
            for (int i = 0; i < kLanes; i++) lv[i] = (uint16_t)(i < 2 ? 1000 + (n * 37 + i * 4001) % 20000 : 0);
            f3.push(lv);
        }
    }
    Ctx c3;
    ampview::draw(c3, f3, "t", "h");
    printf("  amp view, 2 tracks: %d bytes, %d strips, %d points per line\n", c3.bytes, c3.strips, c3.maxStripPts);
    CHECK(c3.strips == 4 && c3.maxStripPts >= 150 && c3.bytes <= kDefaultBudget);

    // short history (just opened, 1.2 s): the lines only span the history, still inside the plot
    Follower young = busyFollower(1.2, 0.45);
    Ctx y;
    ampview::draw(y, young, "t", "h");
    CHECK(y.bytes <= kDefaultBudget && stripsInPlot(y));
    bool shortLine = true;
    for (size_t s = 0; s < y.stripX.size(); s++) shortLine = shortLine && y.stripX[s].front() > kX0 + (kX1 - kX0) / 2;
    CHECK(shortLine);

    // silence: no lines at all, labels only; much smaller list
    Follower quiet;
    uint16_t z[kLanes] = { 0 };
    for (int n = 0; n < 4000; n++) quiet.push(z);
    Ctx q;
    ampview::draw(q, quiet, "t", "h");
    CHECK(q.strips == 0 && q.bytes < 1700);

    // only track 3 sounds: one pair of lines, the other lanes dropped; loud = near the top edge, quiet = the bottom
    Follower one;
    for (int n = 0; n < 6000; n++)
    {
        uint16_t v[kLanes] = { 0 };
        v[2] = n < 3000 ? 32768 : 2048; // 0 dB then -24 dB
        one.push(v);
    }
    Ctx o;
    ampview::draw(o, one, "t", "h");
    CHECK(o.strips == 2 && o.stripColor[1] == seqpal::trackColor(2));
    CHECK(abs(o.stripY[1].back() - yOf(-24)) <= 1 && abs(o.stripY[1].front() - yOf(0)) <= 1);
    CHECK(o.stripY[1].back() > o.stripY[1].front()); // newest (right) is lower than the old loud part

    // a lane below the floor is not drawn (silent lanes dropped); a lane just above is
    Follower low;
    for (int n = 0; n < 2000; n++)
    {
        uint16_t v[kLanes] = { 0 };
        v[0] = 60;  // -54.7 dB
        v[1] = 300; // -40.8 dB
        low.push(v);
    }
    Ctx l;
    ampview::draw(l, low, "t", "h");
    CHECK(l.strips == 2);
}

// `make preview`: dump the draw ops of the scenes for amp_preview.py (SEQ_AMP_PREVIEW=<dir>)
struct DumpCtx
{
    FILE* f;
    void color(uint32_t rgb) { fprintf(f, "C %06X\n", (unsigned)rgb); }
    void width(int w) { fprintf(f, "W %d\n", w); }
    void rect(int x0, int y0, int x1, int y1) { fprintf(f, "R %d %d %d %d\n", x0, y0, x1, y1); }
    void lines(const int16_t* s, int n)
    {
        fprintf(f, "L");
        for (int i = 0; i < n * 4; i++) fprintf(f, " %d", s[i]);
        fprintf(f, "\n");
    }
    void strip(const int16_t* xy, int n)
    {
        fprintf(f, "S");
        for (int i = 0; i < n * 2; i++) fprintf(f, " %d", xy[i]);
        fprintf(f, "\n");
    }
    void textPhase() {}
    void text(int x, int y, int font, int opt, const char* s) { fprintf(f, "T %d %d %d %d %s\n", x, y, font, opt, s); }
    void finish() {}
};

static void dumpScene(const char* dir, const char* name, const Follower& fl)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ops", dir, name);
    DumpCtx c;
    c.f = fopen(path, "w");
    if (!c.f) return;
    ampview::draw(c, fl, "Track levels, dB", "Amp: Close");
    fclose(c.f);
}

static void dumpPreviews(const char* dir)
{
    // 8 tracks, different rhythms (track t: a hit every (t + 2) * 250 ms with a decay), 130 s: quiet for 40 s, normal until 90 s, a loud
    // drop after; tracks 5-8 come in at 20 s. One peak per audio block, like the device delivers it.
    Follower busy;
    {
        uint16_t lv[kLanes];
        unsigned rnd = 777;
        const int blocks = (int)(130000.0 / kBlockMs);
        for (int n = 0; n < blocks; n++)
        {
            const double sec = n * kBlockMs / 1000.0;
            const double gain = sec < 40 ? 0.06 : (sec < 90 ? 0.35 : 0.95);
            double mix = 0;
            for (int t = 0; t < kTracks; t++)
            {
                rnd = rnd * 1664525u + 1013904223u;
                const double period = (t + 2) * 0.25;
                const double ph = fmod(sec, period) / period;
                const double env = exp(-ph * (3.0 + t)) * (0.55 + 0.04 * t);
                double v = gain * env * 32768.0 * (0.8 + 0.2 * ((rnd >> 16) & 255) / 255.0) * (sec < 20 && t > 3 ? 0.0 : 1.0);
                lv[t] = (uint16_t)(v < 0 ? 0 : (v > 32768 ? 32768 : v));
                mix += v;
            }
            lv[kTracks] = (uint16_t)(mix * 0.5 > 32768 ? 32768 : mix * 0.5);
            busy.push(lv);
        }
    }
    busy.info.playing = 1; busy.info.pattern = 12; busy.info.pos = 33; busy.info.len = 64; busy.info.bpm10 = 1275;
    dumpScene(dir, "amp1_eight_tracks", busy);

    Follower quiet;
    uint16_t z[kLanes] = { 0 };
    for (int n = 0; n < 4000; n++) quiet.push(z);
    quiet.info.pattern = 1; quiet.info.bpm10 = 1200;
    dumpScene(dir, "amp2_silence", quiet);

    Follower one;
    for (int n = 0; n < (int)(60000 / kBlockMs); n++)
    {
        uint16_t v[kLanes] = { 0 };
        const double sec = n * kBlockMs / 1000.0;
        v[2] = (uint16_t)(32768 * pow(10.0, -(fmod(sec, 2.0)) * 0.9) * (sec < 30 ? 0.5 : 1.0));
        v[kTracks] = v[2];
        one.push(v);
    }
    one.info.playing = 1; one.info.pattern = 3; one.info.pos = 5; one.info.len = 16; one.info.bpm10 = 900;
    dumpScene(dir, "amp3_one_track", one);

    // four tracks (a typical beat), the other jog scales of the same material
    Follower four;
    {
        uint16_t lv[kLanes];
        const int blocks = (int)(125000.0 / kBlockMs);
        for (int n = 0; n < blocks; n++)
        {
            const double sec = n * kBlockMs / 1000.0;
            double mix = 0;
            for (int t = 0; t < 4; t++)
            {
                const double period = t == 0 ? 0.47 : (t == 1 ? 0.94 : (t == 2 ? 0.235 : 3.76));
                const double ph = fmod(sec + t * 0.11, period) / period;
                const double v = 32768.0 * (t == 3 ? 0.18 : 0.5) * exp(-ph * (t == 2 ? 9.0 : 4.0)) * (sec < 12 && t == 3 ? 0 : 1);
                lv[t] = (uint16_t)v;
                mix += v;
            }
            for (int t = 4; t < kTracks; t++) lv[t] = 0;
            lv[kTracks] = (uint16_t)(mix * 0.6);
            four.push(lv);
        }
    }
    four.info = busy.info;
    dumpScene(dir, "amp4_four_tracks", four);
    Follower fourLong = four;
    fourLong.scale = 3;
    dumpScene(dir, "amp5_four_tracks_scale16s_60s", fourLong);
    Follower fourShort = four;
    fourShort.scale = 0;
    dumpScene(dir, "amp6_four_tracks_scale2s_8s", fourShort);
    // the first seconds after the Master module is entered: 6 s and 20 s of history (the screen fills from the right)
    Follower early;
    {
        uint16_t lv[kLanes];
        for (int n = 0; n < (int)(6000.0 / kBlockMs); n++)
        {
            const double sec = n * kBlockMs / 1000.0;
            double mix = 0;
            for (int tr = 0; tr < 4; tr++)
            {
                const double period = tr == 0 ? 0.47 : (tr == 1 ? 0.94 : (tr == 2 ? 0.235 : 3.76));
                const double ph = fmod(sec + tr * 0.11, period) / period;
                const double v = 32768.0 * (tr == 3 ? 0.18 : 0.5) * exp(-ph * (tr == 2 ? 9.0 : 4.0));
                lv[tr] = (uint16_t)v;
                mix += v;
            }
            for (int tr = 4; tr < kTracks; tr++) lv[tr] = 0;
            lv[kTracks] = (uint16_t)(mix * 0.6);
            early.push(lv);
        }
    }
    early.info = busy.info;
    dumpScene(dir, "amp7_four_tracks_after_6s", early);
    {
        uint16_t lv[kLanes];
        for (int n = (int)(6000.0 / kBlockMs); n < (int)(20000.0 / kBlockMs); n++)
        {
            const double sec = n * kBlockMs / 1000.0;
            double mix = 0;
            for (int tr = 0; tr < 4; tr++)
            {
                const double period = tr == 0 ? 0.47 : (tr == 1 ? 0.94 : (tr == 2 ? 0.235 : 3.76));
                const double ph = fmod(sec + tr * 0.11, period) / period;
                const double v = 32768.0 * (tr == 3 ? 0.18 : 0.5) * exp(-ph * (tr == 2 ? 9.0 : 4.0));
                lv[tr] = (uint16_t)v;
                mix += v;
            }
            for (int tr = 4; tr < kTracks; tr++) lv[tr] = 0;
            lv[kTracks] = (uint16_t)(mix * 0.6);
            early.push(lv);
        }
    }
    dumpScene(dir, "amp8_four_tracks_after_20s", early);
}

int main()
{
    if (const char* dir = getenv("SEQ_AMP_PREVIEW"))
    {
        dumpPreviews(dir);
        return 0;
    }
    printf("amp view (plan 014 B, 015 V, v0.4.1 time axis)\n");
    test_db_table();
    test_cascade();
    test_age_to_x();
    test_scroll();
    test_follower_and_colours();
    test_layout_and_budget();
    printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
