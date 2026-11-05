// Plan 009 P#4: the delay (real effect_shortDelay.cpp, host stub of AudioStream) against the frozen pre-change copy (ref/delay).
// Included by test_cpu.cpp.
#include <new>
#include "effect_shortDelay.h"
#include "ref_shortDelay.h"

extern audio_block_t* stubIn[8];
extern audio_block_t* stubOut[8];
int16_t externalRamDelayBuff[3 * 262144]; // referenced by begin(); the tests give each instance its own buffer with setBuffer()

namespace
{
struct DelayRun
{
    std::vector<int16_t> out[2]; // per output index, whole run
    int transmitted = 0;         // blocks that were sent downstream
};

// feeds `in` (mono, 128 per block) block by block; `ev(block)` is called before each block (parameter changes)
template<typename D, typename Ev>
DelayRun runDelay(D& d, const std::vector<int16_t>& in, bool nullWhenZero, Ev ev)
{
    DelayRun r;
    const int blocks = (int)in.size() / 128;
    r.out[0].assign(in.size(), 0);
    r.out[1].assign(in.size(), 0);
    for (int b = 0; b < blocks; b++)
    {
        ev(b);
        bool allZero = true;
        for (int s = 0; s < 128; s++)
            allZero &= in[b * 128 + s] == 0;
        for (int k = 0; k < 2; k++)
        {
            if (stubOut[k])
            {
                stubOut[k]->ref_count = 1;
                // release through the stub's own free path: drop the pointer, the test leaks nothing material (small blocks)
                free(stubOut[k]);
                stubOut[k] = nullptr;
            }
        }
        if (nullWhenZero && allZero)
            stubIn[0] = nullptr;
        else
        {
            audio_block_t* blk = (audio_block_t*)calloc(1, sizeof(audio_block_t));
            blk->ref_count = 1;
            memcpy(blk->data, &in[b * 128], 256);
            stubIn[0] = blk;
        }
        d.update();
        bool sent = false;
        for (int k = 0; k < 2; k++)
            if (stubOut[k])
            {
                memcpy(&r.out[k][b * 128], stubOut[k]->data, 256);
                sent = true;
            }
        r.transmitted += sent;
    }
    return r;
}

std::vector<int16_t> delayInput(int blocks, int burstEvery, int burstLen, int amp)
{
    std::vector<int16_t> in(blocks * 128, 0);
    for (int b = 0; b < blocks; b++)
        if (b % burstEvery < burstLen)
            for (int s = 0; s < 128; s++)
                in[b * 128 + s] = (int16_t)((int)(rnd() % (2 * amp + 1)) - amp);
    return in;
}

void setupPair(AudioEffectShortDelay& nd, RefShortDelay& rd, std::vector<int16_t>& nb, std::vector<int16_t>& rb, float fb, uint32_t timeMs, bool pp,
               float freq)
{
    nb.assign(3 * 262144, 0x5a5a);
    rb.assign(3 * 262144, 0);
    nd.begin(fb, timeMs, pp, false, 0, freq);
    rd.begin(fb, timeMs, pp, false, 0, freq);
    nd.setBuffer(nb.data()); // clears the lines
    rd.setBuffer(rb.data());
    nd.unblockUpdate();
    rd.unblockUpdate();
}

bool sameOut(const DelayRun& a, const DelayRun& b)
{
    size_t nz = 0;
    for (size_t i = 0; i < a.out[0].size(); i++)
        nz += a.out[0][i] != 0 || a.out[1][i] != 0;
    return nz > 5000 && a.out[0] == b.out[0] && a.out[1] == b.out[1]; // and the run is not trivially silent
}
} // namespace

