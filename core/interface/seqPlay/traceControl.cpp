// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Display control of the trace views (plan 013): thin lines on a plain dark panel. Shapes and ASCII only.
#include "seqPlay/traceControl.h"

#include "FT812.h"
#include "display.h"
#include "seqTraceStages.h"
#include "seqTracks.h"

namespace
{
// The drawing context of the templates over the FT81x display list (all shapes first, the texts after textPhase()).
struct TraceCtx
{
    void color(uint32_t rgb) { API_COLOR(rgb); }
    void width(int w16) { API_LINE_WIDTH(w16); }
    void rect(int x0, int y0, int x1, int y1)
    {
        API_LINE_WIDTH(8);
        API_BEGIN(RECTS);
        API_VERTEX2F(x0, y0);
        API_VERTEX2F(x1 - 1, y1 - 1);
        API_END();
    }
    void lines(const int16_t* s, int n)
    {
        API_BEGIN(LINES);
        for (int i = 0; i < n; i++)
        {
            API_VERTEX2F(s[i * 4], s[i * 4 + 1]);
            API_VERTEX2F(s[i * 4 + 2], s[i * 4 + 3]);
        }
        API_END();
    }
    void strip(const int16_t* xy, int n)
    {
        API_BEGIN(LINE_STRIP);
        for (int i = 0; i < n; i++)
            API_VERTEX2F(xy[i * 2], xy[i * 2 + 1]);
        API_END();
    }
    void subpixel(bool on) { API_VERTEX_FORMAT(on ? 4 : 0); } // v0.4.3: the amp lines in 1/16 px (the frame uses whole pixels)
    void textPhase() { API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA); } // antialiased lines and texts blend normally
    void text(int x, int y, int font, int opt, const char* s)
    {
        API_CMD_TEXT(x, y, fonts[font - 1].handle,
                     (uint16_t)(((opt & 1) ? OPT_CENTERX : 0) | ((opt & 2) ? OPT_CENTERY : 0) | ((opt & 4) ? OPT_RIGHTX : 0)), s);
    }
    void finish() {}
};
} // namespace

cSeqTraceControl::cSeqTraceControl(strControlProperties* properties)
  : cDisplayControl(controlIdSeqTraceView, tracestage::kStages)
{
    colorsCount = 0;
    if (properties == nullptr)
        return;
    posX = properties->x;
    posY = properties->y;
    width = properties->w;
    height = properties->h;
    style = properties->style;
    view = (const TraceViewData*)properties->data;
}

// v0.3.9: the same drawing in two stages, each a FIFO burst of at most tracestage::kStageBytes (seqTraceStages.h); stage 1 continues
// the display list of stage 0 (same RAM_G slot), the data stays frozen in between (busyMs)
void cSeqTraceControl::drawStage(int stage)
{
    if (view == nullptr)
        return;
    if (view->kind == TraceViewData::NONE) // closed between the stages: nothing more to draw, the blend function and whole pixels back
    {
        if (stage == tracestage::kStages - 1)
        {
            API_VERTEX_FORMAT(0);
            API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);
        }
        view->busyMs = 0;
        view->lastComplete = false;
        return;
    }
    if (stage == 0)
    {
        // v0.4.3: ramSize still holds the display list of the last frame (display.cpp resets it after this stage): with the estimate of that
        // frame it tells the view how good its estimate is
        if (view->kind == TraceViewData::AMP && view->amp && view->lastComplete && view->amp->cache.valid)
        {
            view->lastRam = (int32_t)ramSize;
            view->lastEst = view->amp->cache.bytes;
            view->measured = true;
        }
        view->lastComplete = false;
        API_BLEND_FUNC(SRC_ALPHA, ZERO); // the opaque panel: what is below must not shine through
    }
    TraceCtx out;
    tracestage::Split<TraceCtx> c(out, stage);
    if (view->kind == TraceViewData::PITCH && view->pitch && view->range)
        pitchtrace::draw(c, *view->pitch, *view->range, view->title, view->hint);
    else if (view->kind == TraceViewData::AMP && view->amp)
        ampview::draw(c, *view->amp, view->title, view->hint);
#ifdef DEBUG
    if (stage == tracestage::kStages - 1) // FIFO bytes of the two stages and what was dropped (0 = nothing)
    {
        char buf[48];
        if (view->kind == TraceViewData::PITCH && view->pitch) // v0.4.1: + columns read from the live voices, voices sounding now
            snprintf(buf, sizeof buf, "%d+%d-%d s%lu v%d", c.used[0], c.used[1], c.dropped, (unsigned long)view->pitch->fed(),
                     view->pitch->soundingNow());
        else
            snprintf(buf, sizeof buf, "%d+%d-%d", c.used[0], c.used[1], c.dropped);
        API_COLOR(0x808080);
        API_CMD_TEXT(4, 470, fonts[0].handle, OPT_CENTERY, buf);
    }
#endif
    if (stage == tracestage::kStages - 1)
    {
        API_VERTEX_FORMAT(0); // v0.4.3: whole pixels again for the controls after this one (the drawing switches back too; never left on)
        API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);
        view->lastComplete = c.dropped == 0;
    }
    view->busyMs = stage == 0 ? (seqNowMs() | 1u) : 0u;
}

void cSeqTraceControl::update() { drawStage(0); }

void cSeqTraceControl::update2() { drawStage(1); }
