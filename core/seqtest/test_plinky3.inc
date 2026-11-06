// Sequencer tracks mod - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Plan 019 A tests (included by test_plinky.cpp): three instances, shared voice pool, CPU governor, send outputs, old-sound identity,
// plinky.bin with three instances (old files, old readers, migration), Shift multi-edit values, the instrument order mapping.
#include "seqInstOrderLogic.h"
#include "seqPlinkyUiLogic.h"
#include "plinky_governor.h"

// ---------------------------------------------------------------- old sound identity (golden hashes of the v0.3.8 engine)
// The scenario ran on the v0.3.8 engine (git a1351a4 plinky_voice.h) with the Voices limit fixed for the whole run; the hash covers every
// output sample. The engine with three instances and a pool renders instance 0 bit for bit the same.
static uint64_t identityHash(int voices)
{
    Engine e;
    uint64_t h = 1469598103934665603ull;
    int16_t l[kBlock], r[kBlock];
    uint32_t rs = 12345;
    auto rnd = [&]() { rs = rs * 1664525u + 1013904223u; return rs >> 8; };
    const int shapes[] = { 0, 40, 130, 101 };
    for (int blk = 0; blk < 3000; blk++)
    {
        if (blk % 300 == 0)
        {
            e.setParam(P_SHAPE, shapes[(blk / 300) % 4]);
            e.setParam(P_WIDTH, (blk / 300) % 3 == 0 ? 0 : 60);
            e.setParam(P_VOICES, voices);
            e.setParam(P_CUTOFF, 20 + (int)(rnd() % 80));
            e.setParam(P_LFO_DEPTH, (int)(rnd() % 100));
            e.setParam(P_LFO_DEST, (int)(rnd() % 3));
            e.setParam(P_NOISE, (int)(rnd() % 50));
        }
        if (blk % 17 == 3)
            e.noteOn(36 + rnd() % 48, 40 + rnd() % 80);
        if (blk % 23 == 5)
            e.noteOff(36 + rnd() % 48);
        if (blk % 211 == 0)
            e.allNotesOff();
        if (blk % 40 == 9)
            e.controlChange(74, rnd() % 128);
        if (e.render(l, r))
            for (int i = 0; i < kBlock; i++)
            {
                h = (h ^ (uint16_t)l[i]) * 1099511628211ull;
                h = (h ^ (uint16_t)r[i]) * 1099511628211ull;
            }
    }
    return h;
}

static void test_x3_identity()
{
    CHECK(identityHash(1) == 0x1ea2306752d91cf9ull);
    CHECK(identityHash(2) == 0xd261f4f635f2aa3eull);
    CHECK(identityHash(3) == 0x4eb220184f6a58e2ull);
    CHECK(identityHash(4) == 0x674b7f127b0eb286ull);
}

// ---------------------------------------------------------------- instances
static void test_x3_instances()
{
    Engine e;
    CHECK(kInstances == 3 && kPool == 8 && kMaxVoices == 4);
    CHECK(e.param(P_CHANNEL, 0) == 16 && e.param(P_CHANNEL, 1) == 15 && e.param(P_CHANNEL, 2) == 14);
    CHECK(defaultChannel(0) == 16 && defaultChannel(1) == 15 && defaultChannel(2) == 14);
    // own parameters
    e.setParam(P_CUTOFF, 11, 1);
    e.setParam(P_SHAPE, 150, 2);
    CHECK(e.param(P_CUTOFF, 0) == paramInfo(P_CUTOFF).def && e.param(P_CUTOFF, 1) == 11 && e.param(P_CUTOFF, 2) == paramInfo(P_CUTOFF).def);
    CHECK(e.param(P_SHAPE, 2) == 150 && e.param(P_SHAPE, 0) == 0);
    e.setParam(P_CUTOFF, 5, 7); // a bad instance changes nothing
    e.setParam(P_CUTOFF, 5, -1);
    CHECK(e.param(P_CUTOFF, 0) == paramInfo(P_CUTOFF).def);
    // a note of instance 1 sounds with instance 1's parameters only; the other instances stay idle
    Engine a, b;
    a.setParam(P_CUTOFF, 30, 1);
    b.setParam(P_CUTOFF, 90, 1);
    a.noteOn(60, 100, 1);
    b.noteOn(60, 100, 1);
    std::vector<int16_t> la, lb;
    run(a, 30, la);
    run(b, 30, lb);
    CHECK(a.activeVoices(1) == 1 && a.activeVoices(0) == 0 && a.activeVoices(2) == 0);
    double ea = 0, eb = 0;
    for (size_t i = 0; i < la.size(); i++)
    {
        ea += (double)la[i] * la[i];
        eb += (double)lb[i] * lb[i];
    }
    CHECK(ea > 0 && eb > ea * 1.05); // the brighter filter lets more through
    // instance 0 untouched by another instance's cutoff: the same notes on instance 0 sound identical
    Engine c, d;
    d.setParam(P_CUTOFF, 5, 2);
    c.noteOn(64, 90);
    d.noteOn(64, 90);
    std::vector<int16_t> lc, ld;
    run(c, 20, lc);
    run(d, 20, ld);
    CHECK(lc == ld);
    // note off / all off / CC / bend are per instance
    Engine f;
    f.noteOn(60, 100, 0);
    f.noteOn(60, 100, 2);
    std::vector<int16_t> t;
    run(f, 4, t);
    f.noteOff(60, 2);
    run(f, 1, t);
    int held0 = 0, held2 = 0;
    for (int i = 0; i < kPool; i++)
    {
        held0 += f.voice(i).stage != ST_IDLE && f.voice(i).inst == 0 && f.voice(i).gate;
        held2 += f.voice(i).stage != ST_IDLE && f.voice(i).inst == 2 && f.voice(i).gate;
    }
    CHECK(held0 == 1 && held2 == 0);
    f.controlChange(74, 0, 1); // instance 1 only
    run(f, 1, t);
    CHECK(f.effective(P_CUTOFF, 1) == 0 && f.effective(P_CUTOFF, 0) == paramInfo(P_CUTOFF).def && f.param(P_CUTOFF, 1) == paramInfo(P_CUTOFF).def);
    f.allNotesOff(0);
    run(f, 1, t);
    held0 = 0;
    for (int i = 0; i < kPool; i++)
        held0 += f.voice(i).stage != ST_IDLE && f.voice(i).gate;
    CHECK(held0 == 0);
    f.noteOn(60, 100, 1);
    f.noteOn(62, 100, 2);
    run(f, 3, t);
    f.allNotesOff(-1);
    run(f, 2, t);
    held0 = 0;
    for (int i = 0; i < kPool; i++)
        held0 += f.voice(i).gate;
    CHECK(held0 == 0);
    // the trace ring tells the instance of every sample (bits 1 and 2 of `on`)
    Engine g;
    g.noteOn(60, 100, 2);
    std::vector<int16_t> t2;
    run(g, 6, t2);
    bool seen = false;
    for (int i = 0; i < kPool; i++)
    {
        const TraceSample& s = g.traceRing(i)[(g.traceHeadIndex() - 1) & (kTraceLen - 1)];
        if (s.on)
        {
            seen = true;
            CHECK((s.on & 1) == 1 && (s.on >> 1) == 2);
        }
    }
    CHECK(seen);
}

