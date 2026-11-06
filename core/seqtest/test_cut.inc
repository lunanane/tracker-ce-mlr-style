// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// CUT (MLR) layer tests. Included from test_main.cpp (uses CHECK, mk, playUSteps, seqEngine).
#include "seqCut.h"
#include "cutLayer.h"
#include "cutSessionLayer.h"
#include "laneDraw.h"
#include "seqBeatRepeat.h"
#include <stdlib.h>

// Stand-ins for the firmware-only parts of the CUT screen (laneView.cpp: display controls, Shift button, Session host).
static bool g_shiftHeld = false;
static int g_groupRequests = 0;
static int g_lastGroupRequest = -1;
static int g_laneShow = 0, g_laneHide = 0;
static int g_laneCreate = 0, g_laneForget = 0;
void laneViewCreate() { g_laneCreate++; }
void laneViewForget() { g_laneForget++; }
void laneViewShow(bool show) { if (show) g_laneShow++; else g_laneHide++; }
bool laneViewRefresh(bool) { return false; }
bool cutShiftHeld() { return g_shiftHeld; }
bool laneViewAnyOffGrid() { return seqEngine.cut() != 0 && seqEngine.cut()->activeMask() != 0; }

// Stand-ins of the beat repeat glue (seqBeatRepeat.cpp): the controller state is the real seqbr::Ui, the commands to the audio side are recorded.
static seqbr::Ui g_brUi;
static std::vector<seqbr::Result> g_brActs;
static int g_brPanics = 0;
static beatrepeat::Status g_brStatus;
static bool g_brHaveStatus = false;
seqbr::Ui& seqBrUi() { return g_brUi; }
void seqBrAct(const seqbr::Result& r) { g_brActs.push_back(r); }
void seqBrPanic() { g_brUi.panic(); g_brPanics++; }
bool seqBrStatus(beatrepeat::Status* st) { *st = g_brStatus; return g_brHaveStatus; }


// ---- legacy-row shim (plan 016 A) -------------------------------------------------------------------------------------------------
// The tests of plans 002-008 were written for "pad row r of the group = track g + r, 8 pads share the 16 steps of the pattern". With the
// MLR slots a test row r is the track g + r: it is put into slot r & 1 and its grid is set to 1/8 (2 steps per pad), where 16 pattern
// steps give exactly the old 8 positions - so those tests keep checking the same behaviour through the new layout.
static int g_tg = 0;
static void tSetGroup(int first)
{
    cutInit();
    g_tg = first & ~3;
}
static uint8_t tPrep(uint8_t row, uint8_t col)
{
    const uint8_t track = (uint8_t)(g_tg + row);
    cutInit();
    cutAssignSlot((uint8_t)(row & 1), track);
    seqcut::Cut* c = seqEngine.cut();
    if (c && col < seqcut::kPosCols && !(c->view(track).flags2 & seqcut::V2_GRID8))
    {
        seqEngine.postCut(track, seqcut::packGrid(seqcut::GRID_8));
        seqEngine.idleTick();
    }
    return (uint8_t)((row & 1) * 2);
}
static void tCutPad(uint8_t col, uint8_t row, bool down, bool shift = false)
{
    if (col >= seqcut::kCols || row >= seqcut::kRows)
    {
        cutPad(col, row, down, shift);
        return;
    }
    const uint8_t pr = down ? tPrep(row, col) : (uint8_t)((row & 1) * 2);
    cutPad(col, pr, down, shift);
}
static void tAPad(seqsession::CutSessionLayer& a, uint8_t col, uint8_t row, bool down)
{
    const uint8_t pr = down ? tPrep(row, col) : (uint8_t)((row & 1) * 2);
    a.pad(col, pr, down);
}

namespace
{
// A fake pattern for track 0: map of notes (1 = note, 2 = note-off), 16 steps by default.
struct FakePattern
{
    uint8_t map[256];
    int steps;
    int pos; // actual_pos of the track
    int stops;
    bool sample;
    FakePattern() : steps(16), pos(0), stops(0), sample(true) { memset(map, 0, sizeof(map)); }
};
void fpNoteMap(void* u, uint8_t, uint8_t steps, uint8_t* out)
{
    FakePattern* f = (FakePattern*)u;
    for (int i = 0; i < steps; i++)
        out[i] = f->map[i];
}
int fpPos(void* u, uint8_t) { return ((FakePattern*)u)->pos; }
int fpSteps(void* u, uint8_t) { return ((FakePattern*)u)->steps; }
void fpStop(void* u, uint8_t) { ((FakePattern*)u)->stops++; }
bool fpSample(void* u, uint8_t) { return ((FakePattern*)u)->sample; }

void install(seqcut::Cut& c, FakePattern& f)
{
    seqcut::Hooks h = { &f, fpNoteMap, fpPos, fpSteps, fpStop, fpSample };
    c.setHooks(h);
}

// One step boundary of track 0 in the firmware's order: tick_uStep runs inc_uStepGlobal() BEFORE the track loop, so at the
// boundary globalPos is already the step that plays next; the stock advance comes first, then the cut override.
// `global` = globalPos (the step playing now before the call, the step that plays next after it).
seqcut::StepPlan stepOnce(seqcut::Cut& c, FakePattern& f, int& global)
{
    f.pos = (f.pos + 1) % f.steps;
    global = (global + 1) % f.steps;
    c.setStock(0, global); // the stock sequencer runs on the global clock (same length): its position is the global step
    seqcut::StepPlan p = c.boundary(0, f.steps, global);
    if (p.override)
        f.pos = p.pos;
    return p;
}
} // namespace

static void test_cut_logic()
{
    using namespace seqcut;
    for (int sub = 0; sub < SUB_COUNT; sub++)
        for (int d = 0; d < 2; d++)
            for (int col = 0; col < 12; col++)
            {
                uint8_t a = packArg((uint8_t)sub, d != 0, (uint8_t)col);
                CHECK(argSub(a) == sub && argDown(a) == (d != 0) && argCol(a) == col);
            }
    CHECK(groupFirst(0) == 0 && groupFirst(3) == 0 && groupFirst(4) == 4 && groupFirst(7) == 4 && groupFirst(15) == 12 &&
          groupFirst(200) == 12);
    CHECK(trackOfRow(4, 0) == 4 && trackOfRow(4, 3) == 7 && trackOfRow(12, 3) == 15);
    // column <-> position round trip against Playhead::columnPosition
    const int lens[6] = { 8, 16, 64, 100, 127, 128 };
    for (int li = 0; li < 6; li++)
    {
        seqgen::Playhead p;
        p.setLength(lens[li]);
        for (int pos = 0; pos < lens[li]; pos++)
        {
            int c = columnOf(pos, lens[li], 8);
            CHECK(c >= 0 && c < 8);
            CHECK(p.columnPosition(c) <= pos);
            CHECK(c == 7 || p.columnPosition(c + 1) > pos);
        }
        CHECK(columnOf(0, lens[li], 8) == 0 && columnOf(lens[li] - 1, lens[li], 8) == 7);
    }
    CHECK(columnOf(0, 1, 8) <= 7 && columnOf(5, 3, 8) <= 7);
    // position FX value: a 2 s loop sample (88200 frames), 16 steps at 120 bpm = 125 ms per step
    CHECK(positionFxValue(0, 125.0f, 88200, 44100) == 0);
    CHECK(positionFxValue(4, 125.0f, 88200, 44100) == 25);
    CHECK(positionFxValue(8, 125.0f, 88200, 44100) == 50);
    CHECK(positionFxValue(15, 125.0f, 88200, 44100) == 94);
    CHECK(positionFxValue(100, 125.0f, 88200, 44100) == 100);
    CHECK(positionFxValue(3, 125.0f, 0, 44100) == 0);
    CHECK(addPosition(30, 40) == 70 && addPosition(80, 40) == 100);
    // LEDs
    View v[kRows];
    for (int i = 0; i < kRows; i++)
    {
        v[i].col = 0;
        v[i].loopLo = 1;
        v[i].loopHi = 0;
        v[i].flags = (uint8_t)(1 << VF_RATE_SHIFT);
    }
    v[0].col = 3;
    v[1].col = 5;
    v[1].loopLo = 2;
    v[1].loopHi = 6;
    v[1].flags |= VF_ACTIVE | VF_REVERSE;
    v[2].flags |= VF_ACTIVE | VF_STOPPED;
    v[3].col = 7;
    v[3].flags = (uint8_t)(VF_ACTIVE | VF_RECORDED | (2 << VF_RATE_SHIFT));
    uint8_t out[kCutPads];
    fillLeds(v, false, out);
    CHECK(out[3] == LED_BRIGHT && out[2] == LED_DIM && out[0] == LED_DIM);
    CHECK(out[12 + 5] == LED_BRIGHT && out[12 + 2] == LED_LOOP && out[12 + 6] == LED_LOOP && out[12 + 1] == LED_DIM &&
          out[12 + 7] == LED_DIM);
    CHECK(out[12 + COL_REVERSE] == LED_BRIGHT && out[COL_REVERSE] == LED_DIM);
    CHECK(out[24 + 0] == LED_OFF && out[24 + COL_STOP] == LED_BRIGHT);
    CHECK(out[36 + 7] == LED_BRIGHT && out[36 + COL_RESYNC] == LED_BRIGHT && out[36 + COL_RATE] == LED_BRIGHT);
    CHECK(out[COL_RESYNC] == LED_DIM && out[12 + COL_RESYNC] == LED_MID);
    v[0].flags |= VF_REC;
    fillLeds(v, true, out);
    CHECK(out[COL_RESYNC] == LED_BRIGHT);
    fillLeds(v, false, out);
    CHECK(out[COL_RESYNC] == LED_OFF);
    for (int i = 0; i < kCutPads; i++)
        CHECK(out[i] <= 15);
}

static void test_cut_jump_and_retrigger()
{
    using namespace seqcut;
    Cut c;
    FakePattern f;
    f.map[0] = 1; // one long loop sample on step 1
    install(c, f);
    int g = 0;
    CHECK(c.activeMask() == 0);
    StepPlan p = c.boundary(0, 16, 0);
    CHECK(!p.override); // untouched track: stock behaviour
    for (int i = 0; i < 3; i++)
        stepOnce(c, f, g); // stock runs: pos 3
    CHECK(f.pos == 3);
    c.apply(0, packArg(SUB_PAD, true, 4)); // column 4 of 8 = step 8
    CHECK(c.activeMask() == 1);
    p = stepOnce(c, f, g);
    CHECK(p.override && p.pos == 8 && !p.hold && p.retrigger && p.noteStep == 0 && p.elapsed == 8);
    StepPlan r;
    CHECK(c.consumeRetrigger(0, r) && r.noteStep == 0 && r.elapsed == 8);
    CHECK(!c.consumeRetrigger(0, r));
    p = stepOnce(c, f, g); // continues without a retrigger
    CHECK(p.pos == 9 && !p.retrigger);
    c.apply(0, packArg(SUB_PAD, false, 4));
    p = stepOnce(c, f, g);
    CHECK(p.pos == 10 && !p.retrigger);
    // jump onto a step that has the note itself: nothing special
    c.apply(0, packArg(SUB_PAD, true, 0));
    p = stepOnce(c, f, g);
    CHECK(p.pos == 0 && !p.retrigger);
    // jump after a note-off: silence, nothing retriggered
    f.map[4] = 2;
    c.apply(0, packArg(SUB_PAD, false, 0));
    c.apply(0, packArg(SUB_PAD, true, 3)); // step 6
    int stops = f.stops;
    p = stepOnce(c, f, g);
    CHECK(p.pos == 6 && !p.retrigger && f.stops == stops + 1);
    // a track without any note: stops the sounding note, no retrigger
    FakePattern empty;
    Cut c2;
    install(c2, empty);
    c2.apply(0, packArg(SUB_PAD, true, 2));
    int gg = 0;
    p = stepOnce(c2, empty, gg);
    CHECK(p.pos == 4 && !p.retrigger && empty.stops == 1);
}

static void test_cut_loop_reverse_rate()
{
    using namespace seqcut;
    Cut c;
    FakePattern f;
    f.map[0] = 1;
    install(c, f);
    int g = 0;
    c.apply(0, packArg(SUB_PAD, true, 2));
    c.apply(0, packArg(SUB_PAD, true, 3)); // loop columns 2..3 = steps 4..7
    StepPlan p = stepOnce(c, f, g);
    CHECK(p.pos == 4 && p.retrigger && p.elapsed == 4);
    const int exp[6] = { 5, 6, 7, 4, 5, 6 };
    for (int i = 0; i < 6; i++)
    {
        p = stepOnce(c, f, g);
        CHECK(p.pos == exp[i]);
    }
    View v = c.view(0);
    CHECK(v.loopLo == 2 && v.loopHi == 3 && (v.flags & VF_ACTIVE));
    c.apply(0, packArg(SUB_PAD, false, 3));
    c.apply(0, packArg(SUB_PAD, false, 2));
    p = stepOnce(c, f, g);
    v = c.view(0);
    CHECK(v.loopLo == 2 && v.loopHi == 3); // plan 008: the loop latches, it stays after the pads are released
    c.apply(0, packArg(SUB_PAD, true, 6)); // a single pad in the row jumps and ends it
    c.apply(0, packArg(SUB_PAD, false, 6));
    p = stepOnce(c, f, g);
    v = c.view(0);
    CHECK(p.override && p.pos == 12);
    CHECK(v.loopLo > v.loopHi); // no loop range any more
    // reverse
    Cut r;
    FakePattern fr;
    fr.map[0] = 1;
    install(r, fr);
    int gr = 0;
    r.apply(0, packArg(SUB_PAD, true, 7)); // step 14
    p = stepOnce(r, fr, gr);
    CHECK(p.pos == 14);
    r.apply(0, packArg(SUB_PAD, false, 7));
    r.apply(0, packArg(SUB_REVERSE, true, 0));
    CHECK(r.view(0).flags & VF_REVERSE);
    p = stepOnce(r, fr, gr);
    CHECK(p.pos == 13 && p.retrigger && p.elapsed == 13); // reverse: every step replays "as if at"
    p = stepOnce(r, fr, gr);
    CHECK(p.pos == 12);
    // rate double: second half-step
    Cut d;
    FakePattern fd;
    fd.map[0] = 1;
    install(d, fd);
    int gd = 0;
    d.apply(0, packArg(SUB_RATE, true, 0)); // 1 -> 2
    CHECK(viewRate(d.view(0)) == 2);
    p = stepOnce(d, fd, gd);
    StepPlan m;
    CHECK(d.mid(0, 16, m) && m.override && m.pos == (uint8_t)((p.pos + 1) % 16));
    CHECK(!d.mid(0, 16, m)); // only once per step
    // rate half: second step holds
    Cut h;
    FakePattern fh;
    fh.map[0] = 1;
    install(h, fh);
    int gh = 0;
    h.apply(0, packArg(SUB_RATE, true, 0));
    h.apply(0, packArg(SUB_RATE, true, 0)); // 1 -> 2 -> 1/2
    CHECK(viewRate(h.view(0)) == 0);
    p = stepOnce(h, fh, gh);
    CHECK(!p.hold);
    int a = p.pos;
    p = stepOnce(h, fh, gh);
    CHECK(p.hold && p.pos == a && h.holding(0));
    p = stepOnce(h, fh, gh);
    CHECK(!p.hold && p.pos == (a + 1) % 16 && !h.holding(0));
}