static void testDelay()
{
    struct Case
    {
        float fb;
        uint32_t timeMs;
        bool pp;
        float freq;
        const char* name;
    };
    const Case cases[] = {
        { 0.5f, 120, true, 20000.0f, "ping-pong, open" },   { 0.88f, 20, false, 20000.0f, "mono, open, max feedback" },
        { 0.6f, 130, true, 1000.0f, "ping-pong, bandpass" }, { 0.7f, 90, false, 600.0f, "mono, bandpass" },
    };
    for (const Case& c : cases)
    {
        const int blocks = 2600; // 7.5 s: bursts, then long silences (idle skip)
        std::vector<int16_t> in = delayInput(blocks, 700, 25, 12000);
        static AudioEffectShortDelay nd;
        static RefShortDelay rd;
        std::vector<int16_t> nb, rb;
        setupPair(nd, rd, nb, rb, c.fb, c.timeMs, c.pp, c.freq);
        DelayRun a = runDelay(rd, in, false, [](int) {});
        DelayRun b = runDelay(nd, in, true, [](int) {});
        printf("  delay %-28s: %d of %d blocks sent (old: %d)\n", c.name, b.transmitted, blocks, a.transmitted);
        CHECK(sameOut(a, b));
        CHECK(b.transmitted < a.transmitted); // the idle blocks were skipped
        CHECK(b.transmitted > 100);
    }
    // time and feedback changes while running, null and zero input blocks mixed
    {
        const int blocks = 2600;
        std::vector<int16_t> in = delayInput(blocks, 500, 30, 9000);
        static AudioEffectShortDelay nd;
        static RefShortDelay rd;
        std::vector<int16_t> nb, rb;
        setupPair(nd, rd, nb, rb, 0.5f, 200, false, 20000.0f);
        DelayRun a = runDelay(rd, in, false, [&](int b) { if (b == 400) rd.setTime(1500); if (b == 1500) rd.setTime(60); if (b == 1800) rd.setFeedback(0.8f); });
        DelayRun b = runDelay(nd, in, true, [&](int b) { if (b == 400) nd.setTime(1500); if (b == 1500) nd.setTime(60); if (b == 1800) nd.setFeedback(0.8f); });
        CHECK(sameOut(a, b));
    }
    // ping-pong <-> mono: the lines that start to be heard behave as cleared at the switch (the old code gets clear() at the switch)
    {
        const int blocks = 1800;
        std::vector<int16_t> in = delayInput(blocks, 150, 100, 8000);
        static AudioEffectShortDelay nd;
        static RefShortDelay rd;
        std::vector<int16_t> nb, rb;
        setupPair(nd, rd, nb, rb, 0.7f, 250, true, 1200.0f);
        const int sw[] = { 300, 640, 1100, 1111 };
        auto evNew = [&](int b) { for (int k = 0; k < 4; k++) if (b == sw[k]) nd.setPingpongEnable(k % 2 == 0 ? false : true); };
        auto evRef = [&](int b) { for (int k = 0; k < 4; k++) if (b == sw[k]) { rd.setPingpongEnable(k % 2 == 0 ? false : true); rd.clear(); } };
        DelayRun a = runDelay(rd, in, false, evRef);
        DelayRun b = runDelay(nd, in, true, evNew);
        CHECK(sameOut(a, b));
    }
}

// ---- Plan 009 P#5: limiter (real effect_limiter.cpp, stubbed project values) against the frozen old one (ref/limiter) ----
#include "mtStructs.h" // the stub in stubs/
#include "effect_limiter.h"
#include "ref_limiter.h"
StubProject mtProject;

template<typename L>
static std::vector<int16_t> runLimiter(L& lim, const std::vector<int16_t>& main, const std::vector<int16_t>* side, bool sidechainOn,
                                       std::vector<int16_t>* sideOut = nullptr)
{
    std::vector<int16_t> out(main.size());
    lim.setSidechainEnable(sidechainOn);
    for (size_t b = 0; b < main.size() / 128; b++)
    {
        audio_block_t* m = (audio_block_t*)calloc(1, sizeof(audio_block_t));
        m->ref_count = 1;
        memcpy(m->data, &main[b * 128], 256);
        stubIn[0] = m;
        if (side)
        {
            audio_block_t* sc = (audio_block_t*)calloc(1, sizeof(audio_block_t));
            sc->ref_count = 1;
            memcpy(sc->data, &(*side)[b * 128], 256);
            stubIn[1] = sc;
        }
        if (stubOut[0])
        {
            free(stubOut[0]);
            stubOut[0] = nullptr;
        }
        lim.update();
        memcpy(&out[b * 128], stubOut[0]->data, 256);
        (void)sideOut;
    }
    return out;
}

