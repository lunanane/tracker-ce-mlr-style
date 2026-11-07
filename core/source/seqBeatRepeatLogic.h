// Sequencer tracks mod (plan 021) - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Beat repeat control from the CUT / MLR page: pad map, values, LEDs, legend words, the saved settings (sidecar "seqbrpt.bin") and the
// screen model. No Polyend includes, host-tested (core/seqtest/test_session.cpp, test_cut.inc; the DSP is libs/Audio/beat_repeat_core.h).
//
// The pads use the layout of the fader page (plans/004, 005 R3): four horizontal 8-step faders on rows 1-4, columns 1-8, the page /
// mode pads in columns 9-10, LATCH and DISENGAGE style pads in columns 11-12.
//
//          1  2  3  4  5  6  7  8 |  9      10   |  11     12
//   r1    fader 1 (8 steps)        |  REPEAT MIX  |  LATCH  OFF
//   r2    fader 2                  |  AUTO   REPL |   -     RESET
//   r3    fader 3                  |   -     GATE |   -     RESET
//   r4    fader 4                  |   -      -   |   -     RESET
//
//   page REPEAT: r1 GRID (momentary pads 1/4 1/8 1/16 1/32 1/4T 1/8T 1/16T 1/32T: hold = repeat at that grid, jam between them),
//                r2 GATE, r3 DECAY, r4 PITCH DECAY
//   page AUTO:   r1 INTERVAL (select), r2 CHANCE, r3 VARIATION, r4 GRID of the automatic repeats (select)
//   col 10: output mode MIX (the default: repeats on top of the running audio) / REPLACE (INSERT: the audio is muted while repeating) /
//           GATE (only the repeats pass); col 11 row 1: LATCH (the repeat goes on after the release); col 12 row 1: OFF (ends the repeat,
//           like DISENGAGE on the fader page); col 12 rows 2-4: RESET the row's value to its default.
#pragma once
#include "beat_repeat_core.h"
#include "seqCutLogic.h"
#include "seqSidecarLogic.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace seqbr {

using seqcut::kCols;
using seqcut::kRows;
using seqcut::kCutPads;
using seqcut::LED_OFF;
using seqcut::LED_DIM;
using seqcut::LED_MID;
using seqcut::LED_BRIGHT;

enum Page : uint8_t { BG_REPEAT = 0, BG_AUTO = 1, BG_COUNT = 2 };

// The 8 step values (each is 1..8 = the pad that was pressed; the lit bar of a fader)
enum Param : uint8_t { BP_GRIDPAD = 0, BP_GATE, BP_DECAY, BP_PITCH, BP_INTERVAL, BP_CHANCE, BP_VARIATION, BP_AUTOGRID, BP_COUNT };
enum Kind : uint8_t { BK_MOMENTARY = 0, BK_BAR, BK_SELECT };

inline Param paramOf(uint8_t page, uint8_t row)
{
    static const Param p[BG_COUNT][4] = { { BP_GRIDPAD, BP_GATE, BP_DECAY, BP_PITCH }, { BP_INTERVAL, BP_CHANCE, BP_VARIATION, BP_AUTOGRID } };
    return p[page % BG_COUNT][row & 3];
}
inline Kind kindOf(Param p)
{
    switch (p)
    {
        case BP_GRIDPAD: return BK_MOMENTARY;
        case BP_INTERVAL:
        case BP_AUTOGRID: return BK_SELECT;
        default: return BK_BAR;
    }
}
inline const char* paramName(Param p)
{
    static const char* const n[BP_COUNT] = { "GRID", "GATE", "DECAY", "PITCH DECAY", "INTERVAL", "CHANCE", "VARIATION", "AUTO GRID" };
    return n[p < BP_COUNT ? p : 0];
}
inline const char* paramHint(Param p)
{
    static const char* const n[BP_COUNT] = { "hold a pad = repeat at that grid, jam between pads", "how long an automatic repeat lasts",
                                            "level drop per repeat (100 = -6 dB)", "pitch drop per repeat (100 = -3 st)",
                                            "an automatic repeat may start every ...", "chance of a repeat at each interval",
                                            "chance and size of random grid changes", "grid of the automatic repeats" };
    return n[p < BP_COUNT ? p : 0];
}
inline const char* pageName(uint8_t page) { return page == BG_AUTO ? "AUTO" : "REPEAT"; }
inline const char* modeName(uint8_t mode)
{
    return mode == beatrepeat::MODE_INSERT ? "REPLACE" : (mode == beatrepeat::MODE_GATE ? "GATE" : "MIX");
}

