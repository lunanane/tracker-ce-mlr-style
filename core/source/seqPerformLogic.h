// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Pure logic of the alt Performance page (plans/004-perform-faders.md, plan 005 R3): pad -> cell, fader step ->
// value per parameter, LED bars, the engaged / disengaged state of the four fader rows, what survives leaving the page.
// No Polyend includes, host-testable (core/seqtest/test_perform.cpp).
#pragma once
#include <stdint.h>
#include "seqFxCaps.h"

namespace seqperf {

// Column 12 is DISENGAGE (plan 005 R3, owner decision): that row's effect goes back to "not dialled in". CELL_RESET is the
// v0.1 name of the same pad column (alias, so the old checks still read the same).
enum Cell : uint8_t { CELL_NONE = 0, CELL_FADER, CELL_GROUP, CELL_PING, CELL_REV, CELL_LATCH, CELL_DISENGAGE, CELL_RESET = CELL_DISENGAGE };

// Pages (what the four faders control). FX pages use the stock perform slots 4*page .. 4*page+3.
enum Page : uint8_t { PAGE_FX1 = 0, PAGE_FX2 = 1, PAGE_FX3 = 2, PAGE_PING = 3, PAGE_REV = 4, PAGE_COUNT = 5 };

struct PadInfo
{
    Cell cell;
    uint8_t index; // FADER: row 0..3, GROUP: 0..2 (= page), LATCH / DISENGAGE: fader row
    uint8_t step;  // FADER: 1..8 (column + 1)
};

// Pad number 0..47 = row * 12 + column. Column 9 (index 8): groups in rows 1-3, row 4 dark on purpose.
// Column 10: PING (row 1), REV (row 2). Column 11: LATCH. Column 12: DISENGAGE.
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
        p.cell = CELL_DISENGAGE;
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

// Which step (1..8) shows an FX value that was set outside the faders (stock page pad row): nearest step.
inline uint8_t nearestFxStep(int16_t value, int16_t mn, int16_t mx)
{
    uint8_t best = 1;
    int32_t bd = 0x7FFFFFFF;
    for (uint8_t s = 1; s <= 8; s++)
    {
        int32_t d = (int32_t)fxValue(s, mn, mx) - value;
        if (d < 0)
            d = -d;
        if (d < bd)
        {
            bd = d;
            best = s;
        }
    }
    return best;
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
        case P_FEEDBACK: return (double)((seqfx::kDelayFeedbackCap * (step - 1) + 3) / 7); // 0 .. the cap (88): no crackle at the top
        case P_SIZE: return seqfx::kReverbSizeCap * (double)(step - 1) / 7.0;               // 0 .. the cap (0.9)
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

// Is the pad of a non-fader cell lit? engaged: the row of a DISENGAGE pad has something to disengage (lit = it will act).
inline bool padIsLit(const PadInfo& p, uint8_t page, bool latch, bool engaged)
{
    switch (p.cell)
    {
        case CELL_GROUP: return page == p.index;
        case CELL_PING: return page == PAGE_PING;
        case CELL_REV: return page == PAGE_REV;
        case CELL_LATCH: return latch;
        case CELL_DISENGAGE: return engaged;
        default: return false;
    }
}

// ---- engaged state of the four fader rows (plan 005 R3) ----
// A row is ENGAGED when its effect is dialled in: FX page: step != 0; send fader (PING / REV row 1): step != 0; PING / REV rows
// 2-4 are project values and count as dialled once a fader pad was pressed. DISENGAGE (pad column 12) takes the row back to
// "not dialled in". An FX row remembers nothing; a project-value row restores the value it had before the fader touched it. The
// state lives in a long-lived object so it survives leaving the page.
enum ParamState : uint8_t { PS_PROJECT = 0, PS_DIALLED = 1 };
enum RowStatus : uint8_t { RS_OFF = 0, RS_LIVE, RS_LATCHED, RS_PROJECT, RS_DIALLED };

struct State
{
    uint8_t fxStep[3][4];       // FX pages: current step per fader (0 = off)
    uint8_t sendStep[2];        // PING / REV fader 1 (send of the selected tracks)
    bool latch[PAGE_COUNT][4];
    uint8_t paramState[2][3];   // PING / REV faders 2-4: ParamState
    float pre[2][3];            // value of a dialled project-value row before the first fader touch (restored by DISENGAGE)
    uint8_t preRate;            // PING fader 2 only: the sync rate index (delayParams bits 0-5) before the first touch

    void clear()
    {
        for (int p = 0; p < 3; p++)
            for (int r = 0; r < 4; r++)
                fxStep[p][r] = 0;
        for (int i = 0; i < 2; i++)
        {
            sendStep[i] = 0;
            for (int r = 0; r < 3; r++)
            {
                paramState[i][r] = PS_PROJECT;
                pre[i][r] = 0;
            }
        }
        preRate = 0;
        for (int p = 0; p < PAGE_COUNT; p++)
            for (int r = 0; r < 4; r++)
                latch[p][r] = false;
    }
    static bool isParamRow(uint8_t page, uint8_t row) { return page > PAGE_FX3 && row > 0; }
    bool engaged(uint8_t page, uint8_t row) const
    {
        if (page >= PAGE_COUNT || row >= 4)
            return false;
        if (page <= PAGE_FX3)
            return fxStep[page][row] != 0;
        if (row == 0)
            return sendStep[page - PAGE_PING] != 0;
        return paramState[page - PAGE_PING][row - 1] == PS_DIALLED;
    }
    // Effects that are sounding because of the faders (project values do not count: they are not "perform FX").
    bool anyEngaged() const
    {
        for (int p = 0; p < 3; p++)
            for (int r = 0; r < 4; r++)
                if (fxStep[p][r])
                    return true;
        return sendStep[0] || sendStep[1];
    }
    // Bit per page: something is dialled in on it (for the page chips of the screen).
    uint8_t pageMask() const
    {
        uint8_t m = 0;
        for (uint8_t p = 0; p < PAGE_COUNT; p++)
            for (uint8_t r = 0; r < 4; r++)
                if (engaged(p, r))
                    m = (uint8_t)(m | (1u << p));
        return m;
    }
    // DISENGAGE one row: off, latch off, nothing remembered (a project-value row returns to its default; the caller applies it).
    // true: the row had something to take back.
    bool disengage(uint8_t page, uint8_t row)
    {
        if (page >= PAGE_COUNT || row >= 4)
            return false;
        bool had = engaged(page, row) || latch[page][row];
        latch[page][row] = false;
        if (page <= PAGE_FX3)
            fxStep[page][row] = 0;
        else if (row == 0)
            sendStep[page - PAGE_PING] = 0;
        else
        {
            // a project-value row only has something to take back while it is dialled; the caller restores pre[] / preRate
            uint8_t& ps = paramState[page - PAGE_PING][row - 1];
            had = ps == PS_DIALLED;
            ps = PS_PROJECT;
        }
        return had;
    }
    // A fader pad was pressed on a project-value row. The value before the FIRST touch is remembered (value / rate index); a
    // second press while dialled does not overwrite it. Call this BEFORE the new value is applied.
    void dialParam(uint8_t page, uint8_t row, float value, uint8_t rate)
    {
        if (!isParamRow(page, row))
            return;
        uint8_t& ps = paramState[page - PAGE_PING][row - 1];
        if (ps != PS_DIALLED)
        {
            pre[page - PAGE_PING][row - 1] = value;
            if (page == PAGE_PING && row == 1)
                preRate = rate;
        }
        ps = PS_DIALLED;
    }
    // Rows that do NOT keep sounding when the page is left for the mod's own views (a held pad only): bit = FX slot 0..11, bit
    // 12 / 13 = PING / REV send. stockMask: slots the stock page had latched.
    uint16_t dropOnLeave(uint16_t stockMask) const
    {
        uint16_t m = 0;
        for (uint8_t p = 0; p < 3; p++)
            for (uint8_t r = 0; r < 4; r++)
                if (fxStep[p][r] && !keepsOnLeave(p, r, (stockMask & (1u << (p * 4 + r))) != 0))
                    m = (uint16_t)(m | (1u << (p * 4 + r)));
        for (uint8_t i = 0; i < 2; i++)
            if (sendStep[i] && !keepsOnLeave((uint8_t)(PAGE_PING + i), 0, false))
                m = (uint16_t)(m | (1u << (12 + i)));
        return m;
    }
    // Leaving for the mod's own views: does this row keep sounding? Latched rows do, and so do rows the stock page had latched
    // (stockRow). A row that only plays because a pad is held does not.
    bool keepsOnLeave(uint8_t page, uint8_t row, bool stockRow) const
    {
        return engaged(page, row) && (latch[page][row] || stockRow);
    }
    RowStatus status(uint8_t page, uint8_t row) const
    {
        if (isParamRow(page, row))
        {
            uint8_t s = paramState[page - PAGE_PING][row - 1];
            return s == PS_DIALLED ? RS_DIALLED : RS_PROJECT;
        }
        if (!engaged(page, row))
            return RS_OFF;
        return latch[page][row] ? RS_LATCHED : RS_LIVE;
    }
};

// ---- what leaving the fader page does (plan 005 owner decision + M5 table) ----
// Destinations by the button that was pressed.
enum Dest : uint8_t
{
    DEST_STOCK_PERFORMANCE = 0, // Performance: back to the stock page (it takes over its own state)
    DEST_STOCK_KEEPS,           // Master / Params: stock keeps performance mode running through them
    DEST_SEQ_VIEWS,             // Pattern (Play mode) / Song (clip launcher, MLR): the mod's own views
    DEST_OTHER                  // everything else
};
enum Leave : uint8_t
{
    LEAVE_END = 0,     // stock rule: the fader FX end (at the pattern end while playing)
    LEAVE_HAND_OVER,   // the target keeps the performance mode; the fader FX end, the stock page's own state stays
    LEAVE_KEEP_RUNNING // dialled-in FX keep sounding; only held pads end; disengage / unlatch / Stop end them later
};
inline Leave leaveFor(Dest d, bool anyEngaged)
{
    switch (d)
    {
        case DEST_STOCK_PERFORMANCE:
        case DEST_STOCK_KEEPS: return LEAVE_HAND_OVER;
        case DEST_SEQ_VIEWS: return anyEngaged ? LEAVE_KEEP_RUNNING : LEAVE_END;
        default: return LEAVE_END;
    }
}

// Can the page start from what was left running? Needs the FX owner bit still set (nothing turned performance mode off since)
// and the same slot values still sounding.
inline bool canResume(bool kept, bool fxOwnerHeld, bool anyEngaged) { return kept && fxOwnerHeld && anyEngaged; }

// "1-4 7": the tracks of a bit mask (bit t = track t+1), runs of three or more as a range. Writes "-" for none. Returns the length.
inline int formatTracks(char* out, int cap, uint16_t mask)
{
    int n = 0;
    auto put = [&](char c) { if (n < cap - 1) out[n++] = c; };
    auto num = [&](int v) { if (v >= 10) put((char)('0' + v / 10)); put((char)('0' + v % 10)); };
    if (cap < 2)
        return 0;
    int t = 0;
    bool first = true;
    while (t < 16)
    {
        if (!(mask & (1u << t))) { t++; continue; }
        int e = t;
        while (e + 1 < 16 && (mask & (1u << (e + 1))))
            e++;
        if (!first)
            put(' ');
        first = false;
        if (e - t >= 2)
        {
            num(t + 1);
            put('-');
            num(e + 1);
        }
        else
        {
            num(t + 1);
            if (e > t)
            {
                put(' ');
                num(e + 1);
            }
        }
        t = e + 1;
    }
    if (first)
        put('-');
    out[n] = 0;
    return n;
}

} // namespace seqperf
