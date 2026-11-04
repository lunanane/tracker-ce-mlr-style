// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Pure helpers of the CUT (MLR) layer. No Polyend includes, host-testable (core/seqtest).
#pragma once
#include <stdint.h>

namespace seqcut {

enum { kCutTracks = 16, kRows = 4, kCols = 12, kCutPads = 48, kPosCols = 8 };

// Pad columns (0-based) of one row: 0..7 = positions, then the row controls.
enum PadCol : uint8_t { COL_STOP = 8, COL_REVERSE = 9, COL_RATE = 10, COL_RESYNC = 11 };

// Commands main loop -> audio (Engine ring buffer, CMD_CUT). Packed in the ring's "a" byte: sub | down << 3 | col << 4.
enum Sub : uint8_t { SUB_PAD = 0, SUB_REC = 1, SUB_STOP = 2, SUB_REVERSE = 3, SUB_RATE = 4, SUB_RESYNC = 5, SUB_COUNT = 6 };
inline uint8_t packArg(uint8_t sub, bool down, uint8_t col) { return (uint8_t)((sub & 7) | (down ? 8 : 0) | ((col & 15) << 4)); }
inline uint8_t argSub(uint8_t a) { return (uint8_t)(a & 7); }
inline bool argDown(uint8_t a) { return (a & 8) != 0; }
inline uint8_t argCol(uint8_t a) { return (uint8_t)(a >> 4); }

// Group of 4 tracks: firstTrack is clamped to 0, 4, 8, 12 (anything in between rounds down).
inline uint8_t groupFirst(uint8_t firstTrack) { return (uint8_t)((firstTrack > 15 ? 12 : firstTrack) & ~3); }
inline uint8_t trackOfRow(uint8_t firstTrack, uint8_t row) { return (uint8_t)(groupFirst(firstTrack) + (row & 3)); }

// Column (0..columns-1) a position falls into; inverse of Playhead::columnPosition.
inline uint8_t columnOf(int pos, int length, int columns)
{
    if (length < 1 || columns < 1) return 0;
    if (pos < 0) pos = 0;
    if (pos >= length) pos = length - 1;
    int c = (pos * columns) / length;
    while (c > 0 && (c * length) / columns > pos) c--;
    while (c < columns - 1 && ((c + 1) * length) / columns <= pos) c++;
    return (uint8_t)c;
}

// Position FX value (0..100, the whole sample) for a retrigger `elapsedSteps` pattern steps after its note.
//   stepMs: length of one pattern step; sampleFrames at sampleRate; assumes the sample plays at its original speed.
// Returns 0..100 (100 = last percent of the sample; the sample ends there anyway).
inline uint8_t positionFxValue(uint32_t elapsedSteps, float stepMs, uint32_t sampleFrames, uint32_t sampleRate)
{
    if (sampleFrames == 0 || sampleRate == 0 || elapsedSteps == 0) return 0;
    float frames = (float)elapsedSteps * stepMs * 0.001f * (float)sampleRate;
    float pct = frames * 100.0f / (float)sampleFrames;
    if (pct >= 100.0f) return 100;
    if (pct < 0.0f) return 0;
    return (uint8_t)(pct + 0.5f);
}

// Combine with a Position FX the note step already has (both are percentages of the sample).
inline uint8_t addPosition(uint8_t base, uint8_t add)
{
    unsigned v = (unsigned)base + add;
    return (uint8_t)(v > 100 ? 100 : v);
}

// What the pads of one row show (published by the audio side, read by the main loop).
struct View
{
    uint8_t col;      // column of the playhead (0..7)
    uint8_t loopLo;   // held loop range in columns, loopLo > loopHi = none
    uint8_t loopHi;
    uint8_t flags;
};
enum ViewFlags : uint8_t
{
    VF_ACTIVE = 1,    // the track is driven by its playhead (not the stock sequencer)
    VF_STOPPED = 2,
    VF_REVERSE = 4,
    VF_REC = 8,       // a take is being recorded
    VF_RECORDED = 16, // a recorded playhead loop is replaying
    VF_RATE_SHIFT = 5 // bits 5..6: 0 = 1/2, 1 = 1, 2 = 2
};
inline uint8_t viewRate(const View& v) { return (uint8_t)((v.flags >> VF_RATE_SHIFT) & 3); }

enum Led : uint8_t { LED_OFF = 0, LED_DIM = 2, LED_LOOP = 5, LED_MID = 8, LED_BRIGHT = 15 };

// 4 rows x 12 columns, index = row * 12 + col, level 0..15. `blink` toggles about 2x per second.
inline void fillLeds(const View views[kRows], bool blink, uint8_t out[kCutPads])
{
    for (int i = 0; i < kCutPads; i++) out[i] = LED_OFF;
    for (int r = 0; r < kRows; r++)
    {
        const View& v = views[r];
        uint8_t* row = out + r * kCols;
        for (int c = 0; c < kPosCols; c++) row[c] = LED_DIM;
        if (v.loopLo <= v.loopHi)
            for (int c = v.loopLo; c <= v.loopHi && c < kPosCols; c++) row[c] = LED_LOOP;
        if (v.flags & VF_STOPPED)
        {
            for (int c = 0; c < kPosCols; c++) row[c] = LED_OFF;
            row[COL_STOP] = LED_BRIGHT;
        }
        else
        {
            row[v.col < kPosCols ? v.col : kPosCols - 1] = LED_BRIGHT;
            row[COL_STOP] = LED_DIM;
        }
        row[COL_REVERSE] = (v.flags & VF_REVERSE) ? LED_BRIGHT : LED_DIM;
        uint8_t rate = viewRate(v);
        row[COL_RATE] = rate == 1 ? LED_DIM : (rate == 0 ? LED_MID : LED_BRIGHT); // 1/2 = medium, 2 = bright
        if (v.flags & VF_REC)
            row[COL_RESYNC] = blink ? LED_BRIGHT : LED_OFF;      // recording a take
        else if (v.flags & VF_RECORDED)
            row[COL_RESYNC] = LED_BRIGHT;                          // a recorded loop is replaying
        else
            row[COL_RESYNC] = (v.flags & VF_ACTIVE) ? LED_MID : LED_DIM; // off the global grid: resync is useful
    }
}

} // namespace seqcut
