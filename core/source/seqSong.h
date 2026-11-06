// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Firmware glue of the live-set Song page (plan 006): the song loops its current slot unless the slot's scene is flagged
// "play next"; slots can be queued from the Song page. Logic: seqSongLogic.h.
#pragma once
#include "seqSongLogic.h"

// ---- sequencer interrupt (mtSequencer.cpp): cheap flag lookups, no SD access, nothing from the main loop is called ----
// At the last step of the pattern, where the stock song loads the next pattern: true = the slot plays again (nothing is loaded).
bool seqSongStayAtLastStep(bool exporting);
// At the pattern end of song mode: true = no switch, the pattern wraps like a pattern loop. Takes the answer of the last step.
bool seqSongStayAtEnd(bool exporting);
// After every switch of the pattern buffers (switchRamPatternsNow): moves the song position to the queued slot when that slot's pattern
// is the one that plays now. Returns the new playlist position, or -1 to leave the stock search result.
int16_t seqSongSlotAfterSwitch(uint8_t actualPattern);
void seqSongPatternEndTaken(); // the pattern end did something else than the song advance (a queued pattern switch): forget the last-step answer

// ---- main loop ----
// Press on a slot of the Song page (0-based): queue it (switches at the pattern end like a scene pad), or cancel a pending queue
// by pressing the playing slot. Uses the stock pattern switch: the pattern file is read by the file manager's own async load.
// false: refused (busy loading, last step, switch already loaded) or nothing to do.
bool seqSongQueueSlot(uint8_t slot);
// plan 006: the mod drops the song mode when a scene outside the song plays, and puts it back when a pattern of the song is switched to.
bool seqSongModeRestore();          // audio: true once when the mod dropped the song mode earlier (cleared)
void seqSongModeDropped();          // audio: the mod dropped the song mode
void seqSongUserMode();             // a deliberate loop / song choice of the user (or Stop): forget a dropped mode
uint8_t seqSongQueuedSlot();        // 1-based slot shown as queued, 0 = none
bool seqSongSlotStays();         // song mode and the playing slot loops (no play next): the song is like a pattern loop
bool seqSongPlayNext(uint8_t slot); // the scene of that slot is flagged "play next"
void seqSongSetPlayNext(uint8_t slot, bool on); // writes the per-pattern flag (same one the launcher shows)
uint8_t seqSongSlotMarks(uint16_t slot);   // for the slot list of the Song page: bit 0 = play next, bit 1 = queued, bits 4-7 = scene colour
uint8_t seqSongColor(uint8_t slot);        // plan 017: scene colour (seqpal index, 0 = none) of the slot's pattern
uint8_t seqSongStepColor(uint8_t slot, int dir); // steps the colour of the slot's pattern through the palette (wraps); returns the new index
void seqSongPanic();                // Stop: the queue and the last-step answer are dropped
