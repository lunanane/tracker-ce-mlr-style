// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Host tests of the amplitude view (plan 014 B, plan 015 V): dB table, block peak cascade (a 192 bucket linear part and an exponential
// histogram), the time axis (almost linear for the last seconds, compression building up towards the left edge, jog scales), per-track
// lanes and colours, the resampled contour lines, layout over the full height and the display-list budget.
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
    CHECK(c.count() == 0);
    // total span = number of finished groups; spans never decrease towards the old end; coarse levels hold at most kPerLevel buckets
    uint32_t pushes = 0;
    int maxCount = 0;
    bool ok = true;
    const uint32_t kTop = (uint32_t)kGroup << (kLevels - 1);
    for (int n = 1; n <= 400000; n++)
    {
        c.push((uint16_t)(n % 1000));
        pushes++;
        if (c.count() > maxCount) maxCount = c.count();
        if (n < 3000 || n % 997 == 0)
        {
            uint64_t sum = 0;
            int fine = 0;
            for (int i = 0; i < c.count(); i++)
            {
                sum += c.at(i).span;
                if (i > 0 && c.at(i).span < c.at(i - 1).span) ok = false;
                if (c.at(i).span == (uint32_t)kGroup) fine++;
                int same = 0;
                for (int k = 0; k < c.count(); k++) same += c.at(k).span == c.at(i).span;
                if (c.at(i).span > (uint32_t)kGroup && c.at(i).span < kTop && same > kPerLevel) ok = false;
                if (c.at(i).span < kTop && (c.at(i).span & (c.at(i).span - 1))) ok = false; // power of two
            }
            if (sum != (uint64_t)(pushes - pushes % kGroup)) ok = false;
            if (fine > kLinCap) ok = false;
            if (c.count() && c.at(0).span != (uint32_t)kGroup) ok = false;
        }
    }
    CHECK(ok);
    CHECK(maxCount <= kMaxBuckets); // constant size however long it runs
    printf("  cascade: %d buckets after %u block pushes (limit %d), %u bytes per lane, %u bytes for 9 lanes\n", c.count(), pushes,
           (int)kMaxBuckets, (unsigned)sizeof(Cascade), (unsigned)(sizeof(Cascade) * kLanes));
    // the history reaches beyond the horizon (120 s) once it is full
    uint64_t total = 0;
    for (int i = 0; i < c.count(); i++) total += c.at(i).span;
    CHECK((double)total * kBlockMs > (double)kHorizonMs);

    // fresh: the first finished group is one bucket, the next one the second
    Cascade d;
    for (int i = 0; i < kGroup - 1; i++) d.push(100);
    CHECK(d.count() == 0); // not finished yet
    d.push(100);
    CHECK(d.count() == 1 && d.at(0).span == kGroup && d.at(0).avg == 100 && d.at(0).mx == 100);
    for (int i = 0; i < kGroup; i++) d.push((uint16_t)(i == 3 ? 900 : 50));
    CHECK(d.count() == 2 && d.at(0).mx == 900 && d.at(0).avg == (900 + 7 * 50 + 4) / 8 && d.at(1).avg == 100);

    // constant input: every bucket averages to it
    Cascade k;
    for (int i = 0; i < 50000; i++) k.push(12345);
    bool same = true;
    for (int i = 0; i < k.count(); i++) same = same && k.at(i).avg == 12345 && k.at(i).mx == 12345;
    CHECK(same);

    // the maximum of a peak survives every merge, in the bucket that covers it; the average weights by span
    Cascade p;
    const int N = 40000;
    for (int i = 0; i < N; i++) p.push((uint16_t)(i == 10000 ? 30000 : 10));
    uint16_t mx = 0;
    for (int i = 0; i < p.count(); i++) mx = p.at(i).mx > mx ? p.at(i).mx : mx;
    CHECK(mx == 30000);
    double sum = 0, blocks = 0;
    for (int i = 0; i < p.count(); i++) { sum += (double)p.at(i).avg * p.at(i).span; blocks += p.at(i).span; }
    const double truth = ((blocks - 1) * 10.0 + 30000.0) / blocks;
    CHECK(fabs(sum / blocks - truth) < truth * 0.02 + 8); // rounding of the merges only
    // old data ends in wide buckets: the bucket that holds the peak (about 30 s ago) is coarse
    bool wide = false;
    for (int i = 0; i < p.count(); i++) if (p.at(i).mx == 30000 && p.at(i).span >= 8 * 16) wide = true;
    CHECK(wide);
    // recent data is kept at full resolution: the newest kLinCap buckets are the finest (4.4 s)
    bool fine = true;
    for (int i = 0; i < kLinCap - 2; i++) fine = fine && p.at(i).span == (uint32_t)kGroup;
    CHECK(fine);
}

