// Sequencer tracks mod (plan 022 P3) - PolyForm Noncommercial 1.0.0, see seqgen/LICENSE.md. No Polyend includes, host-tested.
// The inner loop of the forward sample playback (single shot and loop forward) for a RESIDENT sample without glide in this block, with
// every piece of state in locals: the stock loop stores iPitchCounter, the interpolation values and the output through members on every
// sample and calls readSampleAtRel (a streamed / pre-cache test) twice. Bit-identical output and final state (test_kernel.cpp compares it
// with a transcription of the stock loop over random blocks). The stock loop stays in place for streamed samples and for blocks with glide.
#pragma once
#include <stdint.h>

namespace pmkernel {

constexpr int32_t kMax16 = 65535; // MAX_16BIT

struct Fwd
{
    int32_t ipc;     // iPitchCounter
    int32_t frac;    // currentFractionPitchCounter
    uint8_t playing; // 0 when the sample ended inside the block
    int16_t sample;  // currentSampelValue (the last one read)
    int32_t dif;     // interpolationDif (the last one)
};

// Loop = false: single shot (past the end point the counter is parked at `length`); Loop = true: forward loop (wraps at loopEnd to loop1).
template <bool Loop>
inline void renderForward(Fwd& s, const int16_t* in, int16_t* out, int n, int32_t length, int32_t endPoint, int32_t loopEnd, uint32_t loop1,
                          int32_t cast, int32_t fracCtl, bool noInterp)
{
    int32_t ipc = s.ipc;
    int32_t frac = s.frac;
    uint8_t playing = s.playing;
    int16_t sv = s.sample;
    int32_t dif = s.dif;
    for (int i = 0; i < n; i++)
    {
        if (length > ipc)
        {
            sv = in[ipc];
            dif = noInterp ? 0 : (int32_t)(in[ipc + 1] - sv);
            out[i] = (int16_t)(sv + (int32_t)(((frac >> 2) * dif) >> 14));
            ipc += cast;
            frac += fracCtl;
            if (frac >= kMax16)
            {
                frac -= kMax16;
                ipc++;
            }
            if (Loop)
            {
                if (ipc >= loopEnd)
                    ipc = (int32_t)(loop1 + (uint32_t)(ipc - loopEnd));
            }
            else if (ipc >= endPoint)
            {
                ipc = length;
            }
        }
        else
        {
            out[i] = 0;
            playing = 0;
        }
    }
    s.ipc = ipc;
    s.frac = frac;
    s.playing = playing;
    s.sample = sv;
    s.dif = dif;
}

} // namespace pmkernel
