// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Host tests of the v0.2.4 FX additions (plan 007): the state variable filter (bandpass width, highpass, smoothing, clicks), the
// control mappings (jog steps, Q, highpass index, ring reverb parameters), the sidecar seqfx.bin (version / compatibility), the
// reverb type switch and the ring reverb itself (silence, decay, stability, all-pass equivalence with the original, pre-delay).
#ifndef M_PI
#define M_PI 3.14159265358979323846 // the stock reverb (cosine_oscillator.h) uses it; -std=c++11 does not define it
#endif
#include "reverb.h"
#include "ring_reverb.h"
#include "seqFxDsp.h"
#include "seqFxLogic.h"
#include "seqSidecarLogic.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

using namespace seqfx;

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
#define CHECK_NEAR(a, b, tol)                                                                       \
    do                                                                                              \
    {                                                                                               \
        g_checks++;                                                                                 \
        if (fabs((double)(a) - (double)(b)) > (tol))                                                \
        {                                                                                           \
            g_fail++;                                                                               \
            printf("FAIL %s:%d  %s = %g, expected %g +- %g\n", __FILE__, __LINE__, #a, (double)(a), \
                   (double)(b), (double)(tol));                                                     \
        }                                                                                           \
    } while (0)

// steady-state gain (linear) of a filter at a frequency: run a sine, measure the RMS of the last part
template<typename F>
static double gainAt(double hz, F&& filter)
{
    const int n = 44100;
    double inSq = 0, outSq = 0;
    for (int i = 0; i < n; i++)
    {
        float x = (float)(10000.0 * sin(2.0 * 3.14159265358979 * hz * i / 44100.0));
        float y = filter(x);
        if (i >= n / 2)
        {
            inSq += (double)x * x;
            outSq += (double)y * y;
        }
    }
    return sqrt(outSq / inSq);
}
static double db(double g) { return 20.0 * log10(g); }

// ---- F1: bandpass ----
static void test_bandpass_width()
{
    // 1 octave: unity at the centre, -3 dB at the edges (1000 / sqrt2, 1000 * sqrt2)
    {
        float q = bwToQ(1.0f);
        CHECK_NEAR(q, 1.41421, 1e-3);
        SvfCoef c = svfCoef(1000.0f, q);
        SvfState s;
        auto f = [&](float x) { return bandpass(s, c, x); };
        CHECK_NEAR(db(gainAt(1000, f)), 0.0, 0.1);
        s.clear();
        CHECK_NEAR(db(gainAt(707.1, f)), -3.0, 0.8);
        s.clear();
        CHECK_NEAR(db(gainAt(1414.2, f)), -3.0, 0.8);
        s.clear();
        CHECK(db(gainAt(100, f)) < -18.0);   // 3.3 octaves down: well cut
        s.clear();
        CHECK(db(gainAt(10000, f)) < -18.0);
    }
    // 1/3 octave is narrow, 3 octaves wide: the width order holds at the same distance from the centre
    {
        SvfCoef cn = svfCoef(1000.0f, bwToQ(1.0f / 3.0f)), cw = svfCoef(1000.0f, bwToQ(3.0f));
        SvfState sn, sw;
        auto fn = [&](float x) { return bandpass(sn, cn, x); };
        auto fw = [&](float x) { return bandpass(sw, cw, x); };
        double gn = gainAt(2000, fn), gw = gainAt(2000, fw);
        CHECK(gn < gw);
        CHECK(db(gn) < -9.0);   // an octave away from a 1/3 octave band
        CHECK(db(gw) > -3.5);   // inside the edges of a 3 octave band (2828 Hz)
        // the edges of the narrow band
        sn.clear();
        double lo = gainAt(1000.0 / pow(2.0, 1.0 / 6.0), fn);
        sn.clear();
        double hi = gainAt(1000.0 * pow(2.0, 1.0 / 6.0), fn);
        CHECK_NEAR(db(lo), -3.0, 0.8);
        CHECK_NEAR(db(hi), -3.0, 0.8);
    }
    // the peak never exceeds unity (the feedback loop gain cannot grow beyond the feedback setting)
    for (int bw = kBwMin; bw <= kBwMax; bw += 4)
    {
        SvfCoef c = svfCoef(2000.0f, bwToQ(bw / 12.0f));
        SvfState s;
        auto f = [&](float x) { return bandpass(s, c, x); };
        double peak = 0;
        for (double hz = 200; hz < 15000; hz *= 1.05)
        {
            s.clear();
            double g = gainAt(hz, f);
            if (g > peak)
                peak = g;
        }
        CHECK(peak < 1.02);
        CHECK(peak > 0.95);
    }
}

static void test_tan_approx()
{
    // the filter coefficient warp, over the whole useful range (20 Hz .. 0.45 fs): relative error below 1e-4
    double worst = 0;
    for (double fc = 10; fc <= 22000; fc *= 1.01)
    {
        double x = 3.14159265358979 * fc / 44100.0;
        if (x > 1.45)
            break;
        double e = fabs((double)tanApprox((float)x) - tan(x)) / tan(x);
        if (e > worst)
            worst = e;
    }
    printf("  tanApprox worst relative error %.2g\n", worst);
    CHECK(worst < 1e-4);
    // continuity at the switch between the two forms
    CHECK_NEAR(tanApprox(0.8999f), tanApprox(0.9001f), 2e-3);
}

// Engaging / releasing the bandpass (and the reverb low cut) must not click: the mix is ramped per sample inside the block.
static void test_mix_ramp_no_click()
{
    // host model of the audio side: blocks of 128, a low-frequency sine through  x + m * (bandpass(x) - x)
    for (int dir = 0; dir < 2; dir++)
    {
        FilterControl fc;
        fc.targetOct = log2f(3000.0f);
        fc.targetQ = bwToQ(1.0f);
        fc.targetMix = dir ? 1.0f : 0.0f; // dir 0: engage, dir 1: release
        fc.blockUpdate();
        SvfState s;
        float prevX = 0, prevY = 0;
        double maxDx = 0, maxDy = 0;
        long idx = 0;
        for (int blk = 0; blk < 120; blk++)
        {
            if (blk == 20)
                fc.targetMix = dir ? 0.0f : 1.0f;
            fc.blockUpdate();
            SvfCoef c = fc.coef();
            float mp = fc.prevMix(), mc = fc.mix();
            if (mp <= 0 && mc <= 0)
                s.clear();
            for (int i = 0; i < 128; i++, idx++)
            {
                float x = (float)(10000.0 * sin(2.0 * 3.14159265358979 * 150.0 * idx / 44100.0));
                float y = x;
                if (mp > 0 || mc > 0)
                {
                    float m = mixAt(mp, mc, i, 128);
                    y = x + m * (bandpass(s, c, x) - x);
                }
                if (idx > 0)
                {
                    maxDx = fmax(maxDx, fabs(x - prevX));
                    maxDy = fmax(maxDy, fabs(y - prevY));
                }
                prevX = x;
                prevY = y;
            }
        }
        printf("  mix ramp %s: max output step %.0f, max input step %.0f\n", dir ? "release" : "engage", maxDy, maxDx);
        CHECK(maxDy <= maxDx * 2.0);
    }
    CHECK_NEAR(mixAt(0.2f, 0.8f, 127, 128), 0.8, 1e-6);
    CHECK(mixAt(0.2f, 0.8f, 0, 128) > 0.2f && mixAt(0.2f, 0.8f, 0, 128) < 0.21f);
    // prevMix follows the previous block
    FilterControl f2;
    f2.targetMix = 1.0f;
    f2.blockUpdate();
    CHECK(f2.prevMix() == 1.0f); // the first update snaps, no ramp from the default
    f2.targetMix = 0.0f;
    f2.blockUpdate();
    CHECK(f2.prevMix() == 1.0f && f2.mix() < 1.0f && f2.mix() > 0.5f);
}

static void test_band_mix_top_octave()
{
    CHECK(bandMix(20000.0f) == 0.0f && bandMix(19000.0f) == 0.0f);
    CHECK(bandMix(9500.0f) >= 0.999f && bandMix(5000.0f) == 1.0f && bandMix(100.0f) == 1.0f);
    CHECK(bandMix(18900.0f) > 0.0f && bandMix(18900.0f) < 0.02f); // one click below "open": still almost unfiltered
    float last = -1.0f;
    for (float hz = 19000.0f; hz >= 9000.0f; hz -= 100.0f) // monotone fade-in over the top octave
    {
        float m = bandMix(hz);
        CHECK(m >= last);
        last = m;
    }
    // about 12 semitone clicks from open to full band
    uint16_t f = 19000;
    int clicks = 0;
    while (bandMix(f) < 1.0f && clicks < 100)
    {
        f = jogCenter(f, -1);
        clicks++;
    }
    CHECK(clicks >= 10 && clicks <= 14);
}

