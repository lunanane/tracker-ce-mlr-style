// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Firmware glue of the per-track sequencers (see plans/001-sequencer-tracks.md).
#pragma once
#include "seqTracksCore.h"

extern seqtracks::Engine seqEngine;
extern seqlogic::TapDetector seqPatternTap; // Pattern button tap detection (interface inputs + pattern editor)

// Any button / pad / jog input other than the Pattern button itself (breaks a Pattern tap).
inline void seqNoteOtherInput()
{
    seqPatternTap.other();
}
