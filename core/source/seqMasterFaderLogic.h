// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Pure logic of the Master fader pages (plan 024): the pad map, the step -> value tables of the new rows, the LEDs. No Polyend includes,
// host-testable (core/seqtest/test_masterfaders.cpp). The delay / reverb rows reuse the Perform fader page's value tables (seqPerformLogic.h),
// the beat repeat page is the controller of the CUT page (seqBeatRepeatLogic.h).
//
//          1  2  3  4  5  6  7  8 |  9       10       11     12
//   r1    fader 1 (8 steps)       |  REVERB   -        -      DISENGAGE 1
//   r2    fader 2                 |  DELAY    -        -      DISENGAGE 2
//   r3    fader 3                 |  HIGH-PASS PING-PONG -    DISENGAGE 3     (pads 10: only on the DELAY page)
//   r4    fader 4                 |  BEAT RPT SYNC     -      DISENGAGE 4
//
//   REVERB: return level, size, damp, predelay      DELAY: return level, time / rate, feedback, band-pass centre (as the Perform PING / REV pages)
//   HIGH-PASS: cutoff (pad 1 = lowest, DISENGAGE = off), resonance
//   BEAT RPT: the beat repeat pads of the CUT page (rows GRID/GATE/DECAY/PITCH, AUTO, FILTER pages by screen button 5), columns 10-12 as there
#pragma once
#include <stdint.h>

#include "seqBeatRepeatLogic.h"
#include "seqPerformLogic.h"

namespace seqmf {

enum Page : uint8_t { MP_REVERB = 0, MP_DELAY = 1, MP_HP = 2, MP_BR = 3, MP_COUNT = 4 };
enum Cell : uint8_t { MC_NONE = 0, MC_FADER, MC_PAGE, MC_RESET, MC_PINGPONG, MC_SYNC, MC_BR };

struct PadInfo
{
    Cell cell;
    uint8_t index; // FADER / RESET: row, PAGE: page
    uint8_t step;  // FADER: 1..8
};

// Pad number 0..47 = row * 12 + column. Column 9 is the page menu on every page.
inline PadInfo mapPad(uint8_t pad, uint8_t page)
{
    PadInfo p = { MC_NONE, 0, 0 };
    if (pad >= 48)
        return p;
    const uint8_t col = (uint8_t)(pad % 12), row = (uint8_t)(pad / 12);
    if (col == 8)
    {
        p.cell = MC_PAGE;
        p.index = row;
        return p;
    }
    if (page == MP_BR)
    {
        p.cell = MC_BR; // the beat repeat controller maps columns 1-8 and 10-12 itself
        p.index = row;
        return p;
    }
    if (col < 8)
    {
        p.cell = MC_FADER;
        p.index = row;
        p.step = (uint8_t)(col + 1);
    }
    else if (col == 9 && page == MP_DELAY && row == 2)
        p.cell = MC_PINGPONG;
    else if (col == 9 && page == MP_DELAY && row == 3)
        p.cell = MC_SYNC;
    else if (col == 11)
    {
        p.cell = MC_RESET;
        p.index = row;
    }
    return p;
}

// The seqperf page whose value tables / engine setters a delay or reverb row uses
inline uint8_t perfPage(uint8_t page) { return page == MP_DELAY ? (uint8_t)seqperf::PAGE_PING : (uint8_t)seqperf::PAGE_REV; }
inline bool isFxPage(uint8_t page) { return page == MP_REVERB || page == MP_DELAY; }

// Return level fader (row 1 of REVERB / DELAY): step 1 = 0 %, step 8 = 100 %
inline uint8_t levelOf(uint8_t step) { return (uint8_t)(((step < 1 ? 0 : (step > 8 ? 7 : step - 1)) * 100 + 3) / 7); }
inline uint8_t levelStep(uint8_t level)
{
    const int s = (level * 7 + 50) / 100 + 1;
    return (uint8_t)(s > 8 ? 8 : s);
}

// Master high-pass: cutoff pad 1 = the lowest corner (60 Hz), pad 8 = 5.1 kHz; the project step 0 = off. Resonance: 0 .. 100 % in 8 steps.
inline uint8_t hpCutStep(const seqbr::BrParams& b) { return b.hpCut > 8 ? (uint8_t)0 : b.hpCut; }

// Pad LEDs (pwm 0 .. 30): the same look as the Perform fader page.
constexpr uint8_t kBar = 24, kBright = 30, kMid = 14, kDimPad = 6, kFaint = 4, kPing = 10;

struct LedIn
{
    uint8_t page;
    uint8_t step[4];     // the lit steps of the four faders (FX pages, HP)
    uint8_t engaged[4];  // column 12: bright when the row has something to take back
    uint8_t pageDot;     // bit per page: something is dialled in
    uint8_t pingpong, sync;
    uint8_t br[48];      // the beat repeat page: seqbr::fillLeds values (0 .. 15)
};

inline void fillLeds(const LedIn& in, uint8_t out[48])
{
    for (int i = 0; i < 48; i++)
        out[i] = 0;
    if (in.page == MP_BR)
    {
        for (int i = 0; i < 48; i++)
            out[i] = (uint8_t)((in.br[i] > 15 ? 15 : in.br[i]) * 2);
    }
    else
    {
        for (uint8_t row = 0; row < 4; row++)
        {
            const uint8_t m = seqperf::barMask(in.step[row]);
            const bool usable = in.page != MP_HP || row < 2;
            for (uint8_t c = 0; c < 8; c++)
                out[row * 12 + c] = (usable && (m & (1u << c))) ? kBar : 0;
            out[row * 12 + 11] = usable ? (in.engaged[row] ? kBright : kFaint) : 0;
        }
        if (in.page == MP_DELAY)
        {
            out[2 * 12 + 9] = in.pingpong ? kPing : 0;
            out[3 * 12 + 9] = in.sync ? kPing : 0;
        }
    }
    for (uint8_t row = 0; row < MP_COUNT; row++) // the page menu: the page you are on is bright, a page with something dialled in is medium
        out[row * 12 + 8] = in.page == row ? kBright : ((in.pageDot & (1u << row)) ? kMid : kDimPad);
}

// Screen buttons while the faders are shown: 0-3 = the pages, 4 = BEAT RPT sub page (REPEAT / AUTO / FILTER), 6 = next page, 7 = close
enum Button : uint8_t { BT_REVERB = 0, BT_DELAY, BT_HP, BT_BR, BT_SUB, BT_UNUSED, BT_NEXT, BT_CLOSE };
inline uint8_t nextPage(uint8_t page) { return (uint8_t)((page + 1) % MP_COUNT); }
inline uint8_t nextSub(uint8_t sub) { return (uint8_t)((sub + 1) % seqbr::BG_COUNT); }

} // namespace seqmf
