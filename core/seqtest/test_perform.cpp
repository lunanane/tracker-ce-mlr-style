// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Host tests of the alt Performance page mapping logic (plans/004-perform-faders.md).
#include "seqModLogic.h"
#include "seqPerformLogic.h"
#include <stdio.h>
#include <string.h>

using namespace seqperf;

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                \
    do                                                          \
    {                                                           \
        g_checks++;                                             \
        if (!(c))                                               \
        {                                                       \
            g_fail++;                                           \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
        }                                                       \
    } while (0)

static void test_pad_map()
{
    for (int row = 0; row < 4; row++)
        for (int col = 0; col < 8; col++)
        {
            PadInfo p = mapPad(padOf(row, col));
            CHECK(p.cell == CELL_FADER && p.index == row && p.step == col + 1);
        }
    for (int row = 0; row < 3; row++)
    {
        PadInfo p = mapPad(padOf(row, 8));
        CHECK(p.cell == CELL_GROUP && p.index == row);
    }
    CHECK(mapPad(padOf(3, 8)).cell == CELL_NONE); // dark on purpose
    CHECK(mapPad(padOf(0, 9)).cell == CELL_PING);
    CHECK(mapPad(padOf(1, 9)).cell == CELL_REV);
    CHECK(mapPad(padOf(2, 9)).cell == CELL_NONE);
    CHECK(mapPad(padOf(3, 9)).cell == CELL_NONE);
    for (int row = 0; row < 4; row++)
    {
        PadInfo l = mapPad(padOf(row, 10)), r = mapPad(padOf(row, 11));
        CHECK(l.cell == CELL_LATCH && l.index == row);
        CHECK(r.cell == CELL_RESET && r.index == row);
    }
    CHECK(mapPad(48).cell == CELL_NONE);
    CHECK(mapPad(255).cell == CELL_NONE);
}

static void test_bar()
{
    CHECK(barMask(0) == 0x00);
    CHECK(barMask(1) == 0x01);
    CHECK(barMask(3) == 0x07);
    CHECK(barMask(7) == 0x7F);
    CHECK(barMask(8) == 0xFF);
    CHECK(barMask(200) == 0xFF);
}

static void test_pages_and_slots()
{
    CHECK(slotOf(PAGE_FX1, 0) == 0 && slotOf(PAGE_FX1, 3) == 3);
    CHECK(slotOf(PAGE_FX2, 0) == 4 && slotOf(PAGE_FX2, 3) == 7);
    CHECK(slotOf(PAGE_FX3, 0) == 8 && slotOf(PAGE_FX3, 3) == 11);
    CHECK(isFxPage(PAGE_FX3) && !isFxPage(PAGE_PING) && !isFxPage(PAGE_REV));
    CHECK(pageForCell(CELL_GROUP, 1, PAGE_PING) == PAGE_FX2);
    CHECK(pageForCell(CELL_PING, 0, PAGE_FX1) == PAGE_PING);
    CHECK(pageForCell(CELL_REV, 0, PAGE_FX1) == PAGE_REV);
    CHECK(pageForCell(CELL_FADER, 0, PAGE_REV) == PAGE_REV);
    CHECK(pageForCell(CELL_LATCH, 0, PAGE_FX3) == PAGE_FX3);
}

static void test_fx_value()
{
    CHECK(fxValue(0, -100, 100) == 0);
    CHECK(fxValue(8, -100, 100) == 100);
    CHECK(fxValue(4, -100, 100) == 0); // middle step of a signed range = neutral
    CHECK(fxValue(1, -100, 100) == -75);
    CHECK(fxValue(8, 0, 100) == 100);
    CHECK(fxValue(1, 0, 100) == 13);
    CHECK(fxValue(8, 0, 7) == 7);
    CHECK(fxValue(9, 0, 100) == 100); // clamped
    int16_t last = -32768;
    for (int s = 1; s <= 8; s++)
    {
        int16_t v = fxValue((uint8_t)s, -24, 24);
        CHECK(v >= last);
        last = v;
    }
    CHECK(fxValue(8, -24, 24) == 24);
}

