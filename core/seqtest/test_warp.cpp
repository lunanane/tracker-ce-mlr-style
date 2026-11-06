// Sequencer tracks mod (plan 016 B) - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Host tests of Warp (time-stretch to the project tempo): the grain engine (warp_core.h) and the settings / sidecar
// (seqWarpLogic.h). Constant pitch under stretch, duration = bars at several tempos, unity ratio = bit-exact source, no
// discontinuities at the grain joins (also at loop edges), loop modes, the streamed-read window, tempo changes, the sidecar.
#include "seqWarpLogic.h"
#include "warp_core.h"
#include <chrono>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

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

static const double kPi = 3.14159265358979323846;
static const double kRate = 44100.0;

struct Src
{
    std::vector<int16_t> d;
    int32_t operator()(int32_t i) const { return d[(size_t)i]; }
};
static Src sine(double hz, int frames, double amp = 20000.0)
{
    Src s;
    s.d.resize((size_t)frames);
    for (int i = 0; i < frames; i++)
        s.d[(size_t)i] = (int16_t)lrint(amp * sin(2.0 * kPi * hz * i / kRate));
    return s;
}
static Src ramp(int frames)
{
    Src s;
    s.d.resize((size_t)frames);
    for (int i = 0; i < frames; i++)
        s.d[(size_t)i] = (int16_t)((int64_t)i * 30000 / frames);
    return s;
}

// renders `frames` output frames in blocks of 128 (ratio follows the target like the firmware does: one follow() per block)
template <class Rd>
static std::vector<int16_t> run(warp::Voice& v, Rd& rd, int frames, float pitch, float targetRatio, int* ended = nullptr)
{
    std::vector<int16_t> out;
    int16_t blk[128];
    for (int done = 0; done < frames; done += 128)
    {
        warp::follow(v, targetRatio);
        warp::render(v, blk, 128, pitch, rd);
        out.insert(out.end(), blk, blk + 128);
        if (!v.active)
        {
            if (ended)
                *ended = (int)out.size();
            break;
        }
    }
    return out;
}

