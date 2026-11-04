// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Pure mapping logic of the alt Performance page (plans/004-perform-faders.md): pad -> cell, fader step ->
// value per parameter, LED bars. No Polyend includes, host-testable (core/seqtest/test_perform.cpp).
#pragma once
#include <stdint.h>

namespace seqperf {

enum Cell : uint8_t { CELL_NONE = 0, CELL_FADER, CELL_GROUP, CELL_PING, CELL_REV, CELL_LATCH, CELL_RESET };

// Pages (what the four faders control). FX pages use the stock perform slots 4*page .. 4*page+3.
enum Page : uint8_t { PAGE_FX1 = 0, PAGE_FX2 = 1, PAGE_FX3 = 2, PAGE_PING = 3, PAGE_REV = 4, PAGE_COUNT = 5 };

struct PadInfo
{
    Cell cell;
    uint8_t index; // FADER: row 0..3, GROUP: 0..2 (= page), LATCH / RESET: fader row
    uint8_t step;  // FADER: 1..8 (column + 1)
};

// Pad number 0..47 = row * 12 + column. Column 9 (index 8): groups in rows 1-3, row 4 dark on purpose.
// Column 10: PING (row 1), REV (row 2). Column 11: LATCH. Column 12: RESET.
inline PadInfo mapPad(uint8_t pad)
{
    PadInfo p = { CELL_NONE, 0, 0 };
    if (pad >= 48)
        return p;
    uint8_t col = (uint8_t)(pad % 12), row = (uint8_t)(pad / 12);
    if (col < 8)
    {
        p.cell = CELL_FADER;
        p.index = row;
        p.step = (uint8_t)(col + 1);
    }
    else if (col == 8)
    {
        if (row < 3)
        {
            p.cell = CELL_GROUP;
            p.index = row;
        }
    }
    else if (col == 9)
    {
        if (row == 0)
            p.cell = CELL_PING;
        else if (row == 1)
            p.cell = CELL_REV;
    }
    else if (col == 10)
    {
        p.cell = CELL_LATCH;
        p.index = row;
    }
    else
    {
        p.cell = CELL_RESET;
        p.index = row;
    }
    return p;
}

inline uint8_t padOf(uint8_t row, uint8_t col) { return (uint8_t)(row * 12 + col); }

// Bar from the left: step 0..8 -> mask, bit c = column c lit.
inline uint8_t barMask(uint8_t step)
{
    if (step >= 8)
        return 0xFF;
    return (uint8_t)((1u << step) - 1u);
}

inline bool isFxPage(uint8_t page) { return page <= PAGE_FX3; }
inline uint8_t slotOf(uint8_t page, uint8_t row) { return (uint8_t)(page * 4 + row); }

// ---- stock perform FX: step 1..8 spread over the FX range, step 0 = neutral (0) ----
inline int16_t fxValue(uint8_t step, int16_t mn, int16_t mx)
{
    if (step == 0)
        return 0;
    if (step > 8)
        step = 8;
    int32_t span = (int32_t)mx - (int32_t)mn;
    return (int16_t)(mn + (span * step + (span >= 0 ? 4 : -4)) / 8);
}

// Send fader (PING / REV fader 1): perform send offset 0..100 on the selected tracks.
inline int16_t sendValue(uint8_t step)
{
    if (step > 8)
        step = 8;
    return (int16_t)((step * 100 + 4) / 8);
}

// ---- project values of the PING / REV pages: step 1..8 covers the whole range (pad 1 = minimum) ----
enum Param : uint8_t { P_TIME, P_RATE, P_FEEDBACK, P_FILTER, P_SIZE, P_DAMP, P_PREDELAY, P_COUNT };

inline double paramValue(Param p, int step)
{
    if (step < 1)
        step = 1;
    if (step > 8)
        step = 8;
    static const double filterHz[8] = { 194, 376, 729, 1414, 2742, 5318, 10313, 20000 };
    switch (p)
    {
        case P_TIME: return (double)(3500 * step * step / 64);       // ms, quadratic: short times reachable
        case P_RATE: return (double)((18 * (step - 1) + 3) / 7);     // sync rate index 0..18
        case P_FEEDBACK: return (double)((99 * (step - 1) + 3) / 7); // 0..99
        case P_FILTER: return filterHz[step - 1];                    // Hz, log spaced
        default: return (double)(step - 1) / 7.0;                    // reverb size / damp / predelay 0..1
    }
}

// Which step (1..8) shows the current value (nearest).
inline uint8_t nearestStep(Param p, double v)
{
    uint8_t best = 1;
    double bd = 1e30;
    for (int s = 1; s <= 8; s++)
    {
        double d = paramValue(p, s) - v;
        if (d < 0)
            d = -d;
        if (d < bd)
        {
            bd = d;
            best = (uint8_t)s;
        }
    }
    return best;
}

// Page change by a group / PING / REV pad. Other cells keep the current page.
inline uint8_t pageForCell(Cell c, uint8_t index, uint8_t current)
{
    if (c == CELL_GROUP && index < 3)
        return index;
    if (c == CELL_PING)
        return PAGE_PING;
    if (c == CELL_REV)
        return PAGE_REV;
    return current;
}

// Is the pad of a non-fader cell lit?
inline bool padIsLit(const PadInfo& p, uint8_t page, bool latch)
{
    switch (p.cell)
    {
        case CELL_GROUP: return page == p.index;
        case CELL_PING: return page == PAGE_PING;
        case CELL_REV: return page == PAGE_REV;
        case CELL_LATCH: return latch;
        case CELL_RESET: return true;
        default: return false;
    }
}

} // namespace seqperf