static void test_send_value()
{
    CHECK(sendValue(0) == 0);
    CHECK(sendValue(8) == 100);
    CHECK(sendValue(4) == 50);
    CHECK(sendValue(1) == 13);
    CHECK(sendValue(99) == 100);
}

static void test_params()
{
    CHECK(paramValue(P_TIME, 1) == 54);
    CHECK(paramValue(P_TIME, 8) == 3500);
    CHECK(paramValue(P_RATE, 1) == 0);
    CHECK(paramValue(P_RATE, 8) == 18);
    CHECK(paramValue(P_FEEDBACK, 1) == 0);
    CHECK(paramValue(P_FEEDBACK, 8) == seqfx::kDelayFeedbackCap); // plan 007: the top step is the cap (88), it crackled at 99
    CHECK(paramValue(P_FILTER, 1) == 194);
    CHECK(paramValue(P_FILTER, 8) == 20000);
    CHECK(paramValue(P_SIZE, 1) == 0.0);
    CHECK(paramValue(P_PREDELAY, 8) == 1.0);
    CHECK(paramValue(P_DAMP, 99) == 1.0 && paramValue(P_DAMP, 0) == 0.0); // clamped
    for (int p = 0; p < P_COUNT; p++)
    {
        double last = -1;
        for (int s = 1; s <= 8; s++)
        {
            double v = paramValue((Param)p, s);
            CHECK(v >= last);
            last = v;
        }
    }
    CHECK(paramValue(P_TIME, 1) >= 1 && paramValue(P_TIME, 8) <= 3500);
    CHECK(paramValue(P_FILTER, 1) >= 100 && paramValue(P_FILTER, 8) <= 20000);
    CHECK(paramValue(P_RATE, 8) <= 18);
}

static void test_nearest()
{
    for (int p = 0; p < P_COUNT; p++)
        for (int s = 1; s <= 8; s++)
            CHECK(nearestStep((Param)p, paramValue((Param)p, s)) == s);
    CHECK(nearestStep(P_TIME, 500) == 3);     // project default 500 ms
    CHECK(nearestStep(P_FEEDBACK, 50) == 5);  // default 50 (57 is nearer than 42)
    CHECK(nearestStep(P_FILTER, 20000) == 8); // default
    // size steps run 0 .. 0.9 (plan 007 cap): 0, .129, .257, .386, .514, .643, .771, .9; the default 0.5 is step 5
    CHECK(nearestStep(P_SIZE, 0.44) == 4 && nearestStep(P_SIZE, 0.5) == 5 && nearestStep(P_SIZE, 1.0) == 8);
    CHECK(paramValue(P_SIZE, 8) == seqfx::kReverbSizeCap && paramValue(P_SIZE, 8) < 1.0);
    CHECK(nearestStep(P_TIME, 1) == 1);
    CHECK(nearestStep(P_TIME, 99999) == 8);
}

static void test_nearest_fx()
{
    for (int s = 1; s <= 8; s++)
        CHECK(nearestFxStep(fxValue((uint8_t)s, -100, 100), -100, 100) == s);
    CHECK(nearestFxStep(0, -100, 100) == 4);
    CHECK(nearestFxStep(100, 0, 100) == 8);
    CHECK(nearestFxStep(-500, -100, 100) == 1);
    CHECK(nearestFxStep(500, -100, 100) == 8);
}