// power centroid of out[from, from + n) in [lo, hi] Hz (1 Hz steps): the pitch of a signal whose amplitude is modulated by the
// grain cross-fades (the modulation adds symmetric sidebands, the centroid stays at the carrier)
static double centroidHz(const std::vector<int16_t>& o, int from, int n, double lo, double hi)
{
    double sm = 0, sf = 0;
    for (double f = lo; f <= hi; f += 1.0)
    {
        double w = 2.0 * kPi * f / kRate, c = 2.0 * cos(w), s1 = 0, s2 = 0;
        for (int i = 0; i < n; i++)
        {
            double s0 = o[(size_t)(from + i)] + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        double m = s1 * s1 + s2 * s2 - c * s1 * s2;
        sm += m;
        sf += m * f;
    }
    return sm > 0 ? sf / sm : 0;
}

static int zeroCrossHz(const std::vector<int16_t>& o, int from, int n)
{
    int c = 0;
    for (int i = from + 1; i < from + n; i++)
        if (o[(size_t)i - 1] < 0 && o[(size_t)i] >= 0)
            c++;
    return (int)lround(c * kRate / n);
}

static int maxDiff(const std::vector<int16_t>& o, int from, int to)
{
    int m = 0;
    for (int i = from + 1; i < to; i++)
    {
        int d = abs((int)o[(size_t)i] - (int)o[(size_t)i - 1]);
        if (d > m)
            m = d;
    }
    return m;
}

// ---------------------------------------------------------------- engine
static void test_unity_is_bit_exact()
{
    // ratio 1, pitch 1: every grain reads the source at the same alignment, the cross-fade of identical signals is the signal
    Src s = sine(1000.0, 60000);
    warp::Voice v;
    warp::start(v, warp::kOnce, 60000, 0, 0, 1.0f, 1.0f);
    std::vector<int16_t> o = run(v, s, 40000, 1.0f, 1.0f);
    int bad = 0;
    for (int i = 0; i < 40000; i++)
        if (o[(size_t)i] != s.d[(size_t)i])
            bad++;
    CHECK(bad == 0);
    // a note starts with its source transient, untouched (no fade-in)
    CHECK(o[0] == s.d[0] && o[1] == s.d[1] && o[200] == s.d[200]);
}

static void test_constant_pitch_under_stretch()
{
    const double f0 = 1000.0;
    Src s = sine(f0, 44100 * 4);
    const float ratios[] = { 0.5f, 0.7f, 0.85f, 1.0f, 1.17f, 1.4f, 2.0f };
    for (float r : ratios)
    {
        warp::Voice v;
        warp::start(v, warp::kLoopFwd, 44100 * 4, 0, 44100 * 4, r, 1.0f);
        std::vector<int16_t> o = run(v, s, 44100 * 2, 1.0f, r);
        double pk = centroidHz(o, 20000, 30000, 850, 1150);
        double zc = zeroCrossHz(o, 20000, 30000);
        printf("   ratio %.2f: centroid %.1f Hz, zero crossings %.0f Hz\n", r, pk, (double)zc);
        CHECK_NEAR(pk, f0, 5.0);       // 0.5 %
        CHECK_NEAR(zc, f0, 0.03 * f0); // zero crossings are noisier at the cross-fades
    }
    // tune on top: the pitch increment moves the pitch, the stretch does not
    const float pitches[] = { 0.5f, 1.0f, 1.5f, 2.0f };
    for (float p : pitches)
    {
        warp::Voice v;
        warp::start(v, warp::kLoopFwd, 44100 * 4, 0, 44100 * 4, 1.2f, p);
        std::vector<int16_t> o = run(v, s, 44100 * 2, p, 1.2f);
        double pk = centroidHz(o, 20000, 30000, f0 * p - 150, f0 * p + 150);
        CHECK_NEAR(pk, f0 * p, 0.006 * f0 * p + 1.0);
    }
}

static void test_alignment_keeps_phase()
{
    // without the phase alignment a steady tone is detuned by the grain hop (here -11 Hz at ratio 0.7); with it the join is
    // phase-coherent. Both are measured, the aligned one must be at least 10 times closer.
    const double f0 = 1000.0;
    Src s = sine(f0, 44100 * 4);
    for (float r : { 0.7f, 1.3f })
    {
        warp::Voice a, b;
        warp::start(a, warp::kLoopFwd, 44100 * 4, 0, 44100 * 4, r, 1.0f);
        warp::start(b, warp::kLoopFwd, 44100 * 4, 0, 44100 * 4, r, 1.0f);
        b.align = 0;
        std::vector<int16_t> oa = run(a, s, 44100 * 2, 1.0f, r), ob = run(b, s, 44100 * 2, 1.0f, r);
        double ea = fabs(centroidHz(oa, 20000, 30000, 850, 1150) - f0);
        double eb = fabs(centroidHz(ob, 20000, 30000, 850, 1150) - f0);
        printf("   ratio %.1f: detune %.2f Hz without alignment, %.2f Hz with\n", r, eb, ea);
        CHECK(eb > 4.0);
        CHECK(ea < 0.5 && ea * 10.0 < eb);
    }
    // the alignment never moves a grain start by more than the search range
    {
        Src d = sine(220.0, 44100 * 4);
        warp::Voice v;
        warp::start(v, warp::kLoopFwd, 44100 * 4, 0, 44100 * 4, 0.9f, 1.0f);
        int16_t blk[128];
        int maxShift = 0;
        for (int i = 0; i < 1500; i++)
        {
            warp::follow(v, 0.9f);
            warp::render(v, blk, 128, 1.0f, d);
            maxShift = abs((int)v.shift) > maxShift ? abs((int)v.shift) : maxShift;
        }
        CHECK(maxShift <= warp::kSearch + 1);
    }
    // noise / silence: no positive correlation to follow, the timeline is kept (no wild shifts, no crash)
    {
        Src z;
        z.d.assign(44100 * 2, 0);
        warp::Voice v;
        warp::start(v, warp::kLoopFwd, 44100 * 2, 0, 44100 * 2, 0.8f, 1.0f);
        int16_t blk[128];
        for (int i = 0; i < 400; i++)
        {
            warp::follow(v, 0.8f);
            warp::render(v, blk, 128, 1.0f, z);
            CHECK(v.shift == 0);
        }
    }
}

static void test_duration_matches_bars()
{
    const float bpms[] = { 60.f, 90.f, 100.f, 120.f, 128.f, 140.f, 160.f };
    const int bars = 4;
    // a 4 bar loop recorded at 120 BPM: 8 s
    const int frames = (int)(bars * 240.0 / 120.0 * seqwarp::kRate);
    Src s;
    s.d.assign((size_t)frames, 1000);
    for (float bpm : bpms)
    {
        float k = seqwarp::ratioK((float)frames, (uint8_t)bars);
        float r = seqwarp::ratioFor(k, bpm);
        warp::Voice v;
        warp::start(v, warp::kOnce, frames, 0, 0, r, 1.0f);
        int ended = 0;
        run(v, s, 44100 * 40, 1.0f, r, &ended);
        double want = bars * 240.0 / bpm * seqwarp::kRate;
        CHECK(ended > 0);
        // within the two fade-out hops of the last grain (+ one block of rounding)
        CHECK_NEAR(ended, want, 2.2 * warp::kHopMax + 128);
        if (fabs(ended - want) > 2.2 * warp::kHopMax + 128)
            printf("   %.0f BPM: %d frames, want %.0f\n", bpm, ended, want);
    }
    // the ratio belongs to bars: 8 bars of the same sample play twice as long
    {
        float r = seqwarp::ratioFor(seqwarp::ratioK((float)frames, 8), 120.0f);
        CHECK_NEAR(r, 0.5f, 1e-4);
    }
}

static void test_no_discontinuities()
{
    // a smooth source: the output may not step faster than the source's own steepest slope (+ the cross-fade of two phases)
    const double hz = 330.0, amp = 20000.0;
    Src s = sine(hz, 44100 * 5, amp);
    const int srcMax = (int)ceil(amp * 2.0 * kPi * hz / kRate);
    const float ratios[] = { 0.6f, 0.9f, 1.0f, 1.1f, 1.5f };
    for (float r : ratios)
    {
        warp::Voice v;
        warp::start(v, warp::kLoopFwd, 44100 * 5, 0, 44100 * 5, r, 1.0f);
        std::vector<int16_t> o = run(v, s, 44100 * 3, 1.0f, r);
        int md = maxDiff(o, 0, (int)o.size());
        CHECK(md <= srcMax * 13 / 10 + 64);
        if (md > srcMax * 13 / 10 + 64)
            printf("   ratio %.2f: max step %d, source max step %d\n", r, md, srcMax);
    }
    // across a loop edge: a loop of whole periods (441 Hz = 100 frames), edge every 20000 frames
    {
        Src l = sine(441.0, 20000, amp);
        const int lm = (int)ceil(amp * 2.0 * kPi * 441.0 / kRate);
        for (float r : { 0.8f, 1.0f, 1.3f })
        {
            warp::Voice v;
            warp::start(v, warp::kLoopFwd, 20000, 0, 20000, r, 1.0f);
            std::vector<int16_t> o = run(v, l, 44100 * 3, 1.0f, r);
            CHECK(v.active);
            CHECK(maxDiff(o, 0, (int)o.size()) <= lm * 13 / 10 + 64);
        }
        // ... and with a pitch ratio that makes the grains read across the edge at a fraction
        warp::Voice v;
        warp::start(v, warp::kLoopFwd, 20000, 0, 20000, 1.0f, 1.3f);
        std::vector<int16_t> o = run(v, l, 44100 * 3, 1.3f, 1.0f);
        CHECK(maxDiff(o, 0, (int)o.size()) <= (int)(lm * 1.3 * 1.3) + 64);
    }
    // the end of a one-shot fades out through the last grains: no step at the end
    {
        Src e = sine(330.0, 30000, amp);
        warp::Voice v;
        warp::start(v, warp::kOnce, 30000, 0, 0, 1.2f, 1.0f);
        int ended = 0;
        std::vector<int16_t> o = run(v, e, 44100 * 2, 1.0f, 1.2f, &ended);
        CHECK(ended > 0 && ended < 44100);
        CHECK(maxDiff(o, 0, ended) <= srcMax * 13 / 10 + 64);
        CHECK(abs(o[(size_t)ended - 1]) < 300); // faded out through the last grains
    }
}

static void test_loop_modes()
{
    const int N = 40000, L1 = 10000, L2 = 30000;
    Src r = ramp(N);
    // forward loop at ratio 1 / pitch 1: the output is the source until the edge, then the loop again, bit-exact
    {
        warp::Voice v;
        warp::start(v, warp::kLoopFwd, N, L1, L2, 1.0f, 1.0f);
        std::vector<int16_t> o = run(v, r, 90000, 1.0f, 1.0f);
        CHECK(v.active);
        int bad = 0;
        for (int i = 0; i < (int)o.size(); i++)
        {
            int src = i < L2 ? i : L1 + (i - L1) % (L2 - L1);
            if (abs((int)o[(size_t)i] - (int)r.d[(size_t)src]) > 1)
                bad++;
        }
        CHECK(bad == 0);
    }
    // one-shot with a tune up: the ramp is read twice as fast
    {
        warp::Voice v;
        warp::start(v, warp::kOnce, N, 0, 0, 1.0f, 2.0f);
        std::vector<int16_t> o = run(v, r, 5000, 2.0f, 1.0f);
        // right after a grain start (hop = 500 at this pitch) the window of the new grain is still ~0: the old grain alone reads
        // the ramp twice as fast, slope 2 * 30000 / 40000 per frame
        CHECK_NEAR(o[1010] - o[1000], 15, 3);
        CHECK_NEAR(o[2010] - o[2000], 15, 3);
    }
    // backward loop: forward to the loop end, then back; wraps at the loop start to the end again
    {
        warp::Voice v;
        warp::start(v, warp::kLoopBwd, N, L1, L2, 1.0f, 1.0f);
        std::vector<int16_t> o = run(v, r, 80000, 1.0f, 1.0f);
        CHECK(v.active);
        CHECK(o[L2 - 500] < o[L2 - 400]);        // rising before the end
        CHECK(o[L2 + 3000] < o[L2 + 2000]);      // falling after it
        CHECK(o[L2 + 3000] > r.d[(size_t)L1] - 50); // inside the loop
        // wraps at the start and keeps falling from the end
        int lo = 30000, hi = 0;
        for (int i = L2 + 21000; i < L2 + 24000 && i < (int)o.size(); i++)
        {
            lo = o[(size_t)i] < lo ? o[(size_t)i] : lo;
            hi = o[(size_t)i] > hi ? o[(size_t)i] : hi;
        }
        CHECK(hi > r.d[(size_t)L2 - 2000]); // back near the loop end after the wrap
    }
    // ping-pong: up, down, up
    {
        warp::Voice v;
        warp::start(v, warp::kPingPong, N, L1, L2, 1.0f, 1.0f);
        std::vector<int16_t> o = run(v, r, 90000, 1.0f, 1.0f);
        CHECK(v.active);
        CHECK(o[L2 - 500] < o[L2 - 400]);
        CHECK(o[L2 + 5000] < o[L2 + 4000]);
        int t = L2 + (L2 - L1) + 5000; // second turn at the loop start: rising again
        CHECK(o[t + 1000] > o[t]);
        // stays inside the loop
        int lo = 30000;
        for (int i = L2 + 100; i < (int)o.size(); i++)
            lo = o[(size_t)i] < lo ? o[(size_t)i] : lo;
        CHECK(lo >= r.d[(size_t)L1] - 80);
    }
    // a one-shot ends and reports it
    {
        warp::Voice v;
        warp::start(v, warp::kOnce, N, 0, 0, 1.0f, 1.0f);
        int ended = 0;
        run(v, r, 90000, 1.0f, 1.0f, &ended);
        CHECK(!v.active && ended >= N && ended < N + 2 * warp::kHopMax + 256);
    }
    // an invalid loop (< 2 frames) plays as a one-shot
    {
        warp::Voice v;
        warp::start(v, warp::kLoopFwd, N, 100, 100, 1.0f, 1.0f);
        CHECK(v.mode == warp::kOnce);
    }
}

// the streamed-read window: reads must stay inside what the stream manager keeps: the ring [playhead - lookbehind,
// playhead - lookbehind + ring) around the playhead the voice reported at the end of the previous block, and the loop-wrap cache
// (2048 frames at the loop start) for reads that wrapped
struct WindowCheck
{
    const Src* s;
    int32_t ringLo, ringHi;    // current window
    int32_t wrapLo, wrapHi;    // loop wrap cache (empty for a one-shot)
    int outside = 0;
    int32_t operator()(int32_t i)
    {
        bool inRing = i >= ringLo && i < ringHi;
        bool inWrap = i >= wrapLo && i < wrapHi;
        if (!inRing && !inWrap)
            outside++;
        return s->d[(size_t)i];
    }
};

static void test_stream_window()
{
    // geometry: a grain reaches 2 * hop * pitch frames from the older grain; the loop-wrap cache and the ring cover it
    for (float p : { 0.25f, 0.5f, 1.0f, 1.3f, 2.0f, 3.0f, 4.0f, 8.0f })
    {
        int32_t span = seqwarp::grainSpan(warp::hopForPitch(p), p);
        CHECK(span <= seqwarp::kLoopWrapFrames);
        CHECK(span + 4 * warp::kHopMax <= seqwarp::kRingFrames - seqwarp::kLookbehind);
    }
    // one-shot: simulate the ring around the reported playhead
    {
        Src s = sine(500.0, 44100 * 6);
        for (float p : { 0.5f, 1.0f, 2.0f })
            for (float r : { 0.7f, 1.0f, 1.4f })
            {
                warp::Voice v;
                warp::start(v, warp::kOnce, 44100 * 6, 0, 0, r, p);
                WindowCheck w;
                w.s = &s;
                w.wrapLo = w.wrapHi = 0;
                int32_t head = 0;
                int16_t blk[128];
                for (int b = 0; b < 1500 && v.active; b++)
                {
                    w.ringLo = head - seqwarp::kLookbehind;
                    w.ringHi = head - seqwarp::kLookbehind + seqwarp::kRingFrames;
                    warp::follow(v, r);
                    warp::render(v, blk, 128, p, w);
                    if (v.active)
                        head = warp::playhead(v);
                }
                CHECK(w.outside == 0);
                if (w.outside)
                    printf("   one-shot ratio %.1f pitch %.1f: %d reads outside the ring\n", r, p, w.outside);
            }
    }
    // forward loop that is NOT in the SDRAM pre-cache: grains that wrap read the loop-start cache
    {
        Src s = sine(500.0, 100000);
        const int L1 = 40000, L2 = 90000;
        for (float p : { 0.5f, 1.0f, 1.3f })
            for (float r : { 0.7f, 1.0f, 1.4f })
            {
                warp::Voice v;
                warp::start(v, warp::kLoopFwd, 100000, L1, L2, r, p);
                WindowCheck w;
                w.s = &s;
                w.wrapLo = L1;
                w.wrapHi = L1 + seqwarp::kLoopWrapFrames;
                int32_t head = 0;
                int16_t blk[128];
                for (int b = 0; b < 3000; b++)
                {
                    w.ringLo = head - seqwarp::kLookbehind;
                    w.ringHi = head - seqwarp::kLookbehind + seqwarp::kRingFrames;
                    warp::follow(v, r);
                    warp::render(v, blk, 128, p, w);
                    head = warp::playhead(v);
                    if (head >= L2) // the stream manager's cur beyond the loop end: it fetches on
                        head = head;
                }
                CHECK(v.active);
                // allow the first reads of the pass after the playhead jumped back to the loop start (ring still refilling)
                CHECK(w.outside <= 3 * 128);
                if (w.outside > 3 * 128)
                    printf("   loop ratio %.1f pitch %.1f: %d reads outside ring / wrap cache\n", r, p, w.outside);
            }
    }
    // MLR jump: a note that starts at a cue point is a new voice with its own timeline; reads stay near the start
    {
        Src s = sine(500.0, 200000);
        warp::Voice v;
        warp::start(v, warp::kOnce, 200000 - 123456, 0, 0, 1.1f, 1.0f); // region from the cue to the end
        WindowCheck w;
        w.s = &s; // here frame 0 of the voice = the cue
        w.wrapLo = w.wrapHi = 0;
        w.ringLo = -seqwarp::kLookbehind;
        w.ringHi = seqwarp::kRingFrames;
        Src cut;
        cut.d.assign(s.d.begin() + 123456, s.d.end());
        w.s = &cut;
        int16_t blk[128];
        warp::render(v, blk, 128, 1.0f, w);
        CHECK(w.outside == 0);
    }
}

static void test_tempo_change()
{
    const double hz = 330.0, amp = 20000.0;
    Src s = sine(hz, 44100 * 20, amp);
    const int srcMax = (int)ceil(amp * 2.0 * kPi * hz / kRate);
    // ratio steps from 1.0 to 1.3 (a tempo jump): the ratio follows smoothly, the audio has no step, the pitch stays
    warp::Voice v;
    warp::start(v, warp::kLoopFwd, 44100 * 20, 0, 44100 * 20, 1.0f, 1.0f);
    std::vector<int16_t> o;
    int16_t blk[128];
    float prev = v.ratio, maxStep = 0;
    int blocksTo95 = -1;
    for (int b = 0; b < 1200; b++)
    {
        float target = b < 400 ? 1.0f : 1.3f;
        warp::follow(v, target);
        float st = fabsf(v.ratio - prev);
        maxStep = st > maxStep ? st : maxStep;
        prev = v.ratio;
        if (b >= 400 && blocksTo95 < 0 && fabsf(v.ratio - 1.3f) < 0.015f)
            blocksTo95 = b - 400;
        warp::render(v, blk, 128, 1.0f, s);
        o.insert(o.end(), blk, blk + 128);
    }
    CHECK(maxStep < 0.035f);            // never a jump: at most 10 % of the step per block
    CHECK(blocksTo95 > 5 && blocksTo95 < 40); // ~ 30 ms .. 120 ms
    CHECK(fabsf(v.ratio - 1.3f) < 1e-3f);
    CHECK(maxDiff(o, 0, (int)o.size()) <= srcMax * 13 / 10 + 64);
    CHECK_NEAR(centroidHz(o, 90000, 20000, 250, 410), hz, 3.0);
    // the timeline moved by the integral of the ratio (the sound follows the tempo, it does not run away)
    // 1200 blocks: 400 at 1.0, then the one-pole approach to 1.3
    double integ = 0;
    {
        double r = 1.0;
        for (int b = 0; b < 1200; b++)
        {
            double t = b < 400 ? 1.0 : 1.3;
            double d = t - r;
            r = (fabs(d) < 0.0005) ? t : r + d * 0.1;
            integ += r * 128;
        }
    }
    // loop of 20 s: no wrap yet; the position the newest grain started at tracks the integral within a grain
    CHECK_NEAR(v.tl, integ, 2.5 * warp::kHopMax * 1.3);
}

struct CountingReader
{
    const Src* s;
    long reads = 0;
    int32_t operator()(int32_t i)
    {
        reads++;
        return s->d[(size_t)i];
    }
};

static void test_cpu_estimate()
{
    // Cost model for the Cortex-M4F (plan 009 P: a stock voice = 22 000 cycles per 128-frame block = 4.3 % of 522 448; its
    // sample reads from SDRAM are ~22 cycles each, 256 reads = 5.6 k). Counted here: the source reads per output frame. Everything
    // else per frame is ~55 cycles of integer / FPU work (2 interpolations, the cross-fade, 2 position updates); the alignment
    // search adds ~2 cycles per MAC (about 5 k MACs per hop).
    Src s = sine(440.0, 44100 * 4);
    for (float ratio : { 1.0f, 1.13f, 0.8f })
    {
        warp::Voice v;
        warp::start(v, warp::kLoopFwd, 44100 * 4, 0, 44100 * 4, ratio, 1.0f);
        CountingReader cr;
        cr.s = &s;
        int16_t blk[128];
        const int blocks = 2000;
        for (int b = 0; b < blocks; b++)
            warp::render(v, blk, 128, 1.0f, cr);
        double readsPerFrame = (double)cr.reads / (blocks * 128.0);
        const double hopsPerFrame = 1.0 / warp::kHopMax;
        const double macs = (ratio == 1.0f) ? 0.0 : 5000.0 * hopsPerFrame; // no search at ratio == pitch
        const double cyc = (readsPerFrame * 22.0 + 55.0 + macs * 2.0) * 128.0;
        printf("warp ratio %.2f: %.2f source reads/frame, ~%.0f cycles/block = %.1f %% of the audio block\n", ratio, readsPerFrame,
               cyc, 100.0 * cyc / 522448.0);
        CHECK(readsPerFrame <= (ratio == 1.0f ? 4.05 : 5.2));
        CHECK(cyc <= 0.065 * 522448.0); // a warped voice's own cost stays under ~6.5 % (a stock voice is 4.3 % in total)
    }
    // host timing, informational only (depends on the machine's load)
    warp::Voice v;
    warp::start(v, warp::kLoopFwd, 44100 * 4, 0, 44100 * 4, 1.13f, 1.0f);
    int16_t blk[128];
    volatile int sink = 0;
    const int blocks = 20000;
    auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; b++)
    {
        warp::render(v, blk, 128, 1.0f, s);
        sink += blk[3];
    }
    auto t1 = std::chrono::steady_clock::now();
    printf("warp host cost: %.0f ns/block\n", std::chrono::duration<double, std::nano>(t1 - t0).count() / blocks);
    (void)sink;
    // state size: the voice struct in internal RAM, once per voice (8 voices) + the window table + the shared search buffer
    printf("warp::Voice = %u bytes, window table = %u bytes, search buffer = %u bytes\n", (unsigned)sizeof(warp::Voice),
           (unsigned)(sizeof(uint16_t) * (warp::kWinSteps + 1)), (unsigned)sizeof(int32_t) * (2 * warp::kSearch + 1024) / warp::kDec);
    CHECK(sizeof(warp::Voice) <= 96);
}

