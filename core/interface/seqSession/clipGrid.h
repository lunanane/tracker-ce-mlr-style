// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Display controls of the clip launcher screen (plan 005 R2): the 12 x 16 grid of clips with the pad window, and a second small
// control for what moves (progress bars, blinking, countdown). The data structs and the geometry are in seqSessionLogic.h
// (namespace clipgrid) and host-tested.
#pragma once
#include "displayControls.h"
#include "seqSessionLogic.h"

// Static part: cells, numbers, flags, pad window, focus, info column. Redrawn when the View changes (events, not every step).
class cSeqClipGrid : public cDisplayControl
{
  public:
    cSeqClipGrid(strControlProperties* properties = nullptr);
    virtual ~cSeqClipGrid() {}

    virtual void update();
    virtual void setStyle(uint32_t style) { this->style = style; }
    virtual void setText(const char* text) { this->text = text; }
    virtual void setValue(int value) { this->value = value; }
    virtual void setColors(const uint32_t* colors) { this->colors = colors; }
    virtual void setData(void* data) { view = (const seqsession::clipgrid::View*)data; }

  private:
    const seqsession::clipgrid::View* view = nullptr;
};

// Animated part: progress bars under the playing clips, the blinking queued clips, the song position bar, the countdown.
// Created before the grid so it is composed on top of it (the display composes the lowest control index last).
class cSeqClipMarks : public cDisplayControl
{
  public:
    cSeqClipMarks(strControlProperties* properties = nullptr);
    virtual ~cSeqClipMarks() {}

    virtual void update();
    virtual void setStyle(uint32_t style) { this->style = style; }
    virtual void setText(const char* text) { this->text = text; }
    virtual void setValue(int value) { this->value = value; }
    virtual void setColors(const uint32_t* colors) { this->colors = colors; }
    virtual void setData(void* data) { marks = (const seqsession::clipgrid::Marks*)data; }

  private:
    const seqsession::clipgrid::Marks* marks = nullptr;
};
