// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Firmware glue of the MLR cue cache and the section waveform (plan 016 A2): see seqCueCache.h for the bookkeeping.
#pragma once
#include "seqMlrCue.h"
#include "seqMlrLogic.h"
#include <stdint.h>

namespace seqmlr {
void update();                       // main loop, every pass: wants of the cue cache + the waveform job (throttled, cheap when MLR is unused)
void idle();                         // VoiceStreamManager::update, no stream job running: ONE chunk of cache / waveform work (blocking read, ~1 ms)
uint16_t cacheMask(uint8_t slot);    // pads of the slot's shown section whose cue is cached (the dots on the lane screen)
const seqcut::WaveCol* wave(uint8_t slot); // min/max overview of the shown section, nullptr while it is being built
} // namespace seqmlr