// ---------------------------------------------------------------- the shared pool
static void test_x3_pool()
{
    // 8 voices: two instances with four each
    {
        Engine e;
        for (int n = 0; n < 4; n++)
        {
            e.noteOn(48 + n * 4, 100, 0);
            e.noteOn(60 + n * 4, 100, 1);
        }
        std::vector<int16_t> t;
        run(e, 4, t);
        CHECK(e.activeVoices() == 8 && e.activeVoices(0) == 4 && e.activeVoices(1) == 4);
        // a ninth note of instance 2 takes a voice from the pool (the oldest note of anyone), the total stays 8
        e.noteOn(72, 100, 2);
        run(e, 4, t);
        CHECK(e.activeVoices() == 8 && e.activeVoices(2) == 1);
        CHECK(e.activeVoices(0) + e.activeVoices(1) == 7);
    }
    // the Voices limit of an instance: at most that many notes held, the oldest is taken
    {
        Engine e;
        e.setParam(P_VOICES, 2, 1);
        for (int n = 0; n < 4; n++)
            e.noteOn(50 + n, 100, 1);
        std::vector<int16_t> t;
        run(e, 4, t);
        CHECK(e.activeVoices(1) == 2);
        int has[128] = { 0 };
        for (int i = 0; i < kPool; i++)
            if (e.voice(i).stage != ST_IDLE)
                has[e.voice(i).note]++;
        CHECK(has[52] == 1 && has[53] == 1 && has[50] == 0 && has[51] == 0); // the newest two
        // other instances keep their own room
        for (int n = 0; n < 4; n++)
            e.noteOn(70 + n, 100, 0);
        run(e, 4, t);
        CHECK(e.activeVoices(0) == 4 && e.activeVoices(1) == 2);
    }
    // the same note again re-uses its voice (legato), per instance
    {
        Engine e;
        e.noteOn(60, 100, 0);
        e.noteOn(60, 100, 1);
        std::vector<int16_t> t;
        run(e, 3, t);
        e.noteOn(60, 90, 0);
        e.noteOn(60, 90, 1);
        run(e, 3, t);
        CHECK(e.activeVoices(0) == 1 && e.activeVoices(1) == 1);
    }
    // a lowered Voices limit releases the extra voices (they ring out, nothing is cut)
    {
        Engine e;
        for (int n = 0; n < 4; n++)
            e.noteOn(50 + n * 3, 100, 2);
        std::vector<int16_t> t;
        run(e, 4, t);
        e.setParam(P_VOICES, 1, 2);
        run(e, 2, t);
        int held = 0;
        for (int i = 0; i < kPool; i++)
            held += e.voice(i).stage != ST_IDLE && e.voice(i).inst == 2 && e.voice(i).gate;
        CHECK(held == 1);
    }
    // 6000 random events over the three instances: always bounded, never more than the pool, per-instance held voices within the limit
    {
        Engine e;
        uint32_t rs = 777;
        auto rnd = [&]() { rs = rs * 1664525u + 1013904223u; return rs >> 8; };
        e.setParam(P_VOICES, 3, 0);
        e.setParam(P_VOICES, 2, 1);
        int16_t l[kBlock], r[kBlock];
        bool ok = true;
        for (int i = 0; i < 6000; i++)
        {
            const int inst = (int)(rnd() % 3);
            switch (rnd() % 6)
            {
                case 0:
                case 1:
                case 2: e.noteOn(36 + rnd() % 60, 30 + rnd() % 90, inst); break;
                case 3: e.noteOff(36 + rnd() % 60, inst); break;
                case 4: if (rnd() % 20 == 0) e.allNotesOff(inst); break;
                default: e.setParam(P_VOICES, 1 + rnd() % 4, inst); break;
            }
            if (e.render(l, r))
                for (int k = 0; k < kBlock; k++)
                    ok = ok && l[k] == l[k] && r[k] == r[k];
            ok = ok && e.activeVoices() <= kPool;
            for (int p = 0; p < kPool; p++)
                ok = ok && voiceFinite(e.voice(p)) && e.voice(p).inst < kInstances;
        }
        CHECK(ok);
    }
    // all three instances at full stretch: bounded output
    {
        Engine e;
        for (int k = 0; k < 3; k++)
        {
            e.setParam(P_LEVEL, 100, k);
            e.setParam(P_DRIVE, 100, k);
            e.setParam(P_RESO, 100, k);
            e.setParam(P_NOISE, 100, k);
            for (int n = 0; n < 4; n++)
                e.noteOn(30 + k * 12 + n * 2, 127, k);
        }
        std::vector<int16_t> t;
        run(e, 400, t);
        bool bounded = true;
        for (int16_t s : t)
            bounded = bounded && s >= -32768 && s <= 32767;
        CHECK(bounded);
        CHECK(e.activeVoices() <= kPool);
    }
}

