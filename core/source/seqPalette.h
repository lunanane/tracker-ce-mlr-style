// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Colours of the trace views and the clip launcher (plan 013): the colours of the four tracker keys (Note, Instrument, FX1, FX2), the
// pattern text colours of the stock theme (Themes.cpp patternTrackerColors: 0x00e8be note, instrYellow, 0xbb58f1 FX1, 0x57f1ff FX2),
// on a plain dark background. No Polyend includes (host-tested).
#ifndef SEQ_PALETTE_H_
#define SEQ_PALETTE_H_

#include <stdint.h>

namespace seqpal
{
constexpr uint32_t kNote = 0x00E8BE;  // Note key (teal)
constexpr uint32_t kInstr = 0xFEF749; // Instrument key (yellow)
constexpr uint32_t kFx1 = 0xBB58F1;   // FX1 key (pink / violet)
constexpr uint32_t kFx2 = 0x57F1FF;   // FX2 key (blue)

constexpr uint32_t kBg = 0x050505;      // plain dark background of the views
constexpr uint32_t kGrid = 0x1C1C1C;    // light semitone / dB grid
constexpr uint32_t kGridHi = 0x3A3A3A;  // octave / 0 dB lines
constexpr uint32_t kText = 0x8A8A8A;    // axis labels
constexpr uint32_t kTitle = 0xC8C8C8;   // title line

// Launcher cell fills: state by colour on a clean dark ground (the outlines carry the bright key colours)
constexpr uint32_t kCellEmpty = 0x0B0B0B;
constexpr uint32_t kCellNotes = 0x242424;
constexpr uint32_t kCellPlaying = 0x0B4A40;  // dark Note teal
constexpr uint32_t kSceneEmpty = 0x0B0B0B;
constexpr uint32_t kSceneNotes = 0x1B2A30;   // dark FX2 blue
constexpr uint32_t kScenePlaying = 0x0B3F49;

// t = 0..256: colour a towards b
inline uint32_t mix(uint32_t a, uint32_t b, int t)
{
    uint32_t out = 0;
    for (int s = 16; s >= 0; s -= 8)
    {
        const int ca = (int)((a >> s) & 0xFF), cb = (int)((b >> s) & 0xFF);
        out |= (uint32_t)(ca + ((cb - ca) * t) / 256) << s;
    }
    return out;
}
// The same colour dimmed towards the background (the ADSR curve / quiet parts)
inline uint32_t dim(uint32_t c) { return mix(c, kBg, 128); }
// Fainter still (the peak lines of the amplitude view behind the level lines)
inline uint32_t faint(uint32_t c) { return mix(c, kBg, 176); }

// Line colour of track 0..15 (plan 014 B): tracks 1-4 the four key colours, 5-8 the same hues lightened towards white, 9-16 the
// first eight dimmed towards the background
inline uint32_t trackColor(int t)
{
    static const uint32_t key[4] = { kNote, kInstr, kFx1, kFx2 };
    uint32_t c = key[t & 3];
    if (t & 4) c = mix(c, 0xFFFFFF, 110);
    if (t & 8) c = mix(c, kBg, 90);
    return c;
}

// Scene colours (plan 017): what the Song page's "Color" step shows next to a slot and the launcher tints a row with. Index 0 = none (the
// plain look of before), 1-4 the four tracker key colours, 5-8 four extras that read on the dark theme (orange, coral red, green, white).
// Stored as 4 bits per pattern in seqclips.bin, so the index is stable: never reorder, only append (up to 15 colours).
enum { kSceneColors = 9 };
constexpr uint32_t kSceneRgb[kSceneColors] = { 0x000000, kNote, kInstr, kFx1, kFx2, 0xFF8A30, 0xFF4D6A, 0x6EE06A, 0xE6E6E6 };
inline uint32_t sceneRgb(uint8_t idx) { return idx > 0 && idx < kSceneColors ? kSceneRgb[idx] : 0; }
inline const char* sceneName(uint8_t idx)
{
    static const char* const n[kSceneColors] = { "None", "Teal", "Yellow", "Purple", "Blue", "Orange", "Red", "Green", "White" };
    return n[idx < kSceneColors ? idx : 0];
}
// One step of the Color control: dir > 0 next, dir < 0 previous, wrapping through None.
inline uint8_t sceneStep(uint8_t idx, int dir)
{
    int i = idx < kSceneColors ? idx : 0;
    i += dir > 0 ? 1 : -1;
    if (i < 0) i = kSceneColors - 1;
    if (i >= kSceneColors) i = 0;
    return (uint8_t)i;
}
// Launcher fills of a coloured scene cell by state (empty / notes / playing): the colour mixed into the dark scene ground
inline uint32_t sceneCellFill(uint8_t idx, int cls)
{
    static const int lvl[3] = { 56, 110, 190 };
    return mix(kSceneEmpty, sceneRgb(idx), lvl[cls < 0 ? 0 : (cls > 2 ? 2 : cls)]);
}
// Tint of a clip with content in a coloured row (the playing clip keeps the teal state fill, so playing stays readable)
inline uint32_t sceneClipFill(uint8_t idx) { return mix(kCellNotes, sceneRgb(idx), 64); }
} // namespace seqpal

#endif
