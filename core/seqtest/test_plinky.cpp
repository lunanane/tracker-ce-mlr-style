// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Host tests of the synth voice engine (plan 009 S): the parameter table and the sidecar plinky.bin, the pitch maths, stability over
// the whole parameter space, silence / idle behaviour (no denormals), voice stealing, parameter smoothing and note changes without
// clicks, the anti-aliasing of the saw, the event queue, and a cost measurement (printed, not asserted).
#include "plinky_voice.h"
#include "seqPlinkyMorphLogic.h"
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
    CHECK(a.v[P_CHANNEL] == 16); // v0.3.2: the PolySynth instrument is MIDI instrument 16 by default (was: off)
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

// ---------------------------------------------------------------- plan 012: snapshots per pattern and the timed morph
using namespace plinkymorph;

static void fill(uint8_t* v, int cutoff, int reso, int voices = 4, int interval = 12, int lfoDest = 0)
{
    Params p;
    memcpy(v, p.v, P_COUNT);
    v[P_CUTOFF] = (uint8_t)cutoff;
    v[P_RESO] = (uint8_t)reso;
    v[P_VOICES] = (uint8_t)voices;
    v[P_INTERVAL] = (uint8_t)interval;
    v[P_LFO_DEST] = (uint8_t)lfoDest;
}

static void test_morph_math()
{
    uint8_t cur[P_COUNT], tgt[P_COUNT];
    fill(cur, 0, 100);
    fill(tgt, 100, 0);
    Morph m;
    // endpoints: at the start the values are the current ones, after the time exactly the target ones
    uint32_t t0 = 5000;
    CHECK(m.begin(cur, tgt, 7, t0, 6000) == 0 && m.running); // nothing is stepped here: all continuous
    CHECK(cur[P_CUTOFF] == 0 && cur[P_RESO] == 100);
    CHECK(m.step(cur, t0) == 0 && cur[P_CUTOFF] == 0);
    // linear: every 10 % of the time moves 10 % of the way (rounded)
    for (int k = 1; k <= 10; k++)
    {
        m.step(cur, t0 + 600u * k - (k == 10 ? 0 : 0));
        CHECK(cur[P_CUTOFF] == 10 * k);
        CHECK(cur[P_RESO] == 100 - 10 * k);
    }
    CHECK(!m.running); // done at the last step
    CHECK(m.step(cur, t0 + 7000) == 0 && cur[P_CUTOFF] == 100 && cur[P_RESO] == 0);
    // monotonic, no overshoot, whole run in 5 ms control ticks
    fill(cur, 20, 90);
    fill(tgt, 80, 10);
    Morph n;
    n.begin(cur, tgt, 3, 0, 6000);
    int lastC = cur[P_CUTOFF], lastR = cur[P_RESO];
    for (uint32_t t = 5; t <= 6000; t += 5)
    {
        n.step(cur, t);
        CHECK(cur[P_CUTOFF] >= lastC && cur[P_CUTOFF] <= 80);
        CHECK(cur[P_RESO] <= lastR && cur[P_RESO] >= 10);
        lastC = cur[P_CUTOFF];
        lastR = cur[P_RESO];
    }
    CHECK(cur[P_CUTOFF] == 80 && cur[P_RESO] == 10 && !n.running);
    CHECK(lastC == 80);
    // time 0 = jump: everything at once, nothing left running
    fill(cur, 0, 100);
    fill(tgt, 100, 0);
    Morph z;
    const uint32_t ch = z.begin(cur, tgt, 2, 100, 0);
    CHECK(!z.running && cur[P_CUTOFF] == 100 && cur[P_RESO] == 0 && (ch & (1u << P_CUTOFF)) && (ch & (1u << P_RESO)));
    // stepped parameters switch at the start, continuous ones move
    fill(cur, 0, 50, 4, 12, 0);
    fill(tgt, 100, 50, 2, 0, 2);
    Morph s;
    const uint32_t cs = s.begin(cur, tgt, 2, 0, 6000);
    CHECK(cur[P_VOICES] == 2 && cur[P_INTERVAL] == 0 && cur[P_LFO_DEST] == 2 && cur[P_CUTOFF] == 0);
    CHECK((cs & (1u << P_VOICES)) && (cs & (1u << P_INTERVAL)) && (cs & (1u << P_LFO_DEST)) && !(cs & (1u << P_CUTOFF)));
    s.step(cur, 3000);
    CHECK(cur[P_CUTOFF] == 50 && cur[P_VOICES] == 2);
    // the channel never takes part
    fill(cur, 0, 50);
    fill(tgt, 100, 50);
    tgt[P_CHANNEL] = 3;
    Morph c;
    c.begin(cur, tgt, 2, 0, 1000);
    c.step(cur, 5000);
    CHECK(cur[P_CHANNEL] == 16);
    // identical target: nothing runs
    fill(cur, 40, 40);
    fill(tgt, 40, 40);
    Morph e;
    e.begin(cur, tgt, 2, 0, 6000);
    CHECK(!e.running);
    // a wrapping millisecond clock
    fill(cur, 0, 50);
    fill(tgt, 100, 50);
    Morph w;
    w.begin(cur, tgt, 2, 0xFFFFFE00u, 1000);
    w.step(cur, 0xFFFFFE00u + 500u);
    CHECK(cur[P_CUTOFF] == 50);
    CHECK(w.remainMs(0xFFFFFE00u + 500u) == 500);
}

