// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// The screen model of the Master fader pages (plan 024): a plain snapshot of the values -> the strFaderView the Perform fader screen control
// draws (seqPerform/faderView.cpp, master = 1: four page chips REVERB / DELAY / HIGH-PASS / BEAT RPT). Header only, no engine access, so the
// device code (masterFaders.cpp) and the host preview / budget test (core/seqtest/test_masterfaders.cpp) run the same code.
#pragma once
#include <stdio.h>
#include <string.h>

#include "seqMasterFaderLogic.h"
#include "seqPerform/faderView.h"

namespace seqmf {

struct Snap
{
    uint8_t page = MP_REVERB;
    uint8_t step[4] = {};         // lit steps (REVERB / DELAY / HIGH-PASS)
    uint8_t dialled[4] = {};      // the fader moved the row; DISENGAGE has something to restore
    char name[4][22] = {};        // REVERB / DELAY rows (filled from the project values by the caller)
    char value[4][16] = {};
    uint8_t pageDot = 0;          // bit per page: something dialled in
    uint8_t pingpong = 0, sync = 0;
    uint8_t hpCut = 0, hpRes = 1; // HIGH-PASS steps (0 = off)
    // BEAT RPT
    uint8_t brSub = 0, brMode = 0, brLatch = 0, brState = 0, brEventGrid = 0, brHeld = 0, repeatIdx = 0, cpu10 = 0, preroll = 0;
    uint8_t brStep[seqbr::BP_COUNT] = {};
}; // only bytes: no padding, so two snapshots compare with memcmp

inline void hpFreqText(char* out, size_t cap, uint8_t step)
{
    const int hz = (int)(seqfx::hpHz(seqbr::hpIdxOf(step)) + 0.5f);
    if (hz >= 1000)
        snprintf(out, cap, "%d.%d kHz", hz / 1000, (hz % 1000) / 100);
    else
        snprintf(out, cap, "%d Hz", hz);
}

inline void fillView(strFaderView& vw, const Snap& s)
{
    using namespace seqperf;
    memset(&vw, 0, sizeof(vw));
    vw.master = 1;
    vw.page = s.page;
    vw.pageMask = s.pageDot;
    snprintf(vw.tracks, sizeof(vw.tracks), "page: pad col 9");
    static const char* const kState[5] = { "DISENGAGED", "ENGAGED", "LATCHED", "PROJECT", "DIALLED" };
    for (uint8_t r = 0; r < 4; r++)
    {
        strFaderBand& b = vw.band[r];
        b.usable = 1;
        b.canLatch = 0;
        if (s.page == MP_REVERB || s.page == MP_DELAY)
        {
            b.step = s.step[r];
            b.engaged = s.dialled[r];
            b.status = s.dialled[r] ? RS_DIALLED : RS_PROJECT;
            snprintf(b.name, sizeof(b.name), "%s", s.name[r]);
            snprintf(b.value, sizeof(b.value), "%s", s.value[r]);
            if (s.dialled[r]) // a stored value needs no note: the state word says PROJECT
                snprintf(b.note, sizeof(b.note), "DISENGAGE restores the earlier value");
            snprintf(b.state, sizeof(b.state), "%s", kState[b.status]);
        }
        else if (s.page == MP_HP)
        {
            if (r > 1)
            {
                b.usable = 0;
                b.status = RS_OFF;
                snprintf(b.name, sizeof(b.name), "(not used)");
                snprintf(b.state, sizeof(b.state), "-");
                continue;
            }
            b.status = RS_PROJECT;
            if (r == 0)
            {
                b.step = s.hpCut;
                b.engaged = s.hpCut != 0;
                b.status = s.hpCut ? RS_LIVE : RS_OFF;
                snprintf(b.name, sizeof(b.name), "High-pass cutoff");
                if (s.hpCut)
                    hpFreqText(b.value, sizeof(b.value), s.hpCut);
                else
                    snprintf(b.value, sizeof(b.value), "off");
                snprintf(b.note, sizeof(b.note), "pad 1 = 60 Hz ... pad 8 = 5.1 kHz");
                snprintf(b.state, sizeof(b.state), "%s", s.hpCut ? "ACTIVE" : "OFF");
            }
            else
            {
                b.step = s.hpRes;
                b.engaged = s.hpRes != 1;
                snprintf(b.name, sizeof(b.name), "Resonance");
                snprintf(b.value, sizeof(b.value), "%d %%", seqbr::percentOf(s.hpRes));
                snprintf(b.note, sizeof(b.note), "12 dB per octave");
                snprintf(b.state, sizeof(b.state), "%s", s.hpRes != 1 ? "DIALLED" : "PROJECT");
                b.status = s.hpRes != 1 ? RS_DIALLED : RS_PROJECT;
            }
        }
        else // BEAT RPT
        {
            using namespace seqbr;
            const seqbr::Param par = paramOf(s.brSub, r);
            const Kind kind = kindOf(par);
            if (kind == BK_NONE)
            {
                b.usable = 0;
                b.status = RS_OFF;
                snprintf(b.name, sizeof(b.name), "(not used)");
                snprintf(b.state, sizeof(b.state), "-");
                continue;
            }
            snprintf(b.name, sizeof(b.name), "%s", paramName(par));
            snprintf(b.note, sizeof(b.note), "%s", paramHint(par));
            const bool active = s.brState != beatrepeat::S_IDLE;
            if (kind == BK_MOMENTARY)
            {
                b.step = active ? (uint8_t)(s.brEventGrid + 1) : (uint8_t)0;
                b.canLatch = 1;
                b.latched = s.brLatch;
                b.engaged = active;
                b.status = active ? RS_LIVE : (s.brLatch ? RS_LATCHED : RS_OFF);
                snprintf(b.value, sizeof(b.value), "%s", active ? beatrepeat::gridName(s.brEventGrid) : "hold");
                snprintf(b.lat, sizeof(b.lat), "LATCH");
                snprintf(b.dis, sizeof(b.dis), "OFF");
                snprintf(b.state, sizeof(b.state), "%s", s.brState == beatrepeat::S_PENDING ? "WAITING" : (s.brState == beatrepeat::S_CAPTURE ? "CAPTURING" : (active ? "REPEAT" : "IDLE")));
            }
            else
            {
                b.step = s.brStep[par];
                b.engaged = s.brStep[par] != BrParams::defaultStep(par);
                b.status = RS_PROJECT;
                valueText(par, s.brStep[par], b.value, sizeof(b.value));
                snprintf(b.dis, sizeof(b.dis), "RESET");
                snprintf(b.state, sizeof(b.state), "%s", kState[RS_PROJECT]);
            }
        }
    }
    // the summary line
    if (s.page == MP_REVERB)
        snprintf(vw.summary, sizeof(vw.summary), "REVERB   DISENGAGE (pad 12): Shift = all four");
    else if (s.page == MP_DELAY)
        snprintf(vw.summary, sizeof(vw.summary), "DELAY   ping-pong %s (pad 10, row 3)   sync %s (row 4)   Shift+DISENGAGE = all four",
                 s.pingpong ? "ON" : "off", s.sync ? "ON" : "off");
    else if (s.page == MP_HP)
        snprintf(vw.summary, sizeof(vw.summary), "HIGH-PASS   DISENGAGE (pad 12, row 1) = off");
    else
    {
        char st[24];
        const bool active = s.brState != beatrepeat::S_IDLE;
        if (active)
            snprintf(st, sizeof(st), "REPEAT %s #%d", beatrepeat::gridName(s.brEventGrid), s.repeatIdx);
        else
            snprintf(st, sizeof(st), s.preroll ? "recording ahead" : "idle");
        snprintf(vw.summary, sizeof(vw.summary), "BEAT RPT %s  [screen button 5: next page]  mode %s  %s  CPU %d.%d %%", seqbr::pageName(s.brSub),
                 seqbr::modeName(s.brMode), st, s.cpu10 / 10, s.cpu10 % 10);
    }
}

} // namespace seqmf