// ---------------------------------------------------------------- settings / sidecar
static void test_bars_and_ratio()
{
    // guess: the sample length at the tempo, nearest of 1 / 2 / 4 / 8 / 16 bars
    const float rate = seqwarp::kRate;
    CHECK(seqwarp::guessBars(8.0f * rate, 120.0f) == 4);   // 8 s at 120 = 4 bars
    CHECK(seqwarp::guessBars(4.0f * rate, 120.0f) == 2);
    CHECK(seqwarp::guessBars(2.0f * rate, 120.0f) == 1);
    CHECK(seqwarp::guessBars(16.0f * rate, 120.0f) == 8);
    CHECK(seqwarp::guessBars(32.0f * rate, 120.0f) == 16);
    CHECK(seqwarp::guessBars(100.0f * rate, 120.0f) == 16); // clamps
    CHECK(seqwarp::guessBars(0.3f * rate, 120.0f) == 1);
    CHECK(seqwarp::guessBars(0.0f, 120.0f) == 1);
    CHECK(seqwarp::guessBars(8.0f * rate, 90.0f) == 4);    // 3 bars -> nearer 4 than 2 (in octaves)
    CHECK(seqwarp::guessBars(8.0f * rate, 75.0f) == 2);    // 2.5 bars -> 2
    CHECK(seqwarp::guessBars(8.0f * rate, 160.0f) == 4);   // 5.33 bars -> 4 (nearer in octaves)
    // resolve
    CHECK(seqwarp::resolveBars(0, 8.0f * rate, 120.0f) == 4);
    CHECK(seqwarp::resolveBars(3, 8.0f * rate, 120.0f) == 3);
    CHECK(seqwarp::resolveBars(200, 8.0f * rate, 120.0f) == seqwarp::kBarsMax);
    // ratio
    float k = seqwarp::ratioK(8.0f * rate, 4);
    CHECK_NEAR(seqwarp::ratioFor(k, 120.0f), 1.0f, 1e-4);
    CHECK_NEAR(seqwarp::ratioFor(k, 144.0f), 1.2f, 1e-4);
    CHECK_NEAR(seqwarp::ratioFor(k, 60.0f), 0.5f, 1e-4);
    CHECK_NEAR(seqwarp::ratioFor(k, 10.0f), seqwarp::kRatioMin, 1e-6); // clamped
    CHECK_NEAR(seqwarp::ratioFor(k, 999.0f), seqwarp::kRatioMax, 1e-6);
    // stepping
    CHECK(seqwarp::stepBars(0, 1) == 1 && seqwarp::stepBars(0, -1) == 0);
    CHECK(seqwarp::stepBars(32, 1) == 32 && seqwarp::stepBars(5, -9) == 0 && seqwarp::stepBars(5, 2) == 7);
}

