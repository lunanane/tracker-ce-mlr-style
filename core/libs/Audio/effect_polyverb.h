// Copyright 2020 Wojciech Jakóbczyk (jakobczyk.woj@gmail.com)

#include "AudioStream.h"

#include "reverb.h"
#include "ring_reverb.h"
#include "seqFxLogic.h"

class AudioEffectPolyverb : public AudioStream
{
  public:
    AudioEffectPolyverb();
    ~AudioEffectPolyverb();
    virtual void update(void);

    // Reset/clear buffers
    void reset();

    // Does reverb still produce sound
    bool isActive();

    // Set threshold of activity check (sample value)
    void setActiveThreshold(float threshold);

    // Wet/dry ratio, 1.0 full wet
    void setAmount(float amount) { reverb.SetAmount(amount); }

    // Decay time, 1.0 infinite
    void setTime(float time)
    {
        reverb.SetTime(time);
        size_ = time;
        ringDirty = true;
    }

    // High frequency damping: the low-pass coefficient (1 - the damp control), 0-1
    void setDamp(float damp)
    {
        reverb.SetLP(damp);
        damp_ = 1.0f - damp;
        ringDirty = true;
    }

    // Delay lines diffusion ratio, 0-1
    void SetDiffusion(float diffusion)
    {
        reverb.SetDiffusion(diffusion);
        diffusion_ = diffusion;
        ringDirty = true;
    }

    // Predelay length, 0-1
    void SetPredelayLength(float length)
    {
        reverb.SetPredelayLength(length);
        predelay_ = length;
        ringDirty = true;
    }

    // Plan 007 F3: seqfx::RT_STOCK / RT_PLINKY. The switch is made in the audio interrupt: fade out (one block), the buffers of
    // the new type are cleared in small pieces while the return is silent, fade in (one block).
    void setType(uint8_t type) { requestedType = type < seqfx::RT_COUNT ? type : (uint8_t)seqfx::RT_STOCK; }
    uint8_t getType() const { return typeSwitch.active; }

    // Plan 007 F2: highpass (low cut) of the reverb input, seqfx::hpHz index 0 = off. Smoothed, no clicks.
    void setHighpass(uint8_t idx);

    void blockUpdate() { isBlockedUpdate = true; }

    void unblockUpdate() { isBlockedUpdate = false; }

  private:
    void runType(int16_t* l, int16_t* r);
    bool clearStep();

    audio_block_t* inputQueueArray[2];
    Reverb reverb;
    ringverb::RingReverb ring;
    seqfx::FilterControl hpControl;
    seqfx::SvfState hpLeft, hpRight;
    float size_ = 0.7f, damp_ = 0.5f, diffusion_ = 0.68f, predelay_ = 0.5f;
    volatile bool ringDirty = true;
    volatile uint8_t requestedType = seqfx::RT_STOCK;
    seqfx::TypeSwitch typeSwitch;
    uint16_t* buffer;
    uint16_t* buffer2;
    bool isBlockedUpdate;
};
