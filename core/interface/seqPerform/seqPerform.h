// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Alt Performance page: four 8-step pad faders (plans/004-perform-faders.md, plan 005 R3). Entered with Shift + the top-row
// Performance button (from the stock page and from other modules); a Performance press leaves to the stock page.
// Dialled-in FX are not saved with the project; since v0.3 they keep sounding when the page is left for ANY other module and end on
// DISENGAGE, UNLATCH or the Stop panic (seqPanicFx). The track selection is the user's last setting (seqFxTrackSel): Stop, disengage,
// module switches, re-entry and project loads never clear it.
#ifndef INTERFACE_SEQPERFORM_H_
#define INTERFACE_SEQPERFORM_H_

#include "core/modulesBase.h"
#include "seqPerform/faderView.h"
#include "seqModLogic.h"
#include "seqPerformLogic.h"

class cSeqPerform : public ModuleBase
{
  public:
    virtual void update();
    virtual void start(uint32_t options);
    virtual void stop();

    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    cSeqPerform() {}
    ~cSeqPerform() {}

    //----------------------------------
    void padEvent(uint8_t pad, uint8_t state);
    void trackButton(uint8_t button);
    void jog(int16_t value);
    void leaveToStock();
    void leaveTo(uint8_t button);
    bool shiftHeld();
    void flushSave();
    void projectLoaded();                 // a project was loaded: nothing of ours is dialled in any more
    void saveSel();                       // plan 010 A5: the engine's track states -> the persistent selection (seqfx.bin)
    void restoreSel();                    // plan 010 A5: the persistent selection -> the engine's track states (no FX is touched)
    void releaseAll();
    void releaseMomentary();
    void refreshScreen();
    void refreshPads();

    // fader logic
    void faderPress(uint8_t row, uint8_t step);
    void faderRelease(uint8_t row);
    void faderDisengage(uint8_t row);
    void releaseRow(uint8_t row);
    void setPage(uint8_t newPage);
    void reapplyActive();
    void syncKnockedOut(uint8_t keepSlot);
    uint8_t rowStep(uint8_t row) const;

    // long-lived state (survives leaving the page, see start())
    void prepareFromElsewhere();          // seqFaderShiftFromElsewhere
    void panic();                         // seqPanicFx (transport Stop)
    void stockEntered();                  // the stock Performance page starts fresh: our kept FX end
    bool keptFx() const { return fxKept; }
    void endEngaged(bool alsoStockRows);  // release every slot / send we hold, latches off

    seqperf::State st;
    uint8_t page = seqperf::PAGE_FX1;
    int16_t fxVal[12];            // value applied to each slot by the faders / stock page (0 = none)
    int16_t stockVal[12];         // value a stock-page pad row had when the page opened
    uint16_t stockOn = 0;         // slots that had a stock pad row selected (bit = slot)
    uint64_t padHeld = 0;         // fader pads currently held (bit = pad)
    bool fxKept = false;          // we left the page with FX dialled in and kept them running
    bool fromElsewhere = false;   // entered by Shift + Performance from another module
    bool fromStock = false;       // entered by Shift + Performance on the stock page (state handed over)
    bool shiftFromEntry = false;  // Shift was still held from the entry press: ignored until its first release
    seqlogic::DirtyGate saveGate; // perform data (stock pad rows) changed: saved after Stop, never while playing
    bool shiftView = false;
    uint8_t leaveKind = seqperf::LEAVE_END;

    uint8_t ledShown[48];

    // display
    hControl label[8];
    hControl bgLabel;
    hControl viewCtrl;

    char buttonText[8][6];
    strFaderView viewData;

    uint32_t shownKey = 0xFFFFFFFF;
    bool screenDirty = true;
};

extern cSeqPerform seqPerform;

#endif /* INTERFACE_SEQPERFORM_H_ */