static void test_morph_interrupt()
{
    // another change in the middle: the new morph starts from the CURRENT values, over the full time again
    uint8_t cur[P_COUNT], a[P_COUNT], b[P_COUNT];
    fill(cur, 0, 50);
    fill(a, 100, 50);
    fill(b, 20, 50);
    Morph m;
    m.begin(cur, a, 1, 0, 6000);
    m.step(cur, 3000);
    CHECK(cur[P_CUTOFF] == 50);
    m.begin(cur, b, 2, 3000, 6000);
    CHECK(cur[P_CUTOFF] == 50 && m.running && m.pattern == 2);
    m.step(cur, 6000); // half way: 50 -> 20 is 35
    CHECK(cur[P_CUTOFF] == 35);
    m.step(cur, 9000);
    CHECK(cur[P_CUTOFF] == 20 && !m.running);
    // hand edit: the parameter leaves the morph, the others go on
    fill(cur, 0, 100);
    fill(a, 100, 0);
    Morph h;
    h.begin(cur, a, 1, 0, 6000);
    h.step(cur, 1500);
    CHECK(cur[P_CUTOFF] == 25 && cur[P_RESO] == 75);
    cur[P_CUTOFF] = 60; // the hand
    h.hold(P_CUTOFF);
    h.step(cur, 3000);
    CHECK(cur[P_CUTOFF] == 60 && cur[P_RESO] == 50);
    h.step(cur, 6000);
    CHECK(cur[P_CUTOFF] == 60 && cur[P_RESO] == 0 && !h.running);
    // a held parameter is free again with the next morph
    Morph h2 = h;
    uint8_t t2[P_COUNT];
    fill(t2, 10, 0);
    h2.begin(cur, t2, 3, 10000, 1000);
    h2.step(cur, 11000);
    CHECK(cur[P_CUTOFF] == 10);
}

