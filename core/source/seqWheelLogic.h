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

// The one table of the match colours (docs/seq-ui-colours.md: match green, near orange, clash red; yellow is focus and never a match colour); re-point them here.
template <class T> inline uint32_t markColor(const T& th, uint8_t mark, uint32_t none)
{
    return mark == seqgen::cam::M_MATCH ? th.green : mark == seqgen::cam::M_GOOD ? th.orange : mark == seqgen::cam::M_CLASH ? th.red : none;
}

// Plan 029 part 2: two clear rings drawn with discs (POINTS): B (major) outside with the number inside each disc, A (relative minor) inside.
// The region stays right of the pattern columns and below the playhead row (y >= 270); the label sits under the wheel.
enum { kX0 = 662, kY0 = 270, kX1 = 798, kY1 = 422, kCx = 730, kCy = 338, kROut = 56, kRIn = 33, kDiscOut = 12, kDiscIn = 7, kLabelY = 415 };

// cell centre of a position code (clock face: number 1 at the top, clockwise)
inline void centre(uint8_t pos, int& x, int& y, int inset = 0) // inset: toward the hub on the outer ring
{
    static const int16_t kSin[12] = { 0, 500, 866, 1000, 866, 500, 0, -500, -866, -1000, -866, -500 };
    const int k = (pos >> 1) % 12, r = (pos & 1) ? kRIn : kROut - inset;
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
    const uint32_t byMark[4] = { p.text, p.mOk, p.mGood, p.mBad }; // index = seqgen::cam::Mark
    const uint32_t mc = byMark[m.mark & 3]; // the match colour of the playing key
    uint32_t nb = 0; // the harmonic neighbours of the playing key (same as neighbour(): number +-1 on its ring, or the other ring of its number)
    if (m.cur < 24) nb = (1u << ((m.cur + 2) % 24)) | (1u << ((m.cur + 22) % 24)) | (1u << (m.cur ^ 1));
    c.color(p.bg);
    c.rect(kX0, kY0, kX1, kY1);
    // one loop, one disc call site (the display-list macros are big when inlined): 0 quiet grey ring around a neighbour (2 px, no colour meaning),
    // 1 every other key, 2 the playing key (bigger, in its match colour), 3 trail = small dim disc, 4 other sounding track = white square;
    // 3 and 4 sit on the hub-side rim of the disc so they stay clear of the number
    for (int pass = 0; pass < 5; pass++)
    {
        c.color(pass == 0 ? p.neighbour : pass == 1 ? p.cell : pass == 2 ? mc : pass == 3 ? p.trail : p.other);
        for (uint8_t pos = 0; pos < 24; pos++)
        {
            const int base = (pos & 1) ? kDiscIn : kDiscOut;
            const bool cur = pos == m.cur, dot = (m.others >> pos) & 1u;
            bool tr = false;
            for (int i = 0; i < kTrail; i++) tr = tr || (m.trail[i] == pos && !cur);
            int r = 0;
            switch (pass)
            {
                case 0: r = ((nb >> pos) & 1u) ? base + 2 : 0; break;
                case 1: r = cur ? 0 : base; break;
                case 2: r = cur ? base + 2 : 0; break;
                case 3: r = (tr && !dot) ? 2 : 0; break;
                default: r = dot ? 3 : 0; break;
            }
            if (!r) continue;
            int x, y;
            centre(pos, x, y, pass >= 3 ? kDiscOut - 1 : 0);
            if (pass == 4) c.rect(x - 2, y - 2, x + 3, y + 3);
            else c.disc(x, y, r);
        }
    }
    c.textPhase();
    char b[4];
    for (int n = 1; n <= 12; n++) // the numbers inside the outer discs
    {
        const uint8_t pos = (uint8_t)((n - 1) * 2);
        int x, y;
        centre(pos, x, y);
        b[0] = (char)(n > 9 ? '1' : '0' + n);
        b[1] = n > 9 ? (char)('0' + n - 10) : 0;
        b[2] = 0;
        c.color(m.cur == pos ? p.dark : ((nb >> pos) & 1u) ? p.text : p.neighbour);
        c.text(x, y, 1, 1 | 2, b);
    }
    if (m.label[0])
    {
        c.color(mc);
        c.text((kX0 + kX1) / 2, kLabelY, 3, 1 | 2, m.label);
    }
    c.finish();
}

} // namespace seqwheel
