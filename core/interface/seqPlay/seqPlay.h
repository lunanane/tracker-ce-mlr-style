// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Play mode screen: pads drive the sequencer of the current track (plans/001-sequencer-tracks.md).
#ifndef INTERFACE_SEQPLAY_H_
#define INTERFACE_SEQPLAY_H_

#include "core/modulesBase.h"

class cSeqPlay : public ModuleBase
{
  public:
    virtual void update();
    virtual void start(uint32_t options);
    virtual void stop();

    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    cSeqPlay() {}
    ~cSeqPlay() {}

    //----------------------------------
    void selectTrack(uint8_t track);
    void padEvent(uint8_t pad, uint8_t state);
    void instrChanged(bool down);
    void jog(int16_t value);
    void shiftChanged();
    void releaseAllPads();
    void refreshScreen();
    void refreshPads();
    void recPressed();

    static constexpr uint8_t kNoOwner = 0xFF;

    uint8_t track = 0;
    bool shiftView = false;
    bool listOpen = false;
    uint8_t listSel = 0;

    uint8_t padOwner[48];
    uint8_t ledShown[48];

    // display
    hControl label[8];
    hControl bgLabel;
    hControl titleLabel;
    hControl infoLabel[4];
    hControl listLabel[3];

    char buttonText[8][12];
    char titleText[40];
    char infoText[4][48];
    const char* recMsg = nullptr; // transient message after a Rec press that could not start

    // what the screen currently shows (refresh only on change)
    uint32_t shownKey = 0xFFFFFFFF;
    bool screenDirty = true;
};

extern cSeqPlay seqPlay;

#endif /* INTERFACE_SEQPLAY_H_ */
