// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Master fader pages (plan 024): the pads of the Master page (Global Mixer 3/3) as 8-step faders like the Perform fader page (plans/004, 005 R3).
// Pages: REVERB and DELAY (the Perform REV / PING rows, with the return level as fader 1), HIGH-PASS (the master high-pass, new) and BEAT RPT (the
// beat repeat pads of the CUT page, plus the MIX level and band-pass of the repeats). The "Faders" screen button of Global Mixer 3/3 opens it as an
// overlay (like the amp view); the pad menu is pad column 9, the screen buttons 1-4 / 7 / 8 name the same pages. The screen is the Perform fader
// screen control (seqPerform/faderView.cpp, master = 1).
//
//   masterFaders.createControl();  // cMasterParams::initDisplayControls
//   masterFaders.forget();         // cMasterParams::stop (the control is gone, the pre-roll ends)
//   masterFaders.open() / close()  // the "Faders" screen button / "Close" (cMasterParams::fadersButton)
//   masterFaders.update();         // every main loop pass of the Master module while open (LEDs, screen)
#ifndef INTERFACE_MASTERPARAMS_MASTERFADERS_H_
#define INTERFACE_MASTERPARAMS_MASTERFADERS_H_

#include <stdint.h>

#include "masterParams/masterFaderView.h"

class cMasterFaders
{
  public:
    enum Result : uint8_t { R_NONE = 0, R_RELABEL, R_CLOSE };

    void createControl();
    void forget();
    bool visible() const { return open_; }
    void open();
    void close();
    void update();
    void padEvent(uint8_t pad, uint8_t state);
    Result buttonEvent(uint8_t button); // screen button 0 .. 7 pressed
    void labels(char out[8][10]) const;  // the words under the screen buttons

  private:
    void setPage(uint8_t page);
    void fader(uint8_t row, uint8_t step);
    void disengage(uint8_t row);
    void preroll();
    void snapshot(seqmf::Snap& s) const;
    void leds();
    void refreshScreen(bool force);
    bool open_ = false;
    uint8_t page_ = seqmf::MP_REVERB;
    seqperf::State st_;     // PING (delay) / REV (reverb) rows 2-4: dialled state and the value before the first touch
    uint8_t retPre_[2] = {}; // return level before the first touch (delay, reverb)
    uint8_t retDial_ = 0;    // bit 0 delay, bit 1 reverb
    int8_t control_ = -1;
    seqmf::Snap shown_;
    bool haveShown_ = false;
    uint32_t drawMs_ = 0, ledMs_ = 0;
    uint8_t ledShown_[48] = {};
    strFaderView view_;
};

extern cMasterFaders masterFaders;

#endif
