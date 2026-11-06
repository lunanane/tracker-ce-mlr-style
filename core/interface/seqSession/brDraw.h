// Sequencer tracks mod (plan 021) - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Drawing of the beat repeat screen on the CUT / MLR page: header (page, mode, state), four bands = the four pad rows (name, value, the 8 pad
// cells as they are lit), the right block (LATCH / OFF / RESET), the captured slice with its waveform and the repeats as blocks (level =
// decay, colour = pitch decay), and the live playhead. Templates over the tiny drawing context of laneDraw.h (colour, alpha, rect, disc, text),
// so the firmware display list and the host tests run the same code. Tracker colours (seqPalette.h) on a plain dark background.
#pragma once
#include "laneDraw.h"
#include "seqBeatRepeatLogic.h"
#include "seqPalette.h"
#include <stdio.h>
#include <string.h>

namespace seqbr {

using seqcut::kHeaderY0;
using seqcut::kHeaderY1;
using seqcut::TX_LEFT;
using seqcut::TX_CENTERX;
using seqcut::TX_CENTERY;
using seqcut::TX_RIGHT;

namespace brdraw {

constexpr int kBandTop = 68, kBandStride = 58, kBandH = 54;
constexpr int kCellX = 196, kCellStride = 52, kCellW = 48, kCellH = 46;
constexpr int kSideX = 622;
constexpr int kViewX = 88, kViewW = kBrViewW, kWaveTop = 316, kWaveH = 56, kBlockTop = 378, kBlockH = 38;

inline uint32_t rowColor(int row)
{
    static const uint32_t c[4] = { seqpal::kNote, seqpal::kInstr, seqpal::kFx1, seqpal::kFx2 };
    return c[row & 3];
}

// the tracker colour of the repeat blocks shifts from teal to purple with the pitch drop
inline uint32_t blockColor(int pitchPercent, int k)
{
    int t = pitchPercent * k * 256 / 100 / 8;
    return seqpal::mix(seqpal::kNote, seqpal::kFx1, t > 256 ? 256 : t);
}

inline int blockX(int k) { return kViewX + k * (kViewW / kBrBlocks); }

template <class C> void chip(C& c, int x, int w, const char* word, bool on, uint32_t col)
{
    c.color(on ? col : seqpal::kCellNotes);
    c.rect(x, kHeaderY0 + 3, x + w, kHeaderY1 - 3);
    c.color(on ? seqpal::kBg : seqpal::kText);
    c.text(x + w / 2, (kHeaderY0 + kHeaderY1) / 2, 5, TX_CENTERX | TX_CENTERY, word);
}

template <class C> void side(C& c, int x, int y, int w, const char* word, bool on, bool dimmed, uint32_t col)
{
    c.color(on ? col : (dimmed ? seqpal::kCellEmpty : seqpal::kCellNotes));
    c.rect(x, y, x + w, y + 26);
    c.color(on ? seqpal::kBg : (dimmed ? seqpal::kGridHi : seqpal::kTitle));
    c.text(x + w / 2, y + 13, 5, TX_CENTERX | TX_CENTERY, word);
}

} // namespace brdraw

// The static part. Redrawn when the model changes (settings, state, a new capture).
template <class C> void drawBeatRepeat(C& c, const BrModel& m)
{
    using namespace brdraw;
    char buf[48];
    // ---- header
    c.color(seqpal::kTitle);
    c.text(12, (kHeaderY0 + kHeaderY1) / 2, 4, TX_CENTERY, "BEAT REPEAT");
    chip(c, 178, 74, "REPEAT", m.page == BG_REPEAT, seqpal::kNote);
    chip(c, 256, 56, "AUTO", m.page == BG_AUTO, seqpal::kInstr);
    chip(c, 322, 78, modeName(m.mode), true, m.mode == beatrepeat::MODE_MIX ? seqpal::kFx2 : (m.mode == beatrepeat::MODE_INSERT ? seqpal::kFx1 : seqpal::kInstr));
    const char* st = m.state == beatrepeat::S_PENDING ? "waiting" : (m.state == beatrepeat::S_CAPTURE ? "capturing" : (m.state == beatrepeat::S_REPEAT ? "REPEAT" : "idle"));
    if (m.state == beatrepeat::S_IDLE)
        snprintf(buf, sizeof(buf), "%s", st);
    else
        snprintf(buf, sizeof(buf), "%s %s%s", st, beatrepeat::gridName(m.eventGrid), m.state == beatrepeat::S_REPEAT ? "" : "");
    c.color(m.state == beatrepeat::S_REPEAT ? seqpal::kNote : seqpal::kText);
    c.text(414, (kHeaderY0 + kHeaderY1) / 2, 5, TX_CENTERY, buf);
    if (m.state == beatrepeat::S_REPEAT)
    {
        snprintf(buf, sizeof(buf), "#%d  -%d dB  -%d st", m.repeatIdx, m.levelDb, m.pitchSemi);
        c.color(seqpal::kTitle);
        c.text(520, (kHeaderY0 + kHeaderY1) / 2, 5, TX_CENTERY, buf);
    }
    else if (m.clamped)
    {
        c.color(seqpal::kInstr);
        c.text(520, (kHeaderY0 + kHeaderY1) / 2, 5, TX_CENTERY, "grid shortened for this tempo");
    }

    // ---- the four bands
    const uint8_t page = m.page;
    for (int row = 0; row < 4; row++)
    {
        const Param par = paramOf(page, (uint8_t)row);
        const Kind kind = kindOf(par);
        const int y0 = kBandTop + row * kBandStride;
        const uint32_t col = rowColor(row);
        c.color(seqpal::kCellEmpty);
        c.rect(6, y0, 794, y0 + kBandH);
        c.color(col);
        c.rect(6, y0, 10, y0 + kBandH);
        for (int i = 0; i < 8; i++)
        {
            const int x = kCellX + i * kCellStride, st = i + 1;
            bool lit = false, bright = false;
            if (kind == BK_MOMENTARY)
            {
                bright = ((m.heldMask >> i) & 1u) != 0 || (m.state != beatrepeat::S_IDLE && m.eventGrid == i);
                lit = bright;
            }
            else if (kind == BK_SELECT)
            {
                bright = m.step[par] == st;
                lit = bright;
            }
            else
            {
                lit = st <= m.step[par];
                bright = st == m.step[par];
            }
            c.color(bright ? col : (lit ? seqpal::mix(col, seqpal::kBg, 120) : seqpal::kCellNotes));
            c.rect(x, y0 + 4, x + kCellW, y0 + 4 + kCellH);
        }
        // the right block: LATCH / OFF on the first row (pads 11 and 12), RESET on the others (pad 12)
        if (row == 0)
        {
            side(c, kSideX, y0 + 4, 74, "LATCH", m.latch != 0, false, seqpal::kInstr);
            side(c, kSideX + 80, y0 + 4, 74, "OFF", m.state != beatrepeat::S_IDLE, false, 0xE0383E);
        }
        else
            side(c, kSideX + 80, y0 + 4, 74, "RESET", kind != BK_MOMENTARY && m.step[par] != BrParams::defaultStep(par), false, seqpal::kGridHi);
    }

    // ---- the slice: waveform of the captured slice, the grid cell, the repeats as blocks
    c.color(seqpal::kCellEmpty);
    c.rect(brdraw::kViewX, brdraw::kWaveTop, brdraw::kViewX + brdraw::kViewW, brdraw::kWaveTop + brdraw::kWaveH);
    if (m.waveOk)
    {
        c.color(seqpal::mix(seqpal::kNote, seqpal::kBg, 90));
        const int mid = brdraw::kWaveTop + brdraw::kWaveH / 2;
        for (int j = 0; j < kBrWave; j++)
        {
            int y0 = mid - (m.hi[j] * (brdraw::kWaveH / 2 - 2)) / 127, y1 = mid - (m.lo[j] * (brdraw::kWaveH / 2 - 2)) / 127;
            if (y1 <= y0)
                y1 = y0 + 1;
            c.rect(brdraw::kViewX + j * 8, y0, brdraw::kViewX + j * 8 + 7, y1);
        }
    }
    // the repeats: a block per repeat, its height the decay curve, its colour the pitch drop
    const uint8_t decay = percentOf(m.step[BP_DECAY]), pitch = percentOf(m.step[BP_PITCH]);
    for (int k = 0; k < kBrBlocks; k++)
    {
        const int h = (brdraw::kBlockH * blockLevel1000(decay, k)) / 1000;
        const bool past = m.state == beatrepeat::S_REPEAT && (int)m.repeatIdx > k;
        c.color(past ? brdraw::blockColor(pitch, k) : seqpal::mix(brdraw::blockColor(pitch, k), seqpal::kBg, 150));
        const int x = brdraw::blockX(k);
        c.rect(x + 2, brdraw::kBlockTop + brdraw::kBlockH - (h < 3 ? 3 : h), x + brdraw::kViewW / kBrBlocks - 4, brdraw::kBlockTop + brdraw::kBlockH);
    }
    // ---- texts last (every text ends the rectangle batch)
    for (int row = 0; row < 4; row++)
    {
        const Param par = paramOf(page, (uint8_t)row);
        const Kind kind = kindOf(par);
        const int y0 = kBandTop + row * kBandStride;
        const uint32_t col = rowColor(row);
        c.color(col);
        c.text(18, y0 + 12, 4, TX_LEFT, paramName(par));
        char v[16];
        if (kind == BK_MOMENTARY)
        {
            if (m.state != beatrepeat::S_IDLE)
                snprintf(v, sizeof(v), "%s", beatrepeat::gridName(m.eventGrid));
            else
                snprintf(v, sizeof(v), "hold");
        }
        else
            valueText(par, m.step[par], v, sizeof(v));
        c.color(seqpal::kTitle);
        c.text(18, y0 + 36, 5, TX_LEFT, v);
        LedState ls;
        memset(&ls, 0, sizeof(ls));
        ls.page = m.page;
        ls.mode = m.mode;
        ls.latch = m.latch;
        ls.heldMask = m.heldMask;
        ls.eventGrid = m.eventGrid;
        ls.state = m.state;
        memcpy(ls.step, m.step, sizeof(ls.step));
        for (int i = 0; i < 8; i++)
        {
            const LegendCell cell = legendCell((uint8_t)i, (uint8_t)row, ls);
            c.color(cell.on ? seqpal::kBg : seqpal::kText);
            c.text(kCellX + i * kCellStride + kCellW / 2, y0 + 4 + kCellH / 2, 5, TX_CENTERX | TX_CENTERY, cell.word);
        }
    }
    c.color(seqpal::kText);
    c.text(10, brdraw::kWaveTop + 8, 5, TX_LEFT, "SLICE");
    c.text(10, brdraw::kBlockTop + 8, 5, TX_LEFT, "REPEATS");
    for (int k = 0; k < kBrBlocks; k += 2)
    {
        const int semi = (int)(3.0f * (float)pitch / 100.0f * (float)k + 0.5f);
        if (semi > 0)
            snprintf(buf, sizeof(buf), "-%d st", semi > 24 ? 24 : semi);
        else
            snprintf(buf, sizeof(buf), "0 st");
        c.text(brdraw::blockX(k) + 6, brdraw::kBlockTop + 6, 5, TX_LEFT, buf);
    }
    snprintf(buf, sizeof(buf), "%d BPM", m.bpm10 / 10);
    c.text(704, brdraw::kWaveTop + 8, 5, TX_LEFT, buf);
    snprintf(buf, sizeof(buf), "CPU %d.%d %%", m.cpu10 / 10, m.cpu10 % 10);
    c.text(704, brdraw::kWaveTop + 26, 5, TX_LEFT, buf);
}

// The pad legend of the beat repeat layer (screen button Help): the 12 x 4 pads as cells with their words, the lit ones highlighted. Same
// geometry as the MLR legend. One line below names what the rows do on this page.
template <class C> void drawBrLegend(C& c, const BrModel& m)
{
    const int x0 = 98, y0 = 74, cw = 52, ch = 66;
    char buf[64];
    c.color(seqpal::kTitle);
    c.text(12, (kHeaderY0 + kHeaderY1) / 2, 4, TX_CENTERY, "BEAT REPEAT  pad legend");
    c.color(seqpal::kText);
    snprintf(buf, sizeof(buf), "page %s   mode %s   %s", pageName(m.page), modeName(m.mode), m.latch ? "LATCH" : "momentary");
    c.text(300, (kHeaderY0 + kHeaderY1) / 2, 5, TX_CENTERY, buf);
    LedState ls;
    memset(&ls, 0, sizeof(ls));
    ls.page = m.page;
    ls.mode = m.mode;
    ls.latch = m.latch;
    ls.heldMask = m.heldMask;
    ls.eventGrid = m.eventGrid;
    ls.state = m.state;
    memcpy(ls.step, m.step, sizeof(ls.step));
    for (int row = 0; row < 4; row++)
    {
        const Param par = paramOf(m.page, (uint8_t)row);
        for (int col = 0; col < kCols; col++)
        {
            const LegendCell cell = legendCell((uint8_t)col, (uint8_t)row, ls);
            const int x = x0 + col * (cw + 2) + (col >= 8 ? 10 : 0), y = y0 + row * (ch + 4);
            c.color(cell.on ? seqpal::mix(brdraw::rowColor(row), seqpal::kBg, 90) : (cell.word[0] ? seqpal::kCellNotes : seqpal::kBg));
            c.rect(x, y, x + cw, y + ch);
            c.color(cell.on ? 0xFFFFFF : (cell.word[0] ? seqpal::kTitle : seqpal::kGridHi));
            c.text(x + cw / 2, y + ch / 2, 5, TX_CENTERX | TX_CENTERY, cell.word);
        }
        c.color(brdraw::rowColor(row));
        c.text(8, y0 + row * (ch + 4) + 8, 5, TX_LEFT, paramName(par));
    }
    c.color(seqpal::kText);
    c.text(12, y0 + 4 * (ch + 4) + 6, 5, TX_LEFT, "pads 9-10: page REPEAT / AUTO, output MIX / REPL(ACE) / GATE    11: LATCH    12: OFF, RESET the row");
    c.text(12, y0 + 4 * (ch + 4) + 26, 5, TX_LEFT, "REPEAT: hold a grid pad = repeat at that grid, jam between pads. Stop ends any repeat. BeatRpt = back to MLR.");
}

// The moving part: the live playhead in the slice view and the repeat being played. Redrawn at most every kRedrawGapMs while something moves.
template <class C> void drawBrHeads(C& c, const BrHead& h)
{
    using namespace brdraw;
    if (h.x < 0)
        return;
    if (h.state == beatrepeat::S_REPEAT && h.repeatIdx > 0)
    {
        const int k = (h.repeatIdx - 1) % kBrBlocks;
        c.alpha(70);
        c.color(0xFFFFFF);
        c.rect(blockX(k) + 2, kBlockTop, blockX(k) + kViewW / kBrBlocks - 4, kBlockTop + kBlockH);
        c.alpha(255);
    }
    c.color(h.state == beatrepeat::S_CAPTURE ? 0xE0383E : 0xFFFFFF);
    c.rect(kViewX + h.x - 1, kWaveTop - 2, kViewX + h.x + 1, kWaveTop + kWaveH + 2);
}

} // namespace seqbr
