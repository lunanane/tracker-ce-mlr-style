// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Play mode as page 3 of the pattern editor's action buttons (plan 005 R1). The editor keeps running underneath: pattern view,
// playhead, title bar, mutes (Shift), Up / Down, Play. This file owns the strip (the 8 bottom labels), the sequencer type list,
// the pad legend, the pad LEDs and the routing of the pads / Rec / wheel to the selected track's sequencer.
#include "seqPlay/seqPlay.h"

#include <stdio.h>
#include <string.h>

#include "FT812.h"
#include "core/interfaceDefs.h"
#include "core/interfaceMenuPopup.h"
#include "display.h"
#include "mtPadsBacklight.h"
#include "mtSequencer.h"
#include "patternEditor/patternEditor.h"
#include "seqTracks.h"

SeqPlayOverlay seqPlayOverlay;

namespace {
cPatternEditor& PE() { return patternEditor; }
constexpr uint32_t kMessageMs = 2000;
constexpr uint32_t kLedMs = 30;
constexpr uint32_t kStripMs = 50;
} // namespace

//##############################################################################################
//###############################        LIFE CYCLE            #################################
//##############################################################################################
void SeqPlayOverlay::createControl()
{
    strControlProperties prop;
    prop.text = nullptr;
    prop.style = 0; // hidden until used
    prop.x = 0;
    prop.y = 0;
    prop.w = 800;
    prop.h = 480;
    prop.data = &view_;
    view_.mode = SeqPlayViewData::HIDDEN;
    viewShownValid_ = false;
    control_ = display.createControl<cSeqPlayLegend>(&prop);
}

void SeqPlayOverlay::moduleStart()
{
    // the editor starts again (module switch, file manager result): Play mode never survives that
    if (active_)
        leave();
}

void SeqPlayOverlay::moduleStop()
{
    leave();
    control_ = -1; // the interface destroys the module's controls right after stop()
    viewShownValid_ = false;
}

void SeqPlayOverlay::enter()
{
    if (active_)
        return;
    active_ = true;
    if (sequencer.isRec())
        sequencer.recEnable(false);
    PE().editMode = 0; // Rec is the sequencer's Rec now
    stripPage_ = seqplay::stripPageOfTrack(selected());
    hold_.clear();
    list_.cancel();
    legend_ = seqplay::LegendGate();
    for (int i = 0; i < 48; i++)
    {
        padOwner_[i] = kNoOwner;
        ledShown_[i] = 0xFF;
    }
    stripKeyValid_ = false;
    late_ = seqplay::LateRelease();
    recKey_.reset();
    recStopPending_ = false;
    msg_ = nullptr;
    lastTrack_ = selected();
    ledTime_ = 0;
    padsBacklight.clearGlobalPads(1, 1, 1); // the stock pad lights give way to the sequencer's
    view_.mode = SeqPlayViewData::HIDDEN;
    viewShownValid_ = false;
}

void SeqPlayOverlay::leave()
{
    if (!active_)
        return;
    // a recording ends with Play mode (punch-in would otherwise keep clearing the track while the user edits)
    applyPlan(seqplay::EV_LEAVE);
    recKey_.reset();
    recStopPending_ = false;
    active_ = false;
    view_.mode = SeqPlayViewData::HIDDEN;
    view_.edge = 0;
    refreshView();
    padsBacklight.clearGlobalPads(1, 1, 1);
    seqTracksViewLeft();
}

void SeqPlayOverlay::panic()
{
    // transport Stop: the whole mod lets go (M6). Pads are released even when Play mode is not on screen any more.
    const bool wasRec = recKey_.on() || seqEngine.recState() != seqrec::Planner::IDLE;
    applyPlan(seqplay::EV_STOP);
    recKey_.reset();
    recStopPending_ = false;
    msg_ = nullptr;
    if (wasRec && active_)
        setMessage("rec: Stop event");
    stripKeyValid_ = false;
}

