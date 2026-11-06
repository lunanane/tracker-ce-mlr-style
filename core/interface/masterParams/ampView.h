// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Master amplitude view (plan 013 M, redesigned in plan 014 B, plan 015 V): the one entry point of the Master page. A dB history of the 8
// sample tracks and the master, fed with ONE PEAK PER AUDIO BLOCK (2.9 ms; taps in the per track analyzers and the limiter, rings in
// EXTERNAL_RAM), on a time axis that is linear for the right 75 % of the width and compresses towards the left axis, see seqAmpViewLogic.h.
//
// v0.4.1: the taps and the history run for as long as the MASTER MODULE is open (begin / forget), not only while the view is open, so the
// view shows its history at once when it is opened. Cost: the taps are about 1.5 % of the block budget with all eight tracks (one pass over
// 128 samples per track channel and per master channel per block), the push is O(1) per block and lane, and the drain runs every 40 ms in
// the main loop of the module; nothing is drawn or refreshed while the view is closed. Not "always on": outside the Master module nobody
// looks at the history and the taps stay detached.
//
//   ampView.createControl();   // first thing in cMasterParams::initDisplayControls (lowest index = drawn on top)
//   ampView.begin();           // cMasterParams::start: attach the taps, start the history (no effect when already running)
//   ampView.forget();          // cMasterParams::stop: detach the taps
//   ampView.toggle();          // the "Amp view" screen button (opens / closes the view, the history keeps running)
//   ampView.update(pageOk);    // every main loop pass; feeds the history (<= 25 passes/s); closes the view when the page is left
//   ampView.jog(delta);        // the jog wheel while the view is open: the time scale (linear 2 ... 16 s, total 8 ... 60 s)
#ifndef INTERFACE_MASTER_AMP_VIEW_H_
#define INTERFACE_MASTER_AMP_VIEW_H_

#include <stdint.h>

#include "seqPlay/traceControl.h"

class AmpView
{
  public:
    void createControl();
    void begin();
    void forget();
    void toggle();
    void close();
    bool visible() const { return data_.kind == TraceViewData::AMP; }
    void update(bool pageOk);
    void jog(int16_t delta);

  private:
    enum { kRings = ampview::kTracks * 2 + 2, kRingMaster = ampview::kTracks * 2 };
    void detach();
    bool running_ = false; // taps attached and history running (Master module open)
    ampview::Follower* follower_ = nullptr; // raw storage in EXTERNAL_RAM, constructed on the first open (never a static constructor)
    TraceViewData data_;
    int8_t control_ = -1;
    uint32_t lastMs_ = 0;
    uint16_t last_[kRings];
};

extern AmpView ampView;

#endif