// ---- step -> value ------------------------------------------------------------------------------------------------------------------
inline uint8_t percentOf(uint8_t step) { return (uint8_t)(((step < 1 ? 0 : (step > 8 ? 7 : step - 1)) * 100 + 3) / 7); } // 0 14 29 43 57 71 86 100
inline uint8_t variationOf(uint8_t step) { return (uint8_t)(((step < 1 ? 0 : (step > 8 ? 7 : step - 1)) * 10 + 3) / 7); } // 0 1 3 4 6 7 9 10
inline uint8_t clampStep(int s) { return (uint8_t)(s < 1 ? 1 : (s > 8 ? 8 : s)); }

// Defaults (the steps): auto grid 1/16, interval 1 bar, chance 0 (off), gate 16/16, variation 0, pitch decay 0, decay 0, mode MIX
struct BrParams
{
    uint8_t step[BP_COUNT];
    uint8_t mode;
    // Plan 026: settings of the MLR page that share this sidecar (it is the file of the CUT page): jump quantise (seqcut::Quant), the tracks
    // of slot A / B. Defaults beat (2), 0, 1. Saved only when changed (sidecar version 2); the defaults keep version 1.
    uint8_t mlrQuant, mlrSlotA, mlrSlotB;
    BrParams() { reset(); }
    void reset()
    {
        for (int i = 0; i < BP_COUNT; i++)
            step[i] = defaultStep((Param)i);
        mode = beatrepeat::MODE_MIX;
        mlrQuant = 2;
        mlrSlotA = 0;
        mlrSlotB = 1;
    }
    bool mlrDefault() const { return mlrQuant == 2 && mlrSlotA == 0 && mlrSlotB == 1; }
    static uint8_t defaultStep(Param p)
    {
        switch (p)
        {
            case BP_GATE: return 8;
            case BP_INTERVAL: return 6;
            case BP_AUTOGRID: return 3;
            case BP_GRIDPAD: return 0;
            default: return 1;
        }
    }
    bool isDefault() const
    {
        for (int i = 0; i < BP_COUNT; i++)
            if (step[i] != defaultStep((Param)i))
                return false;
        return mode == beatrepeat::MODE_MIX && mlrDefault();
    }
    bool operator==(const BrParams& o) const { return memcmp(this, &o, sizeof(*this)) == 0; }
    beatrepeat::Settings settings() const
    {
        beatrepeat::Settings s;
        s.grid = (uint8_t)(clampStep(step[BP_AUTOGRID]) - 1);
        s.interval = (uint8_t)(clampStep(step[BP_INTERVAL]) - 1);
        s.chance = percentOf(step[BP_CHANCE]);
        s.gate = (uint8_t)(clampStep(step[BP_GATE]) - 1);
        s.variation = variationOf(step[BP_VARIATION]);
        s.pitchDecay = percentOf(step[BP_PITCH]);
        s.decay = percentOf(step[BP_DECAY]);
        s.mode = mode < beatrepeat::MODE_COUNT ? mode : (uint8_t)beatrepeat::MODE_MIX;
        return s;
    }