// Top of the reverb size / delay feedback range (owner report: crackle at the top step on the v0.2.3 hardware)
static void test_caps_and_soft_limit()
{
    // plan 010 A2 (owner decision): the caps are 95 % now (they were 0.90 / 88)
    CHECK(kReverbSizeCap == 0.95f && kDelayFeedbackCap == 95);
    CHECK(capReverbSize(1.0f) == 0.95f && capReverbSize(0.5f) == 0.5f && capReverbSize(0.0f) == 0.0f);
    CHECK(capDelayFeedback(99) == 95 && capDelayFeedback(95) == 95 && capDelayFeedback(88) == 88 && capDelayFeedback(50) == 50 &&
          capDelayFeedback(0) == 0);
    // soft limit: identity up to 3/4 scale, odd, monotone, never above the 16 bit range for the largest feedback sum
    CHECK(softLimit(20480) == 20480 && softLimit(-20480) == -20480 && softLimit(100) == 100 && softLimit(0) == 0);
    CHECK(softLimit(20484) == 20481 && softLimit(-20484) == -20481);
    CHECK(softLimit(65534) == 31743 && softLimit(-65534) == -31743); // the largest feedback sum stays inside the 16 bit range
    int32_t last = -1000000;
    bool mono = true, odd = true;
    for (int32_t v = -66000; v <= 66000; v += 7)
    {
        int32_t y = softLimit(v);
        mono = mono && y >= last;
        odd = odd && softLimit(-v) == -y;
        last = y;
    }
    CHECK(mono && odd);

    // model of the delay line write (effect_shortDelay.cpp): loud sustained tone, every feedback from 0 to 99 and the capped
    // values: the written samples never hit the 16 bit rails and the step from one written sample to the next stays small
    for (int amp : { 8000, 20000, 32000 })
        for (int fb : { 50, 80, 88, 95, 99 })
        {
            int feedback = (int)(255 * (fb / 100.0));
            const int D = 22050;
            std::vector<int16_t> line(D, 0);
            int head = 0, rails = 0;
            long peak = 0, maxStep = 0, nearRailStep = 0;
            int32_t prev = 0;
            for (int n = 0; n < 44100 * 8; n++)
            {
                int32_t in = (int32_t)(amp * sin(2.0 * 3.14159265358979 * 220.0 * n / 44100.0));
                int32_t d = line[head];
                int32_t nv = (in << 8) + d * feedback;
                nv = softLimit((nv + (nv < 0 ? 255 : 0)) >> 8);
                if (nv >= 32767 || nv <= -32768)
                    rails++;
                line[head] = (int16_t)nv;
                head = (head + 1) % D;
                if (labs(nv) > peak)
                    peak = labs(nv);
                if (n > 0 && labs(nv - prev) > maxStep)
                    maxStep = labs(nv - prev);
                if (n > 0 && (labs(nv) > 28000 || labs(prev) > 28000) && labs(nv - prev) > nearRailStep)
                    nearRailStep = labs(nv - prev);
                prev = nv;
            }
            CHECK(rails == 0);
            CHECK(peak < 32000);
            // no rail-to-rail jumps: the waveform of the compressed tone stays continuous (a hard clip of the loud sum would not)
            CHECK(maxStep < peak / 2);
            // (plan 010 A2: the bound of the old cap, 88, is kept for every value up to 88; the 95 % loop sustains a steeper
            // compressed waveform through its zero crossings, measured 0.356 of the peak at 95 % / 32000, so the bound is 0.4 up to the
            // new cap. The strict part is the next check: nothing near the rails ever steps - a hard clip would.)
            if (fb <= 88)
                CHECK(maxStep < peak * 3 / 10);
            else if (fb <= kDelayFeedbackCap)
                CHECK(maxStep < peak * 4 / 10);
            CHECK(nearRailStep < peak / 8);
        }

    // the ring reverb at the largest size the controls can reach, with a loud sustained low tone + noise: the output stays inside
    // the 16 bit range without sitting on the rails
    std::vector<int16_t> ring, pre;
    ringverb::RingReverb* r = new ringverb::RingReverb();
    ring.assign(ringverb::kRingSize, 0);
    pre.assign(2 * ringverb::kPreSize, 0);
    r->init(ringverb::PlainRing{ ring.data() }, pre.data());
    r->setParams(ringFade(kReverbSizeCap), ringColor(0.5f), ringApGain(0.85f), 100);
    std::vector<int16_t> l(128), rr(128);
    int rail = 0;
    srand(77);
    for (int blk = 0; blk < 44100 * 8 / 128; blk++)
        for (int pass = 0; pass < 1; pass++)
        {
            for (int i = 0; i < 128; i++)
            {
                double ph = 2.0 * 3.14159265358979 * 80.0 * (blk * 128 + i) / 44100.0;
                l[i] = rr[i] = (int16_t)(26000.0 * sin(ph) + (rand() % 6001) - 3000);
            }
            r->process(l.data(), rr.data(), 128);
            for (int i = 0; i < 128; i++)
                rail += (l[i] >= 32767 || l[i] <= -32768 || rr[i] >= 32767 || rr[i] <= -32768) ? 1 : 0;
        }
    CHECK(rail == 0);
    delete r;
}

static void test_highpass()
{
    SvfCoef c = svfCoef(200.0f, kHpQ);
    SvfState s;
    auto f = [&](float x) { return highpass(s, c, x); };
    CHECK_NEAR(db(gainAt(200, f)), -3.0, 0.5);
    s.clear();
    CHECK(db(gainAt(30, f)) < -30.0); // 12 dB / octave: 2.7 octaves below
    s.clear();
    CHECK(db(gainAt(2000, f)) > -0.3);
    s.clear();
    CHECK(db(gainAt(10000, f)) > -0.2);
}

static void test_extremes_finite()
{
    const float freqs[] = { 1, 20, 100, 1000, 19000, 20000, 22000, 1e6f };
    const float qs[] = { 0.001f, 0.4f, 0.7071f, 4.4f, 100.0f };
    for (float fc : freqs)
        for (float q : qs)
        {
            SvfCoef c = svfCoef(fc, q);
            SvfState s;
            bool ok = true;
            for (int i = 0; i < 2000; i++)
            {
                float x = (i % 50 < 25) ? 20000.0f : -20000.0f;
                float y = bandpass(s, c, x) + highpass(s, c, x);
                if (!(fabsf(y) < 1e7f))
                    ok = false;
            }
            CHECK(ok);
            CHECK(c.a1 > 0 && c.a1 <= 1);
        }
}

static void test_control_smoothing()
{
    FilterControl fc;
    fc.targetOct = log2f(200.0f);
    fc.targetQ = 1.41f;
    fc.targetMix = 1.0f;
    CHECK(fc.blockUpdate()); // the first call snaps to the targets
    CHECK_NEAR(fc.centreHz(), 200.0, 1.0);
    CHECK_NEAR(fc.mix(), 1.0, 1e-6);
    // a jump of 200 -> 10000 Hz: monotone, every step at most kAlpha of the remaining distance, ends exactly at the target
    fc.targetOct = log2f(10000.0f);
    float prev = fc.centreHz();
    int blocks = 0;
    bool mono = true;
    float maxStepOct = 0;
    while (blocks < 200)
    {
        float before = log2f(prev);
        if (!fc.blockUpdate())
            break;
        float now = fc.centreHz();
        if (now < prev - 1e-3f)
            mono = false;
        float stepOct = log2f(now) - before;
        if (stepOct > maxStepOct)
            maxStepOct = stepOct;
        prev = now;
        blocks++;
    }
    CHECK(mono);
    CHECK(blocks < 40);             // settled within ~100 ms
    CHECK(blocks > 5);              // but not a jump
    CHECK_NEAR(fc.centreHz(), 10000.0, 1.0);
    CHECK(maxStepOct < 0.3f * 5.7f + 0.01f); // the first step is alpha of 5.6 octaves
    // bypass crossfade 1 -> 0 -> 1
    fc.targetMix = 0.0f;
    int n = 0;
    float lastMix = fc.mix();
    bool down = true;
    while (fc.mix() > 0.0f && n < 200)
    {
        fc.blockUpdate();
        if (fc.mix() > lastMix + 1e-6f)
            down = false;
        lastMix = fc.mix();
        n++;
    }
    CHECK(down);
    CHECK(n > 5 && n < 40);
    CHECK(fc.mix() == 0.0f);
    fc.targetMix = 1.0f;
    for (int i = 0; i < 60; i++)
        fc.blockUpdate();
    CHECK(fc.mix() == 1.0f);
}

