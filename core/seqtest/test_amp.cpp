// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Host tests of plan 014 B: the amplitude view (dB table, bucket cascade, age -> x mapping, per-track lanes and colours, layout and
// display-list budget).
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
    CHECK(heightOf(0) == 1.0f && heightOf(-60) == 0.0f && heightOf(-200) == 0.0f && heightOf(10) == 1.0f);
    CHECK(yOf(0) == kY0 && yOf(-60) == kY1 && yOf(-6) > yOf(0) && yOf(-48) > yOf(-24));
    // RMS meter value to the level scale
    CHECK(levelOfRms(-1.0f) == 0 && levelOfRms(0.0f) == 0 && levelOfRms(1.0f) == 32768 && levelOfRms(0.5f) == 16384);
    CHECK(levelOfRms(5.0f) == 65535);
}

static void test_cascade()
{
    Cascade c;
    CHECK(c.count() == 0);
    // total span = number of pushes; spans never decrease towards the old end; at most two buckets per span below the top
    const uint32_t kTop = 1u << (kLevels - 1);
    uint32_t pushes = 0;
    int maxCount = 0;
    bool ok = true;
    for (int n = 1; n <= 200000; n++)
    {
        c.push((uint16_t)(n % 1000));
        pushes++;
        if (c.count() > maxCount) maxCount = c.count();
        if (n < 400 || n % 997 == 0)
        {
            uint32_t sum = 0;
            for (int i = 0; i < c.count(); i++)
            {
                sum += c.at(i).span;
                if (i > 0 && c.at(i).span < c.at(i - 1).span) ok = false;
                int same = 0;
                for (int k = 0; k < c.count(); k++) same += c.at(k).span == c.at(i).span;
                if (c.at(i).span < kTop && same > 2) ok = false;
                if (c.at(i).span < kTop && (c.at(i).span & (c.at(i).span - 1))) ok = false; // power of two
            }
            if (sum != pushes) ok = false;
            if (c.at(0).span != 1) ok = false;
        }
    }
    CHECK(ok);
    CHECK(maxCount <= kMaxBuckets); // constant size however long it runs
    printf("  cascade: %d buckets after %u pushes (limit %d), %u bytes per lane\n", c.count(), pushes, (int)kMaxBuckets,
           (unsigned)sizeof(Cascade));

    // fresh: the first buckets are 1, 1 (then 1,1,2 / 1,2,2 ... as the counter carries)
    Cascade d;
    d.push(10);
    d.push(20);
    CHECK(d.count() == 2 && d.at(0).span == 1 && d.at(1).span == 1 && d.at(0).avg == 20 && d.at(1).avg == 10);
    d.push(30); // three of span 1: the two oldest merge
    CHECK(d.count() == 2 && d.at(0).span == 1 && d.at(1).span == 2 && d.at(1).avg == 15 && d.at(1).mx == 20);
    d.push(40);
    CHECK(d.count() == 3 && d.at(0).span == 1 && d.at(1).span == 1 && d.at(2).span == 2);
    // the first 2^k - 1 pushes after a power of two show the spans 1,1,2,2,4,4... in the steady state: 1+1+2+2+4+4 = 14 pushes
    Cascade e;
    for (int i = 0; i < 14; i++) e.push(1);
    bool pairs = true;
    uint32_t total = 0;
    for (int i = 0; i < e.count(); i++) total += e.at(i).span;
    CHECK(total == 14);
    for (int i = 1; i < e.count(); i++) pairs = pairs && e.at(i).span >= e.at(i - 1).span;
    CHECK(pairs);

    // constant input: every bucket averages to it
    Cascade k;
    for (int i = 0; i < 5000; i++) k.push(12345);
    bool same = true;
    for (int i = 0; i < k.count(); i++) same = same && k.at(i).avg == 12345 && k.at(i).mx == 12345;
    CHECK(same);

    // the maximum of a peak survives every merge, in the bucket that covers it; the average weights by span
    Cascade p;
    for (int i = 0; i < 3000; i++) p.push((uint16_t)(i == 1000 ? 30000 : 10));
    uint16_t mx = 0;
    for (int i = 0; i < p.count(); i++) mx = p.at(i).mx > mx ? p.at(i).mx : mx;
    CHECK(mx == 30000);
    uint64_t sum = 0;
    for (int i = 0; i < p.count(); i++) sum += (uint64_t)p.at(i).avg * p.at(i).span;
    const double truth = (2999.0 * 10 + 30000) / 3000.0, got = (double)sum / 3000.0;
    CHECK(fabs(got - truth) < truth * 0.01 + 8); // rounding of the merges only
    // old data ends in the oldest buckets: the bucket that holds the peak is old, i.e. wide
    bool wide = false;
    for (int i = 0; i < p.count(); i++) if (p.at(i).mx == 30000 && p.at(i).span >= 512) wide = true;
    CHECK(wide);
    // recent data is kept at full resolution
    CHECK(p.at(0).span == 1 && p.at(1).span <= 2);
}

