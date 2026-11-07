// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Plan 028: the Camelot wheel of Play mode. 12 numbers on the outer ring (B = major keys) and the inner ring (A = their relative minors); the
// selected track's key is lit in its match colour, the harmonic neighbours (number +-1, or the other side) have an outline, the other
// sounding tracks are dots, and the last roots are a short trail. RECTS and texts only, one display stage; same context as seqParamsLogic.h.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "camelot.h"

namespace seqwheel {

enum { kNone = 0xFF, kTrail = 3 };
// position code of a key: (number - 1) * 2 + side (0 = B outer, 1 = A inner)
inline uint8_t posOf(const seqgen::cam::Scale& s) { return (uint8_t)((s.num - 1) * 2 + (s.minor ? 1 : 0)); }

struct Model
{
    uint8_t on, cur, mark;       // cur: position code or kNone
    uint8_t trail[kTrail];       // earlier positions of the selected track, newest first (kNone = unused)
    uint32_t others;             // bit = position code of another sounding track
    char label[12];              // "8A D DOR"
    Model() { memset(this, 0, sizeof(*this)); cur = trail[0] = trail[1] = trail[2] = kNone; }
};

struct Palette
{
    uint32_t bg, cell, text, dark, neighbour, trail, other, mOk, mGood, mBad; // mark colours: the one table is in seqPlay.h (seqMarkColor)
};

// The one table of the match colours (provisional: green / amber / red). `th` has green, yellow and red (the Theme); re-point them here.
template <class T> inline uint32_t markColor(const T& th, uint8_t mark, uint32_t none)
{
    return mark == seqgen::cam::M_MATCH ? th.green : mark == seqgen::cam::M_GOOD ? th.yellow : mark == seqgen::cam::M_CLASH ? th.red : none;
}

enum { kX0 = 672, kY0 = 264, kX1 = 796, kY1 = 400, kCx = 734, kCy = 328, kROut = 50, kRIn = 28 };

// cell centre of a position code (clock face: number 1 at the top, clockwise)
inline void centre(uint8_t pos, int& x, int& y)
{
    static const int16_t kSin[12] = { 0, 500, 866, 1000, 866, 500, 0, -500, -866, -1000, -866, -500 };
    const int k = (pos >> 1) % 12, r = (pos & 1) ? kRIn : kROut;
    x = kCx + r * kSin[k] / 1000;
    y = kCy - r * kSin[(k + 3) % 12] / 1000;
}
inline bool neighbour(uint8_t a, uint8_t b) // the "safe" moves from a: number +-1 on the same side, or the other side of the same number
{
    const int na = a >> 1, nb = b >> 1, sa = a & 1, sb = b & 1;
    const int d = (na - nb + 12) % 12;
    return (d == 0 && sa != sb) || ((d == 1 || d == 11) && sa == sb);
}

template <class C> void draw(C& c, const Model& m, const Palette& p)
{
    if (!m.on) return;
    c.color(p.bg);
    c.rect(kX0, kY0, kX1, kY1);
    for (uint8_t pos = 0; pos < 24; pos++)
    {
        int x, y;
        centre(pos, x, y);
        const int hw = (pos & 1) ? 5 : 10, hh = (pos & 1) ? 5 : 8;
        if (m.cur != kNone && pos != m.cur && neighbour(m.cur, pos)) // outline of a neighbour
        {
            c.color(p.neighbour);
            c.rect(x - hw - 2, y - hh - 2, x + hw + 2, y + hh + 2);
        }
        c.color(pos == m.cur ? (m.mark == seqgen::cam::M_MATCH ? p.mOk : m.mark == seqgen::cam::M_GOOD ? p.mGood : m.mark == seqgen::cam::M_CLASH ? p.mBad : p.text) : p.cell);
        c.rect(x - hw, y - hh, x + hw, y + hh);
    }
    for (uint8_t pos = 0; pos < 24; pos++)
    {
        int x, y;
        centre(pos, x, y);
        bool dot = (m.others >> pos) & 1u;
        bool tr = false;
        for (int i = 0; i < kTrail; i++) tr = tr || (m.trail[i] == pos && pos != m.cur);
        if (!dot && !tr) continue;
        c.color(dot ? p.other : p.trail);
        c.rect(x - 2, y - 2, x + 3, y + 3);
    }
    c.textPhase();
    char b[4];
    for (int n = 1; n <= 12; n++) // the numbers on the outer ring
    {
        int x, y;
        centre((uint8_t)((n - 1) * 2), x, y);
        snprintf(b, sizeof b, "%d", n);
        c.color(m.cur == (uint8_t)((n - 1) * 2) ? p.dark : p.text);
        c.text(x, y, 1, 1 | 2, b);
    }
    if (m.label[0])
    {
        c.color(p.text);
        c.text((kX0 + kX1) / 2, kY1 - 8, 3, 1 | 2, m.label);
    }
    c.finish();
}

} // namespace seqwheel
