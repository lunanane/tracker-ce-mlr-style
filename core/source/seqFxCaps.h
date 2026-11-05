// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Largest reverb size and delay feedback the fader page and the engine setters allow (owner report on the v0.2.3 hardware test: the
// top step of both crackled). Host simulation (core/seqtest/test_fx.cpp, scratch sims of the stock Polyverb and the delay loop):
// the stock reverb at size 1.0 is an infinite decay: a sustained low tone or a kick train builds up to about twice the level of
// size 0.9 (peak 29272 against 18385 for the same input) and, hot, clips inside the tank; the delay at feedback 0.80 and higher
// hard-clips its 16 bit line with a sustained loud input. Projects saved above a cap play at the cap.
//
// v0.3 (plan 010 A2, owner: "up to 95 % overall, if possible"): both caps are 95. What made the top steps crackle was the hard 16 bit
// clip inside the loops, so the loops saturate softly now: the delay (softLimit in seqFxDsp.h, since v0.2.4), the stock reverb tank
// and its return (softSat below, core/libs/Audio/Polyverb/reverb.h) and the Plinky ring (softLimit on every ring write). The soft
// functions are the identity up to 5/8 of full scale, so the sound below that level is bit-identical to before.
#pragma once
#include <stdint.h>

namespace seqfx {

constexpr float kReverbSizeCap = 0.95f;       // stock "size" (decay time) 0 .. 1
constexpr uint8_t kDelayFeedbackCap = 95;     // stock delay feedback 0 .. 99 (percent)

inline float capReverbSize(float size) { return size > kReverbSizeCap ? kReverbSizeCap : size; }
inline uint8_t capDelayFeedback(uint8_t percent) { return percent > kDelayFeedbackCap ? kDelayFeedbackCap : percent; }

// Soft saturation of a value in unit full scale (+-1.0 = the 16 bit rails): identity up to 5/8, then a smooth knee (slope 1 at the knee,
// u / (1 + u) above) that approaches +-1 and never reaches it. Used inside the stock reverb tank, where the signal is a float and a
// hard clip (Clip16) at the rails is what crackled. Odd, monotone, continuous, bounded by 1.
constexpr float kSoftKnee = 0.625f;
inline float softSat(float v)
{
    const float a = v < 0.0f ? -v : v;
    if (a <= kSoftKnee)
        return v;
    const float u = (a - kSoftKnee) * (1.0f / (1.0f - kSoftKnee));
    const float s = kSoftKnee + (1.0f - kSoftKnee) * u / (1.0f + u);
    return v < 0.0f ? -s : s;
}
// The same for a value in 16 bit LSB (the reverb return): identity up to 20480, below the rails (32768) for any input.
inline float softSat16(float v)
{
    const float a = v < 0.0f ? -v : v;
    if (a <= 20480.0f)
        return v;
    return softSat(v * (1.0f / 32768.0f)) * 32768.0f;
}

}  // namespace seqfx