// The survival table (seqPlayLogic.h planFor): what a leave / Stop / popup does to the overlay's state.
void SeqPlayOverlay::applyPlan(seqplay::Event e)
{
    seqplay::Plan p = seqplay::planFor(e);
    if (p.closeList)
        closeList(false);
    if (p.endRecording && (recKey_.on() || seqEngine.recState() != seqrec::Planner::IDLE))
    {
        if (!seqRecStop())
            recStopPending_ = true; // the queue was full: update() sends it again
        recKey_.reset();
    }
    if (p.releasePads)
        releaseAllPads();
    if (p.clearHold)
        hold_.clear();
}

void SeqPlayOverlay::snapToSelected()
{
    selectTrack(selected());
}

//##############################################################################################
//###############################          HELPERS             #################################
//##############################################################################################
uint8_t SeqPlayOverlay::selected() const
{
    int8_t t = patternEditor.trackerPattern.actualTrack;
    return (uint8_t)(t < 0 ? 0 : (t >= seqplay::kTracks ? seqplay::kTracks - 1 : t));
}

bool SeqPlayOverlay::routable() const
{
    // a popup of the editor (menu) or the fill popup owns the inputs
    return active_ && !mtMenuPopup.isPopupVisible() && patternEditor.fillState == 0;
}

void SeqPlayOverlay::setMessage(const char* text)
{
    msg_ = text;
    msgUntil_ = seqNowMs() + kMessageMs;
    stripKeyValid_ = false;
}

void SeqPlayOverlay::releaseAllPads()
{
    for (int i = 0; i < 48; i++)
    {
        if (padOwner_[i] != kNoOwner)
        {
            seqPadRelease(padOwner_[i], (uint8_t)i);
            padOwner_[i] = kNoOwner;
            late_.released((uint8_t)i); // the finger is still down: its physical release is ours to swallow
        }
    }
}

// Pressing a screen button selects the track and brings its block of four into the pattern view (what Left / Right do by hand in
// stock, but the view pages in blocks, not one track at a time).
void SeqPlayOverlay::selectTrack(uint8_t track)
{
    if (track >= seqplay::kTracks)
        return;
    msg_ = nullptr;
    stripKeyValid_ = false;
    if (track != selected())
    {
        releaseAllPads(); // a chord held on the old track ends with the switch
        for (int i = 0; i < 48; i++)
            ledShown_[i] = 0xFF;
    }
    cPatternEditor& pe = PE();
    uint8_t cols = pe.patternViewMode ? 8 : 4;
    pe.trackerPattern.actualTrack = (int8_t)track;
    pe.trackerPattern.firstVisibleTrack = (int8_t)seqplay::viewFirstTrack(track, cols);
    pe.trackerPattern.selectState = 1; // cursor frame, also in the non-edit mode
    pe.trackerPattern.selectColumn = 0;
    pe.isSelectingNow = 0;
    pe.showFxInfo();
    pe.refreshPattern();
    display.refreshControl(pe.patternControl);
    lastTrack_ = track;
}

void SeqPlayOverlay::closeList(bool apply)
{
    if (!list_.open)
    {
        hold_.listBtn = -1;
        return;
    }
    uint8_t track = seqplay::stripSlotTrack(stripPage_, list_.cell);
    if (apply)
    {
        int type = list_.release(seqEngine.type(track), seqEngine.pendingType(track));
        if (type >= 0)
            seqEngine.postType(track, (uint8_t)type); // at the next bar while the transport runs
    }
    else
    {
        list_.cancel();
    }
    hold_.listBtn = -1;
}

