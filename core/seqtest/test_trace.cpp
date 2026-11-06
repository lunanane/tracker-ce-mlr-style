// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Host tests of plan 013: the engine's pitch ring (glide, LFO, envelope, idle), the pitch trace follower / decimation / auto range /
// layout bounds / display-list size, the master amplitude view (dB table, ring, decimation, layout, size) and the clip launcher
// budgets (planGrid, marksCost) after the restyle.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "plinky_voice.h"
#include "seqPalette.h"
#include "seqPitchTraceLogic.h"
#include "seqSessionLogic.h"

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

// ---- display-list recording context: bytes like the FT81x list (command 4, vertex 4, text 12 + padded string) -------------------
struct Ctx
{
    int bytes = 16; // the control's own blend function calls
    int minX = 9999, maxX = -9999, minY = 9999, maxY = -9999;      // all shape vertices
    int pminY = 9999, pmaxY = -9999;                                // vertices of lines / strips only
    int textMinX = 9999, textMaxX = -9999, textMinY = 9999, textMaxY = -9999;
    int strips = 0, maxStripPts = 0, texts = 0;
    std::vector<std::vector<int>> stripY; // y of each strip
    std::vector<uint32_t> stripColor;
    uint32_t col = 0;
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
    void width(int) { bytes += 4; }
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
        std::vector<int> ys;
        for (int i = 0; i < n; i++) { vtx(xy[i * 2], xy[i * 2 + 1], true); ys.push_back(xy[i * 2 + 1]); }
        stripY.push_back(ys);
        stripColor.push_back(col);
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

using namespace plinkyvoice;

static void runBlocks(Engine& e, int n)
{
    int16_t l[kBlock], r[kBlock];
    for (int i = 0; i < n; i++) e.render(l, r);
}

static void test_engine_ring()
{
    // ring layout equals the logic header's sample
    CHECK(sizeof(TraceSample) == sizeof(pitchtrace::Sample));
    CHECK(kMaxVoices == pitchtrace::kVoices);
    // v0.3.7 (plan 015 V): the engine ring only has to bridge the main loop gaps (the follower keeps its own 3.9 s window of kWin samples):
    // a power of two holding at least 400 ms, one sample every 2 blocks, and kWin samples are 3.9 s
    CHECK((kTraceLen & (kTraceLen - 1)) == 0 && kTraceLen * kTraceEvery * kBlockSec >= 0.4f);
    CHECK(pitchtrace::kWin * kTraceEvery * kBlockSec > 3.8f && pitchtrace::kWin * kTraceEvery * kBlockSec < 4.0f);
    CHECK(pitchtrace::kSampleUs == (int)(kTraceEvery * kBlockSec * 1e6f + 0.5f));

    Engine e;
    CHECK(e.traceHeadIndex() == 0);
    e.setParam(P_GLIDE, 0);
    e.setParam(P_ATTACK, 0);
    e.setParam(P_SUSTAIN, 100);
    e.setParam(P_LFO_DEPTH, 0);
    e.noteOn(60, 100);
    runBlocks(e, 120);
    // one sample per kTraceEvery blocks
    CHECK(e.traceHeadIndex() == 120 / kTraceEvery);
    int voice = -1;
    for (int v = 0; v < kMaxVoices; v++)
        if (e.traceRing(v)[(e.traceHeadIndex() - 1) & (kTraceLen - 1)].on) voice = v;
    CHECK(voice >= 0);
    const TraceSample& s = e.traceRing(voice)[(e.traceHeadIndex() - 1) & (kTraceLen - 1)];
    CHECK(s.pitch == 60 * 32);
    CHECK(s.env > 200);
    // the other voices are idle
    int sounding = 0;
    for (int v = 0; v < kMaxVoices; v++) sounding += e.traceRing(v)[(e.traceHeadIndex() - 1) & (kTraceLen - 1)].on;
    CHECK(sounding == 1);

    // glide: a new note 7 semitones up is reached on a slope, monotonic, not at once
    Engine g;
    g.setParam(P_VOICES, 1);
    g.setParam(P_GLIDE, 60);
    g.setParam(P_SUSTAIN, 100);
    g.noteOn(60, 100);
    runBlocks(g, 400);
    g.noteOn(67, 100);
    runBlocks(g, 24 * kTraceEvery);
    int prev = 0, rising = 0, between = 0;
    const uint16_t gh = g.traceHeadIndex();
    for (int k = 24; k >= 1; k--)
    {
        const TraceSample& t = g.traceRing(0)[(gh - k) & (kTraceLen - 1)];
        CHECK(t.pitch >= prev - 1 || prev == 0);
        if (prev && t.pitch > prev) rising++;
        if (t.pitch > 60 * 32 + 8 && t.pitch < 67 * 32 - 8) between++;
        prev = t.pitch;
    }
    CHECK(rising >= 3 && between >= 2);

    // LFO to pitch: the pitch wiggles around the note
    Engine l;
    l.setParam(P_VOICES, 1);
    l.setParam(P_LFO_DEST, 0);
    l.setParam(P_LFO_DEPTH, 100);
    l.setParam(P_LFO_RATE, 60);
    l.setParam(P_SUSTAIN, 100);
    l.noteOn(60, 100);
    runBlocks(l, 400 * 2);
    int mn = 99999, mx = -99999;
    const uint16_t lh = l.traceHeadIndex();
    for (int k = 1; k <= 60; k++)
    {
        const int p = l.traceRing(0)[(lh - k) & (kTraceLen - 1)].pitch;
        if (p < mn) mn = p;
        if (p > mx) mx = p;
    }
    CHECK(mx - mn > 16); // more than half a semitone of wiggle

    // envelope follows the release and the voice goes idle
    Engine r;
    r.setParam(P_VOICES, 1);
    r.setParam(P_RELEASE, 10);
    r.setParam(P_SUSTAIN, 100);
    r.noteOn(64, 100);
    runBlocks(r, 100);
    r.noteOff(64);
    runBlocks(r, 40 * kTraceEvery);
    const uint16_t rh = r.traceHeadIndex();
    CHECK(!r.traceRing(0)[(rh - 1) & (kTraceLen - 1)].on);
    // the envelope falls during the release (v0.3.7: the samples are 5.8 ms apart, the scan replaces the single sample of 40 back)
    int minRel = 255, prevRel = 256;
    bool fallsMonotone = true;
    for (int k = 40; k >= 1; k--)
    {
        const TraceSample& t = r.traceRing(0)[(rh - k) & (kTraceLen - 1)];
        if (!t.on) continue;
        if (t.env < minRel) minRel = t.env;
        if (t.env > prevRel) fallsMonotone = false;
        prevRel = t.env;
    }
    CHECK(minRel < 255 && fallsMonotone);
    // a silent engine writes nothing (the reader keeps its own clock then)
    Engine q;
    runBlocks(q, 100);
    CHECK(q.traceHeadIndex() == 0);
}

// a fake engine ring for the follower
struct FakeRing
{
    pitchtrace::Sample s[pitchtrace::kVoices][128];
    uint16_t head = 0;
    void put(int pitch, int env, int on)
    {
        for (int v = 0; v < pitchtrace::kVoices; v++)
        {
            pitchtrace::Sample& x = s[v][head & 127];
            x.pitch = (int16_t)(pitch + v * 32);
            x.env = (uint8_t)env;
            x.on = (uint8_t)on;
        }
        head++;
    }
    void sync(pitchtrace::Follower& f, uint32_t now)
    {
        const pitchtrace::Sample* r[pitchtrace::kVoices];
        for (int v = 0; v < pitchtrace::kVoices; v++) r[v] = s[v];
        f.sync(r, 128, head, now);
    }
};

static void test_follower_decimate_range()
{
    FakeRing ring;
    pitchtrace::Follower f;
    ring.sync(f, 100); // first call: only starts
    for (int i = 0; i < 10; i++) ring.put(60 * 32 + i, 128, 1);
    ring.sync(f, 140);
    CHECK(f.at(0, 0).pitch == 60 * 32 + 9);
    CHECK(f.at(1, 0).pitch == 60 * 32 + 9 + 32);
    CHECK(f.at(0, 9).pitch == 60 * 32);
    CHECK(f.at(0, 10).on == 0);
    // wrap of the 16 bit head and of the 128 ring
    ring.head = 65530;
    ring.sync(f, 150); // overrun branch (head jumped): keeps the newest half, no crash
    for (int i = 0; i < pitchtrace::kMinRun + 10; i++) ring.put(70 * 32, 255, 1); // a run longer than the minimum shown run
    ring.sync(f, 200);
    CHECK(f.at(0, 0).pitch == 70 * 32);
    CHECK(f.at(0, pitchtrace::kMinRun + 9).pitch == 70 * 32);
    // idle: no new samples, the clock keeps scrolling with silence
    ring.sync(f, 200 + 30); // 30 ms = 5 sample periods of 5.805 ms
    CHECK(f.at(0, 0).on == 0 && f.at(0, 4).on == 0 && f.at(0, 5).on == 1);
    // a short pause (< 3 periods) does not insert silence
    FakeRing r2;
    pitchtrace::Follower f2;
    r2.sync(f2, 0);
    r2.put(1000, 10, 1);
    r2.sync(f2, 20);
    r2.sync(f2, 20 + 60);
    CHECK(f2.at(0, 0).on == 1);

    // decimation: at most kMaxPts points, last bucket sample = newest, order preserved
    pitchtrace::Follower d;
    FakeRing r3;
    r3.sync(d, 0);
    for (int i = 0; i < pitchtrace::kWin; i++)
    {
        r3.put(i * 4, 100, 1);
        if ((i & 63) == 63) r3.sync(d, 1 + i); // the ring only holds 128: the main loop reads in between
    }
    r3.sync(d, 10 + pitchtrace::kWin);
    pitchtrace::Sample pts[pitchtrace::kMaxPts];
    const int n = pitchtrace::decimate(d, 0, pts, pitchtrace::kMaxPts);
    CHECK(n == pitchtrace::kMaxPts && pitchtrace::kMaxPts >= 160 && pitchtrace::kEnvPts * 2 == pitchtrace::kMaxPts);
    CHECK(pts[n - 1].pitch == (pitchtrace::kWin - 1) * 4);
    bool mono = true;
    for (int i = 1; i < n; i++) mono = mono && pts[i].pitch > pts[i - 1].pitch;
    CHECK(mono);
    pitchtrace::Sample big[pitchtrace::kWin];
    CHECK(pitchtrace::decimate(d, 0, big, pitchtrace::kWin + 50) == pitchtrace::kWin);

    // auto range: min span, grows at once, shrinks slowly, holds in silence
    pitchtrace::Range rg;
    pitchtrace::Follower e;
    rg.follow(e); // nothing sounds: default range stays
    CHECK(!rg.init && rg.ilo() == 48 && rg.ihi() == 72);
    FakeRing r4;
    r4.sync(e, 0);
    for (int i = 0; i < 5; i++) r4.put(60 * 32, 0, 1);
    r4.sync(e, 5);
    rg.follow(e);
    CHECK(rg.init && rg.hi - rg.lo >= pitchtrace::kMinSpan - 0.01f);
    CHECK(rg.lo <= 58 && rg.hi >= 62);
    for (int i = 0; i < 5; i++) r4.put(100 * 32, 0, 1);
    r4.sync(e, 10);
    rg.follow(e);
    CHECK(rg.hi >= 102 && rg.lo <= 58); // grew at once to the high voice (voice 3 sits 3 semitones higher)
    const float hi1 = rg.hi;
    pitchtrace::Follower quiet;
    FakeRing r5;
    r5.sync(quiet, 0);
    for (int i = 0; i < 5; i++) r5.put(60 * 32, 0, 1);
    r5.sync(quiet, 5);
    rg.follow(quiet);
    CHECK(rg.hi < hi1 && rg.hi > hi1 - 6.0f); // slow shrink, 10 % per call
    char nb[8];
    CHECK(strcmp(pitchtrace::noteName(60, nb), "C4") == 0);
    CHECK(strcmp(pitchtrace::noteName(61, nb), "C#4") == 0);
    CHECK(strcmp(pitchtrace::noteName(0, nb), "C-1") == 0);
    CHECK(strcmp(pitchtrace::noteName(127, nb), "G9") == 0);
    CHECK(strcmp(pitchtrace::noteName(-3, nb), "A-2") == 0);
}

static void checkPitchBounds(const Ctx& c);

// ---- v0.3.6 (plan 014 A3): the REAL engine through ring -> follower -> range -> drawing, as the main loop runs it -------------------
struct Sim
{
    Engine e;
    pitchtrace::Follower f;
    pitchtrace::Range rg;
    double ms = 1000.0;
    int blocksSinceSync = 0;
    Sim() { sync(); } // the view opens: the follower starts at the engine's head
    void sync()
    {
        const pitchtrace::Sample* rings[pitchtrace::kVoices];
        for (int v = 0; v < pitchtrace::kVoices; v++) rings[v] = (const pitchtrace::Sample*)e.traceRing(v);
        f.sync(rings, kTraceLen, e.traceHeadIndex(), (uint32_t)ms);
        rg.follow(f);
    }
    // n blocks of audio, the main loop passes about every 14 blocks (40 ms)
    void run(int n)
    {
        int16_t l[kBlock], r[kBlock];
        for (int i = 0; i < n; i++)
        {
            e.render(l, r);
            ms += kBlockSec * 1000.0;
            if (++blocksSinceSync >= 14)
            {
                blocksSinceSync = 0;
                sync();
            }
        }
        sync();
    }
};
struct Seen
{
    int strips[pitchtrace::kVoices];
    int points[pitchtrace::kVoices];
    int minY[pitchtrace::kVoices], maxY[pitchtrace::kVoices];
    int total = 0;
};
static Seen drawSeen(Sim& s)
{
    Ctx c;
    pitchtrace::draw(c, s.f, s.rg, "PolySynth pitch", "FX2: close");
    CHECK(c.bytes <= pitchtrace::kDisplayBudget);
    Seen o;
    for (int v = 0; v < pitchtrace::kVoices; v++)
    {
        o.strips[v] = o.points[v] = 0;
        o.minY[v] = 9999;
        o.maxY[v] = -9999;
    }
    for (size_t i = 0; i < c.stripY.size(); i++)
        for (int v = 0; v < pitchtrace::kVoices; v++)
            if (c.stripColor[i] == pitchtrace::voiceColor(v))
            {
                o.strips[v]++;
                o.points[v] += (int)c.stripY[i].size();
                for (int y : c.stripY[i])
                {
                    if (y < o.minY[v]) o.minY[v] = y;
                    if (y > o.maxY[v]) o.maxY[v] = y;
                }
                o.total++;
            }
    return o;
}

static void test_real_engine_chord_visible()
{
    // a sustained four note chord: four lines, each many points, at different heights
    {
        Sim s;
        s.e.setParam(P_VOICES, 4);
        const uint8_t chord[4] = { 60, 64, 67, 71 };
        for (int i = 0; i < 4; i++) s.e.noteOn(chord[i], 100);
        s.run(14 * 20);
        const Seen o = drawSeen(s);
        int dist = 0;
        for (int v = 0; v < 4; v++)
        {
            CHECK(o.strips[v] >= 1);
            CHECK(o.points[v] >= 8);
            for (int w = v + 1; w < 4; w++) dist += o.maxY[v] != o.maxY[w];
        }
        CHECK(dist == 6); // four distinct heights
    }
    // a staccato stab (note-on + note-off a few blocks later, the engine idles right after): all four voices stay visible
    {
        Sim s;
        s.e.setParam(P_VOICES, 4);
        s.e.setParam(P_RELEASE, 5);
        const uint8_t chord[4] = { 60, 64, 67, 71 };
        for (int i = 0; i < 4; i++) s.e.noteOn(chord[i], 100);
        s.run(3);
        for (int i = 0; i < 4; i++) s.e.noteOff(chord[i]);
        s.run(14 * 4); // engine idle by now, the clock runs
        const Seen o = drawSeen(s);
        for (int v = 0; v < 4; v++)
        {
            CHECK(o.strips[v] >= 1);
            CHECK(o.points[v] >= 2);
        }
        // and a second stab 1.5 s later shows next to the first one
        s.run(14 * 40);
        for (int i = 0; i < 4; i++) s.e.noteOn(chord[i], 100);
        s.run(2);
        for (int i = 0; i < 4; i++) s.e.noteOff(chord[i]);
        s.run(14 * 4);
        const Seen o2 = drawSeen(s);
        for (int v = 0; v < 4; v++) CHECK(o2.strips[v] >= 2);
    }
    // the very first note after the view opened on an idle engine (no earlier sample) and a one-block note
    {
        Sim s;
        s.run(14 * 10);
        s.e.noteOn(62, 100);
        s.e.noteOff(62);
        s.run(14 * 3);
        const Seen o = drawSeen(s);
        int any = 0;
        for (int v = 0; v < 4; v++) any += o.strips[v];
        CHECK(any >= 1);
    }
    // glide: the line moves between the two notes; LFO to pitch: it wiggles
    {
        Sim s;
        s.e.setParam(P_VOICES, 1);
        s.e.setParam(P_GLIDE, 60);
        s.e.setParam(P_SUSTAIN, 100);
        s.e.noteOn(60, 100);
        s.run(14 * 10);
        s.e.noteOn(72, 100);
        s.run(14 * 30);
        const Seen o = drawSeen(s);
        CHECK(o.strips[0] >= 1 && o.points[0] >= 10);
        CHECK(o.maxY[0] - o.minY[0] > 30); // a slope, not a flat line
        Sim l;
        l.e.setParam(P_VOICES, 1);
        l.e.setParam(P_LFO_DEST, 0);
        l.e.setParam(P_LFO_DEPTH, 100);
        l.e.setParam(P_LFO_RATE, 70);
        l.e.setParam(P_SUSTAIN, 100);
        l.e.noteOn(60, 100);
        l.run(14 * 40);
        const Seen q = drawSeen(l);
        CHECK(q.points[0] >= 20 && q.maxY[0] - q.minY[0] >= 6);
    }
    // the follower fed from the first pass after opening shows a note that is already sounding from then on, never beyond the panel
    {
        Sim s;
        s.e.noteOn(48, 100);
        s.e.noteOn(55, 100);
        s.run(14 * 6);
        Ctx c;
        pitchtrace::draw(c, s.f, s.rg, "t", "h");
        checkPitchBounds(c);
        CHECK(c.strips >= 4);
    }
}

static void checkPitchBounds(const Ctx& c)
{
    CHECK(c.minX >= 0 && c.maxX <= 800);
    CHECK(c.pminY >= pitchtrace::kY0 - 1 && c.pmaxY <= pitchtrace::kY1 + 1);
    CHECK(c.minY >= pitchtrace::kPanelY0 && c.maxY <= pitchtrace::kPanelY1);
    CHECK(c.textMinX >= 0 && c.textMaxX <= 800);
    CHECK(c.textMinY >= pitchtrace::kPanelY0 && c.textMaxY <= pitchtrace::kPanelY1);
}

static void test_pitch_layout_and_size()
{
    // case 1: four voices sounding the whole window over a wide range with fast wiggles (every strip full)
    pitchtrace::Follower f;
    FakeRing ring;
    ring.sync(f, 0);
    for (int i = 0; i < pitchtrace::kWin; i++)
        for (int v = 0; v < pitchtrace::kVoices; v++) { /* put() writes all voices; shape per voice below */ }
    for (int i = 0; i < pitchtrace::kWin; i++)
    {
        for (int v = 0; v < pitchtrace::kVoices; v++)
        {
            pitchtrace::Sample& x = ring.s[v][ring.head & 127];
            x.pitch = (int16_t)(((24 + v * 24) * 32) + (int)(sin(i * 0.9 + v) * 40.0));
            x.env = (uint8_t)(128 + 127 * sin(i * 0.3));
            x.on = 1;
        }
        ring.head++;
        if ((i & 63) == 63) ring.sync(f, 1 + i);
    }
    ring.sync(f, 10 + pitchtrace::kWin);
    pitchtrace::Range rg;
    rg.follow(f);
    Ctx c;
    pitchtrace::draw(c, f, rg, "PolySynth pitch  (dim line: envelope)", "FX2: close");
    printf("  pitch trace, 4 full voices, wide range: %d bytes, %d strips, %d texts\n", c.bytes, c.strips, c.texts);
    CHECK(c.bytes <= pitchtrace::kDisplayBudget);
    CHECK(c.strips == 8);
    CHECK(c.maxStripPts <= pitchtrace::kMaxPts);
    checkPitchBounds(c);
    // the envelope line sits above its pitch line (smaller y) wherever the envelope is > 0 (v0.3.7: the envelope line has every second
    // point of the pitch line, so point i of it belongs to point 2 i + 1 of the pitch line)
    bool above = true;
    for (int v = 0; v < 4; v++)
    {
        const std::vector<int>& envY = c.stripY[v * 2];
        const std::vector<int>& pitY = c.stripY[v * 2 + 1];
        CHECK(envY.size() * 2 == pitY.size());
        for (size_t i = 0; i < envY.size() && 2 * i + 1 < pitY.size(); i++) above = above && envY[i] <= pitY[2 * i + 1];
    }
    CHECK(above);
    // colours: the pitch lines carry the four key colours
    CHECK(c.stripColor[1] == seqpal::kNote && c.stripColor[3] == seqpal::kInstr && c.stripColor[5] == seqpal::kFx1 && c.stripColor[7] == seqpal::kFx2);
    CHECK(c.stripColor[0] == seqpal::dim(seqpal::kNote));

    // case 2: a pathological on / off pattern (runs of two points): every curve in many short strips
    pitchtrace::Follower f2;
    FakeRing r2;
    r2.sync(f2, 0);
    for (int i = 0; i < pitchtrace::kWin; i++)
    {
        for (int v = 0; v < pitchtrace::kVoices; v++)
        {
            pitchtrace::Sample& x = r2.s[v][r2.head & 127];
            x.pitch = (int16_t)((48 + v) * 32);
            x.env = 100;
            x.on = (i % (pitchtrace::kMinRun + 5)) < pitchtrace::kMinRun; // v0.3.7: runs of kMinRun (the shortest the follower shows), gaps of 5
        }
        r2.head++;
        if ((i & 63) == 63) r2.sync(f2, 1 + i);
    }
    r2.sync(f2, 10 + pitchtrace::kWin);
    pitchtrace::Range rg2;
    rg2.follow(f2);
    Ctx c2;
    pitchtrace::draw(c2, f2, rg2, "PolySynth pitch  (dim line: envelope)", "FX2: close");
    printf("  pitch trace, worst on/off pattern: %d bytes, %d strips\n", c2.bytes, c2.strips);
    CHECK(c2.bytes <= pitchtrace::kDisplayBudget);
    checkPitchBounds(c2);

    // case 3: extreme pitches (notes 0 and 127, huge span: the semitone grid is dropped, only octaves)
    pitchtrace::Follower f3;
    FakeRing r3;
    r3.sync(f3, 0);
    for (int i = 0; i < pitchtrace::kWin; i++)
    {
        for (int v = 0; v < pitchtrace::kVoices; v++)
        {
            pitchtrace::Sample& x = r3.s[v][r3.head & 127];
            x.pitch = (int16_t)((v % 2 ? 127 : 0) * 32 + (v % 2 ? 31 : 0));
            x.env = 255;
            x.on = 1;
        }
        r3.head++;
        if ((i & 63) == 63) r3.sync(f3, 1 + i);
    }
    r3.sync(f3, 10 + pitchtrace::kWin);
    pitchtrace::Range rg3;
    rg3.follow(f3);
    Ctx c3;
    pitchtrace::draw(c3, f3, rg3, "t", "h");
    CHECK(c3.bytes <= pitchtrace::kDisplayBudget);
    checkPitchBounds(c3);

    // case 4: empty window: just the panel, grid and texts
    pitchtrace::Follower f4;
    pitchtrace::Range rg4;
    Ctx c4;
    pitchtrace::draw(c4, f4, rg4, "t", "h");
    CHECK(c4.strips == 0 && c4.bytes < 1500);
    checkPitchBounds(c4);

    // stock theme colours (Themes.cpp patternTrackerColors) and the dimming
    CHECK(seqpal::kNote == 0x00E8BE && seqpal::kInstr == 0xFEF749 && seqpal::kFx1 == 0xBB58F1 && seqpal::kFx2 == 0x57F1FF);
    CHECK(seqpal::mix(0xFFFFFF, 0x000000, 128) == 0x808080);
    CHECK(seqpal::mix(0x123456, 0xABCDEF, 0) == 0x123456);
}

static void test_launcher_budgets()
{
    using namespace seqsession;
    using namespace seqsession::clipgrid;
    View v;
    memset(&v, 0, sizeof v);
    GridPlan empty = planGrid(v);
    CHECK(empty.cost > 2000 && empty.cost < kGridBudget);
    // every cell lit, every flag set: the optional parts drop, the budget holds
    View w;
    memset(&w, 0, sizeof w);
    for (uint8_t r = 0; r < kVisRows; r++)
    {
        w.scene[r] = CELL_PLAYING;
        w.sceneFlags[r] = F_ONEX;
        for (uint8_t t = 0; t < kTracks; t++)
        {
            w.cell[r][t] = (uint8_t)(1 + (r + t) % 4);
            w.flags[r][t] = F_ONEX | F_END | F_STOPMARK;
        }
    }
    w.msg = MSG_UNDO_CLEARED;
    w.modeHeld = 1;
    w.focusPattern = 5;
    GridPlan worst = planGrid(w);
    printf("  launcher grid: empty %u bytes, worst %u bytes (budget %d)\n", (unsigned)empty.cost, (unsigned)worst.cost, (int)kGridBudget);
    CHECK(worst.cost <= kGridBudget);
    // the marks control: all 16 tracks playing and queued, blinking
    Marks m;
    memset(&m, 0, sizeof m);
    for (uint8_t t = 0; t < kTracks; t++)
    {
        m.playRow[t] = (uint8_t)(t % kVisRows);
        m.prog[t] = 40;
        m.qRow[t] = (uint8_t)((t + 1) % kVisRows);
        m.qKind[t] = MK_BLINK;
    }
    m.sceneRow = 2;
    m.songRow = 1;
    m.blink = 1;
    m.countdown = 3;
    const uint16_t mc = marksCost(m);
    printf("  launcher marks, all playing + queued: %u bytes (budget %d)\n", (unsigned)mc, (int)kMarksBudget);
    CHECK(mc <= kMarksBudget);
    // grid + marks stay well under the 8 KB frame
    CHECK(worst.cost + mc < 8000 - 800);
    // plan 017: scene colours. Twelve rows in eight different colours, everything else as the worst case above: the budget still holds
    // (the optional parts drop first), and the mandatory part grows by the colour batches only
    {
        View c = w;
        for (uint8_t r = 0; r < kVisRows; r++)
            c.sceneColor[r] = (uint8_t)(1 + r % 8);
        GridPlan wc = planGrid(c);
        printf("  launcher grid, 12 coloured rows (8 colours): worst %u bytes (budget %d)\n", (unsigned)wc.cost, (int)kGridBudget);
        CHECK(wc.cost <= kGridBudget && worst.cost <= kGridBudget);
        CHECK(sceneColorCost(c) == 8 * 5 * kBatchCost + 12 * kRectCost);
        CHECK(wc.cost + mc < 8000 - 800);
        // no colours: nothing is added (the plain look costs what it did)
        CHECK(sceneColorCost(w) == 0 && sceneColorCost(v) == 0);
        // the mandatory part with every colour in use still leaves the optional parts room in a normal view
        View n;
        memset(&n, 0, sizeof n);
        for (uint8_t r = 0; r < kVisRows; r++)
            n.sceneColor[r] = (uint8_t)(1 + r % 8);
        GridPlan pn = planGrid(n);
        CHECK(pn.outlines && pn.notches && pn.stops && pn.cost == empty.cost + sceneColorCost(n));
        // one colour on one row
        View o;
        memset(&o, 0, sizeof o);
        o.sceneColor[3] = 2;
        CHECK(sceneColorCost(o) == 5 * kBatchCost + kRectCost && planGrid(o).cost == empty.cost + sceneColorCost(o));
        // an out-of-palette value from a later file is counted at most as one more batch group, never beyond the budget
        o.sceneColor[4] = 15;
        CHECK(planGrid(o).cost <= kGridBudget);
    }
    // launcher palette: distinct states
    CHECK(seqpal::kCellEmpty != seqpal::kCellNotes && seqpal::kCellNotes != seqpal::kCellPlaying && seqpal::kSceneNotes != seqpal::kSceneEmpty);
}

// ---- v0.3.6 (plan 014 A1): host model of the display frame budget (display.cpp step3 appends controls from the highest table index
// down while the sum stays under 8172 bytes and then drops ALL remaining, i.e. the lowest-index ones: title bar, labels, our overlay).
// The pattern control (tracker.cpp) is estimated from its code: per visible row and track about 12 characters as BITMAPS vertices
// (VERTEX2II, 4 bytes each), plus the row / column rectangles, lines, cursor and playhead (about 1.2 KB). The other numbers are the
// measured / limit values of the host tests (overlay 3600 limit, pitch view 4096 limit).
static void test_frame_budget_model()
{
    const int kBudget = 8172;
    const int rows = 15, tracks = 8, charsPerCell = 12;
    const int patternWorst = rows * tracks * charsPerCell * 4 + 1200; // 6960
    const int patternFour = rows * 4 * charsPerCell * 4 + 1200;       // 4 tracks view: 4080
    const int stripLabels = 8 * 200 + 120;                           // eight two-line cells + background label
    const int titleBar = 600;
    const int overlay = 3600, pitch = pitchtrace::kDisplayBudget; // v0.3.7: the pitch view may use its whole budget; the overlay control is hidden while it shows
    // the old overlay: pattern + overlay + strip + title do not fit (this is what pushed the overlay out)
    CHECK(patternWorst + overlay + stripLabels + titleBar > kBudget);
    CHECK(patternFour + overlay + stripLabels + titleBar > kBudget - 1500); // even the four track view has no real room left
    // the view switch: without the pattern control the PolySynth page fits; the pitch view replaces it (both together would not fit)
    CHECK(overlay + stripLabels + titleBar <= kBudget);
    CHECK(pitchtrace::kDisplayBudget + stripLabels + titleBar <= kBudget - 400); // v0.3.7: the raised budget keeps a margin of at least 400 B
    CHECK(overlay + pitch + stripLabels + titleBar > kBudget);
}

// `make pitch-preview`: draw ops of the real engine through ring -> follower -> drawing for pitch_preview (SEQ_PITCH_PREVIEW=<dir>)
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

static void dumpPitch(const char* dir, const char* name, Sim& s)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ops", dir, name);
    DumpCtx c;
    c.f = fopen(path, "w");
    if (!c.f) return;
    pitchtrace::draw(c, s.f, s.rg, "PolySynth pitch  (dim line: envelope)", "FX2: parameters");
    fclose(c.f);
    Ctx m;
    pitchtrace::draw(m, s.f, s.rg, "PolySynth pitch  (dim line: envelope)", "FX2: parameters");
    printf("  %s: %d bytes (budget %d), %d strips, %d points\n", name, m.bytes, (int)pitchtrace::kDisplayBudget, m.strips, m.strips ? 0 : 0);
}