static void testLimiter()
{
    struct Case { uint16_t attack, release, thr; uint8_t mode; const char* name; };
    const Case cases[] = { { 10, 100, 20000, limiterModeSubtle, "subtle, thr 20000" },
                           { 3, 400, 4000, limiterModeSubtle, "subtle, low threshold" },
                           { 20, 200, 30000, limiterModeHard, "hard" } };
    for (const Case& c : cases)
    {
        const int blocks = 900;
        std::vector<int16_t> main(blocks * 128), side(blocks * 128);
        for (size_t i = 0; i < main.size(); i++)
        {
            // loud noise bursts, quiet noise and full-scale extremes (-32768 included)
            int ph = (int)(i / 128 / 60) % 4;
            int amp = ph == 0 ? 32767 : ph == 1 ? 600 : ph == 2 ? 15000 : 0;
            main[i] = amp ? (int16_t)((int)(rnd() % (2 * amp + 1)) - amp) : 0;
            side[i] = (int16_t)((int)(rnd() % 40001) - 20000);
            if (rnd() % 997 == 0) main[i] = -32768;
        }
        mtProject.values.limiterAttack = c.attack;
        mtProject.values.limiterRelease = c.release;
        mtProject.values.limiterThreshold = c.thr;
        mtProject.values.limiterMode = c.mode;
        for (int pass = 0; pass < 2; pass++) // 0: main stream, 1: sidechain
        {
            // fresh zeroed objects like the statics on the device (the old begin() cleared the input ring only)
            AudioEffectLimiter& nl = *new (calloc(1, sizeof(AudioEffectLimiter))) AudioEffectLimiter();
            RefLimiter& rl = *new (calloc(1, sizeof(RefLimiter))) RefLimiter();
            nl.begin();
            rl.begin();
            std::vector<int16_t> a = runLimiter(rl, main, pass ? &side : nullptr, pass == 1);
            std::vector<int16_t> b = runLimiter(nl, main, pass ? &side : nullptr, pass == 1);
            CHECK(a == b);
            size_t nz = 0;
            for (size_t i = 0; i < a.size(); i++) nz += a[i] != 0;
            CHECK(nz > a.size() / 4);
        }
        printf("  limiter %-24s: identical\n", c.name);
    }
}

// ---- Plan 009 P#2: idle voices send no block. Voice chain (filter -> amplifier -> mono-to-stereo -> mixer), once fed zero blocks while
// the voice is idle (the old behaviour) and once fed nothing; the output must be the same (bit-exact without filter; the filter tail
// below a quarter of an output LSB may be cut).
#include "filter_variable.h"
#include "amplifier.h"
#include "monoToStereo.h"
#include "mixer.h"

namespace
{
struct VoiceChain
{
    AudioFilterStateVariable filter;
    AudioAmplifier amp;
    MonoToStereo m2s;
    AudioMixer<4> mix;
    std::vector<int16_t> out; // mixer output per block, 128 zeros when no block was sent
    int blocksOut = 0;

