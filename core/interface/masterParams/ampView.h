// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Master amplitude view (plan 013 M, redesigned in plan 014 B, plan 015 V): the one entry point of the Master page. A dB history of the 8
// sample tracks and the master, fed with ONE PEAK PER AUDIO BLOCK (2.9 ms; taps in the per track analyzers and the limiter, rings in
// EXTERNAL_RAM), on a time axis that is linear for the right 75 % of the width and compresses towards the left axis, see seqAmpViewLogic.h.
//
// v0.4.3: the taps and the history run ALWAYS (from the first main loop pass after boot; owner: "it should look full"): the history no
// longer starts when the Master module is entered and does not stop when it is left, so the view shows the full time scale whenever it is
// opened. Cost: the track taps run only for blocks a track really sends (an idle track sends none: one store), a pass over 128 samples per
// sounding track channel and per master channel per block (MK66, 180 MHz: about 4 cycles per sample, 1 % of the block with 8 sounding
// tracks, 0.2 % for the master alone); the push is O(1) per block and lane and the drain runs every 40 ms in the main loop (about 14 blocks
// x 9 lanes). Nothing is drawn or refreshed while the view is closed.
//
//   ampView.createControl();   // first thing in cMasterParams::initDisplayControls (lowest index = drawn on top)
//   ampView.tick();            // every main loop pass (TrackerImpl::update): attaches the taps once, feeds the history (<= 25 passes/s)
//   ampView.begin();           // cMasterParams::start: the same attach (no effect when already running)
//   ampView.forget();          // cMasterParams::stop: the view's control is gone (the history keeps running)
//   ampView.toggle();          // the "Amp view" screen button (opens / closes the view)
//   ampView.update(pageOk);    // every main loop pass of the Master module: feeds, draws (<= 25 fps) while open, closes when the page is left
//   ampView.jog(delta);        // the jog wheel while the view is open: the time scale (linear 2 ... 16 s, total 8 ... 60 s)
#ifndef INTERFACE_MASTER_AMP_VIEW_H_
#define INTERFACE_MASTER_AMP_VIEW_H_

#include <stdint.h>

#include "seqPlay/traceControl.h"

class AmpView
{
  public:
    void createControl();
    void tick();
    void begin();
    void forget();
    void toggle();
    void close();
    bool visible() const { return data_.kind == TraceViewData::AMP; }
    void update(bool pageOk);
    void jog(int16_t delta);

  private:
    enum { kRings = ampview::kTracks * 2 + 2, kRingMaster = ampview::kTracks * 2 };
    void feed(uint32_t now);
    bool running_ = false; // taps attached and history running (from the first main loop pass)
    ampview::Follower* follower_ = nullptr; // raw storage in EXTERNAL_RAM, constructed by begin() (never a static constructor)
    TraceViewData data_;
    int8_t control_ = -1;
    uint32_t lastMs_ = 0;      // last feed
    uint32_t drawMs_ = 0;      // last refresh of the view
    uint32_t drawnBlocks_ = 0; // history length at that refresh
    int16_t corr_ = 0;         // own display list minus its estimate, measured (frameBudget)
    int8_t pendingJog_ = 0;    // jog steps not applied yet (update)
    uint16_t last_[kRings];
};

extern AmpView ampView;

#endif
