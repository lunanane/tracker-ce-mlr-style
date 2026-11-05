#include "ref_shortDelay.h"
#include "Audio.h"
#include "mtSequencer.h"
#include "utility/dspinst.h"

static int16_t zeroblock[AUDIO_BLOCK_SAMPLES] = {};

const float refTempoSyncRatesShortDelay[19] = {
    8,        6,    4,      3,        2,     1.5,      1,      0.75,     0.5,     0.375,
    0.333333, 0.25, 0.1875, 0.166667, 0.125, 0.083333, 0.0625, 0.041667, 0.03125,
};
extern int16_t externalRamDelayBuff[];

AUDIO_CORE_FLASH void RefShortDelay::begin(float feedback,
                                             uint32_t timeMs,
                                             bool pingPongEnabled,
                                             bool syncEnabled,
                                             uint8_t syncRateIdx,
                                             float frequency)
{
    setPingpongEnable(pingPongEnabled);
    setSyncEnable(syncEnabled);
    setTime(timeMs);
    setFeedback(feedback);
    setRate(syncRateIdx);
    setFilterFrequency(frequency);
    bandpassControl.restart(); // the first block starts at the loaded value, no sweep from the defaults

    setBuffer(externalRamDelayBuff);
    unblockUpdate();
}

AUDIO_CORE_FLASH void RefShortDelay::setBuffer(int16_t* buffer)
{
    pingpongDelaylineRight = &buffer[0];
    pingpongDelaylineLeft = &buffer[REF_MAX_DELAYLINE_LENGTH];
    monoDelayline = &buffer[2 * REF_MAX_DELAYLINE_LENGTH];

    clear();
}

AUDIO_CORE_FLASH void RefShortDelay::setFeedback(float feedback)
{
    if (feedback > kMaxFeedback)
        feedback = kMaxFeedback;
    else if (feedback < 0.0f)
        feedback = 0.0f;

    //	__disable_irq();
    AudioNoInterrupts();
    this->feedback = UINT8_MAX * feedback;
    AudioInterrupts();
    //	__enable_irq();
}
AUDIO_CORE_FLASH void RefShortDelay::setTime(float time)
{
    if (time > REF_MAX_SHORT_DELAY_TIME)
        time = REF_MAX_SHORT_DELAY_TIME;
    //	__disable_irq();
    AudioNoInterrupts();
    timeInSamples = (uint32_t)(time * 44.1f);
    //	__enable_irq();
    AudioInterrupts();
}

AUDIO_CORE_FLASH int RefShortDelay::getTime()
{
    return int(timeInSamples / 44.1f);
}

AUDIO_CORE_FLASH void RefShortDelay::setRate(uint8_t syncRateIdx)
{
    if (!isSync)
        return;

    float tempo = sequencer.getCurrentTempo();

    float freq = (tempo / 15.0f); // *4,bo  beat = 4 step i /60, bo min = 60s
    float timeMs = (1000 / freq) * refTempoSyncRatesShortDelay[syncRateIdx];

    setTime(timeMs);
}
AUDIO_CORE_FLASH void RefShortDelay::setPingpongEnable(bool pingPongEnabled)
{
    isPingpong = pingPongEnabled;
}
AUDIO_CORE_FLASH void RefShortDelay::setSyncEnable(bool syncEnabled)
{
    isSync = syncEnabled;
}

AUDIO_CORE_FLASH void RefShortDelay::setFilterFrequency(float frequency)
{
    if (frequency < seqfx::kCenterMinHz)
        frequency = seqfx::kCenterMinHz;
    bandpassControl.targetOct = log2f(frequency);
    bandpassControl.targetQ = seqfx::bwToQ(filterWidthOctaves);
    bandpassControl.targetMix = seqfx::bandMix(frequency);
}

AUDIO_CORE_FLASH void RefShortDelay::setFilterWidth(float octaves)
{
    filterWidthOctaves = octaves;
    bandpassControl.targetQ = seqfx::bwToQ(octaves);
}

AUDIO_CORE_FLASH void RefShortDelay::clear()
{
    blockUpdate();
    //    AudioNoInterrupts();
    headIdx = 0;
    bandpassLeft.clear();
    bandpassRight.clear();
    bandpassMono.clear();
    silentSamplesCount = REF_MAX_DELAYLINE_LENGTH;

    memset(pingpongDelaylineRight, 0, REF_MAX_DELAYLINE_LENGTH * sizeof(int16_t));
    memset(pingpongDelaylineLeft, 0, REF_MAX_DELAYLINE_LENGTH * sizeof(int16_t));
    memset(monoDelayline, 0, REF_MAX_DELAYLINE_LENGTH * sizeof(int16_t));
    //    AudioInterrupts();
    unblockUpdate();
}

AUDIO_CORE_FLASH void RefShortDelay::blockUpdate()
{
    noRefresh = true;
}
AUDIO_CORE_FLASH void RefShortDelay::unblockUpdate()
{
    noRefresh = false;
}

AUDIO_CORE_RAM void RefShortDelay::update(void)
{
    audio_block_t* input;
    audio_block_t* outputLeft;
    audio_block_t* outputRight;

    input = receiveReadOnly(0);
    outputLeft = allocate();
    outputRight = allocate();

    bool processed = false;

    if (outputLeft != nullptr && outputRight != nullptr)
    {
        processed = process(input != nullptr ? input->data : zeroblock,
                            outputLeft->data,
                            outputRight->data,
                            AUDIO_BLOCK_SAMPLES);
    }

    if (outputRight != nullptr)
    {
        if (processed)
        {
            transmit(outputRight, 0);
        }
        release(outputRight);
    }
    if (outputLeft != nullptr)
    {
        if (processed)
        {
            transmit(outputLeft, 1);
        }
        release(outputLeft);
    }
    if (input != nullptr)
    {
        release(input);
    }
}