static void test_cut_stop_resync_transport()
{
    using namespace seqcut;
    Cut c;
    FakePattern f;
    f.map[0] = 1;
    install(c, f);
    int g = 0;
    for (int i = 0; i < 5; i++)
        stepOnce(c, f, g);
    c.apply(0, packArg(SUB_STOP, true, 0));
    CHECK(c.stopped(0) && !c.holding(0) && f.stops == 1 && (c.view(0).flags & VF_STOPPED));
    StepPlan p = stepOnce(c, f, g);
    CHECK(!p.override && p.hold && c.stopped(0));
    // START: back on the global grid, then (nothing special left) the stock sequencer again
    c.apply(0, packArg(SUB_STOP, true, 0));
    p = stepOnce(c, f, g);
    CHECK(p.override && !p.hold && !c.stopped(0));
    CHECK(c.activeMask() == 0); // plain again: stock
    // a jumped track is off the grid; resync brings it back and counts as a jump (retrigger as if at global)
    c.apply(0, packArg(SUB_PAD, true, 6)); // step 12
    c.apply(0, packArg(SUB_PAD, false, 6));
    p = stepOnce(c, f, g);
    CHECK(p.pos == 12);
    c.apply(0, packArg(SUB_RESYNC, true, 0));
    int expect = (g + 1) % 16; // the global step that plays next (g is the step being left)
    p = stepOnce(c, f, g);
    CHECK(p.override && p.pos == expect && p.retrigger);
    CHECK(c.activeMask() == 0);
    // transport start keeps recorded loops active and restarts them
    const int startG = f.pos + 1; // the playhead counter when the track becomes cut-active; the take starts at its first step
    c.apply(0, packArg(SUB_REC, true, 0));
    c.apply(0, packArg(SUB_PAD, true, 4));
    stepOnce(c, f, g);
    stepOnce(c, f, g);
    stepOnce(c, f, g);
    c.apply(0, packArg(SUB_REC, false, 0));
    c.apply(0, packArg(SUB_PAD, false, 4));
    CHECK(c.view(0).flags & VF_RECORDED);
    c.transportStart();
    CHECK(c.activeMask() == 1);
    f.pos = 0;
    p = stepOnce(c, f, g);
    CHECK(p.override && p.pos == 8 + (((1 - startG) % 3) + 3) % 3); // recorded steps were 8 9 10, restarted at index (1 - start of the take) mod 3 (plan 008: in phase with the take)
}

static void test_cut_review_fixes()
{
    using namespace seqcut;
    // startPoint16: 2 s loop (88200 frames), 125 ms steps, original speed
    uint16_t sp = 0;
    CHECK(startPoint16(8, 125.0f, 88200, 44100, 1000, 0, false, 0, 65535, 65535, sp) && sp >= 32760 && sp <= 32780); // 50%
    CHECK(startPoint16(4, 125.0f, 88200, 44100, 2000, 0, false, 0, 65535, 65535, sp) && sp >= 32760 && sp <= 32780); // twice as fast
    CHECK(startPoint16(2, 125.0f, 88200, 44100, 1000, 16384, false, 0, 65535, 65535, sp) && sp > 16384 + 8000);      // starts at the start point
    CHECK(!startPoint16(20, 125.0f, 88200, 44100, 1000, 0, false, 0, 65535, 65535, sp));                              // behind a one-shot
    CHECK(startPoint16(20, 125.0f, 88200, 44100, 1000, 0, true, 0, 32768, 65535, sp) && sp < 32768);                 // wrapped into the loop
    // a stock position of its own: the real stock sequencer position does not follow the override
    {
        Cut c;
        FakePattern f;
        f.map[0] = 1;
        install(c, f);
        int g = 0;
        stepOnce(c, f, g);
        c.apply(0, packArg(SUB_PAD, true, 6)); // step 12
        c.setStock(0, 1);
        StepPlan p = stepOnce(c, f, g);
        CHECK(p.pos == 12 && c.stock(0) == 2);
        p = stepOnce(c, f, g); // stepOnce stores f.pos (the cut position + 1) as stock, as a real sequencer would not
        CHECK(p.pos == 13);
    }
    // MIDI / synth tracks: reversed running is no jump at every step
    {
        Cut c;
        FakePattern f;
        f.sample = false;
        f.map[0] = 1;
        install(c, f);
        int g = 0;
        c.apply(0, packArg(SUB_PAD, true, 7)); // step 14
        StepPlan p = stepOnce(c, f, g);
        CHECK(p.pos == 14 && p.retrigger);
        c.apply(0, packArg(SUB_PAD, false, 7));
        c.apply(0, packArg(SUB_REVERSE, true, 0));
        for (int i = 0; i < 4; i++)
        {
            p = stepOnce(c, f, g);
            CHECK(!p.retrigger);
        }
    }
    // second half-step is clamped to a shorter pattern
    {
        Cut c;
        FakePattern f;
        f.map[0] = 1;
        install(c, f);
        int g = 0;
        c.apply(0, packArg(SUB_PAD, true, 7));
        c.apply(0, packArg(SUB_RATE, true, 0));
        stepOnce(c, f, g); // pos 14, pos2 15
        StepPlan m;
        CHECK(c.mid(0, 8, m) && m.pos < 8);
    }
    // transport start from a position, stop does not count as hold, reset all
    {
        Cut c;
        FakePattern f;
        f.map[0] = 1;
        install(c, f);
        c.apply(0, packArg(SUB_PAD, true, 2));
        c.apply(1, packArg(SUB_STOP, true, 0));
        CHECK(c.activeMask() == 3 && c.stopped(1) && !c.holding(1));
        c.transportStart(5);
        CHECK(c.stock(0) == 5);
        c.apply(0, packArg(SUB_RESET, true, 0));
        CHECK(c.activeMask() == 0 && !c.stopped(1) && !c.holding(0));
    }
}

static void test_cut_engine_ring()
{
    MockHost h;
    Engine* e = mk(h);
    seqcut::Cut c;
    FakePattern f;
    f.map[0] = 1;
    install(c, f);
    e->setCut(&c);
    CHECK(!e->active());
    CHECK(!e->postCut(16, seqcut::packArg(seqcut::SUB_PAD, true, 0)));
    CHECK(!e->postCut(0, (uint8_t)seqcut::SUB_COUNT));
    CHECK(e->postCut(0, seqcut::packArg(seqcut::SUB_PAD, true, 3)));
    CHECK(e->active()); // pending command
    e->idleTick();      // transport stopped: commands still arrive
    CHECK(c.activeMask() == 1 && e->cutMask() == 1 && e->active());
    int u = 1;
    playUSteps(*e, 3, u);
    // play pressed: the playheads restart with the transport, the flag is consumed by the first tick
    e->cutPlayStarted();
    CHECK(e->active());
    playUSteps(*e, 1, u);
    CHECK(c.activeMask() == 1);
    // project load: every cut row goes back to stock
    e->resetState();
    e->idleTick();
    CHECK(c.activeMask() == 0 && !e->active());
    // without a Cut nothing breaks
    Engine* e2 = mk(h);
    CHECK(e2->postCut(0, seqcut::packArg(seqcut::SUB_PAD, true, 3)));
    e2->idleTick();
    CHECK(e2->cutMask() == 0);
    delete e2;
    delete e;
}

static void test_cut_layer()
{
    using namespace seqcut;
    tSetGroup(0); // constructs the Cut through seqCutInit()
    seqcut::Cut* c = seqEngine.cut();
    CHECK(c != 0);
    if (!c)
        return;
    FakePattern f;
    f.map[0] = 1;
    install(*c, f);
    uint8_t leds[kCutPads];
    cutQuantSet(Q_STEP); // (plan 008: a take starts with its first gesture on the grid; this test steps two steps only)
    cutUpdate();
    seqEngine.idleTick();
    tSetGroup(4); // tracks 4..7
    cutLeds(leds);
    CHECK(leds[0] == LED_BRIGHT && leds[1] == LED_DIM); // idle: playhead shown at column 0
    tCutPad(3, 1, true);                                  // track 5, column 3
    seqEngine.idleTick();
    CHECK(c->activeMask() == (1u << 5));
    // group changes while the pad is held: the release still goes to track 5
    tSetGroup(8);
    tCutPad(3, 1, false);
    seqEngine.idleTick();
    CHECK(c->playhead(5).heldCount() == 0);
    // two pads of a row = loop on that row's track
    tSetGroup(0);
    tCutPad(1, 0, true);
    tCutPad(4, 0, true);
    seqEngine.idleTick();
    CHECK(c->view(0).loopLo == 1 && c->view(0).loopHi == 4);
    cutLeds(leds);
    CHECK(leds[2] == LED_LOOP && leds[3] == LED_LOOP && leds[7] == LED_DIM);
    cutReleaseAll();
    seqEngine.idleTick();
    CHECK(c->playhead(0).heldCount() == 0);
    // controls
    tCutPad(COL_REVERSE, 2, true);
    tCutPad(COL_REVERSE, 2, false);
    tCutPad(COL_RATE, 2, true);
    tCutPad(COL_STOP, 3, true);
    seqEngine.idleTick();
    CHECK(c->view(2).flags & VF_REVERSE);
    CHECK(viewRate(c->view(2)) == 2);
    CHECK(c->view(3).flags & VF_STOPPED);
    // recording: a row starts its take with its first pad, Rec release closes all takes
    cutRec(true);
    tCutPad(5, 0, true);
    tCutPad(5, 0, false);
    seqEngine.idleTick();
    CHECK(c->view(0).flags & VF_REC);
    CHECK(!(c->view(1).flags & VF_REC)); // untouched rows record nothing
    int g = 0;
    f.pos = 0;
    stepOnce(*c, f, g);
    stepOnce(*c, f, g);
    tSetGroup(4); // the take keeps running while the group changes
    cutRec(false);
    seqEngine.idleTick();
    CHECK(!(c->view(0).flags & VF_REC) && (c->view(0).flags & VF_RECORDED));
    // a release that does not fit into the queue is kept and retried
    tSetGroup(0);
    cutReleaseAll();
    seqEngine.idleTick();
    tCutPad(2, 0, true);
    seqEngine.idleTick();
    CHECK(c->playhead(0).heldCount() == 1);
    for (int i = 0; i < 70; i++)
        seqEngine.postCut(9, seqcut::packArg(seqcut::SUB_RESYNC, true, 0)); // fills the queue (inactive track: ignored)
    tCutPad(2, 0, false);
    seqEngine.idleTick();
    seqEngine.idleTick();
    cutLeds(leds); // retries the lost release
    seqEngine.idleTick();
    CHECK(c->playhead(0).heldCount() == 0);
    // out of range pads are ignored
    tCutPad(12, 0, true);
    tCutPad(0, 4, true);
    tSetGroup(0);
}

// ---- integration: Session STOP wins over CUT, clip launch restarts the playhead, Session layer adapter ----
static void test_cut_session_freeze_and_launch()
{
    using namespace seqcut;
    Cut c;
    FakePattern f;
    f.map[0] = 1;
    install(c, f);
    c.apply(0, packArg(SUB_PAD, true, 2)); // jump to column 2 (position 4 of 16)
    int g = 0;
    StepPlan p = stepOnce(c, f, g);
    CHECK(p.override);
    // Session-stopped: the playhead does not advance and nothing is overridden
    View before = c.view(0);
    int prevPos = f.pos;
    for (int i = 0; i < 5; i++)
    {
        f.pos = (f.pos + 1) % f.steps;
        g = (g + 1) % f.steps;
        c.setStock(0, f.pos);
        StepPlan fz = c.boundary(0, f.steps, g, true);
        CHECK(!fz.override && fz.hold && !fz.retrigger);
        StepPlan m;
        CHECK(!c.mid(0, f.steps, m, true));
    }
    CHECK(c.view(0).col == before.col);
    (void)prevPos;
    // clip launch: the playhead restarts at the position the clip enters, a CUT stop ends
    c.apply(0, packArg(SUB_STOP, true, 0));
    CHECK(c.stopped(0));
    c.launched(0, 12, 5);
    CHECK(!c.stopped(0) && c.stock(0) == 5);
    StepPlan l = c.boundary(0, 12, 6);
    CHECK(l.override && l.pos == 5 && !l.retrigger);
    StepPlan n = c.boundary(0, 12, 7);
    CHECK(n.override && n.pos == 6); // keeps running from there
    // a track the playhead does not drive is not touched
    c.launched(3, 12, 5);
    CHECK(c.activeMask() == 1);
}

// The Session CUT layer: group of 4 tracks, group start -> rows
static void test_cut_session_layer_adapter()
{
    using namespace seqcut;
    seqsession::CutSessionLayer a;
    CHECK(a.groupSize() == 4 && strcmp(a.name(), "CUT") == 0);
    a.enter(0);
    seqcut::Cut* c = seqEngine.cut();
    CHECK(c != 0);
    if (!c)
        return;
    FakePattern f;
    f.map[0] = 1;
    install(*c, f);
    const uint8_t starts[4] = { 0, 4, 8, 12 };
    for (int gi = 0; gi < 4; gi++)
    {
        a.leave();
        seqEngine.resetState();
        seqEngine.idleTick();
        tSetGroup(starts[gi]);
        for (uint8_t row = 0; row < 4; row++)
        {
            tAPad(a, 11, row, true); // the last of the 12 pad columns (RESYNC) is the layer's
            tAPad(a, 11, row, false);
        }
        tAPad(a, 3, 2, true); // track starts[gi] + 2, position column 3
        seqEngine.idleTick();
        CHECK(c->activeMask() == (1u << (starts[gi] + 2)));
        tAPad(a, 3, 2, false);
        seqEngine.idleTick();
    }
    // an 8-track group start rounds down to a group of 4
    tSetGroup(8);
    tAPad(a, 0, 1, true);
    tAPad(a, 0, 1, false);
    seqEngine.idleTick();
    CHECK(((c->activeMask() >> 9) & 1u) != 0);
    // leaving lets go of held pads
    tAPad(a, 2, 0, true);
    seqEngine.idleTick();
    CHECK(c->playhead(8).heldCount() == 1);
    a.leave();
    seqEngine.idleTick();
    CHECK(c->playhead(8).heldCount() == 0);
    char lines[4][48];
    a.info(lines);
    CHECK(lines[0][0] != 0);
    // all 48 pads are the layer's
    uint8_t leds[48];
    a.leds(leds);
    for (int i = 0; i < 48; i++)
        CHECK(leds[i] <= 15);
    seqEngine.resetState();
    seqEngine.idleTick();
}


