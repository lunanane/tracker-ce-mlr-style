// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Host tests of the synth voice engine (plan 009 S): the parameter table and the sidecar plinky.bin, the pitch maths, stability over
// the whole parameter space, silence / idle behaviour (no denormals), voice stealing, parameter smoothing and note changes without
// clicks, the anti-aliasing of the saw, the event queue, and a cost measurement (printed, not asserted).
#include "plinky_voice.h"
#include <chrono>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

using namespace plinkyvoice;

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

// renders `blocks` blocks, appends the left channel (and the right one when r != nullptr)
static void run(Engine& e, int blocks, std::vector<int16_t>& l, std::vector<int16_t>* r = nullptr)
{
    int16_t bl[kBlock], br[kBlock];
    for (int b = 0; b < blocks; b++)
    {
        if (!e.render(bl, br))
        {
            memset(bl, 0, sizeof(bl));
            memset(br, 0, sizeof(br));
        }
        l.insert(l.end(), bl, bl + kBlock);
        if (r)
            r->insert(r->end(), br, br + kBlock);
    }
}

static bool voiceFinite(const Voice& v)
{
    for (int i = 0; i < 4; i++)
        if (!(fabsf(v.y[i]) < 1.0e5f))
            return false;
    return isfinite(v.env) && isfinite(v.pitch) && isfinite(v.g) && isfinite(v.k);
}

static void setOpenMono(Engine& e)
{
    e.setParam(P_WIDTH, 0);
    e.setParam(P_DETUNE, 0);
    e.setParam(P_CUTOFF, 100);
    e.setParam(P_RESO, 0);
    e.setParam(P_ATTACK, 0);
    e.setParam(P_SUSTAIN, 100);
    e.setParam(P_SHAPE, 0);
}

static void test_params_and_sidecar()
{
    CHECK(P_COUNT == 21);
    CHECK((int)P_COUNT <= (int)kPayload);
    for (int i = 0; i < P_COUNT; i++)
    {
        const ParamInfo& pi = paramInfo(i);
        CHECK(pi.min <= pi.def && pi.def <= pi.max);
        CHECK(pi.name && strlen(pi.name) > 0 && strlen(pi.name) <= 9);
        CHECK(clampParam(i, -5) == pi.min);
        CHECK(clampParam(i, 1000) == pi.max);
    }
    Params a;
    CHECK(a.isDefault());
    CHECK(a.v[P_CHANNEL] == 0); // off by default: a project that never used it is unchanged
    a.v[P_CUTOFF] = 12;
    a.v[P_INTERVAL] = 19;
    CHECK(!a.isDefault());
    uint8_t buf[64];
    size_t n = a.save(buf, sizeof(buf));
    CHECK(n == (size_t)kFileMax);
    Params b;
    bool newer = true;
    CHECK(b.load(buf, n, &newer) && !newer);
    CHECK(a == b);
    CHECK(a.hash() == b.hash());
    // too small buffer
    CHECK(a.save(buf, 10) == 0);
    // bad CRC
    buf[10] ^= 0x55;
    Params c;
    c.v[P_CUTOFF] = 3;
    CHECK(!c.load(buf, n));
    CHECK(c.v[P_CUTOFF] == 3);
    buf[10] ^= 0x55;
    // out of range value falls back to its default
    a.save(buf, sizeof(buf));
    buf[seqsidecar::kHeader + P_VOICES] = 9;
    uint16_t crc = seqsidecar::crc16(buf, seqsidecar::kHeader + kPayload);
    buf[seqsidecar::kHeader + kPayload] = (uint8_t)(crc & 0xFF);
    buf[seqsidecar::kHeader + kPayload + 1] = (uint8_t)(crc >> 8);
    Params d;
    CHECK(d.load(buf, n));
    CHECK(d.v[P_VOICES] == paramInfo(P_VOICES).def);
    CHECK(d.v[P_CUTOFF] == 12);
    // a newer version is left alone
    a.save(buf, sizeof(buf));
    buf[4] = 9;
    crc = seqsidecar::crc16(buf, seqsidecar::kHeader + kPayload);
    buf[seqsidecar::kHeader + kPayload] = (uint8_t)(crc & 0xFF);
    buf[seqsidecar::kHeader + kPayload + 1] = (uint8_t)(crc >> 8);
    Params f;
    bool nw = false;
    CHECK(!f.load(buf, n, &nw) && nw);
}

