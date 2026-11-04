// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// The control of the Play overlay: type list above a strip cell, and the pad legend. Shapes and ASCII only (the fonts have no
// symbols). Everything is drawn with few commands: cells of one zone share one colour and one RECTS batch.
#include "seqPlay/seqPlay.h"

#include "FT812.h"
#include "display.h"

namespace {
constexpr int16_t kListY = 328;    // three boxes of 30 px, 2 px apart: y 328..422
constexpr int16_t kListStep = 32;
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

static void box(int16_t x, int16_t y, int16_t w, int16_t h)
{
    API_BEGIN(RECTS);
    API_VERTEX2F(x, y);
    API_VERTEX2F(x + w, y + h);
    API_END();
}

void cSeqPlayLegend::update()
{
    if (view == nullptr || view->mode == SeqPlayViewData::HIDDEN)
        return;

    const Theme& th = display.getTheme();
    const int16_t font3 = fonts[2].handle; // 14 px bold
    const int16_t font1 = fonts[0].handle; // 14 px regular

    API_BLEND_FUNC(SRC_ALPHA, ZERO); // opaque: the pattern rows below must not shine through
    API_LINE_WIDTH(16);

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
    // backing panel
    API_COLOR(th.backgroundColor);
    box(kLegendX - 4, kLegendY - 16, 12 * kLegendCellW + 8, 4 * kLegendCellH + 18);

    // cells, one batch per zone
    static const uint8_t kZones[] = { seqplay::Z_NONE, seqplay::Z_NOTE, seqplay::Z_STEP, seqplay::Z_PANEL,
                                      seqplay::Z_MODE, seqplay::Z_ADJ,  seqplay::Z_ACTION, seqplay::Z_HOLD };
    for (uint8_t zi = 0; zi < sizeof(kZones); zi++)
    {
        uint8_t zone = kZones[zi];
        uint32_t color = zone == seqplay::Z_NONE     ? th.grey_22 :
                         zone == seqplay::Z_NOTE     ? th.green :
                         zone == seqplay::Z_STEP     ? th.green :
                         zone == seqplay::Z_PANEL    ? th.cyan :
                         zone == seqplay::Z_MODE     ? th.orange :
                         zone == seqplay::Z_ADJ      ? th.yellow :
                         zone == seqplay::Z_ACTION   ? th.red :
                                                       th.white;
        API_COLOR(color);
        for (uint8_t row = 0; row < 4; row++)
        {
            for (uint8_t col = 0; col < 12; col++)
            {
                if (seqplay::legendCell(view->type, col, row).zone != zone)
                    continue;
                box((int16_t)(kLegendX + col * kLegendCellW + 1), (int16_t)(kLegendY + row * kLegendCellH + 1), kLegendCellW - 2,
                    kLegendCellH - 2);
            }
        }
    }

    API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);

    // caption above, labels in the cells (dark text on the bright cells)
    API_COLOR(th.white);
    API_CMD_TEXT(kLegendX, (int16_t)(kLegendY - 8), font1, OPT_CENTERY, seqplay::legendCaption(view->type));
    API_COLOR(0x000000);
    for (uint8_t row = 0; row < 4; row++)
    {
        for (uint8_t col = 0; col < 12; col++)
        {
            seqplay::LegendCell lc = seqplay::legendCell(view->type, col, row);
            if (lc.txt[0] == 0)
                continue;
            API_CMD_TEXT((int16_t)(kLegendX + col * kLegendCellW + kLegendCellW / 2), (int16_t)(kLegendY + row * kLegendCellH + kLegendCellH / 2),
                         font3, OPT_CENTERX | OPT_CENTERY, lc.txt);
        }
    }
}