// A sweeping centre must not click: the output of a bandpass fed with a sine, while the centre moves block by block over three
// octaves, never steps further than the input itself can.
static void test_no_clicks_while_sweeping()
{
    for (int bwTw : { kBwMin, kBwDefault, kBwMax })
    {
        FilterControl fc;
        fc.targetOct = log2f(300.0f);
        fc.targetQ = bwToQ(bwTw / 12.0f);
        fc.targetMix = 1.0f;
        fc.blockUpdate();
        SvfState s;
        double maxDx = 0, maxDy = 0, maxY = 0;
        float prevX = 0, prevY = 0;
        long idx = 0;
        for (int blk = 0; blk < 400; blk++)
        {
            // the target jumps up and down by 3 octaves every 50 blocks
            fc.targetOct = log2f(((blk / 50) % 2) ? 300.0f : 2400.0f);
            fc.blockUpdate();
            SvfCoef c = fc.coef();
            for (int i = 0; i < 128; i++, idx++)
            {
                float x = (float)(8000.0 * sin(2.0 * 3.14159265358979 * 1000.0 * idx / 44100.0));
                float y = bandpass(s, c, x);
                if (idx > 0)
                {
                    maxDx = fmax(maxDx, fabs(x - prevX));
                    maxDy = fmax(maxDy, fabs(y - prevY));
                }
                maxY = fmax(maxY, fabs(y));
                prevX = x;
                prevY = y;
            }
        }
        CHECK(maxY < 8000.0 * 2.0);       // never more than twice the input level
        CHECK(maxDy < maxDx * 2.0);       // no step beyond twice the steepest slope of the input
    }
}

// ---- mappings ----
static void test_jog_mappings()
{
    // centre: one semitone per step, always moves, clamped to the project range
    CHECK(jogCenter(1000, 1) == 1059);
    CHECK(jogCenter(1000, -1) == 944);
    CHECK(jogCenter(1000, 12) == 2000);
    CHECK(jogCenter(100, -1) == 100);
    CHECK(jogCenter(100, 1) > 100);
    CHECK(jogCenter(20000, 1) == 20000);
    CHECK(jogCenter(19500, 5) == 20000);
    CHECK(jogCenter(50, 0) == 100); // below the range: pulled in
    for (int steps : { 1, 2, 5, 11 })
    {
        uint16_t f = 100;
        int guard = 0;
        while (f < 20000 && guard++ < 1000)
        {
            uint16_t nf = jogCenter(f, steps);
            CHECK(nf > f);
            f = nf;
        }
        CHECK(f == 20000);
        guard = 0;
        while (f > 100 && guard++ < 1000)
        {
            uint16_t nf = jogCenter(f, -steps);
            CHECK(nf < f);
            f = nf;
        }
        CHECK(f == 100);
    }
    // the range end is "open" (filter out of the path), the rest is not
    CHECK(centerOpen(20000) && centerOpen(19000) && !centerOpen(18999) && !centerOpen(100));
    CHECK(kCenterMinHz == 100 && kCenterMaxHz == 20000); // = DELAY_FILTER_FREQ_MIN / MAX of the stock project

    // width: 1/12 octave per step, 1/3 .. 3 octaves
    CHECK(kBwMin == 4 && kBwMax == 36 && kBwDefault == 12);
    CHECK(jogWidth(12, 1) == 13 && jogWidth(12, -1) == 11);
    CHECK(jogWidth(4, -5) == 4 && jogWidth(36, 5) == 36);
    CHECK(jogWidth(0, 0) == 4 && jogWidth(200, 0) == 36);
    CHECK_NEAR(bwOctaves(12), 1.0, 1e-6);
    CHECK_NEAR(bwOctaves(4), 1.0 / 3.0, 1e-6);
    CHECK_NEAR(bwOctaves(36), 3.0, 1e-6);
    CHECK_NEAR(bwToQ(3.0f), 0.4041, 1e-3);
    CHECK_NEAR(bwToQ(1.0f / 3.0f), 4.3185, 5e-3);
    float lastQ = 1e9f;
    for (int b = kBwMin; b <= kBwMax; b++) // wider = lower Q, strictly
    {
        float q = bwToQ(b / 12.0f);
        CHECK(q < lastQ);
        lastQ = q;
    }

    // highpass index: 0 = off, 30 Hz .. ~5.1 kHz (plan 010 A1: the range ended at 72 = 1.8 kHz before)
    CHECK(hpHz(0) == 0.0f);
    CHECK_NEAR(hpHz(1), 30.0, 1e-3);
    CHECK_NEAR(hpHz(13), 60.0, 1e-2);
    CHECK(hpHz(72) > 1800 && hpHz(72) < 1830); // the old top is still index 72 (old seqfx.bin values keep their meaning)
    CHECK(kHpMax == 90);
    CHECK_NEAR(hpHz(kHpMax), 30.0 * pow(2.0, 89.0 / 12.0), 0.5);
    CHECK(hpHz(kHpMax) > 4800 && hpHz(kHpMax) < 5300);
    for (int i = 2; i <= kHpMax; i++)
        CHECK(hpHz((uint8_t)i) > hpHz((uint8_t)(i - 1)));
    CHECK(jogHp(0, -3) == 0 && jogHp(0, 3) == 3);
    CHECK(jogHp(70, 5) == 75 && jogHp(88, 5) == kHpMax && jogHp(kHpMax, 1) == kHpMax);
    CHECK(jogHp(200, 0) == kHpMax);
    CHECK(clampHp(-5) == 0 && clampHp(1000) == kHpMax);
}

static void test_ring_mappings()
{
    // size -> fade: the curve of the original time control, 0 = no loop gain, 250 max (never 256: no runaway)
    CHECK(ringFade(0.0f) == 0);
    CHECK(ringFade(1.0f) == 250);
    CHECK(ringFade(0.5f) == 234);
    CHECK(ringFade(-1.0f) == 0 && ringFade(5.0f) == 250);
    int last = -1;
    for (int i = 0; i <= 100; i++)
    {
        int f = ringFade(i / 100.0f);
        CHECK(f >= last && f <= 250);
        last = f;
    }
    // damp -> colour: brighter at low damp, always a valid one-pole coefficient
    float lastC = 2.0f;
    for (int i = 0; i <= 100; i++)
    {
        float c = ringColor(i / 100.0f);
        CHECK(c > 0.0f && c < 1.0f);
        CHECK(c <= lastC + 1e-6f);
        lastC = c;
    }
    CHECK_NEAR(ringColor(0.0f), 1.0 - exp(-2.0 * 3.14159265 * 18000.0 / 44100.0), 1e-4);
    // diffusion -> all-pass gain: the stock default 0.68 is the gain 0.5 of the original
    CHECK(ringApGain(0.68f) == 16384);
    CHECK(ringApGain(0.0f) == 0);
    CHECK(ringApGain(0.85f) == 20480);
    CHECK(ringApGain(2.0f) == 20480 && ringApGain(-1.0f) == 0);
    CHECK(ringApGain(0.85f) < 32768); // always stable (|g| < 1)
    // predelay
    CHECK(ringPredelay(0.0f) == 0 && ringPredelay(1.0f) == kRingPredelayMax);
    CHECK(ringPredelay(0.5f) == kRingPredelayMax / 2);
    CHECK(ringPredelay(9.0f) == kRingPredelayMax && ringPredelay(-1.0f) == 0);
    CHECK(kRingPredelayMax == (int)ringverb::kPreSize - 1);
}