static void test_pitch_maths()
{
    for (float x = -20.0f; x <= 20.0f; x += 0.137f)
    {
        double ref = pow(2.0, (double)x);
        CHECK(fabs((double)exp2fast(x) / ref - 1.0) < 2.5e-5);
    }
    CHECK(exp2fast(-1000.0f) > 0.0f && exp2fast(1000.0f) < 1.0e37f);
    // A4 = 440 Hz: dphase * fs / 2^32
    CHECK_NEAR((double)semiToDphase(69.0f) * 44100.0 / 4294967296.0, 440.0, 0.05);
    CHECK_NEAR((double)semiToDphase(81.0f) * 44100.0 / 4294967296.0, 880.0, 0.1);
    CHECK(semiToDphase(-200.0f) >= 65536);
    CHECK(semiToDphase(400.0f) > 0);
    // measured pitch of a rendered saw: the strongest frequency within 8 % of the expected one (Goertzel scan, 0.02 % steps)
    for (int note = 36; note <= 96; note += 12)
    {
        Engine e;
        setOpenMono(e);
        e.noteOn((uint8_t)note, 100);
        std::vector<int16_t> o;
        run(e, 200, o);
        const double f = pow(2.0, (note - 69) / 12.0) * 440.0;
        const int N = 8192;
        double best = 0, bestF = 0;
        for (double ff = f * 0.92; ff < f * 1.08; ff *= 1.0002)
        {
            double re = 0, im = 0;
            for (int n = 0; n < N; n++)
            {
                double w = 0.5 - 0.5 * cos(2.0 * 3.14159265358979 * n / N);
                double a = 2.0 * 3.14159265358979 * ff * n / 44100.0;
                double x = o[o.size() - N + n] * w;
                re += x * cos(a);
                im += x * sin(a);
            }
            double m = re * re + im * im;
            if (m > best)
            {
                best = m;
                bestF = ff;
            }
        }
        CHECK_NEAR(bestF / f, 1.0, 0.002);
    }
}

static void test_stability_grid()
{
    int cases = 0;
    const int shapes[] = { 0, 1, 50, 100 };
    const int resos[] = { 0, 50, 100 };
    const int cuts[] = { 0, 60, 100 };
    const int widths[] = { 0, 100 };
    const uint8_t notes[] = { 0, 24, 60, 100, 127 };
    for (int sh : shapes)
        for (int rs : resos)
            for (int ct : cuts)
                for (int wd : widths)
                {
                    Engine e;
                    e.setParam(P_SHAPE, sh);
                    e.setParam(P_RESO, rs);
                    e.setParam(P_CUTOFF, ct);
                    e.setParam(P_WIDTH, wd);
                    e.setParam(P_NOISE, 100);
                    e.setParam(P_DRIVE, 100);
                    e.setParam(P_DETUNE, 100);
                    e.setParam(P_INTERVAL, 24);
                    e.setParam(P_ATTACK, 0);
                    e.setParam(P_LFO_DEPTH, 100);
                    e.setParam(P_LFO_RATE, 100);
                    e.setParam(P_LFO_DEST, cases % 3);
                    e.setParam(P_GLIDE, 30);
                    for (uint8_t n : notes)
                        e.noteOn(n, 127);
                    std::vector<int16_t> l, r;
                    run(e, 200, l, &r);
                    int mx = 0;
                    for (size_t i = 0; i < l.size(); i++)
                        mx = std::max(mx, std::max(abs((int)l[i]), abs((int)r[i])));
                    CHECK(mx <= 32767);
                    for (int i = 0; i < kMaxVoices; i++)
                        CHECK(voiceFinite(e.voice(i)));
                    e.allNotesOff();
                    std::vector<int16_t> t;
                    run(e, 40, t);
                    cases++;
                }
    CHECK(cases == 4 * 3 * 3 * 2);
}