// ---------------------------------------------------------------- send outputs
static void test_x3_sends()
{
    Engine e;
    e.setParam(P_REVERB, 100, 1);
    e.setParam(P_DELAY, 50, 2);
    CHECK(e.wantSend(false) && e.wantSend(true));
    e.noteOn(60, 100, 1);
    e.noteOn(64, 100, 2);
    int16_t d[2][kBlock], rv[2][kBlock], dl[2][kBlock];
    Out o = { d[0], d[1], rv[0], rv[1], dl[0], dl[1] };
    double eDry = 0, eRev = 0, eDly = 0;
    for (int b = 0; b < 40; b++)
    {
        CHECK(e.render(o));
        for (int i = 0; i < kBlock; i++)
        {
            eDry += (double)d[0][i] * d[0][i];
            eRev += (double)rv[0][i] * rv[0][i];
            eDly += (double)dl[0][i] * dl[0][i];
        }
    }
    CHECK(eDry > 0 && eRev > 0 && eDly > 0);
    // instance 1's send level is 0.5 (half on each of the two mixer channels): the reverb bus is about half of that instance's dry level
    // and does not contain instance 2 (reverb 0)
    Engine s1;
    s1.setParam(P_REVERB, 100, 1);
    s1.noteOn(60, 100, 1);
    double d1 = 0, r1 = 0;
    for (int b = 0; b < 30; b++)
    {
        int16_t a[2][kBlock], c[2][kBlock];
        Out oo = { a[0], a[1], c[0], c[1], nullptr, nullptr };
        s1.render(oo);
        for (int i = 0; i < kBlock; i++)
        {
            d1 += (double)a[0][i] * a[0][i];
            r1 += (double)c[0][i] * c[0][i];
        }
    }
    CHECK_NEAR(sqrt(r1 / d1), 0.5, 0.01);
    // no send level anywhere: nothing wanted, the send pointers may be null
    Engine n;
    CHECK(!n.wantSend(false) && !n.wantSend(true));
    n.noteOn(60, 100, 0);
    int16_t x[kBlock], y[kBlock];
    CHECK(n.render(x, y));
    // the dry output of one instance does not depend on the send arguments
    Engine p, q;
    p.setParam(P_REVERB, 80, 0);
    q.setParam(P_REVERB, 80, 0);
    p.noteOn(55, 100);
    q.noteOn(55, 100);
    std::vector<int16_t> lp, lq;
    for (int b = 0; b < 20; b++)
    {
        int16_t a[2][kBlock], c[2][kBlock], z[2][kBlock];
        Out o1 = { a[0], a[1], c[0], c[1], nullptr, nullptr };
        p.render(o1);
        q.render(z[0], z[1]);
        lp.insert(lp.end(), a[0], a[0] + kBlock);
        lq.insert(lq.end(), z[0], z[0] + kBlock);
    }
    CHECK(lp == lq);
}

