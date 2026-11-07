// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Play mode (plan 005 R1): the 4th action-button page of the pattern editor. The stock pattern view stays on screen; the 8 bottom
// labels become the sequencer strip of 8 tracks, the pads play the selected track's sequencer, a hold of a screen button + the
// wheel opens the type list above that cell, holding Shift shows what the pads do. The input router lives in seqPlayHooks.cpp.
#ifndef INTERFACE_SEQPLAY_H_
#define INTERFACE_SEQPLAY_H_

#include <stdint.h>

#include "displayControls.h"
#include "seqPlayLogic.h"
#include "seqNotesViewLogic.h"
#include "seqPlinkyUiLogic.h"
#include "seqParamsLogic.h"
#include "seqRecLogic.h"

// What the overlay control draws (written by the overlay, read by the control on the display refresh).
struct SeqPlayViewData
{
    enum Mode : uint8_t { HIDDEN = 0, LIST = 1, LEGEND = 2, PLINKY = 3, PARAMS = 4 };
    uint8_t mode = HIDDEN;
    uint8_t type = 0; // LEGEND: sequencer type of the selected track
    uint8_t cell = 0; // LIST: strip cell (0..7) the list sits above
    uint8_t sel = 0;  // LIST: highlighted type
    uint8_t cur = 0;  // LIST: the track's current type
    uint8_t page = 0;  // LEGEND (Chord/Arp): visible page (Play / Groove / Feel / Setup)
    uint8_t pon = 0;   // LEGEND (Chord/Arp): parametric groove on
    uint8_t half = 0;  // LEGEND (Mono): steps 1-8 / 9-16
    uint8_t menu = 0;  // LEGEND (Mono): menu view shown
    uint8_t ep = 1;    // LEGEND (Mono): edit page
    uint8_t np = 1;    // LEGEND (Mono): page count
    uint8_t speed = 8; // LEGEND (Mono): speed 1..12
    uint8_t vel = 1;   // LEGEND (Mono): velocity pattern 1..72
    uint8_t tie = 0;   // LEGEND (Mono): tie
    int8_t win = 0;    // LEGEND (Mono): degree window offset
    seqplinky::Setup plSetup;  // LEGEND (Plinky): root / scale / octave / string interval
    uint8_t plMenu = 0;        // LEGEND (Plinky): setup view shown
    seqplinky::Model plinky;   // PLINKY: the parameter overlay
    seqparams::Model prm;      // PARAMS (plan 025): the sequencer settings above the pad legend (second control)
    seqplay::HrSnap hr = {}; // LEGEND (HYPER_RASP types): title, help and cells reported by the script
    uint8_t edge = 0;  // 0 none; 1 / 2: the selected track is on the higher / lower strip page than the one shown (edge marker)
    uint8_t edgeTrack = 0; // that track's number 1..16
    seqnotes::Band notes;  // HIDDEN mode: the notes the selected track's sequencer sounds (v0.3.3)
};

// One custom control for both temporary overlays (type list and pad legend); drawn on rows 12-15 of the pattern view.
class cSeqPlayLegend : public cDisplayControl
{
  public:
    cSeqPlayLegend(strControlProperties* properties = nullptr);
    virtual ~cSeqPlayLegend() {}

    virtual void update();
    virtual void setStyle(uint32_t style) { this->style = style; }
    virtual void setText(const char*) {}
    virtual void setValue(int) {}
    virtual void setColors(const uint32_t*) {}
    virtual void setData(void* data) { view = (const SeqPlayViewData*)data; }

  private:
    const SeqPlayViewData* view = nullptr;
};

class SeqPlayOverlay
{
  public:
    static constexpr uint8_t kNoOwner = 0xFF;
    static constexpr uint8_t kPage = 3; // cPatternEditor::actualActionButtonsPage value of Play mode

    // ---- editor life cycle (cPatternEditor::start / stop / update / refreshActualActionButtonPage) ----
    void createControl(); // cPatternEditor::initDisplayControls, before the pattern controls (lower table index = drawn on top)
    void moduleStart();   // forgets a stale Play state
    void moduleStop();  // the editor leaves the screen: release everything, forget the control
    void update();      // every main loop pass; cheap when Play mode is off
    void bindPage();    // page 3 of the action buttons: labels of the strip (the router handles the buttons)
    bool active() const { return active_; }
    void snapToSelected(); // show the selected track's block of four in the pattern view

    // ---- Play on / off (cPatternEditor::seqPlayEnter / seqPlayLeave call these) ----
    void enter();
    void leave();

