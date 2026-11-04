// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// CUT (MLR) layer tests. Included from test_main.cpp (uses CHECK, mk, playUSteps, seqEngine).
#include "seqCut.h"
#include "cutLayer.h"

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
    c.setStock(0, f.pos);
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
