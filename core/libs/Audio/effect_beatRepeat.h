// Sequencer tracks mod (plan 021) - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Beat repeat as a stereo master insert: master bus (tracks + returns) -> this stage -> limiter. The DSP is beat_repeat_core.h (host
// tested). The slice ring (128 KiB) is raw EXTERNAL_RAM storage in effect_beatRepeat.cpp (no constructor, no initialiser). Idle (no pad,
// no automatic repeats, no tail) the blocks are passed on untouched: no capture, no copy, no clock read.
#ifndef LIBS_AUDIO_EFFECT_BEATREPEAT_H_
#define LIBS_AUDIO_EFFECT_BEATREPEAT_H_

#include "AudioStream.h"
#include "beat_repeat_core.h"

class AudioEffectBeatRepeat : public AudioStream
{
  public:
    AudioEffectBeatRepeat(void)
      : AudioStream(2, inputQueueArray)
    {
        perfStage = seqcpu::S_BEAT; // plan 022 P1
    }
    void begin(); // engine init: attaches the ring (after the SDRAM is up)
    // where the sequencer clock comes from (set once by the glue): fills the block clock; false = no clock (free run)
    typedef bool (*ClockFn)(beatrepeat::Clock& clk);
    void setClockSource(ClockFn fn) { clockFn = fn; }
    virtual void update(void);

    // main loop side
    beatrepeat::Core& core() { return core_; }
    // CPU of the stage in the audio interrupt, % of the block period (average of ~32 blocks, 0 while bypassed)
    float cpuPercent() const { return cpu; }

  private:
    audio_block_t* inputQueueArray[2];
    beatrepeat::Core core_;
    ClockFn clockFn = nullptr;
    float cpu = 0.0f;
};

#endif /* LIBS_AUDIO_EFFECT_BEATREPEAT_H_ */
