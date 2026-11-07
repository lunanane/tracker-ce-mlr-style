// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Camelot wheel (plan 028): one shared helper for the key of every scale-using sequencer. A key is a wheel position (number 1..12, side
// B = major, A = its relative minor) plus a scale flavour per side: the position stays the KEY SIGNATURE, the type moves the tonic
// (8A + DORIAN = D dorian, 8B + MIXOLYDIAN = G mixolydian) so the wheel position and the match marks stay exact for modes.
// Rule (same as the owner's Model:Samples firmware, reimplemented here): root of cB = (11 + 7 (c - 1)) mod 12 (8B = C), cA = cB - 3.
// Header only, integer maths, no heap.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "seqgen.h"

namespace seqgen {
namespace cam {

// scale ids; 0..7 are the order of the Strings scale list (seqplinky::scaleDef)
enum Sc : uint8_t { SC_MAJ = 0, SC_MIN, SC_DOR, SC_PHR, SC_LYD, SC_MIX, SC_PMIN, SC_PMAJ, SC_BLU, SC_HMI, SC_COUNT };
enum { kTypesA = 6, kTypesB = 4 };  // A: Natural Dorian Phrygian Penta Blues Harm min   B: Natural Lydian Mixolyd Penta
enum Mark : uint8_t { M_NONE = 0, M_MATCH, M_GOOD, M_CLASH };

// semitone sets (bit n = n semitones above the tonic) and the tonic's offset from the root of the key signature's major (cB)
inline uint16_t scaleMask(uint8_t sc)
{
    static const uint16_t k[SC_COUNT] = { 0xAB5, 0x5AD, 0x6AD, 0x5AB, 0xAD5, 0x6B5, 0x4A9, 0x295, 0x4E9, 0x9AD };
    return k[sc < SC_COUNT ? sc : 0];
}
inline uint8_t scaleOff(uint8_t sc)
{
    static const uint8_t k[SC_COUNT] = { 0, 9, 2, 4, 5, 7, 9, 0, 9, 9 };
    return k[sc < SC_COUNT ? sc : 0];
}
inline bool scaleIsB(uint8_t sc) { return sc == SC_MAJ || sc == SC_LYD || sc == SC_MIX || sc == SC_PMAJ; } // B side = major-like
// the scale a side's type index stands for
inline uint8_t typeScale(bool minor, uint8_t t)
{
    static const uint8_t a[kTypesA] = { SC_MIN, SC_DOR, SC_PHR, SC_PMIN, SC_BLU, SC_HMI };
    static const uint8_t b[kTypesB] = { SC_MAJ, SC_LYD, SC_MIX, SC_PMAJ };
    return minor ? a[t < kTypesA ? t : 0] : b[t < kTypesB ? t : 0];
}

// Camelot number 1..12 of a key signature whose major is rooted on pitch class cb
inline uint8_t numOfMajor(int cb)
{
    const int c = (7 * (((cb % 12) + 12 + 8) % 12)) % 12; // inverse of (7 c + 4) mod 12
    return (uint8_t)(c == 0 ? 12 : c);
}

struct Scale
{
    uint8_t tonic; // pitch class of the tonic
    uint8_t n;     // notes per octave (5, 6 or 7)
    uint8_t sc;    // Sc
    uint8_t num;   // Camelot number 1..12
    uint8_t minor; // side A
    uint8_t iv[7]; // semitones above the tonic
};

// the scale with this tonic: its Camelot position follows from the key signature
void build(Scale& s, int tonic, uint8_t sc); // (out of line, chordarp.cpp: one copy of the code)

// the pad key (pc = tonic of the major / minor key on the wheel, minor = side A) played with this track's A / B types
void resolve(Scale& s, int pc, bool minor, uint8_t tA, uint8_t tB); // (chordarp.cpp)

// note of degree d over a root note (anchor: the root note sits on the scale's tonic)
inline int degreeNote(const Scale& s, int root, int d) { return root + 12 * floordiv(d, s.n) + s.iv[floormod(d, s.n)]; }
// the root note moved onto the scale's tonic (upwards, 0..11 semitones; 0 for the natural scales)
inline int anchor(const Scale& s, int rootNote) { return rootNote + floormod(s.tonic - rootNote, 12); }

// wheel distance of two positions: MATCH = same position, GOOD = neighbour (number +-1 on the same side, or the other side of the same number)
inline Mark markOf(const Scale& a, const Scale& b)
{
    int d = a.num - b.num;
    d = d < 0 ? -d : d;
    if (d > 6) d = 12 - d;
    if (d == 0) return a.minor == b.minor ? M_MATCH : M_GOOD;
    return (d == 1 && a.minor == b.minor) ? M_GOOD : M_CLASH;
}

// what the display needs of every track: its key (valid), and whether it sounds now
struct Snap
{
    uint8_t valid, sounding;
    Scale s;
};
// The match mark of every track against the others (plan 028): a track that sounds and has a key is compared with the lead's live key when
// there is a lead (lead = track index, -1 none; plan 011 will set it), else with the other tracks that sound now and have a key. The mark is
// the worst relation found (MATCH only when it matches all of them); nothing to compare with = M_NONE.
void marks(const Snap* t, int n, int lead, uint8_t* out); // (chordarp.cpp)

// "8A Am", "8A D DOR", "5B Eb", "8B C PNT": position, tonic and the scale (as the Model:Samples indicator)
void label(char* out, size_t cap, const Scale& s); // (chordarp.cpp)

}  // namespace cam
}  // namespace seqgen
