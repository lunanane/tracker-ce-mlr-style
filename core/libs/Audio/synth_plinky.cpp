// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md). Plinky voice parts: MIT, see PlinkyVoice/plinky_voice.h
#include "synth_plinky.h"
#include "cr_mtb_buffer.h"
#include "Profiler.h"

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
        // plan 019: outputs 0 / 1 dry left / right, 2 / 3 reverb send, 4 / 5 delay send (each instance's send level is applied in the engine)
        audio_block_t* b[6] = { allocate(), allocate(), nullptr, nullptr, nullptr, nullptr };
        if (eng.wantSend(false))
        {
            b[2] = allocate();
            b[3] = allocate();
        }
        if (eng.wantSend(true))
        {
            b[4] = allocate();
            b[5] = allocate();
        }
        const bool sendOk = (!eng.wantSend(false) || (b[2] && b[3])) && (!eng.wantSend(true) || (b[4] && b[5]));
        if (b[0] && b[1] && sendOk)
        {
            // the load of the whole previous audio block (all streams, sequencer, UI interrupts) feeds the voice governor
            const int lastUs = Profiler::getInstance()->lastAudioUs();
            eng.setBlockLoad((uint32_t)(lastUs > 0 ? lastUs : 0) * 1000u / 2902u); // block period 128 / 44117.6 Hz = 2902 us
            plinkyvoice::Out o = { b[0]->data, b[1]->data, b[2] ? b[2]->data : nullptr, b[3] ? b[3]->data : nullptr,
                                   b[4] ? b[4]->data : nullptr, b[5] ? b[5]->data : nullptr };
            if (eng.render(o))
                for (int i = 0; i < 6; i++)
                    if (b[i])
                        transmit(b[i], i);
        }
        for (int i = 0; i < 6; i++)
            if (b[i])
                release(b[i]);
        pct = (float)(cycleCounter() - start) * (100.0f / kBlockCycles);
    }
    cpuAvg += (pct - cpuAvg) * (1.0f / 32.0f);
    cpuPeak = pct > cpuPeak ? pct : cpuPeak * 0.999f;
}
#endif