// ================================================================ plan 005 R4: SYNC ALL, survival (M5), panic (M6), lane screen
namespace
{
// One step boundary of tracks 0..n-1 (same fake pattern for all), firmware order: the global clock advances first.
void stepTracks(seqcut::Cut& c, FakePattern& f, int& global, int n, seqcut::StepPlan* out)
{
    f.pos = (f.pos + 1) % f.steps;
    global = (global + 1) % f.steps;
    for (int t = 0; t < n; t++)
    {
        c.setStock((uint8_t)t, f.pos);
        out[t] = c.boundary((uint8_t)t, f.steps, global);
    }
}

// The shared global Cut object with a fake pattern, clean state.
seqcut::Cut* freshCut(FakePattern& f)
{
    tSetGroup(0); // constructs the Cut through seqCutInit()
    seqcut::Cut* c = seqEngine.cut();
    if (c)
        install(*c, f);
    seqEngine.resetState();
    seqEngine.idleTick();
    // the engine tests below were written for 8 pads over a 16 or 32 step pattern: the grid 1/8 (2 steps per pad) resp. 1/4 (4 steps)
    // gives exactly those positions
    for (uint8_t t = 0; t < 16; t++)
        seqEngine.postCut(t, seqcut::packGrid(f.steps >= 32 ? seqcut::GRID_4 : seqcut::GRID_8));
    seqEngine.idleTick();
    g_shiftHeld = false;
    return c;
}
} // namespace

static void test_syncall_encoding()
{
    using namespace seqcut;
    CHECK(argIsSyncAll(packSyncAll()));
    CHECK(!argIsSyncAll(packArg(SUB_RESET, true, 0)) && !argIsSyncAll(packArg(SUB_RESYNC, true, 1)));
    CHECK(argSub(packSyncAll()) < SUB_COUNT); // a valid command for Engine::postCut
    // the one invalid sub value stays invalid (test_cut_engine_ring relies on it)
    MockHost h;
    Engine* e = mk(h);
    seqcut::Cut c;
    FakePattern f;
    install(c, f);
    e->setCut(&c);
    CHECK(e->postCut(0, packSyncAll()) && !e->postCut(0, (uint8_t)SUB_COUNT));
    e->idleTick();
    delete e;
}

static void test_syncall_one_pass_mixed_state()
{
    using namespace seqcut;
    Cut c;
    FakePattern f;
    f.map[0] = 1;
    f.map[8] = 1;
    install(c, f);
    int g = 0;
    StepPlan plans[5];
    for (int i = 0; i < 3; i++)
        stepTracks(c, f, g, 5, plans);
    // track 0: jammed with a held two-pad loop, track 1: reverse + double rate, track 2: recorded playhead loop,
    // track 3: STOP while a take is open, track 4: never touched
    c.apply(0, packArg(SUB_PAD, true, 1));
    c.apply(0, packArg(SUB_PAD, true, 5));
    c.apply(1, packArg(SUB_REVERSE, true, 0));
    c.apply(1, packArg(SUB_RATE, true, 0)); // 1x -> 2x
    c.apply(2, packArg(SUB_REC, true, 0));
    c.apply(2, packArg(SUB_PAD, true, 4));
    stepTracks(c, f, g, 5, plans);
    stepTracks(c, f, g, 5, plans);
    c.apply(2, packArg(SUB_PAD, false, 4));
    c.apply(2, packArg(SUB_REC, false, 0));
    c.apply(3, packArg(SUB_REC, true, 0));
    c.apply(3, packArg(SUB_STOP, true, 0));
    CHECK(c.activeMask() == 0x0F);
    CHECK(c.view(0).loopLo <= c.view(0).loopHi && (c.view(1).flags & VF_REVERSE) && viewRate(c.view(1)) == 2);
    CHECK(c.view(2).flags & VF_RECORDED);
    CHECK(c.stopped(3) && (c.view(3).flags & VF_REC));

    // ---- SYNC ALL: one command (the track byte is ignored); the state is dropped at once, the snap comes at the next step boundary
    c.apply(15, packSyncAll());
    for (int t = 0; t < 4; t++)
    {
        View v = c.view((uint8_t)t);
        CHECK(!(v.flags & (VF_REVERSE | VF_REC | VF_RECORDED | VF_STOPPED)));
        CHECK(viewRate(v) == 1);
        CHECK(!c.stopped((uint8_t)t) && !c.holding((uint8_t)t));
    }
    CHECK(c.activeMask() == 0x0F); // still driven until the boundary
    stepTracks(c, f, g, 5, plans);
    for (int t = 0; t < 4; t++)
        CHECK(plans[t].override && plans[t].pos == g && !plans[t].hold);
    CHECK(!plans[4].override); // the untouched track keeps the stock sequencer
    CHECK(c.activeMask() == 0); // all four handed back: plain, on the global playhead
    stepTracks(c, f, g, 5, plans); // and the following steps are stock again
    for (int t = 0; t < 5; t++)
        CHECK(!plans[t].override);
}

static void test_syncall_edge_cases()
{
    using namespace seqcut;
    // nothing active: no effect
    {
        Cut c;
        FakePattern f;
        install(c, f);
        c.apply(0, packSyncAll());
        CHECK(c.activeMask() == 0);
    }
    // transport stopped: there is no boundary; Play hands every synced track back to the stock sequencer
    {
        Cut c;
        FakePattern f;
        f.map[0] = 1;
        install(c, f);
        c.apply(0, packArg(SUB_PAD, true, 3));
        c.apply(0, packArg(SUB_PAD, false, 3));
        c.apply(1, packArg(SUB_STOP, true, 0));
        c.apply(2, packArg(SUB_RATE, true, 0));
        c.apply(0, packSyncAll());
        CHECK(c.activeMask() == 0x07);
        c.transportStart();
        CHECK(c.activeMask() == 0);
    }
    // a gesture after SYNC ALL wants the track off the grid again: Play keeps it driven
    {
        Cut c;
        FakePattern f;
        f.map[0] = 1;
        install(c, f);
        c.apply(0, packArg(SUB_PAD, true, 3));
        c.apply(0, packArg(SUB_PAD, false, 3));
        c.apply(0, packSyncAll());
        c.apply(0, packArg(SUB_REVERSE, true, 0));
        CHECK((c.view(0).flags & VF_REVERSE) != 0);
        c.transportStart();
        CHECK(c.activeMask() == 1);
    }
    // Session STOP wins: a frozen track keeps its pending snap until it runs again
    {
        Cut c;
        FakePattern f;
        f.map[0] = 1;
        install(c, f);
        c.apply(0, packArg(SUB_PAD, true, 6));
        int g = 0;
        stepOnce(c, f, g);
        c.apply(0, packSyncAll());
        f.pos = (f.pos + 1) % f.steps;
        g = (g + 1) % f.steps;
        c.setStock(0, g);
        StepPlan fz = c.boundary(0, f.steps, g, true);
        CHECK(!fz.override && fz.hold && c.activeMask() == 1);
        f.pos = (f.pos + 1) % f.steps;
        g = (g + 1) % f.steps;
        c.setStock(0, g);
        StepPlan run = c.boundary(0, f.steps, g, false);
        CHECK(run.override && run.pos == g && c.activeMask() == 0);
    }
    // a rate-2 second half-step queued before SYNC ALL is dropped
    {
        Cut c;
        FakePattern f;
        f.map[0] = 1;
        install(c, f);
        c.apply(0, packArg(SUB_PAD, true, 7));
        c.apply(0, packArg(SUB_RATE, true, 0));
        int g = 0;
        stepOnce(c, f, g);
        c.apply(0, packSyncAll());
        StepPlan m;
        CHECK(!c.mid(0, f.steps, m));
    }
}

// SYNC ALL through the main-loop layer: the Shift + RESYNC pad, the screen button, held pads and takes are forgotten.
static void test_syncall_layer_paths()
{
    using namespace seqcut;
    FakePattern f;
    f.map[0] = 1;
    seqcut::Cut* c = freshCut(f);
    CHECK(c != 0);
    if (!c)
        return;

    // jam all four rows of group 0, one row of group 2; Rec held, a pad held on row 0 (its take starts), two pads held on row 1
    for (uint8_t row = 0; row < 4; row++)
    {
        tCutPad(2, row, true);
        tCutPad(2, row, false);
    }
    tSetGroup(8);
    tCutPad(4, 2, true);
    tCutPad(4, 2, false);
    tSetGroup(0);
    cutRec(true);
    tCutPad(6, 0, true);
    tCutPad(1, 1, true);
    tCutPad(3, 1, true);
    seqEngine.idleTick();
    CHECK((c->activeMask() & 0x0F) == 0x0F && ((c->activeMask() >> 10) & 1u));
    CHECK(c->playhead(0).heldCount() == 1 && c->playhead(1).heldCount() == 2);
    CHECK(c->view(0).flags & VF_REC);

    // without Shift a RESYNC pad belongs to its row only
    tCutPad(COL_RESYNC, 3, true);
    tCutPad(COL_RESYNC, 3, false);
    seqEngine.idleTick();
    CHECK(c->playhead(0).heldCount() == 1 && (c->view(0).flags & VF_REC));

    // Shift + any RESYNC pad: all 16 tracks, held pads and the open take are dropped in one pass
    tCutPad(COL_RESYNC, 2, true, true);
    tCutPad(COL_RESYNC, 2, false, true);
    seqEngine.idleTick();
    CHECK(!cutSyncPending());
    for (uint8_t t = 0; t < 16; t++)
    {
        CHECK(c->playhead(t).heldCount() == 0);
        const View v = c->view(t);
        CHECK(!(v.flags & (VF_REC | VF_RECORDED | VF_REVERSE | VF_STOPPED)) && viewRate(v) == 1);
    }
    // the physical pads and Rec are released afterwards: nothing is posted for them (the take was forgotten)
    tCutPad(6, 0, false);
    tCutPad(1, 1, false);
    tCutPad(3, 1, false);
    cutRec(false);
    seqEngine.idleTick();
    CHECK(!(c->view(0).flags & VF_REC) && c->playhead(0).heldCount() == 0);
    // one boundary later every track is on the grid and stock again: all 16 in the same block
    for (uint8_t t = 0; t < 16; t++)
        c->setStock(t, 6);
    for (uint8_t t = 0; t < 16; t++)
        if ((c->activeMask() >> t) & 1u)
        {
            StepPlan p = c->boundary(t, 16, 6);
            CHECK(p.override && p.pos == 6);
        }
    CHECK(c->activeMask() == 0);
}

static void test_syncall_session_layer_adapter()
{
    using namespace seqcut;
    FakePattern f;
    f.map[0] = 1;
    seqcut::Cut* c = freshCut(f);
    CHECK(c != 0);
    if (!c)
        return;
    seqsession::CutSessionLayer a;
    a.enter(0);
    auto jamAll = [&]() {
        for (uint8_t row = 0; row < 4; row++)
        {
            tAPad(a, 4, row, true);
            tAPad(a, 4, row, false);
        }
        seqEngine.idleTick();
    };
    auto snapAll = [&](int pos) {
        for (uint8_t t = 0; t < 16; t++)
        {
            c->setStock(t, pos);
            c->boundary(t, 16, pos);
        }
    };

    // Shift is read on the press: Shift + RESYNC pad = SYNC ALL
    jamAll();
    CHECK((c->activeMask() & 0x0F) == 0x0F);
    g_shiftHeld = true;
    tAPad(a, 11, 1, true);
    g_shiftHeld = false; // Shift let go first; the pad release has no meaning
    tAPad(a, 11, 1, false);
    seqEngine.idleTick();
    snapAll(7);
    CHECK(c->activeMask() == 0);

    // without Shift: that row only
    jamAll();
    tAPad(a, 11, 1, true);
    tAPad(a, 11, 1, false);
    seqEngine.idleTick();
    snapAll(8);
    CHECK((c->activeMask() & 0x0F) == 0x0D);

    // screen button 6 (index 5) = SYNC ALL; its release and the other buttons are not it; the label shows when something is off grid
    char lab[12];
    CHECK(a.buttonLabel(kScreenButtonSyncAll, lab) && strcmp(lab, "[SYNC ALL]") == 0);
    // (plan 008 A: button 5 = jump quantise and button 7 = the [REC] armed label are the layer's now; tested in test_rec.inc)
    // (plan 016 A: the buttons 1-3 are Assign slot 1 / 2 and Help, 4 is empty; tested in test_mlr_slots)
    CHECK(!a.buttonLabel(7, lab));
    CHECK(!a.button(6, true) && !a.button(7, true));
    CHECK(a.button(kScreenButtonSyncAll, false));
    seqEngine.idleTick();
    CHECK((c->activeMask() & 0x0D) == 0x0D); // the release does nothing
    CHECK(a.button(kScreenButtonSyncAll, true));
    seqEngine.idleTick();
    snapAll(9);
    CHECK(c->activeMask() == 0);
    CHECK(a.buttonLabel(kScreenButtonSyncAll, lab) && strcmp(lab, "SYNC ALL") == 0);
    CHECK(a.drawsOwnScreen());
    a.leave();
    seqEngine.resetState();
    seqEngine.idleTick();
}

static void test_syncall_ring_full_is_retried()
{
    using namespace seqcut;
    FakePattern f;
    f.map[0] = 1;
    seqcut::Cut* c = freshCut(f);
    CHECK(c != 0);
    if (!c)
        return;
    tCutPad(3, 0, true);
    tCutPad(3, 0, false);
    tCutPad(COL_REVERSE, 0, true);
    seqEngine.idleTick();
    CHECK(c->view(0).flags & VF_REVERSE);
    for (int i = 0; i < 70; i++)
        seqEngine.postCut(9, packArg(SUB_RESYNC, true, 0)); // the queue is full
    cutSyncAll();
    CHECK(cutSyncPending());       // kept, not lost
    tCutPad(5, 0, true);            // a press must not overtake the waiting SYNC ALL (it would be wiped by it)
    seqEngine.idleTick();          // the audio side drains the queue
    CHECK(c->view(0).flags & VF_REVERSE);
    cutUpdate();                   // the main loop retries
    CHECK(!cutSyncPending());
    seqEngine.idleTick();
    CHECK(!(c->view(0).flags & VF_REVERSE));
    CHECK(c->playhead(0).heldCount() == 0);
    tCutPad(5, 0, false);
    seqEngine.resetState();
    seqEngine.idleTick();
}

