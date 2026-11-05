// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// One display control for the plan 013 trace views: the PolySynth pitch trace (seqPitchTraceLogic.h) and the master amplitude view
// (seqAmpViewLogic.h). The layout and drawing are the host-tested templates; this control only maps their context onto the FT81x
// display list. Hidden (style 0) until a view shows it.
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
};

class cSeqTraceControl : public cDisplayControl
{
  public:
    cSeqTraceControl(strControlProperties* properties = nullptr);
    virtual ~cSeqTraceControl() {}

    virtual void update();
    virtual void setStyle(uint32_t style) { this->style = style; }
    virtual void setText(const char*) {}
    virtual void setValue(int) {}
    virtual void setColors(const uint32_t*) {}
    virtual void setData(void* data) { view = (const TraceViewData*)data; }

  private:
    const TraceViewData* view = nullptr;
};

#endif
