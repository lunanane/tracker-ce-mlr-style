// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// CUT (MLR) pad layer of the Session view (plan 002). Main loop only. No Polyend includes.
//
// Pad grid: 4 rows x 12 columns (0-based; index = row * 12 + col), same as every other pad layer.
//   row r      = track groupFirst + r (group of 4 tracks, see cutSetGroup)
//   col 0..7   = 8 positions of the track (Playhead::columnPosition with 8 columns); one pad = jump (at the next step),
//                two pads held in one row = loop between them (release = back to the whole track)
//   col 8      = STOP / START the track (START re-aligns it to the global position)
//   col 9      = REVERSE   col 10 = RATE (1/2 -> 1 -> 2 -> 1/2 ...)   col 11 = RESYNC (global position, clears loop/recording)
// Rec held while jamming = playhead recording: a row starts its take with the first pad pressed on it, releasing Rec
// closes all takes and the tracks replay them. Recorded loops and loops of tracks outside the shown group keep running.
// LEDs: playhead position bright, loop range / other positions dim, RESYNC pad: blinking = recording, bright = a
// recorded loop is replaying, medium = the track is off the global grid.
#pragma once
#include <stdint.h>

void cutPad(uint8_t col, uint8_t row, bool down);
void cutLeds(uint8_t* out48);        // 48 levels 0..15, index = row * 12 + col
void cutSetGroup(uint8_t firstTrack); // 0, 4, 8 or 12 (anything else rounds down to a multiple of 4)
void cutRec(bool down);
void cutReleaseAll();                // leaving the layer: lets go of every held pad (loops stop, recording ends)
