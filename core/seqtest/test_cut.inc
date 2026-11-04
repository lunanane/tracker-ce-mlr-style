// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// CUT (MLR) layer tests. Included from test_main.cpp (uses CHECK, mk, playUSteps, seqEngine).
#include "seqCut.h"
#include "cutLayer.h"
#include "cutSessionLayer.h"
#include "laneDraw.h"
#include <stdlib.h>

// Stand-ins for the firmware-only parts of the CUT screen (laneView.cpp: display controls, Shift button, Session host).
static bool g_shiftHeld = false;
static int g_groupRequests = 0;
static int g_lastGroupRequest = -1;
static int g_laneShow = 0, g_laneHide = 0;
void laneViewCreate() {}
void laneViewDestroy() {}
void laneViewShow(bool show) { if (show) g_laneShow++; else g_laneHide++; }
bool laneViewRefresh(bool, uint8_t) { return false; }
bool cutShiftHeld() { return g_shiftHeld; }
void laneViewRequestGroup(uint8_t first) { g_groupRequests++; g_lastGroupRequest = first; }
bool laneViewAnyOffGrid() { return seqEngine.cut() != 0 && seqEngine.cut()->activeMask() != 0; }

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
    CHECK(p.override && p.pos == 9); // recorded steps were 8 9 10, restarted at index 1
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
    cutSetGroup(0); // constructs the Cut through seqCutInit()
    seqcut::Cut* c = seqEngine.cut();
    CHECK(c != 0);
    if (!c)
        return;
    FakePattern f;
    f.map[0] = 1;
    install(*c, f);
    uint8_t leds[kCutPads];
    cutSetGroup(4); // tracks 4..7
    cutLeds(leds);
    CHECK(leds[0] == LED_BRIGHT && leds[1] == LED_DIM); // idle: playhead shown at column 0
    cutPad(3, 1, true);                                  // track 5, column 3
    seqEngine.idleTick();
    CHECK(c->activeMask() == (1u << 5));
    // group changes while the pad is held: the release still goes to track 5
    cutSetGroup(8);
    cutPad(3, 1, false);
    seqEngine.idleTick();
    CHECK(c->playhead(5).heldCount() == 0);
    // two pads of a row = loop on that row's track
    cutSetGroup(0);
    cutPad(1, 0, true);
    cutPad(4, 0, true);
    seqEngine.idleTick();
    CHECK(c->view(0).loopLo == 1 && c->view(0).loopHi == 4);
    cutLeds(leds);
    CHECK(leds[2] == LED_LOOP && leds[3] == LED_LOOP && leds[7] == LED_DIM);
    cutReleaseAll();
    seqEngine.idleTick();
    CHECK(c->playhead(0).heldCount() == 0);
    // controls
    cutPad(COL_REVERSE, 2, true);
    cutPad(COL_REVERSE, 2, false);
    cutPad(COL_RATE, 2, true);
    cutPad(COL_STOP, 3, true);
    seqEngine.idleTick();
    CHECK(c->view(2).flags & VF_REVERSE);
    CHECK(viewRate(c->view(2)) == 2);
    CHECK(c->view(3).flags & VF_STOPPED);
    // recording: a row starts its take with its first pad, Rec release closes all takes
    cutRec(true);
    cutPad(5, 0, true);
    cutPad(5, 0, false);
    seqEngine.idleTick();
    CHECK(c->view(0).flags & VF_REC);
    CHECK(!(c->view(1).flags & VF_REC)); // untouched rows record nothing
    int g = 0;
    f.pos = 0;
    stepOnce(*c, f, g);
    stepOnce(*c, f, g);
    cutSetGroup(4); // the take keeps running while the group changes
    cutRec(false);
    seqEngine.idleTick();
    CHECK(!(c->view(0).flags & VF_REC) && (c->view(0).flags & VF_RECORDED));
    // a release that does not fit into the queue is kept and retried
    cutSetGroup(0);
    cutReleaseAll();
    seqEngine.idleTick();
    cutPad(2, 0, true);
    seqEngine.idleTick();
    CHECK(c->playhead(0).heldCount() == 1);
    for (int i = 0; i < 70; i++)
        seqEngine.postCut(9, seqcut::packArg(seqcut::SUB_RESYNC, true, 0)); // fills the queue (inactive track: ignored)
    cutPad(2, 0, false);
    seqEngine.idleTick();
    seqEngine.idleTick();
    cutLeds(leds); // retries the lost release
    seqEngine.idleTick();
    CHECK(c->playhead(0).heldCount() == 0);
    // out of range pads are ignored
    cutPad(12, 0, true);
    cutPad(0, 4, true);
    cutSetGroup(0);
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
        a.setGroup(starts[gi]);
        for (uint8_t row = 0; row < 4; row++)
        {
            a.pad(11, row, true); // the last of the 12 pad columns (RESYNC) is the layer's
            a.pad(11, row, false);
        }
        a.pad(3, 2, true); // track starts[gi] + 2, position column 3
        seqEngine.idleTick();
        CHECK(c->activeMask() == (1u << (starts[gi] + 2)));
        a.pad(3, 2, false);
        seqEngine.idleTick();
    }
    // an 8-track group start rounds down to a group of 4
    a.setGroup(8);
    a.pad(0, 1, true);
    a.pad(0, 1, false);
    seqEngine.idleTick();
    CHECK(((c->activeMask() >> 9) & 1u) != 0);
    // leaving lets go of held pads
    a.pad(2, 0, true);
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
    cutSetGroup(0); // constructs the Cut through seqCutInit()
    seqcut::Cut* c = seqEngine.cut();
    if (c)
        install(*c, f);
    seqEngine.resetState();
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
        cutPad(2, row, true);
        cutPad(2, row, false);
    }
    cutSetGroup(8);
    cutPad(4, 2, true);
    cutPad(4, 2, false);
    cutSetGroup(0);
    cutRec(true);
    cutPad(6, 0, true);
    cutPad(1, 1, true);
    cutPad(3, 1, true);
    seqEngine.idleTick();
    CHECK((c->activeMask() & 0x0F) == 0x0F && ((c->activeMask() >> 10) & 1u));
    CHECK(c->playhead(0).heldCount() == 1 && c->playhead(1).heldCount() == 2);
    CHECK(c->view(0).flags & VF_REC);

    // without Shift a RESYNC pad belongs to its row only
    cutPad(COL_RESYNC, 3, true);
    cutPad(COL_RESYNC, 3, false);
    seqEngine.idleTick();
    CHECK(c->playhead(0).heldCount() == 1 && (c->view(0).flags & VF_REC));

    // Shift + any RESYNC pad: all 16 tracks, held pads and the open take are dropped in one pass
    cutPad(COL_RESYNC, 2, true, true);
    cutPad(COL_RESYNC, 2, false, true);
    seqEngine.idleTick();
    CHECK(!cutSyncPending());
    for (uint8_t t = 0; t < 16; t++)
    {
        CHECK(c->playhead(t).heldCount() == 0);
        const View v = c->view(t);
        CHECK(!(v.flags & (VF_REC | VF_RECORDED | VF_REVERSE | VF_STOPPED)) && viewRate(v) == 1);
    }
    // the physical pads and Rec are released afterwards: nothing is posted for them (the take was forgotten)
    cutPad(6, 0, false);
    cutPad(1, 1, false);
    cutPad(3, 1, false);
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
            a.pad(4, row, true);
            a.pad(4, row, false);
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
    a.pad(11, 1, true);
    g_shiftHeld = false; // Shift let go first; the pad release has no meaning
    a.pad(11, 1, false);
    seqEngine.idleTick();
    snapAll(7);
    CHECK(c->activeMask() == 0);

    // without Shift: that row only
    jamAll();
    a.pad(11, 1, true);
    a.pad(11, 1, false);
    seqEngine.idleTick();
    snapAll(8);
    CHECK((c->activeMask() & 0x0F) == 0x0D);

    // screen button 6 (index 5) = SYNC ALL; its release and the other buttons are not it; the label shows when something is off grid
    char lab[12];
    CHECK(a.buttonLabel(kScreenButtonSyncAll, lab) && strcmp(lab, "[SYNC ALL]") == 0);
    CHECK(!a.buttonLabel(0, lab) && !a.buttonLabel(4, lab) && !a.buttonLabel(6, lab) && !a.buttonLabel(7, lab));
    CHECK(!a.button(4, true) && !a.button(6, true) && !a.button(7, true));
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
    cutPad(3, 0, true);
    cutPad(3, 0, false);
    cutPad(COL_REVERSE, 0, true);
    seqEngine.idleTick();
    CHECK(c->view(0).flags & VF_REVERSE);
    for (int i = 0; i < 70; i++)
        seqEngine.postCut(9, packArg(SUB_RESYNC, true, 0)); // the queue is full
    cutSyncAll();
    CHECK(cutSyncPending());       // kept, not lost
    cutPad(5, 0, true);            // a press must not overtake the waiting SYNC ALL (it would be wiped by it)
    seqEngine.idleTick();          // the audio side drains the queue
    CHECK(c->view(0).flags & VF_REVERSE);
    cutUpdate();                   // the main loop retries
    CHECK(!cutSyncPending());
    seqEngine.idleTick();
    CHECK(!(c->view(0).flags & VF_REVERSE));
    CHECK(c->playhead(0).heldCount() == 0);
    cutPad(5, 0, false);
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

    seqsession::CutSessionLayer a;
    a.enter(0);
    a.pad(5, 0, true);  a.pad(5, 0, false);                    // row 0: a jam at column 5
    a.pad(COL_REVERSE, 1, true);  a.pad(COL_REVERSE, 1, false); // row 1: reverse
    a.pad(COL_RATE, 2, true);  a.pad(COL_RATE, 2, false);       // row 2: double rate
    a.pad(COL_STOP, 3, true);  a.pad(COL_STOP, 3, false);       // row 3: stopped
    a.setGroup(4);
    a.rec(true);
    a.pad(2, 0, true);                                          // track 4: a take starts ...
    seqEngine.idleTick();
    c->setStock(4, 1);
    c->boundary(4, 16, 1);
    c->setStock(4, 2);
    c->boundary(4, 16, 2);
    a.pad(2, 0, false);
    a.rec(false);                                               // ... and closes: a recorded playhead loop
    seqEngine.idleTick();
    CHECK(c->view(4).flags & VF_RECORDED);
    a.pad(3, 2, true);                                          // track 6: a pad is held in the hand (loop gesture)
    a.pad(5, 2, true);
    a.rec(true);
    a.pad(1, 3, true);                                          // track 7: a take is open
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
    a.setGroup(0);
    a.pad(5, 0, true);
    a.rec(true);
    a.pad(6, 1, true);
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
    a.pad(5, 0, false);  // the physical releases arrive later: ignored
    a.pad(6, 1, false);
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
    CHECK(a.arrow(seqsession::ARROW_LEFT, false) && g_lastGroupRequest == 0);
    CHECK(a.arrow(seqsession::ARROW_RIGHT, false) && g_lastGroupRequest == 8);
    CHECK(!a.arrow(seqsession::ARROW_UP, false) && !a.arrow(seqsession::ARROW_DOWN, false));
    a.setGroup(0);
    g_groupRequests = 0;
    CHECK(a.arrow(seqsession::ARROW_LEFT, false) && g_groupRequests == 0); // handled, nowhere to go
    a.setGroup(12);
    CHECK(a.arrow(seqsession::ARROW_RIGHT, false) && g_groupRequests == 0);
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
        texts++;
        const int w = n * 9, h = 17;
        const int x0 = (opt & seqcut::TX_CENTERX) ? x - w / 2 : ((opt & seqcut::TX_RIGHT) ? x - w : x);
        const int y0 = (opt & seqcut::TX_CENTERY) ? y - h / 2 : y;
        bound(x0, y0, x0 + w, y0 + h);
        if (dump) fprintf(dump, "T %d %d %d %d %s\n", x, y, font, opt, s);
    }
    void finish() { ensure(0); }
    int bytes() const { return words * 4; }
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