// ---------------------------------------------------------------- governor (plan 022 P2: voices must come back)
// A plant: the synth wants `want` voices, the load of a block is the background (MLR stream, reverb, ...) plus vc per sounding voice.
struct GovPlant
{
    Governor g;
    uint32_t base, vc;
    int want;
    GovPlant(uint32_t b, uint32_t v, int w) : base(b), vc(v), want(w) {}
    int step(uint32_t spike = 0)
    {
        const int sounding = want < g.cap ? want : g.cap;
        const uint32_t load = spike ? spike : base + vc * (uint32_t)sounding;
        return g.update(load, sounding, vc);
    }
};
static void test_x3_governor_recovery()
{
    // spike then recovery: an SD / stream catch-up pushes a few blocks over the limit, the load is back to normal right after. The cap
    // drops (the dropout was real) and returns, one voice per 0.25 s (v0.4.3; was 0.67 s), to all 8 within 8 s - and never goes up and
    // down while it does.
    {
        GovPlant p(400, 30, 8);
        for (int i = 0; i < 1000; i++)
            p.step();
        CHECK(p.g.cap == 8);
        for (int i = 0; i < 3; i++)
            p.step(1200);
        CHECK(p.g.cap < 8 && p.g.cap >= 6); // one cut, the hold-off keeps the spike from cutting again
        int prev = p.g.cap, ups = 0, downs = 0, blocks = 0;
        while (p.g.cap < 8 && blocks < 8 * 345)
        {
            p.step();
            blocks++;
            if (p.g.cap > prev) ups++;
            if (p.g.cap < prev) downs++;
            prev = p.g.cap;
        }
        CHECK(p.g.cap == 8 && downs == 0 && ups >= 1 && blocks < 8 * 345);
        // one voice per kRestoreBlocks: not faster than kRestoreBlocks, not slower than 1 s each
        const int missing = 8 - 7;
        CHECK(blocks >= Governor::kRestoreBlocks * missing && blocks <= 345 * missing + 100);
    }
    // a spike while 4 voices sound: the average is not poisoned (clamped), recovery is as fast
    {
        GovPlant p(300, 30, 4);
        for (int i = 0; i < 500; i++)
            p.step();
        for (int i = 0; i < 5; i++)
            p.step(60000); // absurd reading (a stalled timer): clamped in the average
        CHECK(p.g.avg >> 4 < 800);
    }
    // sustained moderate load (an MLR stream at 45 %, 30 per voice): the cap settles at the number of voices that fit, stays there, and the
    // load stays under the cut threshold. 45 % + 8 x 3 % = 69 %: all 8 fit.
    {
        GovPlant p(450, 30, 8);
        for (int i = 0; i < 5000; i++)
            p.step();
        CHECK(p.g.cap == 8);
    }
    // 62 % background + 3 % per voice: 62 + 24 = 86 %. v0.4.3 (owner: act only on a real overrun risk): a high but safe average never
    // cuts (before: >= 80 % cut down to 5..7 voices); all 8 stay and nothing changes
    {
        GovPlant p(620, 30, 8);
        for (int i = 0; i < 4000; i++)
            p.step();
        const int settled = p.g.cap;
        CHECK(settled == 8);
        int changes = 0;
        for (int i = 0; i < 6000; i++)
        {
            const int before = p.g.cap;
            p.step();
            if (p.g.cap != before) changes++;
        }
        CHECK(changes == 0 && p.g.cap == settled);
    }
    // never stuck at 0: a background load at the limit cuts down to the minimum, not to nothing, and when the background goes away the
    // voices come back (7 steps of 0.25 s)
    {
        GovPlant p(960, 30, 8);
        for (int i = 0; i < 3000; i++)
            p.step();
        CHECK(p.g.cap >= Governor::kMinCap && p.g.cap <= Governor::kRiskFloor);
        p.base = 200;
        int blocks = 0;
        while (p.g.cap < 8 && blocks < 20 * 345)
        {
            p.step();
            blocks++;
        }
        CHECK(p.g.cap == 8 && blocks < 8 * 345);
    }
    // an overrun with a single sounding voice cannot take the last voice
    {
        Governor g;
        g.cap = 1;
        for (int i = 0; i < 400; i++)
            g.update(1050, 1);
        CHECK(g.cap == 1);
    }
    // the voice cost from the synth counts: expensive voices (10 % each) need more headroom than cheap ones (1 %)
    {
        Governor cheap, dear;
        cheap.cap = dear.cap = 4;
        for (int i = 0; i < 2000; i++)
        {
            cheap.update(650, 4, 10);
            dear.update(650, 4, 100);
        }
        CHECK(cheap.cap == 8 && dear.cap < 8 && dear.cap >= 4);
    }
}

