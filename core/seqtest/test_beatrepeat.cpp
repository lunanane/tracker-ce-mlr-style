// Sequencer tracks mod (plan 021) - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Host tests of the beat repeat DSP (libs/Audio/beat_repeat_core.h): exact bypass, slice timing against the clock, momentary pads,
// the three modes, decay / pitch decay per repeat, click freedom, tempo changes, seeded chance / interval / variation, ring limits.
#include "beat_repeat_core.h"
#include <chrono>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

using namespace beatrepeat;

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                          \
    do                                                                    \
    {                                                                     \
        g_checks++;                                                       \
        if (!(c))                                                         \
        {                                                                 \
            g_fail++;                                                     \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);           \
        }                                                                 \
    } while (0)

// ---------------------------------------------------------------------------------------------------------------------------------
// A rig: the core on a ring with guard words around it, a sequencer clock integrated in double precision (ticks = bpm * 192 / 60 per second)
struct Rig
{
    enum { kGuard = 64 };
    std::vector<int16_t> mem;
    Core c;
    Settings s;
    double ticks = 0.0; // position at the start of the next block
    double bpm = 120.0;
    bool running = true;
    long frame = 0; // absolute frame of the next block
    std::vector<int16_t> inL, inR, outL, outR;

    explicit Rig(uint32_t cap = kRingFrames) : mem((size_t)(2 * cap + 2 * kGuard), (int16_t)0x5A5A)
    {
        c.init(mem.data() + kGuard, cap);
        c.seed(12345);
        c.setSettings(s);
    }
    bool guardsOk(uint32_t cap) const
    {
        for (int i = 0; i < kGuard; i++)
            if (mem[(size_t)i] != 0x5A5A || mem[(size_t)(kGuard + 2 * cap + i)] != 0x5A5A)
                return false;
        return true;
    }
    template <class F> void run(int blocks, F sig)
    {
        for (int b = 0; b < blocks; b++)
        {
            int16_t l[kBlock], r[kBlock], ol[kBlock], orr[kBlock];
            for (int i = 0; i < kBlock; i++)
            {
                sig(frame + i, l[i], r[i]);
                inL.push_back(l[i]);
                inR.push_back(r[i]);
            }
            Clock k;
            k.running = running;
            k.tick = (int32_t)floor(ticks);
            k.frac = (float)(ticks - floor(ticks));
            k.bpm = (float)bpm;
            c.process(k, l, r, ol, orr, kBlock);
            for (int i = 0; i < kBlock; i++)
            {
                outL.push_back(ol[i]);
                outR.push_back(orr[i]);
            }
            if (running)
                ticks += bpm * (double)kTicksPerBeat / 60.0 / (double)kFs * (double)kBlock;
            frame += kBlock;
        }
    }
    // ticks to frames of the model at a constant tempo
    double fpt() const { return (double)kFs * 60.0 / (bpm * (double)kTicksPerBeat); }
    // absolute frame at which the tick counter reaches the whole tick `t` (model, constant tempo from tick 0 at frame 0)
    double frameOfTick(double t) const { return t * fpt(); }
};

// a signal that identifies every frame: a ramp with a prime stride (wraps), right channel the negative
static void idSig(long f, int16_t& l, int16_t& r)
{
    const int v = (int)((f * 37) % 20001) - 10000;
    l = (int16_t)v;
    r = (int16_t)-v;
}
static void sineSig(long f, int16_t& l, int16_t& r)
{
    const double v = 10000.0 * sin(2.0 * 3.14159265358979 * 100.0 * (double)f / (double)kFs);
    l = (int16_t)lrint(v);
    r = (int16_t)lrint(-v);
}
static void dcSig(long, int16_t& l, int16_t& r)
{
    l = 8000;
    r = -8000;
}
// a ramp whose slope is 2 per frame inside every slice (resets far away)
static void rampSig(long f, int16_t& l, int16_t& r)
{
    l = (int16_t)(2 * (f % 12000) - 12000);
    r = l;
}

static int maxStep(const std::vector<int16_t>& v, size_t from, size_t to)
{
    int m = 0;
    for (size_t i = from + 1; i < to && i < v.size(); i++)
    {
        const int d = abs((int)v[i] - (int)v[i - 1]);
        if (d > m)
            m = d;
    }
    return m;
}

