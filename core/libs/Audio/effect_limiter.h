#ifndef LIBS_AUDIO_EFFECT_LIMITER_H_
#define LIBS_AUDIO_EFFECT_LIMITER_H_

#include "AudioStream.h"

constexpr uint8_t LIMITER_DELAY = 5;

class AudioEffectLimiter : public AudioStream
{
  public:
    AudioEffectLimiter(void)
      : AudioStream(2, inputQueueArray)
    {
    }

    void begin();
    void setThreshold(uint16_t threshold);
    void setAttack(uint16_t attack);
    void setRelease(float release);
    void setSidechainEnable(bool isEnable);
    float getAverageGainChange();
    bool isAvailableGainToGet();
    virtual void update(void);

    void processSidechain();
    void processMainStream();
    void blockUpdate();
    void unblockUpdate();

  private:
    audio_block_t* inputQueueArray[2];
    int16_t inputStorageBuffer[LIMITER_DELAY];
    int16_t outputStorageBuffer[LIMITER_DELAY];
    uint16_t threshold;
    float g = 1.0;
    uint16_t xpeak;
    float attackTime;
    float releaseTime;
    // plan 009 P#5: the 5-sample delays are rings (index of the oldest sample) instead of shift registers moved every sample
    uint8_t inPos = 0;
    uint8_t outPos = 0;
    float averageGain;
    float sumGain;
    bool isAverageGain;
    bool isSideChainEnable;
    bool isBlocked;
};

#endif /* LIBS_AUDIO_EFFECT_LIMITER_H_ */
