// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Clip launcher screen (plan 005 R2). Shapes and ASCII text only (the fonts have no symbols). Display list budget: the whole frame
// may use about 8 KB (display.cpp step3), shared with the title bar and the 8 labels, so the grid draws cells in one RECTS batch per
// colour (8 bytes per cell), numbers and a few text lines; planGrid() (seqSessionLogic.h) sums the cost from the View first and drops the optional parts that do not fit.
#include "FT812.h"
#include "display.h"
#include "displayControls.h"
#include "seqSession/clipGrid.h"
#include "cr_section_macros.h"
#include "seqPalette.h"
#include "seqTraceStages.h"
#include <stdio.h>
#include <string.h>

using namespace seqsession;
using namespace seqsession::clipgrid;

enum { kGridOwnBytes = 4 + 4 }; // CMD_DLSTART of display.cpp + a batch reopened at the start of stage 1

namespace
{
// plan 013 C: a clean dark ground, thin outlines, state by colour from the tracker key palette (seqPalette.h):
//   playing = Note teal (dark teal cell + bright outline and progress line in the marks control), queued = Instrument yellow (blinking
//   outline), armed follow action = FX1 pink outline, Play 1x = yellow notch, row end = pink notch, pad window frame = FX2 blue,
//   scene column = dark blue cells, song row bar = FX2 blue. Text and dim colours come from the theme (songPlayerColors).
// theme colours (songPlayerColors): 0 text, 4 dim, 5 red (status messages only)
struct Palette
{
    uint32_t text, dim, red;
};

Palette palette(const uint32_t* c)
{
    Palette p;
    p.text = c[0];
    p.dim = c[4];
    p.red = c[5];
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

inline void drawRect(int x0, int y0, int x1, int y1) { rect(x0, y0, x1, y1); }
inline void drawOutline(int x0, int y0, int x1, int y1) { outline(x0, y0, x1, y1); }

bool tinted(uint8_t color) { return color > 0 && color < seqpal::kSceneColors; }

bool fillsAs(uint8_t cell, uint8_t cls) // cls 0 empty, 1 notes (also queued / follow), 2 playing
{
    uint8_t c = cell == CELL_PLAYING ? 2 : (cell == CELL_EMPTY ? 0 : 1);
    return c == cls;
}

// v0.3.9: the commands of the grid with their FIFO sizes (FT812.cpp), sent to stage 0 until the next one would pass
// tracestage::kStageBytes, from there on to stage 1 (stage -1: all, host check). A RECTS batch cut by the stage end is reopened at the
// start of stage 1 (the two stage DLs are concatenated, colour and width carry over). Nothing is ever sent beyond the second stage.
struct Gate
{
    int stage = 0;
    int used[2] = { kGridOwnBytes, kGridOwnBytes };
    int dropped = 0;
    bool cut = false;
    uint8_t prim = 0; // RECTS batch open in the single stage drawing
    bool take(int bytes, bool opensBatch = false)
    {
        if (stage < 0)
            return true;
        if (!cut && used[0] + bytes <= tracestage::kStageBytes)
        {
            used[0] += bytes;
            return stage == 0;
        }
        if (!cut)
        {
            cut = true;
            if (prim && !opensBatch) // stage 1 starts inside a batch: open it again there
            {
                used[1] += 4;
                if (stage == 1)
                    API_BEGIN(prim);
            }
        }
        if (dropped == 0 && used[1] + bytes <= tracestage::kStageBytes)
        {
            used[1] += bytes;
            return stage == 1;
        }
        dropped += bytes;
        return false;
    }
    void color(uint32_t c) { if (take(4)) API_COLOR(c); }
    void width(int w) { if (take(4)) API_LINE_WIDTH(w); }
    void begin(uint8_t p)
    {
        if (take(4, true)) API_BEGIN(p);
        prim = p;
    }
    void end()
    {
        if (take(4)) API_END();
        prim = 0;
    }
    void rect(int x0, int y0, int x1, int y1) { if (take(8)) drawRect(x0, y0, x1, y1); }
    void outline(int x0, int y0, int x1, int y1) { if (take(28)) drawOutline(x0, y0, x1, y1); }
    void number(int x, int y, int font, uint16_t opt, int n) { if (take(16)) API_CMD_NUMBER(x, y, font, opt, n); }
    void text(int x, int y, int font, uint16_t opt, const char* s) { if (take(tracestage::textBytes(s))) API_CMD_TEXT(x, y, font, opt, s); }
};

// the View both stages draw (POD, raw storage: no constructor in EXTERNAL_RAM)
__NOINIT(EXTERNAL_RAM) View s_snap;
} // namespace

//--------------------------------------------------------------------------------
cSeqClipGrid::cSeqClipGrid(strControlProperties* properties)
  : cDisplayControl(controlIdSeqClipGrid, tracestage::kStages)
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

// v0.3.9: two display stages (seqTraceStages.h): one stage is one burst into the 4 KB FT81x command FIFO and the busiest launcher is
// above it. Both stages draw the snapshot of the View taken by stage 0 (the session may change its View in between).
void cSeqClipGrid::update()
{
    if (view == nullptr)
        return;
    memcpy(&s_snap, view, sizeof(View));
    drawStage(0);
}

void cSeqClipGrid::update2()
{
    if (view == nullptr)
        return;
    drawStage(1);
}

void cSeqClipGrid::drawStage(int stage)
{
    const View& v = s_snap;
    Gate g;
    g.stage = stage;
    const GridPlan plan = planGrid(v); // what fits in the display list budget (summed, never read back from the driver)
    const Palette pal = palette(colors);
    const strFont* f = &fonts[0];                              // 14 px regular
    const strFont* fs = &fonts[displayFontCount > 4 ? 4 : 0];  // 12 px regular for the numbers
    char buf[32];

    // ---- header: track numbers (the tracks of the pad window bright) and row numbers (the rows of the window bright)
    for (uint8_t t = 0; t < kTracks; t++)
    {
        bool inWin = t >= v.winTrack && t < v.winTrack + 8;
        g.color(inWin ? pal.text : pal.dim);
        g.number(colX(t) + kCellW / 2, posY + 18, fs->handle, OPT_CENTERX | OPT_CENTERY, t + 1);
    }
    int winRow = (int)v.winFirst - (int)v.top;
    for (uint8_t r = 0; r < kVisRows; r++)
    {
        bool inWin = (int)r >= winRow && (int)r < winRow + kWinRows;
        g.color(inWin ? pal.text : pal.dim);
        g.number(kRowNumX + 18, rowY(r) + kCellH / 2, fs->handle, OPT_CENTERX | OPT_CENTERY, v.top + r);
    }

    // ---- cells: one batch per colour (three states for the clips, three for the scene column)
    g.width(16);
    const uint32_t fill[3] = { seqpal::kCellEmpty, seqpal::kCellNotes, seqpal::kCellPlaying };
    const uint32_t sceneFill[3] = { seqpal::kSceneEmpty, seqpal::kSceneNotes, seqpal::kScenePlaying };
    // Scene colours (plan 017): the clips with content of a coloured row and its scene cell are drawn in the row colour's own batches instead
    // (same rects, so the cost is the batches only); empty and playing clips keep their state fills so playing stays readable.
    for (uint8_t cls = 0; cls < 3; cls++)
    {
        g.color(fill[cls]);
        g.begin(RECTS);
        for (uint8_t r = 0; r < kVisRows; r++)
            for (uint8_t t = 0; t < kTracks; t++)
                if (fillsAs(v.cell[r][t], cls) && !(cls == 1 && tinted(v.sceneColor[r])))
                    g.rect(colX(t), rowY(r), colX(t) + kCellW - 1, rowY(r) + kCellH - 1);
        g.end();
        g.color(sceneFill[cls]);
        g.begin(RECTS);
        for (uint8_t r = 0; r < kVisRows; r++)
            if (fillsAs(v.scene[r], cls) && !tinted(v.sceneColor[r]))
                g.rect(sceneX(), rowY(r), sceneX() + kSceneW - 1, rowY(r) + kCellH - 1);
        g.end();
    }
    for (uint8_t c = 1; c < seqpal::kSceneColors; c++)
    {
        bool any = false;
        for (uint8_t r = 0; r < kVisRows; r++)
            any = any || v.sceneColor[r] == c;
        if (!any)
            continue;
        g.color(seqpal::sceneClipFill(c));
        g.begin(RECTS);
        for (uint8_t r = 0; r < kVisRows; r++)
            if (v.sceneColor[r] == c)
                for (uint8_t t = 0; t < kTracks; t++)
                    if (fillsAs(v.cell[r][t], 1))
                        g.rect(colX(t), rowY(r), colX(t) + kCellW - 1, rowY(r) + kCellH - 1);
        g.end();
        for (uint8_t cls = 0; cls < 3; cls++)
        {
            g.color(seqpal::sceneCellFill(c, cls));
            g.begin(RECTS);
            for (uint8_t r = 0; r < kVisRows; r++)
                if (v.sceneColor[r] == c && fillsAs(v.scene[r], cls))
                    g.rect(sceneX(), rowY(r), sceneX() + kSceneW - 1, rowY(r) + kCellH - 1);
            g.end();
        }
        // a 2 px line in the colour under the whole row (in the gap between the rows)
        g.color(seqpal::sceneRgb(c));
        g.begin(RECTS);
        for (uint8_t r = 0; r < kVisRows; r++)
            if (v.sceneColor[r] == c)
                g.rect(colX(0), rowY(r) + kCellH + 1, sceneX() + kSceneW - 1, rowY(r) + kCellH + 2);
        g.end();
    }

    // ---- armed follow actions: a steady pink outline (the blink of a launch the performer queued is in the marks control)
    if (plan.outlines)
    {
        g.width(16);
        g.color(seqpal::kFx1);
        for (uint8_t r = 0; r < kVisRows; r++)
            for (uint8_t t = 0; t < kTracks; t++)
                if (v.cell[r][t] == CELL_FOLLOW)
                    g.outline(colX(t), rowY(r), colX(t) + kCellW - 1, rowY(r) + kCellH - 1);
    }

    // ---- dim square: an empty cell of a sounding track inside the pad window - pressing it stops the track
    if (plan.stops)
    {
        g.width(16);
        g.color(pal.dim);
        g.begin(RECTS);
        for (uint8_t r = 0; r < kVisRows; r++)
            for (uint8_t t = 0; t < kTracks; t++)
                if (v.flags[r][t] & F_STOPMARK)
                    g.rect(colX(t) + kCellW / 2 - 3, rowY(r) + kCellH / 2 - 3, colX(t) + kCellW / 2 + 3, rowY(r) + kCellH / 2 + 3);
        g.end();
    }
    // ---- notch top right of a clip with content: Play 1x. Red: the next row is empty, so the track stops after this clip.
    if (plan.notches)
    {
        g.width(16);
        for (uint8_t pass = 0; pass < 2; pass++)
        {
            g.color(pass == 0 ? seqpal::kInstr : seqpal::kFx1);
            g.begin(RECTS);
            for (uint8_t r = 0; r < kVisRows; r++)
            {
                for (uint8_t t = 0; t < kTracks; t++)
                {
                    uint8_t fl = v.flags[r][t];
                    bool one = (fl & F_ONEX) != 0, end = (fl & F_END) != 0;
                    if (one && (pass == 1) == end)
                        g.rect(colX(t) + kCellW - 9, rowY(r) + 2, colX(t) + kCellW - 3, rowY(r) + 8);
                }
                if ((v.sceneFlags[r] & F_ONEX) && pass == 0) // scene "play next": a bar along the top (the clip notch is a small square)
                    g.rect(sceneX() + 4, rowY(r) + 2, sceneX() + kSceneW - 5, rowY(r) + 5);
            }
            g.end();
        }
    }

    // ---- focused clip (screen button 1 + wheel changes its mode)
    if (v.focusPattern >= v.top && v.focusPattern < v.top + kVisRows)
    {
        g.color(pal.text);
        g.width(16);
        uint8_t r = (uint8_t)(v.focusPattern - v.top);
        g.outline(colX(v.focusTrack) + 1, rowY(r) + 1, colX(v.focusTrack) + kCellW - 2, rowY(r) + kCellH - 2);
    }

    // ---- pad window: a thin blue frame round the 8 x 4 clips and one round their scene pads
    g.color(seqpal::kFx2);
    g.width(24);
    if (winRow + kWinRows > 0 && winRow < (int)kVisRows)
    {
        int r0 = winRow < 0 ? 0 : winRow;
        int r1 = winRow + kWinRows - 1;
        if (r1 > (int)kVisRows - 1) r1 = kVisRows - 1;
        g.outline(colX(v.winTrack) - 3, rowY((uint8_t)r0) - 3, colX(v.winTrack + 7) + kCellW + 2, rowY((uint8_t)r1) + kCellH + 2);
        g.outline(sceneX() - 3, rowY((uint8_t)r0) - 3, sceneX() + kSceneW + 2, rowY((uint8_t)r1) + kCellH + 2);
    }

    // ---- status line
    if (v.msg)
    {
        g.color(pal.red);
        g.text(kX0, kY0 + kVisRows * kRowH + 8, f->handle, 0, msgText(v.msg));
    }

    // ---- info column (x 616-796: at most about 23 characters per line)
    int x = kInfoX, y = posY + 8;
    g.color(pal.text);
    snprintf(buf, sizeof(buf), "Launch: %s", quantShort((Quant)v.quant));
    g.text(x, y, f->handle, 0, buf);
    y += 40; // the countdown (marks control) sits at posY + 28
    if (v.focusPattern) // the scene flag of the focused row (plan 006)
    {
        g.color(pal.text);
        snprintf(buf, sizeof(buf), "Scene %d: %s", v.focusPattern, v.focusScene ? "PLAY NEXT" : "LOOP");
        g.text(x, y, f->handle, 0, buf);
    }
    y += 52;
    g.color(pal.text);
    if (v.focusPattern)
    {
        snprintf(buf, sizeof(buf), "Clip %d  track %d", v.focusPattern, v.focusTrack + 1);
        g.text(x, y, f->handle, 0, buf);
        g.text(x, y + 20, f->handle, 0, clipModeName(v.focusMode));
    }
    else
    {
        g.text(x, y, f->handle, 0, "Press a clip pad");
    }
    if (plan.help) // only while button 1 or 3 is held
    {
        g.color(v.modeHeld ? pal.text : pal.dim);
        g.text(x, y + 52, f->handle, 0, "Wheel: clip loop / 1x");
        g.text(x, y + 72, f->handle, 0, "Shift: scene play next");
        g.color(v.stopHeld ? pal.text : pal.dim);
        g.text(x, y + 100, f->handle, 0, "Hold 3 + pad: stop");
        g.color(pal.dim);
        g.text(x, y + 120, f->handle, 0, "Empty pad = stop");
        g.text(x, y + 148, f->handle, 0, "Notch = plays once");
        g.text(x, y + 168, f->handle, 0, "red: next row empty");
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
    // playing clips: a thin teal outline round the cell
    API_COLOR(seqpal::kNote);
    for (uint8_t t = 0; t < kTracks; t++)
        if (m.playRow[t] < kVisRows)
            outline(colX(t), rowY(m.playRow[t]), colX(t) + kCellW - 1, rowY(m.playRow[t]) + kCellH - 1);
    // queued launches: a yellow outline that blinks for half of the time
    if (m.blink)
    {
        API_COLOR(seqpal::kInstr);
        API_LINE_WIDTH(24);
        for (uint8_t t = 0; t < kTracks; t++)
            if (m.qRow[t] < kVisRows && m.qKind[t] == MK_BLINK)
                outline(colX(t), rowY(m.qRow[t]), colX(t) + kCellW - 1, rowY(m.qRow[t]) + kCellH - 1);
        if (m.sceneRow < kVisRows)
            outline(sceneX(), rowY(m.sceneRow), sceneX() + kSceneW - 1, rowY(m.sceneRow) + kCellH - 1);
        API_LINE_WIDTH(16);
    }
    // progress of the playing clips: a bright teal line along the bottom of the cell
    API_COLOR(seqpal::kNote);
    API_BEGIN(RECTS);
    for (uint8_t t = 0; t < kTracks; t++)
        if (m.playRow[t] < kVisRows && m.prog[t] > 0)
            rect(colX(t) + 1, rowY(m.playRow[t]) + kCellH - 5, colX(t) + m.prog[t], rowY(m.playRow[t]) + kCellH - 3);
    API_END();
    // the row the transport plays: a bar on the left edge, in song and in LIVE
    if (m.songRow < kVisRows)
    {
        API_COLOR(seqpal::kFx2);
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
