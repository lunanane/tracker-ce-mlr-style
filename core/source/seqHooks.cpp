// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
#include "seqHooks.h"
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