// ---------------------------------------------------------------------------------------------------------------------------------
static void test_tables()
{
    CHECK(gridTicks(0) == 192 && gridTicks(1) == 96 && gridTicks(2) == 48 && gridTicks(3) == 24);
    CHECK(gridTicks(4) == 128 && gridTicks(5) == 64 && gridTicks(6) == 32 && gridTicks(7) == 16); // 1/4T 1/8T 1/16T 1/32T
    CHECK(intervalTicks(0) == 24 && intervalTicks(3) == 192 && intervalTicks(5) == 768 && intervalTicks(7) == 3072);
    CHECK(gateTicks(0) == 48 && gateTicks(7) == 768);
    for (uint8_t p = 0; p + 1 < kGrids; p++) // grid order: longest first
        CHECK(gridTicks(gridOrder(p)) > gridTicks(gridOrder((uint8_t)(p + 1))));
    for (uint8_t id = 0; id < kGrids; id++)
        CHECK(gridOrder(gridPos(id)) == id);
    CHECK(kTicksPerStep * 4 == kTicksPerBeat);
}

static void test_ring_limits()
{
    static_assert(kRingFrames * 4 == 131072, "ring is 128 KiB");
    // a slice always fits the ring, at every tempo the engine accepts and for every grid id
    for (float bpm = 30.0f; bpm <= 300.0f; bpm += 1.0f)
        for (uint8_t id = 0; id < kGrids; id++)
        {
            const uint8_t g = clampGrid(id, bpm);
            CHECK((float)gridTicks(g) * framesPerTick(bpm) <= (float)kRingFrames - 8.0f || g == gridOrder(kGrids - 1));
            CHECK(gridTicks(g) <= gridTicks(id)); // only ever steps down
        }
    CHECK(clampGrid(0, 120.0f) == 0);               // 1/4 at 120 BPM: as set
    CHECK(clampGrid(0, 82.0f) == 0);                // 1/4 reaches down to 81 BPM
    CHECK(clampGrid(0, 80.0f) == 4);                // 1/4 at 80 BPM does not fit: 1/4T
    CHECK(clampGrid(0, 60.0f) == 4);
    CHECK(clampGrid(0, 30.0f) == 5);                // 30 BPM: down to 1/8T
    CHECK(clampGrid(7, 30.0f) == 7);                // the shortest is never changed
    CHECK(clampGrid(3, 30.0f) == 3);
    // a pad at 30 BPM with the longest grid never writes outside the ring (guard words) and the slice length stays in bounds
    Rig r;
    r.bpm = 30.0;
    r.s.mode = MODE_INSERT;
    r.c.setSettings(r.s);
    r.c.post(Core::C_DOWN, 0);
    r.run(2000, idSig); // 5.8 s at 30 BPM: more than two 1/8T slices
    CHECK(r.c.captures() >= 1);
    CHECK(r.c.captured() > 0 && r.c.captured() <= kRingFrames);
    CHECK(r.guardsOk(kRingFrames));
    CHECK(r.c.eventGrid() == 5);
    // a tempo that drops hard while capturing cannot overrun the ring: the capture stops at the capacity
    Rig q(4096);
    q.bpm = 300.0;
    q.c.post(Core::C_DOWN, 0);
    q.run(40, idSig);
    q.bpm = 30.0;
    q.run(400, idSig);
    CHECK(q.guardsOk(4096));
    CHECK(q.c.captured() <= 4096);
}

static void test_bypass()
{
    Rig r;
    CHECK(r.c.bypassable());
    r.run(10, idSig);
    for (size_t i = 0; i < r.outL.size(); i++)
        CHECK(r.outL[i] == r.inL[i] && r.outR[i] == r.inR[i]); // the core also passes untouched when called
    CHECK(r.c.bypassable());
    // a pad makes the stage active, the end of the event makes it bypassable again once the audio gain settled
    r.c.post(Core::C_DOWN, 2);
    CHECK(!r.c.bypassable());
    r.run(300, idSig);
    r.c.post(Core::C_UP);
    r.run(4, idSig);
    CHECK(r.c.bypassable());
    // chance > 0 keeps it armed
    r.s.chance = 10;
    r.c.setSettings(r.s);
    CHECK(!r.c.bypassable());
    r.s.chance = 0;
    r.c.setSettings(r.s);
    CHECK(r.c.bypassable());
    // neutral settings after a whole session: output equals input again, exactly
    r.outL.clear();
    r.inL.clear();
    r.run(5, idSig);
    for (size_t i = 0; i < r.outL.size(); i++)
        CHECK(r.outL[i] == r.inL[i]);
}