// ---- sidecar seqfx.bin ----
static void test_sidecar()
{
    Params p;
    CHECK(p.isDefault() && p.bw == 12 && p.hp == 0 && p.type == RT_STOCK);
    uint8_t buf[64];
    CHECK(p.save(buf, 8) == 0); // too small
    size_t n = p.save(buf, sizeof(buf));
    CHECK(n == (size_t)kFileMax && n == 18);
    CHECK(memcmp(buf, "SQFX", 4) == 0 && buf[4] == kWriteVersion);

    Params q;
    q.bw = 30;
    q.hp = 40;
    q.type = RT_PLINKY;
    CHECK(!q.isDefault() && q.hash() != p.hash());
    n = q.save(buf, sizeof(buf));
    Params r;
    bool newer = true;
    CHECK(r.load(buf, n, &newer) && !newer);
    CHECK(r == q);

    // missing / short / corrupt: nothing changes
    Params keep = q;
    CHECK(!keep.load(buf, 0) && keep == q);
    CHECK(!keep.load(buf, n - 1) && keep == q);
    uint8_t bad[64];
    memcpy(bad, buf, n);
    bad[9] ^= 0x01; // payload bit flip -> CRC
    CHECK(!keep.load(bad, n) && keep == q);
    memcpy(bad, buf, n);
    bad[0] = 'X';
    CHECK(!keep.load(bad, n) && keep == q);

    // a file of a newer mod version is reported and left alone
    {
        uint8_t pl[8] = { 12, 0, 0, 0, 0, 0, 0, 0 };
        uint8_t nb[64];
        size_t nn = seqsidecar::frameWrite(nb, sizeof(nb), kMagic, kVersion + 1, pl, 8);
        Params t = q;
        bool nw = false;
        CHECK(!t.load(nb, nn, &nw) && nw && t == q);
    }
    // version 0 is invalid
    {
        uint8_t pl[8] = { 12, 0, 0, 0, 0, 0, 0, 0 };
        uint8_t nb[64];
        size_t nn = seqsidecar::frameWrite(nb, sizeof(nb), kMagic, 0, pl, 8);
        Params t = q;
        CHECK(!t.load(nb, nn) && t == q);
    }
    // out-of-range values fall back to their own default (a hand-edited file never gives a wild setting), the others stay
    {
        uint8_t pl[8] = { 99, 200, 7, 0, 0, 0, 0, 0 };
        uint8_t nb[64];
        size_t nn = seqsidecar::frameWrite(nb, sizeof(nb), kMagic, kVersion, pl, 8);
        Params t;
        t.bw = 20;
        CHECK(t.load(nb, nn) && t.bw == kBwDefault && t.hp == 0 && t.type == RT_STOCK);
        uint8_t pl2[8] = { 3, 72, 1, 0, 0, 0, 0, 0 };
        nn = seqsidecar::frameWrite(nb, sizeof(nb), kMagic, kVersion, pl2, 8);
        CHECK(t.load(nb, nn) && t.bw == kBwDefault && t.hp == 72 && t.type == RT_PLINKY);
        uint8_t pl3[8] = { 4, 0, 0, 0, 0, 0, 0, 0 };
        nn = seqsidecar::frameWrite(nb, sizeof(nb), kMagic, kVersion, pl3, 8);
        CHECK(t.load(nb, nn) && t.bw == 4);
        uint8_t pl4[8] = { 36, 0, 0, 0, 0, 0, 0, 0 };
        nn = seqsidecar::frameWrite(nb, sizeof(nb), kMagic, kVersion, pl4, 8);
        CHECK(t.load(nb, nn) && t.bw == 36);
    }
    // a later version 1 revision with a longer payload is read as far as it is known; a payload shorter than the known fields is refused
    {
        uint8_t pl[12] = { 20, 5, 1, 0, 0, 0, 0, 0, 9, 9, 9, 9 };
        uint8_t nb[64];
        size_t nn = seqsidecar::frameWrite(nb, sizeof(nb), kMagic, kVersion, pl, 12);
        Params t;
        CHECK(t.load(nb, nn) && t.bw == 20 && t.hp == 5 && t.type == RT_PLINKY);
        nn = seqsidecar::frameWrite(nb, sizeof(nb), kMagic, kVersion, pl, 2);
        Params u;
        CHECK(!u.load(nb, nn) && u.isDefault());
    }
    // a project without the file sounds like before: the defaults are Stock, no highpass, 1 octave (the width only matters
    // while the centre is below the "open" end)
    Params none;
    CHECK(none.type == RT_STOCK && none.hp == 0 && hpHz(none.hp) == 0.0f);
    CHECK(strcmp(reverbTypeName(RT_STOCK), "Stock") == 0 && strcmp(reverbTypeName(RT_PLINKY), "ShimmerReverb") == 0);
    CHECK(strcmp(reverbTypeName(77), "Stock") == 0);
}

// ---- reverb type switch ----
static void test_type_switch()
{
    TypeSwitch ts;
    CHECK(ts.step(RT_STOCK) == TypeSwitch::ACT_RUN);
    CHECK(ts.step(RT_STOCK) == TypeSwitch::ACT_RUN);
    // request Plinky: fade out, clear (n blocks), fade in, run; the active type changes only after the clearing
    CHECK(ts.step(RT_PLINKY) == TypeSwitch::ACT_FADE_OUT);
    CHECK(ts.active == RT_STOCK);
    int clears = 0;
    while (ts.step(RT_PLINKY) == TypeSwitch::ACT_CLEAR)
    {
        CHECK(ts.active == RT_STOCK);
        if (++clears == 10)
        {
            ts.clearFinished();
            break;
        }
    }
    CHECK(clears == 10 && ts.active == RT_PLINKY);
    CHECK(ts.step(RT_PLINKY) == TypeSwitch::ACT_FADE_IN);
    CHECK(ts.step(RT_PLINKY) == TypeSwitch::ACT_RUN);

    // changed again while clearing: the clearing restarts for the type that is wanted now
    ts = TypeSwitch();
    CHECK(ts.step(RT_PLINKY) == TypeSwitch::ACT_FADE_OUT);
    CHECK(ts.step(RT_PLINKY) == TypeSwitch::ACT_CLEAR);
    ts.clearOffset = 4096;
    CHECK(ts.step(RT_STOCK) == TypeSwitch::ACT_CLEAR);
    CHECK(ts.clearType == RT_STOCK && ts.clearOffset == 0);
    ts.clearFinished();
    CHECK(ts.active == RT_STOCK && ts.step(RT_STOCK) == TypeSwitch::ACT_FADE_IN);

    // jumpTo: boot / clearReverb
    ts.jumpTo(RT_PLINKY);
    CHECK(ts.active == RT_PLINKY && ts.step(RT_PLINKY) == TypeSwitch::ACT_RUN);

    // the silent piece-wise clearing covers each byte of all segments exactly once, in pieces of at most 8 KB
    {
        const uint32_t sets[3][3] = { { 81920, 0, 0 }, { 32768, 32768, 0 }, { 32768, 32768, 4096 } };
        const int counts[3] = { 1, 2, 3 };
        for (int k = 0; k < 3; k++)
        {
            std::vector<std::vector<uint8_t>> segs;
            for (int s = 0; s < counts[k]; s++)
                segs.push_back(std::vector<uint8_t>(sets[k][s], 0));
            uint32_t off = 0, total = clearTotal(sets[k], counts[k]);
            int seg = 0, pieces = 0;
            uint32_t start = 0, n;
            while ((n = nextClearPiece(sets[k], counts[k], off, 8192, &seg, &start)) != 0)
            {
                CHECK(n <= 8192);
                CHECK(seg >= 0 && seg < counts[k] && start + n <= segs[seg].size());
                for (uint32_t i = 0; i < n; i++)
                    segs[seg][start + i]++;
                pieces++;
            }
            bool all = true;
            for (auto& s : segs)
                for (uint8_t v : s)
                    all = all && v == 1;
            CHECK(all);
            CHECK(off == total);
            CHECK(pieces >= (int)((total + 8191) / 8192));
            CHECK(pieces <= (int)((total + 8191) / 8192) + counts[k] - 1);
        }
    }
    // the stock reverb has two 32 KB buffers, the ring on the original Tracker 80 KB: 8 and 10 blocks of silence (23 / 29 ms)
    {
        const uint32_t stock[2] = { 32768, 32768 };
        const uint32_t ringMem[1] = { (uint32_t)ringverb::RingReverb::kBytes };
        uint32_t off = 0;
        int seg, pieces = 0;
        uint32_t st;
        while (nextClearPiece(stock, 2, off, 8192, &seg, &st))
            pieces++;
        CHECK(pieces == 8);
        off = 0;
        pieces = 0;
        while (nextClearPiece(ringMem, 1, off, 8192, &seg, &st))
            pieces++;
        CHECK(pieces == 10);
    }
}

// ---- the ring reverb ----
static ringverb::RingReverb* makeRing(std::vector<int16_t>& ring, std::vector<int16_t>& pre)
{
    ring.assign(ringverb::kRingSize, 0);
    pre.assign(2 * ringverb::kPreSize, 0);
    ringverb::RingReverb* r = new ringverb::RingReverb();
    r->init(ringverb::PlainRing{ ring.data() }, pre.data());
    return r;
}