// M5 (what survives leaving the CUT layer) and M6 (transport Stop = panic)
static void test_survival_and_panic()
{
    using namespace seqcut;
    FakePattern f;
    f.map[0] = 1;
    seqcut::Cut* c = freshCut(f);
    CHECK(c != 0);
    if (!c)
        return;

    cutQuantSet(Q_STEP); // (plan 008: a take starts with its first gesture on the grid; this test steps single boundaries)
    cutUpdate();
    seqEngine.idleTick();
    seqsession::CutSessionLayer a;
    a.enter(0);
    tAPad(a, 5, 0, true);  tAPad(a, 5, 0, false);                    // row 0: a jam at column 5
    tAPad(a, COL_REVERSE, 1, true);  tAPad(a, COL_REVERSE, 1, false); // row 1: reverse
    tAPad(a, COL_RATE, 2, true);  tAPad(a, COL_RATE, 2, false);       // row 2: double rate
    tAPad(a, COL_STOP, 3, true);  tAPad(a, COL_STOP, 3, false);       // row 3: stopped
    tSetGroup(4);
    a.rec(true);
    tAPad(a, 2, 0, true);                                          // track 4: a take starts ...
    seqEngine.idleTick();
    c->setStock(4, 1);
    c->boundary(4, 16, 1);
    c->setStock(4, 2);
    c->boundary(4, 16, 2);
    tAPad(a, 2, 0, false);
    a.rec(false);                                               // the Rec release means nothing (plan 008 A: Rec is a toggle)
    seqEngine.idleTick();
    CHECK(cutRecHeld());
    a.rec(true);                                                // ... the next press closes: a recorded playhead loop
    seqEngine.idleTick();
    CHECK(c->view(4).flags & VF_RECORDED);
    tAPad(a, 3, 2, true);                                          // track 6: a pad is held in the hand (loop gesture)
    tAPad(a, 5, 2, true);
    a.rec(true);
    tAPad(a, 1, 3, true);                                          // track 7: a take is open
    seqEngine.idleTick();
    CHECK(c->playhead(6).heldCount() == 2 && (c->view(7).flags & VF_REC));
    const uint16_t before = c->activeMask();
    CHECK(before == 0x00DF); // tracks 1-4 and 5, 7, 8 (track 6 was never touched)

    // leaving the layer (to CLIPS or another page)
    a.leave();
    seqEngine.idleTick();
    CHECK(c->activeMask() == before);                           // nothing went back to the stock sequencer
    CHECK(c->view(1).flags & VF_REVERSE);                       // reverse, rate, STOP, the recorded loop, the jam: still there
    CHECK(viewRate(c->view(2)) == 2);
    CHECK(c->view(3).flags & VF_STOPPED);
    CHECK(c->view(4).flags & VF_RECORDED);
    CHECK(c->view(0).flags & VF_ACTIVE);
    CHECK(c->playhead(6).heldCount() == 0);                     // the pads in the hand are let go
    CHECK(!(c->view(7).flags & VF_REC));                        // the open take is closed
    a.enter(0);
    CHECK(c->activeMask() == before);                           // coming back changes nothing

    // M6: Stop = panic. Holds and takes end, SYNC ALL; the next Play starts every track on the grid.
    tSetGroup(0);
    tAPad(a, 5, 0, true);
    a.rec(true);
    tAPad(a, 6, 1, true);
    seqEngine.idleTick();
    cutPanic();
    seqEngine.idleTick();
    CHECK(!cutSyncPending());
    for (uint8_t t = 0; t < 16; t++)
    {
        CHECK(c->playhead(t).heldCount() == 0);
        const View v = c->view(t);
        CHECK(!(v.flags & (VF_REVERSE | VF_STOPPED | VF_REC | VF_RECORDED)) && viewRate(v) == 1);
    }
    c->transportStart();
    CHECK(c->activeMask() == 0);
    tAPad(a, 5, 0, false);  // the physical releases arrive later: ignored
    tAPad(a, 6, 1, false);
    a.rec(false);
    seqEngine.idleTick();
    CHECK(c->activeMask() == 0);
    a.leave();
    seqEngine.resetState();
    seqEngine.idleTick();
}

static void test_group_arrows()
{
    seqsession::CutSessionLayer a;
    a.enter(4);
    g_groupRequests = 0;
    // plan 016 A: no groups of tracks any more (the slots pick their tracks); Left / Right are swallowed, Up / Down are the host's
    CHECK(a.arrow(seqsession::ARROW_LEFT, false) && a.arrow(seqsession::ARROW_RIGHT, false) && g_groupRequests == 0);
    CHECK(!a.arrow(seqsession::ARROW_UP, false) && !a.arrow(seqsession::ARROW_DOWN, false));
    const int showBefore = g_laneShow, hideBefore = g_laneHide;
    a.enter(0);
    a.leave();
    CHECK(g_laneShow == showBefore + 1 && g_laneHide == hideBefore + 1);
    seqEngine.resetState();
    seqEngine.idleTick();
}

// ---- lane screen geometry ----
static void test_lane_geometry()
{
    using namespace seqcut;
    const int lens[] = { 1, 2, 3, 5, 8, 12, 16, 24, 32, 33, 48, 64, 100, 128 };
    for (size_t li = 0; li < sizeof(lens) / sizeof(lens[0]); li++)
    {
        const int steps = lens[li];
        int prev = -1;
        for (int pos = 0; pos < steps; pos++)
        {
            const int col = columnOf(pos, steps, 8);
            for (int fr = 0; fr < 48; fr += 7)
            {
                const int x = laneOffset(pos, fr, steps);
                CHECK(x >= 0 && x < kLaneW);
                CHECK(x >= col * kSegW && x < (col + 1) * kSegW); // inside the segment of its pad
                CHECK(x >= prev);                                  // never moves backwards
                prev = x;
            }
            prev = laneOffset(pos, 47, steps);
        }
        CHECK(laneOffset(0, 0, steps) == columnOf(0, steps, 8) * kSegW);
    }
    // 16 steps: 2 steps per segment, 75 px per segment
    CHECK(laneOffset(0, 0, 16) == 0 && laneOffset(1, 0, 16) == 37 && laneOffset(2, 0, 16) == 75 && laneOffset(15, 47, 16) == 599);
    CHECK(laneOffset(-3, -1, 16) == 0 && laneOffset(5, 99, 16) == laneOffset(5, 47, 16) && laneOffset(3, 5, 0) == 0);
    CHECK(laneOffset(99, 0, 16) == laneOffset(15, 0, 16)); // a stale position is clamped to the last step
    CHECK(laneOffset(4, 24, 16) > laneOffset(4, 0, 16) && laneOffset(4, 47, 16) - laneOffset(4, 0, 16) < 38); // smooth inside a step
    CHECK(laneFrac(1, false) == 0 && laneFrac(48, false) == 47 && laneFrac(1, true) == 47 && laneFrac(48, true) == 0 &&
          laneFrac(0, false) == 0 && laneFrac(200, false) == 47);

    // heads: playhead, segment, grid ghost
    View v;
    v.col = 2; v.loopLo = 1; v.loopHi = 0; v.flags = (uint8_t)((1 << VF_RATE_SHIFT) | VF_ACTIVE);
    HeadInfo h = makeHead(v, 6, 1, 6, 16);
    CHECK(h.x == laneOffset(6, 0, 16) && h.seg == 3 && h.ghost == -1); // same place as the grid: no ghost
    h = makeHead(v, 6, 24, 2, 16);
    CHECK(h.ghost == laneOffset(2, 23, 16) && h.ghost != h.x);
    v.flags = (uint8_t)(1 << VF_RATE_SHIFT); // a stock track is never off the grid
    h = makeHead(v, 6, 1, 2, 16);
    CHECK(h.ghost == -1);
    v.flags = (uint8_t)((1 << VF_RATE_SHIFT) | VF_ACTIVE | VF_REVERSE);
    h = makeHead(v, 6, 1, 2, 16);
    CHECK(h.x == laneOffset(6, 47, 16) && h.ghost == laneOffset(2, 0, 16)); // reverse runs backwards, the grid forwards
    h = makeHead(v, 6, 1, 99, 16); // a stale stock position outside the pattern is no ghost
    CHECK(h.ghost == -1);

    uint8_t bar, beat;
    barBeat(0, bar, beat); CHECK(bar == 1 && beat == 1);
    barBeat(5, bar, beat); CHECK(bar == 1 && beat == 2);
    barBeat(16, bar, beat); CHECK(bar == 2 && beat == 1);
    barBeat(127, bar, beat); CHECK(bar == 8 && beat == 4);
    barBeat(-4, bar, beat); CHECK(bar == 1 && beat == 1);

    // note marks and the redraw gate (25 fps)
    uint32_t w[4] = { 0, 0, 0, 0 };
    noteBitSet(w, 0); noteBitSet(w, 33); noteBitSet(w, 127); noteBitSet(w, 128); noteBitSet(w, -1);
    CHECK(noteBitGet(w, 0) && noteBitGet(w, 33) && noteBitGet(w, 127) && !noteBitGet(w, 1) && !noteBitGet(w, 128) && !noteBitGet(w, -1));
    CHECK(noteBucket(16) == 1 && noteBucket(32) == 1 && noteBucket(33) == 2 && noteBucket(64) == 2 && noteBucket(128) == 4);
    for (int s = 1; s <= kMaxSteps; s++)
        CHECK((s + noteBucket(s) - 1) / noteBucket(s) <= kNoteRects); // never more than kNoteRects marks per lane
    CHECK(!redrawDue(false, 1000, 0) && !redrawDue(true, 1039, 1000) && redrawDue(true, 1040, 1000));
    CHECK(!redrawDue(true, 5, 0xFFFFFFF0u) && redrawDue(true, 60, 0xFFFFFFF0u)); // the millisecond counter wraps
}

static void test_lane_palette()
{
    using namespace seqcut;
    CHECK(mixRgb(0x102030, 0xF0E0D0, 0) == 0x102030);
    CHECK(mixRgb(0x000000, 0xFFFFFF, 128) == 0x7F7F7F);
    CHECK(contrastFg(0x000000) > 0x808080 && contrastFg(0xF0F0F0) < 0x808080);
    Palette p = makePalette(0x101010, contrastFg(0x101010), 0xCD1818, 0xD67022, 0x3FFFFF);
    CHECK(p.lane != p.bg && p.segAlt != p.lane && p.grid != p.segAlt && p.note != p.grid && p.text != p.bg && p.rec == 0xCD1818);
}

// ---- drawing: display-list budget and layout bounds (a stand-in context counts like the FT81x display list) ----
namespace
{
struct CountCtx
{
    int words = 0, rects = 0, discs = 0, texts = 0;
    int minX = 9999, minY = 9999, maxX = -1, maxY = -1;
    uint8_t prim = 0;
    FILE* dump = 0;
    void ensure(uint8_t w)
    {
        if (prim == w) return;
        if (prim) words++;      // END
        if (w == 1) words += 2; // LINE_WIDTH + BEGIN
        else if (w == 2) words += 1;
        prim = w;
    }
    void bound(int x0, int y0, int x1, int y1)
    {
        if (x0 < minX) minX = x0;
        if (y0 < minY) minY = y0;
        if (x1 > maxX) maxX = x1;
        if (y1 > maxY) maxY = y1;
    }
    void color(uint32_t rgb) { words++; if (dump) fprintf(dump, "C %06x\n", (unsigned)rgb); }
    void alpha(uint8_t a) { words++; if (dump) fprintf(dump, "A %d\n", a); }
    void rect(int x0, int y0, int x1, int y1)
    {
        ensure(1);
        words += 2;
        rects++;
        bound(x0, y0, x1, y1);
        if (dump) fprintf(dump, "R %d %d %d %d\n", x0, y0, x1, y1);
    }
    void disc(int cx, int cy, int r)
    {
        ensure(2);
        words += 2;
        discs++;
        bound(cx - r, cy - r, cx + r, cy + r);
        if (dump) fprintf(dump, "D %d %d %d\n", cx, cy, r);
    }
    void text(int x, int y, int font, int opt, const char* s)
    {
        ensure(0);
        const int n = (int)strlen(s);
        words += n * 2 + 2; // glyphs beyond x 511 need CELL + VERTEX2F
        textWords += n * 2 + 2;
        fifoText += 12 + (n + 1 + 3) / 4 * 4; // v0.3.9: what the FT81x command FIFO gets for CMD_TEXT
        texts++;
        const int w = n * (font >= 5 ? 6 : 9), h = font >= 5 ? 13 : 17; // font 5 is the small one (12 px)
        const int x0 = (opt & seqcut::TX_CENTERX) ? x - w / 2 : ((opt & seqcut::TX_RIGHT) ? x - w : x);
        const int y0 = (opt & seqcut::TX_CENTERY) ? y - h / 2 : y;
        bound(x0, y0, x0 + w, y0 + h);
        if (dump) fprintf(dump, "T %d %d %d %d %s\n", x, y, font, opt, s);
    }
    void finish() { ensure(0); }
    int bytes() const { return words * 4; }
    // v0.3.9: bytes of the co-processor command FIFO (one stage = one burst, must stay under 4092; seqTraceStages.h)
    int textWords = 0, fifoText = 0;
    int fifo() const { return (words - textWords) * 4 + fifoText; }
};

seqcut::LaneModel worstLanes(bool rec)
{
    using namespace seqcut;
    LaneModel m;
    memset(&m, 0, sizeof(m));
    m.first = 12;
    m.recHeld = rec ? 1 : 0;
    for (int r = 0; r < kRows; r++)
    {
        LaneInfo& l = m.lane[r];
        l.track = (uint8_t)(12 + r);
        l.steps = kMaxSteps;
        for (int i = 0; i < kMaxSteps; i += 2) // a note on every other step: every bucket of 4 holds one
            noteBitSet(l.notes, i);
        l.loopLo = 1;
        l.loopHi = 6;
        l.flags = (uint8_t)(VF_ACTIVE | VF_STOPPED | VF_REVERSE | VF_REC | VF_RECORDED | (2 << VF_RATE_SHIFT));
    }
    return m;
}
} // namespace

