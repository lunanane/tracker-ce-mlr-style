// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// The control of the Play overlay: type list above a strip cell, and the pad legend. Shapes and ASCII only (the fonts have no
// symbols). Everything is drawn with few commands: cells of one zone share one colour and one RECTS batch.
#include "seqPlay/seqPlay.h"

#include "FT812.h"
#include "display.h"

#include <stdio.h>

namespace {
constexpr int16_t kListStep = 32;
constexpr int16_t kListY = (int16_t)(424 - seqlogic::TYPE_COUNT * kListStep); // TYPE_COUNT boxes of 30 px, 2 px apart, ending at y 422
constexpr int16_t kCellW = 100;
constexpr int16_t kLegendX = 40;   // 12 cells of 60 px: x 40..760
constexpr int16_t kLegendY = 334;  // 4 rows of 22 px: y 334..422
constexpr int16_t kLegendCellW = 60;
constexpr int16_t kLegendCellH = 22;
} // namespace

cSeqPlayLegend::cSeqPlayLegend(strControlProperties* properties)
  : cDisplayControl(controlIdSeqPlayLegend, 1)
{
    colorsCount = 0;
    if (properties == nullptr)
        return;
    posX = properties->x;
    posY = properties->y;
    width = properties->w;
    height = properties->h;
    style = properties->style;
    view = (const SeqPlayViewData*)properties->data;
}

namespace {
// Drawing context of seqPlinkyUiLogic.h over the FT81x display list: rectangles are batched, the texts come after the shapes.
struct PlinkyCtx
{
    bool inRects = false;
    void ensureRects()
    {
        if (inRects)
            return;
        API_LINE_WIDTH(8);
        API_BEGIN(RECTS);
        inRects = true;
    }
    void endRects()
    {
        if (inRects)
            API_END();
        inRects = false;
    }
    void color(uint32_t rgb) { API_COLOR(rgb); }
    void rect(int x0, int y0, int x1, int y1)
    {
        ensureRects();
        API_VERTEX2F(x0, y0);
        API_VERTEX2F(x1 - 1, y1 - 1);
    }
    void textPhase()
    {
        endRects();
        API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);
    }
    void text(int x, int y, int font, int opt, const char* s)
    {
        endRects();
        API_CMD_TEXT(x, y, fonts[font - 1].handle, (uint16_t)(((opt & 1) ? OPT_CENTERX : 0) | ((opt & 2) ? OPT_CENTERY : 0) | ((opt & 4) ? OPT_RIGHTX : 0)), s);
    }
    void finish() { endRects(); }
};
} // namespace

static void box(int16_t x, int16_t y, int16_t w, int16_t h)
{
    API_BEGIN(RECTS);
    API_VERTEX2F(x, y);
    API_VERTEX2F(x + w, y + h);
    API_END();
}