//##############################################################################################
//###############################          INPUT               #################################
//##############################################################################################
void SeqPlayOverlay::screenButton(uint8_t index, uint8_t state)
{
    if (state == buttonPress)
    {
        legend_.screenButton();
        closeList(false);
        selectTrack(seqplay::stripSlotTrack(stripPage_, index));
    }
    else if (state == buttonRelease)
    {
        if (list_.open && hold_.listBtn == (int8_t)index)
            closeList(true); // (the hold state already forgot the button, the list still knows it)
    }
}

void SeqPlayOverlay::jog(int16_t value)
{
    int btn = hold_.wheelButton(); // the open list's button, else the lowest held one
    if (btn < 0)
        return;
    value = interfaceGlobals.invertValForList ? -value : value;
    uint8_t track = seqplay::stripSlotTrack(stripPage_, (uint8_t)btn);
    list_.wheel((uint8_t)btn, seqEngine.type(track), seqEngine.pendingType(track), value);
    if (list_.open)
        hold_.listBtn = (int8_t)btn;
}

// Rec is a toggle (plan 008 A): a press starts a take on the selected track (instant punch-in; Shift + Rec: exactly one loop),
// the next press ends it. Holding the key means nothing: the keys driver sends hold events while it is down, and the release.
void SeqPlayOverlay::recPressed()
{
    msg_ = nullptr;
    stripKeyValid_ = false;
    uint8_t track = selected();
    switch (recKey_.press(seqNowMs(), seqEngine.recState() != seqrec::Planner::IDLE))
    {
        case seqrecui::RecToggle::STOP:
            recStopPending_ = !seqRecStop();
            break;
        case seqrecui::RecToggle::START:
        {
            bool oneLoop = PE().buttons->isButtonPressed(interfaceButtonShift);
            recStopPending_ = false;
            if (!seqRecStart(oneLoop, track))
            {
                recKey_.cancel();
                setMessage(seqRecTrackLaunched(track) ? "on a clip" : seqEngine.type(track) == seqlogic::TYPE_OFF ? "pick a type" : "cannot rec");
            }
            break;
        }
        default: break; // a bounce of the key
    }
}

bool SeqPlayOverlay::button(uint8_t b, uint8_t state)
{
    if (b <= interfaceButton7)
    {
    seqplay::ScreenHold::Kind kind = state == buttonPress ? seqplay::ScreenHold::PRESS :
                                         state == buttonRelease ? seqplay::ScreenHold::RELEASE : seqplay::ScreenHold::HOLD;
        const bool shiftDown = PE().buttons->isButtonPressed(interfaceButtonShift);
        if (kind == seqplay::ScreenHold::PRESS && shiftDown && routable())
            legend_.screenButton(); // a stock mute press: no legend until Shift is released
        // Shift + screen button = the stock mute buttons; a press the overlay took stays the overlay's until its release
        switch (hold_.event(b, kind, routable(), shiftDown))
        {
            case seqplay::ScreenHold::TAKE_PRESS: screenButton(b, buttonPress); return true;
            case seqplay::ScreenHold::TAKE_RELEASE: screenButton(b, buttonRelease); return true;
            case seqplay::ScreenHold::TAKE_HOLD: return true;
            default: return false;
        }
    }

    if (b == interfaceButtonShift && state == buttonPress && active_)
    {
        // the stock mute buttons follow the cursor track's group: the strip shows the same group
        stripPage_ = seqplay::stripPageOfTrack(selected());
        stripKeyValid_ = false;
        return false;
    }

    if (!routable())
        return false;

    switch (b)
    {
        case interfaceButtonPlay:
            // Rec + Play is the stock live-rec arm; Rec belongs to the sequencer here, so it is plain play / stop
            if (!PE().buttons->isButtonPressed(interfaceButtonRec))
                return false;
            if (state == buttonPress)
            {
                if (sequencer.getSeqState() == Sequencer::SEQ_STATE_STOP)
                    sequencer.playPattern();
                else
                    sequencer.stop();
            }
            return true;
        case interfaceButtonLeft:
        case interfaceButtonRight:
            if (PE().buttons->isButtonPressed(interfaceButtonPattern))
                return false; // Pattern + arrows = the stock pattern queue / change
            if (state == buttonPress)
            {
                closeList(false);
                stripPage_ = seqplay::stripPageForArrow(b == interfaceButtonRight);
                stripKeyValid_ = false;
            }
            return true;
        case interfaceButtonRec:
            if (seqrecui::isToggle(state))
                recPressed();
            return true; // hold events and the release are the sequencer's too, they do nothing
        case interfaceButtonNote:
        case interfaceButtonInstr:
        case interfaceButtonFx1:
        case interfaceButtonFx2:
        case interfaceButtonCopy:
        case interfaceButtonInsert:
        case interfaceButtonDelete:
            return true; // no popups or edits over the strip
        default: return false;
    }
}