static void dumpPitchPreviews(const char* dir)
{
    const uint8_t chord[4] = { 60, 64, 67, 71 };
    // 1: a sustained four note chord with a slow LFO to pitch, then a second chord on top
    {
        Sim s;
        s.e.setParam(P_VOICES, 4);
        s.e.setParam(P_ATTACK, 25);
        s.e.setParam(P_SUSTAIN, 70);
        s.e.setParam(P_LFO_DEST, 0);
        s.e.setParam(P_LFO_DEPTH, 12);
        s.e.setParam(P_LFO_RATE, 40);
        for (int i = 0; i < 4; i++) s.e.noteOn(chord[i], 100);
        s.run(14 * 60);
        for (int i = 0; i < 4; i++) s.e.noteOff(chord[i]);
        s.run(14 * 10);
        const uint8_t c2[4] = { 62, 65, 69, 72 };
        for (int i = 0; i < 4; i++) s.e.noteOn(c2[i], 100);
        s.run(14 * 36);
        dumpPitch(dir, "pitch1_chord_lfo", s);
    }
    // 2: staccato chord stabs, then a glide
    {
        Sim s;
        s.e.setParam(P_VOICES, 4);
        s.e.setParam(P_RELEASE, 12);
        s.e.setParam(P_GLIDE, 0);
        const uint8_t seq[4][4] = { { 60, 64, 67, 71 }, { 57, 60, 64, 67 }, { 62, 65, 69, 72 }, { 55, 59, 62, 67 } };
        for (int k = 0; k < 4; k++)
        {
            for (int i = 0; i < 4; i++) s.e.noteOn(seq[k][i], 100);
            s.run(14 * 6);
            for (int i = 0; i < 4; i++) s.e.noteOff(seq[k][i]);
            s.run(14 * 20);
        }
        dumpPitch(dir, "pitch2_stabs", s);
    }
    // 3: one voice with glide between three notes and a fast LFO wiggle
    {
        Sim s;
        s.e.setParam(P_VOICES, 1);
        s.e.setParam(P_GLIDE, 55);
        s.e.setParam(P_SUSTAIN, 100);
        s.e.setParam(P_LFO_DEST, 0);
        s.e.setParam(P_LFO_DEPTH, 25);
        s.e.setParam(P_LFO_RATE, 70);
        s.e.noteOn(60, 100);
        s.run(14 * 30);
        s.e.noteOn(72, 100);
        s.run(14 * 45);
        s.e.noteOn(65, 100);
        s.run(14 * 45);
        dumpPitch(dir, "pitch3_glide_lfo", s);
    }
    // 4: chord with glide + stabs mixed: a held bass note, three voices stabbing on top
    {
        Sim s;
        s.e.setParam(P_VOICES, 4);
        s.e.setParam(P_RELEASE, 15);
        s.e.setParam(P_SUSTAIN, 80);
        s.e.setParam(P_LFO_DEST, 0);
        s.e.setParam(P_LFO_DEPTH, 6);
        s.e.setParam(P_LFO_RATE, 30);
        s.e.noteOn(36, 100);
        for (int k = 0; k < 6; k++)
        {
            s.run(14 * 12);
            s.e.noteOn((uint8_t)(60 + 3 * (k % 4)), 100);
            s.e.noteOn((uint8_t)(64 + 3 * (k % 3)), 100);
            s.run(14 * 4);
            s.e.noteOff((uint8_t)(60 + 3 * (k % 4)));
            s.e.noteOff((uint8_t)(64 + 3 * (k % 3)));
        }
        s.run(14 * 10);
        dumpPitch(dir, "pitch4_bass_and_stabs", s);
    }
}

int main()
{
    if (const char* dir = getenv("SEQ_PITCH_PREVIEW"))
    {
        dumpPitchPreviews(dir);
        return 0;
    }
    test_frame_budget_model();
    test_engine_ring();
    test_follower_decimate_range();
    test_real_engine_chord_visible();
    test_pitch_layout_and_size();
    test_launcher_budgets();
    printf("trace: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