static void test_silence_and_idle()
{
    Engine e;
    int16_t l[kBlock], r[kBlock];
    CHECK(!e.isActive());
    CHECK(!e.render(l, r));
    e.setParam(P_RELEASE, 0);
    e.noteOn(60, 100);
    CHECK(e.isActive());
    std::vector<int16_t> o;
    run(e, 20, o);
    CHECK(e.activeVoices() == 1);
    e.noteOff(60);
    run(e, 60, o); // release 5 ms: gone long before
    CHECK(e.activeVoices() == 0);
    CHECK(!e.isActive());
    CHECK(!e.render(l, r));
    // the idle voice holds no state that could be a denormal
    const Voice& v = e.voice(0);
    for (int i = 0; i < 4; i++)
        CHECK(v.y[i] == 0.0f);
    CHECK(v.env == 0.0f && v.g == 0.0f && v.k == 0.0f);
    // the longest release: the tail dies out (bounded time) and never goes denormal on the way
    Engine e2;
    e2.setParam(P_RELEASE, 100);
    e2.noteOn(48, 127);
    run(e2, 100, o);
    e2.noteOff(48);
    bool denorm = false;
    int blocks = 0;
    while (e2.isActive() && blocks < 20000)
    {
        run(e2, 1, o);
        blocks++;
        const Voice& vv = e2.voice(0);
        for (int i = 0; i < 4; i++)
            if (vv.y[i] != 0.0f && fabsf(vv.y[i]) < 1.2e-38f)
                denorm = true;
    }
    CHECK(!denorm);
    CHECK(blocks < 20000);
    CHECK(!e2.isActive());
    // sustain 0: a held note rings out and the voice frees itself
    Engine e3;
    e3.setParam(P_SUSTAIN, 0);
    e3.setParam(P_DECAY, 20);
    e3.noteOn(60, 100);
    run(e3, 3000, o);
    CHECK(e3.activeVoices() == 0);
}

static void test_voice_stealing()
{
    Engine e;
    e.setParam(P_RELEASE, 100);
    e.setParam(P_ATTACK, 0);
    std::vector<int16_t> o;
    for (uint8_t n = 60; n < 64; n++)
    {
        e.noteOn(n, 100);
        run(e, 4, o);
    }
    CHECK(e.activeVoices() == 4);
    // a fifth note steals the oldest (60), never silences the others
    e.noteOn(70, 100);
    run(e, 4, o);
    CHECK(e.activeVoices() == 4);
    int has[128] = {};
    for (int i = 0; i < kMaxVoices; i++)
        has[e.voice(i).note]++;
    CHECK(has[60] == 0 && has[61] == 1 && has[62] == 1 && has[63] == 1 && has[70] == 1);
    // the same note again re-uses its voice
    e.noteOn(62, 100);
    run(e, 4, o);
    CHECK(e.activeVoices() == 4);
    int n62 = 0;
    for (int i = 0; i < kMaxVoices; i++)
        n62 += e.voice(i).note == 62;
    CHECK(n62 == 1);
    // a releasing voice is stolen before a held one
    e.noteOff(63);
    run(e, 4, o);
    e.noteOn(80, 100);
    run(e, 2, o);
    memset(has, 0, sizeof(has));
    for (int i = 0; i < kMaxVoices; i++)
        has[e.voice(i).note]++;
    CHECK(has[63] == 0 && has[80] == 1 && has[61] == 1 && has[70] == 1 && has[62] == 1);
    // Voices = 2: never more than 2 sound, the extra ones are released
    Engine e2;
    e2.setParam(P_VOICES, 2);
    for (uint8_t n = 60; n < 64; n++)
        e2.noteOn(n, 100);
    run(e2, 6, o);
    CHECK(e2.activeVoices() == 2);
    e2.setParam(P_VOICES, 1);
    run(e2, 6, o);
    int held = 0;
    for (int i = 0; i < kMaxVoices; i++)
        held += e2.voice(i).gate;
    CHECK(held == 1);
    // hammer: random notes never leave more than 4 voices, never crash, always finite
    Engine e3;
    srand(7);
    for (int i = 0; i < 4000; i++)
    {
        int t = rand() % 6;
        if (t < 3)
            e3.noteOn((uint8_t)(24 + rand() % 80), (uint8_t)(1 + rand() % 127));
        else if (t < 5)
            e3.noteOff((uint8_t)(24 + rand() % 80));
        else
            e3.pitchBend((int16_t)(rand() % 16384));
        run(e3, 1, o);
        CHECK(e3.activeVoices() <= kMaxVoices);
    }
    for (int i = 0; i < kMaxVoices; i++)
        CHECK(voiceFinite(e3.voice(i)));
}