// pads that went to the stock editor (they were pressed before Play mode, or while it did not own them): their release goes there too
static uint64_t s_stockPads = 0;

bool SeqPlayOverlay::pad(uint8_t pad, uint8_t state, uint8_t)
{
    if (pad >= 48)
        return false;
    const uint64_t bit = (uint64_t)1 << pad;

    if (state == 0)
    {
        if (padOwner_[pad] != kNoOwner)
        {
            seqPadRelease(padOwner_[pad], pad);
            padOwner_[pad] = kNoOwner;
            return true;
        }
        if (late_.swallow(pad))
            return true; // released earlier by leave / track switch / Stop
        if (s_stockPads & bit)
        {
            s_stockPads &= ~bit;
            return false;
        }
        return routable();
    }

    if (state == 1)
        late_.pressed(pad);
    if (state == 1 && !routable())
    {
        s_stockPads |= bit;
        return false;
    }
    if (!routable())
        return false;

    if (state == 1)
    {
        uint8_t track = selected();
        if (seqEngine.type(track) == seqlogic::TYPE_OFF || padOwner_[pad] != kNoOwner)
            return true; // an Off track has no pads
        if (seqPadPress(track, pad)) // a press may be dropped when the queue is full, a release never
            padOwner_[pad] = track;
    }
    return true; // press, hold
}

bool SeqPlayOverlay::pot(uint8_t pot, int16_t value)
{
    if (pot != interfacePot0 || !routable() || hold_.mask == 0 || PE().buttons->isButtonPressed(interfaceButtonShift))
        return false; // without a held screen button (or with Shift) the wheel is the stock wheel
    jog(value);
    return true;
}

//##############################################################################################
//###############################          SCREEN              #################################
//##############################################################################################
void SeqPlayOverlay::bindPage()
{
    cPatternEditor& pe = PE();
    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlValue(pe.label[i], 1);
        display.setControlData(pe.label[i], nullptr);
        display.setControlStyle(pe.label[i], controlStyleCenterX | controlStyleCenterY | controlStyleFont3 | controlStyleShow);
        display.setControlStyle2(pe.label[i], controlStyleCenterX | controlStyleCenterY | controlStyleFont3);
    }
    refreshStrip(true);
}