static void test_lane_draw_budget_and_bounds()
{
    using namespace seqcut;
    const Palette p = makePalette(0x000000, contrastFg(0x000000), 0xCD1818, 0xD67022, 0x3FFFFF);

    // worst case: 128-step patterns full of notes, every flag set, header red
    CountCtx w;
    drawLanes(w, worstLanes(true), p);
    w.finish();
    HeadModel hm;
    memset(&hm, 0, sizeof(hm));
    for (int r = 0; r < kRows; r++)
    {
        hm.h[r].x = (int16_t)(100 + r * 50);
        hm.h[r].ghost = (int16_t)(300 + r);
        hm.h[r].seg = (uint8_t)(r * 2);
        hm.h[r].flags = VF_ACTIVE;
    }
    hm.bar = 8;
    hm.beat = 4;
    hm.recHeld = 1;
    CountCtx hw;
    drawHeads(hw, hm, p);
    hw.finish();
    printf("  lanes worst case: %d words (%d bytes), %d rects, %d discs, %d texts; playheads %d bytes\n", w.words, w.bytes(), w.rects,
           w.discs, w.texts, hw.bytes());
    printf("  lanes worst case command FIFO: %d bytes, playheads %d (one stage each, FIFO 4096)\n", w.fifo(), hw.fifo());
    CHECK(w.fifo() <= 3840 && hw.fifo() <= 3840); // v0.3.9: one display stage is one burst into the 4 KB FT81x command FIFO
    CHECK(w.bytes() <= 4800);                 // the whole frame (every control on screen) must stay below 8172 bytes of display list
    CHECK(hw.bytes() <= 500);                 // the part that is redrawn up to 25 times per second
    CHECK(w.bytes() + hw.bytes() <= 5200);

    // typical: 16 steps, stock tracks
    LaneModel t;
    memset(&t, 0, sizeof(t));
    for (int r = 0; r < kRows; r++)
    {
        t.lane[r].track = (uint8_t)r;
        t.lane[r].steps = 16;
        t.lane[r].flags = (uint8_t)(1 << VF_RATE_SHIFT);
        t.lane[r].loopLo = 1;
        noteBitSet(t.lane[r].notes, 0);
        noteBitSet(t.lane[r].notes, 4);
        noteBitSet(t.lane[r].notes, 8 + r);
    }
    CountCtx tc;
    drawLanes(tc, t, p);
    tc.finish();
    printf("  lanes typical (16 steps, stock tracks): %d bytes\n", tc.bytes());
    CHECK(tc.bytes() < w.bytes());

    // layout: below the stock title bar (y 28), above the screen button labels (y 424), inside the screen
    CHECK(w.minY >= 30 && w.maxY <= 424 && w.minX >= 0 && w.maxX <= 800);
    CHECK(tc.minY >= 30 && tc.maxY <= 424 && tc.minX >= 0 && tc.maxX <= 800);
    CHECK(hw.minY >= 30 && hw.maxY <= 424 && hw.minX >= 0 && hw.maxX <= 800);
    CHECK(kLaneX + kLaneW < kCellX && kCellX + 48 + 44 <= 800);
    CHECK(kLaneStride >= kLaneH + 2 && kLaneTop + 3 * kLaneStride + kLaneH <= 424);

    // the playhead at the very end of a lane stays inside the screen
    HeadModel ext;
    memset(&ext, 0, sizeof(ext));
    for (int r = 0; r < kRows; r++)
    {
        ext.h[r].x = (int16_t)(kLaneW - 1);
        ext.h[r].ghost = (int16_t)(kLaneW - 1);
        ext.h[r].seg = 7;
    }
    CountCtx ec;
    drawHeads(ec, ext, p);
    ec.finish();
    CHECK(ec.maxX <= 800 && ec.minX >= 0);

    if (const char* dir = getenv("SEQ_LANE_PREVIEW")) // `make preview`: dump the draw ops of a few scenes for lane_preview.py
    {
        char path[512];
        for (int scene = 0; scene < 3; scene++)
        {
            snprintf(path, sizeof(path), "%s/lanes%d.ops", dir, scene);
            FILE* fp = fopen(path, "w");
            if (!fp) continue;
            LaneModel m = scene == 0 ? t : worstLanes(scene == 2);
            if (scene == 0)
            {
                // a typical jam: track 2 jammed with a loop, track 3 reverse at 1/2, track 4 recorded replay
                m.lane[1].flags = (uint8_t)(VF_ACTIVE | (1 << VF_RATE_SHIFT));
                m.lane[1].loopLo = 2;
                m.lane[1].loopHi = 4;
                m.lane[2].flags = (uint8_t)(VF_ACTIVE | VF_REVERSE);
                m.lane[3].flags = (uint8_t)(VF_ACTIVE | VF_RECORDED | (1 << VF_RATE_SHIFT));
                m.lane[3].steps = 24;
            }
            HeadModel h;
            memset(&h, 0, sizeof(h));
            for (int r = 0; r < kRows; r++)
            {
                View v;
                v.col = 0; v.loopLo = 1; v.loopHi = 0; v.flags = m.lane[r].flags;
                h.h[r] = makeHead(v, 3 + r * 2, 20, 1 + r, m.lane[r].steps);
            }
            h.bar = 3;
            h.beat = 2;
            h.recHeld = m.recHeld;
            CountCtx d;
            d.dump = fp;
            drawLanes(d, m, p);
            drawHeads(d, h, p);
            d.finish();
            fclose(fp);
        }
    }
}

// ---- R4 review fixes ----
static void test_r4_handback_retrigger_and_stock_phase()
{
    using namespace seqcut;
    // C1: the snap-back lands after the note step: the note is struck again "as if at" the position, although the track
    // is handed back to the stock sequencer at the same boundary (SYNC ALL and plain RESYNC)
    for (int mode = 0; mode < 2; mode++)
    {
        Cut c;
        FakePattern f;
        f.map[0] = 1;
        install(c, f);
        c.apply(0, packArg(SUB_PAD, true, 4)); // jam to step 8
        c.apply(0, packArg(SUB_PAD, false, 4));
        int g = 0;
        stepOnce(c, f, g);
        stepOnce(c, f, g);
        c.apply(0, mode == 0 ? packSyncAll() : packArg(SUB_RESYNC, true, 0));
        f.pos = (f.pos + 1) % f.steps;
        g = (g + 1) % f.steps;
        c.setStock(0, f.pos);
        const int stockPos = f.pos;
        StepPlan p = c.boundary(0, f.steps, g);
        CHECK(p.override && p.pos == stockPos && p.retrigger && c.activeMask() == 0);
        CHECK(c.hookMask() == 1); // the audio hooks stay on for this one retrigger
        StepPlan r;
        CHECK(c.consumeRetrigger(0, r) && r.retrigger && r.noteStep == 0 && r.elapsed == stockPos);
        CHECK(!c.consumeRetrigger(0, r) && c.hookMask() == 0);
    }
    // M1: a snapped track looks like a stock track at once (no OFF GRID after a panic while stopped)
    {
        Cut c;
        FakePattern f;
        f.map[0] = 1;
        install(c, f);
        c.apply(0, packArg(SUB_PAD, true, 4));
        c.apply(0, packArg(SUB_REVERSE, true, 0));
        CHECK(c.offGridMask() == 1 && (c.view(0).flags & VF_ACTIVE));
        c.apply(0, packSyncAll());
        CHECK(c.activeMask() == 1 && c.offGridMask() == 0 && !(c.view(0).flags & (VF_ACTIVE | VF_REVERSE)));
        c.apply(0, packArg(SUB_REVERSE, true, 0)); // a new gesture: off the grid again
        CHECK(c.offGridMask() == 1 && (c.view(0).flags & VF_ACTIVE));
        c.apply(0, packSyncAll());
        c.transportStart();
        CHECK(c.activeMask() == 0 && c.offGridMask() == 0 && c.hookMask() == 0);
    }
    // M3: the snap lands on the track's own stock position (own length), not on the global counter
    {
        Cut c;
        FakePattern f;
        f.steps = 12;
        f.map[0] = 1;
        install(c, f);
        c.apply(0, packArg(SUB_PAD, true, 6));
        c.apply(0, packSyncAll());
        c.setStock(0, 5);
        StepPlan p = c.boundary(0, 12, 13); // global counter 13 (wraps at 16 on another track), this track is at 5 of 12
        CHECK(p.override && p.pos == 5);
        Cut d;
        install(d, f);
        d.apply(0, packArg(SUB_PAD, true, 6));
        d.apply(0, packArg(SUB_RESYNC, true, 0));
        d.setStock(0, 7);
        p = d.boundary(0, 12, 30);
        CHECK(p.override && p.pos == 7);
    }
    // m5: SUB_RESET col 0 = reset, col 1 = SYNC ALL, anything else is ignored
    {
        Cut c;
        FakePattern f;
        install(c, f);
        c.apply(0, packArg(SUB_PAD, true, 2));
        for (uint8_t col = 2; col < 16; col++)
            c.apply(0, packArg(SUB_RESET, true, col));
        CHECK(c.activeMask() == 1 && c.offGridMask() == 1);
        c.apply(0, packArg(SUB_RESET, false, 0));
        CHECK(c.activeMask() == 0);
    }
    CHECK(!kRedrawPerStep); // default: smooth 25 fps playhead
}

// ---- plan 007 B1: CLIPS <-> CUT handover ("stuck in MLR") ----
// Model of the display's refresh queue (core/libs/NewDisplay/display.cpp step1): the head entry of the queue is only passed once its control
// has been drawn; an entry whose control slot is empty is never passed (step1 returns on it), so everything behind it - the grid of the
// CLIPS layer - is never drawn again. destroyControl() also shifts the slots above the freed one down.
namespace
{
struct FakeDisplay
{
    bool alive[8];
    int queue[32];
    int head, tail;
    int drawn[8];
    FakeDisplay() : head(0), tail(0) { for (int i = 0; i < 8; i++) { alive[i] = false; drawn[i] = 0; } }
    int create() { for (int i = 0; i < 8; i++) if (!alive[i]) { alive[i] = true; return i; } return -1; }
    void destroy(int h) { alive[h] = false; }
    void refresh(int h) { for (int i = head; i != tail; i = (i + 1) % 32) if (queue[i] == h) return; queue[tail] = h; tail = (tail + 1) % 32; }
    void update() { while (head != tail) { int h = queue[head]; if (!alive[h]) return; drawn[h]++; head = (head + 1) % 32; } }
};
} // namespace

static void test_layer_handover_display_queue()
{
    // the old handover: leaving CUT queued a refresh of both lane controls and then destroyed them
    FakeDisplay d;
    int grid = d.create(), lanes = d.create();
    d.refresh(lanes); d.destroy(lanes);   // laneViewShow(false) + laneViewDestroy()
    d.refresh(grid);                      // CLIPS enter: the grid is to be drawn
    d.update();
    CHECK(d.drawn[grid] == 0);            // the bug: the queue is stuck on the dead slot, the grid is never drawn

    // the new handover: created once, hidden and shown only
    FakeDisplay e;
    grid = e.create(); lanes = e.create();
    for (int i = 0; i < 4; i++)
    {
        e.refresh(lanes);                 // laneViewShow(true / false) refreshes its controls
        e.refresh(grid);
        e.update();
    }
    CHECK(e.drawn[grid] == 4 && e.drawn[lanes] == 4);
}

static void test_layer_handover_lifecycle()
{
    FakePattern f;
    f.map[0] = 1;
    seqcut::Cut* c = freshCut(f);
    CHECK(c != 0);
    if (!c)
        return;
    g_laneCreate = g_laneForget = g_laneShow = g_laneHide = 0;
    seqsession::CutSessionLayer a;
    a.createControls();                   // module start
    CHECK(g_laneCreate == 1 && g_laneShow == 0);
    for (int i = 0; i < 5; i++)           // CLIPS -> CUT -> CLIPS, repeatedly
    {
        a.enter(0);
        CHECK(g_laneShow == i + 1 && g_laneHide == i);
        a.leave();
        CHECK(g_laneShow == i + 1 && g_laneHide == i + 1);
    }
    CHECK(g_laneCreate == 1);             // never created again while the module runs, ...
    CHECK(g_laneForget == 0);             // ... and the controls are never given up before the module stops (a destroyed control stalls the display queue)
    a.destroyControls();                  // module stop
    CHECK(g_laneForget == 1);
    a.createControls();                   // module start again: fresh controls
    CHECK(g_laneCreate == 2);
    seqEngine.resetState();
    seqEngine.idleTick();
}

// ================================================================ plan 016 A: MLR slots on a musical grid, cue cache, lane screen
#include "seqCueCache.h"
#include "mlrDraw.h"

// A Cut in section mode of its own (not the shared one), pattern of `steps` steps, grid `g` for track 0.
static void secSetup(seqcut::Cut& c, FakePattern& f, uint8_t g)
{
    install(c, f);
    c.setSectionMode(true);
    c.apply(0, seqcut::packQuant(seqcut::Q_STEP));
    c.apply(0, seqcut::packGrid(g));
}

static void test_mlr_grid_helpers()
{
    using namespace seqcut;
    CHECK(gridSteps(GRID_4) == 4 && gridSteps(GRID_8) == 2 && strcmp(gridName(GRID_4), "1/4") == 0 && strcmp(gridName(GRID_8), "1/8") == 0);
    CHECK(sectionSteps(4) == 64 && sectionSteps(2) == 32);
    CHECK(sectionCount(64, 4) == 1 && sectionCount(65, 4) == 2 && sectionCount(128, 4) == 2 && sectionCount(128, 2) == 4 && sectionCount(1, 4) == 1);
    CHECK(sectionOf(0, 4) == 0 && sectionOf(63, 4) == 0 && sectionOf(64, 4) == 1 && sectionOf(100, 2) == 3 && sectionOf(-5, 4) == 0);
    // pattern lengths that are not a multiple of the section: the last section is short, its pads past the end are inactive
    CHECK(padsIn(0, 70, 4) == 16 && padsIn(1, 70, 4) == 2 && padsIn(2, 70, 4) == 0);
    CHECK(padsIn(0, 16, 4) == 4 && padsIn(0, 16, 2) == 8 && padsIn(0, 5, 2) == 3 && padsIn(0, 64, 4) == 16 && padsIn(1, 96, 2) == 16);
    CHECK(padOfPos(70, 1, 4) == 1 && padOfPos(3, 1, 4) == 0xFF && padOfPos(64, 1, 4) == 0 && padOfPos(127, 1, 4) == 15 && padOfPos(128, 1, 4) == 0xFF);
    // commands share the slot of SUB_RESET but are not RESET, SYNC ALL or quantise
    for (uint8_t g = 0; g < GRID_COUNT; g++)
    {
        const uint8_t a = packGrid(g);
        CHECK(argIsGrid(a) && argGrid(a) == g && !argIsHold(a) && !argIsSyncAll(a) && !argIsQuant(a) && argSub(a) < SUB_COUNT);
    }
    for (int on = 0; on < 2; on++)
    {
        const uint8_t a = packHold(on != 0);
        CHECK(argIsHold(a) && argHold(a) == (on != 0) && !argIsGrid(a) && !argIsSyncAll(a) && !argIsQuant(a));
    }
    CHECK(!argIsGrid(packSyncAll()) && !argIsHold(packSyncAll()) && !argIsGrid(packQuant(Q_BAR)) && !argIsHold(packQuant(Q_BAR)));
    // the 12 x 4 pad map: steps 1-16 on cols 1-8 of the slot's two rows, extras on cols 9-12
    int steps = 0, ctl = 0;
    for (uint8_t row = 0; row < kRows; row++)
        for (uint8_t col = 0; col < kCols; col++)
        {
            const PadInfo p = padInfo(col, row);
            CHECK(p.slot == row / 2);
            if (p.role == PR_STEP) { steps++; CHECK(p.idx == (row & 1) * 8 + col && col < 8); }
            else { ctl++; CHECK(col >= 8 && p.role != PR_NONE); }
        }
    CHECK(steps == 32 && ctl == 16);
    CHECK(padInfo(8, 0).role == PR_STOP && padInfo(9, 0).role == PR_REV && padInfo(10, 0).role == PR_RATE && padInfo(11, 0).role == PR_RSYNC);
    CHECK(padInfo(8, 1).role == PR_GRID && padInfo(9, 1).role == PR_HOLD && padInfo(10, 1).role == PR_REC && padInfo(11, 1).role == PR_MUTE);
    CHECK(padInfo(8, 3).role == PR_GRID && padInfo(11, 2).role == PR_RSYNC && padInfo(12, 0).role == PR_NONE);
    // screen buttons: Assign 1, Assign 2, Help, then Q and SYNC ALL as before; CLIPS (8) stays the host's
    CHECK(kButtonAssign1 == 0 && kButtonAssign2 == 1 && kButtonHelp == 2 && kScreenButtonQuant == 4 && kScreenButtonSyncAll == 5);
}