// largest sample to sample step
static int maxStep(const std::vector<int16_t>& o, size_t from, size_t to)
{
    int m = 0;
    for (size_t i = from + 1; i < to && i < o.size(); i++)
        m = std::max(m, abs((int)o[i] - (int)o[i - 1]));
    return m;
}

static void test_no_clicks()
{
    // Reference: a steady chord. Then the same chord with a parameter jumping to its extreme in mid-block: the largest step may not
    // grow by more than a small factor (the control rate smoothing and the per block ramps remove the jump).
    const int params[] = { P_LEVEL, P_CUTOFF, P_RESO, P_NOISE, P_DETUNE, P_WIDTH, P_DRIVE, P_INTERVAL, P_SHAPE };
    for (int pi : params)
    {
        for (int dir = 0; dir < 2; dir++)
        {
            const int from = 40, to = dir ? paramInfo(pi).max : paramInfo(pi).min;
            Engine r0, r1, e;
            Engine* es[3] = { &r0, &r1, &e };
            for (Engine* x : es)
            {
                x->setParam(P_SHAPE, 20);
                x->setParam(P_ATTACK, 0);
                x->setParam(P_CUTOFF, 70);
                x->setParam(P_WIDTH, 80);
                x->setParam(pi, pi == P_LEVEL ? 70 : (pi == P_INTERVAL ? 12 : from));
            }
            r1.setParam(pi, to);
            for (Engine* x : es)
            {
                x->noteOn(48, 100);
                x->noteOn(55, 100);
                x->noteOn(64, 100);
            }
            std::vector<int16_t> a, b, c;
            run(r0, 100, a);
            run(r1, 100, c);
            run(e, 50, b);
            e.setParam(pi, to);
            run(e, 50, b);
            int limit = std::max(maxStep(a, 3000, a.size()), maxStep(c, 3000, c.size()));
            int sNew = maxStep(b, 3000, b.size());
            if (sNew > limit * 3 / 2 + 400)
                printf("  click candidate: param %d dir %d: step %d vs %d\n", pi, dir, sNew, limit);
            CHECK(sNew <= limit * 3 / 2 + 400);
        }
    }
    // a note on while others sustain, and a steal, make no step bigger than the same note on a free voice
    {
        Engine e, ref;
        e.setParam(P_ATTACK, 0);
        ref.setParam(P_ATTACK, 0);
        for (uint8_t n = 60; n < 64; n++)
            e.noteOn(n, 100);
        std::vector<int16_t> o, rr;
        run(e, 80, o);
        size_t mark = o.size();
        e.noteOn(72, 100); // steals the oldest
        run(e, 40, o);
        int after = maxStep(o, mark, o.size());
        ref.noteOn(61, 100);
        ref.noteOn(62, 100);
        ref.noteOn(63, 100);
        run(ref, 80, rr);
        size_t rmark = rr.size();
        ref.noteOn(72, 100); // the same new note on a free voice
        run(ref, 40, rr);
        int base = maxStep(rr, rmark, rr.size());
        if (after > base * 3 / 2 + 500)
            printf("  steal step %d vs free-voice note on %d\n", after, base);
        CHECK(after <= base * 3 / 2 + 500);
    }
    // note on from silence and note off: the first / last block ramps from / to zero
    {
        Engine e;
        e.setParam(P_ATTACK, 0);
        e.setParam(P_RELEASE, 0);
        e.noteOn(60, 127);
        std::vector<int16_t> o;
        run(e, 1, o);
        CHECK(abs((int)o[0]) < 400);
        run(e, 40, o);
        e.noteOff(60);
        std::vector<int16_t> t;
        run(e, 30, t);
        CHECK(t.back() == 0 && abs((int)t[t.size() - kBlock]) < 3000);
        CHECK(maxStep(t, 0, t.size()) < 12000);
    }
}

