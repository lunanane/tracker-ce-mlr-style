// Sequencer tracks mod (plan 016 B) - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Warp of a sample voice: the playback path of the One-shot and Loop modes when the instrument has Warp on. The DSP is
// warp_core.h (host tested); this file connects it to the voice: start-up (play*() call warpPrepare), the block render, the
// stream playhead and the CPU readout. Everything else (envelope, filter, amp ...) is the stock chain behind update().
#include "Audio.h"
#include "mtAudioEngine.h"
#include "play_memory.h"
#include "seqWarp.h"
#include "utility/dspinst.h"

namespace
{
static_assert(seqwarp::kRingFrames == (int32_t)VoiceStreamManager::kRingFrames, "warp read window assumes the stream ring");
static_assert(seqwarp::kLookbehind == (int32_t)VoiceStreamManager::kLookbehindFrames, "warp read window assumes the stream lookbehind");
static_assert(seqwarp::kLoopWrapFrames == (int32_t)VoiceStreamManager::kLoopWrapFrames, "warp read window assumes the loop-wrap cache");

constexpr int kSlots = 8;        // voices (instrumentPlayer[8])
volatile uint8_t s_warpMask = 0; // bit = voice slot that is warping (the cap counts these)
float s_warpCpu[kSlots];         // per voice: % of the block period (average of ~32 blocks)
constexpr float kBlockCycles = 180000000.0f * AUDIO_BLOCK_SAMPLES / 44100.0f;

inline uint32_t cycleCounter()
{
    return *(volatile uint32_t*)0xE0001004; // DWT_CYCCNT
}

inline int popcount8(uint8_t v)
{
    int c = 0;
    for (; v; v = (uint8_t)(v & (v - 1)))
        c++;
    return c;
}
} // namespace

float AudioPlayMemory::warpCpuPercent(uint8_t* voices)
{
    const uint8_t m = s_warpMask;
    float sum = 0.0f;
    for (int i = 0; i < kSlots; i++)
        if (m & (1u << i))
            sum += s_warpCpu[i];
    if (voices)
        *voices = (uint8_t)popcount8(m);
    return sum;
}

void AudioPlayMemory::warpEnd()
{
    warpV.active = 0;
    if (warpCounted)
    {
        warpCounted = 0;
        if (voiceIndex >= 0 && voiceIndex < kSlots)
        {
            s_warpMask = (uint8_t)(s_warpMask & ~(1u << voiceIndex));
            s_warpCpu[voiceIndex] = 0.0f;
        }
    }
}

// Called by playSingleShot / playLoopForward (also Backward, Ping-pong) after the points are known, before playing = 1.
// Warp starts only for a voice slot that is free under the cap (seqwarp::kMaxVoices); otherwise this note plays un-warped.
AUDIO_CORE_FLASH void AudioPlayMemory::warpPrepare(uint8_t instrIdx)
{
    warpEnd();
    if (instrIdx >= seqwarp::kCount || !seqWarpOn(instrIdx) || reverseDirectionFlag || voiceIndex < 0 || voiceIndex >= kSlots)
        return;
    uint8_t mode;
    switch (currentPlayMode)
    {
        case playModeSingleShot: mode = warp::kOnce; break;
        case playModeLoopForward: mode = warp::kLoopFwd; break;
        case playModeLoopBackward: mode = warp::kLoopBwd; break;
        case playModePingpong: mode = warp::kPingPong; break;
        default: return;
    }
    // the cap: only channel 0 of a voice claims a slot (the stereo channel 1 of the Plus follows it)
    const uint8_t bit = (uint8_t)(1u << voiceIndex);
    if (channel == 0)
    {
        if (popcount8((uint8_t)(s_warpMask & ~bit)) >= seqwarp::kMaxVoices)
            return;
    }
    else if (!(s_warpMask & bit))
        return;

    // the ratio belongs to the instrument's own region (start .. end point), not to a forced one (MLR cue, P step FX):
    // a jump keeps the stretch, its position is a position in the warped timeline
    const strInstrument& in = mtProject.instrument[instrIdx];
    const float region = (in.endPoint > in.startPoint)
                           ? (float)(in.endPoint - in.startPoint) / (float)MAX_16BIT * (float)currentSampleLength
                           : 0.0f;
    if (region < 512.0f)
        return;
    float bpm = engine.getTempoBpm();
    if (!(bpm >= 10.0f))
        bpm = mtProject.values.globalTempo;
    const uint8_t bars = seqwarp::resolveBars(seqWarpBars(instrIdx), region, bpm);
    warpK = seqwarp::ratioK(region, bars);

    int32_t endRel = (int32_t)pointsInSamples.end - (int32_t)pointsInSamples.start;
    if (endRel > (int32_t)length)
        endRel = (int32_t)length;
    int32_t l1 = 0, l2 = 0;
    if (mode != warp::kOnce)
    {
        l1 = (int32_t)pointsInSamples.loop1 - (int32_t)pointsInSamples.start;
        l2 = (int32_t)pointsInSamples.loop2 - (int32_t)pointsInSamples.start;
    }
    *(volatile uint32_t*)0xE0001000 |= 1u; // DWT_CTRL.CYCCNTENA (CPU readout; the reverb enables it as well)
    warp::start(warpV, mode, endRel, l1, l2, seqwarp::ratioFor(warpK, bpm), pitchControl);
    if (channel == 0)
    {
        s_warpMask = (uint8_t)(s_warpMask | bit);
        warpCounted = 1;
        s_warpCpu[voiceIndex] = 0.0f;
    }
}

