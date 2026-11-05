// Copyright 2020 Wojciech Jakóbczyk (jakobczyk.woj@gmail.com)

#include "effect_polyverb.h"
#include "cr_mtb_buffer.h"
#include "utility/dspinst.h"
#include <math.h>
#include <string.h>

#ifdef PLATFORM_IMX
extern uint16_t externalRamReverbBuff[];
#endif

// Cortex-M DWT cycle counter (M4 and M7 share the registers). Enabled here, harmless if the platform already did.
static inline uint32_t cycleCounter()
{
    return *(volatile uint32_t*)0xE0001004;
}
#ifdef PLATFORM_IMX
static constexpr float kBlockCycles = 600000000.0f * AUDIO_BLOCK_SAMPLES / 44100.0f;
#else
static constexpr float kBlockCycles = 180000000.0f * AUDIO_BLOCK_SAMPLES / 44100.0f;
#endif
// The ring reverb of the reverb type "Plinky" (plan 007 F3). Original Tracker: 64 KB ring + 16 KB stereo pre-delay in one block of
// external RAM (80 KB). Tracker Plus / Mini (external RAM is full there): the ring lives in the two buffers of the stock reverb
// (idle while this type runs) and only a 4 KB pre-delay line is added. Cleared in pieces (clearStep) every time the type is entered.
#if defined(PLATFORM_IMX)
static __NOINIT(EXTERNAL_RAM) int16_t ringPreMemory[2 * ringverb::kPreSize];
#else
static __NOINIT(EXTERNAL_RAM) int16_t ringReverbMemory[ringverb::kRingSize + 2 * ringverb::kPreSize];
#endif
#ifdef PLATFORM_IMX
static uint16_t internalRamReverbBuff[16384] __attribute__((section(".noinit")));
#else
// plan 009 P#1: the delay lines of the stock reverb live in internal SRAM (.bss, zeroed at start): 29 uncached SDRAM accesses per sample
// become single-cycle ones. 32 768 + 22 080 bytes. The old SDRAM array externalRamReverbBuff (Tracker.cpp) is no longer used here.
static uint16_t internalRamReverbBuff[Reverb::kBuffer1Words];
static uint16_t sramReverbBuff2[Reverb::kBuffer2Words];
#endif

AudioEffectPolyverb::AudioEffectPolyverb()
  : AudioStream(2, inputQueueArray)
{
    buffer = internalRamReverbBuff;
#ifdef PLATFORM_IMX
    buffer2 = externalRamReverbBuff;
#else
    buffer2 = sramReverbBuff2;
#endif
    reverb.Init(buffer, buffer2);

#if defined(PLATFORM_IMX)
    ring.init(ringverb::SplitRing{ (int16_t*)buffer, (int16_t*)buffer2 }, ringPreMemory);
#else
    ring.init(ringverb::PlainRing{ ringReverbMemory }, ringReverbMemory + ringverb::kRingSize);
#endif
    *(volatile uint32_t*)0xE000EDFC |= (1u << 24); // DEMCR.TRCENA
    *(volatile uint32_t*)0xE0001000 |= 1u;         // DWT_CTRL.CYCCNTENA
    reverb.SetInputGain(0.1f);
    reverb.SetDiffusion(0.625f);
    setTime(0.7f);
    setDamp(0.2f);
    setAmount(0.8f);
    blockUpdate();
}

AudioEffectPolyverb::~AudioEffectPolyverb()
{
}