static void test_sidecar()
{
    seqwarp::Params a;
    CHECK(a.isDefault());
    a.it[0].on = 1;
    a.it[0].bars = 4;
    a.it[7].on = 1;
    a.it[47].bars = 32;
    CHECK(!a.isDefault());
    uint8_t buf[seqwarp::kFileMax + 8];
    memset(buf, 0xCC, sizeof(buf));
    size_t n = a.save(buf, sizeof(buf));
    CHECK(n == (size_t)seqwarp::kFileMax);
    CHECK(a.save(buf, n - 1) == 0); // buffer too small
    seqwarp::Params b;
    bool newer = true;
    CHECK(b.load(buf, n, &newer) && !newer);
    CHECK(a == b);
    CHECK(a.hash() == b.hash());
    b.it[3].on = 1;
    CHECK(a.hash() != b.hash());
    // the file is a frame with its own magic
    CHECK(memcmp(buf, "SWRP", 4) == 0 && buf[4] == seqwarp::kVersion);
    // a corrupt file: bad CRC, wrong magic, too short -> not loaded, nothing changed, not "newer"
    {
        seqwarp::Params c;
        c.it[1].on = 1;
        uint8_t bad[seqwarp::kFileMax];
        memcpy(bad, buf, n);
        bad[20] ^= 0x55;
        CHECK(!c.load(bad, n, &newer) && !newer && c.it[1].on == 1);
        memcpy(bad, buf, n);
        bad[0] = 'X';
        CHECK(!c.load(bad, n, &newer));
        CHECK(!c.load(buf, 5, &newer));
    }
    // a newer version is left alone
    {
        uint8_t nb[seqwarp::kFileMax];
        uint8_t pl[seqwarp::kPayload];
        memset(pl, 0, sizeof(pl));
        size_t m = seqsidecar::frameWrite(nb, sizeof(nb), "SWRP", 2, pl, sizeof(pl));
        seqwarp::Params c;
        c.it[2].on = 1;
        CHECK(!c.load(nb, m, &newer) && newer && c.it[2].on == 1);
    }
    // an older / shorter payload: the instruments it does not carry are off
    {
        uint8_t nb[seqwarp::kFileMax];
        uint8_t pl[8] = { 1, 4, 0, 0, 1, 2, 0, 0 };
        size_t m = seqsidecar::frameWrite(nb, sizeof(nb), "SWRP", 1, pl, sizeof(pl));
        seqwarp::Params c;
        c.it[30].on = 1;
        CHECK(c.load(nb, m, &newer));
        CHECK(c.it[0].on == 1 && c.it[0].bars == 4 && c.it[2].on == 1 && c.it[2].bars == 2);
        CHECK(c.it[30].on == 0 && c.it[47].on == 0);
    }
    // a longer payload (a later version of the same file): read as far as it is known
    {
        uint8_t nb[seqwarp::kFileMax + 64];
        uint8_t pl[seqwarp::kPayload + 40];
        memset(pl, 0, sizeof(pl));
        pl[0] = 1;
        pl[1] = 8;
        pl[seqwarp::kPayload] = 77;
        size_t m = seqsidecar::frameWrite(nb, sizeof(nb), "SWRP", 1, pl, sizeof(pl));
        seqwarp::Params c;
        CHECK(c.load(nb, m, &newer) && c.it[0].on == 1 && c.it[0].bars == 8);
    }
    // wild values fall back to their defaults
    {
        uint8_t nb[seqwarp::kFileMax];
        uint8_t pl[seqwarp::kPayload];
        memset(pl, 0, sizeof(pl));
        pl[0] = 7;   // on must be 0 / 1
        pl[1] = 200; // bars <= 32
        pl[2] = 1;
        pl[3] = 33;
        size_t m = seqsidecar::frameWrite(nb, sizeof(nb), "SWRP", 1, pl, sizeof(pl));
        seqwarp::Params c;
        CHECK(c.load(nb, m, &newer));
        CHECK(c.it[0].on == 0 && c.it[0].bars == 0 && c.it[1].on == 1 && c.it[1].bars == 0);
    }
    // the file size is small
    CHECK(seqwarp::kFileMax == 106);
}

int main()
{
    test_unity_is_bit_exact();
    test_constant_pitch_under_stretch();
    test_alignment_keeps_phase();
    test_duration_matches_bars();
    test_no_discontinuities();
    test_loop_modes();
    test_stream_window();
    test_tempo_change();
    test_cpu_estimate();
    test_bars_and_ratio();
    test_sidecar();
    printf("seqwarp: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
