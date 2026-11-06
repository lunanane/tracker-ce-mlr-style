// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// CUT (MLR) page as a Session layer: adapts the free functions of cutLayer.h to seqsession::SessionLayer
// and owns the lane screen (laneView.*, plan 005 R4, plan 016 A). Two SLOTS (A = pad rows 1-2, B = rows 3-4) each hold one track.
// Screen buttons: 1 / 2 = Assign slot 1 / 2 (the slot's rows become a track picker), 3 = Help (pad legend on the screen), 4 = BeatRpt (plan 021: the pads
// and the screen switch to the beat repeat controls, MLR keeps playing), 5 = jump
// quantise (Q off / step / beat / bar), 6 = SYNC ALL (the layer's own button kButtonLayer); 7 shows [REC] while a take is armed and
// 8 switches back to CLIPS (the host's, this layer returns false for it). The layer draws its own screen: the host hides its title and
// info lines. The host's group buttons are not used here (the layer takes the buttons 1-4 first), Left / Right do nothing.
#pragma once
#include "cutLayer.h"
#include "laneView.h"
#include "seqCutLogic.h"
#include "seqMlrLogic.h"
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
        (void)firstTrack;
        cutInit();
        laneViewShow(true); // the controls exist since the module start (createControls); only shown here (plan 007 B1)
    }
    void setGroup(uint8_t firstTrack) override
    {
        (void)firstTrack; // the slots hold the tracks, not a group of the host
        cutInit();
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
        snprintf(lines[0], 48, "A: track %d   B: track %d", cutSlotTrack(0) + 1, cutSlotTrack(1) + 1);
        snprintf(lines[1], 48, "Pads 1-16: jump, two = latched loop");
        snprintf(lines[2], 48, "STOP REV RATE RSYNC / GRID SECT REC MUTE");
        snprintf(lines[3], 48, "Help = pad legend   BeatRpt = beat repeat");
    }

    // ---- own screen ----
    void createControls() override { laneViewCreate(); } // hidden until enter()
    void destroyControls() override { laneViewForget(); } // the framework does not call this today (the display frees the controls); laneViewCreate resets the handles anyway
    bool refresh(bool force) override { return laneViewRefresh(force); }
    bool drawsOwnScreen() const override { return true; }
    bool button(uint8_t i, bool down) override
    {
        if (i == seqcut::kButtonAssign1 || i == seqcut::kButtonAssign2)
        {
            if (down)
                cutPickToggle((uint8_t)(i - seqcut::kButtonAssign1)); // the slot's rows become the track picker; again = cancel
            return true;
        }
        if (i == seqcut::kButtonHelp)
        {
            if (down)
                cutLegendToggle();
            return true;
        }
        if (i == seqcut::kScreenButtonBeatRpt)
        {
            if (down)
                cutBrToggle(); // plan 021: the pads and the screen switch between MLR and the beat repeat controls
            return true;
        }
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
        if (i == seqcut::kButtonAssign1 || i == seqcut::kButtonAssign2)
        {
            const bool open = cutPickSlot() == (int8_t)(i - seqcut::kButtonAssign1);
            snprintf(out, 12, open ? "[Assign %d]" : "Assign %d", i - seqcut::kButtonAssign1 + 1);
            return true;
        }
        if (i == seqcut::kButtonHelp)
        {
            snprintf(out, 12, "%s", cutLegendShown() ? "[Help]" : "Help");
            return true;
        }
        if (i == seqcut::kScreenButtonBeatRpt)
        {
            snprintf(out, 12, "%s", cutBrActive() ? "[BeatRpt]" : "BeatRpt");
            return true;
        }
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
        return dir == ARROW_LEFT || dir == ARROW_RIGHT; // no groups of tracks here: the slots pick their tracks
    }
};

} // namespace seqsession
