// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// PolySynth pitch trace view (plan 013 V): the one entry point the Play-mode overlay and the PolySynth instrument page call.
//
//   pitchTraceView.createControl();   // when the module builds its display controls (after the module's other controls)
//   pitchTraceView.forget();          // when the module stops (the interface destroys the control)
//   pitchTraceView.show(true, "FX2: parameters");   // open / close; the hint is the labelled key that closes it again
//   pitchTraceView.update();          // every main loop pass: cheap while hidden; refreshes at most 25 fps while visible
//
// While visible the control draws a plain panel over rows y 30..424 of the screen; the pads, the labels and the wheel are not touched.
#ifndef INTERFACE_SEQ_PITCH_TRACE_H_
#define INTERFACE_SEQ_PITCH_TRACE_H_

#include <stdint.h>

#include "seqPlay/traceControl.h"

class PitchTraceView
{
  public:
    void createControl();
    void forget();
    void show(bool on, const char* hint);
    bool visible() const { return data_.kind == TraceViewData::PITCH; }
    void update();

  private:
    // v0.3.7: the follower (672 samples x 4 voices, 10.8 KB) is raw storage in EXTERNAL_RAM, constructed on the first show
    pitchtrace::Follower* follower_ = nullptr;
    pitchtrace::Range range_;
    TraceViewData data_;
    int8_t control_ = -1;
    uint32_t lastMs_ = 0;
};

extern PitchTraceView pitchTraceView;

#endif