static void test_x3_governor()
{
    Governor g;
    CHECK(g.cap == 8);
    // a quiet system keeps all voices
    for (int i = 0; i < 2000; i++)
        g.update(400, 8);
    CHECK(g.cap == 8);
    // one overrun block: cap = sounding - 1, then 250 ms of hold-off (86 blocks) without another cut
    CHECK(g.update(1050, 8) == 7);
    for (int i = 0; i < 80; i++)
        CHECK(g.update(1100, 7) == 7);
    // after the hold-off the next overrun cuts again
    for (int i = 0; i < 10; i++)
        g.update(400, 7);
    CHECK(g.update(1010, 7) == 6);
    // restore (plan 022 P2: was 2 s / 690 blocks of average < 65 %; v0.4.3: 0.25 s, was 0.67 s): kRestoreBlocks of quiet blocks (< 93 %)
    // with room for one more voice (predicted average < 90 %) give one voice back
    Governor h;
    h.update(1100, 8);
    CHECK(h.cap == 7);
    int blocks = 0;
    while (h.cap < 8 && blocks < 3000)
    {
        h.update(500, 7);
        blocks++;
    }
    CHECK(h.cap == 8 && blocks >= Governor::kRestoreBlocks && blocks < Governor::kRestoreBlocks + 60);
    // a block near the limit (>= 96 %) breaks the stretch: the count starts again. v0.4.3: below the 97 % risk line it cuts nothing (before,
    // within a second of the first overrun, a 96 % block was the second risk block and took one more voice)
    Governor k;
    k.update(1100, 8);
    for (int i = 0; i < Governor::kRestoreBlocks - 30; i++)
        k.update(500, 7);
    k.update(960, 7);
    CHECK(k.cap == 7);
    for (int i = 0; i < Governor::kRestoreBlocks - 30; i++)
        k.update(500, 7);
    CHECK(k.cap == 7); // without the restart the voice would be back by now
    for (int i = 0; i < 30; i++)
        k.update(500, 7);
    CHECK(k.cap == 8);
    // a block at 94 % (v0.4.3: was 91 %, now a quiet block) delays the count by a quarter of it, it does not reset it
    Governor k2;
    k2.update(1100, 8);
    for (int i = 0; i < Governor::kRestoreBlocks - 30; i++)
        k2.update(500, 7);
    k2.update(945, 7);
    CHECK(k2.cap == 7);
    for (int i = 0; i < 120; i++)
        k2.update(500, 7);
    CHECK(k2.cap == 8);
    // the risk rule (v0.4.3): a block >= 97 % cuts one voice, but never below the floor; blocks at 96 % are safe (before: two blocks >= 95 %
    // within a second cut one voice)
    Governor r;
    r.update(960, 8);
    CHECK(r.cap == 8);
    r.update(960, 8);
    CHECK(r.cap == 8);
    r.update(975, 8);
    CHECK(r.cap == 7);
    Governor fl;
    for (int i = 0; i < 4000; i++)
        fl.update(990, fl.cap);
    CHECK(fl.cap >= Governor::kRiskFloor || fl.cap == 0);
    // v0.4.3 (owner): a high but safe average never cuts (before: >= 88 % cut slowly, down to nothing); 96 % sustained is still safe
    Governor hi;
    for (int i = 0; i < 2000; i++)
        hi.update(890, hi.cap);
    CHECK(hi.cap == 8);
    for (int i = 0; i < 2000; i++)
        hi.update(960, hi.cap);
    CHECK(hi.cap == 8);
    // idle blocks never cut
    Governor id;
    for (int i = 0; i < 1000; i++)
        id.update(1100, 0);
    CHECK(id.cap == 8);
    id.reset();
    CHECK(id.cap == 8 && id.hold == 0);
    CHECK(id.update(0, 5) == 8);

    // engine integration: 8 voices sound, the measured load is 110 %: voices fade out one per hold-off, the sound never jumps
    Engine e;
    for (int k2 = 0; k2 < 2; k2++)
        for (int n = 0; n < 4; n++)
            e.noteOn(48 + k2 * 12 + n * 3, 110, k2);
    int16_t l[kBlock], rr[kBlock];
    std::vector<int16_t> trace;
    for (int b = 0; b < 40; b++)
    {
        e.setBlockLoad(500);
        e.render(l, rr);
        trace.insert(trace.end(), l, l + kBlock);
    }
    CHECK(e.activeVoices() == 8 && e.voiceCap() == 8);
    for (int b = 0; b < 400; b++)
    {
        e.setBlockLoad(1100);
        if (e.render(l, rr))
            trace.insert(trace.end(), l, l + kBlock);
    }
    CHECK(e.voiceCap() < 8);
    CHECK(e.activeVoices() <= e.voiceCap());
    CHECK(e.activeVoices() >= 1);
    // no click: the largest step between neighbouring samples stays far below a hard cut of a full voice (about the steady peak)
    int steady = 0, maxStep = 0;
    for (size_t i = 1; i < trace.size(); i++)
    {
        const int a = trace[i] - trace[i - 1];
        const int mag = a < 0 ? -a : a;
        if (mag > maxStep)
            maxStep = mag;
    }
    for (size_t i = 0; i < 40 * kBlock; i++)
        steady = steady < abs(trace[i]) ? abs(trace[i]) : steady;
    CHECK(maxStep < steady); // a cut voice fades over a block
    // back to a quiet system: the cap returns
    for (int b = 0; b < 9000; b++)
    {
        e.setBlockLoad(300);
        e.render(l, rr);
    }
    CHECK(e.voiceCap() == 8);
    // new notes respect the cap
    Engine c;
    for (int b = 0; b < 200; b++)
    {
        c.setBlockLoad(1100);
        c.render(l, rr);
    }
    CHECK(c.voiceCap() == 8); // nothing sounds: the governor leaves the cap alone
    for (int k2 = 0; k2 < 3; k2++)
        for (int n = 0; n < 3; n++)
            c.noteOn(40 + k2 * 10 + n, 100, k2);
    for (int b = 0; b < 700; b++)
    {
        c.setBlockLoad(1050);
        c.render(l, rr);
        CHECK(c.activeVoices() <= (c.voiceCap() > 0 ? 9 : 9));
    }
    CHECK(c.voiceCap() < 8);
    c.noteOn(90, 100, 0);
    c.noteOn(91, 100, 1);
    c.noteOn(92, 100, 2);
    c.setBlockLoad(1050);
    c.render(l, rr);
    CHECK(c.activeVoices() <= c.voiceCap() + 1); // a new note steals instead of adding (the next block enforces the cap)
    c.setBlockLoad(1050);
    c.render(l, rr);
    CHECK(c.activeVoices() <= c.voiceCap());
}

// ---------------------------------------------------------------- plinky.bin with three instances
static void fillSnap(plinkymorph::Store& st, int pat, int seed)
{
    uint8_t v[P_COUNT];
    for (int i = 0; i < P_COUNT; i++)
        v[i] = clampParam(i, paramInfo(i).def + seed + i);
    st.set(pat, v);
}
static void fillSnapLegacy(plinkymorph::Store& st, int pat, int seed)
{
    uint8_t v[P_COUNT];
    for (int i = 0; i < P_COUNT; i++)
        v[i] = i < kLegacyCount ? clampParam(i, paramInfo(i).def + seed + i) : paramInfo(i).def;
    st.set(pat, v);
}