// Pads address the 16 steps of the section the playhead is in: grid 1/4 and 1/8, a short last section, section advance.
static void test_mlr_engine_sections()
{
    using namespace seqcut;
    for (uint8_t g = 0; g < GRID_COUNT; g++)
    {
        const int spp = gridSteps(g), ss = sectionSteps(spp);
        Cut c;
        FakePattern f;
        f.steps = 128;
        secSetup(c, f, g);
        CHECK(c.sectionMode() && c.grid(0) == g);
        int gl = 0;
        for (int i = 0; i < ss + 3; i++) stepOnce(c, f, gl);          // the playhead is in section 1 (stock runs on)
        const View v = c.view(0);
        CHECK(v.sec == 1 && v.nsec == 128 / ss && v.pads == 16 && ((v.flags2 & V2_GRID8) != 0) == (g == GRID_8));
        c.apply(0, packArg(SUB_PAD, true, 5));
        c.apply(0, packArg(SUB_PAD, false, 5));
        StepPlan p = stepOnce(c, f, gl);
        CHECK(p.override && p.pos == ss + 5 * spp);                    // pad 5 of the section the playhead is in
        CHECK(c.view(0).col == 5 && c.view(0).sec == 1);
    }
    // a short last section: inactive pads do nothing and do not activate the track
    {
        Cut c;
        FakePattern f;
        f.steps = 70;
        secSetup(c, f, GRID_4);
        int gl = 0;
        for (int i = 0; i < 66; i++) stepOnce(c, f, gl);
        c.apply(0, packQuant(Q_STEP));
        CHECK(c.view(0).sec == 1 && c.view(0).pads == 2 && c.view(0).nsec == 2);
        c.apply(0, packArg(SUB_PAD, true, 9));                         // behind the end
        CHECK(c.playhead(0).heldCount() == 0);
        c.apply(0, packArg(SUB_PAD, false, 9));
        c.apply(0, packArg(SUB_PAD, true, 1));                         // step 68
        c.apply(0, packArg(SUB_PAD, false, 1));
        StepPlan p = stepOnce(c, f, gl);
        CHECK(p.override && p.pos == 68);
        // a two-pad loop across the end of the pattern is clipped to the pattern
        c.apply(0, packArg(SUB_PAD, true, 0));
        c.apply(0, packArg(SUB_PAD, true, 1));
        c.apply(0, packArg(SUB_PAD, false, 0));
        c.apply(0, packArg(SUB_PAD, false, 1));
        p = stepOnce(c, f, gl);
        CHECK(c.view(0).loopLo == 0 && c.view(0).loopHi == 1);
        for (int i = 0; i < 20; i++) { p = stepOnce(c, f, gl); CHECK(p.override && p.pos >= 64 && p.pos <= 69); }
        CHECK(c.view(0).sec == 1);                                     // a loop keeps the playhead in its section
        // an inactive pad press on a track that was not driven leaves it alone
        c.apply(5, packArg(SUB_PAD, true, 15));
        CHECK((c.activeMask() & (1u << 5)) == 0);
    }
    // resolution switch: ends a hold and a latched loop, the pads change meaning at once
    {
        Cut c;
        FakePattern f;
        f.steps = 128;
        secSetup(c, f, GRID_4);
        int gl = 0;
        for (int i = 0; i < 70; i++) stepOnce(c, f, gl);
        c.apply(0, packArg(SUB_PAD, true, 2));
        c.apply(0, packArg(SUB_PAD, true, 4));
        c.apply(0, packArg(SUB_PAD, false, 2));
        c.apply(0, packArg(SUB_PAD, false, 4));
        stepOnce(c, f, gl);
        CHECK(c.playhead(0).looping() && c.view(0).loopLo == 2 && c.view(0).loopHi == 4);
        c.apply(0, packGrid(GRID_8));
        CHECK(!c.playhead(0).looping() && (c.view(0).flags2 & V2_GRID8) && c.view(0).nsec == 4);
        c.apply(0, packArg(SUB_PAD, true, 3));
        c.apply(0, packArg(SUB_PAD, false, 3));
        StepPlan p = stepOnce(c, f, gl);
        const int sec = c.view(0).sec;
        CHECK(p.override && p.pos == sec * 32 + 6);
    }
    // section HOLD: the pads stay on the held section while the playhead moves on; FOLLOW lets go
    {
        Cut c;
        FakePattern f;
        f.steps = 128;
        secSetup(c, f, GRID_8);
        int gl = 0;
        for (int i = 0; i < 40; i++) stepOnce(c, f, gl);               // section 1 (32..63)
        c.apply(0, packHold(true));
        CHECK((c.view(0).flags2 & V2_HOLD) && c.holdSection(0) == 1);
        for (int i = 0; i < 40; i++) stepOnce(c, f, gl);               // the playhead is in section 2 now
        CHECK(c.view(0).sec == 1 && c.view(0).col == 0xFF);           // shown: the held section, the playhead is elsewhere
        c.apply(0, packArg(SUB_PAD, true, 2));
        c.apply(0, packArg(SUB_PAD, false, 2));
        StepPlan p = stepOnce(c, f, gl);
        CHECK(p.override && p.pos == 32 + 4 && c.view(0).col == 2);
        c.apply(0, packHold(false));
        CHECK(!(c.view(0).flags2 & V2_HOLD) && c.holdSection(0) == -1);
        // the hold of a track the playhead does not drive survives SYNC ALL (stock tracks show the held section too)
        c.apply(0, packHold(true));
        c.apply(0, packSyncAll());
        CHECK(c.holdSection(0) >= 0);
        c.apply(0, packHold(false));
    }
    // RESET (project load) puts grid and hold back to the defaults
    {
        Cut c;
        FakePattern f;
        secSetup(c, f, GRID_8);
        c.apply(0, packHold(true));
        c.apply(0, packArg(SUB_RESET, false, 0));
        CHECK(c.grid(0) == GRID_4 && c.holdSection(0) == -1);
    }
    // legacy layout (section mode off) still answers with the old 8-column views
    {
        Cut c;
        FakePattern f;
        install(c, f);
        c.apply(0, packArg(SUB_PAD, true, 3));
        c.apply(0, packArg(SUB_PAD, true, 8));                         // column 8 is no position in the legacy layout
        CHECK(c.playhead(0).heldCount() == 1 && c.view(0).pads == 0 && c.view(0).flags2 == 0);
    }
}

static void test_mlr_leds()
{
    using namespace seqcut;
    SlotLeds s[kSlots];
    memset(s, 0, sizeof(s));
    for (int k = 0; k < kSlots; k++)
    {
        s[k].track = (uint8_t)(k * 3);
        s[k].v.col = 0xFF;
        s[k].v.loopLo = 1;
        s[k].v.loopHi = 0;
        s[k].v.flags = (uint8_t)(1 << VF_RATE_SHIFT);
        s[k].v.pads = 16;
    }
    s[0].v.col = 9;                                      // slot A: playhead on pad 10 (row 2 of the slot)
    s[0].v.loopLo = 2;
    s[0].v.loopHi = 4;
    s[1].v.pads = 5;                                     // slot B: short section
    s[1].v.col = 2;
    s[1].v.flags |= VF_REVERSE | VF_ACTIVE;
    s[1].v.flags2 = V2_GRID8 | V2_HOLD;
    s[1].muted = 1;
    uint8_t out[kCutPads];
    fillMlrLeds(s, false, out);
    CHECK(out[12 + 1] == LED_BRIGHT && out[0] == LED_DIM && out[2] == LED_LOOP && out[4] == LED_LOOP && out[5] == LED_DIM);   // slot A rows 0, 1
    CHECK(out[2 * 12 + 2] == LED_BRIGHT && out[2 * 12 + 4] == LED_DIM && out[2 * 12 + 5] == LED_OFF && out[3 * 12 + 0] == LED_OFF);  // slot B: pads 6.. dark
    CHECK(out[COL_REVERSE] == LED_DIM && out[2 * 12 + COL_REVERSE] == LED_BRIGHT);                                         // REV
    CHECK(out[12 + 8] == LED_DIM && out[3 * 12 + 8] == LED_BRIGHT && out[3 * 12 + 9] == LED_BRIGHT);                       // GRID (slot B 1/8), HOLD
    CHECK(out[3 * 12 + 11] == LED_BRIGHT && out[12 + 11] == LED_DIM);                                                      // MUTE
    // the track picker: the 16 pads are tracks; lit = has notes, bright = the assigned track
    s[0].picking = 1;
    s[0].track = 6;
    s[0].has = 0x00FF;
    fillMlrLeds(s, false, out);
    CHECK(out[6] == LED_BRIGHT && out[0] == LED_MID && out[7] == LED_MID && out[12 + 0] == LED_OFF && out[12 + 7] == LED_OFF);   // tracks 1-8 lit, 9-16 off
    CHECK(out[2 * 12 + 2] == LED_BRIGHT);                                              // the other slot keeps meshing
    for (int i = 0; i < kCutPads; i++) CHECK(out[i] <= 15);
    // stopped: pads dark, STOP bright; recording blinks RSYNC
    s[0].picking = 0;
    s[0].v.flags |= VF_STOPPED | VF_REC;
    fillMlrLeds(s, true, out);
    CHECK(out[3] == LED_OFF && out[COL_STOP] == LED_BRIGHT && out[COL_RESYNC] == LED_BRIGHT);
    fillMlrLeds(s, false, out);
    CHECK(out[COL_RESYNC] == LED_OFF);
}

static void test_mlr_legend_words()
{
    using namespace seqcut;
    SlotLeds sl;
    memset(&sl, 0, sizeof(sl));
    sl.v.pads = 10;
    sl.v.col = 3;
    sl.v.flags = (uint8_t)(1 << VF_RATE_SHIFT);
    for (uint8_t row = 0; row < kRows; row++)
        for (uint8_t col = 0; col < kCols; col++)
        {
            const LegendCell c = legendCell(col, row, sl);
            CHECK(c.word[0] != 0 && strlen(c.word) <= 6);                   // real words, at most 6 characters
        }
    CHECK(strcmp(legendCell(0, 0, sl).word, "1") == 0 && strcmp(legendCell(7, 1, sl).word, "16") == 0 && legendCell(2, 1, sl).dim && !legendCell(2, 0, sl).dim);
    CHECK(legendCell(3, 0, sl).on && !legendCell(4, 0, sl).on);             // the playhead pad is highlighted
    CHECK(strcmp(legendCell(8, 0, sl).word, "STOP") == 0 && strcmp(legendCell(9, 0, sl).word, "REV") == 0 && strcmp(legendCell(10, 0, sl).word, "RATE") == 0 &&
          strcmp(legendCell(11, 0, sl).word, "RSYNC") == 0);
    CHECK(strcmp(legendCell(8, 1, sl).word, "GRID/4") == 0 && strcmp(legendCell(9, 1, sl).word, "FOLLOW") == 0 && strcmp(legendCell(10, 1, sl).word, "REC") == 0 &&
          strcmp(legendCell(11, 1, sl).word, "MUTE") == 0);
    // states highlighted
    sl.v.flags2 = V2_GRID8 | V2_HOLD;
    sl.v.flags |= VF_REVERSE | VF_STOPPED;
    sl.v.flags = (uint8_t)((sl.v.flags & ~(3 << VF_RATE_SHIFT)) | (2 << VF_RATE_SHIFT));
    sl.rec = 1;
    sl.muted = 1;
    CHECK(legendCell(8, 1, sl).on && strcmp(legendCell(8, 1, sl).word, "GRID/8") == 0 && legendCell(9, 1, sl).on && strcmp(legendCell(9, 1, sl).word, "HOLD") == 0);
    CHECK(legendCell(10, 1, sl).on && legendCell(11, 1, sl).on && legendCell(9, 0, sl).on && legendCell(10, 0, sl).on && strcmp(legendCell(10, 0, sl).word, "RATEx2") == 0);
    CHECK(legendCell(8, 0, sl).on && strcmp(legendCell(8, 0, sl).word, "START") == 0);
    // the picker: T1..T16, the assigned track highlighted, tracks without notes dim
    sl.picking = 1;
    sl.track = 4;
    sl.has = 0x0010 | 0x0001;
    CHECK(strcmp(legendCell(0, 0, sl).word, "T1") == 0 && strcmp(legendCell(7, 1, sl).word, "T16") == 0);
    CHECK(legendCell(4, 0, sl).on && !legendCell(0, 0, sl).on && !legendCell(0, 0, sl).dim && legendCell(1, 0, sl).dim);
}

// ---- the cue cache bookkeeping (pure parts) ----
namespace
{
struct CueWorld
{
    uint32_t hash = 7;
    uint32_t base = 40000;   // frames per step is 1000 from here
    bool streamed = true;
    int calls = 0;
};
bool cueFn(void* u, int pos, uint8_t& instr, uint32_t& frame, uint32_t& hash, uint8_t& stereo)
{
    CueWorld* w = (CueWorld*)u;
    w->calls++;
    if (pos % 17 == 16) return false; // a step without a sounding note
    instr = 3;
    frame = w->base + (uint32_t)pos * 1000;
    hash = w->hash;
    stereo = 1;
    return w->streamed;
}
} // namespace

