// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// One display control for the plan 013 trace views: the PolySynth pitch trace (seqPitchTraceLogic.h) and the master amplitude view
// (seqAmpViewLogic.h). The layout and drawing are the host-tested templates; this control only maps their context onto the FT81x
// display list. Hidden (style 0) until a view shows it. v0.3.9: two display stages of at most 3840 FIFO bytes each (seqTraceStages.h).
// v0.4.3: the amp lines in 1/16 px (VERTEX_FORMAT(4), back to 0 before the texts and at the end of the last stage).
#ifndef INTERFACE_SEQ_TRACE_CONTROL_H_
#define INTERFACE_SEQ_TRACE_CONTROL_H_

#include "displayControls.h"
#include "seqAmpViewLogic.h"
#include "seqPitchTraceLogic.h"

struct TraceViewData
{
    enum Kind : uint8_t { NONE = 0, PITCH = 1, AMP = 2 };
    uint8_t kind = NONE;
    const pitchtrace::Follower* pitch = nullptr;
    const pitchtrace::Range* range = nullptr;
    const ampview::Follower* amp = nullptr;
    char title[40] = { 0 };
    char hint[40] = { 0 };
    // v0.3.9: != 0 (the ms of stage 0) while a frame is half built: the view must not change its follower until stage 1 has drawn it
    mutable volatile uint32_t busyMs = 0;
    // v0.4.3 (amp view): the display list the last complete frame really took (the control's RAM_G size, noted when the next frame starts)
    // and the view's own estimate of it; measured = a new pair (read and cleared by the view)
    mutable int32_t lastRam = 0, lastEst = 0;
    mutable bool lastComplete = false;
    mutable volatile bool measured = false;
};

class cSeqTraceControl : public cDisplayControl
{
  public:
    cSeqTraceControl(strControlProperties* properties = nullptr);
    virtual ~cSeqTraceControl() {}

    virtual void update();
    virtual void update2();
    virtual void setStyle(uint32_t style) { this->style = style; }
    virtual void setText(const char*) {}
    virtual void setValue(int) {}
    virtual void setColors(const uint32_t*) {}
    virtual void setData(void* data) { view = (const TraceViewData*)data; }

  private:
    void drawStage(int stage);
    const TraceViewData* view = nullptr;
};

#endif
