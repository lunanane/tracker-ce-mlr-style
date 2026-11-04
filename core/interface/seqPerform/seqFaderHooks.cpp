// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// R3 (fader page on Shift + Performance) fills this file. Hook contracts: core/source/seqHooks.h
#include "seqHooks.h"

#include "seqPerform/seqPerform.h"

// Stock page, Shift held on the press: the fader page opens with the perform FX state handed over.
bool seqFaderShiftFromStock()
{
    seqPerform.fromStock = true; // start() hands the stock state over (not guessed from dontClearPerformanceMode)
    return true;
}

// Any other module: Shift + Performance opens the fader page. On the fader page itself the Performance button is bound by the
// page (a press goes back to the stock page), so the stock switching is left alone there.
bool seqFaderShiftFromElsewhere(bool onFaderPage)
{
    if (onFaderPage)
        return false;
    seqPerform.prepareFromElsewhere();
    return true;
}

// The stock Performance page started fresh (not handed over from the fader page): FX we kept running end first.
void seqFaderStockEntered()
{
    seqPerform.stockEntered();
}

// Transport Stop: every perform FX we hold ends, the faders go back to "not dialled in".
void seqPanicFx()
{
    seqPerform.panic();
}

// A project was loaded (sidecar load dispatcher): perform FX of the old project are gone, nothing is dialled in.
void seqFaderProjectLoaded()
{
    seqPerform.projectLoaded();
}

// Kept FX followed on to a page that does not keep them (interface.cpp switchModuleByButton): the stock rule applies.
void seqFaderEndKept()
{
    seqPerform.endKeptStockRule();
}

bool seqFaderKept()
{
    return seqPerform.keptFx();
}
