// Sequencer tracks mod (plan 024) - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Included by test_beatrepeat.cpp (uses its Rig, CHECK and signal helpers).
// Plan 024: pre-roll (the page being open fills the ring all the time), MIX level, band-pass on the repeats, the master high-pass.
static double rms(const std::vector<int16_t>& v, size_t a, size_t b)
{
    double s = 0;
    for (size_t i = a; i < b; i++)
        s += (double)v[i] * (double)v[i];
    return sqrt(s / (double)(b - a));
}
static void toneSig(double hz, double amp, long f, int16_t& l, int16_t& r)
{
    l = (int16_t)lrint(amp * sin(2.0 * 3.14159265358979 * hz * (double)f / (double)kFs));
    r = l;
}

// first frame at or after `from` where the output differs from the input (INSERT: the repeat has started, the audio ramps out)
static long firstDiff(const Rig& r, long from)
{
    for (size_t i = (size_t)from; i < r.outL.size(); i++)
        if (r.outL[i] != r.inL[i])
            return (long)i;
    return -1;
}

static void test_preroll()
{
    // pre-roll keeps the stage out of bypass; switching it off (nothing else going on) gives the bypass back
    {
        Rig r;
        CHECK(r.c.bypassable());
        r.c.post(Core::C_PRE, 1);
        r.run(3, idSig);
        CHECK(r.c.preroll() && !r.c.bypassable());
        CHECK(r.c.history() > 0);
        r.c.post(Core::C_PRE, 0);
        r.run(3, idSig);
        CHECK(!r.c.preroll() && r.c.bypassable() && r.c.history() == 0);
    }
    // a press while recording ahead: the repeats start at the NEXT boundary with the cell that has just ended - at most one grid after the
    // press (without pre-roll: capture first, the repeats start one grid later), the content equals the input of that cell
    for (int tempo = 0; tempo < 3; tempo++)
    {
        const double bpms[3] = { 97.3, 120.0, 168.0 };
        Rig r;
        r.bpm = bpms[tempo];
        r.s.mode = MODE_INSERT;
        r.c.setSettings(r.s);
        r.c.post(Core::C_PRE, 1);
        r.run(300, idSig); // plenty of history
        const long pressFrame = r.frame;
        r.c.post(Core::C_DOWN, 2); // 1/16
        r.run(120, idSig);
        CHECK(r.c.captures() == 1 && r.c.state() == S_REPEAT);
        const long L = (long)r.c.captured();
        CHECK(labs(L - (long)floor(48.0 * r.fpt() + 0.5)) <= 1);
        const double bTick = ceil((double)pressFrame / r.fpt() / 48.0) * 48.0;
        const double Sm = r.frameOfTick(bTick);
        const long S = firstDiff(r, pressFrame);
        CHECK(labs(S - (long)Sm) <= 4); // the repeats start at the boundary of the clock
        bool same = true;
        for (long m = 100; m < L - 100 && same; m += 53)
            same = r.outL[(size_t)(S + m)] == r.inL[(size_t)(S - L + m)] && r.outR[(size_t)(S + m)] == r.inR[(size_t)(S - L + m)];
        CHECK(same); // the cell that has just ended, exactly
        CHECK(S - pressFrame <= L + 3); // the first repeat sounds at most one grid after the press
        // the same press without the pre-roll: the first repeat comes one grid later
        Rig q;
        q.bpm = bpms[tempo];
        q.s.mode = MODE_INSERT;
        q.c.setSettings(q.s);
        q.run(300, idSig);
        q.c.post(Core::C_DOWN, 2);
        q.run(120, idSig);
        const long Lq = (long)q.c.captured();
        const long Sq = firstDiff(q, pressFrame);
        CHECK(labs(Sq - S - Lq) <= 4); // one grid later than with the pre-roll
    }
    // not enough history (the page was opened a moment ago): the capture of plan 021 takes over - no garbage
    {
        Rig r;
        r.s.mode = MODE_INSERT;
        r.c.setSettings(r.s);
        r.c.post(Core::C_PRE, 1);
        r.c.post(Core::C_DOWN, 0); // 1/4 = 192 ticks, ~22000 frames
        r.run(60, idSig);          // 7680 frames of history only
        CHECK(r.c.state() == S_PENDING || r.c.state() == S_CAPTURE);
        r.run(400, idSig);
        CHECK(r.c.state() == S_REPEAT && r.c.captures() == 1);
        const long L = (long)r.c.captured();
        const size_t n = r.outL.size();
        const long S = firstDiff(r, 60 * 128);
        bool same = true;
        for (long m = 200; m < L - 200 && same; m += 97)
            same = r.outL[(size_t)(S + m)] == r.inL[(size_t)(S - L + m)];
        CHECK(same); // the captured cell, exactly (the pre-roll history was too short for the fast start)
    }
    // the ring wraps: a small ring (4096 frames) and a 1/32 grid (2757 frames at 120 BPM) over many wraps; the slice crosses the end
    {
        Rig r(4096);
        r.s.mode = MODE_INSERT;
        r.c.setSettings(r.s);
        r.c.post(Core::C_PRE, 1);
        r.run(97, idSig);
        r.c.post(Core::C_DOWN, 3);
        r.run(70, idSig);
        CHECK(r.c.state() == S_REPEAT);
        const long L = (long)r.c.captured();
        const long S = firstDiff(r, 97 * 128);
        bool same = true;
        for (long m = 100; m < L - 100 && same; m += 41)
            same = r.outL[(size_t)(S + m)] == r.inL[(size_t)(S - L + m)];
        CHECK(same);
        CHECK(labs(S - (long)r.frameOfTick(ceil((double)(97 * 128) / r.fpt() / 24.0) * 24.0)) <= 4);
        CHECK(r.guardsOk(4096));
        CHECK(r.c.sliceBase() < 4096);
    }
    // releasing ends the repeat and the recording goes on: a second press is as quick as the first
    {
        Rig r;
        r.s.mode = MODE_INSERT;
        r.c.setSettings(r.s);
        r.c.post(Core::C_PRE, 1);
        r.run(200, idSig);
        r.c.post(Core::C_DOWN, 3);
        r.run(80, idSig);
        r.c.post(Core::C_UP);
        r.run(60, idSig); // the history builds up again (a grid is 24 ticks = 2757 frames = 22 blocks)
        CHECK(r.c.state() == S_IDLE && r.c.history() > 2757);
        const uint32_t before = r.c.captures();
        r.c.post(Core::C_DOWN, 3);
        r.run(30, idSig);
        CHECK(r.c.captures() == before + 1 && r.c.state() == S_REPEAT);
    }
}