static void test_cue_cache_logic()
{
    using namespace seqcue;
    CueWorld w;
    Want wa[kCues], wb[kCues];
    // wants: 16 pads of section 0 at grid 1/4 (4 steps per pad), frames below `streamedFrom` need no cue, silent steps none
    computeWants(cueFn, &w, 0, 4, 100, 0, wa);
    for (int k = 0; k < kCues; k++) CHECK(wa[k].on == (k * 4 % 17 != 16 ? 1 : 0) && (!wa[k].on || (wa[k].frame == 40000u + k * 4000u && wa[k].instr == 3 && wa[k].stereo)));
    computeWants(cueFn, &w, 0, 4, 100, 50000, wb);
    CHECK(!wb[0].on && !wb[2].on && wb[3].on);                         // frame 40000, 48000 are inside the pre-cache; 52000 is not
    computeWants(cueFn, &w, 1, 4, 70, 0, wb);                          // section 1 of a 70 step pattern: steps 64..69 -> pads 0, 1
    CHECK(wb[0].on && wb[1].on && !wb[2].on && wb[0].frame == 40000u + 64u * 1000u);
    w.streamed = false;
    computeWants(cueFn, &w, 0, 4, 100, 0, wb);
    for (int k = 0; k < kCues; k++) CHECK(!wb[k].on);                  // resident sample: nothing to cache
    w.streamed = true;

    Cache c;
    c.reset();
    c.want(0, 0, wa);
    const uint8_t cur[kSlots] = { 0, 0 }, noPre[kSlots] = { 0, 0 }, from[kSlots] = { 0, 0 };
    int idx, ch, loads = 0, wanted = 0;
    for (int k = 0; k < kCues; k++) wanted += wa[k].on;
    while (c.next(cur, noPre, from, idx, ch))
    {
        CHECK(idx >= Cache::index(0, 0, 0) && idx < Cache::index(0, 1, 0));    // slot 0, bank 0 only (no prefetch, slot 1 wants nothing)
        c.loaded(idx, ch);
        loads++;
        CHECK(loads <= 40);
    }
    CHECK(loads == wanted * 2);                                         // stereo: two channel reads per cue
    CHECK(c.readyMask(0, 0) != 0 && c.pending(0, 0) == 0);
    // find: the start of a cue and anywhere inside its window; not outside, not for another instrument
    int fi;
    uint32_t delta;
    CHECK(c.find(3, 40000u + 4000u, fi, delta) && delta == 0 && fi == Cache::index(0, 0, 1));
    CHECK(c.find(3, 40000u + 4000u + kFrames - 1, fi, delta) && delta == (uint32_t)kFrames - 1);
    CHECK(!c.find(3, 40000u + 4000u + kFrames, fi, delta) && !c.find(2, 44000u, fi, delta) && !c.find(3, 43999u, fi, delta));
    // a cue that is not loaded yet is a miss (behave as before)
    c.want(0, 0, wa);
    CHECK(c.find(3, 44000u, fi, delta));                                // unchanged wants keep their data
    w.hash = 8;                                                         // the sample changed: everything stale
    computeWants(cueFn, &w, 0, 4, 100, 0, wb);
    c.want(0, 0, wb);
    CHECK(!c.find(3, 44000u, fi, delta) && c.readyMask(0, 0) == 0 && c.pending(0, 0) == wanted);
    w.hash = 7;
    // only the cues whose frame changed are loaded again (loop region / tempo move some of them)
    c.want(0, 0, wa);
    while (c.next(cur, noPre, from, idx, ch)) c.loaded(idx, ch);
    Want moved[kCues];
    memcpy(moved, wa, sizeof(moved));
    moved[1].frame += 123;
    c.want(0, 0, moved);
    CHECK(c.pending(0, 0) == 1 && !c.find(3, 44000u, fi, delta) && c.find(3, 40000u + 8000u, fi, delta));
    // order: the cues after the playhead pad first; the next section only when prefetching
    c.reset();
    c.want(0, 0, wa);
    computeWants(cueFn, &w, 1, 4, 128, 0, wb);
    c.want(0, 1, wb);
    const uint8_t from9[kSlots] = { 9, 0 }, pre1[kSlots] = { 1, 0 };
    CHECK(c.next(cur, noPre, from9, idx, ch) && idx == Cache::index(0, 0, 10));
    for (int guard = 0; guard < 64 && c.next(cur, noPre, from9, idx, ch); guard++) c.loaded(idx, ch);
    CHECK(c.pending(0, 0) == 0 && c.pending(0, 1) > 0 && !c.next(cur, noPre, from9, idx, ch));    // next section waits ...
    CHECK(c.next(cur, pre1, from9, idx, ch) && idx >= Cache::index(0, 1, 0) && idx < Cache::index(1, 0, 0));  // ... until the playhead nears the end
    // bank swap at the section change: the prefetched bank is the current one and its cues are found without any new read
    for (int guard = 0; guard < 64 && c.next(cur, pre1, from9, idx, ch); guard++) c.loaded(idx, ch);
    const uint8_t cur1[kSlots] = { 1, 0 };
    CHECK(!c.next(cur1, noPre, from, idx, ch) && c.find(3, wb[1].frame, fi, delta) && fi == Cache::index(0, 1, 1));
    // both slots are cached independently
    c.want(1, 0, wa);
    CHECK(c.next(cur1, noPre, from, idx, ch) && idx >= Cache::index(1, 0, 0) && idx < Cache::index(1, 1, 0));
    CHECK(kEntries == 64 && kFrames == 3072 && (unsigned)kEntries * kChannels * kFrames * 2 == 786432u);
}

static void test_cue_latency_model()
{
    using namespace seqcue;
    // press-to-sound of a jump in a streamed loop (quantise off: the next 1 ms engine tick; the grid wait is the same with and without)
    LatencyIn l;
    l.tickWaitMs = 1.0f;
    l.mainLoopMs = 8.0f;     // worst gap between two VoiceStreamManager::update() passes (cooperative main loop)
    l.sdFirstChunkMs = 6.0f; // seek + 8 KB ADMA read
    l.blockMs = 128.0f / 44.1f;
    const float before = latencyUncachedMs(l), after = latencyCachedMs(l);
    printf("  jump latency, streamed loop (host model): before %.1f ms, after (cache hit) %.1f ms\n", before, after);
    CHECK(before > 17.0f && before < 20.0f && after < 4.0f && after < before / 4);
    // the window outlasts the stream at 4x playback speed, and not at absurd speeds
    CHECK(windowCoversStream(l, 1.0f, 44100.0f) && windowCoversStream(l, 4.0f, 44100.0f) && !windowCoversStream(l, 8.0f, 44100.0f));
    LatencyIn slow = l;
    slow.sdFirstChunkMs = 60.0f; // a slow card
    CHECK(windowCoversStream(slow, 1.0f, 44100.0f) && !windowCoversStream(slow, 2.0f, 44100.0f));
}

// ---- the slots through the layer: assign, picker, extras, buttons, legend ----
static void test_mlr_slots_layer()
{
    using namespace seqcut;
    FakePattern f;
    f.steps = 128;
    f.map[0] = 1;
    seqcut::Cut* c = freshCut(f);
    CHECK(c != 0);
    if (!c)
        return;
    cutAssignSlot(0, 0);
    cutAssignSlot(1, 1);
    seqsession::CutSessionLayer a;
    a.enter(0);
    CHECK(a.groupSize() == 4 && strcmp(a.name(), "CUT") == 0);
    // defaults A = track 1, B = track 2; a track belongs to one slot (taking the other slot's track swaps)
    CHECK(cutSlotTrack(0) == 0 && cutSlotTrack(1) == 1);
    cutAssignSlot(0, 1);
    CHECK(cutSlotTrack(0) == 1 && cutSlotTrack(1) == 0);
    cutAssignSlot(0, 7);
    cutAssignSlot(1, 3);
    CHECK(cutSlotTrack(0) == 7 && cutSlotTrack(1) == 3);
    cutAssignSlot(0, 40);                                            // out of range: ignored
    CHECK(cutSlotTrack(0) == 7);

    // pads: slot A (rows 0-1) plays track 8, slot B (rows 2-3) track 4; step 9..16 are the second row
    seqEngine.postCut(7, packGrid(GRID_4));
    seqEngine.postCut(3, packGrid(GRID_4));
    cutQuantSet(Q_STEP);
    cutUpdate();
    seqEngine.idleTick();
    a.pad(2, 1, true);                                               // slot A, second row, col 3 = step 11 -> position 40
    a.pad(2, 1, false);
    a.pad(5, 2, true);                                               // slot B, first row, col 6 = step 6
    a.pad(5, 2, false);
    seqEngine.idleTick();
    CHECK(c->activeMask() == ((1u << 7) | (1u << 3)));
    c->setStock(7, 5);
    StepPlan p = c->boundary(7, 128, 6);
    CHECK(p.override && p.pos == 10 * 4);
    c->setStock(3, 5);
    p = c->boundary(3, 128, 6);
    CHECK(p.override && p.pos == 5 * 4);

    // the picker: "Assign 1" turns the rows 1-2 into a track picker, the other slot keeps meshing; again = cancel
    char lab[12];
    CHECK(a.buttonLabel(kButtonAssign1, lab) && strcmp(lab, "Assign 1") == 0 && a.buttonLabel(kButtonAssign2, lab) && strcmp(lab, "Assign 2") == 0);
    CHECK(a.buttonLabel(kButtonHelp, lab) && strcmp(lab, "Help") == 0 && a.buttonLabel(3, lab) && strcmp(lab, "BeatRpt") == 0); // plan 021: the free screen button 4 is BeatRpt now (was: empty)
    CHECK(a.button(kButtonAssign1, true) && a.button(kButtonAssign1, false) && cutPickSlot() == 0);
    CHECK(a.buttonLabel(kButtonAssign1, lab) && strcmp(lab, "[Assign 1]") == 0);
    uint8_t leds[kCutPads];
    cutLeds(leds);
    CHECK(leds[7] == LED_BRIGHT && leds[0] == LED_MID && leds[12 + 7] == LED_OFF);   // track 8 is the assigned one; tracks 1-8 have notes (stub), 9-16 not
    a.pad(5, 2, true);                                               // slot B keeps working while slot A picks
    a.pad(5, 2, false);
    CHECK(cutPickSlot() == 0 && cutSlotTrack(1) == 3);
    a.pad(2, 0, true);                                               // pad 3 = track 3
    a.pad(2, 0, false);
    CHECK(cutPickSlot() == -1 && cutSlotTrack(0) == 2);
    seqEngine.idleTick();
    CHECK(((c->activeMask() >> 2) & 1u) == 0);                       // picking pressed no step
    CHECK(a.button(kButtonAssign2, true) && cutPickSlot() == 1);
    CHECK(a.button(kButtonAssign2, true) && cutPickSlot() == -1);    // again = cancel
    CHECK(a.button(kButtonAssign2, true) && cutPickSlot() == 1);
    a.pad(1, 3, true);                                               // second row of slot B: pad 10 of the picker = track 10
    a.pad(1, 3, false);
    CHECK(cutPickSlot() == -1 && cutSlotTrack(1) == 9);
    CHECK(a.button(kButtonAssign1, true) && a.button(kButtonAssign2, true) && cutPickSlot() == 1);   // one picker at a time
    a.button(kButtonAssign2, true);

    // extras of slot A (track 3, rows 0-1): second row = GRID, SECT, REC, MUTE
    cutAssignSlot(0, 2);
    seqEngine.idleTick();
    a.pad(8, 1, true);
    a.pad(8, 1, false);
    seqEngine.idleTick();
    CHECK(c->grid(2) == GRID_8 && (c->view(2).flags2 & V2_GRID8));
    a.pad(8, 1, true);
    a.pad(8, 1, false);
    seqEngine.idleTick();
    CHECK(c->grid(2) == GRID_4);
    a.pad(9, 1, true);
    a.pad(9, 1, false);
    seqEngine.idleTick();
    CHECK(c->holdSection(2) >= 0 && (c->view(2).flags2 & V2_HOLD));
    a.pad(9, 1, true);
    a.pad(9, 1, false);
    seqEngine.idleTick();
    CHECK(c->holdSection(2) == -1);
    CHECK(!seqCutTrackMuted(2));
    a.pad(11, 1, true);
    a.pad(11, 1, false);
    CHECK(seqCutTrackMuted(2));
    a.pad(11, 1, true);
    a.pad(11, 1, false);
    CHECK(!seqCutTrackMuted(2));
    cutRec(false);                                                   // a take of this slot only: REC pad = start, again = close
    a.pad(10, 1, true);
    a.pad(10, 1, false);
    CHECK(cutTrackRecArmed(2));
    a.pad(10, 1, true);
    a.pad(10, 1, false);
    CHECK(!cutTrackRecArmed(2));
    // first row extras are the stock ones; the second row of the extras of a slot belongs to its own track only
    a.pad(9, 0, true);
    a.pad(9, 0, false);
    seqEngine.idleTick();
    CHECK(c->view(2).flags & VF_REVERSE);

    // Help toggles the legend, the pads keep working under it
    CHECK(!cutLegendShown() && a.button(kButtonHelp, true) && cutLegendShown());
    CHECK(a.buttonLabel(kButtonHelp, lab) && strcmp(lab, "[Help]") == 0);
    a.pad(4, 0, true);
    a.pad(4, 0, false);
    seqEngine.idleTick();
    CHECK(cutLegendShown() && c->playhead(2).heldCount() == 0);
    CHECK(a.button(kButtonHelp, true) && !cutLegendShown());
    // Q, SYNC ALL and CLIPS keep their places; the layer swallows Left / Right, no groups
    CHECK(a.buttonLabel(kScreenButtonQuant, lab) && lab[0] == 'Q' && a.buttonLabel(kScreenButtonSyncAll, lab));
    char lines[4][48];
    a.info(lines);
    CHECK(strstr(lines[0], "A: track 3") != 0);
    a.leave();
    seqEngine.resetState();
    seqEngine.idleTick();
    cutAssignSlot(0, 0);
    cutAssignSlot(1, 1);
}

