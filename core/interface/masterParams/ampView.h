// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Master amplitude view (plan 013 M, redesigned in plan 014 B, plan 015 V): the one entry point of the Master page. A dB history of the 8
// sample tracks and the master, fed with ONE PEAK PER AUDIO BLOCK (2.9 ms; taps in the per track analyzers and the limiter, rings in
// EXTERNAL_RAM, attached only while the view is open), on a time axis that is almost linear for the last seconds, see seqAmpViewLogic.h.
//
//   ampView.createControl();   // first thing in cMasterParams::initDisplayControls (lowest index = drawn on top)
//   ampView.forget();          // cMasterParams::stop
//   ampView.toggle();          // the "Amp view" screen button
//   ampView.update(pageOk);    // every main loop pass; closes the view when the page is left; <= 25 fps and the audio tap only while open
//   ampView.jog(delta);        // the jog wheel while the view is open: the time scale (right half = 2 ... 60 s)
#ifndef INTERFACE_MASTER_AMP_VIEW_H_
#define INTERFACE_MASTER_AMP_VIEW_H_

#include <stdint.h>

#include "seqPlay/traceControl.h"

class AmpView
{
  public:
    void createControl();
    void forget();
    void toggle();
    void close();
    bool visible() const { return data_.kind == TraceViewData::AMP; }
    void update(bool pageOk);
    void jog(int16_t delta);

  private:
    enum { kRings = ampview::kTracks * 2 + 2, kRingMaster = ampview::kTracks * 2 };
    void detach();
    ampview::Follower* follower_ = nullptr; // raw storage in EXTERNAL_RAM, constructed on the first open (never a static constructor)
    TraceViewData data_;
    int8_t control_ = -1;
    uint32_t lastMs_ = 0;
    uint16_t last_[kRings];
};

extern AmpView ampView;

#endif