static void test_ring_allpass_equals_original()
{
    // with the gain 0.5 the all-pass is exactly  acc -= d >> 1; buf[i] = sat(acc); acc = (acc >> 1) + d  (the original)
    srand(7);
    std::vector<int16_t> a(ringverb::kRingSize), b(ringverb::kRingSize);
    for (size_t i = 0; i < a.size(); i++)
        a[i] = b[i] = (int16_t)(rand() - RAND_MAX / 2);
    uint32_t ia = 100, ib = 100;
    bool same = true;
    for (int k = 0; k < 20000; k++)
    {
        int32_t acc = (rand() % 70000) - 35000;
        int len = 1 + rand() % 3000;
        int32_t ra = ringverb::apStage(ringverb::PlainRing{ a.data() }, ia, len, acc, 16384);
        // reference
        uint32_t j = (ib + (uint32_t)len) & ringverb::kRingMask;
        int32_t d = b[j];
        int32_t acc2 = acc;
        acc2 -= d >> 1;
        // plan 010 A2: the stage writes through the soft limit (identity up to 20480; a quarter of the slope above, then the 16 bit rails)
        {
            int32_t w = acc2;
            if (w > 20480)
                w = 20480 + ((w - 20480) >> 2);
            else if (w < -20480)
                w = -20480 - ((-w - 20480) >> 2);
            b[ib] = (int16_t)(w < -32768 ? -32768 : (w > 32767 ? 32767 : w));
        }
        acc2 = (acc2 >> 1) + d;
        ib = j;
        if (ra != acc2 || ia != ib)
            same = false;
    }
    CHECK(same);
    CHECK(memcmp(a.data(), b.data(), a.size() * 2) == 0);
    // delay stage
    uint32_t i1 = 5;
    a[(5 + 700) & ringverb::kRingMask] = 1234;
    int32_t out = ringverb::delayStage(ringverb::PlainRing{ a.data() }, i1, 700, -77);
    CHECK(out == 1234 && a[5] == -77 && i1 == 705);
    // saturating store
    int32_t out2 = ringverb::delayStage(ringverb::PlainRing{ a.data() }, i1, 10, 99999);
    (void)out2;
    CHECK(a[705] == 32767);
}

static double rms(const std::vector<int16_t>& v, size_t from, size_t to)
{
    double s = 0;
    for (size_t i = from; i < to; i++)
        s += (double)v[i] * v[i];
    return sqrt(s / (double)(to - from));
}

static void runBlocks(ringverb::RingReverb& r, std::vector<int16_t>& l, std::vector<int16_t>& rr)
{
    for (size_t i = 0; i + 128 <= l.size(); i += 128)
        r.process(&l[i], &rr[i], 128);
}

static void test_ring_silence_and_decay()
{
    std::vector<int16_t> ring, pre;
    ringverb::RingReverb* r = makeRing(ring, pre);
    r->setParams(ringFade(0.5f), ringColor(0.5f), ringApGain(0.68f), 0);
    // silence in -> exactly silence out
    {
        std::vector<int16_t> l(44100, 0), rr(44100, 0);
        runBlocks(*r, l, rr);
        bool z = true;
        for (size_t i = 0; i < l.size(); i++)
            z = z && l[i] == 0 && rr[i] == 0;
        CHECK(z);
    }
    // a noise burst (not a single impulse: the tail of an impulse falls below one LSB within a few seconds): a tail on both
    // channels that decays
    const size_t N = 44100 * 12;
    std::vector<int16_t> l(N, 0), rr(N, 0);
    srand(21);
    for (size_t i = 0; i < 22050; i++)
    {
        l[i] = (int16_t)((rand() % 16001) - 8000);
        rr[i] = (int16_t)((rand() % 16001) - 8000);
    }
    r->setLevels(32768, 0, 256); // the wet part only
    r->resetState();
    runBlocks(*r, l, rr);
    double e1 = rms(l, 44100, 44100 * 2) + rms(rr, 44100, 44100 * 2);
    double e2 = rms(l, 44100 * 3, 44100 * 4) + rms(rr, 44100 * 3, 44100 * 4);
    double e3 = rms(l, 44100 * 6, 44100 * 7) + rms(rr, 44100 * 6, 44100 * 7);
    printf("  ring tail rms (L+R): 1-2 s %.2f, 3-4 s %.2f, 6-7 s %.2f\n", e1, e2, e3);
    CHECK(e1 > 20.0); // there is a tail
    CHECK(e2 < e1 * 0.8);
    CHECK(e3 < e2 * 0.8); // it decays
    CHECK(rms(rr, 44100, 44100 * 2) > 5.0 && rms(l, 44100, 44100 * 2) > 5.0);
    delete r;

    // a longer size decays slower than a shorter one
    double tail[2];
    for (int k = 0; k < 2; k++)
    {
        std::vector<int16_t> a, b;
        ringverb::RingReverb* q = makeRing(a, b);
        q->setParams(ringFade(k ? 0.9f : 0.3f), ringColor(0.5f), ringApGain(0.68f), 0);
        std::vector<int16_t> x(44100 * 4, 0), y(44100 * 4, 0);
        for (size_t i = 0; i < 22050; i++)
        {
            x[i] = (int16_t)((rand() % 16001) - 8000);
            y[i] = (int16_t)((rand() % 16001) - 8000);
        }
        q->setLevels(32768, 0, 256);
        runBlocks(*q, x, y);
        tail[k] = rms(x, 44100 * 3, 44100 * 4) + rms(y, 44100 * 3, 44100 * 4);
        delete q;
    }
    CHECK(tail[1] > tail[0] * 2.0);
}

static void test_ring_stability()
{
    // full-scale noise at the longest decay and the highest diffusion for 2 s, then silence: no wrap-around garbage, no runaway,
    // the tail only ever decays
    std::vector<int16_t> ring, pre;
    ringverb::RingReverb* r = makeRing(ring, pre);
    r->setParams(250, ringColor(0.0f), ringApGain(0.85f), 4095);
    srand(3);
    std::vector<int16_t> l(128), rr(128);
    double winE[8] = { 0 };
    int blocksPerSec = 44100 / 128;
    for (int blk = 0; blk < blocksPerSec * 24; blk++)
    {
        bool loud = blk < blocksPerSec * 2;
        for (int i = 0; i < 128; i++)
        {
            l[i] = loud ? (int16_t)((rand() % 65536) - 32768) : 0;
            rr[i] = loud ? (int16_t)((rand() % 65536) - 32768) : 0;
        }
        r->process(l.data(), rr.data(), 128);
        int win = (blk / blocksPerSec - 2) / 3; // 3 s windows after the burst
        if (win >= 0 && win < 8)
            for (int i = 0; i < 128; i++)
                winE[win] += (double)l[i] * l[i] + (double)rr[i] * rr[i];
    }
    printf("  ring tail energy per 3 s window at fade 250 (after a full-scale burst): %.3g %.3g %.3g %.3g\n", winE[0], winE[1],
           winE[2], winE[3]);
    CHECK(winE[0] > 0);
    for (int w = 1; w < 7; w++)
        CHECK(winE[w] <= winE[w - 1]);
    CHECK(winE[3] < winE[0] * 0.05);
    delete r;
}

static void test_ring_predelay_and_levels()
{
    // the wet part starts after the pre-delay (dry share off so only the reverb is heard)
    for (int pd : { 0, 500, 2000, 4095 })
    {
        std::vector<int16_t> ring, pre;
        ringverb::RingReverb* r = makeRing(ring, pre);
        r->setLevels(32768, 0, 256);
        r->setParams(ringFade(0.6f), ringColor(0.3f), ringApGain(0.68f), pd);
        r->resetState();
        // resetState starts the slewing pre-delay at the target
        std::vector<int16_t> l(8192, 0), rr(8192, 0);
        l[0] = 30000;
        runBlocks(*r, l, rr);
        int first = -1;
        for (int i = 0; i < (int)l.size(); i++)
            if (l[i] != 0 || rr[i] != 0)
            {
                first = i;
                break;
            }
        // the first sound of the reverb is the answer of the second leg (its long delay, 2179 samples), after the pre-delay
        CHECK(first >= pd + 2100);
        CHECK(first <= pd + 2300);
        delete r;
    }
    // the dry share: with no reverb input path (fade 0) a fifth of the input comes back at once
    {
        std::vector<int16_t> ring, pre;
        ringverb::RingReverb* r = makeRing(ring, pre);
        r->setParams(0, 0.5f, 16384, 0);
        r->setLevels(0, 51, 256); // the reverb input is muted: only the dry share
        std::vector<int16_t> l(128, 10000), rr(128, -10000);
        r->process(l.data(), rr.data(), 128);
        CHECK(l[10] == (10000 * 51) >> 8 && rr[10] == (-10000 * 51) >> 8);
        delete r;
    }
    // level of the default settings with noise in: informative (the Tracker's stock reverb is the reference by ear)
    {
        std::vector<int16_t> ring, pre;
        ringverb::RingReverb* r = makeRing(ring, pre);
        r->setParams(ringFade(0.5f), ringColor(0.5f), ringApGain(0.68f), ringPredelay(0.5f));
        srand(11);
        std::vector<int16_t> l(44100 * 3), rr(44100 * 3), l0;
        for (size_t i = 0; i < l.size(); i++)
        {
            l[i] = (int16_t)((rand() % 6001) - 3000);
            rr[i] = (int16_t)((rand() % 6001) - 3000);
        }
        l0 = l;
        runBlocks(*r, l, rr);
        double in = rms(l0, 44100, 44100 * 3), out = rms(l, 44100, 44100 * 3);
        printf("  ring level at defaults, noise in: in %.0f rms, return %.0f rms (%.1f dB)\n", in, out, 20 * log10(out / in));
        CHECK(out > in * 0.2 && out < in * 1.0); // louder than the dry share alone, quieter than the input
        delete r;
    }
}