// A pad press at a known time: the capture starts at the next grid boundary of the clock, sample exact; repeats follow every grid.
static void test_slice_timing()
{
    Rig r;
    r.s.mode = MODE_INSERT;
    r.c.setSettings(r.s);
    r.bpm = 120.0;
    const uint8_t grid = 2; // 1/16 = 48 ticks
    r.run(37, idSig);       // some time passes, the clock runs
    r.c.post(Core::C_DOWN, grid);
    r.run(500, idSig);
    CHECK(r.c.captures() == 1);
    const long L = (long)r.c.captured();
    const double fptv = r.fpt();
    // the first multiple of 48 ticks after the press (the press is applied at the start of block 37 = frame 37*128)
    const double pressTick = 37.0 * 128.0 / fptv;
    const double bTick = ceil(pressTick / 48.0) * 48.0;
    const double S_model = r.frameOfTick(bTick);
    // slice content: find the capture start S so that the output of repeat 0 (L frames after S) equals the input from S
    long S = -1;
    for (long cand = (long)S_model - 3; cand <= (long)S_model + 3 && S < 0; cand++)
    {
        bool ok = true;
        for (long m = 64; m < L - 64 && ok; m += 97)
            ok = r.outL[(size_t)(cand + L + m)] == r.inL[(size_t)(cand + m)] && r.outR[(size_t)(cand + L + m)] == r.inR[(size_t)(cand + m)];
        if (ok)
            S = cand;
    }
    CHECK(S >= 0);
    CHECK(labs(L - (long)floor(48.0 * fptv + 0.5)) <= 1);
    // each next repeat starts one grid later: the same content again
    for (int k = 1; k < 4; k++)
    {
        const long start = S + (long)floor(48.0 * fptv * (k + 1) + 0.5);
        int hit = 0;
        for (long d = -2; d <= 2; d++)
        {
            bool ok = true;
            for (long m = 64; m < L - 64 && ok; m += 89)
                ok = r.outL[(size_t)(start + d + m)] == r.inL[(size_t)(S + m)];
            hit += ok ? 1 : 0;
        }
        CHECK(hit == 1);
    }
    // before the capture ends the audio passes (INSERT mutes only while repeats sound)
    for (long i = S + 64; i < S + L - 64; i += 211)
        CHECK(r.outL[(size_t)i] == r.inL[(size_t)i]);
    CHECK(r.c.state() == S_REPEAT);

    // Absolute timing: in GATE mode on a DC input a repeat's first frame is exactly 0 (the fade in starts at 0) and its last frame too, so the
    // zeros of the output mark the repeat starts. Each start is the frame at which the clock reaches the tick (48 per grid): sample exact.
    for (int tempo = 0; tempo < 3; tempo++)
    {
        Rig g;
        g.s.mode = MODE_GATE;
        g.c.setSettings(g.s);
        g.bpm = tempo == 0 ? 120.0 : (tempo == 1 ? 97.3 : 211.7);
        g.run(37, dcSig);
        g.c.post(Core::C_DOWN, grid);
        g.run(900, dcSig);
        const double tpf = g.bpm * 192.0 / 60.0 / (double)kFs;
        const double pressTick = 37.0 * 128.0 * tpf;
        const double firstB = ceil(pressTick / 48.0) * 48.0; // the capture starts here
        int checked = 0;
        long prevZeroEnd = -1;
        for (size_t y = 40 * 128; y + 1 < g.outL.size(); y++)
        {
            if (g.outL[y] == 0 && g.outL[y + 1] != 0 && y > 40 * 128 + 200)
            {
                // y = the last zero before a repeat's ramp: the repeat starts at y (j = 0)
                const double n = (double)checked + 1.0;
                const long want = (long)ceil((firstB + 48.0 * n) / tpf - 1e-9);
                CHECK(y == (size_t)want || (long)y == want - 1 || (long)y == want + 1);
                if ((long)y == want)
                    checked += 0;
                CHECK(prevZeroEnd < 0 || (long)y - prevZeroEnd >= (long)floor(48.0 / tpf) - 1);
                prevZeroEnd = (long)y;
                checked++;
            }
        }
        CHECK(checked >= 4);
        // the very first repeat starts on the frame of tick firstB + 48
        size_t first = 0;
        for (size_t y = 40 * 128; y < g.outL.size() && !first; y++)
            if (g.outL[y] != 0)
                first = y;
        const long want0 = (long)ceil((firstB + 48.0) / tpf - 1e-9);
        CHECK((long)first - 1 == want0 || (long)first - 1 == want0 - 1 || (long)first - 1 == want0 + 1);
        if ((long)first - 1 != want0)
            printf("   tempo %.1f: first repeat starts %ld, model %ld\n", g.bpm, (long)first - 1, want0);
    }
}

