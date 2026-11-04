// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Firmware glue of the live-set Song page (plan 006): the song loops its current slot unless the slot's scene is flagged
// "play next"; slots can be queued from the Song page. Logic: seqSongLogic.h.
#pragma once
#include "seqSongLogic.h"

// ---- sequencer interrupt (mtSequencer.cpp): cheap flag lookups, no SD access, nothing from the main loop is called ----
// At the last step of the pattern, where the stock song loads the next pattern: true = the slot plays again (nothing is loaded).
bool seqSongStayAtLastStep(bool breakFx, bool exporting);
// At the pattern end of song mode: true = no switch, the pattern wraps like a pattern loop. Takes the answer of the last step.
bool seqSongStayAtEnd(bool breakFx, bool exporting);
// After every switch of the pattern buffers (switchRamPatternsNow): moves the song position to the queued slot when that slot's pattern
// is the one that plays now. Returns the new playlist position, or -1 to leave the stock search result.
int16_t seqSongSlotAfterSwitch(uint8_t actualPattern);
void seqSongPatternEndTaken(); // the pattern end did something else than the song advance (a queued pattern switch): forget the last-step answer

// ---- main loop ----
// Press on a slot of the Song page (0-based): queue it (switches at the pattern end like a scene pad), or cancel a pending queue
// by pressing the playing slot. Uses the stock pattern switch: the pattern file is read by the file manager's own async load.
// false: refused (busy loading) or nothing to do.
bool seqSongQueueSlot(uint8_t slot);
uint8_t seqSongQueuedSlot();        // 1-based slot shown as queued, 0 = none
bool seqSongSlotStays();         // song mode and the playing slot loops (no play next): the song is like a pattern loop
bool seqSongPlayNext(uint8_t slot); // the scene of that slot is flagged "play next"
void seqSongSetPlayNext(uint8_t slot, bool on); // writes the per-pattern flag (same one the launcher shows)
uint8_t seqSongSlotMarks(uint16_t slot);   // for the slot list of the Song page: bit 0 = play next, bit 1 = queued
void seqSongPanic();                // Stop: the queue and the last-step answer are dropped
