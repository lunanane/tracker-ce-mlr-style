// Plan 027: ARM bench of the ring reverb (Plinky type), run by emu.py. sc 0 = ring in SDRAM, plain loop (v0.4.4), 1 = ring in SRAM (compute only), 2 = ring in SDRAM with SRAM tiles (plan 027).
#include <new>
#include "Polyverb/ring_reverb.h"
using namespace ringverb;
__attribute__((section(".sdram"))) alignas(8) int16_t g_ringS[kRingSize + 2 * kPreSize];
alignas(8) int16_t g_scratch[6000];
alignas(8) int16_t g_ringR[kRingSize + 2 * kPreSize];
alignas(8) static unsigned char g_mem[sizeof(RingReverb)];
static RingReverb* R;
alignas(8) int16_t g_l[128], g_r[128];
extern "C" int g_sc;
int g_sc;
extern "C" void setup(int sc)
{
    int16_t* m = sc != 1 ? g_ringS : g_ringR;
    R = new (g_mem) RingReverb();
    R->init(PlainRing{ m }, m + kRingSize);
    R->setParams(200, 0.5f, 16384, 100);
    if (sc == 2) R->setScratch(g_scratch, 6000);
    else R->setTiling(false);
}
static uint32_t rng = 1;
extern "C" void run(int n)
{
    for (int b = 0; b < n; b++)
    {
        for (int i = 0; i < 128; i++)
        {
            rng = rng * 1664525u + 1013904223u;
            g_l[i] = (int16_t)((rng >> 16) & 0x1fff) - 4096;
            g_r[i] = (int16_t)((rng >> 8) & 0x1fff) - 4096;
        }
        R->process(g_l, g_r, 128);
    }
}
extern "C" unsigned hash()
{
    unsigned h = 2166136261u;
    const unsigned char* p = (const unsigned char*)g_l;
    for (unsigned i = 0; i < 256; i++) h = (h ^ p[i]) * 16777619u;
    p = (const unsigned char*)g_r;
    for (unsigned i = 0; i < 256; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}
extern "C" int voices() { return 0; }