    static audio_block_t* take(int idx)
    {
        audio_block_t* b = stubOut[idx];
        stubOut[idx] = nullptr;
        return b;
    }
    // one block; in = nullptr: the voice sent nothing; zeros: the voice sent a block of zeros
    void step(const int16_t* in, bool zeros)
    {
        audio_block_t* b = nullptr;
        if (in)
        {
            b = (audio_block_t*)calloc(1, sizeof(audio_block_t));
            b->ref_count = 1;
            memcpy(b->data, in, 256);
        }
        else if (zeros)
        {
            b = (audio_block_t*)calloc(1, sizeof(audio_block_t));
            b->ref_count = 1;
        }
        stubIn[0] = b;
        filter.update();
        audio_block_t* f = take(0);
        stubIn[0] = f;
        amp.update();
        audio_block_t* a = take(0);
        stubIn[0] = a;
        m2s.update();
        audio_block_t* l = take(0);
        audio_block_t* r = take(1);
        stubIn[0] = l;
        stubIn[1] = r;
        mix.update();
        audio_block_t* o = take(0);
        size_t at = out.size();
        out.resize(at + 128, 0);
        if (o)
        {
            memcpy(&out[at], o->data, 256);
            blocksOut++;
            free(o);
        }
    }
};
} // namespace

static void testIdleVoices()
{
    AudioFilterStateVariable::initCutoffLookup();
    for (int useFilter = 0; useFilter < 2; useFilter++)
        for (int variant = 0; variant < 2; variant++)
        {
            // zeroed storage like the statics on the device (the filter leaves some members uninitialised)
            VoiceChain& a = *new (calloc(1, sizeof(VoiceChain))) VoiceChain();
            VoiceChain& b = *new (calloc(1, sizeof(VoiceChain))) VoiceChain();
            VoiceChain* chains[2] = { &a, &b };
            for (VoiceChain* c : chains)
            {
                if (useFilter)
                {
                    c->filter.connect();
                    c->filter.setCutoff(variant ? 0.15f : 0.5f);
                    c->filter.resonance(variant ? 4.0f : 1.0f);
                    c->filter.setType(0);
                }
                else
                {
                    c->filter.disconnect();
                }
            }
            g_rng = 777;
            int maxDiff = 0;
            for (int blk = 0; blk < 1500; blk++)
            {
                const int cycle = blk % 300;
                const bool playing = cycle < 40; // 40 blocks of sound, 260 of idle
                int16_t in[128];
                for (int s = 0; s < 128; s++)
                    in[s] = playing ? (int16_t)((int)(rnd() % 20001) - 10000) : 0;
                // controls change while idle and while playing: volume, track gain, pan, filter settings
                if (blk % 300 == 100) { for (VoiceChain* c : chains) { c->amp.gain(0.3f); c->mix.gain(0, 0.5f); } }
                if (blk % 300 == 7) { for (VoiceChain* c : chains) { c->amp.gain(0.9f); c->mix.gain(0, 1.0f); c->mix.gain(1, 0.7f); } }
                if (useFilter && blk % 300 == 150) { for (VoiceChain* c : chains) { c->filter.setCutoff(0.6f); } }
                if (useFilter && blk == 905) { for (VoiceChain* c : chains) { c->filter.disconnect(); } }
                if (useFilter && blk == 1205) { for (VoiceChain* c : chains) { c->filter.connect(); } }
                a.step(playing ? in : nullptr, true);  // old: zeros while idle
                b.step(playing ? in : nullptr, false); // new: nothing while idle
                for (int s = 0; s < 128; s++)
                {
                    int d = abs((int)a.out[blk * 128 + s] - (int)b.out[blk * 128 + s]);
                    if (d > maxDiff) maxDiff = d;
                }
            }
            printf("  idle voice, filter %s%s: %d of %d mixer blocks sent (old: %d), max difference %d LSB\n", useFilter ? "on" : "off",
                   useFilter ? (variant ? " (low, resonant)" : " (mid)") : "", b.blocksOut, 1500, a.blocksOut, maxDiff);
            CHECK(b.blocksOut < a.blocksOut);
            CHECK(useFilter ? maxDiff <= 2 : maxDiff == 0); // the cut filter tail: +-1..2 LSB of rounding noise (-84 dBFS)
            CHECK(a.out.size() == b.out.size());
        }
}