AUDIO_CORE_RAM audio_block_t* AudioPlayMemory::updateWarp()
{
    audio_block_t* block = allocate();
    if (!block)
        return nullptr;
    if (!playing)
    {
        release(block);
        return nullptr;
    }
    const uint32_t t0 = cycleCounter();

    // glide: the stock code steps pitchControl per sample; here per block
    if (constrainsInSamples.pitchSmoothing && pitchSmoothingControlCounter <= constrainsInSamples.pitchSmoothing)
    {
        uint32_t steps = constrainsInSamples.pitchSmoothing - pitchSmoothingControlCounter + 1;
        if (steps > (uint32_t)AUDIO_BLOCK_SAMPLES)
            steps = AUDIO_BLOCK_SAMPLES;
        pitchControl += pitchSmoothingControl * (float)steps;
        pitchSmoothingControlCounter += steps;
        if (pitchControl > notes[MAX_NOTE - 1])
        {
            pitchControl = notes[MAX_NOTE - 1];
            pitchSmoothingControlCounter = constrainsInSamples.pitchSmoothing + 1;
        }
        else if (pitchControl < notes[MIN_NOTE])
        {
            pitchControl = notes[MIN_NOTE];
            pitchSmoothingControlCounter = constrainsInSamples.pitchSmoothing + 1;
        }
    }

    // tempo: the ratio follows the tempo smoothly (a tempo change or ramp never jumps the timeline)
    const float bpm = engine.getTempoBpm();
    if (bpm >= 10.0f)
        warp::follow(warpV, seqwarp::ratioFor(warpK, bpm));

    const int16_t* in = (const int16_t*)next;
    auto rd = [this, in](int32_t rel) -> int32_t { return readSampleAtRel(in, rel); };
    warp::render(warpV, block->data, AUDIO_BLOCK_SAMPLES, pitchControl, rd);

    if (warpV.active)
    {
        if (currentIsStreamed && channel == 0 && voiceIndex >= 0)
        {
            const uint32_t absFrame = pointsInSamples.start + (uint32_t)warp::playhead(warpV);
            const float speed = pitchControl > warpV.ratio ? pitchControl : warpV.ratio;
            voiceStreamManager.updateVoicePlayhead((uint8_t)voiceIndex, absFrame, warpV.tdir, speed);
        }
        if (warpCounted)
        {
            const float pct = (float)(cycleCounter() - t0) * (100.0f / kBlockCycles);
            s_warpCpu[voiceIndex] += (pct - s_warpCpu[voiceIndex]) * (1.0f / 32.0f);
        }
    }
    else
    {
        playing = 0; // the sound ended (end of a one-shot): same as the stock path
        warpEnd();
        if (currentIsStreamed && channel == 0 && voiceIndex >= 0)
            voiceStreamManager.notifyNoteOff((uint8_t)voiceIndex);
    }
    return block;
}
