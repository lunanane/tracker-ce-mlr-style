// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md). Plinky voice parts: MIT, see PlinkyVoice/plinky_voice.h
// Audio stream of the synth voice engine (plan 009 S): two outputs (left, right), no inputs. Original Tracker only (FEATURE_PLINKY).
#pragma once
#include "AudioStream.h"
#include "PlinkyVoice/plinky_voice.h"

class AudioSynthPlinky : public AudioStream
{
  public:
    AudioSynthPlinky() : AudioStream(0, NULL) { perfStage = seqcpu::S_PLINKY; } // plan 022 P1
    virtual void update(void);

    plinkyvoice::Engine& engine() { return eng; }

    // CPU used by update() in percent of the 128 sample block period (average of ~32 blocks, and a slowly decaying peak),
    // measured with the DWT cycle counter like the reverb readout
    float cpuPercent() const { return cpuAvg; }
    float cpuPeakPercent() const { return cpuPeak; }

  private:
    plinkyvoice::Engine eng;
    float cpuAvg = 0.0f, cpuPeak = 0.0f;
};
