// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Firmware glue of the per-track sequencers (see plans/001-sequencer-tracks.md).
#pragma once
#include "seqTracksCore.h"

extern seqtracks::Engine seqEngine;
extern seqlogic::TapDetector seqPatternTap; // Pattern button tap detection (interface inputs + pattern editor)
extern seqlogic::TapDetector seqPerformTap; // Performance button tap detection (interface inputs + Performance pages)
extern seqlogic::SongToggle seqSongToggle; // Song button: plain press + release (no hold) on the Song page opens the launcher (Interface::buttonChange)
extern bool seqSongToggleNow;              // set for the duration of the Song release that completed a plain press
extern uint8_t seqPadsHeld; // pads currently held (any page); informational, held pads do not block a tap (plan 005 M4)

uint32_t seqNowMs(); // free-running milliseconds (tap timing)

// A button other than the mode buttons themselves was used: breaks the running taps (only buttons break a tap; pads and the jog
// wheel do not). The flags say which mode button changed: it does not break its own tap.
inline void seqNoteButtonInput(bool isPattern, bool isPerformance, bool isSong)
{
    if (!isPattern)
        seqPatternTap.other();
    if (!isPerformance)
        seqPerformTap.other();
    if (!isSong)
        seqSongToggle.other();
}

// Release of a mode button: true = it was a tap. A quick press that was refused (another button interfered) sets the
// "refused" signal; the title bar flashes once (seqTapRefusedTake, polled by the interface).
extern volatile bool seqTapRefusedFlag;
inline bool seqTapUp(seqlogic::TapDetector& d)
{
    bool tap = d.up(seqNowMs());
    if (d.refusedLast())
        seqTapRefusedFlag = true;
    return tap;
}
inline bool seqTapRefusedTake()
{
    bool r = seqTapRefusedFlag;
    seqTapRefusedFlag = false;
    return r;
}

// Transport Stop = panic (plan 005 M6): Sequencer::stop() asks (any context), the main loop runs seqPanic() (seqHooks.h) once.
void seqPanicRequest();

// ---- pads (main loop) ----
bool seqPadPress(uint8_t track, uint8_t pad, uint8_t velo = 0); // false: dropped (queue full); a press may be lost. velo: pad pressure (Plinky type)
void seqPadRelease(uint8_t track, uint8_t pad); // never lost: kept and retried from seqTracksUpdate until accepted

// ---- CUT (MLR) layer (main loop) ----
void seqCutInit(); // constructs the 16 playheads in external RAM (idempotent; also done when the workspace is loaded)

// ---- record (main loop; Play mode screen calls these) ----
bool seqRecTrackLaunched(uint8_t track); // the track holds a launched Session clip: it is not recorded
void seqTracksViewLeft();                // Play / Session view closed: asks for the project autosave
bool seqRecStart(bool oneLoop, uint8_t track); // false: track is Off or already recording. Starts the transport if stopped.
bool seqRecStopPatternChanged(); // like seqRecStop, the end is reported as "pattern changed"
bool seqRecStop(); // false: the command did not fit into the queue (the caller retries)
void seqTracksUpdate(); // once per main loop pass (Tracker::update): applies recorded steps, watches the state

// ---- project sidecar "seqtracks.bin" (main loop, file manager steps) ----
void seqTracksLoadWorkspace();                                      // Workspace/seqtracks.bin -> engine (missing/invalid: all Off)
void seqTracksSaveWorkspace();                                      // engine -> Workspace/seqtracks.bin when it changed
void seqTracksSaveProject(const char* path, const char* name);      // engine -> <path>/<name>/seqtracks.bin (+ workspace)
void seqTracksImportProject(const char* path, const char* name);    // <path>/<name>/seqtracks.bin -> Workspace (when present)