static void test_ring_determinism_and_params_in_flight()
{
    std::vector<int16_t> r1, p1, r2, p2;
    ringverb::RingReverb* a = makeRing(r1, p1);
    ringverb::RingReverb* b = makeRing(r2, p2);
    srand(5);
    std::vector<int16_t> l(128), rr(128), l2(128), rr2(128);
    bool same = true;
    for (int blk = 0; blk < 800; blk++)
    {
        for (int i = 0; i < 128; i++)
        {
            l[i] = l2[i] = (int16_t)((rand() % 20000) - 10000);
            rr[i] = rr2[i] = (int16_t)((rand() % 20000) - 10000);
        }
        // parameters moving in flight (all four) must stay finite and equal for both instances
        float t = (float)(blk % 100) / 100.0f;
        a->setParams(ringFade(t), ringColor(1.0f - t), ringApGain(t * 0.85f), ringPredelay(t));
        b->setParams(ringFade(t), ringColor(1.0f - t), ringApGain(t * 0.85f), ringPredelay(t));
        a->process(l.data(), rr.data(), 128);
        b->process(l2.data(), rr2.data(), 128);
        if (memcmp(l.data(), l2.data(), 256) || memcmp(rr.data(), rr2.data(), 256))
            same = false;
    }
    CHECK(same);
    CHECK(a->predelay() >= 0 && a->predelay() <= (int)ringverb::kPreSize - 1);
    delete a;
    delete b;
}

// The Tracker Plus / Mini keep the ring in two half blocks (the idle buffers of the stock reverb): the sound is identical
static void test_ring_split_equals_plain()
{
    std::vector<int16_t> one(ringverb::kRingSize, 0), a(ringverb::kRingSize / 2, 0), c(ringverb::kRingSize / 2, 0);
    std::vector<int16_t> p1(2 * ringverb::kPreSize, 0), p2(2 * ringverb::kPreSize, 0);
    ringverb::BasicRingReverb<ringverb::PlainRing> plain;
    ringverb::BasicRingReverb<ringverb::SplitRing> split;
    plain.init(ringverb::PlainRing{ one.data() }, p1.data());
    split.init(ringverb::SplitRing{ a.data(), c.data() }, p2.data());
    plain.setParams(ringFade(0.7f), ringColor(0.4f), ringApGain(0.7f), ringPredelay(0.3f));
    split.setParams(ringFade(0.7f), ringColor(0.4f), ringApGain(0.7f), ringPredelay(0.3f));
    srand(9);
    bool same = true;
    std::vector<int16_t> l1(128), r1(128), l2(128), r2(128);
    for (int blk = 0; blk < 600; blk++)
    {
        for (int i = 0; i < 128; i++)
        {
            l1[i] = l2[i] = (int16_t)((rand() % 24001) - 12000);
            r1[i] = r2[i] = (int16_t)((rand() % 24001) - 12000);
        }
        plain.process(l1.data(), r1.data(), 128);
        split.process(l2.data(), r2.data(), 128);
        if (memcmp(l1.data(), l2.data(), 256) || memcmp(r1.data(), r2.data(), 256))
            same = false;
    }
    CHECK(same);
    // the halves are used as the two halves of the ring
    ringverb::SplitRing sr{ a.data(), c.data() };
    sr[5] = 11;
    sr[16384 + 5] = 22;
    CHECK(a[5] == 11 && c[5] == 22);
}

static void test_ring_silence_bypass()
{
    std::vector<int16_t> ring, pre;
    ringverb::RingReverb* r = makeRing(ring, pre);
    r->setParams(ringFade(0.2f), ringColor(0.5f), ringApGain(0.68f), 0);
    srand(31);
    std::vector<int16_t> l(128), rr(128);
    // a short burst, then silence for 3 s: the tail dies, then the bypass takes over and the output stays exactly the dry share
    for (int blk = 0; blk < 44100 * 3 / 128; blk++)
    {
        for (int i = 0; i < 128; i++)
            l[i] = rr[i] = blk < 20 ? (int16_t)((rand() % 16001) - 8000) : 0;
        r->process(l.data(), rr.data(), 128);
    }
    bool zero = true;
    for (int i = 0; i < 128; i++)
        zero = zero && l[i] == 0 && rr[i] == 0;
    CHECK(zero);
    // after the bypass is active, input wakes it up at once: the dry share is immediate, a tail follows
    for (int i = 0; i < 128; i++)
        l[i] = rr[i] = 8000;
    r->process(l.data(), rr.data(), 128);
    CHECK(l[5] == (8000 * 51) >> 8);
    double e = 0;
    for (int blk = 0; blk < 200; blk++)
    {
        for (int i = 0; i < 128; i++)
            l[i] = rr[i] = blk < 5 ? (int16_t)((rand() % 16001) - 8000) : 0;
        r->process(l.data(), rr.data(), 128);
        if (blk > 40)
            for (int i = 0; i < 128; i++)
                e += fabs((double)l[i]) + fabs((double)rr[i]);
    }
    CHECK(e > 100.0);
    delete r;
}

static void test_ring_geometry()
{
    // the loop must fit into the ring with the deepest shimmer tap
    int deepest = (int)((4095 + 8192) / ringverb::kRateScale) + 1;
    int firstLeg = ringverb::kLenAp1 + ringverb::kLenAp2 + ringverb::kLenAp3 + ringverb::kLenAp4 + ringverb::kLenWob1 +
                   ringverb::kLenAp5 + ringverb::kLenDel1;
    CHECK(firstLeg + deepest + 4 < (int)ringverb::kRingSize);
    CHECK(ringverb::kLoopLength + 400 < (int)ringverb::kRingSize);
    CHECK(ringverb::kRingSize == 32768 && ringverb::kPreSize == 4096);
    // lengths are the original ones x 44100 / 32000 (the original halves what it lists)
    CHECK(ringverb::kLenAp1 == (int)(142 / 2 * 44100.0 / 32000 + 0.5));
    CHECK(ringverb::kLenAp2 == (int)(379 / 2 * 44100.0 / 32000 + 0.5));
    CHECK(ringverb::kLenAp3 == (int)(107 / 2 * 44100.0 / 32000 + 0.5));
    CHECK(ringverb::kLenAp4 == (int)(277 / 2 * 44100.0 / 32000 + 0.5));
    CHECK(ringverb::kLenWob1 == (int)(672 / 2 * 44100.0 / 32000 + 0.5));
    CHECK(ringverb::kLenAp5 == (int)(1800 / 2 * 44100.0 / 32000 + 0.5));
    CHECK(ringverb::kLenDel1 == (int)(4453 / 2 * 44100.0 / 32000 + 0.5));
    CHECK(ringverb::kLenWob2 == (int)(908 / 2 * 44100.0 / 32000 + 0.5));
    CHECK(ringverb::kLenAp6 == (int)(2656 / 2 * 44100.0 / 32000 + 0.5));
    CHECK(ringverb::kLenDel2 == (int)(3163 / 2 * 44100.0 / 32000 + 0.5));
    CHECK(ringverb::RingReverb::ringBytes() == 65536);
    CHECK(ringverb::RingReverb::preBytes() == 2 * ringverb::kPreSize * 2);
}

