// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Master amplitude view (plan 013 M, redesigned in plan 014 B): the one entry point of the Master page. A dB history of the 8 sample
// tracks (their RMS meters) and the master (limiter peak tap) on an exponential time axis, see seqAmpViewLogic.h.
//
//   ampView.createControl();   // first thing in cMasterParams::initDisplayControls (lowest index = drawn on top)
//   ampView.forget();          // cMasterParams::stop
//   ampView.toggle();          // the "Amp view" screen button
//   ampView.update(pageOk);    // every main loop pass; closes the view when the page is left; <= 25 fps and the audio tap only while open
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

  private:
    ampview::Follower follower_;
    TraceViewData data_;
    int8_t control_ = -1;
};

extern AmpView ampView;

#endif
