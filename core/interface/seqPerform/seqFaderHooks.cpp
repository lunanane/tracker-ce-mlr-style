// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// R3 (fader page on Shift + Performance) fills this file. Hook contracts: core/source/seqHooks.h
#include "seqHooks.h"

// Step 0: the stock page opens the fader page on Shift + Performance (v0.1 flow, state handed over); other modules do nothing yet.
bool seqFaderShiftFromStock() { return true; }
bool seqFaderShiftFromElsewhere(bool) { return false; }
void seqPanicFx() {}