// ---------------------------------------------------------------- plan 010 A2: soft saturation, 95 % loops
static void test_soft_sat()
{
    CHECK(softSat(0.0f) == 0.0f && softSat(0.5f) == 0.5f && softSat(-0.625f) == -0.625f && softSat(0.625f) == 0.625f);
    CHECK(softSat16(20480.0f) == 20480.0f && softSat16(-12345.5f) == -12345.5f && softSat16(0.0f) == 0.0f);
    // continuous at the knee with slope 1, odd, monotone, bounded below 1 (the 16 bit rails) for any input
    CHECK_NEAR(softSat(0.6251f) - softSat(0.625f), 0.0001, 1e-6);
    float last = -2.0f;
    bool mono = true, odd = true, bounded = true;
    for (float v = -50.0f; v <= 50.0f; v += 0.013f)
    {
        float y = softSat(v);
        mono = mono && y >= last;
        odd = odd && softSat(-v) == -y;
        bounded = bounded && fabsf(y) <= 1.0f;
        last = y;
    }
    CHECK(mono && odd && bounded);
    CHECK(softSat(2.0f) < 0.95f && softSat(2.0f) > 0.9f && softSat(1e9f) <= 1.0f);
    CHECK(fabsf(softSat16(65536.0f)) < 32767.0f && fabsf(softSat16(-1e7f)) <= 32768.0f);
    CHECK_NEAR(softSat16(30000.0f), softSat(30000.0f / 32768.0f) * 32768.0f, 0.5);
}

// The stock reverb (the real Reverb of core/libs/Audio/Polyverb) at 90 and 95 % size with loud sustained input: bounded, nothing near
// the rails, and no hard-clip steps. Mono input = both channels the same (the worst case for the tank).
static void test_stock_reverb_loud()
{
    static uint16_t b1[Reverb::kBuffer1Words], b2[Reverb::kBuffer2Words];
    static Reverb rv;
    const int blocks = 44100 * 8 / 128;
    for (float size : { 0.90f, 0.95f })
        for (float damp : { 0.2f, 0.5f })
            for (int src = 0; src < 5; src++) // sines 8000 / 20000 / 32000 at 80 Hz, 32000 at 1 kHz, full-scale noise
            {
                memset(b1, 0, sizeof(b1));
                memset(b2, 0, sizeof(b2));
                rv.Init(b1, b2);
                rv.SetInputGain(0.1f);
                rv.SetDiffusion(0.625f);
                rv.SetTime(size);
                rv.SetLP(damp);
                rv.SetAmount(1.0f);
                rv.SetPredelayLength(0.0f);
                rv.ResetState();
                int16_t l[128], r[128];
                long peak = 0, nearRailStep = 0, maxStep = 0;
                double e1 = 0, e2 = 0;
                int prev = 0;
                uint32_t rng = 99;
                for (int b = 0; b < blocks; b++)
                {
                    for (int i = 0; i < 128; i++)
                    {
                        long n = (long)b * 128 + i;
                        double a = src == 0 ? 8000 : (src == 1 ? 20000 : 32000);
                        double f = src == 3 ? 1000.0 : 80.0;
                        if (src == 4)
                        {
                            rng = rng * 1664525u + 1013904223u;
                            l[i] = r[i] = (int16_t)((int)((rng >> 8) % 65535) - 32767);
                        }
                        else
                            l[i] = r[i] = (int16_t)(a * sin(2.0 * 3.14159265358979 * f * n / 44100.0));
                    }
                    rv.Process(l, r, 128);
                    for (int i = 0; i < 128; i++)
                    {
                        long y = l[i];
                        long ay = labs(y);
                        if (ay > peak)
                            peak = ay;
                        if (b > 0 || i > 0)
                        {
                            long st = labs(y - prev);
                            if (st > maxStep)
                                maxStep = st;
                            if ((ay > 26000 || labs((long)prev) > 26000) && st > nearRailStep)
                                nearRailStep = st;
                        }
                        prev = (int)y;
                        if (b >= blocks / 2 && b < blocks * 3 / 4)
                            e1 += (double)y * y;
                        if (b >= blocks * 3 / 4)
                            e2 += (double)y * y;
                    }
                }
                printf("  stock reverb size %.2f damp %.1f src %d: peak %ld maxStep %ld nearRailStep %ld energy ratio %.2f\n", size, damp, src, peak, maxStep,
                       nearRailStep, e1 > 0 ? e2 / e1 : 0.0);
                // bounded: well inside the 16 bit range (the soft return stays below 31000), and settled (the energy of seconds 6-8 is not above that of seconds 4-6: no runaway)
                CHECK(peak < 31000);
                CHECK(e2 <= e1 * 1.2 + 1.0);
                if (src != 4)
                {
                    // a sine in gives a smooth wave out: no step near the rails (a hard clip or a wrapped conversion would jump there)
                    CHECK(nearRailStep < 3500);
                    CHECK(maxStep < 20000);
                }
                else
                {
                    CHECK(nearRailStep < 20000); // noise: steps are natural, but never a wrap from + to - rail
                }
            }
}

// shimmer mapping (plan 010 A3): percent <-> the ring's 0 .. 127, the default is exactly the former fixed value
static void test_shimmer_mapping()
{
    CHECK(kShimmerDefaultRaw == 32 && shimmerPercent(32) == 25 && shimmerFromPercent(25) == 32);
    CHECK(shimmerFromPercent(0) == 0 && shimmerFromPercent(100) == 127 && shimmerPercent(0) == 0 && shimmerPercent(127) == 100);
    CHECK(shimmerFromPercent(-5) == 0 && shimmerFromPercent(500) == 127 && shimmerPercent(255) == 100);
    uint8_t last = 0;
    bool mono = true, round = true;
    for (int p = 0; p <= 100; p++)
    {
        uint8_t raw = shimmerFromPercent(p);
        mono = mono && raw >= last;
        round = round && shimmerPercent(raw) == p; // every percent has its own raw value and reads back
        last = raw;
    }
    CHECK(mono && round);
    CHECK(jogShimmer(32, 1) == shimmerFromPercent(26) && jogShimmer(32, -1) == shimmerFromPercent(24));
    CHECK(jogShimmer(0, -3) == 0 && jogShimmer(127, 3) == 127 && jogShimmer(127, -1) < 127);
}

// the ring: shimmer follows its target in small steps (no click), the default is the old fixed behaviour
static void test_ring_shimmer()
{
    ringverb::RingReverb* a = new ringverb::RingReverb();
    ringverb::RingReverb* b = new ringverb::RingReverb();
    std::vector<int16_t> ra(ringverb::kRingSize, 0), rb(ringverb::kRingSize, 0), pa(2 * ringverb::kPreSize, 0), pb(2 * ringverb::kPreSize, 0);
    a->init(ringverb::PlainRing{ ra.data() }, pa.data());
    b->init(ringverb::PlainRing{ rb.data() }, pb.data());
    a->setParams(ringFade(0.7f), ringColor(0.4f), ringApGain(0.68f), 0);
    b->setParams(ringFade(0.7f), ringColor(0.4f), ringApGain(0.68f), 0);
    CHECK(a->shimmer() == 32);
    b->setShimmer(32); // explicitly the default: identical output
    std::vector<int16_t> la(128), lb(128), xa(128), xb(128);
    bool same = true;
    srand(5);
    for (int blk = 0; blk < 600; blk++)
    {
        for (int i = 0; i < 128; i++)
            la[i] = lb[i] = xa[i] = xb[i] = (int16_t)(rand() % 8001 - 4000);
        a->process(la.data(), xa.data(), 128);
        b->process(lb.data(), xb.data(), 128);
        same = same && la == lb && xa == xb;
    }
    CHECK(same);
    // a jump of the target: reached in 2 per block (48 blocks for 32 -> 127), monotone, never a step bigger than 2
    b->setShimmer(127);
    int blocks = 0, prev = b->shimmer();
    bool monotone = true;
    int maxJump = 0;
    while (b->shimmer() != 127 && blocks < 200)
    {
        for (int i = 0; i < 128; i++)
            lb[i] = xb[i] = 0;
        b->process(lb.data(), xb.data(), 128);
        monotone = monotone && b->shimmer() >= prev;
        if (b->shimmer() - prev > maxJump)
            maxJump = b->shimmer() - prev;
        prev = b->shimmer();
        blocks++;
    }
    CHECK(monotone && maxJump <= 2 && blocks >= 40 && blocks <= 60);
    b->setShimmer(500); // clamps
    for (int k = 0; k < 5; k++)
        b->process(lb.data(), xb.data(), 128);
    CHECK(b->shimmer() == 127);
    delete a;
    delete b;
}

