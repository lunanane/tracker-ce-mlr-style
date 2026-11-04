// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// CLIPS layer of the Session view: launch one track's part of a pattern, launch scenes, stop, quantise.
#pragma once
#include "seqSession/sessionLayer.h"
#include "seqSessionLogic.h"

namespace seqsession {

class ClipsLayer : public SessionLayer
{
  public:
    const char* name() const override { return "CLIPS"; }
    void enter(uint8_t firstTrack) override;
    void leave() override { stopHeld_ = false; }
    void setGroup(uint8_t firstTrack) override { first_ = firstTrack; }
    void pad(uint8_t col, uint8_t row, bool down) override;
    void leds(uint8_t out[48]) override;
    void update(uint32_t nowMs) override { nowMs_ = nowMs; }
    bool jog(int16_t value, bool shift) override;
    void info(char lines[4][48]) override;

  private:
    uint8_t first_ = 0;
    Window window_;
    bool stopHeld_ = false;
    uint32_t nowMs_ = 0;
    uint32_t msgUntil_ = 0;
    const char* msg_ = nullptr;
};

} // namespace seqsession