static void test_age_to_x()
{
    for (int sc = 0; sc < kScales; sc++)
    {
        CHECK(xOfAge(0, sc) == (float)kX1 && xOfAge(-5, sc) == (float)kX1);
        CHECK(xOfAge((float)kHorizonMs, sc) == (float)kX0 && xOfAge(1e9f, sc) == (float)kX0);
        float prev = xOfAge(0, sc), prevStep = 1e9f;
        bool mono = true, compressed = true;
        for (int a = 40; a <= kHorizonMs; a += 40)
        {
            const float x = xOfAge((float)a, sc);
            if (x > prev) mono = false;
            prev = x;
        }
        CHECK(mono);
        // compression: the same age step covers fewer pixels the older it is
        for (float a = 0; a < 100000; a = a * 1.5f + 100)
        {
            const float step = xOfAge(a, sc) - xOfAge(a + 1000, sc);
            if (step > prevStep + 0.01f) compressed = false;
            prevStep = step;
        }
        CHECK(compressed);
        // the time marks sit in order inside the plot, the last one on the left edge
        float last = (float)kX1 + 1;
        bool order = true;
        for (int i = 0; i < 5; i++)
        {
            const float x = xOfAge((float)kMarkMs[i], sc);
            if (!(x < last)) order = false;
            last = x;
        }
        CHECK(order && xOfAge((float)kMarkMs[4], sc) == (float)kX0);
        // the right half of the width shows the chosen span (2 ... 60 s)
        const float xm = 0.5f * (kX0 + kX1);
        CHECK(fabsf(xOfAge((float)kHalfSpanS[sc] * 1000.0f, sc) - xm) < 1.0f);
        CHECK(kHalfSpanS[sc] < kHalfSpanS[sc < kScales - 1 ? sc + 1 : sc] || sc == kScales - 1);
    }
    // default: the right half covers the last 4 s (owner: "values are compressed after only 10 %"), almost linearly: the first 10 % of
    // the width are about 0.4 s, the first quarter about a second; the compression builds up gradually, the left edge is the full history
    const int D = kDefaultScale;
    const float W = (float)(kX1 - kX0);
    CHECK(kHalfSpanS[D] >= 3 && kHalfSpanS[D] <= 5);
    CHECK(xOfAge(3000, D) > 0.5f * (kX0 + kX1) - 1.0f); // 3 s are inside the right half
    CHECK(xOfAge(0, D) - xOfAge(1000, D) > 0.20f * W);  // the last second takes more than a fifth of the width (was 16 %)
    // the speed (ms of history per 10 % of the width) grows gradually: no knee near the right edge
    float prevMs = 0;
    bool gradual = true;
    for (int q = 0; q < 10; q++)
    {
        const float a0 = ageMsOfU(0.1f * q, D), a1 = ageMsOfU(0.1f * (q + 1), D);
        const float ms = a1 - a0;
        if (q > 0 && ms < prevMs * 1.0f) gradual = false;
        prevMs = ms;
    }
    CHECK(gradual);
    CHECK(ageMsOfU(0.1f, D) < 600.0f);                       // the first 10 % of the width are still well below a second
    CHECK(ageMsOfU(0.1f, D) > 150.0f);
    CHECK(xOfAge(60000, D) - xOfAge(120000, D) < 0.2f * W);   // the last minute of the history takes a small part
    CHECK(fabsf(ageMsOfU(1.0f, D) - (float)kHorizonMs) < 1.0f);
    // the jog scales are ordered: a longer right half = a longer almost linear part
    for (int sc = 0; sc + 1 < kScales; sc++) CHECK(xOfAge(5000, sc) < xOfAge(5000, sc + 1));

    // geometry of a full cascade: start blocks increase, the total reaches beyond the horizon
    Cascade c;
    for (int i = 0; i < 100000; i++) c.push(1000); // -30 dB
    uint32_t st[kMaxBuckets + 1];
    const int n = geometry(c, st);
    bool inc = st[0] == 0;
    for (int i = 1; i <= n; i++) inc = inc && st[i] > st[i - 1];
    CHECK(inc && (double)st[n] * kBlockMs > kHorizonMs);
    // the resampled line: every column inside the plot, x increasing, newest at the right edge, as many points as columns
    int16_t o[(2 * kMaxCols + 2) * 2];
    const int cols = 120;
    const int cnt = resample(c, st, n, cols, 1, D, o);
    CHECK(cnt == cols && o[0] == kX0 && o[(cnt - 1) * 2] == kX1);
    bool xs = true;
    for (int i = 1; i < cnt; i++) xs = xs && o[i * 2] > o[(i - 1) * 2];
    CHECK(xs);
    for (int i = 0; i < cnt; i++) CHECK(o[i * 2 + 1] >= kY0 && o[i * 2 + 1] <= kY1);
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
    fourLong.scale = 5;
    dumpScene(dir, "amp5_four_tracks_scale20s", fourLong);
    Follower fourShort = four;
    fourShort.scale = 0;
    dumpScene(dir, "amp6_four_tracks_scale2s", fourShort);
}

int main()
{
    if (const char* dir = getenv("SEQ_AMP_PREVIEW"))
    {
        dumpPreviews(dir);
        return 0;
    }
    printf("amp view (plan 014 B, 015 V)\n");
    test_db_table();
    test_cascade();
    test_age_to_x();
    test_follower_and_colours();
    test_layout_and_budget();
    printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
