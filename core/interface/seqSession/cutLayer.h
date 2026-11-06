// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// CUT (MLR) pad layer of the Session view (plan 002). Main loop only. No Polyend includes.
//
// Pad grid: 4 rows x 12 columns (0-based; index = row * 12 + col), same as every other pad layer.
//   row r      = track groupFirst + r (group of 4 tracks, see cutSetGroup)
//   col 0..7   = 8 positions of the track (Playhead::columnPosition with 8 columns); one pad = jump (at the next step),
//                two pads in one row = a loop between them that latches (a single pad in the row ends it)
//   col 8      = STOP / START the track (START re-aligns it to the global position)
//   col 9      = REVERSE   col 10 = RATE (1/2 -> 1 -> 2 -> 1/2 ...)   col 11 = RESYNC (global position, clears loop/recording)
// Rec (toggle) = playhead recording: press Rec, a row starts its take with the first pad pressed on it, press Rec again to close
// all takes; the tracks then replay what was played during the take. Recorded loops and loops of tracks outside the shown group keep running.
// LEDs: playhead position bright, loop range / other positions dim, RESYNC pad: blinking = recording, bright = a
// recorded loop is replaying, medium = the track is off the global grid.
// SYNC ALL (screen button 6, or Shift + any RESYNC pad): every track of all 16 snaps back onto the global playhead (loops,
// jams, recorded replays, reverse, rate and STOP are dropped) at its next step boundary, in one audio-context pass.
#pragma once
#include <stdint.h>

void cutPad(uint8_t col, uint8_t row, bool down, bool shift = false); // shift: Shift held on the press (RESYNC pad = SYNC ALL)
void cutLeds(uint8_t* out48);        // 48 levels 0..15, index = row * 12 + col
void cutInit();                      // constructs the Cut through seqCutInit() (the first use of the layer does it too)
bool cutSlotsUsed();                 // the MLR layer was used at least once (the cue cache works only then)
bool cutTrackRecArmed(uint8_t track); // a take of the track is armed (Rec key or the slot's REC pad)
uint8_t cutSlotTrack(uint8_t slot);  // the track slot 0 (A) / 1 (B) holds; defaults 0 and 1
void cutAssignSlot(uint8_t slot, uint8_t track); // a track belongs to one slot: taking the other slot's track swaps them
void cutPickToggle(uint8_t slot);    // "Assign slot n" screen button: the slot's two pad rows become the track picker / back
int8_t cutPickSlot();                // slot being picked for, -1 = none
void cutLegendToggle();              // "Help" screen button: the pad legend on the lane screen
bool cutLegendShown();
void cutRec(bool down);              // set the Rec (take armed) state directly: true = armed, false = close all takes
void cutRecToggle();                 // the Rec key (a press): armed <-> closed, hold events and the release are not passed here
uint8_t cutQuantMode();              // jump quantise: seqcut::Quant (off / step / beat / bar), default beat
void cutQuantSet(uint8_t mode);      // the audio side learns it with one command, kept and retried until accepted
void cutQuantCycle();                // screen button 5: off -> step -> beat -> bar -> off
void cutReleaseAll();                // leaving the layer: lets go of every held pad and ends open takes; running jams, recorded loops,
                                     // reverse / rate / STOP of the tracks are NOT touched (plan 005 M5)
void cutSyncAll();                   // SYNC ALL: forgets held pads / takes, queues the command (kept and retried until accepted)
void cutPanic();                     // transport Stop: release holds and takes, then SYNC ALL (the next Play starts on the grid)
void cutUpdate();                    // once per main loop pass, any view: retries lost releases and a SYNC ALL that did not fit
bool cutRecHeld();                   // a take is armed (Rec was pressed an odd number of times in the CUT layer)
bool cutSyncPending();               // a SYNC ALL is waiting for room in the command queue