static void test_age_to_x()
{
    CHECK(xOfAge(0) == (float)kX1 && xOfAge(-5) == (float)kX1);
    CHECK(xOfAge((float)kHorizonMs) == (float)kX0 && xOfAge(1e9f) == (float)kX0);
    float prev = xOfAge(0), prevStep = 1e9f;
    bool mono = true, compressed = true;
    for (int a = 40; a <= kHorizonMs; a += 40)
    {
        const float x = xOfAge((float)a);
        if (x > prev) mono = false;
        prev = x;
    }
    CHECK(mono);
    // compression: the same age step covers fewer pixels the older it is
    for (float a = 0; a < 100000; a = a * 1.5f + 100)
    {
        const float step = xOfAge(a) - xOfAge(a + 1000);
        if (step > prevStep + 0.001f) compressed = false;
        prevStep = step;
    }
    CHECK(compressed);
    // the time marks sit in order inside the plot, the last one on the left edge
    float last = (float)kX1 + 1;
    bool order = true;
    for (int i = 0; i < 5; i++)
    {
        const float x = xOfAge((float)kMarkMs[i]);
        if (!(x < last)) order = false;
        last = x;
    }
    CHECK(order && xOfAge((float)kMarkMs[4]) == (float)kX0);
    // the first second takes a good part of the width (recent history at high resolution), the last minute a small one
    CHECK(xOfAge(0) - xOfAge(1000) > 0.12f * (kX1 - kX0));
    CHECK(xOfAge(60000) - xOfAge(120000) < 0.2f * (kX1 - kX0));

    // geometry of a full cascade: x strictly increasing from the oldest point to the newest, newest at the right edge, inside the plot
    Cascade c;
    for (int i = 0; i < 100000; i++) c.push(100);
    int16_t xs[kMaxBuckets];
    const int n = geometry(c, xs);
    CHECK(n >= 12 && n <= kMaxBuckets);
    bool inc = xs[0] == kX1;
    for (int i = 1; i < n; i++) inc = inc && xs[i] <= xs[i - 1];
    CHECK(inc && xs[n - 1] == kX0);
    for (int i = 0; i < n; i++) CHECK(xs[i] >= kX0 && xs[i] <= kX1);
}