static void test_mix_level()
{
    // DC input 8000: in MIX the output inside a repeat is 8000 + mix * 8000 (the repeat of a constant is the constant, between the fades)
    const uint8_t mixes[4] = { 0, 50, 100, 25 };
    for (int k = 0; k < 4; k++)
    {
        Rig r;
        r.s.mode = MODE_MIX;
        r.s.mix = mixes[k];
        r.c.setSettings(r.s);
        r.c.post(Core::C_PRE, 1);
        r.run(200, dcSig);
        r.c.post(Core::C_DOWN, 2);
        r.run(120, dcSig);
        CHECK(r.c.state() == S_REPEAT);
        const double want = 8000.0 + 8000.0 * mixes[k] / 100.0;
        double best = 1e9;
        for (size_t i = r.outL.size() - 2000; i < r.outL.size(); i++) // the middle of a repeat reaches the full value
            best = fmin(best, fabs((double)r.outL[i] - want));
        CHECK(best < 3.0);
        double top = 0;
        for (size_t i = r.outL.size() - 2000; i < r.outL.size(); i++)
            top = fmax(top, (double)r.outL[i]);
        CHECK(top < want + 3.0); // never louder than dry + mix * repeat
        // INSERT: the level has no effect (the repeats replace the audio at full level)
        Rig q;
        q.s.mode = MODE_INSERT;
        q.s.mix = mixes[k];
        q.c.setSettings(q.s);
        q.c.post(Core::C_PRE, 1);
        q.run(200, dcSig);
        q.c.post(Core::C_DOWN, 2);
        q.run(120, dcSig);
        double hi = 0;
        for (size_t i = q.outL.size() - 2000; i < q.outL.size(); i++)
            hi = fmax(hi, (double)q.outL[i]);
        CHECK(hi > 7997.0 && hi < 8003.0);
    }
    // the level is ramped (no step): from 100 to 0 while repeating the largest sample step stays small
    {
        Rig r;
        r.s.mode = MODE_MIX;
        r.c.setSettings(r.s);
        r.c.post(Core::C_PRE, 1);
        r.run(200, dcSig);
        r.c.post(Core::C_DOWN, 2);
        r.run(40, dcSig);
        r.s.mix = 0;
        r.c.setSettings(r.s);
        const size_t from = r.outL.size();
        r.run(8, dcSig);
        int mx = 0;
        for (size_t i = from + 1; i < r.outL.size(); i++)
            mx = std::max(mx, abs((int)r.outL[i] - (int)r.outL[i - 1]));
        CHECK(mx <= 8000 / 256 + 260); // 1/256 of the repeat per frame, plus the 32 frame fades of the repeat itself (8000 / 32)
    }
}

