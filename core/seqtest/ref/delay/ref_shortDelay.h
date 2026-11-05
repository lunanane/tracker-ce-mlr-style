#ifndef REF_SHORTDELAY_H_
#define REF_SHORTDELAY_H_
#include "AudioStream.h"
#include "seqFxLogic.h"

constexpr uint8_t REF_MAX_SHORT_DELAY_VOICES = 12;
constexpr uint16_t REF_MAX_SHORT_DELAY_TIME = 3500;
constexpr uint32_t REF_MAX_DELAYLINE_LENGTH = 262144;

class RefShortDelay : public AudioStream
{
  public:
    RefShortDelay(void)
      : AudioStream(1, inputQueueArray)
    {
        blockUpdate();
    }

    void begin(float feedback,
               uint32_t time,
               bool pingPongEnabled,
               bool syncEnabled,
               uint8_t syncRatioIdx,
               float frequency);
    void setBuffer(int16_t* buffer);
    void setFeedback(float f);
    void setTime(float t);
    int getTime();
    void setRate(uint8_t r);
    void setPingpongEnable(bool pp);
    void setSyncEnable(bool se);
    // Plan 007 F1: the filter in the feedback loop is a bandpass. frequency = centre in Hz (>= seqfx::kOpenHz: out of the signal
    // path, the stock "open" setting), width in octaves. Both move smoothly (block-rate smoothing, no clicks).
    void setFilterFrequency(float frequency);
    void setFilterWidth(float octaves);
    virtual void update(void);
    bool process(int16_t input[], int16_t outputLeft[], int16_t outputRight[], int sampleCount);
    void clear();
    void blockUpdate();
    void unblockUpdate();
    bool isSilent() const
    {
        return silentSamplesCount >= timeInSamples;
    }

  private:
    static constexpr int kFeedbackShift = 8;
    static constexpr float kMaxFeedback = seqfx::kDelayFeedbackCap / 100.0f; // plan 007: the cap (0.88), shared with the fader page
    static constexpr uint32_t delaylineLengthMask = REF_MAX_DELAYLINE_LENGTH - 1;
    int16_t* pingpongDelaylineRight;
    int16_t* pingpongDelaylineLeft;
    int16_t* monoDelayline;
    uint32_t headIdx;
    audio_block_t* inputQueueArray[1];
    uint32_t timeInSamples;
    uint8_t feedback;
    bool noRefresh;
    uint8_t
      isPingpong; // uint8_t allows to use conditionally execution instead conditionally branch
    bool isSync;
    seqfx::FilterControl bandpassControl;
    seqfx::SvfState bandpassRight;
    seqfx::SvfState bandpassLeft;
    seqfx::SvfState bandpassMono;
    float filterWidthOctaves = 1.0f;
    uint32_t silentSamplesCount = REF_MAX_DELAYLINE_LENGTH;
};

#endif /* REF_SHORTDELAY_H_ */