    // ---- sidecar "seqbrpt.bin": frame of seqSidecarLogic.h, magic "SBRP", payload = the 7 steps + mode (8 bytes, version 1), plus (version 2)
    // the MLR page settings: quantise, slot A track, slot B track (11 bytes) ----
    // Oldest version that fits: a file whose MLR settings are the defaults is written as version 1 (readable by every earlier firmware);
    // version 2 only when the quantise or a slot track was changed. The reader takes the bytes it knows and leaves the rest, a newer file
    // than the reader is never rewritten (FRAME_NEWER).
    static const char* magic() { return "SBRP"; }
    enum : uint8_t { kVersion = 2 };
    enum : size_t { kPayload = 8, kPayload2 = 11, kFileMax = kPayload2 + seqsidecar::kOverhead };
    void raw(uint8_t* p) const
    {
        p[0] = step[BP_AUTOGRID];
        p[1] = step[BP_INTERVAL];
        p[2] = step[BP_CHANCE];
        p[3] = step[BP_GATE];
        p[4] = step[BP_VARIATION];
        p[5] = step[BP_PITCH];
        p[6] = step[BP_DECAY];
        p[7] = mode;
        p[8] = mlrQuant;
        p[9] = mlrSlotA;
        p[10] = mlrSlotB;
    }
    uint16_t hash() const
    {
        uint8_t b[kPayload2];
        raw(b);
        return seqsidecar::crc16(b, kPayload2);
    }
    size_t save(uint8_t* buf, size_t cap) const
    {
        if (cap < (size_t)kFileMax)
            return 0;
        uint8_t* p = buf + seqsidecar::kHeader;
        raw(p);
        const bool v1 = mlrDefault(); // oldest version that fits
        return seqsidecar::frameWrite(buf, cap, magic(), v1 ? 1 : 2, p, v1 ? kPayload : kPayload2);
    }
    // false: missing / bad frame (nothing changed). *newer: written by a newer mod version (leave the file alone). A short payload keeps
    // the defaults for what is missing; a value out of range falls back to its own default.
    bool load(const uint8_t* buf, size_t len, bool* newer = nullptr)
    {
        const uint8_t* p = nullptr;
        size_t n = 0;
        uint8_t v = 0;
        seqsidecar::FrameStatus st = seqsidecar::frameRead(buf, len, magic(), kVersion, &p, &n, &v);
        if (newer)
            *newer = st == seqsidecar::FRAME_NEWER;
        if (st != seqsidecar::FRAME_OK)
            return false;
        BrParams d;
        static const Param order[7] = { BP_AUTOGRID, BP_INTERVAL, BP_CHANCE, BP_GATE, BP_VARIATION, BP_PITCH, BP_DECAY };
        for (size_t i = 0; i < 7; i++)
            if (i < n && p[i] >= 1 && p[i] <= 8)
                d.step[order[i]] = p[i];
        if (n > 7 && p[7] < beatrepeat::MODE_COUNT)
            d.mode = p[7];
        if (v >= 2 && n >= kPayload2 && p[8] < 7 && p[9] < 16 && p[10] < 16 && p[9] != p[10]) // 7 = seqcut::Q_COUNT
        {
            d.mlrQuant = p[8];
            d.mlrSlotA = p[9];
            d.mlrSlotB = p[10];
        }
        *this = d;
        return true;
    }
};

// ---- pad map -------------------------------------------------------------------------------------------------------------------------
enum Cell : uint8_t { BC_NONE = 0, BC_FADER, BC_PAGE, BC_MODE, BC_LATCH, BC_OFF, BC_RESET };
struct PadInfo
{
    Cell cell;
    uint8_t index; // FADER: row, PAGE: page, MODE: output mode, RESET: row
    uint8_t step;  // FADER: 1..8
};
inline PadInfo mapPad(uint8_t col, uint8_t row)
{
    PadInfo p = { BC_NONE, 0, 0 };
    if (col >= kCols || row >= kRows)
        return p;
    if (col < 8)
    {
        p.cell = BC_FADER;
        p.index = row;
        p.step = (uint8_t)(col + 1);
    }
    else if (col == 8)
    {
        if (row < BG_COUNT)
        {
            p.cell = BC_PAGE;
            p.index = row;
        }
    }
    else if (col == 9)
    {
        if (row < beatrepeat::MODE_COUNT)
        {
            p.cell = BC_MODE;
            p.index = row; // MODE_MIX, MODE_INSERT ("REPLACE"), MODE_GATE
        }
    }
    else if (col == 10)
    {
        if (row == 0)
            p.cell = BC_LATCH;
    }
    else
    {
        if (row == 0)
            p.cell = BC_OFF;
        else
        {
            p.cell = BC_RESET;
            p.index = row;
        }
    }
    return p;
}

// ---- the controller: pad events -> what the audio side has to be told ---------------------------------------------------------------
enum Action : uint8_t
{
    BA_NONE = 0,
    BA_DOWN,     // a grid pad: repeat at grid `arg`
    BA_UP,       // the last grid pad was released
    BA_LATCH,    // latch on (arg 1) / off (arg 0)
    BA_OFF,      // end the repeat
    BA_SETTINGS  // values or mode changed: push the settings
};
struct Result
{
    Action act;
    uint8_t arg;
};

