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

    // plan 013 M (master amplitude view): peak |sample| of the blocks this stage sent on since the last call, 0 .. 32768. Measured only
    // while the tap is on (one pass over the block); read from the main loop, tear tolerant.
    void setPeakTap(bool on) { peakTap = on; }
    uint32_t takePeak() { return __atomic_exchange_n(&outPeak, (uint32_t)0, __ATOMIC_ACQ_REL); }

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
    volatile bool peakTap = false;
    volatile uint32_t outPeak = 0;
    void tapPeak(const int16_t* d);
};

#endif /* LIBS_AUDIO_EFFECT_LIMITER_H_ */