    // ---- input router (seqHooks.h) ----
    bool button(uint8_t button, uint8_t state);
    bool pad(uint8_t pad, uint8_t state, uint8_t velo);
    bool pot(uint8_t pot, int16_t value);
    void panic(); // transport Stop (plan 005 M6): end a recording, release held pads, cancel the type list

    uint8_t stripPage() const { return stripPage_; }
    bool plinkyOpen() const { return plinkyOpen_; }

  private:
    bool routable() const;
    uint8_t selected() const;
    void selectTrack(uint8_t track);
    void screenButton(uint8_t index, uint8_t state);
    void recPressed();
    void jog(int16_t value);
    void releaseAllPads();
    void applyPlan(seqplay::Event e);
    void closeList(bool apply);
    void refreshStrip(bool force);
    void refreshPads();
    void refreshView();
    void setMessage(const char* text);
    void plinkyToggle();
    void plinkyNav(uint8_t button);
    void plinkyJog(int16_t value);
    void plinkyDefault();
    void plinkyClose();
    // plan 025: the sequencer settings page above the pad legend
    bool paramsOpen() const;
    void paramsNav(uint8_t button);
    void paramsJog(int16_t value);
    void paramsDefault();
    void paramsPost(uint8_t flat, int value);
    void refreshParams(uint8_t sel);
    void syncPattern();   // v0.3.6 A1: the pattern control is not drawn while the PolySynth view is shown
    bool editKey(uint8_t button, uint8_t state);
    bool paramsKey(uint8_t state);
    void borrowEdit();
    void returnEdit();

    bool active_ = false;
    uint8_t stripPage_ = 0;
    uint8_t lastTrack_ = 0xFF;
    int8_t control_ = -1;
    int8_t controlP_ = -1;          // plan 025: second cSeqPlayLegend control, mode PARAMS (created after the legend = drawn below it)
    SeqPlayViewData viewP_;
    SeqPlayViewData viewPShown_;
    bool viewPShownValid_ = false;
    seqparams::Cursor prmCur_;
    uint8_t prmTrack_ = 0xFF, prmType_ = 0xFF;
    int8_t prmVals_[seqparams::kMaxParams];
    int8_t prmPendVal_ = 0;         // a value just posted: shown (and the next jog starts from it) until the audio side has taken it
    uint8_t prmPendIdx_ = 0xFF, prmPendTrack_ = 0xFF;
    uint32_t prmPendUntil_ = 0;
    bool prmShown_ = false;         // the settings page is on screen (the pattern control is hidden then)
    bool delTaken_ = false;         // a Delete press was taken as "reset the value" (its hold / release are too)
    seqplay::ScreenHold hold_; // screen buttons whose press the overlay took (their release is taken too), the list's button
    seqplay::TypeList list_;
    seqplay::LegendGate legend_;
    uint8_t padOwner_[48];
    uint8_t ledShown_[48];
    SeqPlayViewData view_;
    SeqPlayViewData viewShown_;
    bool viewShownValid_ = false;
    uint32_t cellKey_[8];
    bool stripKeyValid_ = false;
    seqplay::LateRelease late_;
    seqrecui::RecToggle recKey_; // the Rec key: a toggle on the press (hold / release mean nothing), plan 008 A
    bool recStopPending_ = false; // a Rec stop that did not fit into the command queue: retried every pass
    const char* msg_ = nullptr;
    uint32_t msgUntil_ = 0;
    uint32_t ledTime_ = 0;
    seqplay::EditKeys keys_;       // v0.3.2: stock editing keys (Note / Instr / FX1 / FX2 / Copy / Insert / Delete)
    bool editBorrowed_ = false;    // the stock edit mode is on for a held editing key (Rec is the take key in Play mode)
    bool plinkyOpen_ = false;      // the Plinky parameter overlay (plan 010 B)
    bool patternHidden_ = false;   // v0.3.6 A1: we hid the pattern control (view switch to the PolySynth page, or the frame budget)
    bool budgetHidden_ = false;    // ... because the shown controls exceeded the display list budget
    seqplay::LedGuard ledGuard_;   // v0.3.6 A4
    bool ledWasRoutable_ = true;
    bool shiftSeen_ = false;       // Shift was down at the last pass (it closes an open type list)
    bool paramsTaken_ = false;     // the Instrument Parameters press was ours (its hold / release are too)
    seqplinky::Cursor plinkyCur_;
    uint32_t plinkyTime_ = 0;
    uint8_t plinkySel_ = 0xFF;     // the track whose instance the view shows (plan 019)
    uint32_t notesTime_ = 0;
    seqnotes::Band notesBand_;
    char ownerMsg_[24];
    char line1_[8][12];
    char line2_[8][12];
};

extern SeqPlayOverlay seqPlayOverlay;

#endif /* INTERFACE_SEQPLAY_H_ */