static void test_pad_lit()
{
    PadInfo g1 = mapPad(padOf(1, 8)), ping = mapPad(padOf(0, 9)), rev = mapPad(padOf(1, 9));
    PadInfo lat = mapPad(padOf(2, 10)), rst = mapPad(padOf(2, 11)), dark = mapPad(padOf(3, 8));
    CHECK(padIsLit(g1, PAGE_FX2, false, true) && !padIsLit(g1, PAGE_FX1, false, true));
    CHECK(padIsLit(ping, PAGE_PING, false, true) && !padIsLit(ping, PAGE_REV, false, true));
    CHECK(padIsLit(rev, PAGE_REV, false, true) && !padIsLit(rev, PAGE_PING, false, true));
    CHECK(padIsLit(lat, PAGE_FX1, true, true) && !padIsLit(lat, PAGE_FX1, false, true));
    CHECK(padIsLit(rst, PAGE_FX1, false, true));
    CHECK(!padIsLit(dark, PAGE_FX1, true, true) && !padIsLit(dark, PAGE_PING, true, true));
}


// ---------------------------------------------------------------------------------------------------------------------
// Plan 005 R3: DISENGAGE column, engaged state, what survives leaving the page, owner mask interplay
// ---------------------------------------------------------------------------------------------------------------------
static void test_disengage_pad()
{
    for (int row = 0; row < 4; row++)
    {
        PadInfo d = mapPad(padOf(row, 11));
        CHECK(d.cell == CELL_DISENGAGE && d.index == row);
        CHECK(CELL_RESET == CELL_DISENGAGE); // the old name is the same column
    }
    PadInfo d = mapPad(padOf(0, 11));
    CHECK(padIsLit(d, PAGE_FX1, false, true) && !padIsLit(d, PAGE_FX1, false, false)); // lit = there is something to disengage
    CHECK(!padIsLit(mapPad(padOf(0, 10)), PAGE_FX1, false, true));                     // LATCH does not follow the engaged flag
}

static void test_state_engaged()
{
    State s;
    s.clear();
    CHECK(!s.anyEngaged() && s.pageMask() == 0);
    for (int p = 0; p < PAGE_COUNT; p++)
        for (int r = 0; r < 4; r++)
            CHECK(!s.engaged((uint8_t)p, (uint8_t)r) &&
                  s.status((uint8_t)p, (uint8_t)r) == (State::isParamRow((uint8_t)p, (uint8_t)r) ? RS_PROJECT : RS_OFF));
    s.fxStep[0][1] = 5;
    s.fxStep[2][3] = 8;
    s.sendStep[1] = 3;
    CHECK(s.engaged(PAGE_FX1, 1) && s.engaged(PAGE_FX3, 3) && s.engaged(PAGE_REV, 0));
    CHECK(!s.engaged(PAGE_FX1, 0) && !s.engaged(PAGE_PING, 0));
    CHECK(s.anyEngaged());
    CHECK(s.pageMask() == ((1u << PAGE_FX1) | (1u << PAGE_FX3) | (1u << PAGE_REV)));
    CHECK(s.status(PAGE_FX1, 1) == RS_LIVE);
    s.latch[PAGE_FX1][1] = true;
    CHECK(s.status(PAGE_FX1, 1) == RS_LATCHED);
    // project-value rows are dialled by a fader press, and do not count as perform FX
    State q;
    q.clear();
    CHECK(State::isParamRow(PAGE_PING, 1) && State::isParamRow(PAGE_REV, 3) && !State::isParamRow(PAGE_PING, 0) &&
          !State::isParamRow(PAGE_FX1, 2));
    q.dialParam(PAGE_PING, 2, 40.0f, 0);
    CHECK(q.engaged(PAGE_PING, 2) && q.status(PAGE_PING, 2) == RS_DIALLED && !q.anyEngaged());
    CHECK(q.pageMask() == (1u << PAGE_PING));
    q.dialParam(PAGE_FX1, 0, 1.0f, 0); // no effect on FX rows
    CHECK(!q.engaged(PAGE_FX1, 0));
}

