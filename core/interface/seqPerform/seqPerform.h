// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Alt Performance page: four 8-step pad faders (plans/004-perform-faders.md). Entered / left with a
// Performance tap on the Performance page; the stock Performance page is untouched when never tapped.
#ifndef INTERFACE_SEQPERFORM_H_
#define INTERFACE_SEQPERFORM_H_

#include "core/modulesBase.h"
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
    void releaseAll();
    void refreshScreen();
    void refreshPads();

    // fader logic
    void faderPress(uint8_t row, uint8_t step);
    void faderRelease(uint8_t row);
    void faderReset(uint8_t row);
    void releaseRow(uint8_t row);
    void setPage(uint8_t newPage);
    void reapplyActive();
    void syncKnockedOut(uint8_t keepSlot);
    uint8_t rowStep(uint8_t row) const;

    uint8_t page = seqperf::PAGE_FX1;
    uint8_t fxStep[3][4];         // FX pages: current step per fader (0 = off)
    int16_t fxVal[12];            // value applied to each slot by the faders / stock page (0 = none)
    int16_t stockVal[12];         // value a stock-page pad row had when the page opened
    uint16_t stockOn = 0;         // slots that had a stock pad row selected (bit = slot)
    uint8_t sendStep[2];          // PING / REV fader 1 (send of the selected tracks)
    bool latch[seqperf::PAGE_COUNT][4];
    uint64_t padHeld = 0;         // fader pads currently held (bit = pad)
    bool shiftView = false;

    uint8_t ledShown[48];

    // display
    hControl label[8];
    hControl bgLabel;
    hControl titleLabel;
    hControl infoLabel[5];

    char buttonText[8][6];
    char infoText[5][52];

    uint32_t shownKey = 0xFFFFFFFF;
    bool screenDirty = true;
};

extern cSeqPerform seqPerform;

#endif /* INTERFACE_SEQPERFORM_H_ */
