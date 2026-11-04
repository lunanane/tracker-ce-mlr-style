// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Clip launcher screen (plan 005 R2). Shapes and ASCII text only (the fonts have no symbols). Display list budget: the whole frame
// may use about 8 KB (display.cpp step3), shared with the title bar and the 8 labels, so the grid draws cells in one RECTS batch per
// colour (8 bytes per cell), numbers and a few text lines; planGrid() (seqSessionLogic.h) sums the cost from the View first and drops the optional parts that do not fit.
#include "FT812.h"
#include "display.h"
#include "displayControls.h"
#include "seqSession/clipGrid.h"
#include <stdio.h>

using namespace seqsession;
using namespace seqsession::clipgrid;

namespace
{
// theme colours (songPlayerColors): 0 text, 4 dim, 5 red, 6 black, 7 mid grey, 8 block, 9 green
struct Palette
{
    uint32_t text, dim, red, black, notes, empty, play;
};

Palette palette(const uint32_t* c)
{
    Palette p;
    p.text = c[0];
    p.dim = c[4];
    p.red = c[5];
    p.black = c[6];
    p.notes = c[7];
    p.empty = c[8];
    p.play = c[9];
    return p;
}

inline void rect(int x0, int y0, int x1, int y1)
{
    API_VERTEX2F((int16_t)x0, (int16_t)y0);
    API_VERTEX2F((int16_t)x1, (int16_t)y1);
}

void outline(int x0, int y0, int x1, int y1)
{
    API_BEGIN(LINE_STRIP);
    API_VERTEX2F((int16_t)x0, (int16_t)y0);
    API_VERTEX2F((int16_t)x1, (int16_t)y0);
    API_VERTEX2F((int16_t)x1, (int16_t)y1);
    API_VERTEX2F((int16_t)x0, (int16_t)y1);
    API_VERTEX2F((int16_t)x0, (int16_t)y0);
    API_END();
}

bool fillsAs(uint8_t cell, uint8_t cls) // cls 0 empty, 1 notes (also queued / follow), 2 playing
{
    uint8_t c = cell == CELL_PLAYING ? 2 : (cell == CELL_EMPTY ? 0 : 1);
    return c == cls;
}
} // namespace

//--------------------------------------------------------------------------------
cSeqClipGrid::cSeqClipGrid(strControlProperties* properties)
  : cDisplayControl(controlIdSeqClipGrid, 1)
{
    colorsCount = 10;
    colors = display.getTheme().songPlayerColors;
    autoRefresh = false;
    if (properties == nullptr)
        return;
    posX = properties->x;
    posY = properties->y;
    width = properties->w;
    height = properties->h;
    style = properties->style;
    view = (const View*)properties->data;
}