struct Ui
{
    uint8_t page = BG_REPEAT;
    bool latch = false;
    uint8_t held[8];      // grid pads held, in press order
    uint8_t nHeld = 0;
    BrParams prm;

    uint8_t heldMask() const
    {
        uint8_t m = 0;
        for (uint8_t i = 0; i < nHeld; i++)
            m = (uint8_t)(m | (1u << held[i]));
        return m;
    }
    // A pad event on the beat repeat layer. Up to two actions result (a release can re-grid and the row can change a value): `r` has room
    // for 2 entries; returns how many.
    int pad(uint8_t col, uint8_t row, bool down, Result r[2])
    {
        r[0].act = r[1].act = BA_NONE;
        r[0].arg = r[1].arg = 0;
        const PadInfo p = mapPad(col, row);
        if (p.cell == BC_FADER && kindOf(paramOf(page, p.index)) == BK_MOMENTARY)
        {
            const uint8_t g = (uint8_t)(p.step - 1);
            if (down)
            {
                removeHeld(g);
                if (nHeld < 8)
                    held[nHeld++] = g;
                r[0].act = BA_DOWN;
                r[0].arg = g;
                return 1;
            }
            const bool newest = nHeld > 0 && held[nHeld - 1] == g;
            if (!removeHeld(g))
                return 0;
            if (nHeld > 0)
            {
                if (!newest)
                    return 0; // an older pad let go while a newer one is held: nothing changes
                r[0].act = BA_DOWN; // jam: the pad that is still held takes over
                r[0].arg = held[nHeld - 1];
                return 1;
            }
            r[0].act = BA_UP;
            return 1;
        }
        if (!down)
            return 0;
        switch (p.cell)
        {
            case BC_FADER:
            {
                const Param par = paramOf(page, p.index);
                prm.step[par] = p.step;
                r[0].act = BA_SETTINGS;
                return 1;
            }
            case BC_PAGE: page = p.index; return 0;
            case BC_MODE:
                prm.mode = p.index;
                r[0].act = BA_SETTINGS;
                return 1;
            case BC_LATCH:
                latch = !latch;
                r[0].act = BA_LATCH;
                r[0].arg = latch ? 1 : 0;
                return 1;
            case BC_OFF:
                r[0].act = BA_OFF;
                return 1;
            case BC_RESET:
            {
                const Param par = paramOf(page, p.index);
                if (kindOf(par) == BK_MOMENTARY)
                    return 0;
                prm.step[par] = BrParams::defaultStep(par);
                r[0].act = BA_SETTINGS;
                return 1;
            }
            default: return 0;
        }
    }
    // the layer is left / the panic: the pads in the hand are let go
    bool releaseAll()
    {
        const bool had = nHeld > 0;
        nHeld = 0;
        return had;
    }
    void panic()
    {
        nHeld = 0;
        latch = false;
    }

  private:
    bool removeHeld(uint8_t g)
    {
        for (uint8_t i = 0; i < nHeld; i++)
            if (held[i] == g)
            {
                for (uint8_t k = i; k + 1 < nHeld; k++)
                    held[k] = held[k + 1];
                nHeld--;
                return true;
            }
        return false;
    }
};

