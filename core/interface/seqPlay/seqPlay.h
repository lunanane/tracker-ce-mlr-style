// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Play mode (plan 005 R1): the 4th action-button page of the pattern editor. The stock pattern view stays on screen; the 8 bottom
// labels become the sequencer strip of 8 tracks, the pads play the selected track's sequencer, a hold of a screen button + the
// wheel opens the type list above that cell, holding Shift shows what the pads do. The input router lives in seqPlayHooks.cpp.
#ifndef INTERFACE_SEQPLAY_H_
#define INTERFACE_SEQPLAY_H_

#include <stdint.h>

#include "displayControls.h"
#include "seqPlayLogic.h"

// What the overlay control draws (written by the overlay, read by the control on the display refresh).
struct SeqPlayViewData
{
    enum Mode : uint8_t { HIDDEN = 0, LIST = 1, LEGEND = 2 };
    uint8_t mode = HIDDEN;
    uint8_t type = 0; // LEGEND: sequencer type of the selected track
    uint8_t cell = 0; // LIST: strip cell (0..7) the list sits above
    uint8_t sel = 0;  // LIST: highlighted type
    uint8_t cur = 0;  // LIST: the track's current type
    uint8_t panel = 0; // LEGEND (Chord/Arp): visible panel
    uint8_t half = 0;  // LEGEND (Mono): steps 1-8 / 9-16
    int8_t win = 0;    // LEGEND (Mono): degree window offset
    uint8_t edge = 0;  // 0 none; 1 / 2: the selected track is on the higher / lower strip page than the one shown (edge marker)
    uint8_t edgeTrack = 0; // that track's number 1..16
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

    bool active_ = false;
    uint8_t stripPage_ = 0;
    uint8_t lastTrack_ = 0xFF;
    int8_t control_ = -1;
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
    const char* msg_ = nullptr;
    uint32_t msgUntil_ = 0;
    uint32_t ledTime_ = 0;
    char line1_[8][12];
    char line2_[8][12];
};

extern SeqPlayOverlay seqPlayOverlay;

#endif /* INTERFACE_SEQPLAY_H_ */