void SeqPlayOverlay::refreshStrip(bool force)
{
    uint8_t sel = selected();
    seqrec::Planner::State rs = seqEngine.recState();
    uint8_t recTrack = seqEngine.recTrack();
    uint8_t hints = seqEngine.recHints();
    uint16_t cut = seqEngine.cutOffGridMask(); // really off the grid: not tracks SYNC ALL has snapped or that only wait for a hand-back retrigger
    if (!stripKeyValid_)
        force = true;

    cPatternEditor& pe = PE();
    const Theme& th = display.getTheme();
    bool any = false;
    for (uint8_t i = 0; i < 8; i++)
    {
        uint8_t t = seqplay::stripSlotTrack(stripPage_, i);
        seqplay::StripCell c;
        c.track = t;
        c.type = seqEngine.type(t);
        c.pending = seqEngine.pendingType(t);
        c.selected = t == sel;
        c.takenOver = seqEngine.takenOver(t);
        c.rec = rs != seqrec::Planner::IDLE && recTrack == t;
        c.launched = seqRecTrackLaunched(t);
        c.cut = (cut >> t) & 1u;
        if (t == recTrack)
            c.hint = seqplay::recHintText(hints & seqrec::HINT_OVERFLOW, hints & seqrec::HINT_NEED_MIDI, hints & seqrec::HINT_REDUCED);
        c.msg = (c.selected && msg_) ? msg_ : nullptr;

        // only the cells whose content changed are rewritten
        uint32_t key = 2166136261u;
        auto mix = [&key](uint32_t v) { key = (key ^ v) * 16777619u; };
        mix(t);
        mix(c.type | (c.pending << 4) | ((uint32_t)c.takenOver << 12) | ((uint32_t)c.rec << 13) | ((uint32_t)c.launched << 14) |
            ((uint32_t)c.cut << 15) | ((uint32_t)c.selected << 16) | ((uint32_t)(c.msg != nullptr) << 17) |
            ((uint32_t)(c.hint ? c.hint[0] : 0) << 18));
        if (!force && key == cellKey_[i])
            continue;
        cellKey_[i] = key;
        any = true;

        seqplay::stripLine1(line1_[i], sizeof(line1_[i]), c);
        seqplay::stripLine2(line2_[i], sizeof(line2_[i]), c);
        const uint32_t* colors;
        switch (seqplay::stripTone(c))
        {
            case seqplay::TONE_REC: colors = th.redButtonLabelsColors; break;
            case seqplay::TONE_SELECTED: colors = th.highlightedButtonLabelsColors; break;
            case seqplay::TONE_ON: colors = th.activeButtonLabelsColors; break;
            default: colors = th.inactiveButtonLabelsColors; break;
        }
        display.setControlText(pe.label[i], line1_[i]);
        display.setControlText2(pe.label[i], line2_[i]);
        display.setControlColors(pe.label[i], colors);
        display.refreshControl(pe.label[i]);
    }
    stripKeyValid_ = true;
    if (any)
        display.synchronizeRefresh();
}

void SeqPlayOverlay::refreshPads()
{
    uint8_t out[48];
    seqEngine.leds(selected(), out);
    for (int i = 0; i < 48; i++)
    {
        uint8_t level = out[i] > 15 ? 15 : out[i];
        uint8_t pwm = (uint8_t)(level * 2);
        if (pwm != ledShown_[i])
        {
            ledShown_[i] = pwm;
            padsBacklight.setBackLayer(pwm > 0 ? 1 : 0, pwm, i);
        }
    }
}