// share of the spectrum energy that is neither the fundamental nor one of its harmonics
static double aliasShare(const std::vector<float>& x, double f0)
{
    const int N = 4096;
    double total = 0, bad = 0;
    for (int k = 1; k < N / 2; k++)
    {
        double re = 0, im = 0;
        for (int n = 0; n < N; n++)
        {
            double w = 0.5 - 0.5 * cos(2.0 * 3.14159265358979 * n / N);
            double ph = 2.0 * 3.14159265358979 * k * n / N;
            re += x[n] * w * cos(ph);
            im -= x[n] * w * sin(ph);
        }
        double m = re * re + im * im;
        double hz = k * 44100.0 / N;
        double h = hz / f0;
        double dist = fabs(h - floor(h + 0.5)) * f0; // Hz from the nearest harmonic
        total += m;
        if (dist > 3.0 * 44100.0 / N)
            bad += m;
    }
    return bad / total;
}

static void test_antialiasing()
{
    const double f0 = 2093.0045; // C7
    Engine e;
    setOpenMono(e);
    e.setParam(P_LEVEL, 50);
    e.noteOn(96, 127);
    std::vector<int16_t> o;
    run(e, 60, o);
    std::vector<float> x(4096), naive(4096);
    for (int i = 0; i < 4096; i++)
        x[i] = o[o.size() - 4096 + i] * (1.0f / 32768.0f);
    // naive saw of the same pitch for reference
    double ph = 0.1, dp = f0 / 44100.0;
    for (int i = 0; i < 4096; i++)
    {
        naive[i] = (float)(2.0 * ph - 1.0);
        ph += dp;
        if (ph >= 1.0)
            ph -= 1.0;
    }
    double a = aliasShare(x, f0), b = aliasShare(naive, f0);
    printf("  alias share (energy off the harmonics): polyBLEP saw %.5f, naive saw %.5f\n", a, b);
    CHECK(a < b * 0.25);
    CHECK(a < 0.003);
}

static void test_event_queue_and_cc()
{
    Engine e;
    int ok = 0;
    for (int i = 0; i < 100; i++)
        ok += e.noteOn(60, 100);
    CHECK(ok == 30); // the queue holds E_N - 2 events until the audio block drains it
    std::vector<int16_t> o;
    run(e, 1, o);
    CHECK(e.noteOn(61, 100));
    e.controlChange(74, 127);
    e.controlChange(1, 127);
    run(e, 1, o);
    CHECK(e.effective(P_CUTOFF) == 100 && e.param(P_CUTOFF) == paramInfo(P_CUTOFF).def); // a CC moves the effective value, not the saved patch
    e.controlChange(74, 0);
    run(e, 1, o);
    CHECK(e.effective(P_CUTOFF) == 0 && e.param(P_CUTOFF) == paramInfo(P_CUTOFF).def);
    e.controlChange(123, 0);
    run(e, 400, o);
    CHECK(e.activeVoices() == 0);
    e.pitchBend(16383);
    e.pitchBend(0);
    run(e, 2, o);
    // note on with velocity 0 is a note off
    e.noteOn(50, 100);
    run(e, 2, o);
    e.noteOn(50, 0);
    CHECK(true);
    run(e, 1, o);
    CHECK(e.voice(0).gate == 0);
}

