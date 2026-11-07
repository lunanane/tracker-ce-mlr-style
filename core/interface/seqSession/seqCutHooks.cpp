// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// R4 (MLR screen, SYNC ALL): the hooks of the CUT layer. Hook contracts: core/source/seqHooks.h
#include "seqHooks.h"

#include "cutLayer.h"
#include "elapsedMillis.h"
#include "seqMlr.h"

// Transport Stop = panic (plan 005 M6): held MLR pads and open takes end, SYNC ALL drops every jam, loop, recorded loop, reverse,
// rate and STOP. The transport is stopped, so no step boundary follows: the next Play starts every track on the grid.
void seqPanicCut()
{
    cutPanic();
}

// Every main loop pass (seqTracksUpdate), whatever view is on screen: a SYNC ALL or a release that did not fit into the command
// queue is retried here until it is accepted.
void seqCutUpdate()
{
    static elapsedMillis clock; // never reset: the time base of the deliberate two-pad loop (plan 026)
    cutUpdate((uint32_t)clock);
    seqmlr::update(); // cue cache wants + section waveform (throttled)
}

void seqCutLoadSettings(uint8_t quant, uint8_t slotA, uint8_t slotB)
{
    cutMlrLoad(quant, slotA, slotB);
}
