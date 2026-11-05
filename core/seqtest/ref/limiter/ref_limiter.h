#ifndef REF_LIMITER_H_
#define REF_LIMITER_H_

#include "AudioStream.h"

constexpr uint8_t REF_LIMITER_DELAY = 5;

class RefLimiter : public AudioStream
{
  public:
    RefLimiter(void)
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
    int16_t inputStorageBuffer[REF_LIMITER_DELAY];
    int16_t outputStorageBuffer[REF_LIMITER_DELAY];
    uint16_t threshold;
    float g = 1.0;
    uint16_t xpeak;
    float attackTime;
    float releaseTime;
    uint16_t a;
    float coeff;
    float f;
    float averageGain;
    float sumGain;
    bool isAverageGain;
    bool isSideChainEnable;
    bool isBlocked;
};

#endif /* REF_LIMITER_H_ */