void cSeqPlayLegend::update()
{
    if (view == nullptr || (view->mode == SeqPlayViewData::HIDDEN && view->edge == 0 && !view->notes.show))
        return;

    const Theme& th = display.getTheme();
    const int16_t font3 = fonts[2].handle; // 14 px bold
    const int16_t font1 = fonts[0].handle; // 14 px regular

    API_BLEND_FUNC(SRC_ALPHA, ZERO); // opaque: the pattern rows below must not shine through
    API_LINE_WIDTH(16);

    // the selected track is on the other strip page: a small marker at that edge of the strip
    if (view->edge != 0)
    {
        char t[8];
        snprintf(t, sizeof t, view->edge == 1 ? "T%d >" : "< T%d", view->edgeTrack);
        const int16_t cx = view->edge == 1 ? 748 : 52;
        API_COLOR(th.backgroundColor);
        box((int16_t)(cx - 34), 404, 68, 18);
        API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);
        API_COLOR(th.yellow);
        API_CMD_TEXT(cx, 413, font3, OPT_CENTERX | OPT_CENTERY, t);
        API_BLEND_FUNC(SRC_ALPHA, ZERO);
    }

    if (view->mode == SeqPlayViewData::HIDDEN)
    {
        if (view->notes.show)
        {
            seqnotes::Palette pal = { th.backgroundColor, th.grey_22, th.green, th.yellow, th.white };
            PlinkyCtx c;
            seqnotes::drawBand(c, view->notes, pal);
        }
        API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);
        return;
    }

    if (view->mode == SeqPlayViewData::PLINKY)
    {
        seqplinky::Palette pal = { th.backgroundColor, th.grey_22, 0x555555, th.white, th.cyan, th.yellow, 0x000000, th.cyan };
        PlinkyCtx c;
        seqplinky::draw(c, view->plinky, pal);
        return;
    }

    if (view->mode == SeqPlayViewData::LIST)
    {
        const int16_t x = (int16_t)(view->cell * kCellW + 1);
        for (uint8_t i = 0; i < seqlogic::TYPE_COUNT; i++)
        {
            const int16_t y = (int16_t)(kListY + i * kListStep);
            const uint32_t* c = (i == view->sel) ? th.highlightedButtonLabelsColors : th.inactiveButtonLabelsColors;
            API_COLOR(c[1]);
            box(x, y, kCellW - 2, 30);
            API_COLOR(c[2]);
            API_LINE_WIDTH(8);
            API_BEGIN(LINE_STRIP);
            API_VERTEX2F(x, y);
            API_VERTEX2F(x + kCellW - 2, y);
            API_VERTEX2F(x + kCellW - 2, y + 30);
            API_VERTEX2F(x, y + 30);
            API_VERTEX2F(x, y);
            API_END();
            API_LINE_WIDTH(16);
            if (i == view->cur) // the type the track has now: a small square left of the name
            {
                API_COLOR(c[0]);
                box(x + 7, y + 12, 6, 6);
            }
        }
        API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);
        for (uint8_t i = 0; i < seqlogic::TYPE_COUNT; i++)
        {
            const uint32_t* c = (i == view->sel) ? th.highlightedButtonLabelsColors : th.inactiveButtonLabelsColors;
            API_COLOR(c[0]);
            API_CMD_TEXT((int16_t)(x + (kCellW - 2) / 2 + 4), (int16_t)(kListY + i * kListStep + 15), font3, OPT_CENTERX | OPT_CENTERY,
                         seqplay::typeName(i));
        }
        return;
    }

    // ---- legend ----
    seqplay::LegendState st;
    st.type = view->type;
    st.page = view->page;
    st.pon = view->pon;
    st.half = view->half;
    st.menu = view->menu;
    st.ep = view->ep;
    st.np = view->np;
    st.speed = view->speed;
    st.vel = view->vel;
    st.tie = view->tie;
    st.win = view->win;
    st.hr = &view->hr;
    st.pl = view->plSetup;
    st.plMenu = view->plMenu;

    // backing panel: two caption lines (state, how to play) above the 12 x 4 cells
    API_COLOR(th.backgroundColor);
    box(kLegendX - 4, kLegendY - 52, 12 * kLegendCellW + 8, 4 * kLegendCellH + 54);

    // cells: one RECTS batch per zone (one colour, one BEGIN / END); the active mode pad is white
    static const uint8_t kZones[] = { seqplay::Z_NONE, seqplay::Z_NOTE, seqplay::Z_STEP, seqplay::Z_PANEL,
                                      seqplay::Z_MODE, seqplay::Z_ADJ,  seqplay::Z_ACTION, seqplay::Z_HOLD, 0xFF };
    for (uint8_t zi = 0; zi < sizeof(kZones); zi++)
    {
        const bool onBatch = kZones[zi] == 0xFF; // last batch: the pads that are on now
        const uint8_t zone = kZones[zi];
        const uint32_t color = onBatch                       ? th.white :
                               zone == seqplay::Z_NONE       ? th.grey_22 :
                               zone == seqplay::Z_NOTE       ? th.green :
                               zone == seqplay::Z_STEP       ? th.green :
                               zone == seqplay::Z_PANEL      ? th.cyan :
                               zone == seqplay::Z_MODE       ? th.orange :
                               zone == seqplay::Z_HOLD       ? th.orange :
                               zone == seqplay::Z_ADJ        ? th.yellow :
                               zone == seqplay::Z_ACTION     ? th.red :
                                                               th.white;
        API_COLOR(color);
        API_BEGIN(RECTS);
        for (uint8_t row = 0; row < 4; row++)
        {
            for (uint8_t col = 0; col < 12; col++)
            {
                seqplay::LegendCell lc = seqplay::legendCell(st, col, row);
                if (onBatch ? !lc.on : (lc.zone != zone || lc.on))
                    continue;
                const int16_t x = (int16_t)(kLegendX + col * kLegendCellW + 1);
                const int16_t y = (int16_t)(kLegendY + row * kLegendCellH + 1);
                API_VERTEX2F(x, y);
                API_VERTEX2F(x + kLegendCellW - 2, y + kLegendCellH - 2);
            }
        }
        API_END();
    }

    API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);

    // caption above: line 1 = what is shown + the state, line 2 = how to play it; labels in the cells (dark text on the bright cells)
    char cap[96];
    seqplay::legendTitle(st, cap, sizeof cap);
    API_COLOR(th.white);
    API_CMD_TEXT(kLegendX, (int16_t)(kLegendY - 27), font3, OPT_CENTERY, cap);
    API_CMD_TEXT(kLegendX, (int16_t)(kLegendY - 9), font1, OPT_CENTERY, seqplay::legendHelp(st));
    API_COLOR(th.yellow);
    API_CMD_TEXT((int16_t)(kLegendX + 12 * kLegendCellW), (int16_t)(kLegendY - 44), font1, OPT_CENTERY | OPT_RIGHTX, "tap track again to close");
    API_COLOR(th.white);
    API_COLOR(0x000000);
    for (uint8_t row = 0; row < 4; row++)
    {
        for (uint8_t col = 0; col < 12; col++)
        {
            seqplay::LegendCell lc = seqplay::legendCell(st, col, row);
            if (lc.txt[0] == 0)
                continue;
            API_CMD_TEXT((int16_t)(kLegendX + col * kLegendCellW + kLegendCellW / 2), (int16_t)(kLegendY + row * kLegendCellH + kLegendCellH / 2),
                         font3, OPT_CENTERX | OPT_CENTERY, lc.txt);
        }
    }
}
