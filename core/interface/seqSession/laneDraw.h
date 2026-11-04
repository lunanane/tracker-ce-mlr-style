// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Drawing of the CUT (MLR) lane screen (plan 005 R4) as templates over a tiny drawing context, so the same code runs on the
// display list of the firmware (laneView.cpp) and in the host tests (op counting for the display-list budget, preview dump).
//
// Context C provides (screen 800 x 480, pixels; rect x1/y1 are exclusive):
//   void color(uint32_t rgb);  void alpha(uint8_t a);        // alpha 255 = opaque
//   void rect(int x0, int y0, int x1, int y1);               // filled rectangle
//   void disc(int cx, int cy, int r);                        // filled circle
//   void text(int x, int y, int font, int opt, const char*)  // font 1..5 (controlStyleFont<n>), opt = TextOpt bits
//
// The screen is a map of the pads: lane = pad row, 8 segments = pad columns 1..8, right block = pad columns 9..12.
// ASCII-only fonts: every symbol is a shape. Stays below the stock title bar (y 0..28).
#pragma once
#include "seqCutLogic.h"
#include <stdio.h>

namespace seqcut {

enum TextOpt : uint8_t { TX_LEFT = 0, TX_CENTERX = 1, TX_CENTERY = 2, TX_RIGHT = 4 };
enum { kHeaderY0 = 32, kHeaderY1 = 62 };

namespace draw {

// Triangle made of 9 stripes (points right, or left).
template <class C> void triangle(C& c, int cx, int cy, bool right)
{
    for (int dy = -8; dy <= 8; dy += 2)
    {
        int len = 18 - 2 * (dy < 0 ? -dy : dy); // 18 at the middle, 2 at the edges
        int x0 = right ? cx - 9 : cx + 9 - len;
        c.rect(x0, cy + dy, x0 + len, cy + dy + 2);
    }
}

template <class C> void rightBlock(C& c, const LaneInfo& l, int top, const Palette& p)
{
    const bool stopped = (l.flags & VF_STOPPED) != 0;
    const bool rev = (l.flags & VF_REVERSE) != 0;
    const View v = { 0, 1, 0, l.flags };
    const uint8_t rate = viewRate(v);
    const int cx0 = kCellX, cx1 = kCellX + 48, cw = 44, ch = 36;
    const int cy0 = top + 2, cy1 = top + 42;
    // cell backgrounds: STOP | REV on the first row, RATE | RESYNC on the second (the pad columns 9..12 in reading order)
    c.color(p.lane);
    c.rect(cx0, cy0, cx0 + cw, cy0 + ch);
    c.rect(cx1, cy0, cx1 + cw, cy0 + ch);
    c.rect(cx0, cy1, cx0 + cw, cy1 + ch);
    c.rect(cx1, cy1, cx1 + cw, cy1 + ch);
    // STOP: a filled square when stopped, otherwise a dim "playing" triangle
    if (stopped)
    {
        c.color(p.text);
        c.rect(cx0 + cw / 2 - 8, cy0 + ch / 2 - 8, cx0 + cw / 2 + 8, cy0 + ch / 2 + 8);
    }
    else
    {
        c.color(p.textDim);
        triangle(c, cx0 + cw / 2, cy0 + ch / 2, true);
    }
    // REVERSE: triangle pointing back (bright) or forward (dim)
    c.color(rev ? p.text : p.textDim);
    triangle(c, cx1 + cw / 2, cy0 + ch / 2, !rev);
    // RATE: 1/2, 1x, 2x
    c.color(rate == 1 ? p.textDim : p.text);
    c.text(cx0 + cw / 2, cy1 + ch / 2, 4, TX_CENTERX | TX_CENTERY, rate == 0 ? "1/2" : (rate == 1 ? "1x" : "2x"));
    // RESYNC: red = recording a take, ring = a recorded loop replays, filled = off the grid, dim = on the grid
    const int rx = cx1 + cw / 2, ry = cy1 + ch / 2;
    if (l.flags & VF_REC)
    {
        c.color(p.rec);
        c.disc(rx, ry, 9);
    }
    else if (l.flags & VF_RECORDED)
    {
        c.color(p.text);
        c.disc(rx, ry, 9);
        c.color(p.lane);
        c.disc(rx, ry, 5);
    }
    else if (l.flags & VF_ACTIVE)
    {
        c.color(p.text);
        c.disc(rx, ry, 8);
    }
    else
    {
        c.color(p.textDim);
        c.disc(rx, ry, 4);
    }
}

template <class C> void lane(C& c, const LaneInfo& l, int i, const Palette& p)
{
    const int top = kLaneTop + i * kLaneStride, bot = top + kLaneH;
    const bool stopped = (l.flags & VF_STOPPED) != 0;
    const int steps = l.steps < 1 ? 1 : (l.steps > kMaxSteps ? kMaxSteps : l.steps);

    // lane body: the 8 pad segments alternate in shade
    c.color(p.lane);
    c.rect(kLaneX, top, kLaneX + kLaneW, bot);
    c.color(p.segAlt);
    for (int k = 1; k < kPosCols; k += 2)
        c.rect(kLaneX + k * kSegW, top, kLaneX + (k + 1) * kSegW, bot);

    // beat / bar ticks along the bottom edge
    c.color(p.grid);
    const int every = steps > 64 ? 8 : 4;
    for (int s = 0; s < steps; s += every)
    {
        int x = kLaneX + laneOffset(s, 0, steps);
        int h = (s % 16 == 0) ? 14 : 8;
        c.rect(x, bot - h, x + 1, bot);
    }

    // note marks (steps share a mark above kNoteRects steps)
    c.color(p.note);
    const int b = noteBucket(steps);
    for (int s = 0; s < steps; s += b)
    {
        bool any = false;
        for (int j = s; j < s + b && j < steps; j++)
            if (noteBitGet(l.notes, j)) any = true;
        if (!any) continue;
        int x0 = kLaneX + laneOffset(s, 0, steps);
        int x1 = kLaneX + (s + b >= steps ? kLaneW : laneOffset(s + b, 0, steps)) - 1;
        if (x1 <= x0) x1 = x0 + 2;
        c.rect(x0, top + 10, x1, top + 24);
    }

    // loop bracket over its segments
    if (l.loopLo <= l.loopHi && l.loopLo < kPosCols)
    {
        int hi = l.loopHi < kPosCols ? l.loopHi : kPosCols - 1;
        int x0 = kLaneX + l.loopLo * kSegW, x1 = kLaneX + (hi + 1) * kSegW;
        c.color(p.loop);
        c.rect(x0, top, x1, top + 4);
        c.rect(x0, top, x0 + 3, top + 18);
        c.rect(x1 - 3, top, x1, top + 18);
    }

    // a stopped lane is dimmed
    if (stopped)
    {
        c.alpha(170);
        c.color(p.bg);
        c.rect(kLaneX, top, kLaneX + kLaneW, bot);
        c.alpha(255);
    }

    // recording a take: red edge
    if (l.flags & VF_REC)
    {
        c.color(p.rec);
        c.rect(kLaneX, top, kLaneX + kLaneW, top + 2);
        c.rect(kLaneX, bot - 2, kLaneX + kLaneW, bot);
        c.rect(kLaneX, top, kLaneX + 2, bot);
        c.rect(kLaneX + kLaneW - 2, top, kLaneX + kLaneW, bot);
    }

    // label column: track number, then what makes it special
    char buf[16];
    c.color(stopped ? p.textDim : p.text);
    snprintf(buf, sizeof(buf), "%d", l.track + 1);
    c.text(10, top + 6, 4, TX_LEFT, buf);
    const uint8_t rate = viewRate(View{ 0, 1, 0, l.flags });
    if (l.flags & VF_ACTIVE)
    {
        c.color(p.textDim);
        if (l.flags & VF_STOPPED) c.text(10, top + 34, 3, TX_LEFT, "STOP");
        else if (l.flags & VF_REC) { c.color(p.rec); c.text(10, top + 34, 3, TX_LEFT, "REC"); }
        else if (l.flags & VF_RECORDED) c.text(10, top + 34, 3, TX_LEFT, "REPLAY");
        else if (l.loopLo <= l.loopHi) c.text(10, top + 34, 3, TX_LEFT, "LOOP");
        else c.text(10, top + 34, 3, TX_LEFT, "OFF GRID");
        c.color(p.textDim);
        if ((l.flags & VF_REVERSE) && rate != 1) c.text(10, top + 54, 3, TX_LEFT, rate == 0 ? "REV 1/2" : "REV 2x");
        else if (l.flags & VF_REVERSE) c.text(10, top + 54, 3, TX_LEFT, "REV");
        else if (rate != 1) c.text(10, top + 54, 3, TX_LEFT, rate == 0 ? "1/2 speed" : "2x speed");
    }

    rightBlock(c, l, top, p);
}

} // namespace draw

// The static part: header, 4 lanes with ticks, notes, loop brackets, flags, right block. Redrawn on events.
template <class C> void drawLanes(C& c, const LaneModel& m, const Palette& p)
{
    const int mid = (kHeaderY0 + kHeaderY1) / 2;
    if (m.recHeld)
    {
        c.color(p.rec);
        c.rect(0, kHeaderY0, 800, kHeaderY1);
    }
    char buf[24];
    snprintf(buf, sizeof(buf), "CUT  %d-%d", m.first + 1, m.first + kRows);
    c.color(m.recHeld ? p.recText : p.text);
    c.text(12, mid, 4, TX_CENTERY, buf);
    if (m.recHeld)
    {
        c.color(p.recText);
        c.disc(360, mid, 6);
        c.text(374, mid, 4, TX_CENTERY, "REC");
    }
    for (int i = 0; i < kRows; i++)
        draw::lane(c, m.lane[i], i, p);
}

// The moving part: filled segment, grid ghost, playhead line, bar.beat. Redrawn every kRedrawGapMs while something moves.
template <class C> void drawHeads(C& c, const HeadModel& m, const Palette& p)
{
    for (int i = 0; i < kRows; i++)
    {
        const HeadInfo& h = m.h[i];
        const int top = kLaneTop + i * kLaneStride, bot = top + kLaneH;
        const bool stopped = (h.flags & VF_STOPPED) != 0;
        // the pad segment the playhead is in
        c.alpha(60);
        c.color(p.head);
        c.rect(kLaneX + h.seg * kSegW, top, kLaneX + (h.seg + 1) * kSegW, bot);
        c.alpha(255);
        // grid ghost: where the track would be if it was not jammed
        if (h.ghost >= 0)
        {
            int gx = kLaneX + h.ghost;
            c.color(p.ghost);
            c.rect(gx - 2, top + 30, gx + 3, top + 33);
            c.rect(gx, top + 33, gx + 1, top + 50);
        }
        // the playhead: a thin line over the whole lane
        int x = kLaneX + h.x;
        c.color(stopped ? p.textDim : p.head);
        c.rect(x - 1, top - 2, x + 2, bot + 2);
    }
    char buf[24];
    snprintf(buf, sizeof(buf), "bar %d.%d", m.bar, m.beat);
    c.color(m.recHeld ? p.recText : p.text);
    c.text(788, (kHeaderY0 + kHeaderY1) / 2, 4, TX_RIGHT | TX_CENTERY, buf);
}

} // namespace seqcut
