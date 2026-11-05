#include "effect_limiter.h"
#include "ampLogarythmicValues.h"
#include "mtStructs.h"
#include "util_math.h"
#include <cmath>
#include <string.h>

int16_t zeroblock[AUDIO_BLOCK_SAMPLES];

AUDIO_CORE_FLASH void AudioEffectLimiter::begin()
{
    setAttack(mtProject.values.limiterAttack);
    setRelease(mtProject.values.limiterRelease);
    setThreshold(mtProject.values.limiterThreshold);
    isSideChainEnable = (bool)mtProject.values.limiterSidechain;
    isAverageGain = false;

    memset(inputStorageBuffer, 0, sizeof(inputStorageBuffer));
    memset(outputStorageBuffer, 0, sizeof(outputStorageBuffer)); // plan 009 P#5: both rings start clean (was: the input one only)
    inPos = 0;
    outPos = 0;
}

AUDIO_CORE_FLASH void AudioEffectLimiter::setThreshold(uint16_t ts)
{
    uint8_t quantizedThresholdIndex = mapClamped<int>(ts, 0, MAX_SIGNED_16BIT, 0, 100);
    threshold = limiterThresholdValues[quantizedThresholdIndex];
}
AUDIO_CORE_FLASH void AudioEffectLimiter::setAttack(uint16_t a)
{
    if (mtProject.values.limiterMode == limiterModeSubtle)
        attackTime = 1 - expf(-2.2f * (1000.0f / (44100.0f * a)));
    else if (mtProject.values.limiterMode == limiterModeHard)
        attackTime = a / 1000.0f;
}
AUDIO_CORE_FLASH void AudioEffectLimiter::setRelease(float r)
{
    if (mtProject.values.limiterMode == limiterModeSubtle)
        releaseTime = 1 - expf(-2.2f * (1000.0f / (44100.0f * r)));
    else if (mtProject.values.limiterMode == limiterModeHard)
        releaseTime = r / 1000.0f;
}

AUDIO_CORE_FLASH void AudioEffectLimiter::setSidechainEnable(bool isEnable)
{
    isSideChainEnable = isEnable;
}

AUDIO_CORE_FLASH float AudioEffectLimiter::getAverageGainChange()
{
    isAverageGain = false;
    return averageGain;
}
AUDIO_CORE_FLASH bool AudioEffectLimiter::isAvailableGainToGet()
{
    return isAverageGain;
}

AUDIO_CORE_RAM void AudioEffectLimiter::tapPeak(const int16_t* d)
{
    uint32_t pk = 0;
    for (uint8_t n = 0; n < AUDIO_BLOCK_SAMPLES; n++)
    {
        const int32_t s = d[n];
        const uint32_t a = (uint32_t)(s < 0 ? -s : s);
        pk = a > pk ? a : pk;
    }
    if (pk > outPeak)
        outPeak = pk;
    ring.put(pk); // plan 015 V: the peak of this block for the amplitude view (nothing when no ring is attached)
}

AUDIO_CORE_RAM void AudioEffectLimiter::update(void)
{
    if (isBlocked)
        return;

    if (isSideChainEnable)
        processSidechain();
    else
        processMainStream();
}

