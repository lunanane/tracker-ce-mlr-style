// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// R2 hooks of the clip launcher (plan 005). Contracts: core/source/seqHooks.h
#include "seqHooks.h"
#include "seqLauncher.h"

// Transport Stop = panic: queued launches and stops are dropped, launched clips end, LIVE ends (the song is stock again).
void seqPanicClips() { seqSessionPanic(); }