// review fixes: CC automation never overwrites the saved patch, releases are never lost, a note shorter than a block still sounds
static void test_cc_release_and_short_notes()
{
    {
        Engine e;
        std::vector<int16_t> o;
        e.setParam(P_REVERB, 20);
        e.controlChange(91, 127);
        e.controlChange(93, 64);
        e.controlChange(7, 0);
        run(e, 1, o);
        CHECK(e.param(P_REVERB) == 20 && e.param(P_DELAY) == paramInfo(P_DELAY).def && e.param(P_LEVEL) == paramInfo(P_LEVEL).def);
        CHECK(e.effective(P_REVERB) == 100 && e.effective(P_DELAY) == 50 && e.effective(P_LEVEL) == 0);
        Params saved;
        for (int i = 0; i < P_COUNT; i++) saved.v[i] = e.param(i);
        CHECK(saved.v[P_REVERB] == 20); // what plinky.bin would store
        e.setParam(P_REVERB, 30); // the UI takes the effective value back
        CHECK(e.effective(P_REVERB) == 30 && e.param(P_REVERB) == 30);
    }
    {
        // the queue is full of note-ons: the release does not fit, but it is remembered and applied
        Engine e;
        std::vector<int16_t> o;
        e.noteOn(60, 100);
        run(e, 2, o);
        for (int i = 0; i < 40; i++) e.noteOn(70, 100);
        CHECK(e.noteOff(60)); // reports success: it will be applied
        run(e, 1, o);
        bool held = false;
        for (int i = 0; i < kMaxVoices; i++)
            if (e.voice(i).stage != ST_IDLE && e.voice(i).note == 60 && e.voice(i).gate) held = true;
        CHECK(!held);
        for (int i = 0; i < 40; i++) e.noteOn(71, 100);
        CHECK(e.allNotesOff());
        run(e, 1, o);
        for (int i = 0; i < kMaxVoices; i++) CHECK(!e.voice(i).gate);
    }
    {
        // note-on and note-off before the first block renders: the note is heard for a block, then releases
        Engine e;
        setOpenMono(e);
        e.noteOn(60, 100);
        e.noteOff(60);
        std::vector<int16_t> o;
        run(e, 1, o);
        double energy = 0;
        for (size_t i = 0; i < o.size(); i++) energy += (double)o[i] * o[i];
        CHECK(energy > 1.0e6);
        CHECK(e.voice(0).gate == 0 && e.voice(0).stage != ST_IDLE);
        run(e, 400, o);
        CHECK(e.activeVoices() == 0);
    }
}

static void test_determinism_and_levels()
{
    Engine a, b;
    std::vector<int16_t> la, lb;
    for (Engine* x : { &a, &b })
    {
        x->noteOn(55, 90);
        x->noteOn(62, 100);
    }
    run(a, 100, la);
    run(b, 100, lb);
    CHECK(la == lb);
    // level: audible, never over the range even for four loud notes with everything up
    Engine e;
    e.setParam(P_LEVEL, 100);
    e.setParam(P_DRIVE, 100);
    e.setParam(P_CUTOFF, 100);
    for (uint8_t n = 40; n < 44; n++)
        e.noteOn(n, 127);
    std::vector<int16_t> o;
    run(e, 200, o);
    int mx = 0;
    double rms = 0;
    for (size_t i = 0; i < o.size(); i++)
    {
        mx = std::max(mx, abs((int)o[i]));
        rms += (double)o[i] * o[i];
    }
    rms = sqrt(rms / o.size());
    printf("  4 notes, everything up: peak %d, rms %.0f\n", mx, rms);
    CHECK(mx <= 32767 && rms > 500);
    // default patch, one note: sensible level (-20 .. -3 dBFS rms)
    Engine d;
    d.noteOn(60, 100);
    std::vector<int16_t> p;
    run(d, 100, p);
    double r2 = 0;
    for (size_t i = 5000; i < p.size(); i++)
        r2 += (double)p[i] * p[i];
    r2 = sqrt(r2 / (p.size() - 5000));
    printf("  default patch, one note: rms %.0f (%.1f dBFS)\n", r2, 20 * log10(r2 / 32768.0));
    CHECK(r2 > 800 && r2 < 24000);
}