// ---- LEDs ---------------------------------------------------------------------------------------------------------------------------
struct LedState
{
    uint8_t page, mode, latch;
    uint8_t heldMask;   // grid pads held
    uint8_t eventGrid;  // grid id of the repeat in progress
    uint8_t state;      // beatrepeat::State
    uint8_t step[BP_COUNT];
};
inline void fillLeds(const LedState& s, bool blink, uint8_t out[kCutPads])
{
    for (int i = 0; i < kCutPads; i++)
        out[i] = LED_OFF;
    for (uint8_t row = 0; row < 4; row++)
    {
        const Param par = paramOf(s.page, row);
        uint8_t* r = out + row * kCols;
        for (uint8_t c = 0; c < 8; c++)
        {
            const uint8_t st = (uint8_t)(c + 1);
            switch (kindOf(par))
            {
                case BK_MOMENTARY:
                    r[c] = LED_DIM;
                    if (s.state != beatrepeat::S_IDLE && s.eventGrid == c)
                        r[c] = (s.state == beatrepeat::S_PENDING) ? (blink ? LED_BRIGHT : LED_MID) : LED_BRIGHT;
                    if ((s.heldMask >> c) & 1u)
                        r[c] = LED_BRIGHT;
                    break;
                case BK_SELECT: r[c] = s.step[par] == st ? LED_BRIGHT : LED_DIM; break;
                default: r[c] = st < s.step[par] ? LED_MID : (st == s.step[par] ? LED_BRIGHT : LED_DIM); break;
            }
        }
        // columns 9-12
        r[8] = row < BG_COUNT ? (s.page == row ? LED_BRIGHT : LED_DIM) : LED_OFF;
        r[9] = row < beatrepeat::MODE_COUNT ? (s.mode == row ? LED_BRIGHT : LED_DIM) : LED_OFF;
        r[10] = row == 0 ? (s.latch ? LED_BRIGHT : LED_DIM) : LED_OFF;
        if (row == 0)
            r[11] = s.state != beatrepeat::S_IDLE ? (blink ? LED_BRIGHT : LED_MID) : LED_DIM;
        else
            r[11] = (kindOf(par) != BK_MOMENTARY && s.step[par] != BrParams::defaultStep(par)) ? LED_MID : LED_DIM;
    }
}

// ---- words (legend, screen) ---------------------------------------------------------------------------------------------------------
struct LegendCell
{
    char word[8];
    uint8_t on;
    uint8_t dim;
};
inline LegendCell legendCell(uint8_t col, uint8_t row, const LedState& s)
{
    LegendCell c;
    c.word[0] = 0;
    c.on = 0;
    c.dim = 0;
    const PadInfo p = mapPad(col, row);
    switch (p.cell)
    {
        case BC_FADER:
        {
            const Param par = paramOf(s.page, p.index);
            switch (par)
            {
                case BP_GRIDPAD:
                    snprintf(c.word, sizeof(c.word), "%s", beatrepeat::gridName((uint8_t)(p.step - 1)));
                    c.on = ((s.heldMask >> (p.step - 1)) & 1u) != 0 || (s.state != beatrepeat::S_IDLE && s.eventGrid == p.step - 1);
                    break;
                case BP_INTERVAL:
                    snprintf(c.word, sizeof(c.word), "%s", beatrepeat::intervalName((uint8_t)(p.step - 1)));
                    c.on = s.step[par] == p.step;
                    break;
                case BP_AUTOGRID:
                    snprintf(c.word, sizeof(c.word), "%s", beatrepeat::gridName((uint8_t)(p.step - 1)));
                    c.on = s.step[par] == p.step;
                    break;
                case BP_GATE: snprintf(c.word, sizeof(c.word), "%s", beatrepeat::gateName((uint8_t)(p.step - 1))); c.on = p.step <= s.step[par]; break;
                case BP_VARIATION: snprintf(c.word, sizeof(c.word), "%d", variationOf(p.step)); c.on = p.step <= s.step[par]; break;
                default: snprintf(c.word, sizeof(c.word), "%d", percentOf(p.step)); c.on = p.step <= s.step[par]; break;
            }
            break;
        }
        case BC_PAGE: snprintf(c.word, sizeof(c.word), "%s", p.index == BG_AUTO ? "AUTO" : "REPT"); c.on = s.page == p.index; break;
        case BC_MODE: snprintf(c.word, sizeof(c.word), "%s", p.index == beatrepeat::MODE_INSERT ? "REPL" : modeName(p.index)); c.on = s.mode == p.index; break;
        case BC_LATCH: snprintf(c.word, sizeof(c.word), "LATCH"); c.on = s.latch != 0; break;
        case BC_OFF: snprintf(c.word, sizeof(c.word), "OFF"); c.on = s.state != beatrepeat::S_IDLE; break;
        case BC_RESET: snprintf(c.word, sizeof(c.word), "RESET"); break;
        default: c.dim = 1; break;
    }
    return c;
}

