// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// CLIPS layer of the Session view = the clip launcher (plan 005 R2). Rows are patterns (row n = pattern n), moved by hand; the pads
// cover an 8 x 4 window of the 16 x 255 clip grid: columns 1-8 clips, column 10 the scene of the row.
//   screen button 1 (hold) + wheel = play mode of the focused clip (Shift: the whole row)   2 (tap) = launch quantise
//   3 (hold) + pad = stop that track / stop all (scene pad)                                  4 = BACK TO SONG while LIVE
#pragma once
#include "display.h"
#include "seqSession/clipGrid.h"
#include "seqSession/sessionLayer.h"
#include "seqSessionLogic.h"

namespace seqsession {

class ClipsLayer : public SessionLayer
{
  public:
    const char* name() const override { return "CLIPS"; }
    void enter(uint8_t firstTrack) override;
    void leave() override;
    void setGroup(uint8_t firstTrack) override;
    void pad(uint8_t col, uint8_t row, bool down) override;
    void leds(uint8_t out[48]) override;
    void update(uint32_t nowMs) override { nowMs_ = nowMs; }
    bool jog(int16_t value, bool shift) override;
    void info(char lines[4][48]) override { (void)lines; }

    void createControls() override;
    void destroyControls() override;
    bool refresh(bool force) override;
    bool button(uint8_t i, bool down) override;
    bool buttonLabel(uint8_t i, char out[12]) override;
    bool arrow(ArrowDir dir, bool shift) override;
    bool drawsOwnScreen() const override { return true; }

  private:
    void build(clipgrid::View& v, clipgrid::Marks& m);
    void showControls(bool on);
    void say(uint8_t msg);
    bool hasNotes(uint8_t pattern, uint8_t track) const;

    uint8_t first_ = 0;       // first track of the window (0 or 8)
    Window window_;           // first pattern of the 4 pad rows
    uint8_t top_ = 1;         // first pattern shown in the grid
    uint8_t focusPattern_ = 0;
    uint8_t focusTrack_ = 0;
    bool modeHeld_ = false;   // screen button 1
    bool stopHeld_ = false;   // screen button 3
    bool dirty_ = true;
    uint32_t nowMs_ = 0;
    uint32_t lastBuild_ = 0;
    uint32_t lastMarks_ = 0;
    uint32_t msgUntil_ = 0;
    uint8_t msg_ = clipgrid::MSG_NONE;

    hControl grid_ = -1;
    hControl marks_ = -1;
    clipgrid::View view_;
    clipgrid::Marks marksData_;
};

} // namespace seqsession
