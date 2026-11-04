// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Firmware glue of the per-track sequencers (see plans/001-sequencer-tracks.md).
#pragma once
#include "seqTracksCore.h"

extern seqtracks::Engine seqEngine;
extern seqlogic::TapDetector seqPatternTap; // Pattern button tap detection (interface inputs + pattern editor)
extern uint8_t seqPadsHeld; // pads currently held (any page); a Performance tap needs none
extern seqlogic::TapDetector seqPerformTap; // Performance button tap detection (interface inputs + Performance pages)

uint32_t seqNowMs(); // free-running milliseconds (Pattern tap timing)

// Any button / pad / jog input other than the Pattern button itself (breaks a Pattern tap).
inline void seqNoteOtherInput()
{
    seqPatternTap.other();
    seqPerformTap.other();
}

// ---- pads (main loop) ----
bool seqPadPress(uint8_t track, uint8_t pad);   // false: dropped (queue full); a press may be lost
void seqPadRelease(uint8_t track, uint8_t pad); // never lost: kept and retried from seqTracksUpdate until accepted

// ---- CUT (MLR) layer (main loop) ----
void seqCutInit(); // constructs the 16 playheads in external RAM (idempotent; also done when the workspace is loaded)

// ---- record (main loop; Play mode screen calls these) ----
bool seqRecStart(bool oneLoop, uint8_t track); // false: track is Off or already recording. Starts the transport if stopped.
void seqRecStop();
void seqTracksUpdate(); // once per main loop pass (Tracker::update): applies recorded steps, watches the state

// ---- project sidecar "seqtracks.bin" (main loop, file manager steps) ----
void seqTracksLoadWorkspace();                                      // Workspace/seqtracks.bin -> engine (missing/invalid: all Off)
void seqTracksSaveWorkspace();                                      // engine -> Workspace/seqtracks.bin when it changed
void seqTracksSaveProject(const char* path, const char* name);      // engine -> <path>/<name>/seqtracks.bin (+ workspace)
void seqTracksImportProject(const char* path, const char* name);    // <path>/<name>/seqtracks.bin -> Workspace (when present)