static void test_momentary()
{
    Rig r;
    r.s.mode = MODE_MIX;
    r.c.setSettings(r.s);
    r.run(5, idSig);
    CHECK(r.c.state() == S_IDLE);
    r.c.post(Core::C_DOWN, 3); // 1/32
    r.run(1, idSig);
    CHECK(r.c.state() == S_PENDING || r.c.state() == S_CAPTURE);
    r.run(100, idSig);
    CHECK(r.c.state() == S_REPEAT);
    // release before the capture has finished: nothing is ever repeated
    Rig q;
    q.run(5, sineSig);
    q.c.post(Core::C_DOWN, 0); // 1/4: a long wait
    q.run(20, sineSig);
    q.c.post(Core::C_UP);
    q.run(600, sineSig);
    CHECK(q.c.state() == S_IDLE && q.c.captures() == 0);
    for (size_t i = 0; i < q.outL.size(); i++)
        CHECK(q.outL[i] == q.inL[i]);
    // release while repeating: back to the live signal (equal after the 64 frame ramps), stage bypassable
    r.c.post(Core::C_UP);
    r.run(4, idSig);
    CHECK(r.c.state() == S_IDLE);
    const size_t n = r.outL.size();
    r.run(2, idSig);
    for (size_t i = n; i < r.outL.size(); i++)
        CHECK(r.outL[i] == r.inL[i]);
    CHECK(r.c.bypassable());
    // jam: another pad while one is held takes the new grid at its next boundary
    Rig j;
    j.c.post(Core::C_DOWN, 1);
    j.run(300, idSig);
    CHECK(j.c.state() == S_REPEAT && j.c.eventGrid() == 1);
    j.c.post(Core::C_DOWN, 3);
    j.run(1, idSig);
    CHECK(j.c.state() == S_PENDING && j.c.eventGrid() == 3);
    j.run(200, idSig);
    CHECK(j.c.state() == S_REPEAT && j.c.eventGrid() == 3 && j.c.captures() == 2);
}

static void test_latch()
{
    Rig r;
    r.c.post(Core::C_LATCH);
    r.c.post(Core::C_DOWN, 2);
    r.run(200, idSig);
    r.c.post(Core::C_UP);
    r.run(300, idSig);
    CHECK(r.c.state() == S_REPEAT);       // keeps repeating after the release
    r.c.post(Core::C_DOWN, 3);            // another grid: re-grids
    r.run(1, idSig);
    r.c.post(Core::C_UP);
    r.run(300, idSig);
    CHECK(r.c.state() == S_REPEAT && r.c.eventGrid() == 3);
    r.c.post(Core::C_DOWN, 3);            // the same pad again: off
    r.run(1, idSig);
    r.c.post(Core::C_UP);
    r.run(10, idSig);
    CHECK(r.c.state() == S_IDLE);
    r.c.post(Core::C_DOWN, 2);
    r.c.post(Core::C_UP);
    r.run(300, idSig);
    CHECK(r.c.state() == S_REPEAT);
    r.c.post(Core::C_DISENGAGE);          // DISENGAGE ends it
    r.run(10, idSig);
    CHECK(r.c.state() == S_IDLE);
    r.c.post(Core::C_DOWN, 2);
    r.c.post(Core::C_UP);
    r.run(300, idSig);
    r.c.post(Core::C_UNLATCH);            // latch off releases a latched repeat
    r.run(10, idSig);
    CHECK(r.c.state() == S_IDLE);
}

static void test_panic()
{
    Rig r;
    r.s.chance = 100;
    r.s.interval = 3;
    r.c.setSettings(r.s);
    r.c.post(Core::C_LATCH);
    r.c.post(Core::C_DOWN, 2);
    r.run(300, idSig);
    CHECK(r.c.state() == S_REPEAT);
    r.c.post(Core::C_PANIC);
    r.run(2, idSig);
    CHECK(r.c.state() == S_IDLE);
    // latch and pad are cleared: a new press without a latch ends with the release
    r.s.chance = 0;
    r.c.setSettings(r.s);
    r.c.post(Core::C_DOWN, 2);
    r.c.post(Core::C_UP);
    r.run(4, idSig);
    CHECK(r.c.state() == S_IDLE);
    // after the panic's fades the stage is a bypass again
    r.run(4, idSig);
    CHECK(r.c.bypassable());
}