static void test_bandpass()
{
    // INSERT, output = the repeats only: a tone through a band-pass centred on 3.2 kHz (1 octave wide)
    auto level = [](double hz, uint16_t centre, uint8_t tw) {
        Rig r;
        r.s.mode = MODE_INSERT;
        r.s.bpHz = centre;
        r.s.bpTw = tw;
        r.c.setSettings(r.s);
        r.c.post(Core::C_PRE, 1);
        auto sg = [hz](long f, int16_t& l, int16_t& rr) { toneSig(hz, 8000.0, f, l, rr); };
        r.run(600, sg);
        r.c.post(Core::C_DOWN, 1); // 1/8 = 5514 frames at 120 BPM
        r.run(200, sg);
        const size_t n = r.outL.size();
        return rms(r.outL, n - 4000, n - 400);
    };
    const double ref = 8000.0 / sqrt(2.0);
    const double low = level(100.0, 3200, 12), mid = level(3200.0, 3200, 12), open = level(100.0, 20000, 12);
    CHECK(open > 0.9 * ref && open < 1.1 * ref); // open (>= 19000): the filter is out of the path, the repeat is plain
    CHECK(mid > 0.7 * ref && mid < 1.3 * ref);   // the centre passes
    CHECK(low < 0.15 * ref);                     // 100 Hz is far outside: strongly attenuated
    const double wideSide = level(800.0, 3200, 36), narrowSide = level(800.0, 3200, 4);
    CHECK(narrowSide < wideSide);                // a narrower band passes less of what is next to the centre
    // moving the centre while repeating does not click (smoothed per block)
    {
        Rig r;
        r.s.mode = MODE_INSERT;
        r.s.bpHz = 3200;
        r.c.setSettings(r.s);
        r.c.post(Core::C_PRE, 1);
        auto sg = [](long f, int16_t& l, int16_t& rr) { toneSig(500.0, 6000.0, f, l, rr); };
        r.run(300, sg);
        r.c.post(Core::C_DOWN, 1);
        r.run(60, sg);
        r.s.bpHz = 400;
        r.c.setSettings(r.s);
        const size_t from = r.outL.size();
        r.run(20, sg);
        int mx = 0;
        for (size_t i = from + 1; i < r.outL.size(); i++)
            mx = std::max(mx, abs((int)r.outL[i] - (int)r.outL[i - 1]));
        CHECK(mx < 3000);
    }
}

static void test_master_hp()
{
    CHECK(!MasterHp().active()); // off: nothing to do
    auto gain = [](double hz, uint8_t idx, uint8_t res) {
        MasterHp hp;
        hp.set(idx, res);
        std::vector<int16_t> o;
        for (int b = 0; b < 200; b++)
        {
            int16_t l[128], r[128];
            for (int i = 0; i < 128; i++)
                toneSig(hz, 8000.0, (long)b * 128 + i, l[i], r[i]);
            hp.process(l, r, 128);
            for (int i = 0; i < 128; i++)
                o.push_back(l[i]);
        }
        return rms(o, o.size() - 6400, o.size()) / (8000.0 / sqrt(2.0));
    };
    // idx 62 = 30 * 2^(61/12) = 1013 Hz: 12 dB / octave
    CHECK(fabs(seqfx::hpHz(62) - 1013.0f) < 30.0f);
    const double g100 = gain(100.0, 62, 0), g500 = gain(500.0, 62, 0), g1k = gain(1013.0, 62, 0), g8k = gain(8000.0, 62, 0);
    CHECK(g100 < 0.02);                    // a decade below the corner: -40 dB
    CHECK(g500 > g100 * 4 && g500 < 0.4);  // one octave below: about -12 dB
    CHECK(fabs(g1k - 0.707) < 0.08);       // -3 dB at the corner (Butterworth)
    CHECK(fabs(g8k - 1.0) < 0.05);         // the pass band is flat
    CHECK(gain(1013.0, 62, 100) > 3.0 * g1k); // resonance raises the corner (Q 5.7)
    CHECK(gain(1013.0, 62, 100) > gain(1013.0, 62, 50));
    // on -> off: crossfaded (no step), and once settled the stage is inactive again (zero cost, the wrapper does not call it)
    {
        MasterHp hp;
        std::vector<int16_t> o;
        auto blk = [&](long b) {
            int16_t l[128], r[128];
            for (int i = 0; i < 128; i++)
                toneSig(200.0, 8000.0, b * 128 + i, l[i], r[i]);
            hp.process(l, r, 128);
            for (int i = 0; i < 128; i++)
                o.push_back(l[i]);
        };
        hp.set(70, 0);
        CHECK(hp.active());
        for (long b = 0; b < 100; b++)
            blk(b);
        hp.set(0, 0);
        CHECK(hp.active()); // the crossfade is still running
        for (long b = 100; b < 200; b++)
            blk(b);
        CHECK(!hp.active());
        int mx = 0;
        for (size_t i = 1; i < o.size(); i++)
            mx = std::max(mx, abs((int)o[i] - (int)o[i - 1]));
        CHECK(mx < 8000 * 2 * 3.14159 * 200.0 / 44117.0 * 1.8); // never more than the slope of the tone itself (with the filter phase shift)
    }
    // the stage as the wrapper uses it: the high-pass alone (no repeat, no pre-roll) keeps the core in bypass but the wrapper active
    {
        Rig r;
        Settings s;
        s.hpIdx = 62;
        r.c.setSettings(s);
        CHECK(r.c.bypassable() && r.c.hp().active());
        s.hpIdx = 0;
        r.c.setSettings(s);
    }
}