static void test_x3_file()
{
    using namespace plinkymorph;
    Params cur[3];
    Store s0, s1, s2;
    Store* sp[3] = { &s0, &s1, &s2 };
    const Store* cp[3] = { &s0, &s1, &s2 };
    uint8_t hs[3] = { 12, 7, 33 };
    for (int k = 0; k < 3; k++)
    {
        cur[k] = instDefaults(k);
        for (int i = 1; i < (k == 0 ? kLegacyCount : P_COUNT); i++) // instance 0 first without the newer parameters (its PLVI frame is tested below)
            cur[k].v[i] = clampParam(i, paramInfo(i).def + 3 * k + i % 5);
    }
    fillSnapLegacy(s0, 4, 2);
    fillSnap(s1, 9, 5);
    fillSnap(s1, 200, 1);
    fillSnap(s2, 255, 3);
    static uint8_t buf[kFileMaxBank];
    const size_t n = saveBank(buf, sizeof(buf), cur, cp, hs);
    CHECK(n > 0 && n < sizeof(buf));

    Params lc[3];
    Store t0, t1, t2;
    Store* tp[3] = { &t0, &t1, &t2 };
    uint8_t lh[3] = { 0, 0, 0 };
    bool newer = true;
    CHECK(loadBank(buf, n, lc, tp, lh, &newer) && !newer);
    for (int k = 0; k < 3; k++)
    {
        CHECK(lc[k] == cur[k]);
        CHECK(lh[k] == hs[k]);
    }
    CHECK(t0.count() == 1 && t1.count() == 2 && t2.count() == 1);
    CHECK(t0.has(4) && t1.has(9) && t1.has(200) && t2.has(255) && !t2.has(4));
    for (int i = 1; i < P_COUNT; i++)
    {
        CHECK(t1.v[9][i] == s1.v[9][i] && t1.v[200][i] == s1.v[200][i] && t2.v[255][i] == s2.v[255][i] && t0.v[4][i] == s0.v[4][i]);
    }
    // an older mod reads instance 0 exactly from the same bytes: the version 1 frame and the PLVM trailer
    {
        Params old;
        CHECK(old.load(buf, n));
        CHECK(old == cur[0]);
        Params ob;
        Store os;
        uint8_t oh = 0;
        bool nw = true;
        CHECK(loadFile(buf, n, ob, os, oh, &nw) && !nw && ob == cur[0] && oh == hs[0] && os.count() == 1 && os.has(4));
    }
    // a v0.3.8 file (frame 1 only, or with a trailer) has no PLVI frames: instances 1 and 2 are the defaults (channels 15 and 14)
    {
        Params only = instDefaults(0);
        only.v[P_CUTOFF] = 33;
        Store emp;
        uint8_t b2[256];
        const size_t m = only.save(b2, sizeof(b2));
        Params l2[3];
        Store u0, u1, u2;
        Store* up[3] = { &u0, &u1, &u2 };
        uint8_t h2[3];
        CHECK(loadBank(b2, m, l2, up, h2));
        CHECK(l2[0].v[P_CUTOFF] == 33 && l2[0].v[P_CHANNEL] == 16);
        CHECK(l2[1] == instDefaults(1) && l2[2] == instDefaults(2) && l2[1].v[P_CHANNEL] == 15 && l2[2].v[P_CHANNEL] == 14);
        CHECK(h2[1] == kHalfSecDefault && u1.count() == 0);
        // nothing to say = the file is byte-identical to the old one
        Params dflt[3] = { instDefaults(0), instDefaults(1), instDefaults(2) };
        Store e0, e1, e2;
        const Store* ep[3] = { &e0, &e1, &e2 };
        uint8_t eh[3] = { kHalfSecDefault, kHalfSecDefault, kHalfSecDefault };
        uint8_t b3[kFileMaxBank], b4[256];
        const size_t m3 = saveBank(b3, sizeof(b3), dflt, ep, eh);
        const size_t m4 = dflt[0].save(b4, sizeof(b4));
        CHECK(m3 == m4 && memcmp(b3, b4, m3) == 0);
        // only instance 0 changed: still the plain old file for an old reader, plus nothing else
        Params one[3] = { instDefaults(0), instDefaults(1), instDefaults(2) };
        one[0].v[P_CUTOFF] = 12;
        const size_t m5 = saveBank(b3, sizeof(b3), one, ep, eh);
        const size_t m6 = one[0].save(b4, sizeof(b4));
        CHECK(m5 == m6 && memcmp(b3, b4, m5) == 0);
    }
    // migration of a v0.3 file that claimed another channel: instance 0 keeps it, the others take distinct free ones
    {
        for (int claimed = 1; claimed <= 16; claimed++)
        {
            Params p0 = instDefaults(0);
            p0.v[P_CHANNEL] = (uint8_t)claimed;
            uint8_t b2[256];
            const size_t m = p0.save(b2, sizeof(b2));
            Params l2[3];
            Store u0, u1, u2;
            Store* up[3] = { &u0, &u1, &u2 };
            uint8_t h2[3];
            CHECK(loadBank(b2, m, l2, up, h2));
            CHECK(l2[0].v[P_CHANNEL] == claimed);
            CHECK(l2[1].v[P_CHANNEL] != l2[0].v[P_CHANNEL] && l2[2].v[P_CHANNEL] != l2[0].v[P_CHANNEL] && l2[1].v[P_CHANNEL] != l2[2].v[P_CHANNEL]);
            for (int k = 0; k < 3; k++)
                CHECK(l2[k].v[P_CHANNEL] >= 1 && l2[k].v[P_CHANNEL] <= 16);
        }
        // channel 0 (a v0.3 file with the voice off) becomes 16
        Params p0 = instDefaults(0);
        p0.v[P_CHANNEL] = 0;
        uint8_t b2[256];
        const size_t m = p0.save(b2, sizeof(b2));
        Params l2[3];
        Store u0, u1, u2;
        Store* up[3] = { &u0, &u1, &u2 };
        uint8_t h2[3];
        CHECK(loadBank(b2, m, l2, up, h2) && l2[0].v[P_CHANNEL] == 16 && l2[1].v[P_CHANNEL] == 15 && l2[2].v[P_CHANNEL] == 14);
    }
    // a corrupt PLVI frame is ignored (that instance keeps its defaults), the rest loads; a truncated file loads as far as it is whole
    {
        uint8_t b2[kFileMaxBank];
        memcpy(b2, buf, n);
        // find the first PLVI frame and flip a payload byte
        size_t f1 = seqsidecar::kHeader + ((size_t)b2[6] | ((size_t)b2[7] << 8)) + seqsidecar::kCrc;
        size_t p2 = f1; // PLVM trailer (instance 0 has a snapshot)
        p2 += seqsidecar::kHeader + ((size_t)b2[p2 + 6] | ((size_t)b2[p2 + 7] << 8)) + seqsidecar::kCrc;
        CHECK(memcmp(b2 + p2, kInstMagic, 4) == 0);
        b2[p2 + seqsidecar::kHeader + 5] ^= 0x55; // inside the payload: the CRC fails
        Params l2[3];
        Store u0, u1, u2;
        Store* up[3] = { &u0, &u1, &u2 };
        uint8_t h2[3];
        CHECK(loadBank(b2, n, l2, up, h2));
        CHECK(l2[0] == cur[0]);
        // the first PLVI frame belongs to instance 1 here (instance 0 has no extras): it fell back to the defaults, instance 2 is whole
        CHECK(l2[1] == instDefaults(1) && u1.count() == 0);
        CHECK(l2[2] == cur[2] && u2.count() == 1);
        // truncated in the middle of the last frame
        Params l3[3];
        Store v0, v1, v2;
        Store* vp[3] = { &v0, &v1, &v2 };
        uint8_t h3[3];
        CHECK(loadBank(buf, n - 7, l3, vp, h3));
        CHECK(l3[0] == cur[0] && l3[1] == cur[1] && l3[2] == instDefaults(2));
    }
    // a newer frame version sets the "newer" flag (the file is left alone); the known frames still load
    {
        uint8_t b2[kFileMaxBank];
        memcpy(b2, buf, n);
        size_t f1 = seqsidecar::kHeader + ((size_t)b2[6] | ((size_t)b2[7] << 8)) + seqsidecar::kCrc;
        size_t p2 = f1 + seqsidecar::kHeader + ((size_t)b2[f1 + 6] | ((size_t)b2[f1 + 7] << 8)) + seqsidecar::kCrc;
        const size_t len = seqsidecar::kHeader + ((size_t)b2[p2 + 6] | ((size_t)b2[p2 + 7] << 8)) + seqsidecar::kCrc;
        uint8_t pl[8192];
        memcpy(pl, b2 + p2 + seqsidecar::kHeader, len - seqsidecar::kOverhead);
        seqsidecar::frameWrite(b2 + p2, len, kInstMagic, kInstVersion + 1, pl, len - seqsidecar::kOverhead);
        Params l2[3];
        Store u0, u1, u2;
        Store* up[3] = { &u0, &u1, &u2 };
        uint8_t h2[3];
        bool nw = false;
        CHECK(loadBank(b2, n, l2, up, h2, &nw) && nw);
        CHECK(l2[0] == cur[0]);
    }
    // a frame with fewer parameters (an older writer of a longer list is impossible, a shorter one is what a build before extensions wrote)
    {
        uint8_t b2[512], pl[64];
        size_t o = 0;
        pl[o++] = 1;               // instance 1
        pl[o++] = kLegacyCount;    // 21 parameters
        for (int i = 0; i < kLegacyCount; i++)
            pl[o++] = i == P_CUTOFF ? 17 : paramInfo(i).def;
        pl[1 + 0] = kLegacyCount;
        pl[2 + P_CHANNEL] = 15;
        pl[o++] = 20;              // half seconds
        pl[o++] = 0;               // no snapshots
        Params p0 = instDefaults(0);
        uint8_t head[256];
        const size_t m = p0.save(head, sizeof(head));
        memcpy(b2, head, m);
        const size_t t = seqsidecar::frameWrite(b2 + m, sizeof(b2) - m, kInstMagic, kInstVersion, pl, o);
        Params l2[3];
        Store u0, u1, u2;
        Store* up[3] = { &u0, &u1, &u2 };
        uint8_t h2[3];
        CHECK(loadBank(b2, m + t, l2, up, h2));
        CHECK(l2[1].v[P_CUTOFF] == 17 && h2[1] == 20 && l2[1].v[P_CHANNEL] == 15);
    }
    (void)sp;
}