void cSeqClipGrid::update()
{
    if (view == nullptr)
        return;
    const View& v = *view;
    const GridPlan plan = planGrid(v); // what fits in the display list budget (summed, never read back from the driver)
    const Palette pal = palette(colors);
    const strFont* f = &fonts[0];                              // 14 px regular
    const strFont* fs = &fonts[displayFontCount > 4 ? 4 : 0];  // 12 px regular for the numbers
    char buf[32];

    // ---- header: track numbers (the tracks of the pad window bright) and row numbers (the rows of the window bright)
    for (uint8_t t = 0; t < kTracks; t++)
    {
        bool inWin = t >= v.winTrack && t < v.winTrack + 8;
        API_COLOR(inWin ? pal.text : pal.dim);
        API_CMD_NUMBER(colX(t) + kCellW / 2, posY + 18, fs->handle, OPT_CENTERX | OPT_CENTERY, t + 1);
    }
    int winRow = (int)v.winFirst - (int)v.top;
    for (uint8_t r = 0; r < kVisRows; r++)
    {
        bool inWin = (int)r >= winRow && (int)r < winRow + kWinRows;
        API_COLOR(inWin ? pal.text : pal.dim);
        API_CMD_NUMBER(kRowNumX + 18, rowY(r) + kCellH / 2, fs->handle, OPT_CENTERX | OPT_CENTERY, v.top + r);
    }

    // ---- cells: one batch per colour
    API_LINE_WIDTH(16);
    const uint32_t fill[3] = { pal.empty, pal.notes, pal.play };
    for (uint8_t cls = 0; cls < 3; cls++)
    {
        API_COLOR(fill[cls]);
        API_BEGIN(RECTS);
        for (uint8_t r = 0; r < kVisRows; r++)
        {
            for (uint8_t t = 0; t < kTracks; t++)
                if (fillsAs(v.cell[r][t], cls))
                    rect(colX(t), rowY(r), colX(t) + kCellW - 1, rowY(r) + kCellH - 1);
            if (fillsAs(v.scene[r], cls))
                rect(sceneX(), rowY(r), sceneX() + kSceneW - 1, rowY(r) + kCellH - 1);
        }
        API_END();
    }

    // ---- armed follow actions: a steady green outline (the blink of a launch the performer queued is in the marks control)
    if (plan.outlines)
    {
        API_LINE_WIDTH(16);
        API_COLOR(pal.play);
        for (uint8_t r = 0; r < kVisRows; r++)
            for (uint8_t t = 0; t < kTracks; t++)
                if (v.cell[r][t] == CELL_FOLLOW)
                    outline(colX(t), rowY(r), colX(t) + kCellW - 1, rowY(r) + kCellH - 1);
    }

    // ---- dim square: an empty cell of a sounding track inside the pad window - pressing it stops the track
    if (plan.stops)
    {
        API_LINE_WIDTH(16);
        API_COLOR(pal.dim);
        API_BEGIN(RECTS);
        for (uint8_t r = 0; r < kVisRows; r++)
            for (uint8_t t = 0; t < kTracks; t++)
                if (v.flags[r][t] & F_STOPMARK)
                    rect(colX(t) + kCellW / 2 - 3, rowY(r) + kCellH / 2 - 3, colX(t) + kCellW / 2 + 3, rowY(r) + kCellH / 2 + 3);
        API_END();
    }
    // ---- notch top right of a clip with content: Play 1x. Red: the next row is empty, so the track stops after this clip.
    if (plan.notches)
    {
        API_LINE_WIDTH(16);
        for (uint8_t pass = 0; pass < 2; pass++)
        {
            API_COLOR(pass == 0 ? pal.text : pal.red);
            API_BEGIN(RECTS);
            for (uint8_t r = 0; r < kVisRows; r++)
            {
                for (uint8_t t = 0; t < kTracks; t++)
                {
                    uint8_t fl = v.flags[r][t];
                    bool one = (fl & F_ONEX) != 0, end = (fl & F_END) != 0;
                    if (one && (pass == 1) == end)
                        rect(colX(t) + kCellW - 9, rowY(r) + 2, colX(t) + kCellW - 3, rowY(r) + 8);
                }
                if ((v.sceneFlags[r] & F_ONEX) && pass == 0)
                    rect(sceneX() + kSceneW - 9, rowY(r) + 2, sceneX() + kSceneW - 3, rowY(r) + 8);
            }
            API_END();
        }
    }

    // ---- focused clip (screen button 1 + wheel changes its mode)
    if (v.focusPattern >= v.top && v.focusPattern < v.top + kVisRows)
    {
        API_COLOR(pal.text);
        API_LINE_WIDTH(16);
        uint8_t r = (uint8_t)(v.focusPattern - v.top);
        outline(colX(v.focusTrack) + 1, rowY(r) + 1, colX(v.focusTrack) + kCellW - 2, rowY(r) + kCellH - 2);
    }

    // ---- pad window: a red frame round the 8 x 4 clips and one round their scene pads
    API_COLOR(pal.red);
    API_LINE_WIDTH(32);
    if (winRow + kWinRows > 0 && winRow < (int)kVisRows)
    {
        int r0 = winRow < 0 ? 0 : winRow;
        int r1 = winRow + kWinRows - 1;
        if (r1 > (int)kVisRows - 1) r1 = kVisRows - 1;
        outline(colX(v.winTrack) - 3, rowY((uint8_t)r0) - 3, colX(v.winTrack + 7) + kCellW + 2, rowY((uint8_t)r1) + kCellH + 2);
        outline(sceneX() - 3, rowY((uint8_t)r0) - 3, sceneX() + kSceneW + 2, rowY((uint8_t)r1) + kCellH + 2);
    }

    // ---- status line
    if (v.msg)
    {
        API_COLOR(pal.red);
        API_CMD_TEXT(kX0, kY0 + kVisRows * kRowH + 8, f->handle, 0, msgText(v.msg));
    }

    // ---- info column (x 616-796: at most about 23 characters per line)
    int x = kInfoX, y = posY + 8;
    API_COLOR(pal.text);
    snprintf(buf, sizeof(buf), "Launch: %s", quantShort((Quant)v.quant));
    API_CMD_TEXT(x, y, f->handle, 0, buf);
    y += 40; // the countdown (marks control) sits at posY + 28
    if (v.live)
    {
        API_COLOR(pal.red);
        API_CMD_TEXT(x, y, f->handle, 0, "LIVE: song frozen");
        snprintf(buf, sizeof(buf), "slot %d, btn 4: back", v.liveSlot);
        API_CMD_TEXT(x, y + 20, f->handle, 0, buf);
    }
    y += 52;
    API_COLOR(pal.text);
    if (v.focusPattern)
    {
        snprintf(buf, sizeof(buf), "Clip %d  track %d", v.focusPattern, v.focusTrack + 1);
        API_CMD_TEXT(x, y, f->handle, 0, buf);
        API_CMD_TEXT(x, y + 20, f->handle, 0, clipModeName(v.focusMode));
    }
    else
    {
        API_CMD_TEXT(x, y, f->handle, 0, "Press a clip pad");
    }
    if (plan.help) // only while button 1 or 3 is held
    {
        API_COLOR(v.modeHeld ? pal.text : pal.dim);
        API_CMD_TEXT(x, y + 52, f->handle, 0, "Wheel: loop / 1x");
        API_CMD_TEXT(x, y + 72, f->handle, 0, "Shift: whole row");
        API_COLOR(v.stopHeld ? pal.text : pal.dim);
        API_CMD_TEXT(x, y + 100, f->handle, 0, "Hold 3 + pad: stop");
        API_COLOR(pal.dim);
        API_CMD_TEXT(x, y + 120, f->handle, 0, "Empty pad = stop");
        API_CMD_TEXT(x, y + 148, f->handle, 0, "Notch = plays once");
        API_CMD_TEXT(x, y + 168, f->handle, 0, "red: next row empty");
    }
}

