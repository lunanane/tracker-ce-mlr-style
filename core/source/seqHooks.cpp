// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
#include "seqHooks.h"
#include "mtSequencer.h"
#include "seqPerformLogic.h"
#ifdef FEATURE_PLINKY
#include "seqPlinky.h"
#endif

void seqPanic()
{
    seqPanicPlay();
    seqPanicClips();
    seqPanicCut();
    seqPanicFx();
#ifdef FEATURE_PLINKY
    seqPanicPlinky();
#endif
}

// v0.3.1 M1: see seqHooks.h
bool seqEndPerformFxForExport()
{
    if (!seqperf::exportEndsFx(seqFaderKept(), sequencer.isPerformanceMode()))
        return false;
    seqPanicClips(); // clip-launcher takeovers (they own the performance flag too)
    seqPanicFx();
    return true;
}
