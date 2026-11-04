// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// CUT (MLR) page as a Session layer: adapts the free functions of cutLayer.h to seqsession::SessionLayer
// and owns the lane screen (laneView.*, plan 005 R4). Group of 4 tracks (screen buttons 1-4 = tracks 1-4 / 5-8 / 9-12 / 13-16,
// Left / Right page the groups), all 12 pad columns belong to the layer. Screen button 5 = jump quantise (Q off / step / beat / bar), 6 = SYNC ALL (the layer's own button
// kButtonLayer); 7 shows [REC] while a take is armed (otherwise an empty gap) and 8 switches back to CLIPS (the host's, this layer returns false for it). The layer draws its own screen: the host hides its title and info lines.
#pragma once
#include "cutLayer.h"
#include "laneView.h"
#include "seqCutLogic.h"
#include "seqSessionLogic.h"
#include "sessionLayer.h"
#include <stdio.h>

namespace seqsession {

static_assert((int)seqcut::kScreenButtonSyncAll == (int)kButtonLayer, "SYNC ALL sits on the layer's own screen button");

class CutSessionLayer : public SessionLayer
{
  public:
    const char* name() const override { return "CUT"; }
    uint8_t groupSize() const override { return 4; }
    void enter(uint8_t firstTrack) override
    {
        setGroup(firstTrack);
        laneViewShow(true); // the controls exist since the module start (createControls); only shown here (plan 007 B1)
    }
    void setGroup(uint8_t firstTrack) override
    {
        first_ = (uint8_t)(firstTrack & 0x0C); // 0, 4, 8, 12 (an 8-track group start rounds down)
        cutSetGroup(first_);
    }
    // Hidden (CLIPS) or Session left: only the pads in the hand are let go and an open take is closed (plan 005 M5). Jams, loops,
    // recorded playhead loops, reverse, rate and STOP of the tracks keep running; only SYNC ALL or transport Stop end them.
    void leave() override
    {
        cutReleaseAll();
        laneViewShow(false); // hidden, never destroyed: a destroyed control with a queued refresh stalls the display queue
    }
    void pad(uint8_t col, uint8_t row, bool down) override { cutPad(col, row, down, down && cutShiftHeld()); } // Shift + RESYNC = SYNC ALL
    void leds(uint8_t out[48]) override { cutLeds(out); }
    void rec(bool down) override
    {
        if (down)
            cutRecToggle(); // Rec is a toggle: the press arms / closes the takes; the release (and the hold events the host drops) mean nothing
    }
    void info(char lines[4][48]) override
    {
        snprintf(lines[0], 48, "Rows = tracks %d-%d", first_ + 1, first_ + 4);
        snprintf(lines[1], 48, "Pads 1-8: jump, two held: loop");
        snprintf(lines[2], 48, "9 STOP  10 REV  11 RATE  12 SYNC");
        snprintf(lines[3], 48, "Rec on, jam, Rec off = record a loop");
    }

    // ---- own screen ----
    void createControls() override { laneViewCreate(); } // hidden until enter()
    void destroyControls() override { laneViewForget(); } // the framework does not call this today (the display frees the controls); laneViewCreate resets the handles anyway
    bool refresh(bool force) override { return laneViewRefresh(force, first_); }
    bool drawsOwnScreen() const override { return true; }
    bool button(uint8_t i, bool down) override
    {
        if (i == seqcut::kScreenButtonQuant)
        {
            if (down)
                cutQuantCycle(); // jump quantise: off -> step -> beat -> bar
            return true;
        }
        if (i != seqcut::kScreenButtonSyncAll)
            return false;
        if (down)
            cutSyncAll();
        return true;
    }
    bool buttonLabel(uint8_t i, char out[12]) override
    {
        if (i == seqcut::kScreenButtonQuant)
        {
            snprintf(out, 12, "Q %s", seqcut::quantName(cutQuantMode())); // the value is the label
            return true;
        }
        if (i == seqcut::kScreenButtonRec)
        {
            snprintf(out, 12, "%s", cutRecHeld() ? "[REC]" : ""); // a take is armed: the first pad of a row starts its take
            return true;
        }
        if (i != seqcut::kScreenButtonSyncAll)
            return false;
        snprintf(out, 12, "%s", laneViewAnyOffGrid() ? "[SYNC ALL]" : "SYNC ALL"); // brackets: some track is off the grid
        return true;
    }
    bool arrow(ArrowDir dir, bool shift) override
    {
        (void)shift;
        if (dir == ARROW_LEFT)
        {
            if (first_ >= 4)
                laneViewRequestGroup((uint8_t)(first_ - 4));
            return true;
        }
        if (dir == ARROW_RIGHT)
        {
            if (first_ + 4 < seqcut::kCutTracks)
                laneViewRequestGroup((uint8_t)(first_ + 4));
            return true;
        }
        return false;
    }

  private:
    uint8_t first_ = 0;
};

} // namespace seqsession
