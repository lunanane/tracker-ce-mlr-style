// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
#include "seqPerform/faderView.h"

#include <string.h>

#include "FT812.h"
#include "Themes.h"
#include "display.h"

using namespace seqperf;

// Geometry (screen 800 x 480, stock title bar y 0-28, stock screen-button labels y 424-479).
static const int16_t kChipY = 31, kChipH = 20;
static const int16_t kStepNumY = 58;
static const int16_t kBand0 = 66, kBandStep = 80, kBandH = 78;
static const int16_t kBarX = 222, kCellW = 46, kCellStep = 50, kCellY = 8, kCellH = 38;
static const int16_t kSideX = 628;
static const int16_t kFooterY = 390;

// font styles -> index into fonts[] (FONT_INDEX_FROM_STYLE): Font1 14 regular, Font2 17 regular, Font3 14 bold, Font4 17 bold,
// Font5 12 regular
enum : uint8_t { F14 = 0, F17 = 1, F14B = 2, F17B = 3, F12 = 4 };

cSeqFaderView::cSeqFaderView(strControlProperties* properties)
  : cDisplayControl(controlIdSeqFaderView, 2)
{
    autoRefresh = false;
    colors = display.getTheme().defaultTextColors;
    if (properties == nullptr)
        return;
    posX = properties->x;
    posY = properties->y;
    width = properties->w;
    height = properties->h;
    setStyle(properties->style);
}

namespace {

void box(int16_t x, int16_t y, int16_t w, int16_t h)
{
    API_VERTEX2F(x + 1, y + 1);
    API_VERTEX2F(x + w - 1, y + h - 1);
}

void fill(uint32_t color, uint8_t alpha, int16_t x, int16_t y, int16_t w, int16_t h)
{
    API_COLOR(color);
    API_COLOR_A(alpha);
    box(x, y, w, h);
}

void drawText(uint32_t color, uint8_t alpha, int16_t x, int16_t y, uint8_t font, uint16_t opt, const char* s, int16_t maxW)
{
    if (s == nullptr || s[0] == 0)
        return;
    API_COLOR(color);
    API_COLOR_A(alpha);
    uint16_t len = getMaxTextLengthThatFitSpecificWidth(&fonts[font], s, maxW);
    if (len < strlen(s))
        API_CMD_TEXT(x, y, fonts[font].handle, opt | OPT_CENTERY, s, len);
    else
        API_CMD_TEXT(x, y, fonts[font].handle, opt | OPT_CENTERY, s);
}

struct Pal
{
    uint32_t bright, panel, cell, live, latched, project, off, warn;
};

Pal palette()
{
    Theme& th = display.getTheme();
    Pal p;
    p.bright = th.white;
    p.panel = th.grey_22;
    p.cell = th.activeLabelsColors[1];
    p.live = th.green;
    p.latched = th.orange;
    p.project = th.cyan;
    p.off = th.white;
    p.warn = th.red;
    return p;
}

uint32_t statusColor(const Pal& p, uint8_t status)
{
    switch (status)
    {
        case RS_LIVE:
        case RS_DIALLED: return p.live;
        case RS_LATCHED: return p.latched;
        case RS_PROJECT:
        default: return p.off;
    }
}

const char* chipName(uint8_t i, bool master)
{
    static const char* const n[PAGE_COUNT] = { "FX 1-4", "FX 5-8", "FX 9-12", "PING", "REV" };
    static const char* const m[4] = { "REVERB", "DELAY", "HIGH-PASS", "BEAT RPT" };
    return master ? m[i] : n[i];
}

void chipPos(uint8_t i, int16_t& x, int16_t& w, bool master)
{
    static const int16_t xs[PAGE_COUNT] = { 6, 88, 170, 274, 342 };
    static const int16_t ws[PAGE_COUNT] = { 78, 78, 84, 62, 52 };
    static const int16_t mx[4] = { 6, 96, 186, 296 };
    static const int16_t mw[4] = { 86, 86, 106, 100 };
    x = master ? mx[i] : xs[i];
    w = master ? mw[i] : ws[i];
}

} // namespace