// plan 009 P#5: same arithmetic as before, with the state in locals, the divide only when the peak is above the threshold (below it
// threshold / xpeak is >= 1 and the result is clamped to 1, so f = 1 exactly) and the 5-sample delays as rings.
AUDIO_CORE_RAM void AudioEffectLimiter::processSidechain()
{
    audio_block_t* inputBlock;
    audio_block_t* sidechainBlock;
    const int16_t* inputData;
    int16_t* outputData;

    inputBlock = receiveWritable(0);
    if (!inputBlock)
    {
        // plan 010: a missing input block is silence, not "do nothing" (the stale samples of the lookahead would stay, and the
        // chain downstream would starve): treated as zeros
        inputBlock = allocate();
        if (inputBlock)
            memset(inputBlock->data, 0, sizeof(inputBlock->data));
    }
    sidechainBlock = receiveReadOnly(1);
    if (!inputBlock)
    {
        if (sidechainBlock)
            release(sidechainBlock);
        return;
    }

    inputData = sidechainBlock ? sidechainBlock->data : zeroblock;
    outputData = inputBlock->data;

    const float attack = attackTime;
    const float release_ = releaseTime;
    const uint16_t thr = threshold;
    float gain = g;
    uint16_t peak = xpeak;
    float sum = sumGain;
    uint8_t ip = inPos, op = outPos;

    for (uint8_t n = 0; n < AUDIO_BLOCK_SAMPLES; n++)
    {
        const int16_t in = *inputData++;
        const uint16_t a = (in < 0) ? -in : in;

        float coeff = (a > peak) ? attack : release_;
        peak = (1 - coeff) * peak + coeff * a;

        const float f = (peak > thr) ? (float)thr / (float)peak : 1.0f;

        coeff = (f < gain) ? attack : release_;
        gain = (1 - coeff) * gain + coeff * f;

        const int16_t outputTemp = *outputData;
        *outputData++ = gain * outputStorageBuffer[op] + (1 - gain) * in;

        sum += gain;

        inputStorageBuffer[ip] = in;
        outputStorageBuffer[op] = outputTemp;
        ip = (ip == LIMITER_DELAY - 1) ? 0 : ip + 1;
        op = (op == LIMITER_DELAY - 1) ? 0 : op + 1;
    }

    g = gain;
    xpeak = peak;
    inPos = ip;
    outPos = op;
    averageGain = sum / AUDIO_BLOCK_SAMPLES;
    sumGain = 0.0f;
    isAverageGain = true;

    if (peakTap)
        tapPeak(inputBlock->data);
    transmit(inputBlock, 0);
    release(inputBlock);
    if (sidechainBlock)
        release(sidechainBlock);
}
AUDIO_CORE_RAM void AudioEffectLimiter::processMainStream()
{
    audio_block_t* inputBlock;
    audio_block_t* sidechainBlock;
    int16_t* inputData;

    inputBlock = receiveWritable(0);
    if (!inputBlock)
    {
        // plan 010: a missing input block is silence, not "do nothing" (the stale samples of the lookahead would stay, and the
        // chain downstream would starve): treated as zeros
        inputBlock = allocate();
        if (inputBlock)
            memset(inputBlock->data, 0, sizeof(inputBlock->data));
    }
    sidechainBlock = receiveReadOnly(1);
    if (!inputBlock)
    {
        if (sidechainBlock)
            release(sidechainBlock);
        return;
    }

    if (sidechainBlock)
    {
        release(sidechainBlock);
        sidechainBlock = nullptr;
    }
    inputData = inputBlock->data;

    const float attack = attackTime;
    const float release_ = releaseTime;
    const uint16_t thr = threshold;
    float gain = g;
    uint16_t peak = xpeak;
    float sum = sumGain;
    uint8_t ip = inPos;

    for (uint8_t n = 0; n < AUDIO_BLOCK_SAMPLES; n++)
    {
        const int16_t in = *inputData;
        const uint16_t a = (in < 0) ? -in : in;

        float coeff = (a > peak) ? attack : release_;
        peak = (1 - coeff) * peak + coeff * a;

        const float f = (peak > thr) ? (float)thr / (float)peak : 1.0f;

        coeff = (f < gain) ? attack : release_;
        gain = (1 - coeff) * gain + coeff * f;

        *inputData++ = gain * inputStorageBuffer[ip];

        sum += gain;

        inputStorageBuffer[ip] = in;
        ip = (ip == LIMITER_DELAY - 1) ? 0 : ip + 1;
    }

    g = gain;
    xpeak = peak;
    inPos = ip;
    averageGain = sum / AUDIO_BLOCK_SAMPLES;
    sumGain = 0.0f;
    isAverageGain = true;

    if (peakTap)
        tapPeak(inputBlock->data);
    transmit(inputBlock, 0);
    release(inputBlock);
}

AUDIO_CORE_FLASH void AudioEffectLimiter::blockUpdate()
{
    isBlocked = true;
}
AUDIO_CORE_FLASH void AudioEffectLimiter::unblockUpdate()
{
    isBlocked = false;
}