AUDIO_CORE_RAM void AudioEffectPolyverb::update(void)
{
    if (isBlockedUpdate)
        return;

    audio_block_t* block_left;
    audio_block_t* block_right;

    // plan 010: a missing input block is silence (zeros), not "skip the block": the tail of the reverb keeps running and the output
    // stays clocked. Today the always-on line-in chain keeps the bus fed (see connectInputToMixer); the silence skip of the stock
    // reverb (ProcessOrSkip) still applies to the zeros.
    block_left = receiveWritable(0);
    if (block_left == NULL)
    {
        block_left = allocate();
        if (block_left == NULL)
            return;
        memset(block_left->data, 0, sizeof(block_left->data));
    }
    block_right = receiveWritable(1);
    if (block_right == NULL)
    {
        block_right = allocate();
        if (block_right == NULL)
        {
            release(block_left);
            return;
        }
        memset(block_right->data, 0, sizeof(block_right->data));
    }

    int16_t* l = block_left->data;
    int16_t* r = block_right->data;

    // F2: highpass on the reverb input (both types). Smoothed centre, out of the path (mix 0) it costs nothing.
    const uint32_t cycleStart = cycleCounter();
    hpControl.blockUpdate();
    const float hpPrev = hpControl.prevMix();
    const float hpCur = hpControl.mix();
    if (hpPrev > 0.0f || hpCur > 0.0f)
    {
        const seqfx::SvfCoef hc = hpControl.coef();
        for (int s = 0; s < AUDIO_BLOCK_SAMPLES; s++)
        {
            const float hpMix = seqfx::mixAt(hpPrev, hpCur, s, AUDIO_BLOCK_SAMPLES); // linear ramp: off <-> on never steps
            float xl = (float)l[s], xr = (float)r[s];
            l[s] = (int16_t)signed_saturate_rshift((int32_t)(xl + hpMix * (seqfx::highpass(hpLeft, hc, xl) - xl)), 16, 0);
            r[s] = (int16_t)signed_saturate_rshift((int32_t)(xr + hpMix * (seqfx::highpass(hpRight, hc, xr) - xr)), 16, 0);
        }
        seqfx::svfFlush(hpLeft);
        seqfx::svfFlush(hpRight);
    }
    else
    {
        hpLeft.clear();
        hpRight.clear();
    }

    // F3: the type switch is a small state machine, one step per block
    switch (typeSwitch.step(requestedType))
    {
        case seqfx::TypeSwitch::ACT_FADE_OUT:
            runType(l, r);
            for (int s = 0; s < AUDIO_BLOCK_SAMPLES; s++)
            {
                int32_t gain = AUDIO_BLOCK_SAMPLES - s; // 128 .. 1
                l[s] = (int16_t)((l[s] * gain) >> 7);
                r[s] = (int16_t)((r[s] * gain) >> 7);
            }
            break;
        case seqfx::TypeSwitch::ACT_CLEAR:
            for (int s = 0; s < AUDIO_BLOCK_SAMPLES; s++)
                l[s] = r[s] = 0;
            if (clearStep())
            {
                typeSwitch.clearFinished();
                if (typeSwitch.active == seqfx::RT_STOCK)
                    reverb.ResetState();
                else
                    ring.resetState();
                ringDirty = true;
            }
            break;
        case seqfx::TypeSwitch::ACT_FADE_IN:
            runType(l, r);
            for (int s = 0; s < AUDIO_BLOCK_SAMPLES; s++)
            {
                int32_t gain = s + 1; // 1 .. 128
                l[s] = (int16_t)((l[s] * gain) >> 7);
                r[s] = (int16_t)((r[s] * gain) >> 7);
            }
            break;
        default: runType(l, r); break;
    }

    // CPU of this block (DWT cycle counter): average and slowly decaying peak, in percent of the block period
    {
        const float pct = (float)(cycleCounter() - cycleStart) * (100.0f / kBlockCycles);
        cpuAvg += (pct - cpuAvg) * (1.0f / 32.0f);
        cpuPeak = pct > cpuPeak ? pct : cpuPeak * 0.999f;
    }

    transmit(block_left, 0);
    transmit(block_right, 1);

    release(block_left);
    release(block_right);
}