static void test_inherit_and_controller()
{
    Controller c;
    uint8_t v[P_COUNT], s5[P_COUNT], s9[P_COUNT];
    Params base;
    memcpy(v, base.v, P_COUNT);
    fill(s5, 80, 10);
    fill(s9, 10, 90);
    c.store.set(5, s5);
    c.store.set(9, s9);
    CHECK(c.store.has(5) && c.store.has(9) && !c.store.has(6) && !c.store.has(0) && c.store.count() == 2);
    // the first pass after a load applies the shown pattern's snapshot at once
    CHECK(c.onPattern(5, true, v, 1000) != 0 && v[P_CUTOFF] == 80 && !c.morph.running);
    // same pattern: nothing
    CHECK(c.onPattern(5, true, v, 1100) == 0);
    // a pattern without a snapshot inherits: nothing changes, no morph
    CHECK(c.onPattern(6, true, v, 2000) == 0 && v[P_CUTOFF] == 80 && !c.morph.running);
    // a pattern with a snapshot while playing: morph over the project time (default 6 s)
    CHECK(c.halfSec == 12 && c.durMs() == 6000);
    CHECK(c.onPattern(9, true, v, 3000) == 0 && c.morph.running && c.morph.pattern == 9);
    c.tick(v, 3000 + 3000);
    CHECK(v[P_CUTOFF] == 45 && v[P_RESO] == 50);
    c.tick(v, 3000 + 6000);
    CHECK(v[P_CUTOFF] == 10 && v[P_RESO] == 90 && !c.morph.running);
    // stopped: the shown pattern's sound at once
    CHECK(c.onPattern(5, false, v, 20000) != 0 && v[P_CUTOFF] == 80 && !c.morph.running);
    // morph time 0 = jump while playing
    c.halfSec = 0;
    c.onPattern(9, true, v, 30000);
    CHECK(v[P_CUTOFF] == 10 && !c.morph.running);
    // 30 s
    c.halfSec = kHalfSecMax;
    CHECK(c.durMs() == 30000);
    c.onPattern(5, true, v, 40000);
    c.tick(v, 55000);
    CHECK(v[P_CUTOFF] == 45);
    c.tick(v, 70000);
    CHECK(v[P_CUTOFF] == 80);
    // invalid pattern numbers do nothing
    CHECK(c.onPattern(0, true, v, 0) == 0 && c.onPattern(300, true, v, 0) == 0);

    // edit: the first edit of a pattern without a snapshot makes the current sound the pattern's own
    Controller d;
    memcpy(v, base.v, P_COUNT);
    d.onPattern(3, true, v, 0);
    v[P_CUTOFF] = 33;
    CHECK(d.edit(3, P_CUTOFF, 33, v));
    CHECK(d.store.has(3) && d.store.get(3)[P_CUTOFF] == 33 && d.store.get(3)[P_RESO] == base.v[P_RESO]);
    CHECK(!d.edit(3, P_CUTOFF, 33, v)); // no change: not dirty
    CHECK(d.edit(3, P_RESO, 77, v) && d.store.get(3)[P_RESO] == 77 && d.store.get(3)[P_CUTOFF] == 33);
    CHECK(!d.edit(0, P_RESO, 5, v) && !d.edit(3, P_CHANNEL, 5, v));
    // another pattern's snapshot is not touched
    CHECK(!d.store.has(4));
    // an edit during a morph: out of the morph, in the snapshot of the pattern that plays; a new snapshot starts from the morph's target
    Controller e;
    memcpy(v, base.v, P_COUNT);
    e.store.set(1, s5);
    e.onPattern(2, true, v, 0);
    e.onPattern(1, true, v, 100);   // morph 1: towards s5
    e.tick(v, 3100);
    CHECK(v[P_CUTOFF] > base.v[P_CUTOFF] && v[P_CUTOFF] < 80);
    v[P_CUTOFF] = 5;                  // hand
    CHECK(e.edit(1, P_CUTOFF, 5, v));
    e.tick(v, 6100);
    CHECK(v[P_CUTOFF] == 5 && v[P_RESO] == 10 && e.store.get(1)[P_CUTOFF] == 5);
    Controller f;
    memcpy(v, base.v, P_COUNT);
    f.store.set(7, s9);
    f.onPattern(8, true, v, 0);
    f.onPattern(7, true, v, 10); // morph towards s9 while pattern 7 plays; pattern 7 has a snapshot
    f.tick(v, 3010);
    f.onPattern(8, true, v, 3010); // 8 inherits: the morph goes on (nothing changes)
    CHECK(f.morph.running);
    v[P_NOISE] = 12;
    CHECK(f.edit(8, P_NOISE, 12, v)); // a first edit on 8 while morphing: the snapshot gets the target values, not the values on the way
    CHECK(f.store.get(8)[P_CUTOFF] == 10 && f.store.get(8)[P_RESO] == 90 && f.store.get(8)[P_NOISE] == 12);

    // the mark: a snapshot differs from the inherited one (nearest lower snapshot, else the base)
    Store st;
    uint8_t x[P_COUNT];
    fill(x, 70, 20);
    st.set(2, x);
    CHECK(st.marks(2) == (1u << P_CUTOFF));       // the base has cutoff 60
    fill(x, 70, 25);
    st.set(6, x);
    CHECK(st.marks(6) == (1u << P_RESO));         // inherits from 2
    CHECK(st.marks(1) == 0 && st.marks(5) == 0);  // no snapshot: no mark
    CHECK(!st.differs(6, P_CUTOFF) && st.differs(6, P_RESO));
    CHECK(clipTarget(true, 12) == 12 && clipTarget(false, 12) == 0 && clipTarget(true, 0) == 0 && clipTarget(true, 256) == 0);
}

