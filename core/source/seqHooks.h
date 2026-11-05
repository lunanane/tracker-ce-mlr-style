// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Hook points of the v0.2 rework (plan 005, step 0). The shared files (interface inputs, interface.cpp, fPatternEdit, fSongEdit,
// fPerformance, ...) only call these functions; each one is DEFINED in a file that belongs to exactly one later step, so R1-R4
// fill their own file and do not edit the shared callers again. Every hook is a no-op / "not handled" until its step fills it.
//
//   R1 Play overlay   interface/seqPlay/seqPlayHooks.cpp        seqPlayRoute*, seqPanicPlay
//   R2 clip launcher  interface/seqSession/seqClipsHooks.cpp    seqPanicClips
//   R3 fader page     interface/seqPerform/seqFaderHooks.cpp    seqFaderShiftFrom*, seqPanicFx
//   R4 MLR screen     interface/seqSession/seqCutHooks.cpp      seqPanicCut
#pragma once
#include <stdint.h>

// ---- R1: input router of the Play overlay (called first by Interface::buttonChange / padPressed / padReleased / padHold /
// potChange; true = consumed, the stock module does not see the event). Main loop. ----
bool seqPlayRouteButton(uint8_t button, uint8_t state); // state: buttonPress / buttonHold / buttonRelease
bool seqPlayRoutePad(uint8_t pad, uint8_t state, uint8_t velo); // state 1 press, 0 release, 2 hold
bool seqPlayRoutePot(uint8_t pot, int16_t value);

// ---- R3: fader page = Performance key, stock Performance page = Shift + Performance (owner change 2026-11-04). ----
// On the stock Performance page (fPerformance.cpp functPerformanceButton, Shift held on the press): true = open the fader page
// with the perform FX state handed over (v0.1 flow); false = do the stock thing (flip the tracks page + mute view).
bool seqFaderShiftFromStock();
// From any other module, plain Performance key (Interface::switchModuleByButton, before the module is deactivated): true = the caller then activates
// seqPerform (the hook has made sure its start() finds a clean state, e.g. read the perform data). false = stock switching.
// onFaderPage: the fader page is the module that is on screen now.
bool seqFaderShiftFromElsewhere(bool onFaderPage);
// The stock Performance page starts fresh (fPerformance.cpp cPerformanceMode::start, not handed over): perform FX the fader page
// kept running end here, before the stock page resets its track states.
void seqFaderStockEntered();
// A project was loaded (seqSidecarsLoadWorkspace): perform FX of the old project end, the fader state is cleared, no pattern reload.
void seqFaderProjectLoaded();
// Kept fader FX (left running on any module switch since v0.3): seqFaderKept() asks. Stock modules that would turn performance mode off
// when the user leaves them (Master, instrument parameters) leave it alone while this is true. (There is no "end the kept FX"
// hook any more: they end by DISENGAGE, UNLATCH, the Stop panic or a project load only.)
bool seqFaderKept();

// ---- panic (plan 005 M6): transport Stop is the panic of the whole mod. seqPanic() runs once on the main loop after every
// Stop (seqTracksUpdate) and calls the four hooks below; each ends what its module owns. ----
void seqPanic();
void seqPanicPlay();   // R1: end a recording, release held pads / takeovers / latched arps
void seqPanicClips();  // R2: drop queued launches, stop-all, clips back to the pattern, LIVE -> song
void seqPanicFx();     // R3: all perform FX off (faders back to "not dialled in", latches released)
void seqPanicCut();    // R4: SYNC ALL (cutPanic), release MLR holds and recordings

// v0.3.1 M1: before any export starts (Project > Export Song / Pattern / Stems, Render selection): kept perform FX or performance mode
// end with the panic semantics (incl. the gated pattern reload), so the export renders the song as written and its own sequencer.stop()
// has nothing left to panic. true: something was ended (the caller shows "Perform FX ended for export").
bool seqEndPerformFxForExport();

// ---- R4: once per main loop pass (seqTracksUpdate), any view: retries a SYNC ALL / releases that did not fit into the queue. ----
void seqCutUpdate();
