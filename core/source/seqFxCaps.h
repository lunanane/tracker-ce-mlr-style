// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Largest reverb size and delay feedback the fader page and the engine setters allow (owner report on the v0.2.3 hardware test: the
// top step of both crackled). Host simulation (core/seqtest/test_fx.cpp, scratch sims of the stock Polyverb and the delay loop):
// the stock reverb at size 1.0 is an infinite decay: a sustained low tone or a kick train builds up to about twice the level of
// size 0.9 (peak 29272 against 18385 for the same input) and, hot, clips inside the tank; the delay at feedback 0.80 and higher
// hard-clips its 16 bit line with a sustained loud input. Projects saved above a cap play at the cap.
#pragma once
#include <stdint.h>

namespace seqfx {

constexpr float kReverbSizeCap = 0.90f;       // stock "size" (decay time) 0 .. 1
constexpr uint8_t kDelayFeedbackCap = 88;     // stock delay feedback 0 .. 99 (percent)

inline float capReverbSize(float size) { return size > kReverbSizeCap ? kReverbSizeCap : size; }
inline uint8_t capDelayFeedback(uint8_t percent) { return percent > kDelayFeedbackCap ? kDelayFeedbackCap : percent; }

}  // namespace seqfx
