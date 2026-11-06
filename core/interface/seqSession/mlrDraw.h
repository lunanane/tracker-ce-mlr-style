// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Drawing of the MLR lane screen (plan 016 A): two slot lanes of 16 pads (the current section), the waveform of the section, step
// lines, loop bracket, cache dots, the names of the extra pads, the track picker and the pad legend. Templates over a tiny drawing
// context (same contract as laneDraw.h), so the firmware display list and the host tests / PNG preview run the same code.
// Colours are the tracker key colours (seqPalette.h): slot A = Note teal, slot B = FX1 purple, loop = Instrument yellow,
// ghost = FX2 blue, on a plain dark background.
#pragma once
#include "brDraw.h"
#include "laneDraw.h"
#include "seqMlrLogic.h"
#include "seqPalette.h"
#include <string.h>

namespace seqcut {
namespace mlr {

constexpr uint32_t kRed = 0xE0383E;
inline uint32_t slotColor(int k) { return k == 0 ? seqpal::kNote : seqpal::kFx1; }
inline int laneTop(int k) { return kMlrLaneTop + k * kMlrLaneStride; }

// The names of the eight extra pads of a slot (2 columns x 4 rows, in the order of the pads: STOP REV / RATE RSYNC / GRID SECT / REC MUTE)
template <class C> void extras(C& c, const SlotInfo& s, int k, bool texts)
{
    const int top = laneTop(k);
    SlotLeds sl;
    memset(&sl, 0, sizeof(sl));
    sl.v.flags = s.flags;
    sl.v.flags2 = s.flags2;
    sl.v.pads = s.pads;
    sl.muted = s.muted;
    sl.rec = s.rec;
    for (int i = 0; i < 8; i++)
    {
        const LegendCell cell = legendCell((uint8_t)(kPosCols + i % 4), (uint8_t)(i / 4), sl);
        const int x = kMlrBlockX + (i & 1) * 49, y = top + 4 + (i >> 1) * 38;
        if (!texts)
        {
            c.color(cell.on ? seqpal::mix(slotColor(k), seqpal::kBg, 150) : seqpal::kCellEmpty);
            c.rect(x, y, x + 47, y + 34);
        }
        else
        {
            c.color(cell.on ? 0xFFFFFF : seqpal::kText);
            c.text(x + 23, y + 17, 5, TX_CENTERX | TX_CENTERY, cell.word);
        }
    }
}

template <class C> void lane(C& c, const SlotInfo& s, int k)
{
    const int top = laneTop(k), bot = top + kMlrLaneH;
    const uint32_t col = slotColor(k);
    const bool stopped = (s.flags & VF_STOPPED) != 0;
    c.color(seqpal::kCellEmpty);
    c.rect(kMlrLaneX, top, kMlrLaneX + kMlrLaneW, bot);

    // the waveform of the section: thin dim envelope of the slot colour
    if (s.waveOk && !s.picking)
    {
        c.color(seqpal::faint(col));
        const int mid = top + 80;
        for (int j = 0; j + 1 < kWaveCols; j += 2) // 152 columns of the overview, drawn two at a time (display list budget)
        {
            const WaveCol& w = s.wave[j];
            const WaveCol& w2 = s.wave[j + 1];
            const int hi = w.hi > w2.hi ? w.hi : w2.hi, lo = w.lo < w2.lo ? w.lo : w2.lo;
            int y0 = mid - (hi * 54) / 127, y1 = mid - (lo * 54) / 127;
            if (y1 <= y0) y1 = y0 + 1;
            c.rect(kMlrLaneX + j * 4, y0, kMlrLaneX + j * 4 + 7, y1);
        }
    }
    // inactive pads (behind the end of the pattern)
    if (s.pads < kMlrPads)
    {
        c.color(seqpal::kBg);
        c.rect(kMlrLaneX + s.pads * kMlrPadW, top, kMlrLaneX + kMlrLaneW, bot);
    }
    // the 16 step boundaries (the beat lines a little brighter)
    for (int i = 0; i <= kMlrPads; i++)
    {
        c.color((i % 4 == 0) ? seqpal::kGridHi : seqpal::kGrid);
        c.rect(kMlrLaneX + i * kMlrPadW - (i == kMlrPads ? 1 : 0), top, kMlrLaneX + i * kMlrPadW + (i == kMlrPads ? 0 : 1), bot);
    }
    char buf[32];
    // note marks and cache dots
    for (int i = 0; i < kMlrPads && i < s.pads; i++)
    {
        const int x = kMlrLaneX + i * kMlrPadW;
        if ((s.noteMask >> i) & 1u)
        {
            c.color(col);
            c.rect(x + 6, bot - 22, x + kMlrPadW - 6, bot - 18);
        }
        if ((s.cacheMask >> i) & 1u) // a dot per cached cue
        {
            c.color(col);
            c.disc(x + kMlrPadW / 2, bot - 8, 3);
        }
    }
    // loop bracket
    if (s.loopLo <= s.loopHi && s.loopHi < kMlrPads)
    {
        const int x0 = kMlrLaneX + s.loopLo * kMlrPadW, x1 = kMlrLaneX + (s.loopHi + 1) * kMlrPadW;
        c.color(seqpal::kInstr);
        c.rect(x0, top, x1, top + 3);
        c.rect(x0, top, x0 + 2, top + 16);
        c.rect(x1 - 2, top, x1, top + 16);
    }
    if (stopped)
    {
        c.alpha(170);
        c.color(seqpal::kBg);
        c.rect(kMlrLaneX, top, kMlrLaneX + kMlrLaneW, bot);
        c.alpha(255);
    }
    if (s.flags & VF_REC)
    {
        c.color(kRed);
        c.rect(kMlrLaneX, top, kMlrLaneX + kMlrLaneW, top + 2);
        c.rect(kMlrLaneX, bot - 2, kMlrLaneX + kMlrLaneW, bot);
    }
    // the track picker: the 16 pads are the tracks (cells here, their words with the other texts below)
    if (s.picking)
    {
        for (int i = 0; i < kMlrPads; i++)
        {
            const int x = kMlrLaneX + i * kMlrPadW;
            const bool me = i == s.track, has = (s.has >> i) & 1u;
            c.color(me ? col : (has ? seqpal::kCellNotes : seqpal::kBg));
            c.rect(x + 2, top + 30, x + kMlrPadW - 2, top + 100);
        }
    }
    extras(c, s, k, false);

    // all texts last: every text ends the rectangle batch of the display list
    c.color(seqpal::kText);
    if (!s.picking)
        for (int i = 0; i < kMlrPads && i < s.pads; i += 4) // 1, 5, 9, 13 (the legend names every pad)
        {
            snprintf(buf, sizeof(buf), "%d", i + 1);
            c.text(kMlrLaneX + i * kMlrPadW + 3, top + 4, 5, TX_LEFT, buf);
        }
    if (s.picking)
    {
        for (int i = 0; i < kMlrPads; i++)
        {
            const bool me = i == s.track, has = (s.has >> i) & 1u;
            c.color(me ? seqpal::kBg : (has ? seqpal::kTitle : seqpal::kGridHi));
            snprintf(buf, sizeof(buf), "T%d", i + 1);
            c.text(kMlrLaneX + i * kMlrPadW + kMlrPadW / 2, top + 65, 4, TX_CENTERX | TX_CENTERY, buf);
        }
        c.color(0xFFFFFF);
        snprintf(buf, sizeof(buf), "Slot %d: pick a track", k + 1);
        c.text(kMlrLaneX + kMlrLaneW / 2, top + 120, 4, TX_CENTERX | TX_CENTERY, buf);
    }

    // label column: slot, track, grid, section, what is special
    c.color(col);
    snprintf(buf, sizeof(buf), "%c", 'A' + k);
    c.text(10, top + 4, 4, TX_LEFT, buf);
    c.color(seqpal::kTitle);
    snprintf(buf, sizeof(buf), "T%d", s.track + 1);
    c.text(34, top + 4, 4, TX_LEFT, buf);
    c.color(seqpal::kText);
    snprintf(buf, sizeof(buf), "grid %s", gridName((s.flags2 & V2_GRID8) ? GRID_8 : GRID_4));
    c.text(10, top + 34, 5, TX_LEFT, buf);
    snprintf(buf, sizeof(buf), "sec %d/%d%s", s.sec + 1, s.nsec < 1 ? 1 : s.nsec, (s.flags2 & V2_HOLD) ? " H" : "");
    c.text(10, top + 52, 5, TX_LEFT, buf);
    const char* st = (s.flags & VF_STOPPED) ? "STOP" : ((s.flags & VF_REC) ? "REC" : ((s.flags & VF_RECORDED) ? "REPLAY" : (s.loopLo <= s.loopHi ? "LOOP" : ((s.flags & VF_ACTIVE) ? "OFF GRID" : ""))));
    if (st[0]) c.text(10, top + 72, 5, TX_LEFT, st);
    if (s.muted) c.text(10, top + 90, 5, TX_LEFT, "MUTED");
    const uint8_t rate = viewRate(View{ 0, 1, 0, s.flags, 0, 0, 0, 0 });
    if ((s.flags & VF_REVERSE) || rate != 1)
        c.text(10, top + 108, 5, TX_LEFT, (s.flags & VF_REVERSE) ? (rate == 0 ? "REV 1/2" : (rate == 2 ? "REV 2x" : "REV")) : (rate == 0 ? "1/2 speed" : "2x speed"));
    extras(c, s, k, true);
}

// The pad legend: the 12 x 4 pads as cells with their words; states highlighted. Slot names at the left.
template <class C> void legend(C& c, const MlrModel& m)
{
    const int x0 = 56, y0 = 74, cw = 58, ch = 80;
    for (int row = 0; row < kRows; row++)
    {
        const int k = row / 2;
        const SlotInfo& s = m.slot[k];
        SlotLeds sl;
        memset(&sl, 0, sizeof(sl));
        sl.v.flags = s.flags;
        sl.v.flags2 = s.flags2;
        sl.v.pads = s.pads;
        sl.v.col = 0xFF;
        sl.track = s.track;
        sl.picking = s.picking;
        sl.muted = s.muted;
        sl.rec = s.rec;
        sl.has = s.has;
        if (m.legendPad[k] != 0xFF) sl.v.col = m.legendPad[k];
        if ((row & 1) == 0)
        {
            char nm[12];
            c.color(slotColor(k));
            snprintf(nm, sizeof(nm), "%c", 'A' + k);
            c.text(14, y0 + row * (ch + 4) + 6, 4, TX_LEFT, nm);
            c.color(seqpal::kTitle);
            snprintf(nm, sizeof(nm), "T%d", s.track + 1);
            c.text(14, y0 + row * (ch + 4) + 30, 4, TX_LEFT, nm);
        }
        for (int col = 0; col < kCols; col++)
        {
            const LegendCell cell = legendCell((uint8_t)col, (uint8_t)row, sl);
            const int x = x0 + col * (cw + 2) + (col >= kPosCols ? 10 : 0), y = y0 + row * (ch + 4) + ((row & 1) ? 0 : 0);
            c.color(cell.on ? seqpal::mix(slotColor(k), seqpal::kBg, 120) : (cell.dim ? seqpal::kBg : seqpal::kCellNotes));
            c.rect(x, y, x + cw, y + ch);
            c.color(cell.on ? 0xFFFFFF : (cell.dim ? seqpal::kGridHi : seqpal::kTitle));
            c.text(x + cw / 2, y + ch / 2, (cell.word[0] >= '0' && cell.word[0] <= '9') ? 4 : 5, TX_CENTERX | TX_CENTERY, cell.word);
        }
    }
}

} // namespace mlr

// The static part: header, the two lanes (or the legend). Redrawn on events.
template <class C> void drawMlr(C& c, const MlrModel& m)
{
    const int mid = (kHeaderY0 + kHeaderY1) / 2;
    if (m.recHeld)
    {
        c.color(mlr::kRed);
        c.rect(0, kHeaderY0, 800, kHeaderY1);
    }
    char buf[64];
    if (m.brMode)
    {
        if (m.legend)
            seqbr::drawBrLegend(c, m.br);
        else
            seqbr::drawBeatRepeat(c, m.br);
        return;
    }
    c.color(m.recHeld ? 0xFFFFFF : seqpal::kTitle);
    c.text(12, mid, 4, TX_CENTERY, m.legend ? "MLR  pad legend" : "MLR");
    if (m.recHeld)
    {
        c.disc(360, mid, 6);
        c.text(374, mid, 4, TX_CENTERY, "REC");
        c.text(440, mid, 5, TX_CENTERY, "play, press Rec again to loop it");
    }
    else
    {
        c.color(seqpal::kText);
        snprintf(buf, sizeof(buf), "A: T%d %s    B: T%d %s", m.slot[0].track + 1, gridName((m.slot[0].flags2 & V2_GRID8) ? GRID_8 : GRID_4),
                 m.slot[1].track + 1, gridName((m.slot[1].flags2 & V2_GRID8) ? GRID_8 : GRID_4));
        c.text(m.legend ? 270 : 150, mid, 5, TX_CENTERY, buf);
    }
    if (m.legend)
    {
        mlr::legend(c, m);
        return;
    }
    for (int k = 0; k < kSlots; k++)
        mlr::lane(c, m.slot[k], k);
}

// The moving part: playhead pad, grid ghost, playhead line, bar.beat. Redrawn every kRedrawGapMs while something moves.
template <class C> void drawMlrHeads(C& c, const MlrHeadModel& m)
{
    if (m.brMode)
    {
        if (!m.legend)
            seqbr::drawBrHeads(c, m.br);
        char bb[24];
        snprintf(bb, sizeof(bb), "bar %d.%d", m.bar, m.beat);
        c.color(seqpal::kTitle);
        c.text(788, (kHeaderY0 + kHeaderY1) / 2, 4, TX_RIGHT | TX_CENTERY, bb);
        return;
    }
    for (int k = 0; k < kSlots && !m.legend; k++)
    {
        const MlrHead& h = m.h[k];
        if (h.x < 0) continue;
        const int top = mlr::laneTop(k), bot = top + kMlrLaneH;
        const bool stopped = (h.flags & VF_STOPPED) != 0;
        if (h.pad != 0xFF)
        {
            c.alpha(40);
            c.color(mlr::slotColor(k));
            c.rect(kMlrLaneX + h.pad * kMlrPadW, top, kMlrLaneX + (h.pad + 1) * kMlrPadW, bot);
            c.alpha(255);
        }
        if (h.ghost >= 0)
        {
            const int gx = kMlrLaneX + h.ghost;
            c.color(seqpal::kFx2);
            c.rect(gx - 2, top + 20, gx + 3, top + 23);
            c.rect(gx, top + 23, gx + 1, top + 40);
        }
        const int x = kMlrLaneX + h.x;
        c.color(stopped ? seqpal::kText : 0xFFFFFF);
        c.rect(x - 1, top - 2, x + 1, bot + 2);
    }
    char buf[24];
    snprintf(buf, sizeof(buf), "bar %d.%d", m.bar, m.beat);
    c.color(m.recHeld ? 0xFFFFFF : seqpal::kTitle);
    c.text(788, (kHeaderY0 + kHeaderY1) / 2, 4, TX_RIGHT | TX_CENTERY, buf);
}

} // namespace seqcut