// ---------------------------------------------------------------- Shift multi-edit values
static void test_x3_multi_edit()
{
    using namespace seqplinky;
    // the same delta on every instance, each clamped on its own
    const int cur[3] = { 10, 50, 98 };
    int out[3];
    for (int k = 0; k < 3; k++)
        out[k] = allValue(P_CUTOFF, cur[k], 5);
    CHECK(out[0] == 15 && out[1] == 55 && out[2] == 100);
    for (int k = 0; k < 3; k++)
        out[k] = allValue(P_CUTOFF, cur[k], -20);
    CHECK(out[0] == 0 && out[1] == 30 && out[2] == 78);
    // the delta is what the focused instance moved: jog the shown one, add the difference to the others
    const int shown = 1;
    const int nv = jogValue(P_RESO, cur[shown], 1, false);
    const int delta = nv - cur[shown];
    CHECK(delta == 5);
    for (int k = 0; k < 3; k++)
        out[k] = allValue(P_RESO, cur[k], delta);
    CHECK(out[shown] == nv && out[0] == 15 && out[2] == 100);
    // stepped / short ranges and the morph time
    CHECK(allValue(P_VOICES, 4, 1) == 4 && allValue(P_VOICES, 1, -1) == 1 && allValue(P_VOICES, 2, 1) == 3);
    CHECK(allValue(P_INTERVAL, 12, 1) == 13 && allValue(P_INTERVAL, 24, 3) == 24);
    CHECK(allValue(P_MORPH, 58, 5) == plinkymorph::kHalfSecMax && allValue(P_MORPH, 1, -4) == 0);
    CHECK(allValue(P_SHAPE, 225, 5) == kShapeMax && allValue(P_SHAPE, 3, -5) == 0);
    // property: every result is in range for every parameter and delta
    bool ok = true;
    for (int p = 0; p < kUiParams; p++)
        for (int d = -60; d <= 60; d += 7)
            for (int c = uiInfo(p).min; c <= uiInfo(p).max; c += 13)
            {
                const int v = allValue(p, c, d);
                ok = ok && v >= uiInfo(p).min && v <= uiInfo(p).max;
                ok = ok && (d == 0 ? v == c : true);
            }
    CHECK(ok);
    // tabs and the ALL marker are in the model
    Model m;
    CHECK(m.inst == 0 && m.all == 0);
    CHECK(tabColor(0) == seqpal::kNote && tabColor(1) == seqpal::kInstr && tabColor(2) == seqpal::kFx1);
}