//--------------------------------------------------------------------------------
cSeqClipMarks::cSeqClipMarks(strControlProperties* properties)
  : cDisplayControl(controlIdSeqClipMarks, 1)
{
    colorsCount = 10;
    colors = display.getTheme().songPlayerColors;
    autoRefresh = false;
    if (properties == nullptr)
        return;
    posX = properties->x;
    posY = properties->y;
    width = properties->w;
    height = properties->h;
    style = properties->style;
    marks = (const Marks*)properties->data;
}

void cSeqClipMarks::update()
{
    if (marks == nullptr)
        return;
    const Marks& m = *marks;
    const Palette pal = palette(colors);

    API_LINE_WIDTH(16);
    // queued launches blink bright for half of the time
    if (m.blink)
    {
        API_COLOR(pal.text);
        API_BEGIN(RECTS);
        for (uint8_t t = 0; t < kTracks; t++)
            if (m.qRow[t] < kVisRows && m.qKind[t] == MK_BLINK)
                rect(colX(t), rowY(m.qRow[t]), colX(t) + kCellW - 1, rowY(m.qRow[t]) + kCellH - 1);
        if (m.sceneRow < kVisRows)
            rect(sceneX(), rowY(m.sceneRow), sceneX() + kSceneW - 1, rowY(m.sceneRow) + kCellH - 1);
        API_END();
    }
    // progress of the playing clips: a dark bar along the bottom of the cell
    API_COLOR(pal.black);
    API_BEGIN(RECTS);
    for (uint8_t t = 0; t < kTracks; t++)
        if (m.playRow[t] < kVisRows && m.prog[t] > 0)
            rect(colX(t) + 1, rowY(m.playRow[t]) + kCellH - 5, colX(t) + m.prog[t], rowY(m.playRow[t]) + kCellH - 3);
    API_END();
    // the row the transport plays: a bar on the left edge, in song and in LIVE
    if (m.songRow < kVisRows)
    {
        API_COLOR(pal.play);
        API_BEGIN(RECTS);
        rect(1, rowY(m.songRow), 3, rowY(m.songRow) + kCellH - 1);
        API_END();
    }
    // countdown to the next launch point
    if (m.countdown != 0xFF)
    {
        char buf[24];
        API_COLOR(pal.text);
        snprintf(buf, sizeof(buf), "next in %u steps", (unsigned)m.countdown);
        API_CMD_TEXT(kInfoX, posY + 28, fonts[0].handle, 0, buf);
    }
}
