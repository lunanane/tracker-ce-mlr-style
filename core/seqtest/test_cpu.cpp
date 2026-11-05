// Plan 009 P (CPU savings): host tests that the changed DSP gives the same samples as the frozen pre-change code (bit-exact items) or stays
// within a stated bound (inaudible items). Each section names the finding number of plans/009-plinky-synth-and-cpu.md "P".
#include <math.h>
#include <stdint.h>
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

static uint32_t g_rng = 12345;
static uint32_t rnd() { g_rng = g_rng * 1664525u + 1013904223u; return g_rng >> 8; }

// ---- #1 stock reverb: buffer 2 cut from 16 384 to 11 040 words, bit-identical output ----
int ref_reverb_run(int16_t*, int16_t*, int, float, float, float, float, bool);
int new_reverb_run(int16_t*, int16_t*, int, float, float, float, float, bool);

static void makeInput(std::vector<int16_t>& l, std::vector<int16_t>& r, int blocks)
{
    l.assign(blocks * 128, 0);
    r.assign(blocks * 128, 0);
    // noise bursts of different levels with silent gaps, and one full-scale click train
    for (int b = 0; b < blocks; b++)
    {
        int phase = (b / 40) % 4;
        int amp = phase == 0 ? 20000 : phase == 1 ? 0 : phase == 2 ? 300 : 32767;
        for (int s = 0; s < 128; s++)
        {
            l[b * 128 + s] = amp ? (int16_t)((int)(rnd() % (2 * amp + 1)) - amp) : 0;
            r[b * 128 + s] = amp ? (int16_t)((int)(rnd() % (2 * amp + 1)) - amp) : 0;
        }
    }
}

static void testReverbSram()
{
    const float tp[4][4] = { { 0.7f, 0.2f, 0.8f, 0.0f }, { 0.95f, 0.05f, 1.0f, 0.5f }, { 0.3f, 0.9f, 0.4f, 1.0f }, { 0.85f, 0.5f, 0.0f, 0.25f } };
    for (int k = 0; k < 4; k++)
    {
        const int blocks = 1500; // 192 000 samples: ~12 wraps of buffer 1, ~17 of the cut buffer 2
        std::vector<int16_t> il, ir;
        makeInput(il, ir, blocks);
        std::vector<int16_t> al = il, ar = ir, bl = il, br = ir;
        ref_reverb_run(al.data(), ar.data(), blocks, tp[k][0], tp[k][1], tp[k][2], tp[k][3], false);
        new_reverb_run(bl.data(), br.data(), blocks, tp[k][0], tp[k][1], tp[k][2], tp[k][3], false);
        CHECK(memcmp(al.data(), bl.data(), al.size() * 2) == 0);
        CHECK(memcmp(ar.data(), br.data(), ar.size() * 2) == 0);
        // the output is not trivially equal because it is silent
        long nz = 0;
        for (size_t i = 0; i < al.size(); i++)
            nz += al[i] != 0;
        CHECK(nz > (long)al.size() / 2);
    }
}

// ---- #7 idle skip: identical while there is signal, the skipped tail is below -76 dBFS ----
static void testReverbIdleSkip()
{
    const float tp[3][4] = { { 0.7f, 0.2f, 0.8f, 0.0f }, { 0.95f, 0.05f, 1.0f, 0.5f }, { 0.3f, 0.9f, 0.4f, 1.0f } };
    for (int k = 0; k < 3; k++)
    {
        const int blocks = 4000; // 11.6 s: three bursts, each followed by seconds of silence
        std::vector<int16_t> il(blocks * 128, 0), ir(blocks * 128, 0);
        const int burstAt[3] = { 0, 1200, 2600 };
        for (int bi = 0; bi < 3; bi++)
            for (int b = burstAt[bi]; b < burstAt[bi] + (bi == 1 ? 1 : 30); b++) // bi 1 is a single click block
                for (int s = 0; s < 128; s++)
                {
                    il[b * 128 + s] = (int16_t)((int)(rnd() % 40001) - 20000);
                    ir[b * 128 + s] = (int16_t)((int)(rnd() % 40001) - 20000);
                }
        std::vector<int16_t> al = il, ar = ir, bl = il, br = ir;
        ref_reverb_run(al.data(), ar.data(), blocks, tp[k][0], tp[k][1], tp[k][2], tp[k][3], false);
        int skipped = new_reverb_run(bl.data(), br.data(), blocks, tp[k][0], tp[k][1], tp[k][2], tp[k][3], true);
        // (a) up to the second burst: the only difference is the skipped tail itself, below the threshold
        int maxDiff = 0, firstDiffBlock = -1;
        for (size_t i = 0; i < (size_t)burstAt[1] * 128; i++)
        {
            int d = abs((int)al[i] - (int)bl[i]), e = abs((int)ar[i] - (int)br[i]);
            if (d > maxDiff) maxDiff = d;
            if (e > maxDiff) maxDiff = e;
            if ((d || e) && firstDiffBlock < 0) firstDiffBlock = (int)(i / 128);
        }
        // (b) after a skip the free-running modulation LFO has a different phase (it does not advance while skipped), so the samples of the
        // next burst differ; its level must not (RMS over the 300 blocks that follow each burst, left channel)
        double rmsRatioMin = 1e9, rmsRatioMax = 0;
        for (int bi = 1; bi < 3; bi++)
        {
            double sa = 0, sb = 0;
            for (int i = burstAt[bi] * 128; i < (burstAt[bi] + 300) * 128; i++)
            {
                sa += (double)al[i] * al[i];
                sb += (double)bl[i] * bl[i];
            }
            double ratio = sqrt(sb / sa);
            if (ratio < rmsRatioMin) rmsRatioMin = ratio;
            if (ratio > rmsRatioMax) rmsRatioMax = ratio;
        }
        printf("  idle skip type %d: %d of %d blocks skipped, tail difference max %d LSB (first in block %d), later burst RMS ratio %.3f..%.3f\n", k, skipped, blocks, maxDiff, firstDiffBlock, rmsRatioMin, rmsRatioMax);
        CHECK(skipped > 0);
        CHECK(maxDiff <= 16);                                    // 16 LSB = -66 dBFS peak; the skipped tail is below the 5 LSB average
        CHECK(firstDiffBlock < 0 || firstDiffBlock >= 256 + 30); // never before the input has been silent for the hold-off
        CHECK(rmsRatioMin > 0.8 && rmsRatioMax < 1.25);
    }
    // input never silent: nothing is skipped, output bit-identical to the old code
    std::vector<int16_t> il, ir;
    makeInput(il, ir, 400);
    for (size_t i = 0; i < il.size(); i++) { il[i] |= 1; ir[i] |= 1; } // never exactly zero
    std::vector<int16_t> al = il, ar = ir, bl = il, br = ir;
    ref_reverb_run(al.data(), ar.data(), 400, 0.7f, 0.2f, 0.8f, 0.0f, false);
    CHECK(new_reverb_run(bl.data(), br.data(), 400, 0.7f, 0.2f, 0.8f, 0.0f, true) == 0);
    CHECK(al == bl && ar == br);
}

#include "cpu_delay_tests.inc"

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    testReverbSram();
    testReverbIdleSkip();
    testDelay();
    testLimiter();
    testIdleVoices();
    testOutputStage();
    printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