AUDIO_CORE_RAM void AudioEffectPolyverb::runType(int16_t* l, int16_t* r)
{
    if (typeSwitch.active == seqfx::RT_PLINKY)
    {
        if (ringDirty)
        {
            ringDirty = false;
            ring.setParams(seqfx::ringFade(size_), seqfx::ringColor(damp_), seqfx::ringApGain(diffusion_),
                           seqfx::ringPredelay(predelay_));
        }
        ring.process(l, r, AUDIO_BLOCK_SAMPLES);
    }
    else
    {
        reverb.ProcessOrSkip(l, r, AUDIO_BLOCK_SAMPLES); // plan 009 P#7: nothing to do while idle and the tail is gone
    }
}

// Clears 8 KB of the memory of typeSwitch.clearType per call; true when everything is clean. The stock reverb has two buffers;
// the ring reverb is one block (original Tracker) or the same two stock buffers plus the small pre-delay line (Plus / Mini).
AUDIO_CORE_RAM bool AudioEffectPolyverb::clearStep()
{
    uint8_t* seg[3];
    uint32_t len[3];
    int n = 0;
#if defined(PLATFORM_IMX)
    seg[n] = (uint8_t*)buffer;
    len[n++] = 16384 * sizeof(uint16_t); // internalRamReverbBuff
    seg[n] = (uint8_t*)buffer2;
    len[n++] = 16384 * sizeof(uint16_t); // externalRamReverbBuff (REVERB_MEMORY_SIZE)
    if (typeSwitch.clearType == seqfx::RT_PLINKY)
    {
        seg[n] = (uint8_t*)ringPreMemory;
        len[n++] = sizeof(ringPreMemory);
    }
#else
    if (typeSwitch.clearType == seqfx::RT_PLINKY)
    {
        seg[n] = (uint8_t*)ringReverbMemory;
        len[n++] = sizeof(ringReverbMemory);
    }
    else
    {
        seg[n] = (uint8_t*)buffer;
        len[n++] = sizeof(internalRamReverbBuff);
        seg[n] = (uint8_t*)buffer2;
        len[n++] = sizeof(sramReverbBuff2);
    }
#endif
    int which = 0;
    uint32_t start = 0;
    uint32_t m = seqfx::nextClearPiece(len, n, typeSwitch.clearOffset, 8192, &which, &start);
    if (m)
        memset(seg[which] + start, 0, m);
    return typeSwitch.clearOffset >= seqfx::clearTotal(len, n);
}

AUDIO_CORE_FLASH void AudioEffectPolyverb::setHighpass(uint8_t idx)
{
    float hz = seqfx::hpHz(idx);
    if (hz <= 0.0f)
    {
        hpControl.targetMix = 0.0f;
        return;
    }
    hpControl.targetOct = log2f(hz);
    hpControl.targetQ = seqfx::kHpQ;
    hpControl.targetMix = 1.0f;
}

AUDIO_CORE_FLASH void AudioEffectPolyverb::setShimmer(uint8_t raw)
{
    ring.setShimmer(raw); // plan 010 A3: smoothed in the ring (a few units per block)
}

AUDIO_CORE_FLASH void AudioEffectPolyverb::reset(void)
{
    isBlockedUpdate = true;
    typeSwitch.jumpTo(requestedType);
    if (typeSwitch.active == seqfx::RT_PLINKY)
    {
#if defined(PLATFORM_IMX)
        reverb.Reset(); // clears the two stock buffers, which hold the ring here
        memset(ringPreMemory, 0, sizeof(ringPreMemory));
#else
        memset(ringReverbMemory, 0, sizeof(ringReverbMemory));
#endif
        ring.resetState();
        ringDirty = true;
    }
    else
    {
        reverb.Reset();
    }
    isBlockedUpdate = false;
}

AUDIO_CORE_FLASH bool AudioEffectPolyverb::isActive(void)
{
    return reverb.IsActive();
}

AUDIO_CORE_FLASH void AudioEffectPolyverb::setActiveThreshold(float threshold)
{
    reverb.SetActiveThreshold(threshold);
}
