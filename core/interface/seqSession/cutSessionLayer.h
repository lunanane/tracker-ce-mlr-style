// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// CUT (MLR) page as a Session layer: adapts the free functions of cutLayer.h to seqsession::SessionLayer.
// Group of 4 tracks (screen buttons 1-4 = tracks 1-4 / 5-8 / 9-12 / 13-16), all 12 pad columns belong to the layer.
#pragma once
#include "cutLayer.h"
#include "sessionLayer.h"
#include <stdio.h>

namespace seqsession {

class CutSessionLayer : public SessionLayer
{
  public:
    const char* name() const override { return "CUT"; }
    uint8_t groupSize() const override { return 4; }
    void setGroup(uint8_t firstTrack) override
    {
        first_ = (uint8_t)(firstTrack & 0x0C); // 0, 4, 8, 12 (an 8-track group start rounds down)
        cutSetGroup(first_);
    }
    void leave() override { cutReleaseAll(); } // hidden or Session left: loops stop, a recording ends
    void pad(uint8_t col, uint8_t row, bool down) override { cutPad(col, row, down); }
    void leds(uint8_t out[48]) override { cutLeds(out); }
    void rec(bool down) override { cutRec(down); }
    void info(char lines[4][48]) override
    {
        snprintf(lines[0], 48, "Rows = tracks %d-%d", first_ + 1, first_ + 4);
        snprintf(lines[1], 48, "Pads 1-8: jump, two held: loop");
        snprintf(lines[2], 48, "9 STOP  10 REV  11 RATE  12 SYNC");
        snprintf(lines[3], 48, "Hold Rec while jamming = record a loop");
    }

  private:
    uint8_t first_ = 0;
};

} // namespace seqsession