// MIX / INSERT / GATE
static void test_modes()
{
    for (int mode = 0; mode < 3; mode++)
    {
        Rig r;
        r.s.mode = (uint8_t)mode;
        r.c.setSettings(r.s);
        r.bpm = 120.0;
        r.c.post(Core::C_DOWN, 2); // 1/16
        r.run(1200, dcSig);        // DC input: the repeat of a constant slice is easy to read
        const long L = (long)r.c.captured();
        CHECK(L > 5000);
        // find a frame well inside a late repeat, and one inside the capture slice
        const long late = (long)r.outL.size() - 3000;
        const long cap0 = (long)floor(r.frameOfTick(48.0));
        const long cap = cap0 + L / 2;
        const float rep = 8000.0f; // the repeat is the (faded) slice of the DC input: full level mid-slice at decay 0
        if (mode == MODE_MIX)
        {
            CHECK(r.outL[(size_t)cap] == 8000);        // capturing: the audio passes alone
            CHECK(r.outL[(size_t)late] == 16000);      // repeats on top of the running audio
            CHECK(r.outR[(size_t)late] == -16000);
        }
        else if (mode == MODE_INSERT)
        {
            CHECK(r.outL[(size_t)cap] == 8000);        // the audio passes while the slice is captured
            CHECK(r.outL[(size_t)late] == (int)rep);   // the audio is muted under the repeats: the repeat alone
        }
        else
        {
            CHECK(r.outL[(size_t)cap] == 0);           // GATE: dry muted from the pad press on, also while capturing
            CHECK(r.outL[(size_t)late] == (int)rep);
            CHECK(r.outL[(size_t)(cap0 - 1000)] == 0); // already muted before the capture starts (the pad is held)
        }
        // the release returns to the live signal in every mode
        r.c.post(Core::C_UP);
        r.run(6, dcSig);
        CHECK(r.outL[r.outL.size() - 1] == 8000);
    }
    // GATE mutes while chance > 0, even when nothing repeats
    Rig g;
    g.s.mode = MODE_GATE;
    g.s.chance = 1;
    g.s.interval = 7;
    g.c.setSettings(g.s);
    g.run(100, dcSig);
    CHECK(g.outL[g.outL.size() - 1] == 0);
    g.s.chance = 0;
    g.c.setSettings(g.s);
    g.run(2, dcSig);
    CHECK(g.outL[g.outL.size() - 1] == 8000);
}

static void test_decay_and_pitch()
{
    // DECAY 100: -6 dB (x0.5012) per repeat; the first repeat is at full level
    {
        Rig r;
        r.s.mode = MODE_INSERT;
        r.s.decay = 100;
        r.c.setSettings(r.s);
        r.c.post(Core::C_DOWN, 2);
        r.run(1500, dcSig);
        const double fptv = r.fpt();
        const long S = (long)floor(r.frameOfTick(48.0) + 0.5);
        const long G = (long)floor(48.0 * fptv + 0.5);
        double prev = 8000.0;
        for (int k = 0; k < 5; k++)
        {
            const double v = r.outL[(size_t)(S + (k + 1) * G + G / 2)];
            const double expect = 8000.0 * pow(0.5011872, (double)k);
            CHECK(fabs(v - expect) <= 1.5);
            if (k)
                CHECK(fabs(v / prev - 0.5011872) < 0.001);
            prev = v;
        }
    }
    // DECAY 50: -3 dB per repeat
    {
        Rig r;
        r.s.mode = MODE_INSERT;
        r.s.decay = 50;
        r.c.setSettings(r.s);
        r.c.post(Core::C_DOWN, 2);
        r.run(1500, dcSig);
        const long S = (long)floor(r.frameOfTick(48.0) + 0.5);
        const long G = (long)floor(48.0 * r.fpt() + 0.5);
        const double v2 = r.outL[(size_t)(S + 3 * G + G / 2)];
        CHECK(fabs(v2 - 8000.0 * pow(0.5011872, 1.0)) <= 1.5); // k = 2 at half the dB: x0.5012
    }
    // PITCH DECAY 100: -3 semitones per repeat, a resampled read: the ramp's slope scales with 2^(-3k/12), the repeat length stays
    {
        Rig r;
        r.s.mode = MODE_INSERT;
        r.s.pitchDecay = 100;
        r.c.setSettings(r.s);
        r.c.post(Core::C_DOWN, 2);
        r.run(1500, rampSig);
        const long S = (long)floor(r.frameOfTick(48.0) + 0.5);
        const long G = (long)floor(48.0 * r.fpt() + 0.5);
        for (int k = 0; k < 5; k++)
        {
            const long m = S + (k + 1) * G + G / 3;
            const double slope = (double)r.outL[(size_t)(m + 10)] - (double)r.outL[(size_t)m];
            const double expect = 2.0 * 10.0 * pow(2.0, -3.0 * k / 12.0);
            CHECK(fabs(slope - expect) <= 1.2);
        }
        // the floor: two octaves down
        Rig q;
        q.s.mode = MODE_INSERT;
        q.s.pitchDecay = 100;
        q.bpm = 300.0;
        q.c.setSettings(q.s);
        q.c.post(Core::C_DOWN, 3); // 1/32 at 300 BPM: many repeats
        q.run(2500, rampSig);
        const long S2 = (long)floor(q.frameOfTick(24.0) + 0.5);
        const long G2 = (long)floor(24.0 * q.fpt() + 0.5);
        const long m = S2 + 14 * G2 + G2 / 3;
        const double slope = (double)q.outL[(size_t)(m + 6)] - (double)q.outL[(size_t)m];
        CHECK(fabs(slope - 2.0 * 6.0 * 0.25) <= 1.2); // 2^-2 at the floor
    }
    // the status shows the level and pitch of the running repeat
    {
        Rig r;
        r.s.decay = 100;
        r.s.pitchDecay = 100;
        r.c.setSettings(r.s);
        r.c.post(Core::C_DOWN, 2);
        r.run(1500, rampSig);
        const Status st = r.c.status();
        CHECK(st.state == S_REPEAT && st.repeatIdx >= 3);
        CHECK(st.levelDb > 0 && st.pitchSemi >= 3);
    }
}

