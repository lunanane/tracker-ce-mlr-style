// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md). Plinky voice parts: MIT, see PlinkyVoice/plinky_voice.h
#include "synth_plinky.h"
#include "cr_mtb_buffer.h"

#ifdef FEATURE_PLINKY
static inline uint32_t cycleCounter()
{
    return *(volatile uint32_t*)0xE0001004;
}
static constexpr float kBlockCycles = 180000000.0f * AUDIO_BLOCK_SAMPLES / 44100.0f;
static_assert(AUDIO_BLOCK_SAMPLES == plinkyvoice::kBlock, "the voice engine renders one audio block per call");

AUDIO_CORE_RAM void AudioSynthPlinky::update(void)
{
    const uint32_t start = cycleCounter();
    float pct = 0.0f;
    if (eng.isActive())
    {
        audio_block_t* l = allocate();
        audio_block_t* r = allocate();
        if (l && r)
        {
            if (eng.render(l->data, r->data))
            {
                transmit(l, 0);
                transmit(r, 1);
            }
        }
        if (l)
            release(l);
        if (r)
            release(r);
        pct = (float)(cycleCounter() - start) * (100.0f / kBlockCycles);
    }
    cpuAvg += (pct - cpuAvg) * (1.0f / 32.0f);
    cpuPeak = pct > cpuPeak ? pct : cpuPeak * 0.999f;
}
#endif
