// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Plinky sequencer type (plan 010 B): the pad layout of the track that plays the Plinky voice. Pure logic, no Polyend includes,
// host-testable (core/seqtest/test_plinky.cpp).
//
// Layout ("strings", after the Plinky): the 12 x 4 pads are 12 strings (columns) of 4 notes (rows). A string is a run of the
// scale; the bottom row is the lowest note of the string, the top row the highest. Each column starts `interval` scale degrees
// above the column to its left (default 3 = a 4th in a 7 note scale), so neighbouring strings overlap like the strings of a guitar.
// The bottom right pad (column 11, row 3) is the Setup pad: it switches between the notes and the setup view.
//   Setup view:  row 0 = root (C .. B), row 1 = scale (columns 0..7) and octave (columns 8..11 = 1..4),
//                row 2 = string interval (columns 0..6 = 1..7 scale degrees), row 3 column 11 = back to the notes.
#pragma once
#include <stdint.h>

namespace seqplinky {

enum { kCols = 12, kRows = 4, kPads = 48, kScales = 8, kMenuCol = 11, kMenuRow = 3, kDefaultVelocity = 100, kSetupBytes = 4 };

struct ScaleDef
{
    const char* name;
    const char* shortName; // <= 6 characters (pad legend)
    uint8_t size;
    uint8_t step[8]; // semitones above the root
};
inline const ScaleDef& scaleDef(uint8_t i)
{
    static const ScaleDef k[kScales] = {
        { "Major", "Major", 7, { 0, 2, 4, 5, 7, 9, 11, 0 } },
        { "Minor", "Minor", 7, { 0, 2, 3, 5, 7, 8, 10, 0 } },
        { "Dorian", "Dorian", 7, { 0, 2, 3, 5, 7, 9, 10, 0 } },
        { "Phrygian", "Phryg", 7, { 0, 1, 3, 5, 7, 8, 10, 0 } },
        { "Lydian", "Lydian", 7, { 0, 2, 4, 6, 7, 9, 11, 0 } },
        { "Mixolyd", "Mixo", 7, { 0, 2, 4, 5, 7, 9, 10, 0 } },
        { "Pent min", "PentMi", 5, { 0, 3, 5, 7, 10, 0, 0, 0 } },
        { "Pent maj", "PentMa", 5, { 0, 2, 4, 7, 9, 0, 0, 0 } },
    };
    return k[i < kScales ? i : 0];
}

// root 0..11 (C..B), scale 0..7, octave 1..4 (the lowest note of the grid is C of MIDI octave 12 * (octave + 1)), interval 1..7
struct Setup
{
    uint8_t root = 0;
    uint8_t scale = 1; // minor
    uint8_t octave = 2;
    uint8_t interval = 3;
    void clamp()
    {
        if (root > 11) root = 0;
        if (scale >= kScales) scale = 1;
        if (octave < 1 || octave > 4) octave = 2;
        if (interval < 1 || interval > 7) interval = 3;
    }
    bool operator==(const Setup& o) const { return root == o.root && scale == o.scale && octave == o.octave && interval == o.interval; }
    void save(uint8_t* out) const
    {
        out[0] = root;
        out[1] = scale;
        out[2] = octave;
        out[3] = interval;
    }
    // false: block too short (nothing changed)
    bool load(const uint8_t* in, unsigned n)
    {
        if (n < (unsigned)kSetupBytes) return false;
        root = in[0];
        scale = in[1];
        octave = in[2];
        interval = in[3];
        clamp();
        return true;
    }
};

inline bool isMenuPad(uint8_t pad) { return pad % kCols == kMenuCol && pad / kCols == kMenuRow; }

// scale degree of a pad (0 = the lowest note of the grid); row 0 is the top pad row
inline int padDegree(const Setup& s, uint8_t col, uint8_t row) { return (int)col * s.interval + (kRows - 1 - (int)row); }

inline uint8_t degreeNote(const Setup& s, int degree)
{
    const ScaleDef& sc = scaleDef(s.scale);
    int oct = degree / sc.size;
    int n = 12 * (s.octave + 1) + s.root + 12 * oct + sc.step[degree % sc.size];
    return (uint8_t)(n > 127 ? 127 : n);
}

inline uint8_t padNote(const Setup& s, uint8_t col, uint8_t row) { return degreeNote(s, padDegree(s, col, row)); }

// A setup pad in the setup view: true when the setup changed
inline bool setupPad(Setup& s, uint8_t col, uint8_t row)
{
    const Setup before = s;
    if (row == 0 && col < 12) s.root = col;
    else if (row == 1 && col < 8) s.scale = col;
    else if (row == 1 && col >= 8) s.octave = (uint8_t)(col - 7);
    else if (row == 2 && col < 7) s.interval = (uint8_t)(col + 1);
    return !(s == before);
}

// LED level 0..15 of a pad. held = a note pad is down. Notes view: the root notes shine, the rest glows dim, the Setup pad shows
// whether the setup view is on. Setup view: the chosen entries are bright, the other entries dim, unused pads dark.
inline uint8_t padLevel(const Setup& s, bool menu, uint8_t col, uint8_t row, bool held)
{
    if (col == kMenuCol && row == kMenuRow) return menu ? 15 : 4;
    if (!menu)
    {
        if (held) return 15;
        return (padNote(s, col, row) % 12 == (s.root % 12)) ? 8 : 2;
    }
    if (row == 0) return col == s.root ? 15 : 3;
    if (row == 1)
    {
        if (col < 8) return col == s.scale ? 15 : 3;
        return (col - 7) == s.octave ? 15 : 3;
    }
    if (row == 2 && col < 7) return (col + 1) == s.interval ? 15 : 3;
    return 0;
}

inline const char* noteName(uint8_t note, char* buf6)
{
    static const char* const kPc[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int oct = (int)(note / 12) - 2; // 60 = C3
    int i = 0;
    for (const char* p = kPc[note % 12]; *p; p++) buf6[i++] = *p;
    if (oct < 0)
    {
        buf6[i++] = '-';
        buf6[i++] = (char)('0' + (-oct > 9 ? 9 : -oct));
    }
    else
        buf6[i++] = (char)('0' + (oct > 9 ? 9 : oct));
    buf6[i] = 0;
    return buf6;
}

} // namespace seqplinky
