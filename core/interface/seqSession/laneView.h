// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// CUT (MLR) lane screen of the Session view (plan 005 R4): two display controls and the glue that fills them.
//   cSeqLanes     static part (header, lanes, ticks, notes, loop brackets, flags, right block): redrawn on events only
//   cSeqPlayheads moving part (filled segment, grid ghost, playhead line, bar.beat): redrawn when a line moved, <= 25 fps
// The drawing itself is laneDraw.h (shared with the host tests); geometry and models are seqCutLogic.h.
// Free functions (called by CutSessionLayer, main loop). The control classes live in laneView.cpp; the host tests provide stand-ins
// for these functions (no display code on the host).
#pragma once
#include <stdint.h>

void laneViewCreate();                                   // module start: both controls, hidden (created once, only shown / hidden after)
void laneViewForget();                                   // module stop: the display frees the controls itself, drop the handles
void laneViewShow(bool show);                            // the CUT layer becomes visible / hidden
bool laneViewRefresh(bool force, uint8_t firstTrack);    // main loop, every pass while visible; true = the screen changed
bool cutShiftHeld();                                     // the Shift button is down
void laneViewRequestGroup(uint8_t firstTrack);           // Left / Right: ask the Session host to switch the group of 4
bool laneViewAnyOffGrid();                               // a track of the 16 is driven by its playhead (SYNC ALL has work to do)
