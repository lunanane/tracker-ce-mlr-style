#include "effect_shortDelay.h"
#include "Audio.h"
#include "mtSequencer.h"
#include "utility/dspinst.h"

static int16_t zeroblock[AUDIO_BLOCK_SAMPLES] = {};

const float tempoSyncRatesShortDelay[19] = {
    8,        6,    4,      3,        2,     1.5,      1,      0.75,     0.5,     0.375,
    0.333333, 0.25, 0.1875, 0.166667, 0.125, 0.083333, 0.0625, 0.041667, 0.03125,
};
extern int16_t externalRamDelayBuff[];

AUDIO_CORE_FLASH void AudioEffectShortDelay::begin(float feedback,
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

AUDIO_CORE_FLASH void AudioEffectShortDelay::setBuffer(int16_t* buffer)
{
    pingpongDelaylineRight = &buffer[0];
    pingpongDelaylineLeft = &buffer[MAX_DELAYLINE_LENGTH];
    monoDelayline = &buffer[2 * MAX_DELAYLINE_LENGTH];

    clear();
}

AUDIO_CORE_FLASH void AudioEffectShortDelay::setFeedback(float feedback)
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
AUDIO_CORE_FLASH void AudioEffectShortDelay::setTime(float time)
{
    if (time > MAX_SHORT_DELAY_TIME)
        time = MAX_SHORT_DELAY_TIME;
    //	__disable_irq();
    AudioNoInterrupts();
    timeInSamples = (uint32_t)(time * 44.1f);
    //	__enable_irq();
    AudioInterrupts();
}

AUDIO_CORE_FLASH int AudioEffectShortDelay::getTime()
{
    return int(timeInSamples / 44.1f);
}

AUDIO_CORE_FLASH void AudioEffectShortDelay::setRate(uint8_t syncRateIdx)
{
    if (!isSync)
        return;

    float tempo = sequencer.getCurrentTempo();

    float freq = (tempo / 15.0f); // *4,bo  beat = 4 step i /60, bo min = 60s
    float timeMs = (1000 / freq) * tempoSyncRatesShortDelay[syncRateIdx];

    setTime(timeMs);
}
AUDIO_CORE_FLASH void AudioEffectShortDelay::setPingpongEnable(bool pingPongEnabled)
{
    isPingpong = pingPongEnabled;
}
AUDIO_CORE_FLASH void AudioEffectShortDelay::setSyncEnable(bool syncEnabled)
{
    isSync = syncEnabled;
}

AUDIO_CORE_FLASH void AudioEffectShortDelay::setFilterFrequency(float frequency)
{
    if (frequency < seqfx::kCenterMinHz)
        frequency = seqfx::kCenterMinHz;
    bandpassControl.targetOct = log2f(frequency);
    bandpassControl.targetQ = seqfx::bwToQ(filterWidthOctaves);
    bandpassControl.targetMix = seqfx::bandMix(frequency);
}

AUDIO_CORE_FLASH void AudioEffectShortDelay::setFilterWidth(float octaves)
{
    filterWidthOctaves = octaves;
    bandpassControl.targetQ = seqfx::bwToQ(octaves);
}

AUDIO_CORE_FLASH void AudioEffectShortDelay::clear()
{
    blockUpdate();
    //    AudioNoInterrupts();
    headIdx = 0;
    bandpassLeft.clear();
    bandpassRight.clear();
    bandpassMono.clear();
    silentSamplesCount = MAX_DELAYLINE_LENGTH;
    heardPingpong = isPingpong != 0;
    validSamples = MAX_DELAYLINE_LENGTH; // all lines are zero now

    memset(pingpongDelaylineRight, 0, MAX_DELAYLINE_LENGTH * sizeof(int16_t));
    memset(pingpongDelaylineLeft, 0, MAX_DELAYLINE_LENGTH * sizeof(int16_t));
    memset(monoDelayline, 0, MAX_DELAYLINE_LENGTH * sizeof(int16_t));
    //    AudioInterrupts();
    unblockUpdate();
}

AUDIO_CORE_FLASH void AudioEffectShortDelay::blockUpdate()
{
    noRefresh = true;
}
AUDIO_CORE_FLASH void AudioEffectShortDelay::unblockUpdate()
{
    noRefresh = false;
}

AUDIO_CORE_RAM void AudioEffectShortDelay::update(void)
{
    audio_block_t* input;
    audio_block_t* outputLeft;
    audio_block_t* outputRight;

    input = receiveReadOnly(0);

    // plan 009 P#4: nothing in, nothing left in the heard lines (the last timeInSamples samples written were all zero): the block would be
    // all zero again. Write the zeros, move on and send nothing (the mixers treat a missing block as silence).
    if (!noRefresh && timeInSamples != 0 && silentSamplesCount >= timeInSamples && heardPingpong == (isPingpong != 0))
    {
        bool silentIn = true;
        if (input != nullptr)
        {
            const uint32_t* w = reinterpret_cast<const uint32_t*>(input->data);
            for (int i = 0; i < AUDIO_BLOCK_SAMPLES / 2; i++)
                silentIn &= w[i] == 0;
        }
        if (silentIn)
        {
            skipIdleBlock();
            if (input != nullptr)
                release(input);
            return;
        }
    }

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

AUDIO_CORE_RAM bool AudioEffectShortDelay::process(int16_t input[],
                                               int16_t outputLeft[],
                                               int16_t outputRight[],
                                               int sampleCount)
{
    if (noRefresh == true)
    {
        return false;
    }

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
    if (pingpong != heardPingpong)
    {
        // the other set of lines starts to be heard: it counts as cleared (see validSamples); the filters start clean, too
        heardPingpong = pingpong;
        validSamples = 0;
        silentSamplesCount = 0;
        bandpassLeft.clear();
        bandpassRight.clear();
        bandpassMono.clear();
    }
    const uint32_t time = timeInSamples;
    const uint32_t warm = validSamples < time ? time - validSamples : 0; // the first `warm` samples of this block read a line not yet written

    // plan 027: the lines are in SDRAM (single accesses cost 10 .. 20 cycles each). A block reads one run of the line `time` samples back and
    // writes one run at the head, so both runs go through SRAM copies (word bursts) when they cannot touch each other.
    const bool tiled = sampleCount <= kTileMax && time >= (uint32_t)sampleCount + 4 && time + (uint32_t)sampleCount + 4 <= MAX_DELAYLINE_LENGTH &&
                       (((uintptr_t)pingpongDelaylineLeft | (uintptr_t)pingpongDelaylineRight | (uintptr_t)monoDelayline) & 3u) == 0;
    if (pingpong)
    {
        if (tiled)
            runLines<true, true>(input, outputLeft, outputRight, sampleCount, time, warm, filterOn, mixPrev, mixCur, bp);
        else
            runLines<true, false>(input, outputLeft, outputRight, sampleCount, time, warm, filterOn, mixPrev, mixCur, bp);
    }
    else
    {
        if (tiled)
            runLines<false, true>(input, outputLeft, outputRight, sampleCount, time, warm, filterOn, mixPrev, mixCur, bp);
        else
            runLines<false, false>(input, outputLeft, outputRight, sampleCount, time, warm, filterOn, mixPrev, mixCur, bp);
    }

    validSamples = validSamples + sampleCount < MAX_DELAYLINE_LENGTH ? validSamples + sampleCount : MAX_DELAYLINE_LENGTH;

    if (filterOn)
    {
        seqfx::svfFlush(bandpassLeft);
        seqfx::svfFlush(bandpassRight);
        seqfx::svfFlush(bandpassMono);
    }

    return true;
}

// One block of the heard lines. PP = ping-pong (left + right lines), otherwise the mono line. Same arithmetic as before plan 009 P#4,
// per heard line; the lines that are not heard are neither read nor written.
// A run of `len` samples of a line from index `a` (wraps at the end of the line), copied to / from SRAM. a and len are even.
static AUDIO_CORE_RAM void delayRun(int16_t* sram, int16_t* line, uint32_t a, uint32_t len, bool toSram)
{
    const uint32_t first = a + len <= MAX_DELAYLINE_LENGTH ? len : MAX_DELAYLINE_LENGTH - a;
    if (toSram)
    {
        memcpy(sram, line + a, first * sizeof(int16_t));
        if (first < len)
            memcpy(sram + first, line, (len - first) * sizeof(int16_t));
    }
    else
    {
        memcpy(line + a, sram, first * sizeof(int16_t));
        if (first < len)
            memcpy(line, sram + first, (len - first) * sizeof(int16_t));
    }
}

template<bool PP, bool TL>
AUDIO_CORE_RAM void AudioEffectShortDelay::runLines(const int16_t* input,
                                                int16_t* outputLeft,
                                                int16_t* outputRight,
                                                int sampleCount,
                                                uint32_t time,
                                                uint32_t warm,
                                                bool filterOn,
                                                float mixPrev,
                                                float mixCur,
                                                const seqfx::SvfCoef& bp)
{
    int16_t* const lineLeft0 = pingpongDelaylineLeft;
    int16_t* const lineRight0 = pingpongDelaylineRight;
    int16_t* const lineMono0 = monoDelayline;
    // plan 027 (TL): SRAM copies of the read run (index sample + readOff) and the write run (sample + writeOff) of each heard line
    alignas(4) int16_t rdA[TL ? kTileMax + 4 : 1], rdB[(TL && PP) ? kTileMax + 4 : 1], wrA[TL ? kTileMax + 4 : 1], wrB[(TL && PP) ? kTileMax + 4 : 1];
    uint32_t rdStart = 0, wrStart = 0, rdLen = 0, wrLen = 0;
    if (TL)
    {
        rdStart = (headIdx - time) & delaylineLengthMask;
        wrStart = headIdx & delaylineLengthMask;
        rdLen = ((rdStart & 1u) + (uint32_t)sampleCount + 1u) & ~1u;
        wrLen = ((wrStart & 1u) + (uint32_t)sampleCount + 1u) & ~1u;
        rdStart &= ~1u;
        wrStart &= ~1u;
        // PP: A = left line, B = right line; mono: A = the mono line
        delayRun(rdA, PP ? lineLeft0 : lineMono0, rdStart, rdLen, true);
        delayRun(wrA, PP ? lineLeft0 : lineMono0, wrStart, wrLen, true);
        if (PP)
        {
            delayRun(rdB, lineRight0, rdStart, rdLen, true);
            delayRun(wrB, lineRight0, wrStart, wrLen, true);
        }
    }
    const uint32_t rdOff = TL ? (((headIdx - time) & delaylineLengthMask) & 1u) : 0u;
    const uint32_t wrOff = TL ? ((headIdx & delaylineLengthMask) & 1u) : 0u;
    // the lines the loop reads / writes through (an index per sample: the line index, or the offset into the SRAM copy)
    int16_t* const rdLeft = TL ? rdA : lineLeft0;
    int16_t* const rdRight = TL ? rdB : lineRight0;
    int16_t* const rdMono = TL ? rdA : lineMono0;
    int16_t* const wrLeft = TL ? wrA : lineLeft0;
    int16_t* const wrRight = TL ? wrB : lineRight0;
    int16_t* const wrMono = TL ? wrA : lineMono0;
    const int32_t fb = feedback;
    uint32_t head = headIdx;
    uint32_t silent = silentSamplesCount;
    static constexpr int32_t kFeedbackRoundBias = (1 << kFeedbackShift) - 1;

    for (int sample = 0; sample < sampleCount; sample++)
    {
        const uint32_t delayedSampleIdx = TL ? (uint32_t)sample + rdOff : ((head - time) & delaylineLengthMask);
        const bool cold = (uint32_t)sample < warm;
        int32_t filteredSampleRight = 0;
        int32_t filteredSampleLeft = 0;
        int32_t filteredSampleMono = 0;
        if (PP)
        {
            int16_t delayedSampleRight = cold ? 0 : rdRight[delayedSampleIdx];
            int16_t delayedSampleLeft = cold ? 0 : rdLeft[delayedSampleIdx];
            filteredSampleRight = delayedSampleRight;
            filteredSampleLeft = delayedSampleLeft;
            if (filterOn)
            {
                // the mix ramps linearly over the block: engaging / releasing the band never steps the signal
                const float m = seqfx::mixAt(mixPrev, mixCur, sample, sampleCount);
                float xr = (float)delayedSampleRight, xl = (float)delayedSampleLeft;
                filteredSampleRight =
                  signed_saturate_rshift((int32_t)(xr + m * (seqfx::bandpass(bandpassRight, bp, xr) - xr)), 16, 0);
                filteredSampleLeft =
                  signed_saturate_rshift((int32_t)(xl + m * (seqfx::bandpass(bandpassLeft, bp, xl) - xl)), 16, 0);
            }
        }
        else
        {
            int16_t delayedSampleMono = cold ? 0 : rdMono[delayedSampleIdx];
            filteredSampleMono = delayedSampleMono;
            if (filterOn)
            {
                const float m = seqfx::mixAt(mixPrev, mixCur, sample, sampleCount);
                float xm = (float)delayedSampleMono;
                filteredSampleMono =
                  signed_saturate_rshift((int32_t)(xm + m * (seqfx::bandpass(bandpassMono, bp, xm) - xm)), 16, 0);
            }
        }

        int16_t outL = PP ? filteredSampleLeft : filteredSampleMono;
        int16_t outR = PP ? filteredSampleRight : filteredSampleMono;
        *outputLeft++ = outL;
        *outputRight++ = outR;

        int32_t inputShifted = ((*input++) << kFeedbackShift);
        bool isSampleSilent;
        const uint32_t headSampleIdx = TL ? (uint32_t)sample + wrOff : (head & delaylineLengthMask);
        head++;

        // soft limit instead of the hard 16 bit clip: a loud sustained signal at a high feedback compresses instead of crackling
        if (PP)
        {
            int32_t newDelayedSampleLeft = inputShifted + filteredSampleRight * fb;
            int32_t newDelayedSampleRight = filteredSampleLeft * fb;
            int16_t saturatedLeft = (int16_t)signed_saturate_rshift(
              seqfx::softLimit((newDelayedSampleLeft + (newDelayedSampleLeft < 0 ? kFeedbackRoundBias : 0)) >> kFeedbackShift), 16, 0);
            int16_t saturatedRight = (int16_t)signed_saturate_rshift(
              seqfx::softLimit((newDelayedSampleRight + (newDelayedSampleRight < 0 ? kFeedbackRoundBias : 0)) >> kFeedbackShift), 16, 0);
            isSampleSilent = outL == 0 && outR == 0 && saturatedLeft == 0 && saturatedRight == 0;
            wrLeft[headSampleIdx] = saturatedLeft;
            wrRight[headSampleIdx] = saturatedRight;
        }
        else
        {
            int32_t newDelayedSampleMono = inputShifted + filteredSampleMono * fb;
            int16_t saturatedMono = (int16_t)signed_saturate_rshift(
              seqfx::softLimit((newDelayedSampleMono + (newDelayedSampleMono < 0 ? kFeedbackRoundBias : 0)) >> kFeedbackShift), 16, 0);
            isSampleSilent = outL == 0 && saturatedMono == 0;
            wrMono[headSampleIdx] = saturatedMono;
        }

        if (isSampleSilent)
        {
            if (silent < MAX_DELAYLINE_LENGTH)
                silent++;
        }
        else
        {
            silent = 0;
        }
    }

    if (TL)
    {
        delayRun(wrA, PP ? lineLeft0 : lineMono0, wrStart, wrLen, false);
        if (PP)
            delayRun(wrB, lineRight0, wrStart, wrLen, false);
    }
    headIdx = head;
    silentSamplesCount = silent;
}

// An idle block (see update()): the heard lines get a block of zeros, the head moves on, the filters are clean.
AUDIO_CORE_RAM void AudioEffectShortDelay::skipIdleBlock()
{
    const uint32_t start = headIdx & delaylineLengthMask;
    const uint32_t first = start + AUDIO_BLOCK_SAMPLES <= MAX_DELAYLINE_LENGTH ? AUDIO_BLOCK_SAMPLES : MAX_DELAYLINE_LENGTH - start;
    int16_t* lines[2] = { isPingpong ? pingpongDelaylineLeft : monoDelayline, isPingpong ? pingpongDelaylineRight : nullptr };
    for (int k = 0; k < 2; k++)
    {
        if (lines[k] == nullptr)
            continue;
        memset(lines[k] + start, 0, first * sizeof(int16_t));
        if (first < AUDIO_BLOCK_SAMPLES)
            memset(lines[k], 0, (AUDIO_BLOCK_SAMPLES - first) * sizeof(int16_t));
    }
    headIdx += AUDIO_BLOCK_SAMPLES;
    if (silentSamplesCount + AUDIO_BLOCK_SAMPLES < MAX_DELAYLINE_LENGTH)
        silentSamplesCount += AUDIO_BLOCK_SAMPLES;
    else
        silentSamplesCount = MAX_DELAYLINE_LENGTH;
    if (validSamples + AUDIO_BLOCK_SAMPLES < MAX_DELAYLINE_LENGTH)
        validSamples += AUDIO_BLOCK_SAMPLES;
    else
        validSamples = MAX_DELAYLINE_LENGTH;
    bandpassLeft.clear();
    bandpassRight.clear();
    bandpassMono.clear();
}
