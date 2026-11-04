// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Pure helpers of the CUT (MLR) layer. No Polyend includes, host-testable (core/seqtest).
#pragma once
#include <stdint.h>

namespace seqcut {

enum { kCutTracks = 16, kRows = 4, kCols = 12, kCutPads = 48, kPosCols = 8 };

// Pad columns (0-based) of one row: 0..7 = positions, then the row controls.
enum PadCol : uint8_t { COL_STOP = 8, COL_REVERSE = 9, COL_RATE = 10, COL_RESYNC = 11 };

// Commands main loop -> audio (Engine ring buffer, CMD_CUT). Packed in the ring's "a" byte: sub | down << 3 | col << 4.
enum Sub : uint8_t { SUB_PAD = 0, SUB_REC = 1, SUB_STOP = 2, SUB_REVERSE = 3, SUB_RATE = 4, SUB_RESYNC = 5, SUB_RESET = 6, SUB_COUNT = 7 };
inline uint8_t packArg(uint8_t sub, bool down, uint8_t col) { return (uint8_t)((sub & 7) | (down ? 8 : 0) | ((col & 15) << 4)); }
inline uint8_t argSub(uint8_t a) { return (uint8_t)(a & 7); }
inline bool argDown(uint8_t a) { return (a & 8) != 0; }
inline uint8_t argCol(uint8_t a) { return (uint8_t)(a >> 4); }

// SYNC ALL (plan 005 R4): every track back onto the global playhead, in one pass of the audio context. It shares the sub slot of
// SUB_RESET (col 1; col 0 = RESET): the last free 3-bit slot (7) stays "invalid" for Engine::postCut and its ring test.
//   RESET    : all cut rows back to stock at once (project load / new project)
//   SYNC ALL : held pads, recorded loops, rate and reverse of every cut track are dropped, STOP ends; each track snaps to the
//              global position at its next step boundary and is then handed back to the stock sequencer. With the transport
//              stopped there is no boundary: the next Play hands every such track back (Cut::transportStart).
inline uint8_t packSyncAll() { return packArg(SUB_RESET, false, 1); }
inline bool argIsSyncAll(uint8_t a) { return argSub(a) == SUB_RESET && argCol(a) == 1; }

// Jump quantise (plan 008 A): when a pad gesture (jump, loop) takes effect. One setting for all tracks, shared slot of SUB_RESET:
// col 2 + mode. step = the next step, off = at once (the next audio tick, in the middle of the step if need be),
// beat = the next beat (4 steps), bar = the next bar (16 steps, or the whole pattern when its length is not a multiple of 16). Default
// beat. The grid is counted in steps from the pattern start (the track's own stock position, Cut::boundary).
enum Quant : uint8_t { Q_OFF = 0, Q_STEP = 1, Q_BEAT = 2, Q_BAR = 3, Q_COUNT = 4, Q_DEFAULT = Q_BEAT };
inline uint8_t packQuant(uint8_t mode) { return packArg(SUB_RESET, false, (uint8_t)(2 + (mode % Q_COUNT))); }
inline bool argIsQuant(uint8_t a) { return argSub(a) == SUB_RESET && argCol(a) >= 2 && argCol(a) < 2 + Q_COUNT; }
inline uint8_t argQuant(uint8_t a) { return (uint8_t)(argCol(a) - 2); }
inline uint8_t quantSteps(uint8_t mode) { return mode == Q_BAR ? 16 : (mode == Q_BEAT ? 4 : 1); }
// The grid of a track of `steps` steps: beat = 4, bar = 16 when the pattern is a whole number of bars; otherwise a bar is the whole
// pattern (so the grid never drifts against the pattern start; patterns longer than 64 steps that are not a multiple of 16 use 64).
inline uint8_t quantStepsFor(uint8_t mode, int steps)
{
    if (mode != Q_BAR) return quantSteps(mode);
    if (steps % 16 == 0) return 16;
    return (uint8_t)(steps < 1 ? 1 : (steps > 64 ? 64 : steps));
}
inline uint8_t quantNext(uint8_t mode) { return (uint8_t)((mode + 1) % Q_COUNT); }
inline const char* quantName(uint8_t mode)
{
    return mode == Q_OFF ? "off" : (mode == Q_STEP ? "step" : (mode == Q_BEAT ? "beat" : "bar"));
}

// Screen button (0-based) of SYNC ALL in the CUT layer: button 6 = seqsession::kButtonLayer (the layer's own button; static_assert in
// cutSessionLayer.h); 7 is an empty gap and 8 switches CLIPS <-> CUT (both owned by the host).
enum { kScreenButtonSyncAll = 5, kScreenButtonQuant = 4, kScreenButtonRec = 6 };
// Button 5 (index 4) = jump quantise (tap cycles off / step / beat / bar, its label shows the value); button 7 (index 6) shows [REC] while a take is armed.

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

// Start point (0..65535 of the whole sample, the unit of Instrument::startPoint) for a retrigger `elapsedSteps` pattern steps
// after its note: the instrument's own start + elapsed time x pitch ratio, wrapped into the loop for loop play modes.
//   startPoint16: instrument start (0..65535); sampleFrames at sampleRate; speed1000 = playback speed x 1000 (1000 = original)
//   loop1/loop2: loop points (0..65535) when the play mode loops (loopOn), endPoint16: end point (0..65535)
// Returns false when the position lies behind the end of a one-shot sample (nothing to play).
inline bool startPoint16(uint32_t elapsedSteps, float stepMs, uint32_t sampleFrames, uint32_t sampleRate, uint32_t speed1000,
                         uint16_t startPoint16, bool loopOn, uint16_t loop1, uint16_t loop2, uint16_t endPoint16, uint16_t& out)
{
    out = startPoint16;
    if (sampleFrames == 0 || sampleRate == 0) return true;
    const float f = 65535.0f;
    float start = (float)startPoint16 * (float)sampleFrames / f;
    float elapsed = (float)elapsedSteps * stepMs * 0.001f * (float)sampleRate * (float)speed1000 * 0.001f;
    float pos = start + elapsed;
    float end = (float)endPoint16 * (float)sampleFrames / f;
    if (loopOn && loop2 > loop1)
    {
        float l1 = (float)loop1 * (float)sampleFrames / f, l2 = (float)loop2 * (float)sampleFrames / f;
        if (pos >= l2)
        {
            float span = l2 - l1;
            float k = (pos - l1) / span;
            pos = l1 + (k - (float)(uint32_t)k) * span;
        }
    }
    else if (pos >= end)
        return false;
    if (pos < 0.0f) pos = 0.0f;
    float v = pos * f / (float)sampleFrames;
    out = (uint16_t)(v >= f ? 65535.0f : v);
    return true;
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

// ================================================================ lane screen (plan 005 R4)
// One lane per pad row (4 tracks of the group), the lane split into the 8 pad segments. Pure geometry and models: the drawing
// itself is laneDraw.h (templates), the firmware controls are laneView.*.
constexpr int kLaneX = 100;        // x of the lane (segments), 8 x 75 px
constexpr int kLaneW = 600;
constexpr int kSegW = 75;
constexpr int kLaneTop = 70;       // y of the first lane (the stock title bar occupies y 0..28, the header y 32..62)
constexpr int kLaneH = 80;
constexpr int kLaneStride = 84;
constexpr int kCellX = 704;        // right block: the pad columns 9..12 as 2 x 2 cells
constexpr int kMaxSteps = 128;     // MAXSTEP + 1
constexpr int kNoteRects = 32;     // most note marks drawn per lane (steps are merged into buckets above that)
constexpr bool kRedrawPerStep = false; // fallback if the 25 fps playhead disturbs the SD streams: true = redraw once per step (one-line switch)
constexpr int kRedrawGapMs = 40;   // playhead strips: at most 25 frames per second

// First step of pad column `col` (0..8) in a pattern of `steps` steps (Playhead::columnPosition with 8 columns).
inline int laneColStart(int col, int steps) { return (col * steps) / kPosCols; }

// x offset (0..kLaneW-1) inside the lane of position `pos` plus frac48/48 of a step. Piecewise linear per pad segment, so the line
// is inside the segment whose pad it is, and moves smoothly (1/48 step) from one segment to the next.
inline int laneOffset(int pos, int frac48, int steps)
{
    if (steps < 1) return 0;
    if (pos < 0) pos = 0;
    if (pos >= steps) pos = steps - 1;
    if (frac48 < 0) frac48 = 0;
    if (frac48 > 47) frac48 = 47;
    const int c = columnOf(pos, steps, kPosCols);
    int span = laneColStart(c + 1, steps) - laneColStart(c, steps);
    if (span < 1) span = 1;
    const int within = (pos - laneColStart(c, steps)) * 48 + frac48; // 1/48 steps from the segment start
    int x = c * kSegW + (within * kSegW) / (span * 48);
    return x >= kLaneW ? kLaneW - 1 : x;
}

// Micro step 1..48 of the step -> 0..47 (reverse runs through the step backwards).
inline int laneFrac(int uStep, bool reverse)
{
    int f = uStep < 1 ? 0 : (uStep > 48 ? 47 : uStep - 1);
    return reverse ? 47 - f : f;
}

// Bit set of the steps that hold a note (up to 128 steps).
inline void noteBitSet(uint32_t w[4], int i) { if (i >= 0 && i < kMaxSteps) w[i >> 5] |= (1u << (i & 31)); }
inline bool noteBitGet(const uint32_t w[4], int i) { return i >= 0 && i < kMaxSteps && ((w[i >> 5] >> (i & 31)) & 1u) != 0; }
// Steps per note mark: 1 up to 32 steps, above that several steps share a mark (never more than kNoteRects marks).
inline int noteBucket(int steps) { return steps <= kNoteRects ? 1 : (steps + kNoteRects - 1) / kNoteRects; }

// The static part of the lanes (redrawn on events: group, flags, loop range, pattern edits).
struct LaneInfo
{
    uint8_t track, flags, loopLo, loopHi, steps, pad_[3];
    uint32_t notes[4];
};
struct LaneModel
{
    uint8_t first, recHeld, pad_[2];
    LaneInfo lane[kRows];
};
// The moving part (redrawn at most every kRedrawGapMs when something moved).
struct HeadInfo
{
    int16_t x;     // playhead offset in the lane
    int16_t ghost; // where the track would be on the grid, -1 = same place / not cut-active
    uint8_t seg;   // pad segment the playhead is in
    uint8_t flags; // View flags
};
struct HeadModel
{
    HeadInfo h[kRows];
    uint8_t bar, beat, recHeld, pad_; // bar and beat (1-based) from the stock position of track 1
};

inline HeadInfo makeHead(const View& v, int pos, int uStep, int stock, int steps)
{
    HeadInfo h;
    h.flags = v.flags;
    h.x = (int16_t)laneOffset(pos, laneFrac(uStep, (v.flags & VF_REVERSE) != 0), steps);
    h.seg = columnOf(pos, steps, kPosCols);
    h.ghost = -1;
    if ((v.flags & VF_ACTIVE) && stock != pos && stock >= 0 && stock < steps)
        h.ghost = (int16_t)laneOffset(stock, laneFrac(uStep, false), steps); // the stock sequencer runs forward
    return h;
}

// Pattern bar of 16 steps (not the song's time signature) and beat of 4 steps, from the stock position of track 1.
inline void barBeat(int stockPos, uint8_t& bar, uint8_t& beat)
{
    if (stockPos < 0) stockPos = 0;
    int b = stockPos / 16 + 1;
    bar = (uint8_t)(b > 255 ? 255 : b);
    beat = (uint8_t)((stockPos % 16) / 4 + 1);
}

// Redraw gate for the moving part: something moved and the last frame is old enough (now/last in ms, wrap safe).
inline bool redrawDue(bool moved, uint32_t nowMs, uint32_t lastMs) { return moved && (uint32_t)(nowMs - lastMs) >= (uint32_t)kRedrawGapMs; }

// Colours derived from the theme (RGB 0xRRGGBB): works for dark and light themes.
inline uint32_t mixRgb(uint32_t a, uint32_t b, int t256)
{
    uint32_t out = 0;
    for (int sh = 0; sh <= 16; sh += 8)
    {
        int ca = (int)((a >> sh) & 255), cb = (int)((b >> sh) & 255);
        out |= (uint32_t)((ca * (256 - t256) + cb * t256) >> 8) << sh;
    }
    return out;
}
// Text / line colour that reads on `bg` (dark theme -> light grey, light theme -> dark grey).
inline uint32_t contrastFg(uint32_t bg)
{
    int lum = (int)(((bg >> 16) & 255) * 30 + ((bg >> 8) & 255) * 59 + (bg & 255) * 11) / 100;
    return lum < 128 ? 0xE6E6E6u : 0x1E1E1Eu;
}
struct Palette
{
    uint32_t bg, lane, segAlt, grid, note, text, textDim, head, ghost, loop, rec, recText;
};
inline Palette makePalette(uint32_t bg, uint32_t fg, uint32_t red, uint32_t orange, uint32_t cyan)
{
    Palette p;
    p.bg = bg;
    p.lane = mixRgb(bg, fg, 20);
    p.segAlt = mixRgb(bg, fg, 38);
    p.grid = mixRgb(bg, fg, 90);
    p.note = mixRgb(bg, fg, 150);
    p.text = fg;
    p.textDim = mixRgb(bg, fg, 110);
    p.head = fg;
    p.ghost = cyan;
    p.loop = orange;
    p.rec = red;
    p.recText = 0xFFFFFF;
    return p;
}

} // namespace seqcut