static void test_disengage()
{
    State s;
    s.clear();
    for (int r = 0; r < 4; r++)
    {
        s.fxStep[0][r] = (uint8_t)(r + 2);
        s.latch[0][r] = (r & 1) != 0;
    }
    s.fxStep[1][0] = 7;
    s.sendStep[0] = 4;
    s.latch[PAGE_PING][0] = true;
    // one row: off, latch off, nothing remembered; the other rows and pages are untouched
    CHECK(s.disengage(PAGE_FX1, 1));
    CHECK(s.fxStep[0][1] == 0 && !s.latch[0][1] && !s.engaged(PAGE_FX1, 1) && s.status(PAGE_FX1, 1) == RS_OFF);
    CHECK(s.fxStep[0][0] == 2 && s.fxStep[0][2] == 4 && s.latch[0][3] && s.fxStep[1][0] == 7 && s.sendStep[0] == 4);
    CHECK(!s.disengage(PAGE_FX1, 1)); // already off: nothing to take back
    // a latch armed on an empty row is also taken back
    s.latch[PAGE_FX2][2] = true;
    CHECK(s.disengage(PAGE_FX2, 2) && !s.latch[PAGE_FX2][2]);
    // send row
    CHECK(s.disengage(PAGE_PING, 0) && s.sendStep[0] == 0 && !s.latch[PAGE_PING][0]);
    CHECK(s.anyEngaged()); // the FX rows of page 1 are still there
    // project-value rows: an untouched row has nothing to disengage; a dialled row gets its earlier value back, once
    CHECK(s.status(PAGE_REV, 2) == RS_PROJECT);
    CHECK(!s.disengage(PAGE_REV, 2) && s.status(PAGE_REV, 2) == RS_PROJECT && !s.engaged(PAGE_REV, 2));
    s.dialParam(PAGE_REV, 2, 0.25f, 0);
    CHECK(s.status(PAGE_REV, 2) == RS_DIALLED && s.engaged(PAGE_REV, 2) && s.pre[1][1] == 0.25f);
    s.dialParam(PAGE_REV, 2, 0.9f, 0); // a second press must not overwrite the remembered value
    CHECK(s.pre[1][1] == 0.25f && s.status(PAGE_REV, 2) == RS_DIALLED);
    CHECK(s.disengage(PAGE_REV, 2) && s.status(PAGE_REV, 2) == RS_PROJECT && s.pre[1][1] == 0.25f); // caller restores pre[]
    CHECK(!s.disengage(PAGE_REV, 2));
    s.dialParam(PAGE_REV, 2, 0.6f, 0); // dialled again after disengage: a fresh snapshot
    CHECK(s.pre[1][1] == 0.6f);
    s.disengage(PAGE_REV, 2);
    // PING fader 2 also remembers the sync rate index
    s.dialParam(PAGE_PING, 1, 500.0f, 7);
    s.dialParam(PAGE_PING, 1, 3500.0f, 9);
    CHECK(s.pre[0][0] == 500.0f && s.preRate == 7);
    s.disengage(PAGE_PING, 1);
    // the whole page (Shift + DISENGAGE)
    for (int r = 0; r < 4; r++)
        s.disengage(PAGE_FX1, (uint8_t)r);
    for (int r = 0; r < 4; r++)
        CHECK(!s.engaged(PAGE_FX1, (uint8_t)r) && !s.latch[PAGE_FX1][r]);
    CHECK(s.fxStep[1][0] == 7);                      // other page untouched
    CHECK(!s.disengage(9, 0) && !s.disengage(0, 9)); // out of range
    s.clear();
    CHECK(!s.anyEngaged() && s.pageMask() == 0 && s.status(PAGE_PING, 1) == RS_PROJECT); // Stop panic = clear
}

