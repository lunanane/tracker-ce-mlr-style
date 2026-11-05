#ifndef LIBS_AUDIO_EFFECT_SHORTDELAY_H_
#define LIBS_AUDIO_EFFECT_SHORTDELAY_H_
#include "AudioStream.h"
#include "seqFxLogic.h"

constexpr uint8_t MAX_SHORT_DELAY_VOICES = 12;
constexpr uint16_t MAX_SHORT_DELAY_TIME = 3500;
constexpr uint32_t MAX_DELAYLINE_LENGTH = 262144;

class AudioEffectShortDelay : public AudioStream
{
  public:
    AudioEffectShortDelay(void)
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
    // plan 009 P#4: lets a host test read the bookkeeping
    uint32_t headIndexForTest() const { return headIdx; }

  private:
    // plan 009 P#4: one loop per mode, only the lines that are heard are read / updated (ping-pong: left + right, otherwise mono)
    template<bool PP>
    void runLines(const int16_t* input, int16_t* outputLeft, int16_t* outputRight, int sampleCount, uint32_t time, uint32_t warm,
                  bool filterOn, float mixPrev, float mixCur, const seqfx::SvfCoef& bp);
    void skipIdleBlock();

    static constexpr int kFeedbackShift = 8;
    static constexpr float kMaxFeedback = seqfx::kDelayFeedbackCap / 100.0f; // plan 007: the cap (0.88), shared with the fader page
    static constexpr uint32_t delaylineLengthMask = MAX_DELAYLINE_LENGTH - 1;
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
    uint32_t silentSamplesCount = MAX_DELAYLINE_LENGTH;
    // plan 009 P#4: the mode the lines were last updated in, and how many samples have been written to the heard lines since that mode
    // began. A line that was not heard keeps its old content; it is treated as cleared (reads give 0) until it has been written for a
    // whole delay time, so switching ping-pong <-> mono behaves like clearing the other lines, without clearing 1 MB of SDRAM.
    bool heardPingpong = false;
    uint32_t validSamples = MAX_DELAYLINE_LENGTH;
};

#endif /* LIBS_AUDIO_EFFECT_SHORTDELAY_H_ */