AUDIO_CORE_RAM bool RefShortDelay::process(int16_t input[],
                                               int16_t outputLeft[],
                                               int16_t outputRight[],
                                               int sampleCount)
{
    if (noRefresh == true)
    {
        return false;
    }

    int32_t newDelayedSampleLeft = 0;
    int32_t newDelayedSampleRight = 0;
    int32_t newDelayedSampleMono = 0;

    // bandpass in the feedback loop: the control is smoothed once per block; out of the path (mix 0) it costs nothing
    bandpassControl.blockUpdate();
    const float mixPrev = bandpassControl.prevMix();
    const float mixCur = bandpassControl.mix();
    const bool filterOn = mixPrev > 0.0f || mixCur > 0.0f;
    const seqfx::SvfCoef bp = bandpassControl.coef();
    const bool pingpong = isPingpong != 0;
    if (!filterOn)
    {
        bandpassLeft.clear();
        bandpassRight.clear();
        bandpassMono.clear();
    }

    for (int sample = 0; sample < sampleCount; sample++)
    {
        uint32_t delayedSampleIdx = (headIdx - timeInSamples) & delaylineLengthMask;
        int16_t delayedSampleRight = pingpongDelaylineRight[delayedSampleIdx];
        int16_t delayedSampleLeft = pingpongDelaylineLeft[delayedSampleIdx];
        int16_t delayedSampleMono = monoDelayline[delayedSampleIdx];

        int32_t filteredSampleRight = delayedSampleRight;
        int32_t filteredSampleLeft = delayedSampleLeft;
        int32_t filteredSampleMono = delayedSampleMono;
        if (filterOn)
        {
            // the mix ramps linearly over the block: engaging / releasing the band never steps the signal.
            // Only the lines that are heard run the filter (ping-pong: 2, mono: 1); the other one passes through.
            const float m = seqfx::mixAt(mixPrev, mixCur, sample, sampleCount);
            if (pingpong)
            {
                float xr = (float)delayedSampleRight, xl = (float)delayedSampleLeft;
                filteredSampleRight =
                  signed_saturate_rshift((int32_t)(xr + m * (seqfx::bandpass(bandpassRight, bp, xr) - xr)), 16, 0);
                filteredSampleLeft =
                  signed_saturate_rshift((int32_t)(xl + m * (seqfx::bandpass(bandpassLeft, bp, xl) - xl)), 16, 0);
            }
            else
            {
                float xm = (float)delayedSampleMono;
                filteredSampleMono =
                  signed_saturate_rshift((int32_t)(xm + m * (seqfx::bandpass(bandpassMono, bp, xm) - xm)), 16, 0);
            }
        }

        int16_t outL = isPingpong ? filteredSampleLeft : filteredSampleMono;
        int16_t outR = isPingpong ? filteredSampleRight : filteredSampleMono;
        *outputLeft++ = outL;
        *outputRight++ = outR;

        int32_t inputShifted = ((*input++) << kFeedbackShift);

        newDelayedSampleLeft = inputShifted + filteredSampleRight * feedback;
        newDelayedSampleRight = filteredSampleLeft * feedback;
        newDelayedSampleMono = inputShifted + filteredSampleMono * feedback;

        static constexpr int32_t kFeedbackRoundBias = (1 << kFeedbackShift) - 1;
        // soft limit instead of the hard 16 bit clip: a loud sustained signal at a high feedback compresses instead of crackling
        int16_t saturatedLeft = (int16_t)signed_saturate_rshift(
          seqfx::softLimit((newDelayedSampleLeft + (newDelayedSampleLeft < 0 ? kFeedbackRoundBias : 0)) >> kFeedbackShift), 16, 0);
        int16_t saturatedRight = (int16_t)signed_saturate_rshift(
          seqfx::softLimit((newDelayedSampleRight + (newDelayedSampleRight < 0 ? kFeedbackRoundBias : 0)) >> kFeedbackShift), 16, 0);
        int16_t saturatedMono = (int16_t)signed_saturate_rshift(
          seqfx::softLimit((newDelayedSampleMono + (newDelayedSampleMono < 0 ? kFeedbackRoundBias : 0)) >> kFeedbackShift), 16, 0);

        bool isSampleSilent =
          isPingpong ? (outL == 0 && outR == 0 && saturatedLeft == 0 && saturatedRight == 0)
                     : (outL == 0 && saturatedMono == 0);
        if (isSampleSilent)
        {
            if (silentSamplesCount < REF_MAX_DELAYLINE_LENGTH)
            {
                silentSamplesCount++;
            }
        }
        else
        {
            silentSamplesCount = 0;
        }

        uint32_t headSampleIdx = headIdx++ & delaylineLengthMask;

        pingpongDelaylineLeft[headSampleIdx] = saturatedLeft;
        pingpongDelaylineRight[headSampleIdx] = saturatedRight;
        monoDelayline[headSampleIdx] = saturatedMono;
    }

    if (filterOn)
    {
        seqfx::svfFlush(bandpassLeft);
        seqfx::svfFlush(bandpassRight);
        seqfx::svfFlush(bandpassMono);
    }

    return true;
}