static void test_follower_and_colours()
{
    Follower f;
    CHECK(!f.due(1000));
    CHECK(!f.due(1039));
    CHECK(f.due(1040));
    uint16_t lv[kLanes];
    for (int i = 0; i < kLanes; i++) lv[i] = (uint16_t)(100 * (i + 1));
    f.push(1040, lv);
    CHECK(!f.due(1050) && f.due(1080));
    for (int i = 0; i < kLanes; i++) CHECK(f.newest(i) == 100 * (i + 1));
    CHECK(f.due(5000)); // far behind: no burst
    f.push(5000, lv);
    CHECK(!f.due(5001));
    // lane k only ever sees its own values (per-track mapping)
    Follower g;
    g.due(0);
    for (int n = 0; n < 50; n++)
    {
        for (int i = 0; i < kLanes; i++) lv[i] = (uint16_t)(i == 3 ? 20000 : 0);
        g.push(40 * (n + 1), lv);
    }
    for (int i = 0; i < kLanes; i++) CHECK(audible(g.lane(i)) == (i == 3));

    // colours: tracks 1-4 are the key colours, 5-8 their lighter variants, all eight distinct; 9-16 dimmer variants
    CHECK(seqpal::trackColor(0) == seqpal::kNote && seqpal::trackColor(1) == seqpal::kInstr);
    CHECK(seqpal::trackColor(2) == seqpal::kFx1 && seqpal::trackColor(3) == seqpal::kFx2);
    for (int a = 0; a < 16; a++)
        for (int b = a + 1; b < 16; b++) CHECK(seqpal::trackColor(a) != seqpal::trackColor(b));
    for (int t = 0; t < 4; t++) CHECK(seqpal::trackColor(t + 4) != seqpal::trackColor(t));
}

static Follower busyFollower(int cols)
{
    Follower f;
    f.due(0);
    uint16_t lv[kLanes];
    for (int n = 0; n < cols; n++)
    {
        for (int i = 0; i < kLanes; i++) lv[i] = (uint16_t)(1 + ((n * 977 + i * 4001) % 32768));
        f.push(40 * (n + 1), lv);
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
    Follower busy = busyFollower(30000); // all eight tracks and the master loud for twenty minutes
    Ctx c;
    ampview::draw(c, busy, "Track levels, dB", "Amp view: close");
    printf("  amp view, 8 loud tracks + master: %d bytes, %d strips (max %d points), %d texts\n", c.bytes, c.strips, c.maxStripPts,
           c.texts);
    CHECK(c.bytes <= 4096);
    CHECK(c.strips == 8 * 2 + 1); // per track a peak line and an average line, the master one line
    CHECK(c.maxStripPts <= kMaxBuckets);
    CHECK(c.minX >= 0 && c.maxX <= 800);
    CHECK(c.minY >= kPanelY0 && c.maxY <= kPanelY1);
    CHECK(c.pminY >= 36 && c.pmaxY <= kY1); // grid and header boxes
    CHECK(stripsInPlot(c));                 // every data line inside the plot rectangle
    CHECK(c.textMinX >= 0 && c.textMaxX <= 800 && c.textMinY >= kPanelY0 && c.textMaxY <= kPanelY1);
    // every line runs from the left part to the right edge, x never goes backwards, per-track colours as palette
    bool xok = true;
    for (size_t s = 0; s < c.stripX.size(); s++)
    {
        for (size_t i = 1; i < c.stripX[s].size(); i++) xok = xok && c.stripX[s][i] >= c.stripX[s][i - 1];
        xok = xok && c.stripX[s].back() == kX1;
    }
    CHECK(xok);
    CHECK(c.stripColor[0] == seqpal::faint(seqpal::trackColor(0))); // pass 0: the dim peak lines in track order
    CHECK(c.stripColor[7] == seqpal::faint(seqpal::trackColor(7)));
    CHECK(c.stripColor[8] == seqpal::trackColor(0) && c.stripColor[15] == seqpal::trackColor(7));
    CHECK(c.stripColor[16] == seqpal::kTitle);
    CHECK(c.stripWidth[16] <= c.stripWidth[8]); // the master thinner

    // short history (just opened): few points, still within bounds
    Follower young = busyFollower(5);
    Ctx y;
    ampview::draw(y, young, "t", "h");
    CHECK(y.bytes <= 4096 && stripsInPlot(y) && y.maxStripPts <= 5);

    // silence: no lines at all, labels only; much smaller list
    Follower quiet;
    quiet.due(0);
    uint16_t z[kLanes] = { 0 };
    for (int n = 0; n < 500; n++) quiet.push(40 * (n + 1), z);
    Ctx q;
    ampview::draw(q, quiet, "t", "h");
    CHECK(q.strips == 0 && q.bytes < 1800);

    // only track 3 sounds: one pair of lines, the other lanes dropped; loud = near the top edge, quiet = the bottom
    Follower one;
    one.due(0);
    for (int n = 0; n < 500; n++)
    {
        uint16_t v[kLanes] = { 0 };
        v[2] = n < 250 ? 32768 : 2048; // 0 dB then -24 dB
        one.push(40 * (n + 1), v);
    }
    Ctx o;
    ampview::draw(o, one, "t", "h");
    CHECK(o.strips == 2 && o.stripColor[1] == seqpal::trackColor(2));
    CHECK(o.stripY[1].back() == yOf(-24) && o.stripY[1].front() == yOf(0));
    CHECK(o.stripY[1].back() > o.stripY[1].front()); // newest (right) is lower than the old loud part
}

// `make preview`: dump the draw ops of three scenes for amp_preview.py (SEQ_AMP_PREVIEW=<dir>)
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
    ampview::draw(c, fl, "Track levels, dB", "Amp view: close");
    fclose(c.f);
}

