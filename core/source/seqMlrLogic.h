// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// MLR with two SLOTS on a musical grid (plan 016 A). Pure helpers, no Polyend includes, host-tested in core/seqtest.
//
//   Slot A = pad rows 1-2, slot B = pad rows 3-4. Each slot holds one track (picked with "Assign slot 1/2" on the screen buttons).
//   Per slot: 8x2 pads (cols 1-8) = 16 STEPS of the musical grid (1/4 = one beat = 4 pattern steps, or 1/8 = 2 steps per pad) =
//   one SECTION (64 or 32 pattern steps). The pads address the section the playhead is in (FOLLOW) or the one that was held (HOLD).
//   Steps behind the end of the pattern are dark and inactive.
//   cols 9-12: first row  STOP  REV   RATE  RSYNC      second row  GRID  SECT  REC  MUTE
#pragma once
#include "seqCutLogic.h"
#include <stdio.h>
#include <string.h>

namespace seqcut {

enum { kSlots = 2, kMlrPads = 16, kPickTracks = 16, kWaveCols = 152 };

// ---- grid and sections ---------------------------------------------------------------------------------------------------
enum Grid : uint8_t { GRID_4 = 0, GRID_8 = 1, GRID_COUNT = 2 };
inline int gridSteps(uint8_t g) { return g == GRID_8 ? 2 : 4; }                       // pattern steps per pad
inline const char* gridName(uint8_t g) { return g == GRID_8 ? "1/8" : "1/4"; }
inline int sectionSteps(int spp) { return spp * kMlrPads; }
inline int sectionCount(int len, int spp)
{
    const int ss = sectionSteps(spp < 1 ? 1 : spp);
    return len < 1 ? 1 : (len + ss - 1) / ss;
}
inline int sectionOf(int pos, int spp)
{
    const int ss = sectionSteps(spp < 1 ? 1 : spp);
    return pos < 0 ? 0 : pos / ss;
}
// Pads of section `sec` that map to a step of a pattern of `len` steps (16 unless the section is the short last one).
inline int padsIn(int sec, int len, int spp)
{
    const int left = len - sec * sectionSteps(spp);
    return left <= 0 ? 0 : (left >= sectionSteps(spp) ? kMlrPads : (left + spp - 1) / spp);
}
// Pad (0..15) of position `pos` inside section `sec`, or 0xFF when the position is in another section.
inline uint8_t padOfPos(int pos, int sec, int spp)
{
    const int base = sec * sectionSteps(spp);
    if (pos < base || pos >= base + sectionSteps(spp)) return 0xFF;
    return (uint8_t)((pos - base) / spp);
}

// ---- commands (SUB_RESET slot of the ring byte; the track is the command's track) ---------------------------------------------
//   col 6, 7  = GRID 1/4, 1/8 for the track (also ends a section hold: the section index means something else)
//   col 8, 9  = SECTION follow / hold (hold = the section the track plays in now)
inline uint8_t packGrid(uint8_t g) { return packArg(SUB_RESET, false, (uint8_t)(6 + (g % GRID_COUNT))); }
inline bool argIsGrid(uint8_t a) { return argSub(a) == SUB_RESET && (argCol(a) == 6 || argCol(a) == 7); }
inline uint8_t argGrid(uint8_t a) { return (uint8_t)(argCol(a) - 6); }
inline uint8_t packHold(bool on) { return packArg(SUB_RESET, false, (uint8_t)(on ? 9 : 8)); }
inline bool argIsHold(uint8_t a) { return argSub(a) == SUB_RESET && (argCol(a) == 8 || argCol(a) == 9); }
inline bool argHold(uint8_t a) { return argCol(a) == 9; }

// Screen buttons (0-based) of the MLR layer: 0 Assign slot 1, 1 Assign slot 2, 2 Help (pad legend), 3 empty, 4 Q (quantise),
// 5 SYNC ALL (the layer's own button), 6 shows [REC] while a take is armed, 7 CLIPS (the host's).
enum { kButtonAssign1 = 0, kButtonAssign2 = 1, kButtonHelp = 2 };

// ---- pad map ------------------------------------------------------------------------------------------------------------------
enum PadRole : uint8_t { PR_STEP = 0, PR_STOP, PR_REV, PR_RATE, PR_RSYNC, PR_GRID, PR_HOLD, PR_REC, PR_MUTE, PR_NONE };
struct PadInfo
{
    uint8_t role, slot, idx; // idx = step 0..15 for PR_STEP
};
inline PadInfo padInfo(uint8_t col, uint8_t row)
{
    PadInfo p = { PR_NONE, (uint8_t)(row / 2), 0 };
    if (col >= kCols || row >= kRows) return p;
    const uint8_t second = (uint8_t)(row & 1);
    if (col < kPosCols)
    {
        p.role = PR_STEP;
        p.idx = (uint8_t)(second * kPosCols + col);
        return p;
    }
    static const uint8_t ctl[2][4] = { { PR_STOP, PR_REV, PR_RATE, PR_RSYNC }, { PR_GRID, PR_HOLD, PR_REC, PR_MUTE } };
    p.role = ctl[second][col - kPosCols];
    return p;
}

// ---- LEDs ---------------------------------------------------------------------------------------------------------------------
struct SlotLeds
{
    View v;
    uint8_t track;
    uint8_t picking, muted, rec; // picker open, project mute, a take of this slot is armed
    uint16_t has;                // picker: tracks that hold something (bit per track)
};
inline void fillMlrLeds(const SlotLeds s[kSlots], bool blink, uint8_t out[kCutPads])
{
    for (int i = 0; i < kCutPads; i++) out[i] = LED_OFF;
    for (int k = 0; k < kSlots; k++)
    {
        const SlotLeds& sl = s[k];
        const View& v = sl.v;
        uint8_t* r0 = out + (2 * k) * kCols;
        uint8_t* r1 = out + (2 * k + 1) * kCols;
        for (int i = 0; i < kMlrPads; i++)
        {
            uint8_t* cell = (i < kPosCols ? r0 : r1) + (i % kPosCols);
            if (sl.picking)
            {
                *cell = i == sl.track ? LED_BRIGHT : (((sl.has >> i) & 1u) ? LED_MID : LED_OFF);
                continue;
            }
            if (i >= v.pads) continue; // behind the end of the pattern: dark
            *cell = LED_DIM;
            if (v.loopLo <= v.loopHi && i >= v.loopLo && i <= v.loopHi) *cell = LED_LOOP;
            if (!(v.flags & VF_STOPPED) && v.col == i) *cell = LED_BRIGHT;
            if (v.flags & VF_STOPPED) *cell = LED_OFF;
        }
        r0[COL_STOP] = (v.flags & VF_STOPPED) ? LED_BRIGHT : LED_DIM;
        r0[COL_REVERSE] = (v.flags & VF_REVERSE) ? LED_BRIGHT : LED_DIM;
        const uint8_t rate = viewRate(v);
        r0[COL_RATE] = rate == 1 ? LED_DIM : (rate == 0 ? LED_MID : LED_BRIGHT);
        if (v.flags & VF_REC) r0[COL_RESYNC] = blink ? LED_BRIGHT : LED_OFF;
        else if (v.flags & VF_RECORDED) r0[COL_RESYNC] = LED_BRIGHT;
        else r0[COL_RESYNC] = (v.flags & VF_ACTIVE) ? LED_MID : LED_DIM;
        r1[COL_STOP] = (v.flags2 & V2_GRID8) ? LED_BRIGHT : LED_DIM;
        r1[COL_REVERSE] = (v.flags2 & V2_HOLD) ? LED_BRIGHT : LED_DIM;
        r1[COL_RATE] = (v.flags & VF_REC) ? (blink ? LED_BRIGHT : LED_MID) : (sl.rec ? LED_BRIGHT : LED_DIM);
        r1[COL_RESYNC] = sl.muted ? LED_BRIGHT : LED_DIM;
    }
}

// ---- the legend (a pad map on the screen) ------------------------------------------------------------------------------------
struct LegendCell
{
    char word[8];
    uint8_t on;    // state highlighted
    uint8_t dim;   // inactive pad
};
inline LegendCell legendCell(uint8_t col, uint8_t row, const SlotLeds& sl)
{
    LegendCell c;
    c.word[0] = 0;
    c.on = 0;
    c.dim = 0;
    const PadInfo p = padInfo(col, row);
    const View& v = sl.v;
    switch (p.role)
    {
        case PR_STEP:
            if (sl.picking)
            {
                snprintf(c.word, sizeof(c.word), "T%d", p.idx + 1);
                c.on = p.idx == sl.track;
                c.dim = !((sl.has >> p.idx) & 1u) && !c.on;
            }
            else
            {
                snprintf(c.word, sizeof(c.word), "%d", p.idx + 1);
                c.dim = p.idx >= v.pads;
                c.on = v.col == p.idx;
            }
            break;
        case PR_STOP: snprintf(c.word, sizeof(c.word), "%s", (v.flags & VF_STOPPED) ? "START" : "STOP"); c.on = (v.flags & VF_STOPPED) != 0; break;
        case PR_REV: snprintf(c.word, sizeof(c.word), "REV"); c.on = (v.flags & VF_REVERSE) != 0; break;
        case PR_RATE:
        {
            const uint8_t r = viewRate(v);
            snprintf(c.word, sizeof(c.word), "%s", r == 0 ? "RATE/2" : (r == 2 ? "RATEx2" : "RATE"));
            c.on = r != 1;
            break;
        }
        case PR_RSYNC: snprintf(c.word, sizeof(c.word), "RSYNC"); c.on = (v.flags & VF_ACTIVE) != 0; break;
        case PR_GRID: snprintf(c.word, sizeof(c.word), "GRID/%d", (v.flags2 & V2_GRID8) ? 8 : 4); c.on = (v.flags2 & V2_GRID8) != 0; break;
        case PR_HOLD: snprintf(c.word, sizeof(c.word), "%s", (v.flags2 & V2_HOLD) ? "HOLD" : "FOLLOW"); c.on = (v.flags2 & V2_HOLD) != 0; break;
        case PR_REC: snprintf(c.word, sizeof(c.word), "REC"); c.on = (v.flags & VF_REC) != 0 || sl.rec; break;
        case PR_MUTE: snprintf(c.word, sizeof(c.word), "MUTE"); c.on = sl.muted != 0; break;
        default: break;
    }
    return c;
}

// ---- waveform overview ---------------------------------------------------------------------------------------------------------
// One min/max pair per screen column of the lane (kWaveCols), -127..127 (the peak of the section, normalised to the lane height).
struct WaveCol
{
    int8_t lo, hi;
};
inline int8_t waveScale(int v) // 16 bit sample -> -127..127
{
    int s = v / 258;
    return (int8_t)(s > 127 ? 127 : (s < -127 ? -127 : s));
}
// Fold `n` samples (stride `stride`) into a column.
inline void waveFold(WaveCol& c, const int16_t* s, int n, int stride)
{
    int lo = 32767, hi = -32768;
    for (int i = 0; i < n; i++)
    {
        const int v = s[i * stride];
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    if (n <= 0) lo = hi = 0;
    c.lo = waveScale(lo);
    c.hi = waveScale(hi);
}
// Column j of kWaveCols covers section steps [j*ss/kWaveCols, (j+1)*ss/kWaveCols).
inline int waveColStep(int j, int sectionLen) { return (j * sectionLen) / kWaveCols; }

// ---- lane screen (geometry, models) ---------------------------------------------------------------------------------------------
constexpr int kMlrLaneX = 88;     // x of the lane: 16 pads of 38 px
constexpr int kMlrPadW = 38;
constexpr int kMlrLaneW = 16 * kMlrPadW; // 608
constexpr int kMlrLaneTop = 70;
constexpr int kMlrLaneH = 164;
constexpr int kMlrLaneStride = 172;
constexpr int kMlrBlockX = 700;   // names of the extra pads: 2 columns of 49 px (cells 47 wide)
constexpr int kMlrWaveMid = 120;  // y of the waveform centre inside a lane (offset from the lane top)

struct SlotInfo
{
    uint8_t track, flags, flags2, spp, sec, nsec, pads, loopLo, loopHi, picking, muted, rec, waveOk, pad_[3];
    uint16_t cacheMask, noteMask, has, pad2_;
    WaveCol wave[kWaveCols];
};
struct MlrModel
{
    uint8_t recHeld, legend, pickSlot, pad_;
    uint8_t legendPad[kSlots]; // legend only: the pad each playhead is on (0xFF none), so the legend follows the playhead
    uint8_t pad2_[2];
    SlotInfo slot[kSlots];
};
struct MlrHead
{
    int16_t x;     // playhead offset in the lane, -1 = not in the shown section
    int16_t ghost; // where the track would be on the grid (stock position), -1 = none
    uint8_t pad;   // pad the playhead is in (0xFF none)
    uint8_t flags;
};
struct MlrHeadModel
{
    MlrHead h[kSlots];
    uint8_t bar, beat, recHeld, legend;
};

// x offset (0..kMlrLaneW-1) of position `pos` + frac48/48 of a step inside the section `sec`, -1 when the position is elsewhere.
inline int mlrOffset(int pos, int frac48, int sec, int spp)
{
    const uint8_t pad = padOfPos(pos, sec, spp);
    if (pad == 0xFF) return -1;
    if (frac48 < 0) frac48 = 0;
    if (frac48 > 47) frac48 = 47;
    const int within = ((pos - sec * sectionSteps(spp)) % spp) * 48 + frac48;
    return pad * kMlrPadW + (within * kMlrPadW) / (spp * 48);
}
inline MlrHead makeMlrHead(const View& v, int pos, int uStep, int stock)
{
    MlrHead h;
    h.flags = v.flags;
    const int spp = (v.flags2 & V2_GRID8) ? 2 : 4;
    h.x = (int16_t)mlrOffset(pos, laneFrac(uStep, (v.flags & VF_REVERSE) != 0), v.sec, spp);
    h.pad = padOfPos(pos, v.sec, spp);
    h.ghost = -1;
    if ((v.flags & VF_ACTIVE) && stock != pos && stock >= 0) h.ghost = (int16_t)mlrOffset(stock, 0, v.sec, spp);
    return h;
}

} // namespace seqcut
