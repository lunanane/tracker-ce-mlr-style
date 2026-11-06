// Sequencer tracks mod (v0.4.3) - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md). ARM bench of the PolySynth render, run by emu.py.
#include <new>
#include "PlinkyVoice/plinky_voice.h"
using namespace plinkyvoice;
alignas(8) static unsigned char g_mem[sizeof(Engine)];
static Engine* E;
alignas(8) int16_t g_out[6][128];
__attribute__((section(".sdram"))) alignas(8) int16_t g_chorus[3][2 * kChLen];
extern "C" int g_sc;
int g_sc;
extern "C" void setup(int sc)
{
    E = new (g_mem) Engine();
    int ninst = (sc >= 1 && sc != 7) ? 3 : 1;
    for (int k = 0; k < 3; k++)
        E->setChorusMemory(k, g_chorus[k]);
    for (int k = 0; k < ninst; k++)
    {
        if (sc >= 1) { E->setParam(P_REVERB, 30, k); E->setParam(P_DELAY, 20, k); }
        if (sc == 8) { E->setParam(P_SUB1, 50, k); E->setParam(P_FTYPE, 4, k); }
        if (sc == 2) E->setParam(P_CH_MIX, 50, k);
        if (sc == 3) { E->setParam(P_SUB1, 50, k); E->setParam(P_FTYPE, 1, k); }
        if (sc == 4) { E->setParam(P_FENV_AMT, 80, k); }
        if (sc == 5) { E->setParam(P_SHAPE, 150, k); }
        if (sc == 6) { E->setParam(P_WIDTH, 0, k); }
    }
    // notes: scenario 0 = 4 voices on instance 0; others = 8 voices over 3 instances (3,3,2)
    if (sc == 0 || sc == 7)
        for (int n = 0; n < 4; n++) E->noteOn(48 + n * 4, 100, 0);
    else
        for (int n = 0; n < 8; n++) E->noteOn(48 + n * 3, 100, n % 3);
}
extern "C" void run(int n)
{
    Out o = { g_out[0], g_out[1], g_out[2], g_out[3], g_out[4], g_out[5] };
    if (g_sc == 0) { o.revL = o.revR = o.dlyL = o.dlyR = nullptr; }
    for (int i = 0; i < n; i++)
    {
        E->setBlockLoad(300);
        E->render(o);
    }
}
extern "C" unsigned hash()
{
    unsigned h = 2166136261u;
    const unsigned char* p = (const unsigned char*)g_out;
    for (unsigned i = 0; i < sizeof(g_out); i++) h = (h ^ p[i]) * 16777619u;
    return h;
}
extern "C" int voices() { return E->activeVoices(); }