static void test_leave_rules()
{
    // owner decision: Pattern / Song (Play mode, clip launcher, MLR) keep dialled-in FX; nothing dialled in = stock rule
    CHECK(leaveFor(DEST_SEQ_VIEWS, true) == LEAVE_KEEP_RUNNING);
    CHECK(leaveFor(DEST_SEQ_VIEWS, false) == LEAVE_END);
    // plan 010 A4 (owner): dialled-in FX stay engaged on EVERY module switch (these two lines were LEAVE_END / LEAVE_HAND_OVER before)
    CHECK(leaveFor(DEST_OTHER, true) == LEAVE_KEEP_RUNNING && leaveFor(DEST_OTHER, false) == LEAVE_END);
    CHECK(leaveFor(DEST_STOCK_PERFORMANCE, true) == LEAVE_HAND_OVER && leaveFor(DEST_STOCK_PERFORMANCE, false) == LEAVE_HAND_OVER);
    CHECK(leaveFor(DEST_STOCK_KEEPS, true) == LEAVE_KEEP_RUNNING && leaveFor(DEST_STOCK_KEEPS, false) == LEAVE_HAND_OVER);
    for (int d = DEST_STOCK_KEEPS; d <= DEST_OTHER; d++)
        CHECK(leaveFor((Dest)d, true) == LEAVE_KEEP_RUNNING); // no destination ends engaged FX
    // what keeps sounding on leave: latched rows and stock-latched rows; a row that only plays under a held pad does not
    State s;
    s.clear();
    s.fxStep[0][0] = 5; // held pad only
    s.fxStep[0][1] = 6;
    s.latch[0][1] = true;
    s.fxStep[0][2] = 3; // stock pad row handed over
    s.sendStep[0] = 2;  // held send
    s.sendStep[1] = 4;
    s.latch[PAGE_REV][0] = true;
    CHECK(!s.keepsOnLeave(PAGE_FX1, 0, false));
    CHECK(s.keepsOnLeave(PAGE_FX1, 1, false));
    CHECK(s.keepsOnLeave(PAGE_FX1, 2, true) && !s.keepsOnLeave(PAGE_FX1, 2, false));
    CHECK(!s.keepsOnLeave(PAGE_FX1, 3, true)); // not engaged: nothing to keep
    CHECK(!s.keepsOnLeave(PAGE_PING, 0, false) && s.keepsOnLeave(PAGE_REV, 0, false));
    // persistence across mode switches: after dropping what does not keep, the page can pick up what was left
    for (int p = 0; p < 3; p++)
        for (int r = 0; r < 4; r++)
            if (s.fxStep[p][r] && !s.keepsOnLeave((uint8_t)p, (uint8_t)r, p == 0 && r == 2))
                s.fxStep[p][r] = 0;
    for (int i = 0; i < 2; i++)
        if (s.sendStep[i] && !s.keepsOnLeave((uint8_t)(PAGE_PING + i), 0, false))
            s.sendStep[i] = 0;
    CHECK(s.fxStep[0][0] == 0 && s.fxStep[0][1] == 6 && s.fxStep[0][2] == 3 && s.sendStep[0] == 0 && s.sendStep[1] == 4);
    CHECK(s.anyEngaged() && s.latch[0][1] && s.latch[PAGE_REV][0]);
    CHECK(canResume(true, true, s.anyEngaged()));
    CHECK(!canResume(false, true, true)); // never left running
    CHECK(!canResume(true, false, true)); // performance mode was switched off meanwhile
    CHECK(!canResume(true, true, false)); // everything was disengaged
}

static void test_owner_interplay()
{
    using namespace seqlogic;
    // fader FX alone: letting go switches performance mode off (the caller reloads the pattern)
    PerfOwners o;
    o.claim(PERF_OWNER_FX);
    CHECK(o.release(PERF_OWNER_FX) && !o.any());
    // clips launched, then the fader page: the clips own the flag as well, the FX end does NOT switch it off
    PerfOwners both;
    both.claim(PERF_OWNER_LAUNCHER);
    CHECK(both.claim(PERF_OWNER_FX) == false); // not the first claim: the flag was already on
    CHECK(!both.release(PERF_OWNER_FX) && both.has(PERF_OWNER_LAUNCHER) && both.any());
    CHECK(both.release(PERF_OWNER_LAUNCHER)); // the launcher letting go last switches it off
    // FX left running into the launcher: the FX bit stays set while the launcher claims too; Stop panic drops only the FX bit
    PerfOwners kept;
    kept.claim(PERF_OWNER_FX);
    kept.claim(PERF_OWNER_LAUNCHER);
    CHECK(kept.has(PERF_OWNER_FX) && kept.othersThan(PERF_OWNER_FX));
    CHECK(!kept.release(PERF_OWNER_FX) && kept.has(PERF_OWNER_LAUNCHER)); // clips keep playing after the panic
    // a resume needs the FX bit; without it (turned off meanwhile) nothing resumes
    PerfOwners gone;
    gone.claim(PERF_OWNER_LAUNCHER);
    CHECK(!canResume(true, gone.has(PERF_OWNER_FX), true));
}

