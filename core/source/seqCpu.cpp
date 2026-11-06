// Sequencer tracks mod - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md). CPU monitor (plan 022 P1), see seqCpu.h.
#include "seqCpu.h"
#ifdef FEATURE_PLINKY
#include "seqPlinky.h"
#endif
#include <string.h>

namespace seqcpu {

volatile bool g_on = false;
uint32_t g_nestedCycles = 0;
Snap g_live;
volatile Rd g_rdKind = R_OTHER;

static Snap s_pub;
static volatile uint32_t s_pubSeq = 0; // odd while s_pub is being written
static bool s_have = false;

void init()
{
    *(volatile uint32_t*)0xE0001000 |= 1u; // DWT_CTRL.CYCCNTENA (the reverb / warp readouts enable it as well)
}

void setActive(bool on)
{
    if (on == g_on)
        return;
    if (on)
    {
        snapClear(g_live, 0);
        s_have = false;
    }
    g_on = on;
}

void blockEnd(uint32_t isrCycles)
{
    if (snapBlock(g_live, isrCycles))
    {
        if (g_on)
        {
            s_pubSeq = s_pubSeq + 1;
            s_pub = g_live;
            s_pubSeq = s_pubSeq + 1;
        }
        snapClear(g_live, g_live.isrMax);
    }
}

bool snapshot(Snap& out)
{
    if (!g_on)
        return false;
    for (int tries = 0; tries < 4; tries++)
    {
        const uint32_t a = s_pubSeq;
        if (a & 1u)
            continue;
        out = s_pub;
        if (a == s_pubSeq)
        {
            s_have = s_have || out.blocks != 0;
            return out.blocks != 0;
        }
    }
    return false;
}

int buildText(char* out, int cap)
{
    Snap s;
    if (!snapshot(s))
    {
        strncpy(out, "CPU monitor\nmeasuring ... (a window is half a second)\n\nClose: button 8", (size_t)cap);
        out[cap - 1] = 0;
        return (int)strlen(out);
    }
    Extra e = { -1, 0, 0, 0 };
#ifdef FEATURE_PLINKY
    e.plinkyCap = seqPlinkyVoiceCap();
    e.plinkyVoices = seqPlinkyVoicesActive();
    e.plinkyCpu10 = (int)(seqPlinkyCpu() * 10.0f);
    e.plinkyPeak10 = (int)(seqPlinkyCpuPeak() * 10.0f);
#endif
    return format(s, e, out, cap);
}

} // namespace seqcpu
