// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// R1 (Play overlay) fills this file. Hook contracts: core/source/seqHooks.h
#include "seqHooks.h"

bool seqPlayRouteButton(uint8_t, uint8_t) { return false; }
bool seqPlayRoutePad(uint8_t, uint8_t, uint8_t) { return false; }
bool seqPlayRoutePot(uint8_t, int16_t) { return false; }
void seqPanicPlay() {}