static void dumpPreviews(const char* dir)
{
    // 8 tracks, different rhythms (track t: a hit every (t + 2) * 250 ms with a decay), 130 s: quiet for 40 s, normal until 90 s, a loud drop after
    Follower busy;
    busy.due(0);
    for (int n = 0; n < 3250; n++)
    {
        const double sec = n * 0.04;
        const double gain = sec < 40 ? 0.06 : (sec < 90 ? 0.35 : 0.95);
        uint16_t lv[kLanes];
        double mix = 0;
        for (int t = 0; t < kTracks; t++)
        {
            const double period = (t + 2) * 0.25;
            const double ph = fmod(sec, period) / period;
            const double env = exp(-ph * (3.0 + t)) * (0.55 + 0.04 * t);
            double v = gain * env * 32768.0 * (sec < 20 && t > 3 ? 0.0 : 1.0); // tracks 5-8 come in at 20 s
            lv[t] = (uint16_t)(v < 0 ? 0 : (v > 32768 ? 32768 : v));
            mix += v;
        }
        lv[kTracks] = (uint16_t)(mix * 0.5 > 32768 ? 32768 : mix * 0.5);
        busy.push(40 * (n + 1), lv);
    }
    busy.info.playing = 1; busy.info.pattern = 12; busy.info.pos = 33; busy.info.len = 64; busy.info.bpm10 = 1275;
    dumpScene(dir, "amp1_eight_tracks", busy);

    Follower quiet;
    quiet.due(0);
    uint16_t z[kLanes] = { 0 };
    for (int n = 0; n < 500; n++) quiet.push(40 * (n + 1), z);
    quiet.info.pattern = 1; quiet.info.bpm10 = 1200;
    dumpScene(dir, "amp2_silence", quiet);

    Follower one;
    one.due(0);
    for (int n = 0; n < 1500; n++)
    {
        uint16_t v[kLanes] = { 0 };
        const double sec = n * 0.04;
        v[2] = (uint16_t)(32768 * pow(10.0, -(fmod(sec, 2.0)) * 0.9) * (sec < 30 ? 0.5 : 1.0));
        v[kTracks] = v[2];
        one.push(40 * (n + 1), v);
    }
    one.info.playing = 1; one.info.pattern = 3; one.info.pos = 5; one.info.len = 16; one.info.bpm10 = 900;
    dumpScene(dir, "amp3_one_track", one);
}

int main()
{
    if (const char* dir = getenv("SEQ_AMP_PREVIEW"))
    {
        dumpPreviews(dir);
        return 0;
    }
    printf("amp view (plan 014 B)\n");
    test_db_table();
    test_cascade();
    test_age_to_x();
    test_follower_and_colours();
    test_layout_and_budget();
    printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