static void test_format_tracks()
{
    char b[40];
    formatTracks(b, sizeof(b), 0);
    CHECK(strcmp(b, "-") == 0);
    formatTracks(b, sizeof(b), 0x0001);
    CHECK(strcmp(b, "1") == 0);
    formatTracks(b, sizeof(b), 0x0003);
    CHECK(strcmp(b, "1 2") == 0);
    formatTracks(b, sizeof(b), 0x000F);
    CHECK(strcmp(b, "1-4") == 0);
    formatTracks(b, sizeof(b), 0x004F);
    CHECK(strcmp(b, "1-4 7") == 0);
    formatTracks(b, sizeof(b), 0x8000);
    CHECK(strcmp(b, "16") == 0);
    formatTracks(b, sizeof(b), 0xFFFF);
    CHECK(strcmp(b, "1-16") == 0);
    formatTracks(b, sizeof(b), 0x0A05);
    CHECK(strcmp(b, "1 3 10 12") == 0);
    char small[4];
    int n = formatTracks(small, sizeof(small), 0xFFFF); // never writes past the buffer
    CHECK(n <= 3 && small[n] == 0);
}

static void test_drop_on_leave_resume_panic_load()
{
    State s;
    s.clear();
    s.fxStep[0][0] = 5;                                  // held pad only
    s.fxStep[0][1] = 6; s.latch[0][1] = true;            // latched
    s.fxStep[1][3] = 2;                                  // stock row
    s.sendStep[0] = 3;                                   // held send
    s.sendStep[1] = 4; s.latch[PAGE_REV][0] = true;      // latched send
    uint16_t stock = (uint16_t)(1u << 7);
    uint16_t drop = s.dropOnLeave(stock);
    CHECK(drop == ((1u << 0) | (1u << 12)));             // slot 1 and the PING send end; the rest keeps sounding
    s.clear();
    CHECK(s.dropOnLeave(0xFFFF) == 0);
    // resume: kept state comes back only with the FX owner and something engaged
    State k;
    k.clear();
    k.fxStep[0][1] = 6; k.latch[0][1] = true;
    CHECK(canResume(true, true, k.anyEngaged()));
    // Stop panic: everything cleared, nothing to resume afterwards (project values stay: they are not in State)
    k.dialParam(PAGE_PING, 2, 50.0f, 0);
    k.clear();
    CHECK(!k.anyEngaged() && k.pageMask() == 0 && !k.latch[0][1] && k.status(PAGE_PING, 2) == RS_PROJECT);
    CHECK(!canResume(true, true, k.anyEngaged()));
    // project load: same clear, and the snapshots are gone too
    k.dialParam(PAGE_REV, 3, 0.4f, 0);
    k.clear();
    CHECK(k.pre[1][2] == 0 && k.preRate == 0 && !k.engaged(PAGE_REV, 3));
}

int main()
{
    test_pad_map();
    test_bar();
    test_pages_and_slots();
    test_fx_value();
    test_send_value();
    test_params();
    test_nearest();
    test_nearest_fx();
    test_pad_lit();
    test_disengage_pad();
    test_state_engaged();
    test_disengage();
    test_leave_rules();
    test_owner_interplay();
    test_format_tracks();
    test_drop_on_leave_resume_panic_load();
    printf("seqperf: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
