// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Session view: pad layers (CLIPS clip launcher, CUT) on the 48 pads. Reached from Play mode with a Pattern tap
// (Editor -> Play -> Session -> Editor), see plans/002-session-view.md.
#ifndef INTERFACE_SEQSESSION_H_
#define INTERFACE_SEQSESSION_H_

#include "core/modulesBase.h"
#include "seqSession/clipsLayer.h"
#include "seqSession/sessionLayer.h"

class cSeqSession : public ModuleBase
{
  public:
    virtual void update();
    virtual void start(uint32_t options);
    virtual void stop();

    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    cSeqSession() {}
    ~cSeqSession() {}

    //----------------------------------
    // Plug in the CUT layer (default: a stub that shows "CUT - coming"). Call before the Session view is first shown.
    void setCutLayer(seqsession::SessionLayer* layer) { layers[1] = layer; }

    void selectGroup(uint8_t first);
    void releaseHeldPads();
    void selectLayer(uint8_t index);
    void padEvent(uint8_t pad, uint8_t state);
    void jog(int16_t value);
    void refreshScreen(bool force);
    void refreshPads();

    uint8_t groupFirst = 0; // first track of the 8 shown (0 or 8)
    uint8_t layerIndex = 0; // 0 = CLIPS, 1 = CUT
    seqsession::SessionLayer* layers[2] = { &clips, &cutStub };

    seqsession::ClipsLayer clips;
    seqsession::CutStubLayer cutStub;

    uint8_t ledShown[48];
    bool padHeld[48];

    // display
    hControl label[8];
    hControl bgLabel;
    hControl titleLabel;
    hControl infoLabel[4];

    char buttonText[8][12];
    char titleText[40];
    char infoText[4][48];
    char shownInfo[4][48];
    char shownTitle[40];
    char shownButtons[8][12];
};

extern cSeqSession seqSession;

#endif /* INTERFACE_SEQSESSION_H_ */
