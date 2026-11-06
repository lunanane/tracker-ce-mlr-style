#pragma once

#include "AudioStream.h"

class Synth;

class PolySynth : public AudioStream
{
  public:
    PolySynth(void)
      : AudioStream(0, NULL)
    {
        perfStage = seqcpu::S_SYNTH; // plan 022 P1
    }

    void init(Synth* trackerSynth);
    void setGain(int idx, float gainDB);
    virtual void update(void) override;

    static constexpr int kSend1Offset = 2 * kSynthParts;
    static constexpr int kSend2Offset = 3 * kSynthParts;
    static constexpr int kRightChannelOffset = kSynthParts;

  private:
    Synth* trackerSynth = nullptr;
    bool isBlocked = true;
    float gainDB[kSynthParts] = {};
};