// the ring at 95 %: the near-unity loop writes through the soft limit. Loud sustained input stays bounded with no rail contact
static void test_ring_loud_95()
{
    ringverb::RingReverb* r = new ringverb::RingReverb();
    std::vector<int16_t> ring(ringverb::kRingSize, 0), pre(2 * ringverb::kPreSize, 0);
    r->init(ringverb::PlainRing{ ring.data() }, pre.data());
    for (int shim : { 32, 127 })
        for (int src = 0; src < 4; src++)
        {
            std::fill(ring.begin(), ring.end(), 0);
            r->resetState();
            r->setShimmer(shim);
            r->setParams(ringFade(kReverbSizeCap), ringColor(0.3f), ringApGain(0.85f), 0);
            int16_t l[128], rr[128];
            long peak = 0, railHits = 0, nearStep = 0;
            int prev = 0;
            uint32_t rng = 3;
            for (int b = 0; b < 44100 * 8 / 128; b++)
            {
                for (int i = 0; i < 128; i++)
                {
                    long n = (long)b * 128 + i;
                    if (src == 3)
                    {
                        rng = rng * 1664525u + 1013904223u;
                        l[i] = rr[i] = (int16_t)((int)((rng >> 8) % 65535) - 32767);
                    }
                    else
                        l[i] = rr[i] = (int16_t)((src == 0 ? 8000 : (src == 1 ? 20000 : 32000)) * sin(2.0 * 3.14159265358979 * 80.0 * n / 44100.0));
                }
                r->process(l, rr, 128);
                for (int i = 0; i < 128; i++)
                {
                    long ay = labs(l[i]);
                    peak = ay > peak ? ay : peak;
                    railHits += (l[i] >= 32767 || l[i] <= -32768 || rr[i] >= 32767 || rr[i] <= -32768);
                    if (src != 3 && (ay > 26000 || labs((long)prev) > 26000) && labs(l[i] - prev) > nearStep)
                        nearStep = labs(l[i] - prev);
                    prev = l[i];
                }
            }
            printf("  ring 95%% shimmer %d src %d: peak %ld rail hits %ld near-rail step %ld\n", shim, src, peak, railHits, nearStep);
            CHECK(railHits == 0);
            if (src != 3)
                CHECK(nearStep < 6000);
        }
    delete r;
}

// the sidecar, version 2 (plan 010): shimmer and the track selection; a version 1 file loads with the defaults
static void test_sidecar_v2()
{
    CHECK(kVersion == 2);
    Params q;
    q.bw = 20;
    q.hp = 88; // above the old top
    q.type = RT_PLINKY;
    q.shim = shimmerFromPercent(80);
    q.sel = 0x8005;
    uint8_t buf[64];
    size_t n = q.save(buf, sizeof(buf));
    // v0.3.1: written as version 1 with the flag in reserved byte 6, so v0.2.x still loads bandwidth / highpass / type
    CHECK(n == (size_t)kFileMax && buf[4] == 1 && kWriteVersion == 1 && buf[seqsidecar::kHeader + 6] == kNewFieldsFlag);
    Params r;
    CHECK(r.load(buf, n) && r == q && r.hasSel && r.hp == 88 && r.shim == q.shim && r.sel == 0x8005);
    {
        // a v0.2.x style reader: version 1, the first three bytes only
        const uint8_t* pp = nullptr;
        size_t pn = 0;
        uint8_t pv = 0;
        CHECK(seqsidecar::frameRead(buf, n, kMagic, 1, &pp, &pn, &pv) == seqsidecar::FRAME_OK && pv == 1 && pp[0] == 20 && pp[2] == RT_PLINKY);
        // a version 1 file without the flag (v0.2.x) has garbage-free zeros: no selection read even if bytes 3..5 hold something
        uint8_t fl[8] = { 20, 10, RT_PLINKY, 100, 5, 0, 0, 0 };
        uint8_t fb[64];
        size_t fn = seqsidecar::frameWrite(fb, sizeof(fb), kMagic, 1, fl, 8);
        Params u;
        CHECK(u.load(fb, fn) && !u.hasSel && u.shim == kShimmerDefaultRaw && u.sel == 0);
        fl[6] = kNewFieldsFlag;
        fn = seqsidecar::frameWrite(fb, sizeof(fb), kMagic, 1, fl, 8);
        CHECK(u.load(fb, fn) && u.hasSel && u.shim == 100 && u.sel == 5);
        // and a v0.3 version 2 file still loads
        fl[6] = 0;
        fn = seqsidecar::frameWrite(fb, sizeof(fb), kMagic, 2, fl, 8);
        CHECK(u.load(fb, fn) && u.hasSel && u.shim == 100 && u.sel == 5);
    }
    CHECK(q.hash() != Params().hash());
    Params s = q;
    s.sel = 0x0005;
    CHECK(s.hash() != q.hash() && !(s == q));
    Params d;
    CHECK(d.isDefault() && d.shim == 32 && d.sel == 0);
    d.shim = 40;
    CHECK(!d.isDefault());
    d = Params();
    d.sel = 1;
    CHECK(!d.isDefault());

    // a version 1 file (what v0.2.x wrote: zeros after the first three bytes): defaults for the new fields, hasSel false
    uint8_t pl[8] = { 18, 60, RT_PLINKY, 0, 0, 0, 0, 0 };
    uint8_t old[64];
    size_t on = seqsidecar::frameWrite(old, sizeof(old), kMagic, 1, pl, 8);
    Params o;
    o.shim = 99;
    o.sel = 77;
    CHECK(o.load(old, on) && o.bw == 18 && o.hp == 60 && o.type == RT_PLINKY);
    CHECK(o.shim == kShimmerDefaultRaw && o.sel == 0 && !o.hasSel);
    // an old file with hp values up to the old top 72 stays valid, and so does the new top; beyond is the off default
    pl[1] = 72;
    on = seqsidecar::frameWrite(old, sizeof(old), kMagic, 1, pl, 8);
    CHECK(o.load(old, on) && o.hp == 72);
    pl[1] = kHpMax;
    on = seqsidecar::frameWrite(old, sizeof(old), kMagic, 2, pl, 8);
    CHECK(o.load(old, on) && o.hp == kHpMax);
    pl[1] = kHpMax + 1;
    on = seqsidecar::frameWrite(old, sizeof(old), kMagic, 2, pl, 8);
    CHECK(o.load(old, on) && o.hp == 0);
    // a shimmer byte out of range falls back to the default
    pl[1] = 0;
    pl[3] = 200;
    on = seqsidecar::frameWrite(old, sizeof(old), kMagic, 2, pl, 8);
    CHECK(o.load(old, on) && o.shim == kShimmerDefaultRaw && o.hasSel);
}

// the track selection rules (plan 010 A5)
static void test_track_selection()
{
    uint8_t st[16] = { 0 };
    st[0] = st[2] = st[15] = 1;
    CHECK(selFromStates(st, 16) == 0x8005);
    CHECK(selFromStates(st, 8) == 0x0005); // a smaller engine array
    uint8_t back[16];
    selToStates(0x8005, back, 16);
    CHECK(memcmp(st, back, 16) == 0);
    // a turn-off clears the engine's states: the stored mask puts them back (what Stop / leaving / project load do)
    memset(st, 0, sizeof(st));
    selToStates(0x8005, st, 16);
    CHECK(selFromStates(st, 16) == 0x8005);
    // project load: the first file after power-on supplies the selection, later loads keep it
    bool known = false;
    CHECK(selAfterLoad(&known, 0, false, 0) == 0 && !known);        // no file yet: nothing known
    CHECK(selAfterLoad(&known, 0, true, 0x0030) == 0x0030 && known); // first file read: persisted selection adopted
    CHECK(selAfterLoad(&known, 0x0030, true, 0x0001) == 0x0030);    // another project's file: the user's selection stays
    CHECK(selAfterLoad(&known, 0x0030, false, 0) == 0x0030);        // a project without the file: stays
    bool user = false; // the user toggled a track before any file: also known
    user = true;
    CHECK(selAfterLoad(&user, 0x0002, true, 0x00F0) == 0x0002);
    // an empty selection is a valid user setting and is kept as well
    known = true;
    CHECK(selAfterLoad(&known, 0, true, 0x00F0) == 0);
}

int main()
{
    test_track_selection();
    test_soft_sat();
    test_stock_reverb_loud();
    test_shimmer_mapping();
    test_ring_shimmer();
    test_ring_loud_95();
    test_sidecar_v2();
    test_bandpass_width();
    test_caps_and_soft_limit();
    test_highpass();
    test_mix_ramp_no_click();
    test_band_mix_top_octave();
    test_tan_approx();
    test_extremes_finite();
    test_control_smoothing();
    test_no_clicks_while_sweeping();
    test_jog_mappings();
    test_ring_mappings();
    test_sidecar();
    test_type_switch();
    test_ring_geometry();
    test_ring_silence_bypass();
    test_ring_split_equals_plain();
    test_ring_allpass_equals_original();
    test_ring_silence_and_decay();
    test_ring_stability();
    test_ring_predelay_and_levels();
    test_ring_determinism_and_params_in_flight();
    printf("seqfx: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