static void test_clip_target()
{
    // a clip launch on a PolySynth track morphs to the clip's source pattern snapshot; last launch wins; non-PolySynth tracks do nothing
    Controller c;
    uint8_t v[P_COUNT], s4[P_COUNT], s8[P_COUNT];
    Params base;
    memcpy(v, base.v, P_COUNT);
    fill(s4, 90, 20);
    fill(s8, 10, 20);
    c.store.set(4, s4);
    c.store.set(8, s8);
    c.onPattern(1, true, v, 0);
    CHECK(c.onClip(4, false, true, v, 100) == 0 && !c.morph.running); // the track does not play the PolySynth
    CHECK(c.onClip(5, true, true, v, 100) == 0 && !c.morph.running);  // the source pattern has no snapshot: inherits
    CHECK(c.onClip(4, true, true, v, 100) == 0 && c.morph.running && c.morph.pattern == 4);
    c.tick(v, 3100);
    const int mid = v[P_CUTOFF];
    CHECK(mid > base.v[P_CUTOFF] && mid < 90);
    c.onClip(8, true, true, v, 3100); // last launch wins, from where the first one is
    CHECK(c.morph.pattern == 8 && c.morph.running);
    c.tick(v, 9100);
    CHECK(v[P_CUTOFF] == 10 && !c.morph.running);
    // stopped: at once
    c.onClip(4, true, false, v, 20000);
    CHECK(v[P_CUTOFF] == 90 && !c.morph.running);
    // the playing pattern did not change: no second morph from the pattern watcher
    CHECK(c.onPattern(1, true, v, 20010) == 0);
}

