// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Display control of the trace views (plan 013): thin lines on a plain dark panel. Shapes and ASCII only.
#include "seqPlay/traceControl.h"

#include "FT812.h"
#include "display.h"

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
  : cDisplayControl(controlIdSeqTraceView, 1)
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

void cSeqTraceControl::update()
{
    if (view == nullptr || view->kind == TraceViewData::NONE)
        return;
    API_BLEND_FUNC(SRC_ALPHA, ZERO); // the opaque panel: what is below must not shine through
    TraceCtx c;
    if (view->kind == TraceViewData::PITCH && view->pitch && view->range)
        pitchtrace::draw(c, *view->pitch, *view->range, view->title, view->hint);
    else if (view->kind == TraceViewData::AMP && view->amp)
        ampview::draw(c, *view->amp, view->title, view->hint);
    API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);
}
