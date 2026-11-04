// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// R1 (Play overlay): the input router hooks and the Stop panic. Hook contracts: core/source/seqHooks.h. The logic is in
// seqPlayOverlay.cpp; these are thin so the shared callers (interfaceInputs.cpp, seqHooks.cpp) never change again.
#include "seqHooks.h"
#include "seqPlay/seqPlay.h"

bool seqPlayRouteButton(uint8_t button, uint8_t state)
{
    return seqPlayOverlay.button(button, state);
}

bool seqPlayRoutePad(uint8_t pad, uint8_t state, uint8_t velo)
{
    return seqPlayOverlay.pad(pad, state, velo);
}

bool seqPlayRoutePot(uint8_t pot, int16_t value)
{
    return seqPlayOverlay.pot(pot, value);
}

void seqPanicPlay()
{
    seqPlayOverlay.panic();
}