static void test_morph_file()
{
    Params cur;
    cur.v[P_CUTOFF] = 41;
    cur.v[P_REVERB] = 25;
    Store st;
    uint8_t a[P_COUNT], b[P_COUNT];
    fill(a, 11, 22, 3, 5, 1);
    fill(b, 99, 98, 1, 24, 2);
    st.set(3, a);
    st.set(255, b);
    uint8_t buf[kFileMaxAll];
    size_t n = saveFile(buf, sizeof(buf), cur, st, 20);
    CHECK(n > (size_t)kFileMax && n <= (size_t)kFileMaxAll);
    CHECK(n == (size_t)kFileMax + seqsidecar::kOverhead + 2 + 2 * kEntry);
    // round trip
    Params base;
    Store st2;
    uint8_t half = 0;
    bool newer = true;
    CHECK(loadFile(buf, n, base, st2, half, &newer) && !newer);
    CHECK(base == cur && half == 20 && st2.count() == 2);
    CHECK(st2.has(3) && st2.has(255) && !st2.has(4));
    CHECK(memcmp(st2.get(3) + 1, a + 1, P_COUNT - 1) == 0 && memcmp(st2.get(255) + 1, b + 1, P_COUNT - 1) == 0);
    CHECK(memcmp(st2.base, cur.v, P_COUNT) == 0);
    // an older mod reads the same file: frame 1 (version 1) first, the trailer is ignored
    Params old;
    bool nw = true;
    CHECK(old.load(buf, n, &nw) && !nw && old == cur);
    // an older file (frame 1 only) loads as the base, no snapshots, default time
    uint8_t obuf[64];
    size_t on = cur.save(obuf, sizeof(obuf));
    Params b2;
    Store st3;
    half = 99;
    CHECK(loadFile(obuf, on, b2, st3, half, &newer) && b2 == cur && st3.count() == 0 && half == kHalfSecDefault && !newer);
    // nothing to say: the file is exactly the old one
    Store empty;
    CHECK(saveFile(buf, sizeof(buf), cur, empty, kHalfSecDefault) == on && memcmp(buf, obuf, on) == 0);
    // only the time differs: a trailer without entries
    n = saveFile(buf, sizeof(buf), cur, empty, 0);
    CHECK(n == (size_t)kFileMax + seqsidecar::kOverhead + 2);
    CHECK(loadFile(buf, n, b2, st3, half, &newer) && half == 0 && st3.count() == 0);
    // a bad trailer CRC: the base stays, no snapshots, default time
    n = saveFile(buf, sizeof(buf), cur, st, 20);
    buf[kFileMax + 12] ^= 0x5A;
    CHECK(loadFile(buf, n, b2, st3, half, &newer) && b2 == cur && st3.count() == 0 && half == kHalfSecDefault && !newer);
    buf[kFileMax + 12] ^= 0x5A;
    // a truncated file (trailer cut): the old part still loads
    CHECK(loadFile(buf, n - 7, b2, st3, half, &newer) && st3.count() == 0);
    // a trailer of a newer version: values load as far as known, the file is left alone
    buf[kFileMax + 4] = 9;
    uint16_t crc = seqsidecar::crc16(buf + kFileMax, n - kFileMax - 2);
    buf[n - 2] = (uint8_t)(crc & 0xFF);
    buf[n - 1] = (uint8_t)(crc >> 8);
    newer = false;
    CHECK(loadFile(buf, n, b2, st3, half, &newer) && newer && st3.count() == 0);
    // out of range values in an entry fall back to their default; pattern 0 is dropped
    n = saveFile(buf, sizeof(buf), cur, st, 20);
    buf[kFileMax + seqsidecar::kHeader + 2 + 1 + (P_VOICES - 1)] = 9;     // entry 0 (pattern 3): voices out of range
    buf[kFileMax + seqsidecar::kHeader + 2 + kEntry] = 0;                 // entry 1: pattern 0
    crc = seqsidecar::crc16(buf + kFileMax, n - kFileMax - 2);
    buf[n - 2] = (uint8_t)(crc & 0xFF);
    buf[n - 1] = (uint8_t)(crc >> 8);
    CHECK(loadFile(buf, n, b2, st3, half, &newer) && st3.count() == 1 && st3.get(3)[P_VOICES] == paramInfo(P_VOICES).def);
    // all 255 patterns: fits the buffer
    Store full;
    for (int p = 1; p < 256; p++)
        full.set(p, a);
    n = saveFile(buf, sizeof(buf), cur, full, 12);
    CHECK(n > 0 && n <= sizeof(buf));
    CHECK(loadFile(buf, n, b2, st3, half, &newer) && st3.count() == 255);
    CHECK(saveFile(buf, 100, cur, full, 12) == 0); // a buffer too small
    // texts
    char t[32];
    titleText(t, sizeof t, 3200, 12);
    CHECK(strcmp(t, "Morph 3.2s > P12") == 0);
    titleText(t, sizeof t, 3150, 255);
    CHECK(strcmp(t, "Morph 3.2s > P255") == 0);
    timeText(12, t, sizeof t);
    CHECK(strcmp(t, "6.0 s") == 0);
    timeText(0, t, sizeof t);
    CHECK(strcmp(t, "Jump") == 0);
    timeText(59, t, sizeof t);
    CHECK(strcmp(t, "29.5 s") == 0);
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
    test_morph_math();
    test_morph_interrupt();
    test_inherit_and_controller();
    test_clip_target();
    test_morph_file();
    test_cost();
    printf("plinky: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
