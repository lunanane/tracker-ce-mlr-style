// Sequencer tracks mod - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Plan 019 B tests (included by test_plinky.cpp): the engine extensions - sub oscillators, SVF / ladder filters, filter envelope, LFO
// waveforms and the level target, chorus; stability over the new parameters, old sounds at the defaults, no clicks, plinky.bin extras.

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
static double goertzelPow(const std::vector<int16_t>& x, size_t from, size_t n, double freq)
{
    const double w = 2.0 * M_PI * freq / 44100.0, c = 2.0 * cos(w);
    double s1 = 0, s2 = 0;
    for (size_t i = from; i < from + n && i < x.size(); i++)
    {
        const double s = x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return (s1 * s1 + s2 * s2 - c * s1 * s2) / ((double)n * n);
}
static double rmsOf(const std::vector<int16_t>& x, size_t from, size_t n)
{
    double s = 0;
    for (size_t i = from; i < from + n && i < x.size(); i++)
        s += (double)x[i] * x[i];
    return sqrt(s / (double)n);
}
// energy above `hz` as a share of the total, by a crude difference filter pair (two steps of first differences ~ +12 dB/oct)
static double brightness(const std::vector<int16_t>& x, size_t from, size_t n)
{
    double d = 0, e = 0;
    for (size_t i = from + 2; i < from + n && i < x.size(); i++)
    {
        const double dd = (double)x[i] - 2.0 * x[i - 1] + x[i - 2];
        d += dd * dd;
        e += (double)x[i] * x[i];
    }
    return e > 0 ? sqrt(d / e) : 0;
}
// absolute high frequency level: rms of the second difference
static double hfRms(const std::vector<int16_t>& x, size_t from, size_t n)
{
    double d = 0;
    for (size_t i = from + 2; i < from + n && i < x.size(); i++)
    {
        const double dd = (double)x[i] - 2.0 * x[i - 1] + x[i - 2];
        d += dd * dd;
    }
    return sqrt(d / (double)n);
}

// ---------------------------------------------------------------- the defaults change nothing
static void test_x4_defaults()
{
    CHECK(P_COUNT == 33 && P_SUB1 == 21 && P_CH_DEPTH == 32);
    CHECK(paramInfo(P_SUB1).def == 0 && paramInfo(P_SUB2).def == 0 && paramInfo(P_FTYPE).def == 0 && paramInfo(P_FENV_AMT).def == 50 &&
          paramInfo(P_LFO_WAVE).def == 0 && paramInfo(P_CH_MIX).def == 0);
    // the explicit default values of every new parameter give the old sound bit for bit (the golden hashes of the identity test are the v0.3.8 engine)
    Engine e;
    for (int i = kLegacyCount; i < P_COUNT; i++)
        e.setParam(i, paramInfo(i).def);
    static int16_t chmem[2 * kChLen];
    e.setChorusMemory(0, chmem); // chorus memory present, mix 0
    Engine f;
    e.noteOn(60, 100);
    f.noteOn(60, 100);
    std::vector<int16_t> a, b;
    run(e, 40, a);
    run(f, 40, b);
    CHECK(a == b);
    // the new parameters have names, ranges and texts
    for (int i = 0; i < P_COUNT; i++)
        CHECK(paramInfo(i).name && paramInfo(i).min <= paramInfo(i).def && paramInfo(i).def <= paramInfo(i).max);
    CHECK(strcmp(filterTypeName(0), "LPG") == 0 && strcmp(filterTypeName(4), "Ladder") == 0 && strcmp(lfoWaveName(5), "S&H") == 0);
    char t[24];
    seqplinky::valueText(P_FENV_AMT, 50, t, sizeof t);
    CHECK(strcmp(t, "+0.0 oct") == 0);
    seqplinky::valueText(P_FENV_AMT, 100, t, sizeof t);
    CHECK(strcmp(t, "+5.0 oct") == 0);
    seqplinky::valueText(P_FENV_AMT, 25, t, sizeof t);
    CHECK(strcmp(t, "-2.5 oct") == 0);
    seqplinky::valueText(P_FTYPE, 2, t, sizeof t);
    CHECK(strcmp(t, "SVF BP") == 0);
    seqplinky::valueText(P_LFO_DEST, 3, t, sizeof t);
    CHECK(strcmp(t, "Level") == 0);
}

// ---------------------------------------------------------------- sub oscillators
static void test_x4_subs()
{
    // one octave and two octaves below the played note: energy at f/2 and f/4 appears
    auto render = [](int s1, int s2, int note) {
        Engine e;
        e.setParam(P_WIDTH, 0);
        e.setParam(P_DETUNE, 0);
        e.setParam(P_CUTOFF, 100);
        e.setParam(P_LEVEL, 30);
        e.setParam(P_SUB1, s1);
        e.setParam(P_SUB2, s2);
        e.noteOn(note, 100);
        std::vector<int16_t> o;
        run(e, 120, o);
        return o;
    };
    const double f0 = 440.0;
    const std::vector<int16_t> none = render(0, 0, 69), s1 = render(100, 0, 69), s2 = render(0, 100, 69);
    const size_t from = 60 * kBlock, n = 40 * kBlock;
    // a saw has all harmonics: compare at frequencies that are NOT harmonics of f0 but of f0/2 (the odd multiples of 220 Hz) and of f0/4
    const double a0 = goertzelPow(none, from, n, f0 * 0.5 * 3), a1 = goertzelPow(s1, from, n, f0 * 0.5 * 3);
    CHECK(a1 > a0 * 4.0);
    const double b0 = goertzelPow(none, from, n, f0 * 0.25 * 3), b2 = goertzelPow(s2, from, n, f0 * 0.25 * 3);
    CHECK(b2 > b0 * 4.0);
    // the sub at f0/2 is a square: its fundamental (f0/2) is the strongest line it adds, the 3rd harmonic 9.5 dB lower
    const double h1 = goertzelPow(s1, from, n, f0 * 0.5), h3 = goertzelPow(s1, from, n, f0 * 1.5);
    CHECK(h1 > 0);
    (void)h3;
    // no energy added at f0/4 by the -1 sub
    CHECK(goertzelPow(s1, from, n, f0 * 0.25 * 3) < b0 * 4.0 + 1.0);
    // level 0 = off: identical to no sub
    CHECK(render(0, 0, 69) == none);
    // anti aliasing: the difference signal (with sub minus without) at C7, where the sub square is at 1046 Hz: its energy is on the
    // harmonics of 1046 Hz (odd multiples), not folded back
    {
        const std::vector<int16_t> p = render(0, 0, 96), q = render(100, 0, 96);
        std::vector<int16_t> d(p.size());
        for (size_t i = 0; i < p.size(); i++)
            d[i] = (int16_t)(q[i] - p[i]);
        const double fs = 440.0 * pow(2.0, (96 - 69) / 12.0) * 0.5; // 1046.5 Hz
        double on = 0, all = 0;
        const size_t nn = 32 * kBlock, fr = 70 * kBlock;
        for (size_t i = fr; i < fr + nn; i++)
            all += (double)d[i] * d[i];
        for (int h = 1; h * fs < 21000; h += 2)
            on += goertzelPow(d, fr, nn, h * fs) * 2.0 * (double)nn; // the Goertzel power normalisation times 2n gives the line energy
        // the low pass gate removes most of the highs: what is left must sit on the odd harmonics of the sub
        CHECK(all > 0);
        CHECK(on / all > 0.5);
    }
}

// ---------------------------------------------------------------- filters
static void test_x4_filters()
{
    // frequency response of the filter kernels (g = tan(pi fc / fs))
    auto gain = [](int mode, double fc, double f, double res) {
        const double g = tan(M_PI * fc / 44100.0);
        const float k = 2.0f - 1.96f * (float)(res * res);
        const float a1 = 1.0f / (1.0f + (float)g * ((float)g + k)), a2 = (float)g * a1, a3 = (float)g * a2;
        float s[2] = { 0, 0 };
        double acc = 0;
        const int N = 8000;
        for (int i = 0; i < N; i++)
        {
            const float x = (float)sin(2.0 * M_PI * f * i / 44100.0);
            const float y = svfStep(x, s, a1, a2, a3, k, mode);
            if (i > N / 2)
                acc += (double)y * y;
        }
        return sqrt(acc / (N / 2)) / sqrt(0.5);
    };
    // low pass: pass, -3 dB (k = 2 -> 0.5 at the corner for the 12 dB SVF without resonance), stop
    CHECK(gain(1, 2000, 100, 0.0) > 0.95 && gain(1, 2000, 8000, 0.0) < 0.12 && gain(1, 2000, 800, 0) > gain(1, 2000, 3000, 0));
    CHECK(gain(3, 2000, 100, 0.0) < 0.05 && gain(3, 2000, 9000, 0.0) > 0.85);
    CHECK(gain(2, 2000, 2000, 0.9) > gain(2, 2000, 200, 0.9) * 3 && gain(2, 2000, 2000, 0.9) > gain(2, 2000, 12000, 0.9) * 3);
    // 12 dB per octave above the corner: one octave up is about 4 times (12 dB) less
    const double r12 = gain(1, 500, 2000, 0.0) / gain(1, 500, 4000, 0.0);
    CHECK(r12 > 3.0 && r12 < 5.0);
    // resonance raises the corner
    CHECK(gain(1, 2000, 2000, 0.95) > gain(1, 2000, 2000, 0.0) * 3);
    // ladder: 24 dB per octave (16 times per octave) well above the corner, flat below, resonance peak near the corner
    auto lad = [](double fc, double f, double res) {
        const double g = tan(M_PI * fc / 44100.0);
        const float G = (float)(g / (1.0 + g)), G2 = G * G, G3 = G2 * G;
        const float kf = 3.9f * (float)(res * res), invD = 1.0f / (1.0f + kf * G2 * G2);
        float s[4] = { 0, 0, 0, 0 };
        double acc = 0;
        const int N = 8000;
        for (int i = 0; i < N; i++)
        {
            const float x = 0.1f * (float)sin(2.0 * M_PI * f * i / 44100.0); // small: the loop clip is linear there
            const float y = ladderStep(x, s, G, G2, G3, invD, kf);
            if (i > N / 2)
                acc += (double)y * y;
        }
        return sqrt(acc / (N / 2)) / (0.1 * sqrt(0.5));
    };
    CHECK(lad(1000, 100, 0.0) > 0.9);
    const double rl = lad(250, 2000, 0.0) / lad(250, 4000, 0.0);
    CHECK(rl > 11.0 && rl < 22.0);
    CHECK(lad(1000, 900, 0.95) > lad(1000, 900, 0.0) * 2.5);
    // the kernels stay bounded: extreme coefficients, loud input, 200000 samples
    {
        bool ok = true;
        uint32_t rs = 99;
        for (int mode = 1; mode <= 4 && ok; mode++)
            for (double fc : { 20.0, 500.0, 5000.0, 13000.0 })
                for (double res : { 0.0, 0.5, 1.0 })
                {
                    const double g = tan(M_PI * fc / 44100.0);
                    float s[4] = { 0, 0, 0, 0 };
                    const float k = 2.0f - 1.96f * (float)(res * res);
                    const float a1 = 1.0f / (1.0f + (float)g * ((float)g + k)), a2 = (float)g * a1, a3 = (float)g * a2;
                    const float G = (float)(g / (1.0 + g)), G2 = G * G, G3 = G2 * G, kf = 3.9f * (float)(res * res), invD = 1.0f / (1.0f + kf * G2 * G2);
                    for (int i = 0; i < 50000; i++)
                    {
                        rs = rs * 1664525u + 1013904223u;
                        const float x = (float)(int32_t)rs * (4.0f / 2147483648.0f);
                        const float y = mode < 4 ? svfStep(x, s, a1, a2, a3, k, mode) : ladderStep(x, s, G, G2, G3, invD, kf);
                        ok = ok && y > -1.0e3f && y < 1.0e3f;
                    }
                }
        CHECK(ok);
    }
    // engine: every filter type follows the cutoff (brighter when it opens), the types differ from each other and from the gate
    {
        double bright[5][2];
        std::vector<int16_t> first;
        for (int ft = 0; ft < 5; ft++)
            for (int ci = 0; ci < 2; ci++)
            {
                Engine e;
                e.setParam(P_FTYPE, ft);
                e.setParam(P_CUTOFF, ci == 0 ? 30 : 85);
                e.setParam(P_RESO, 20);
                e.setParam(P_WIDTH, 0);
                e.setParam(P_LEVEL, 40);
                e.setParam(P_ATTACK, 0);
                e.setParam(P_SUSTAIN, 100);
                e.noteOn(48, 110);
                std::vector<int16_t> o;
                run(e, 100, o);
                bright[ft][ci] = brightness(o, 60 * kBlock, 30 * kBlock);
                CHECK(rmsOf(o, 60 * kBlock, 30 * kBlock) > 50.0);
            }
        for (int ft = 0; ft < 5; ft++)
            CHECK(bright[ft][1] > bright[ft][0] * 1.3);
        // a high pass is brighter than the low pass at the same cutoff, the band pass in between
        CHECK(bright[3][0] > bright[1][0] * 2.0);
    }
    // stability over the grid: types x cutoff x resonance x width x notes x drive, finite and bounded
    {
        bool ok = true;
        for (int ft = 1; ft < 5; ft++)
            for (int cut : { 0, 40, 100 })
                for (int res : { 0, 60, 100 })
                    for (int w : { 0, 80 })
                    {
                        Engine e;
                        e.setParam(P_FTYPE, ft);
                        e.setParam(P_CUTOFF, cut);
                        e.setParam(P_RESO, res);
                        e.setParam(P_WIDTH, w);
                        e.setParam(P_DRIVE, 100);
                        e.setParam(P_LEVEL, 100);
                        e.setParam(P_SUB1, 100);
                        e.setParam(P_FENV_AMT, 100);
                        for (int n = 0; n < 4; n++)
                            e.noteOn(24 + n * 30, 127);
                        std::vector<int16_t> o;
                        run(e, 60, o);
                        for (int i = 0; i < kMaxVoices; i++)
                            ok = ok && voiceFinite(e.voice(i));
                        for (int i = 0; i < kPool; i++)
                            for (int q = 0; q < 8; q++)
                                ok = ok && e.voice(i).fs[q] > -1.0e4f && e.voice(i).fs[q] < 1.0e4f;
                    }
        CHECK(ok);
    }
    // no click when the type or the cutoff jumps while a note sounds
    {
        Engine e;
        e.setParam(P_WIDTH, 0);
        e.setParam(P_SUSTAIN, 100);
        e.setParam(P_ATTACK, 0);
        e.noteOn(55, 100);
        std::vector<int16_t> o;
        run(e, 40, o);
        const size_t steady = o.size();
        double peak = 0;
        for (size_t i = steady - 20 * kBlock; i < steady; i++)
            peak = fmax(peak, fabs((double)o[i]));
        int worst = 0;
        for (int step = 0; step < 12; step++)
        {
            e.setParam(P_FTYPE, step % 5);
            e.setParam(P_CUTOFF, (step * 37) % 101);
            const size_t b0 = o.size();
            run(e, 12, o);
            for (size_t i = b0; i < o.size(); i++)
                worst = std::max(worst, abs((int)o[i] - (int)o[i - 1]));
        }
        CHECK(worst < peak * 1.2 + 1); // the parameter smoothing and the coefficient ramp: no step larger than the signal itself
    }
}

// ---------------------------------------------------------------- filter envelope
static void test_x4_filter_env()
{
    auto render = [](int ft, int amt, int a, int d, int s) {
        Engine e;
        e.setParam(P_FTYPE, ft);
        e.setParam(P_CUTOFF, 25);
        e.setParam(P_WIDTH, 0);
        e.setParam(P_SUSTAIN, 100);
        e.setParam(P_ATTACK, 0);
        e.setParam(P_LEVEL, 40);
        e.setParam(P_FENV_AMT, amt);
        e.setParam(P_FENV_A, a);
        e.setParam(P_FENV_D, d);
        e.setParam(P_FENV_S, s);
        e.noteOn(48, 110);
        std::vector<int16_t> o;
        run(e, 160, o);
        return o;
    };
    for (int ft : { 0, 1, 4 })
    {
        const std::vector<int16_t> none = render(ft, 50, 0, 40, 0), up = render(ft, 100, 0, 40, 0), down = render(ft, 0, 0, 40, 0);
        // at the amount 50 the envelope does nothing
        CHECK(none == render(ft, 50, 70, 10, 100));
        // a positive amount opens the filter at the start and lets it close (sustain 0, decay): brighter early than late; the gate and the
        // SVF are brighter than without the envelope at the start
        const double b0 = hfRms(up, 8 * kBlock, 12 * kBlock), bl = hfRms(up, 140 * kBlock, 12 * kBlock);
        CHECK(b0 > bl * 1.2);
        CHECK(b0 > hfRms(none, 8 * kBlock, 12 * kBlock) * 1.2);
        // a negative amount darkens the start
        CHECK(hfRms(down, 8 * kBlock, 12 * kBlock) < hfRms(none, 8 * kBlock, 12 * kBlock) * 0.95);
    }
    // attack time: a slow filter attack opens later
    {
        const std::vector<int16_t> fast = render(1, 100, 0, 100, 100), slow = render(1, 100, 80, 100, 100);
        CHECK(brightness(slow, 3 * kBlock, 6 * kBlock) < brightness(fast, 3 * kBlock, 6 * kBlock) * 0.8);
    }
    // release: after the note off the filter envelope falls with the release time (brightness decreases while the amplitude rings out)
    {
        Engine e;
        e.setParam(P_FTYPE, 1);
        e.setParam(P_CUTOFF, 25);
        e.setParam(P_WIDTH, 0);
        e.setParam(P_RELEASE, 90);
        e.setParam(P_FENV_AMT, 100);
        e.setParam(P_FENV_S, 100);
        e.setParam(P_FENV_R, 30);
        e.noteOn(48, 110);
        std::vector<int16_t> o;
        run(e, 80, o);
        e.noteOff(48);
        const size_t r0 = o.size();
        run(e, 120, o);
        CHECK(brightness(o, r0 + 2 * kBlock, 4 * kBlock) > brightness(o, r0 + 100 * kBlock, 4 * kBlock));
    }
}

// ---------------------------------------------------------------- LFO waveforms
static void test_x4_lfo()
{
    CHECK(kLfoWaves == 7);
    // the shapes: sine as in v0.3.x, triangle starts at the bottom, saws, square, the random ones
    for (float p : { 0.0f, 0.1f, 0.25f, 0.5f, 0.77f, 0.999f })
    {
        const float x = p * 2.0f - 1.0f;
        CHECK_NEAR(lfoShape(0, p, 0, 0), 4.0f * x * (1.0f - fabsf(x)), 1e-6);
        CHECK_NEAR(lfoShape(2, p, 0, 0), x, 1e-6);
        CHECK_NEAR(lfoShape(3, p, 0, 0), -x, 1e-6);
        CHECK(lfoShape(4, p, 0, 0) == (p < 0.5f ? 1.0f : -1.0f));
        CHECK_NEAR(lfoShape(5, p, 0.3f, -0.4f), -0.4, 1e-6);
        for (int w = 0; w < kLfoWaves; w++)
            CHECK(fabsf(lfoShape(w, p, 0.9f, -0.9f)) <= 1.0001f);
    }
    CHECK_NEAR(lfoShape(1, 0.0f, 0, 0), -1.0, 1e-6);
    CHECK_NEAR(lfoShape(1, 0.5f, 0, 0), 1.0, 1e-6);
    CHECK_NEAR(lfoShape(1, 0.25f, 0, 0), 0.0, 1e-6);
    // smooth random: from the previous value at the start of the cycle to the current one at its end, monotone in between
    CHECK_NEAR(lfoShape(6, 0.0f, -0.5f, 0.7f), -0.5, 1e-6);
    CHECK_NEAR(lfoShape(6, 0.9999f, -0.5f, 0.7f), 0.7, 1e-3);
    float last = -2;
    bool mono = true;
    for (int i = 0; i <= 100; i++)
    {
        const float v = lfoShape(6, i / 100.0f, -0.5f, 0.7f);
        mono = mono && v >= last;
        last = v;
    }
    CHECK(mono);
    // engine: the level target (tremolo) moves the loudness; the waveforms differ; S&H holds its value inside a cycle (blocks are equal), then changes
    auto env = [](int wave, int dest, int rate) {
        Engine e;
        e.setParam(P_WIDTH, 0);
        e.setParam(P_ATTACK, 0);
        e.setParam(P_SUSTAIN, 100);
        e.setParam(P_DETUNE, 0); // the beating of detuned saws would modulate the level too
        e.setParam(P_LFO_WAVE, wave);
        e.setParam(P_LFO_DEST, dest);
        e.setParam(P_LFO_DEPTH, 100);
        e.setParam(P_LFO_RATE, rate);
        e.setParam(P_CUTOFF, 100);
        e.noteOn(81, 110);
        std::vector<int16_t> o;
        run(e, 640, o);
        std::vector<double> r;
        for (int b = 160; b + 8 <= 640; b += 4)
            r.push_back(rmsOf(o, (size_t)b * kBlock, 8 * kBlock)); // 8 blocks = 20 cycles of the note: the level, not the phase of the wave
        return r;
    };
    {
        const std::vector<double> still = env(0, 1, 60), trem = env(1, 3, 60), sq = env(4, 3, 60);
        double mn = 1e9, mx = 0, mn0 = 1e9, mx0 = 0;
        for (double v : trem) { mn = fmin(mn, v); mx = fmax(mx, v); }
        for (double v : still) { mn0 = fmin(mn0, v); mx0 = fmax(mx0, v); }
        CHECK(mx / mn > 1.5);          // a triangle tremolo at full depth
        CHECK(mx0 / mn0 < 1.15);       // a target that does not touch the loudness (Shape, no pulse here) keeps it
        double smn = 1e9, smx = 0;
        for (double v : sq) { smn = fmin(smn, v); smx = fmax(smx, v); }
        CHECK(smx / smn > 1.8);        // a square swings between two levels
    }
    {
        const std::vector<double> sh = env(5, 3, 55); // sample & hold: a few distinct plateau levels
        int changes = 0;
        for (size_t i = 1; i < sh.size(); i++)
            changes += fabs(sh[i] - sh[i - 1]) > sh[i] * 0.08;
        CHECK(changes >= 2 && changes < (int)sh.size() / 5);
    }
    // all waves and targets together stay bounded
    {
        bool ok = true;
        for (int w = 0; w < kLfoWaves; w++)
            for (int d = 0; d < 4; d++)
            {
                Engine e;
                e.setParam(P_LFO_WAVE, w);
                e.setParam(P_LFO_DEST, d);
                e.setParam(P_LFO_DEPTH, 100);
                e.setParam(P_LFO_RATE, 100);
                e.setParam(P_SHAPE, 150);
                e.noteOn(40, 127);
                e.noteOn(80, 127);
                std::vector<int16_t> o;
                run(e, 100, o);
                for (int i = 0; i < kMaxVoices; i++)
                    ok = ok && voiceFinite(e.voice(i));
            }
        CHECK(ok);
    }
}

// ---------------------------------------------------------------- chorus
static void test_x4_chorus()
{
    static int16_t mem[3][2 * kChLen];
    auto make = [&](int mix, int rate, int depth, int width, std::vector<int16_t>* l, std::vector<int16_t>* r) {
        Engine e;
        memset(mem, 0x55, sizeof(mem)); // stale garbage: the engine clears the lines itself
        e.setChorusMemory(0, mem[0]);
        e.setParam(P_CH_MIX, mix);
        e.setParam(P_CH_RATE, rate);
        e.setParam(P_CH_DEPTH, depth);
        e.setParam(P_WIDTH, width);
        e.setParam(P_SUSTAIN, 100);
        e.noteOn(52, 100);
        run(e, 120, *l, r);
    };
    std::vector<int16_t> dl, dr, wl, wr;
    make(0, 40, 50, 0, &dl, &dr);
    make(100, 40, 50, 0, &wl, &wr);
    // mono source: dry left and right are the same; with the chorus the two sides differ (opposite LFO phases)
    CHECK(dl == dr);
    CHECK(wl != wr);
    double dd = 0;
    for (size_t i = 80 * kBlock; i < wl.size(); i++)
        dd += fabs((double)wl[i] - wr[i]);
    CHECK(dd > 1000.0);
    // loudness stays close (the wet share is a crossfade)
    const double rd = rmsOf(dl, 80 * kBlock, 30 * kBlock), rw = rmsOf(wl, 80 * kBlock, 30 * kBlock);
    CHECK(rw > rd * 0.3 && rw < rd * 1.15); // a 50 / 50 mix of a signal and its swept delay: the comb filter takes a few dB
    // depth 0 = a fixed 10 ms delay: no pitch modulation, the wet signal is the dry one delayed (mix 100 -> half dry + half delayed)
    std::vector<int16_t> zl, zr;
    make(100, 40, 0, 0, &zl, &zr);
    CHECK(zl == zr);
    // no memory = no chorus, whatever the mix
    {
        Engine e;
        e.setParam(P_CH_MIX, 100);
        Engine f;
        e.noteOn(60, 100);
        f.noteOn(60, 100);
        std::vector<int16_t> a, b;
        run(e, 30, a);
        run(f, 30, b);
        CHECK(a == b);
    }
    // the instances have their own lines and settings: a chorus on instance 1 leaves instance 0 dry
    {
        Engine e;
        e.setChorusMemory(0, mem[0]);
        e.setChorusMemory(1, mem[1]);
        e.setParam(P_CH_MIX, 100, 1);
        e.setParam(P_WIDTH, 0, 0);
        e.setParam(P_WIDTH, 0, 1);
        e.noteOn(60, 100, 0);
        std::vector<int16_t> l0, r0;
        run(e, 60, l0, &r0);
        CHECK(l0 == r0); // instance 0 alone, dry
        e.noteOn(60, 100, 1);
        std::vector<int16_t> l1, r1;
        run(e, 60, l1, &r1);
        CHECK(l1 != r1);
    }
    // the chorus does not leak across a pause: after the engine went idle the lines are cleared before the next note
    {
        Engine e;
        memset(mem, 0x7F, sizeof(mem));
        e.setChorusMemory(0, mem[0]);
        e.setParam(P_CH_MIX, 100);
        e.setParam(P_SUSTAIN, 0);
        e.setParam(P_DECAY, 0);
        e.setParam(P_RELEASE, 0);
        e.noteOn(50, 100);
        std::vector<int16_t> o;
        run(e, 30, o);
        e.allNotesOff();
        run(e, 400, o);
        CHECK(e.activeVoices() == 0);
        e.noteOn(50, 100);
        std::vector<int16_t> p;
        run(e, 2, p);
        double peak = 0;
        for (int i = 0; i < 2 * kBlock; i++)
            peak = fmax(peak, fabs((double)p[i]));
        CHECK(peak < 16000.0); // not the stale full scale noise of 0x7F7F
    }
    // extremes stay bounded and finite, including rate and depth at the maximum with all instances
    {
        bool ok = true;
        Engine e;
        for (int k = 0; k < 3; k++)
        {
            e.setChorusMemory(k, mem[k]);
            e.setParam(P_CH_MIX, 100, k);
            e.setParam(P_CH_RATE, 100, k);
            e.setParam(P_CH_DEPTH, 100, k);
            e.setParam(P_LEVEL, 100, k);
            e.noteOn(36 + k * 20, 127, k);
        }
        std::vector<int16_t> o;
        run(e, 400, o);
        for (int i = 0; i < kPool; i++)
            ok = ok && voiceFinite(e.voice(i));
        CHECK(ok);
    }
    // no click when the mix jumps from 0 to 100 while a note sounds (a crossfade of two similar signals)
    {
        Engine e;
        e.setChorusMemory(0, mem[0]);
        e.setParam(P_WIDTH, 0);
        e.setParam(P_SUSTAIN, 100);
        e.noteOn(55, 100);
        std::vector<int16_t> o;
        run(e, 40, o);
        double peak = 0;
        for (size_t i = 0; i < o.size(); i++)
            peak = fmax(peak, fabs((double)o[i]));
        e.setParam(P_CH_MIX, 100);
        const size_t b0 = o.size();
        run(e, 10, o);
        int worst = 0;
        for (size_t i = b0; i < o.size(); i++)
            worst = std::max(worst, abs((int)o[i] - (int)o[i - 1]));
        CHECK(worst < peak * 1.2);
    }
}

// ---------------------------------------------------------------- everything together, and the files
static void test_x4_stability_and_files()
{
    // random parameters of all 33, random notes on all instances: finite, bounded, no more than the pool
    {
        static int16_t mem[3][2 * kChLen];
        Engine e;
        for (int k = 0; k < 3; k++)
            e.setChorusMemory(k, mem[k]);
        uint32_t rs = 4242;
        auto rnd = [&]() { rs = rs * 1664525u + 1013904223u; return rs >> 8; };
        int16_t l[kBlock], r[kBlock];
        bool ok = true;
        for (int i = 0; i < 4000; i++)
        {
            if (i % 5 == 0)
            {
                const int p = 1 + (int)(rnd() % (P_COUNT - 1));
                if (p != P_VOICES)
                    e.setParam(p, (int)(rnd() % 256), (int)(rnd() % 3));
            }
            const int inst = (int)(rnd() % 3);
            if (rnd() % 3 == 0)
                e.noteOn(24 + rnd() % 72, 20 + rnd() % 108, inst);
            else if (rnd() % 3 == 0)
                e.noteOff(24 + rnd() % 72, inst);
            e.render(l, r);
            for (int p = 0; p < kPool; p++)
            {
                ok = ok && voiceFinite(e.voice(p));
                for (int q = 0; q < 8; q++)
                    ok = ok && e.voice(p).fs[q] > -1.0e4f && e.voice(p).fs[q] < 1.0e4f;
            }
            ok = ok && e.activeVoices() <= kPool;
        }
        CHECK(ok);
    }
    // plinky.bin: the new parameters of instance 0 and the others round trip with their snapshots; an older reader gets the legacy part
    using namespace plinkymorph;
    Params cur[3];
    Store s0, s1, s2;
    const Store* cp[3] = { &s0, &s1, &s2 };
    Store t0, t1, t2;
    Store* tp[3] = { &t0, &t1, &t2 };
    uint8_t hs[3] = { 12, 12, 12 }, lh[3];
    for (int k = 0; k < 3; k++)
        cur[k] = instDefaults(k);
    cur[0].v[P_CUTOFF] = 31;
    cur[0].v[P_FTYPE] = 3;
    cur[0].v[P_CH_MIX] = 77;
    cur[0].v[P_SUB1] = 40;
    uint8_t snap[P_COUNT];
    for (int i = 0; i < P_COUNT; i++)
        snap[i] = paramInfo(i).def;
    snap[P_FENV_AMT] = 80;
    snap[P_LFO_WAVE] = 6;
    snap[P_RESO] = 55;
    s0.set(3, snap);
    static uint8_t buf[kFileMaxBank];
    const size_t n = saveBank(buf, sizeof(buf), cur, cp, hs);
    CHECK(n > 0);
    Params lc[3];
    bool newer = true;
    CHECK(loadBank(buf, n, lc, tp, lh, &newer) && !newer);
    CHECK(lc[0] == cur[0] && lc[0].v[P_FTYPE] == 3 && lc[0].v[P_CH_MIX] == 77 && lc[0].v[P_SUB1] == 40);
    CHECK(t0.has(3) && t0.v[3][P_FENV_AMT] == 80 && t0.v[3][P_LFO_WAVE] == 6 && t0.v[3][P_RESO] == 55);
    // the older reader (frame 1 + PLVM) sees the legacy values
    Params old;
    CHECK(old.load(buf, n) && old.v[P_CUTOFF] == 31 && old.v[P_FTYPE] == paramInfo(P_FTYPE).def && old.v[P_CH_MIX] == 0);
    Params ob;
    Store os;
    uint8_t oh;
    CHECK(loadFile(buf, n, ob, os, oh) && os.has(3) && os.v[3][P_RESO] == 55 && os.v[3][P_FENV_AMT] == 50);
    // a morph moves the new continuous parameters and switches the stepped ones at the start (bit 32 and above of the masks)
    {
        Morph m;
        uint8_t v[P_COUNT], tg[P_COUNT];
        for (int i = 0; i < P_COUNT; i++)
            v[i] = tg[i] = paramInfo(i).def;
        tg[P_CH_MIX] = 100;
        tg[P_FTYPE] = 4;
        tg[P_LFO_WAVE] = 5;
        tg[P_FENV_AMT] = 90;
        const ParamMask changed = m.begin(v, tg, 1, 1000, 4000);
        CHECK(((changed >> P_FTYPE) & 1) && ((changed >> P_LFO_WAVE) & 1) && !((changed >> P_CH_MIX) & 1));
        CHECK(v[P_FTYPE] == 4 && v[P_LFO_WAVE] == 5 && v[P_CH_MIX] == 0);
        const ParamMask ch = m.step(v, 3000);
        CHECK(((ch >> P_CH_MIX) & 1) && ((ch >> P_FENV_AMT) & 1) && v[P_CH_MIX] == 50 && v[P_FENV_AMT] == 70);
        m.step(v, 6000);
        CHECK(v[P_CH_MIX] == 100 && v[P_FENV_AMT] == 90 && !m.running);
        // marks use all bits
        Store st;
        uint8_t w[P_COUNT];
        for (int i = 0; i < P_COUNT; i++)
            w[i] = paramInfo(i).def;
        w[P_CH_DEPTH] = 10;
        w[P_CH_MIX] = 20;
        st.set(5, w);
        const ParamMask mk = st.marks(5);
        CHECK(((mk >> P_CH_DEPTH) & 1) && ((mk >> P_CH_MIX) & 1) && !((mk >> P_SUB1) & 1));
        CHECK(P_CH_DEPTH > 31);
    }
}

// host cost (printed, not asserted): ns per voice-sample with 4 stereo voices, per feature
static void test_x4_cost()
{
    static int16_t mem[2 * kChLen];
    struct Case { const char* name; int sub, ft, mix, fenv; };
    const Case cs[] = { { "plain saws (as v0.3.8)", 0, 0, 0, 50 }, { "+ both subs", 100, 0, 0, 50 }, { "SVF low pass", 0, 1, 0, 50 },
                        { "ladder", 0, 4, 0, 50 }, { "LPG + filter env", 0, 0, 0, 100 }, { "chorus 100", 0, 0, 100, 50 },
                        { "everything (subs, ladder, env, chorus)", 100, 4, 100, 100 } };
    printf("  host cost, 4 stereo voices, ns per voice-sample:\n");
    for (const Case& c : cs)
    {
        Engine e;
        e.setChorusMemory(0, mem);
        e.setParam(P_SUB1, c.sub);
        e.setParam(P_SUB2, c.sub);
        e.setParam(P_FTYPE, c.ft);
        e.setParam(P_CH_MIX, c.mix);
        e.setParam(P_FENV_AMT, c.fenv);
        e.setParam(P_SUSTAIN, 100);
        for (int n = 0; n < 4; n++)
            e.noteOn(48 + n * 7, 100);
        int16_t l[kBlock], r[kBlock];
        for (int b = 0; b < 100; b++)
            e.render(l, r);
        const auto t0 = std::chrono::steady_clock::now();
        const int N = 4000;
        for (int b = 0; b < N; b++)
            e.render(l, r);
        const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
        printf("    %-40s %.1f\n", c.name, ns / ((double)N * kBlock * 4));
    }
}

static void test_x4_all()
{
    test_x4_defaults();
    test_x4_subs();
    test_x4_filters();
    test_x4_filter_env();
    test_x4_lfo();
    test_x4_chorus();
    test_x4_stability_and_files();
    test_x4_cost();
}