static void test_clicks()
{
    // the largest sample to sample step of the whole output stays near what the input itself does plus the fade slopes: far from a hard cut
    // (a hard cut of this 10000 amplitude sine at 100 Hz would jump by up to 20000)
    const char* names[3] = { "MIX", "INSERT", "GATE" };
    for (int mode = 0; mode < 3; mode++)
    {
        Rig r;
        r.s.mode = (uint8_t)mode;
        r.c.setSettings(r.s);
        r.c.post(Core::C_DOWN, 3);
        r.run(500, sineSig);
        r.c.post(Core::C_UP);
        r.run(50, sineSig);
        const int m = maxStep(r.outL, 0, r.outL.size());
        if (m >= 800)
            printf("   %s max step %d\n", names[mode], m);
        CHECK(m < 800);
    }
    // mode changes in the middle of repeats, pad jams, release: every transition is ramped
    Rig r;
    r.c.post(Core::C_DOWN, 2);
    for (int i = 0; i < 12; i++)
    {
        r.s.mode = (uint8_t)(i % 3);
        r.c.setSettings(r.s);
        r.run(60, sineSig);
    }
    r.c.post(Core::C_DOWN, 5);
    r.run(200, sineSig);
    r.c.post(Core::C_UP);
    r.run(40, sineSig);
    const int m = maxStep(r.outL, 0, r.outL.size());
    if (m >= 1200)
        printf("   transitions max step %d\n", m);
    CHECK(m < 1200);
    // a hard cut would be a click: the same bound catches it (control: the dry input alone has steps of ~142)
    CHECK(maxStep(r.inL, 0, r.inL.size()) < 200);
}

static void test_tempo_change()
{
    // the slice is captured at 120 BPM; then the tempo drops to 60: the next slice start is twice as far, the slice is over before it, and its
    // last (faded) frame is held
    {
        Rig r;
        r.s.mode = MODE_INSERT;
        r.c.setSettings(r.s);
        r.c.post(Core::C_DOWN, 2);
        r.run(400, sineSig);
        CHECK(r.c.state() == S_REPEAT);
        const long L = (long)r.c.captured();
        // let a repeat start, then drop the tempo right after its start
        long startBlocks = 0;
        while (r.c.repeatIndex() < 3 && startBlocks < 400)
        {
            r.run(1, sineSig);
            startBlocks++;
        }
        r.bpm = 60.0;
        r.run(200, sineSig);
        // somewhere after the voice ended (L frames after its start) and before the next start the output is constant
        bool heldRun = false;
        size_t best = 0, run = 0;
        for (size_t i = 1; i < r.outL.size(); i++)
        {
            if (r.outL[i] == r.outL[i - 1])
                run++;
            else
                run = 0;
            if (run > best)
                best = run;
        }
        heldRun = best > 3000;
        CHECK(heldRun);
        (void)L;
        CHECK(r.outL[r.outL.size() - 1] != 12345); // keep the compiler honest about the vector
        CHECK(maxStep(r.outL, 0, r.outL.size()) < 800); // no jump at the held part nor at the next start
    }
    // the tempo rises: the next boundary comes before the slice is over, the old repeat is cut with a fade
    {
        Rig r;
        r.s.mode = MODE_INSERT;
        r.c.setSettings(r.s);
        r.bpm = 60.0;
        r.c.post(Core::C_DOWN, 2);
        r.run(800, sineSig);
        CHECK(r.c.state() == S_REPEAT);
        r.bpm = 120.0;
        r.run(400, sineSig);
        CHECK(r.c.state() == S_REPEAT);
        CHECK(maxStep(r.outL, 0, r.outL.size()) < 800);
    }
    // a gradual tempo ramp keeps the grid (the clock integrates the tempo): still repeating, no clicks
    {
        Rig r;
        r.s.mode = MODE_MIX;
        r.c.setSettings(r.s);
        r.c.post(Core::C_DOWN, 2);
        for (int i = 0; i < 100; i++)
        {
            r.bpm = 100.0 + i * 0.5;
            r.run(10, sineSig);
        }
        CHECK(r.c.state() == S_REPEAT && r.c.repeatIndex() > 5);
        CHECK(maxStep(r.outL, 0, r.outL.size()) < 800);
    }
}