// ---------------------------------------------------------------- instrument order: synths first
static void test_x3_instrument_order()
{
    using namespace instorder;
    // the usual layout: 48 samples, 16 MIDI instruments (48 .. 63), PolySynth 1 / 2 / 3 on MIDI 16 / 15 / 14 = 63 / 62 / 61
    Layout L = { 48, 16, { 63, 62, 61 } };
    CHECK(L.synthCount() == 3 && L.total() == 64);
    CHECK(popupPos(L, 63) == 0 && popupPos(L, 62) == 1 && popupPos(L, 61) == 2);
    CHECK(popupPos(L, 0) == 3 && popupPos(L, 47) == 50 && popupPos(L, 48) == 51 && popupPos(L, 60) == 63);
    CHECK(popupIdx(L, 0) == 63 && popupIdx(L, 1) == 62 && popupIdx(L, 2) == 61 && popupIdx(L, 3) == 0 && popupIdx(L, 50) == 47 &&
          popupIdx(L, 51) == 48 && popupIdx(L, 63) == 60);
    // a bijection over all 64 instruments, and the stored index is what comes back
    bool bij = true;
    int seen[64] = { 0 };
    for (int i = 0; i < 64; i++)
    {
        const int p = popupPos(L, i);
        bij = bij && p >= 0 && p < 64 && popupIdx(L, p) == i;
        seen[p]++;
    }
    for (int p = 0; p < 64; p++)
        bij = bij && seen[p] == 1;
    CHECK(bij);
    // out of range positions clamp
    CHECK(popupIdx(L, -5) == 63 && popupIdx(L, 500) == 60);
    // jog order: PolySynth 1 / 2 / 3, then MIDI 60 .. 48 (the stock order is descending), then the samples
    uint8_t order[64];
    CHECK(jogOrder(L, order) == 64);
    CHECK(order[0] == 63 && order[1] == 62 && order[2] == 61 && order[3] == 60 && order[15] == 48 && order[16] == 0 && order[63] == 47);
    bool perm = true;
    int cnt[64] = { 0 };
    for (int i = 0; i < 64; i++)
        cnt[order[i]]++;
    for (int i = 0; i < 64; i++)
        perm = perm && cnt[i] == 1;
    CHECK(perm);
    // stepping: from the top, delta moves down the list, clamped at both ends
    CHECK(jogStep(L, 63, 1) == 62 && jogStep(L, 61, 1) == 60 && jogStep(L, 48, 1) == 0 && jogStep(L, 47, 1) == 47 && jogStep(L, 63, -1) == 63);
    CHECK(jogStep(L, 0, -1) == 48 && jogStep(L, 60, -1) == 61 && jogStep(L, 62, -1) == 63 && jogStep(L, 63, 3) == 60);
    CHECK(jogStep(L, 63, 500) == 47 && jogStep(L, 47, -500) == 63);
    // walking down visits every instrument once
    int at = 63, steps = 0;
    bool visited[64] = { false };
    visited[at] = true;
    for (;;)
    {
        const int nx = jogStep(L, at, 1);
        if (nx == at)
            break;
        at = nx;
        steps++;
        visited[at] = true;
    }
    bool all = steps == 63;
    for (int i = 0; i < 64; i++)
        all = all && visited[i];
    CHECK(all);
    // other slots (a v0.3 project that claimed channel 5, the others on their defaults after the migration)
    Layout M = { 48, 16, { 52, 62, 61 } };
    CHECK(popupPos(M, 52) == 0 && popupPos(M, 62) == 1 && popupPos(M, 61) == 2 && popupPos(M, 63) == 63); // the last of the other MIDI instruments
    CHECK(popupIdx(M, 0) == 52 && popupIdx(M, 3) == 0 && popupIdx(M, 51) == 48 && popupIdx(M, 52) == 49);
    uint8_t o2[64];
    jogOrder(M, o2);
    CHECK(o2[0] == 52 && o2[1] == 62 && o2[2] == 61 && o2[3] == 63 && o2[4] == 60);
    // no PolySynth slot at all: the stock order (popup: samples then MIDI ascending; jog: MIDI descending then samples)
    Layout N = { 48, 16, { -1, -1, -1 } };
    CHECK(popupPos(N, 5) == 5 && popupPos(N, 60) == 60 && popupIdx(N, 55) == 55);
    CHECK(jogOrder(N, order) == 64 && order[0] == 63 && order[16] == 0);
    // one instance missing: the others keep their rank
    Layout O = { 48, 16, { 63, -1, 61 } };
    CHECK(O.synthCount() == 2 && popupPos(O, 61) == 1 && popupPos(O, 0) == 2 && popupIdx(O, 1) == 61);
    // the mapping never touches the stored index: converting back and forth gives the same index
    bool same = true;
    for (int i = 0; i < 64; i++)
        same = same && popupIdx(O, popupPos(O, i)) == i && jogStep(O, i, 0) == i;
    CHECK(same);
}

static void test_x3_all()
{
    test_x3_identity();
    test_x3_instances();
    test_x3_pool();
    test_x3_sends();
    test_x3_governor();
    test_x3_governor_recovery();
    test_x3_file();
    test_x3_multi_edit();
    test_x3_instrument_order();
}