void SeqPlayOverlay::refreshView()
{
    if (control_ < 0)
        return;
    uint8_t sel = selected();
    view_.edge = (active_ && !list_.open) ? seqplay::edgeSide(sel, stripPage_) : 0;
    view_.edgeTrack = (uint8_t)(sel + 1);
    if (active_ && list_.open)
    {
        view_.mode = SeqPlayViewData::LIST;
        view_.cell = list_.cell;
        view_.sel = list_.sel;
        view_.cur = seqEngine.type(seqplay::stripSlotTrack(stripPage_, list_.cell));
    }
    else if (active_ && legend_.visible(seqNowMs()) && seqEngine.type(sel) != seqlogic::TYPE_OFF)
    {
        view_.mode = SeqPlayViewData::LEGEND;
        view_.type = seqEngine.type(sel);
        if (view_.type == seqlogic::TYPE_CHORDARP)
        {
            const seqgen::ChordArp& ca = seqEngine.chordArp(sel);
            view_.page = (uint8_t)ca.getParam(seqgen::ChordArp::P_PAGE);
            view_.pon = (uint8_t)ca.getParam(seqgen::ChordArp::P_PON);
        }
        else if (seqlogic::isHrType(view_.type))
        {
            // HYPER_RASP script: it reports its own legend (title, help, 12 x 4 cells)
            // (inside the engine's critical section: a pending type change may rebuild the script in the audio interrupt)
            seqEngine.withHr(
                sel,
                [](const seqgen::HrScript* hs, void* user) {
                    seqplay::HrSnap& snap = *static_cast<seqplay::HrSnap*>(user);
                    if (hs)
                        seqplay::fillHrSnap(snap, *hs);
                    else
                        memset(&snap, 0, sizeof(snap));
                },
                &view_.hr);
        }
        else
        {
            const seqgen::Mono& mo = seqEngine.mono(sel);
            view_.half = (uint8_t)mo.getParam(seqgen::Mono::P_HALF);
            view_.win = (int8_t)mo.getParam(seqgen::Mono::P_WIN);
            view_.menu = (uint8_t)mo.getParam(seqgen::Mono::P_MENU);
            view_.ep = (uint8_t)mo.getParam(seqgen::Mono::P_EP);
            view_.np = (uint8_t)mo.getParam(seqgen::Mono::P_NP);
            view_.speed = (uint8_t)mo.getParam(seqgen::Mono::P_SPEED);
            view_.vel = (uint8_t)mo.getParam(seqgen::Mono::P_VG);
            view_.tie = (uint8_t)mo.getParam(seqgen::Mono::P_TIE);
        }
    }
    else
    {
        view_.mode = SeqPlayViewData::HIDDEN;
    }

    if (viewShownValid_ && memcmp(&view_, &viewShown_, sizeof(view_)) == 0)
        return;
    bool wasHidden = !viewShownValid_ || (viewShown_.mode == SeqPlayViewData::HIDDEN && viewShown_.edge == 0);
    viewShown_ = view_;
    viewShownValid_ = true;

    if (view_.mode == SeqPlayViewData::HIDDEN && view_.edge == 0)
        display.setControlHide(control_);
    else if (wasHidden)
        display.setControlShow(control_);
    display.refreshControl(control_);
    display.synchronizeRefresh();
}

void SeqPlayOverlay::update()
{
    if (!active_)
        return;
    uint32_t now = seqNowMs();

    // Shift: the legend (after 250 ms) and cancel an open type list
    bool shift = PE().buttons->isButtonPressed(interfaceButtonShift);
    if (shift && !legend_.shift)
        closeList(false);
    if (!routable() && list_.open)
        applyPlan(seqplay::EV_POPUP); // a popup took the inputs: the list closes, pads and recording go on
    legend_.shiftChanged(shift, now);

    // the Rec toggle follows the audio side: a take that ended by itself (loop done, transport stop, track Off) clears it
    if (recStopPending_ && seqRecStop())
        recStopPending_ = false;
    if (recKey_.update(now, seqEngine.recState() != seqrec::Planner::IDLE))
    {
        const char* why = seqrecui::endText(seqEngine.recWhy());
        if (why && !msg_)
            setMessage(why);
        stripKeyValid_ = false;
    }

    // the cursor moved by other means (a reload): the pads follow the visible selection
    if (selected() != lastTrack_)
    {
        releaseAllPads();
        for (int i = 0; i < 48; i++)
            ledShown_[i] = 0xFF;
        lastTrack_ = selected();
        stripKeyValid_ = false;
    }

    if (msg_ && (int32_t)(now - msgUntil_) >= 0)
    {
        msg_ = nullptr;
        stripKeyValid_ = false;
    }

    static uint32_t stripTime = 0;
    if (!shift && (uint32_t)(now - stripTime) >= kStripMs) // under Shift the labels are the stock mute buttons
    {
        stripTime = now;
        refreshStrip(false);
    }

    refreshView();

    if ((uint32_t)(now - ledTime_) >= kLedMs)
    {
        ledTime_ = now;
        refreshPads();
    }
}