static void test_cutoff_brightness_and_lfo()
{
    // a lower cutoff is darker: less high frequency energy (first difference as a crude high pass)
    double hf[3];
    int cuts[3] = { 10, 50, 100 };
    for (int c = 0; c < 3; c++)
    {
        Engine e;
        setOpenMono(e);
        e.setParam(P_CUTOFF, cuts[c]);
        e.noteOn(48, 127);
        std::vector<int16_t> o;
        run(e, 200, o);
        double s = 0, tot = 0;
        for (size_t i = 8000; i < o.size(); i++)
        {
            double d = o[i] - o[i - 1];
            s += d * d;
            tot += (double)o[i] * o[i];
        }
        hf[c] = s / (tot + 1);
    }
    CHECK(hf[0] < hf[1] && hf[1] < hf[2]);
    // vibrato moves the pitch: more distinct zero crossing intervals with LFO depth than without
    Engine e;
    setOpenMono(e);
    e.setParam(P_LFO_DEPTH, 100);
    e.setParam(P_LFO_RATE, 80);
    e.noteOn(60, 100);
    std::vector<int16_t> o;
    run(e, 600, o);
    int minI = 1000000, maxI = 0, last = -1;
    for (size_t i = 3000; i + 1 < o.size(); i++)
        if (o[i] <= 0 && o[i + 1] > 0)
        {
            if (last >= 0)
            {
                minI = std::min(minI, (int)i - last);
                maxI = std::max(maxI, (int)i - last);
            }
            last = (int)i;
        }
    CHECK(maxI - minI >= 3);
}

static double nsPerVoiceSample(int voices, int width, int shape, int reps)
{
    Engine e;
    e.setParam(P_WIDTH, width);
    e.setParam(P_SHAPE, shape);
    e.setParam(P_RELEASE, 100);
    for (int i = 0; i < voices; i++)
        e.noteOn((uint8_t)(48 + i * 4), 100);
    int16_t l[kBlock], r[kBlock];
    for (int i = 0; i < 20; i++)
        e.render(l, r);
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < reps; i++)
        e.render(l, r);
    auto t1 = std::chrono::steady_clock::now();
    double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
    return ns / ((double)reps * kBlock * voices);
}

static void test_cost()
{
    printf("  host cost, ns per voice-sample (saw / pulse, stereo / mono):\n");
    for (int voices = 1; voices <= 4; voices++)
        printf("    %d voice%s: stereo saw %.1f   stereo pulse %.1f   mono saw %.1f   mono pulse %.1f\n", voices,
               voices > 1 ? "s" : " ", nsPerVoiceSample(voices, 60, 0, 4000), nsPerVoiceSample(voices, 60, 40, 4000),
               nsPerVoiceSample(voices, 0, 0, 4000), nsPerVoiceSample(voices, 0, 40, 4000));
    // an idle engine costs nothing
    Engine e;
    int16_t l[kBlock], r[kBlock];
    CHECK(!e.render(l, r));
}

int main()
{
    test_params_and_sidecar();
    test_pitch_maths();
    test_stability_grid();
    test_silence_and_idle();
    test_voice_stealing();
    test_no_clicks();
    test_antialiasing();
    test_event_queue_and_cc();
    test_cc_release_and_short_notes();
    test_determinism_and_levels();
    test_cutoff_brightness_and_lfo();
    test_cost();
    printf("plinky: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