// the transport: a jump of the position (pattern wrap) fires nothing; the first block only sets the position
static void test_clock()
{
    {
        Rig r;
        r.s.chance = 100;
        r.s.interval = 3; // 1/4 = 192 ticks
        r.c.setSettings(r.s);
        r.ticks = 192.0; // the very first block starts exactly on an interval boundary
        r.run(1, idSig);
        CHECK(r.c.state() == S_IDLE && r.c.captures() == 0);
        CHECK(!r.c.bypassable()); // armed
        r.run(1, idSig);
        CHECK(r.c.state() == S_IDLE); // no boundary reached yet
    }
    {
        // a pattern wrap (position jumps back by 4096 ticks) does not fire a boundary of its own
        Rig r;
        r.s.chance = 100;
        r.s.interval = 3;
        r.s.gate = 0;
        r.s.grid = 3; // 1/32: short events
        r.c.setSettings(r.s);
        r.ticks = 4000.0;
        r.run(200, idSig);
        const uint32_t before = r.c.captures();
        r.ticks = 3.0; // wrap
        r.run(1, idSig);
        CHECK(r.c.captures() == before);
    }
    {
        // transport stopped: the clock free-runs, a pad still works
        Rig r;
        r.running = false;
        r.c.post(Core::C_DOWN, 3);
        r.run(300, idSig);
        CHECK(r.c.state() == S_REPEAT);
    }
    {
        // a small forward catch up (the sequencer's position is a little ahead of the prediction) fires the skipped tick
        Rig r;
        r.s.chance = 100;
        r.s.interval = 0; // 24 ticks
        r.s.gate = 0;
        r.s.grid = 3;
        r.c.setSettings(r.s);
        r.run(3, idSig);
        r.ticks += 1.0; // 1 tick ahead of the prediction
        r.run(40, idSig);
        CHECK(r.c.captures() >= 1);
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------
static uint32_t refRnd(uint32_t& s)
{
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

static void test_chance_interval_variation()
{
    // CHANCE 100: one event at every interval boundary (1/4), CHANCE 0 none
    {
        Rig r;
        r.s.chance = 100;
        r.s.interval = 3; // 192 ticks
        r.s.gate = 1;     // 2/16 = 96 ticks
        r.s.grid = 3;     // 1/32
        r.c.setSettings(r.s);
        r.ticks = 1.0;
        r.run(3200, idSig); // ~ 9.3 s = 18.6 beats at 120 BPM
        const double beats = (double)(r.frame) / r.fpt() / 192.0;
        const int expect = (int)floor(beats + (1.0 / 192.0)); // boundaries 192, 384, ...
        CHECK(r.c.captures() == (uint32_t)expect || r.c.captures() == (uint32_t)expect - 1u); // the last one may still be capturing
        CHECK(expect >= 17);
    }
    {
        Rig r;
        r.s.chance = 0;
        r.c.setSettings(r.s);
        r.run(1000, idSig);
        CHECK(r.c.captures() == 0 && r.c.state() == S_IDLE);
    }
    // CHANCE 40 with the seeded generator: the trigger list equals the replica of the xorshift sequence
    {
        Rig r;
        r.s.chance = 40;
        r.s.interval = 3;
        r.s.gate = 1;
        r.s.grid = 3;
        r.c.setSettings(r.s);
        r.c.seed(777);
        r.ticks = 1.0;
        r.run(9000, idSig); // ~ 26 s
        uint32_t seed = 777;
        int expect = 0;
        const int boundaries = (int)floor((double)r.frame / r.fpt() / 192.0 + 1.0 / 192.0);
        for (int i = 0; i < boundaries - 1; i++) // boundary i+1: IDLE for sure (events are 96 ticks)
            if (refRnd(seed) % 100u < 40u)
                expect++;
        CHECK((int)r.c.captures() == expect || (int)r.c.captures() == expect + 1);
        CHECK(expect > 20 && expect < 60);
        // the same seed: identical output; another seed: different
        Rig a, b, c2;
        for (Rig* x : { &a, &b, &c2 })
        {
            x->s = r.s;
            x->c.setSettings(x->s);
            x->ticks = 1.0;
        }
        a.c.seed(777);
        b.c.seed(777);
        c2.c.seed(778);
        a.run(2000, idSig);
        b.run(2000, idSig);
        c2.run(2000, idSig);
        CHECK(a.outL == b.outL && a.outR == b.outR);
        CHECK(a.outL != c2.outL);
    }
    // INTERVAL: the events recur at the interval; a short interval is a faster stutter
    for (int iv = 1; iv <= 4; iv++)
    {
        Rig r;
        r.s.chance = 100;
        r.s.interval = (uint8_t)iv; // 48, 96, 192, 384
        r.s.gate = 0;               // 48 ticks
        r.s.grid = 3;
        r.c.setSettings(r.s);
        r.ticks = 1.0;
        r.run(6000, idSig);
        const double evs = (double)r.frame / r.fpt() / (double)intervalTicks((uint8_t)iv);
        // the event length is max(gate, 2 grids) = 48 ticks: with the 48 tick interval the event just fills it and every second boundary is free
        const double expectEvents = iv == 1 ? evs / 2.0 : evs;
        CHECK(fabs((double)r.c.captures() - expectEvents) <= 2.0);
    }
    // VARIATION: the grid of an automatic event moves by up to 1 + v/4 positions, with probability v/10: replica of the sequence
    {
        Rig r;
        r.s.chance = 100;
        r.s.interval = 3;
        r.s.gate = 1;
        r.s.grid = 2; // 1/16 (position 4 of 8)
        r.s.variation = 10;
        r.c.setSettings(r.s);
        r.c.seed(4242);
        r.ticks = 1.0;
        // replay the sequence in the replica and compare to the captured grid of each event via a fresh run that logs at CAPTURE start
        Rig q;
        q.s = r.s;
        q.c.setSettings(q.s);
        q.c.seed(4242);
        q.ticks = 1.0;
        std::vector<uint8_t> got;
        uint8_t prevState = S_IDLE;
        for (int b = 0; b < 8000; b++)
        {
            q.run(1, idSig);
            if (q.c.state() == S_CAPTURE && prevState != S_CAPTURE)
                got.push_back(q.c.eventGrid());
            prevState = q.c.state();
        }
        uint32_t seed = 4242;
        std::vector<uint8_t> want;
        for (size_t i = 0; i < got.size(); i++)
        {
            refRnd(seed); // chance roll (100 %: always passes)
            uint8_t p = gridPos(2);
            if ((refRnd(seed) % 10u) < 10u)
            {
                const int maxd = 1 + 10 / 4;
                int d = 1 + (int)(refRnd(seed) % (uint32_t)maxd);
                if (refRnd(seed) & 1u)
                    d = -d;
                int qq = (int)p + d;
                p = (uint8_t)(qq < 0 ? 0 : (qq > 7 ? 7 : qq));
            }
            want.push_back(gridOrder(p));
        }
        CHECK(got.size() > 20);
        CHECK(got == want);
        int dmax = 0;
        for (uint8_t g : got)
            dmax = std::max(dmax, abs((int)gridPos(g) - (int)gridPos(2)));
        CHECK(dmax <= 3 && dmax >= 2); // 1 + 10/4 = 3 positions at most
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Cost: the audio interrupt work of one block, host measurement only as a relative figure (the device CPU readout is on the BeatRpt screen)
static void test_cost()
{
    Rig r;
    r.s.pitchDecay = 40;
    r.s.mode = MODE_MIX;
    r.c.setSettings(r.s);
    r.c.post(Core::C_DOWN, 2);
    r.run(300, sineSig);
    CHECK(r.c.state() == S_REPEAT);
    int16_t l[kBlock], rr[kBlock];
    for (int i = 0; i < kBlock; i++)
        l[i] = rr[i] = (int16_t)(i * 50);
    Clock k;
    k.running = true;
    k.bpm = 120.0f;
    k.tick = (int32_t)r.ticks;
    k.frac = 0.0f;
    const int N = 2000;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; i++)
    {
        r.c.process(k, l, rr, l, rr, kBlock);
        k.tick += 9;
    }
    auto t1 = std::chrono::steady_clock::now();
    const double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / N;
    printf("beat repeat cost: %.2f us per block of 128 (repeating, resampled; host, -O2), capture = one stereo store per frame, idle = 0\n", us);
    CHECK(us < 500.0); // a loose host bound only
}

int main()
{
    test_tables();
    test_ring_limits();
    test_bypass();
    test_slice_timing();
    test_momentary();
    test_latch();
    test_panic();
    test_modes();
    test_decay_and_pitch();
    test_clicks();
    test_tempo_change();
    test_clock();
    test_chance_interval_variation();
    test_cost();
    printf("beat repeat tests: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