//--------------------------------------------------------------------------------
// stage 0: every shape
void cSeqFaderView::update()
{
    if (view == nullptr)
        return;
    const Pal p = palette();
    API_SAVE_CONTEXT();
    API_LINE_WIDTH(16);
    API_BEGIN(RECTS);

    // page chips: the pads of columns 9 and 10; the active page filled, a dot where something is dialled in
    const uint8_t chips = view->master ? 4 : PAGE_COUNT;
    for (uint8_t i = 0; i < chips; i++)
    {
        int16_t x, w;
        chipPos(i, x, w, view->master != 0);
        bool active = view->page == i;
        fill(active ? p.bright : p.panel, 255, x, kChipY, w, kChipH);
        if (view->pageMask & (1u << i))
            fill(p.live, 255, x + w - 11, kChipY + 3, 8, 8);
    }

    for (uint8_t r = 0; r < 4; r++)
    {
        const strFaderBand& b = view->band[r];
        int16_t y0 = kBand0 + r * kBandStep;
        uint32_t sc = statusColor(p, b.status);
        uint8_t dim = b.usable ? 255 : 90;
        // band panel, status stripe on the left
        fill(p.panel, 255, 6, y0, 788, kBandH);
        fill(sc, b.status == RS_OFF ? 70 : 255, 6, y0, 5, kBandH);

        // the long bar: 8 steps, lit from the left
        for (uint8_t c = 0; c < 8; c++)
        {
            bool lit = c < b.step && b.usable;
            int16_t x = kBarX + c * kCellStep;
            if (lit)
            {
                bool soft = b.status == RS_PROJECT;
                fill(sc, soft ? 150 : 255, x, y0 + kCellY, kCellW, kCellH);
            }
            else
            {
                fill(p.cell, dim, x, y0 + kCellY, kCellW, kCellH);
            }
        }

        // LATCH and DISENGAGE cells (pad columns 11 and 12)
        if (b.canLatch)
            fill(b.latched ? p.latched : p.cell, 255, kSideX, y0 + 8, 78, 26);
        else
            fill(p.cell, 60, kSideX, y0 + 8, 78, 26);
        fill(b.engaged ? p.warn : p.cell, b.engaged ? 255 : 90, kSideX + 86, y0 + 8, 78, 26);
    }

    // footer strip behind the group line
    fill(p.panel, 255, 6, kFooterY, 788, 30);
    API_END();
    API_RESTORE_CONTEXT();
}

//--------------------------------------------------------------------------------
// stage 1: every text
void cSeqFaderView::update2()
{
    if (view == nullptr)
        return;
    const Pal p = palette();
    API_SAVE_CONTEXT();

    const uint8_t chips = view->master ? 4 : PAGE_COUNT;
    for (uint8_t i = 0; i < chips; i++)
    {
        int16_t x, w;
        chipPos(i, x, w, view->master != 0);
        bool active = view->page == i;
        drawText(active ? p.panel : p.bright, active ? 255 : 170, x + w / 2, kChipY + kChipH / 2, F14B, OPT_CENTERX, chipName(i, view->master != 0), w - 4);
    }
    // tracks the FX and sends affect (the 8 buttons under the screen select them)
    drawText(view->noTracks ? p.warn : p.bright, view->noTracks ? 255 : 200, 794, kChipY + kChipH / 2, F14, OPT_RIGHTX, view->tracks, 380);

    // step numbers over the cells
    static const char* const nums[8] = { "1", "2", "3", "4", "5", "6", "7", "8" };
    for (uint8_t c = 0; c < 8; c++)
        drawText(p.bright, 130, kBarX + c * kCellStep + kCellW / 2, kStepNumY, F12, OPT_CENTERX, nums[c], 30);

    for (uint8_t r = 0; r < 4; r++)
    {
        const strFaderBand& b = view->band[r];
        int16_t y0 = kBand0 + r * kBandStep;
        uint32_t sc = statusColor(p, b.status);
        uint8_t dim = b.usable ? 255 : 120;
        char num[2] = { (char)('1' + r), 0 };
        drawText(p.bright, dim, 22, y0 + 24, F17B, OPT_CENTERX, num, 20);
        drawText(p.bright, dim, 36, y0 + 18, F14B, 0, b.name, 178);
        drawText(b.status == RS_OFF ? p.bright : sc, b.status == RS_OFF ? 150 : 255, 36, y0 + 38, F17, 0, b.value, 178);
        drawText(p.bright, 150, 36, y0 + 62, F12, 0, b.note, 580);

        drawText(b.latched ? p.panel : p.bright, b.canLatch ? 255 : 70, kSideX + 39, y0 + 21, F12, OPT_CENTERX, b.lat[0] ? b.lat : "LATCH", 76);
        drawText(p.bright, b.engaged ? 255 : 110, kSideX + 86 + 39, y0 + 21, F12, OPT_CENTERX, b.dis[0] ? b.dis : "DISENGAGE", 76);
        drawText(b.status == RS_OFF ? p.bright : sc, b.status == RS_OFF ? 140 : 255, kSideX + 82, y0 + 54, F14B, OPT_CENTERX, b.state, 160);
    }

    drawText(p.bright, 230, 14, kFooterY + 15, F17, 0, view->summary, 770);
    API_RESTORE_CONTEXT();
}
