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
void ref_reverb_run(int16_t*, int16_t*, int, float, float, float, float, float*);
void new_reverb_run(int16_t*, int16_t*, int, float, float, float, float, float*);

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
        ref_reverb_run(al.data(), ar.data(), blocks, tp[k][0], tp[k][1], tp[k][2], tp[k][3], nullptr);
        new_reverb_run(bl.data(), br.data(), blocks, tp[k][0], tp[k][1], tp[k][2], tp[k][3], nullptr);
        CHECK(memcmp(al.data(), bl.data(), al.size() * 2) == 0);
        CHECK(memcmp(ar.data(), br.data(), ar.size() * 2) == 0);
        // the output is not trivially equal because it is silent
        long nz = 0;
        for (size_t i = 0; i < al.size(); i++)
            nz += al[i] != 0;
        CHECK(nz > (long)al.size() / 2);
    }
}

int main()
{
    testReverbSram();
    printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