// ---- the lane screen: budget, bounds, preview ----
namespace
{
seqcut::MlrModel mlrScene(int scene)
{
    using namespace seqcut;
    MlrModel m;
    memset(&m, 0, sizeof(m));
    m.pickSlot = 0xFF;
    m.legendPad[0] = m.legendPad[1] = 0xFF;
    for (int k = 0; k < kSlots; k++)
    {
        SlotInfo& s = m.slot[k];
        s.track = (uint8_t)(k * 6 + 1);
        s.flags = (uint8_t)(VF_ACTIVE | (1 << VF_RATE_SHIFT));
        s.spp = 4;
        s.sec = (uint8_t)(2 + k);
        s.nsec = 8;
        s.pads = 16;
        s.loopLo = 1;
        s.loopHi = 0;
        s.noteMask = (uint16_t)(0x5A5A >> k);
        s.cacheMask = (uint16_t)(0x0FFF >> (k * 3));
        s.waveOk = 1;
        for (int j = 0; j < kWaveCols; j++)
        {
            int amp = (int)(60 + 50 * ((j * (7 + k)) % 23) / 23) - (j % 16 == 0 ? 40 : 0);
            s.wave[j].hi = (int8_t)amp;
            s.wave[j].lo = (int8_t)-amp;
        }
    }
    if (scene == 1)                  // everything on: loop, rec, stopped, reverse, grid 1/8, hold, short section, mute, rec header
    {
        m.recHeld = 1;
        for (int k = 0; k < kSlots; k++)
        {
            SlotInfo& s = m.slot[k];
            s.flags = (uint8_t)(VF_ACTIVE | VF_STOPPED | VF_REVERSE | VF_REC | VF_RECORDED | (2 << VF_RATE_SHIFT));
            s.flags2 = V2_GRID8 | V2_HOLD;
            s.spp = 2;
            s.loopLo = 2;
            s.loopHi = 9;
            s.pads = (uint8_t)(k ? 11 : 16);
            s.muted = 1;
            s.rec = 1;
            s.noteMask = 0xFFFF;
            s.cacheMask = 0xFFFF;
            s.track = 15;
        }
    }
    if (scene == 2)                  // slot A is the track picker
    {
        m.pickSlot = 0;
        m.slot[0].picking = 1;
        m.slot[0].has = 0x5F5F;
        m.slot[0].track = 6;
        m.slot[0].waveOk = 0;
    }
    if (scene == 3)                  // the legend
    {
        m.legend = 1;
        m.legendPad[0] = 4;
        m.legendPad[1] = 11;
        m.slot[0].flags2 = V2_GRID8;
        m.slot[1].flags2 = V2_HOLD;
        m.slot[1].muted = 1;
        m.slot[1].rec = 1;
        m.slot[0].flags = (uint8_t)(VF_ACTIVE | VF_REVERSE | (2 << VF_RATE_SHIFT));
    }
    return m;
}
} // namespace

static void test_mlr_screen_budget_and_bounds()
{
    using namespace seqcut;
    CHECK(kMlrLaneX + kMlrLaneW < kMlrBlockX && kMlrBlockX + 2 * 49 <= 800 && kMlrLaneW == 16 * kMlrPadW && kMlrPadW * 4 == 152 && kWaveCols == 152);
    CHECK(kMlrLaneTop >= kHeaderY1 + 4 && kMlrLaneStride >= kMlrLaneH + 2 && kMlrLaneTop + kMlrLaneStride + kMlrLaneH <= 424);
    int worst = 0, worstHeads = 0;
    for (int scene = 0; scene < 4; scene++)
    {
        const MlrModel m = mlrScene(scene);
        CountCtx w;
        drawMlr(w, m);
        w.finish();
        MlrHeadModel hm;
        memset(&hm, 0, sizeof(hm));
        for (int k = 0; k < kSlots; k++)
        {
            hm.h[k].x = (int16_t)(kMlrLaneW - 1);
            hm.h[k].ghost = (int16_t)(kMlrLaneW - 1);
            hm.h[k].pad = 15;
            hm.h[k].flags = VF_ACTIVE;
        }
        hm.bar = 8;
        hm.beat = 4;
        hm.legend = m.legend;
        CountCtx hw;
        drawMlrHeads(hw, hm);
        hw.finish();
        printf("  MLR screen scene %d: %d bytes static (%d rects, %d discs, %d texts), %d bytes playheads\n", scene, w.bytes(), w.rects, w.discs, w.texts, hw.bytes());
        if (!(w.minY >= 30 && w.maxY <= 424 && w.minX >= 0 && w.maxX <= 800)) printf("  bounds scene %d: x %d..%d y %d..%d\n", scene, w.minX, w.maxX, w.minY, w.maxY);
        CHECK(w.minY >= 30 && w.maxY <= 424 && w.minX >= 0 && w.maxX <= 800);
        CHECK(hw.minY >= 30 && hw.maxY <= 424 && hw.minX >= 0 && hw.maxX <= 800);
        if (w.bytes() > worst) worst = w.bytes();
        printf("  MLR screen scene %d: command FIFO %d bytes static, %d playheads (one stage each, FIFO 4096)\n", scene, w.fifo(), hw.fifo());
        CHECK(hw.fifo() <= 3840); // v0.3.9: one display stage is one burst into the 4 KB FT81x command FIFO
        {   // v0.3.9: the lanes control draws in two stages (seqTraceStages.h BatchSplit): each stage fits the FIFO, together the whole screen
            CountCtx s0, s1;
            tracestage::BatchSplit<CountCtx> c0(s0, 0), c1(s1, 1);
            drawMlr(c0, m);
            c0.finish();
            s0.finish();
            drawMlr(c1, m);
            c1.finish();
            s1.finish();
            const int f0 = s0.fifo() + 4 + 4 + (s1.rects + s1.discs ? 8 : 0), f1 = s1.fifo() + 4 + 8 + 4; // + CMD_DLSTART, SAVE / RESTORE, reopened batch
            printf("  MLR screen scene %d: stages %d + %d FIFO bytes (dropped %d)\n", scene, f0, f1, c0.dropped);
            CHECK(f0 <= 3840 && f1 <= 3840 && c0.dropped == 0 && c1.dropped == 0);
            CHECK(s0.rects + s1.rects == w.rects && s0.discs + s1.discs == w.discs && s0.texts + s1.texts == w.texts);
            CHECK(c0.used[0] == c1.used[0] && c0.used[1] == c1.used[1]); // both stages cut at the same command
        }
        if (hw.bytes() > worstHeads) worstHeads = hw.bytes();
    }
    CHECK(worst <= 5400 && worstHeads <= 500 && worst + worstHeads <= 5800);   // the whole frame must stay below 8172 bytes of display list
    // the legend replaces the lanes and is the cheapest of all; the playhead part is empty then (bar.beat only)
    CountCtx lg;
    drawMlr(lg, mlrScene(3));
    lg.finish();
    CountCtx ln;
    drawMlr(ln, mlrScene(0));
    ln.finish();
    CHECK(lg.bytes() < ln.bytes());
    // the head at the end of a lane stays on the screen; a head outside the shown section is not drawn
    MlrHeadModel gone;
    memset(&gone, 0, sizeof(gone));
    gone.h[0].x = -1;
    gone.h[1].x = -1;
    CountCtx gc;
    drawMlrHeads(gc, gone);
    gc.finish();
    CHECK(gc.rects == 0);
    // head model from a view: pad, ghost
    View v = {};
    v.flags = VF_ACTIVE;
    v.flags2 = 0;
    v.sec = 1;
    v.pads = 16;
    MlrHead h = makeMlrHead(v, 70, 1, 70);
    CHECK(h.pad == 1 && h.x == mlrOffset(70, 0, 1, 4) && h.ghost == -1);
    h = makeMlrHead(v, 70, 24, 66);
    CHECK(h.ghost == mlrOffset(66, 0, 1, 4) && h.ghost != h.x && h.x > 1 * kMlrPadW && h.x < 2 * kMlrPadW);
    h = makeMlrHead(v, 10, 1, 10);                                  // the playhead is in another section
    CHECK(h.x == -1 && h.pad == 0xFF);
    v.flags2 = V2_GRID8;
    h = makeMlrHead(v, 40, 1, 40);                                  // 1/8: section 1 starts at step 32, 2 steps per pad
    CHECK(h.pad == 4 && h.x == 4 * kMlrPadW);
    CHECK(mlrOffset(127, 47, 3, 2) < kMlrLaneW && mlrOffset(127, 47, 3, 2) >= 15 * kMlrPadW);
    // waveform helpers
    WaveCol col;
    int16_t smp[4] = { -300, 12000, 32767, -32768 };
    waveFold(col, smp, 4, 1);
    CHECK(col.lo == -127 && col.hi == 127);
    waveFold(col, smp, 0, 1);
    CHECK(col.lo == 0 && col.hi == 0);
    CHECK(waveColStep(0, 64) == 0 && waveColStep(kWaveCols - 1, 64) < 64 && waveColStep(kWaveCols, 64) == 64);

    if (const char* dir = getenv("SEQ_LANE_PREVIEW")) // `make preview`: dump the draw ops of the scenes for lane_preview.py
    {
        char path[512];
        for (int scene = 0; scene < 4; scene++)
        {
            snprintf(path, sizeof(path), "%s/mlr%d.ops", dir, scene);
            FILE* fp = fopen(path, "w");
            if (!fp) continue;
            const MlrModel m = mlrScene(scene);
            MlrHeadModel h;
            memset(&h, 0, sizeof(h));
            for (int k = 0; k < kSlots; k++)
            {
                View vv = {};
                vv.flags = m.slot[k].flags;
                vv.flags2 = m.slot[k].flags2;
                vv.sec = m.slot[k].sec;
                h.h[k] = makeMlrHead(vv, m.slot[k].sec * sectionSteps(m.slot[k].spp) + 6 + k * 21, 20, m.slot[k].sec * sectionSteps(m.slot[k].spp) + 2 + k * 9);
            }
            h.bar = 3;
            h.beat = 2;
            h.recHeld = m.recHeld;
            h.legend = m.legend;
            CountCtx d;
            d.dump = fp;
            drawMlr(d, m);
            drawMlrHeads(d, h);
            d.finish();
            fclose(fp);
        }
    }
}

// ---- MLR section mapping (owner device report: the screen scrolled to the next section, the pads still hit the first one).
// Sweep 1: first gesture of a track, all quantise modes, both grids, 128 / 192 steps, any playhead position: the jump lands on the pad of the
// section the screen (view().sec) showed at the press. Sweep 2: a track played before, SYNC ALL / RSYNC in between. Sweep 3: the press right
// after SYNC ALL / RSYNC, before the next step boundary (the screen shows the stock position; the playhead used to be aligned only at the
// boundary: stale section, the press was dropped). A STOPped track keeps its playhead frozen on purpose and is not swept.
static void test_mlr_section_sweep()
{
    using namespace seqcut;
    int bad = 0, total = 0;
    for (uint8_t q = Q_OFF; q < Q_COUNT; q++)
    for (uint8_t g = 0; g < GRID_COUNT; g++)
    for (int steps : { 128, 192 })
    for (int startPos = 0; startPos < steps; startPos += 7)
    for (int k = 0; k < 16; k += 5)
    {
        const int spp = gridSteps(g), ss = sectionSteps(spp);
        Cut c;
        FakePattern f;
        f.steps = steps;
        f.map[0] = 1;
        install(c, f);
        c.setSectionMode(true);
        c.apply(0, packQuant(q));
        c.apply(0, packGrid(g));
        int gl = 0;
        for (int i = 0; i < startPos; i++) stepOnce(c, f, gl);
        const View v = c.view(0);
        if (k >= v.pads) continue;
        c.apply(0, packArg(SUB_PAD, true, (uint8_t)k));
        c.apply(0, packArg(SUB_PAD, false, (uint8_t)k));
        int landed = -1;
        for (int i = 0; i < 40 && landed < 0; i++)
        {
            StepPlan p = stepOnce(c, f, gl);
            if (p.override && p.pos / ss == v.sec && p.pos % ss == k * spp) landed = p.pos;
            if (p.override && p.pos / ss != v.sec) landed = -2;
        }
        total++;
        if (landed < 0) { bad++; if (bad < 6) printf("sweep: q %d grid %d steps %d pos %d pad %d: shown sec %d, landed %d\n", q, g, steps, startPos, k, v.sec, landed); }
    }
    CHECK(total > 1000 && bad == 0);
}

static void test_mlr_section_sweep2()
{
    using namespace seqcut;
    int bad = 0, total = 0;
    for (int scen = 0; scen < 3; scen++)
    for (uint8_t q = Q_STEP; q < Q_COUNT; q++)
    for (uint8_t g = 0; g < GRID_COUNT; g++)
    for (int startPos = 70; startPos < 190; startPos += 11)
    for (int k = 0; k < 16; k += 5)
    {
        const int spp = gridSteps(g), ss = sectionSteps(spp);
        Cut c; FakePattern f; f.steps = 192; f.map[0] = 1;
        install(c, f); c.setSectionMode(true);
        c.apply(0, packQuant(q)); c.apply(0, packGrid(g));
        int gl = 0;
        // first gesture early in the pattern: the track goes off the grid in section 0
        c.apply(0, packArg(SUB_PAD, true, 2)); c.apply(0, packArg(SUB_PAD, false, 2));
        for (int i = 0; i < 12; i++) stepOnce(c, f, gl);
        if (scen == 1) c.apply(0, packSyncAll());
        if (scen == 2) c.apply(0, packArg(SUB_RESYNC, true, 0));
        for (int i = 12; i < startPos; i++) stepOnce(c, f, gl);
        const View v = c.view(0);
        if (k >= v.pads) continue;
        c.apply(0, packArg(SUB_PAD, true, (uint8_t)k)); c.apply(0, packArg(SUB_PAD, false, (uint8_t)k));
        int landed = -1;
        for (int i = 0; i < 40 && landed < 0; i++)
        {
            StepPlan p = stepOnce(c, f, gl);
            if (p.override && p.pos / ss == v.sec && p.pos % ss == k * spp) landed = p.pos;
        }
        total++;
        if (landed < 0) { bad++; if (bad < 8) printf("sweep2: scen %d q %d grid %d pos %d pad %d: shown sec %d, landed %d now sec %d\n", scen, q, g, startPos, k, v.sec, landed, c.view(0).sec); }
    }
    CHECK(total > 500 && bad == 0);
}

static void test_mlr_section_sweep3()
{
    using namespace seqcut;
    int bad = 0, total = 0;
    for (int scen = 0; scen < 2; scen++)
    for (uint8_t g = 0; g < GRID_COUNT; g++)
    for (int startPos = 70; startPos < 190; startPos += 11)
    for (int k = 0; k < 16; k += 5)
    {
        const int spp = gridSteps(g), ss = sectionSteps(spp);
        Cut c; FakePattern f; f.steps = 192; f.map[0] = 1;
        install(c, f); c.setSectionMode(true);
        c.apply(0, packQuant(Q_STEP)); c.apply(0, packGrid(g));
        int gl = 0;
        c.apply(0, packArg(SUB_PAD, true, 2)); c.apply(0, packArg(SUB_PAD, false, 2));
        for (int i = 0; i < startPos; i++) stepOnce(c, f, gl);
        if (scen == 0) c.apply(0, packSyncAll());
        else c.apply(0, packArg(SUB_RESYNC, true, 0));
        const View v = c.view(0);   // right after: before the next boundary
        if (k >= v.pads) continue;
        c.apply(0, packArg(SUB_PAD, true, (uint8_t)k)); c.apply(0, packArg(SUB_PAD, false, (uint8_t)k));
        int landed = -1;
        for (int i = 0; i < 40 && landed < 0; i++)
        {
            StepPlan p = stepOnce(c, f, gl);
            if (p.override && p.pos / ss == v.sec && p.pos % ss == k * spp) landed = p.pos;
        }
        total++;
        if (landed < 0) { bad++; if (bad < 8) printf("sweep3: scen %d grid %d pos %d pad %d: shown sec %d, landed %d\n", scen, g, startPos, k, v.sec, landed); }
    }
    CHECK(total > 100 && bad == 0);
}
