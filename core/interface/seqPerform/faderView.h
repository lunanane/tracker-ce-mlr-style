// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Screen of the alt Performance page (plan 005 R3): four bands = the four pad rows. One custom control, two display-list stages
// (shapes, then text). The module fills a strFaderView snapshot; the control only draws it.
#ifndef INTERFACE_SEQPERFORM_FADERVIEW_H_
#define INTERFACE_SEQPERFORM_FADERVIEW_H_

#include "displayControls.h"
#include "seqPerformLogic.h"

struct strFaderBand
{
    char name[22];  // FX name / "Time" / "Delay send" ...
    char value[16]; // "+42", "500 ms", "off"
    char note[64];  // second line: "slot 3   range -100..100", "project value"
    char state[12]; // "LIVE", "LATCHED", "DISENGAGED", ...
    uint8_t step;   // 0..8 cells lit from the left
    uint8_t status; // seqperf::RowStatus
    uint8_t usable; // 0 = empty slot (no FX): the band is drawn dim
    uint8_t canLatch;
    uint8_t latched;
    uint8_t engaged;
};

struct strFaderView
{
    uint8_t page;       // seqperf::Page
    uint8_t pageMask;   // pages with something dialled in (dot on the chip)
    uint8_t noTracks;   // no track selected: FX are inaudible (warning colour)
    char tracks[40];    // "affects 1-4 7"
    char summary[96];   // group line: "PING   time 500 ms   fb 50   filter 20000 Hz   ping-pong ON   sync off"
    strFaderBand band[4];
};

class cSeqFaderView : public cDisplayControl
{
  public:
    cSeqFaderView(strControlProperties* properties = nullptr);
    virtual ~cSeqFaderView() {}

    virtual void update();  // stage 0: shapes
    virtual void update2(); // stage 1: text

    virtual void setStyle(uint32_t style) { this->style = style; }
    virtual void setText(const char* text) { this->text = text; }
    virtual void setValue(int value) { this->value = value; }
    virtual void setColors(const uint32_t* colors) { this->colors = colors; }
    virtual void setData(void* data) { view = (const strFaderView*)data; }

  private:
    const strFaderView* view = nullptr;
};

#endif /* INTERFACE_SEQPERFORM_FADERVIEW_H_ */
