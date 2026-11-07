// Sequencer tracks mod (plan 021) - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
#include "effect_beatRepeat.h"
#include "cr_section_macros.h"
#include <string.h>

// The slice ring: raw storage in the SDRAM, NO constructor / initialiser (platforms/mk66/check_sdram_ctors.sh): the SDRAM is powered up
// after the static constructors run. It is never read before a capture wrote it (the core reads only what it captured).
__NOINIT(EXTERNAL_RAM) __attribute__((aligned(32))) static int16_t s_ring[2 * beatrepeat::kRingFrames];

namespace
{
constexpr float kBlockCycles = 180000000.0f * AUDIO_BLOCK_SAMPLES / 44117.64706f;
inline uint32_t cycleCounter()
{
    return *(volatile uint32_t*)0xE0001004; // DWT_CYCCNT (enabled by the reverb / warp readouts)
}
} // namespace

void AudioEffectBeatRepeat::begin()
{
    core_.init(s_ring, beatrepeat::kRingFrames);
    volatile uint32_t* const dwtCtrl = (volatile uint32_t*)0xE0001000;
    *dwtCtrl = *dwtCtrl | 1u; // DWT_CTRL.CYCCNTENA
}

AUDIO_CORE_RAM void AudioEffectBeatRepeat::update(void)
{
    audio_block_t* l = receiveWritable(0);
    audio_block_t* r = receiveWritable(1);
    const bool hpOn = core_.hp().active();
    if (core_.bypassable() && !hpOn)
    {
        // exact bypass: the blocks go on as they are (a missing block stays missing - the limiter treats it as silence)
        core_.noteBypass();
        cpu = 0.0f;
        if (l)
        {
            transmit(l, 0);
            release(l);
        }
        if (r)
        {
            transmit(r, 1);
            release(r);
        }
        return;
    }
    if (!l)
    {
        l = allocate();
        if (l)
            memset(l->data, 0, sizeof(l->data));
    }
    if (!r)
    {
        r = allocate();
        if (r)
            memset(r->data, 0, sizeof(r->data));
    }
    if (!l || !r)
    {
        if (l)
            release(l);
        if (r)
            release(r);
        return;
    }
    beatrepeat::Clock clk;
    if (clockFn)
        clockFn(clk);
    const uint32_t t0 = cycleCounter();
    if (!core_.bypassable())
        core_.process(clk, l->data, r->data, l->data, r->data, AUDIO_BLOCK_SAMPLES);
    else
        core_.noteBypass();
    if (hpOn)
        core_.hp().process(l->data, r->data, AUDIO_BLOCK_SAMPLES); // plan 024: master high-pass (bypassed = not called)
    const float pct = (float)(cycleCounter() - t0) * (100.0f / kBlockCycles);
    cpu += (pct - cpu) * (1.0f / 32.0f);
    transmit(l, 0);
    transmit(r, 1);
    release(l);
    release(r);
}
