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
} // namespace seqpal

#endif