// value text of a row ("1/16", "-3.0 st", "50 %", ...); kept short for the 8 cells and the band
inline void valueText(Param p, uint8_t step, char* out, size_t cap)
{
    switch (p)
    {
        case BP_GRIDPAD: snprintf(out, cap, "hold"); break;
        case BP_GATE: snprintf(out, cap, "%s", beatrepeat::gateName((uint8_t)(clampStep(step) - 1))); break;
        case BP_DECAY:
        {
            const int d = percentOf(step) * 60 / 100; // tenths of a dB per repeat
            if (d)
                snprintf(out, cap, "-%d.%d dB", d / 10, d % 10);
            else
                snprintf(out, cap, "off");
            break;
        }
        case BP_PITCH:
        {
            const int s = percentOf(step) * 30 / 100; // tenths of a semitone per repeat
            if (s)
                snprintf(out, cap, "-%d.%d st", s / 10, s % 10);
            else
                snprintf(out, cap, "off");
            break;
        }
        case BP_INTERVAL: snprintf(out, cap, "%s", beatrepeat::intervalName((uint8_t)(clampStep(step) - 1))); break;
        case BP_CHANCE: snprintf(out, cap, percentOf(step) ? "%d %%" : "off", percentOf(step)); break;
        case BP_VARIATION: snprintf(out, cap, "%d", variationOf(step)); break;
        case BP_AUTOGRID: snprintf(out, cap, "%s", beatrepeat::gridName((uint8_t)(clampStep(step) - 1))); break;
        default: out[0] = 0; break;
    }
}

// ---- screen model ---------------------------------------------------------------------------------------------------------------------
enum { kBrWave = 76, kBrBlocks = 8, kBrViewW = 608 }; // kBrViewW: width of the slice view in pixels (8 per waveform column)
struct BrModel
{
    uint8_t page, mode, latch, state;
    uint8_t heldMask, eventGrid, setGrid, clamped; // setGrid: grid asked for; clamped: the tempo could not hold it (eventGrid is shorter)
    uint8_t step[BP_COUNT];
    uint8_t repeatIdx, levelDb, pitchSemi, cpu10; // cpu10: CPU of the stage in 0.1 % (0 while idle)
    uint16_t bpm10;
    uint8_t waveOk, pad_[3];
    int8_t lo[kBrWave], hi[kBrWave];
};
struct BrHead
{
    int16_t x;       // playhead offset inside the slice view, -1 = none
    uint8_t state, repeatIdx;
};
inline BrHead makeBrHead(uint8_t state, uint16_t slice1000, uint8_t repeatIdx, int width)
{
    BrHead h;
    h.state = state;
    h.repeatIdx = repeatIdx;
    h.x = (state == beatrepeat::S_CAPTURE || state == beatrepeat::S_REPEAT) ? (int16_t)((slice1000 * width) / 1000) : (int16_t)-1;
    return h;
}

// Level of the k-th repeat as a fraction of full (0..1000) for the decay curve on the screen
inline int blockLevel1000(uint8_t decayPercent, int k)
{
    float g = 1.0f;
    for (int i = 0; i < k; i++)
        g *= powf(0.5011872f, (float)decayPercent / 100.0f);
    return (int)(g * 1000.0f + 0.5f);
}

inline int8_t scale(int v) // 16 bit sample -> -127..127
{
    const int s = v / 258;
    return (int8_t)(s > 127 ? 127 : (s < -127 ? -127 : s));
}
// Waveform columns of the captured slice from interleaved L R frames (ring): min / max of the left channel per column, -127..127
inline void foldWave(const int16_t* ring, uint32_t frames, int8_t lo[kBrWave], int8_t hi[kBrWave])
{
    for (int j = 0; j < kBrWave; j++)
    {
        const uint32_t a = frames ? (uint32_t)(((uint64_t)j * frames) / kBrWave) : 0u;
        uint32_t b = frames ? (uint32_t)(((uint64_t)(j + 1) * frames) / kBrWave) : 0u;
        if (b <= a)
            b = a + 1;
        const uint32_t span = b - a;
        const uint32_t stride = span > 12 ? span / 12 : 1; // at most about 12 reads per column: the ring is slow SDRAM
        int mn = 32767, mx = -32768;
        for (uint32_t i = a; i < b && i < frames; i += stride)
        {
            const int v = ring[2 * i];
            if (v < mn)
                mn = v;
            if (v > mx)
                mx = v;
        }
        if (mn > mx)
            mn = mx = 0;
        lo[j] = scale(mn);
        hi[j] = scale(mx);
    }
}

} // namespace seqbr
