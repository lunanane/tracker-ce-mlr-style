// Plan 009 P#4: the delay (real effect_shortDelay.cpp, host stub of AudioStream) against the frozen pre-change copy (ref/delay).
// Included by test_cpu.cpp.
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
