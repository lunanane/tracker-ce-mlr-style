// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Plan 026: deliberate two-pad loop (engage timing), jog quantise, ratchet, the stale-hold section bug, the sidecar version rule.
// Included from test_main.cpp after test_rec.inc (uses CHECK, FakePattern, install, stepOnce, freshCut, tCutPad, g_nowMs).

namespace
{
// A Cut as the firmware builds it (sections on, plan 026 behaviour on), track 0 with a 64 step pattern.
void plan26Cut(seqcut::Cut& c, FakePattern& f, uint8_t quant, uint8_t grid)
{
    f.steps = 64;
    f.map[0] = 1;
    install(c, f);
    c.setSectionMode(true);
    c.setDeliberate(true);
    c.apply(0, seqcut::packQuant(quant));
    c.apply(0, seqcut::packGrid(grid));
}
} // namespace

// ---- engage: which press starts a loop, and how long both pads must be held -----------------------------------------------------------
static void test_p26_engage_engine()
{
    using namespace seqcut;
    // The earlier behaviour (deliberate off): the second pad pressed while one is down is a loop at once - this is the accidental
    // trigger (a roll of two fingers over neighbouring pads, or a finger that rests while the other jams).
    {
        Cut c;
        FakePattern f;
        f.steps = 64;
        install(c, f);
        c.setSectionMode(true);
        c.apply(0, packQuant(Q_STEP));
        c.apply(0, packGrid(GRID_4));
        c.apply(0, packArg(SUB_PAD, true, 2));
        c.apply(0, packArg(SUB_PAD, true, 3));
        CHECK(c.view(0).loopLo == 2 && c.view(0).loopHi == 3);
    }
    // deliberate: the second pad is a jump (instant), no loop until engage
    Cut c;
    FakePattern f;
    plan26Cut(c, f, Q_STEP, GRID_4);
    int g = 0;
    stepOnce(c, f, g);
    c.apply(0, packArg(SUB_PAD, true, 2));
    StepPlan p = stepOnce(c, f, g);
    CHECK(p.override && p.pos == 8);
    c.apply(0, packArg(SUB_PAD, true, 3)); // the roll: pad 3 goes down while pad 2 is still down
    CHECK(!c.playhead(0).engaged() && c.view(0).loopLo > c.view(0).loopHi);
    p = stepOnce(c, f, g);
    CHECK(p.override && p.pos == 12 && !c.playhead(0).looping()); // jumped to pad 3 on the very next step: jamming is instant
    c.apply(0, packArg(SUB_PAD, false, 2));
    c.apply(0, packArg(SUB_PAD, false, 3));
    CHECK(!c.playhead(0).looping());
    // engage with fewer than two pads down does nothing
    c.apply(0, packEngage());
    CHECK(!c.playhead(0).engaged());
    // two pads down + engage -> loop over their range from the first pad, latched after the release
    c.apply(0, packArg(SUB_PAD, true, 2));
    c.apply(0, packArg(SUB_PAD, true, 5));
    c.apply(0, packEngage());
    CHECK(c.playhead(0).engaged() && c.view(0).loopLo == 2 && c.view(0).loopHi == 5);
    p = stepOnce(c, f, g);
    CHECK(p.override && p.pos == 8); // starts at the first pad's step
    for (int i = 0; i < 30; i++)
    {
        p = stepOnce(c, f, g);
        CHECK(p.pos >= 8 && p.pos <= 23);
    }
    c.apply(0, packArg(SUB_PAD, false, 2));
    c.apply(0, packArg(SUB_PAD, false, 5));
    CHECK(c.playhead(0).looping() && !c.playhead(0).engaged());
    // a third pad while engaged extends the loop at once (no second hold)
    c.apply(0, packArg(SUB_PAD, true, 1));
    c.apply(0, packArg(SUB_PAD, true, 6));
    c.apply(0, packEngage());
    c.apply(0, packArg(SUB_PAD, true, 8));
    stepOnce(c, f, g);
    CHECK(c.view(0).loopLo == 1 && c.view(0).loopHi == 8);
    // one pad afterwards ends the loop (as ever)
    c.apply(0, packArg(SUB_PAD, false, 1));
    c.apply(0, packArg(SUB_PAD, false, 6));
    c.apply(0, packArg(SUB_PAD, false, 8));
    c.apply(0, packArg(SUB_PAD, true, 10));
    c.apply(0, packArg(SUB_PAD, false, 10));
    stepOnce(c, f, g);
    p = stepOnce(c, f, g);
    CHECK(!c.playhead(0).looping() && p.pos >= 40);
    // the engage command packs into the SUB_RESET slot without touching the others
    CHECK(argIsEngage(packEngage()) && !argIsQuant(packEngage()) && !argIsGrid(packEngage()) && !argIsHold(packEngage()) && !argIsSyncAll(packEngage()));
    for (uint8_t m = 0; m < Q_COUNT; m++)
        CHECK(!argIsEngage(packQuant(m)) && argIsQuant(packQuant(m)) && argQuant(packQuant(m)) == m);
}

static void test_p26_engage_timing_layer()
{
    using namespace seqcut;
    FakePattern f;
    seqcut::Cut* c = freshCut(f);
    if (!c)
        return;
    CHECK(kEngageMs >= 300 && kEngageMs <= 400); // the owner's 0.3 - 0.4 s
    cutUpdate(g_nowMs);
    CHECK(cutEngagePercent(0) == 0);
    tCutPad(1, 0, true);
    seqEngine.idleTick();
    CHECK(cutEngagePercent(0) == 0 && !c->playhead(0).engaged()); // a single pad: nothing to engage
    g_nowMs += 40;
    cutUpdate(g_nowMs);
    tCutPad(4, 0, true); // the second pad of slot A
    seqEngine.idleTick();
    CHECK(cutEngagePercent(0) == 1 && cutEngagePercent(1) == 0);
    CHECK(c->view(0).loopLo > c->view(0).loopHi && !c->playhead(0).engaged()); // jumping, no loop yet
    const uint32_t t0 = g_nowMs;
    cutUpdate(t0 + kEngageMs / 2);
    seqEngine.idleTick();
    CHECK(cutEngagePercent(0) >= 45 && cutEngagePercent(0) <= 55 && !c->playhead(0).engaged());
    cutUpdate(t0 + kEngageMs - 1);
    seqEngine.idleTick();
    CHECK(cutEngagePercent(0) >= 99 && !c->playhead(0).engaged()); // 1 ms short: not yet
    cutUpdate(t0 + kEngageMs);
    seqEngine.idleTick();
    CHECK(c->playhead(0).engaged() && c->view(0).loopLo == 1 && c->view(0).loopHi == 4 && cutEngagePercent(0) == 0);
    g_nowMs = t0 + kEngageMs + 10;
    tCutPad(1, 0, false);
    tCutPad(4, 0, false);
    seqEngine.idleTick();
    CHECK(c->playhead(0).looping() || c->playhead(0).pending()); // latched (it starts at the next grid point)

    // released before the time: never engages (a quick roll over two pads)
    freshCut(f);
    g_nowMs += 1000;
    cutUpdate(g_nowMs);
    tCutPad(1, 0, true);
    tCutPad(2, 0, true);
    seqEngine.idleTick();
    CHECK(cutEngagePercent(0) == 1);
    cutUpdate(g_nowMs + kEngageMs - 50);
    tCutPad(1, 0, false);
    cutUpdate(g_nowMs + kEngageMs + 500);
    tCutPad(2, 0, false);
    seqEngine.idleTick();
    CHECK(!c->playhead(0).engaged() && !c->playhead(0).looping() && cutEngagePercent(0) == 0);

    // the timer belongs to the second press, not to the first: a pad held for ever and a second one tapped does not engage
    freshCut(f);
    g_nowMs += 5000;
    cutUpdate(g_nowMs);
    tCutPad(1, 0, true);
    g_nowMs += 2000; // the first pad has been down for 2 s
    cutUpdate(g_nowMs);
    tCutPad(5, 0, true);
    tCutPad(5, 0, false);
    cutUpdate(g_nowMs + 100);
    seqEngine.idleTick();
    CHECK(!c->playhead(0).engaged() && cutEngagePercent(0) == 0);
    tCutPad(1, 0, false);

    // the two slots time separately
    freshCut(f);
    g_nowMs += 3000;
    cutUpdate(g_nowMs);
    tCutPad(1, 0, true);
    tCutPad(2, 0, true);
    tCutPad(1, 1, true);
    seqEngine.idleTick();
    CHECK(cutEngagePercent(0) == 1 && cutEngagePercent(1) == 0);
    cutReleaseAll();
    seqEngine.idleTick();
    CHECK(cutEngagePercent(0) == 0);

    // SYNC ALL / panic clear a running engage timer
    freshCut(f);
    g_nowMs += 3000;
    cutUpdate(g_nowMs);
    tCutPad(1, 0, true);
    tCutPad(2, 0, true);
    cutPanic();
    cutUpdate(g_nowMs + kEngageMs * 2);
    seqEngine.idleTick();
    CHECK(cutEngagePercent(0) == 0 && !c->playhead(0).engaged());
    freshCut(f);
}

// ---- jog: dial the quantise ---------------------------------------------------------------------------------------------------------
static void test_p26_jog_quantise()
{
    using namespace seqcut;
    // the dial order: off, 1/32, 1/16, 1/8, 1/4, 1/2, 1 bar; stops at both ends
    uint8_t m = Q_OFF;
    const uint8_t order[7] = { Q_OFF, Q_32, Q_STEP, Q_8, Q_BEAT, Q_HALF, Q_BAR };
    for (int i = 0; i < 7; i++)
    {
        CHECK(m == order[i]);
        m = quantDial(m, 1);
    }
    CHECK(m == Q_BAR && quantDial(Q_BAR, 1) == Q_BAR && quantDial(Q_OFF, -1) == Q_OFF);
    for (int i = 6; i > 0; i--)
        CHECK(quantDial(order[i], -1) == order[i - 1]);
    CHECK(quantDial(Q_BEAT, 0) == Q_BEAT);
    // names, steps: 1/32 = half a step (quantises at the step start and at the middle), 1/16 = 1 step, 1/8 = 2, 1/4 = 4, 1/2 = 8, bar = 16
    CHECK(strcmp(quantName(Q_OFF), "off") == 0 && strcmp(quantName(Q_32), "1/32") == 0 && strcmp(quantName(Q_STEP), "1/16") == 0 &&
          strcmp(quantName(Q_8), "1/8") == 0 && strcmp(quantName(Q_BEAT), "1/4") == 0 && strcmp(quantName(Q_HALF), "1/2") == 0 &&
          strcmp(quantName(Q_BAR), "1 bar") == 0);
    CHECK(quantSteps(Q_32) == 1 && quantSteps(Q_STEP) == 1 && quantSteps(Q_8) == 2 && quantSteps(Q_BEAT) == 4 && quantSteps(Q_HALF) == 8 &&
          quantSteps(Q_BAR) == 16);
    CHECK(quantStepsFor(Q_8, 24) == 2 && quantStepsFor(Q_HALF, 24) == 8 && quantStepsFor(Q_32, 24) == 1);
    // the screen button still taps through off -> 1/16 -> 1/4 -> 1 bar, from a dialled value too
    CHECK(quantNext(Q_OFF) == Q_STEP && quantNext(Q_STEP) == Q_BEAT && quantNext(Q_BEAT) == Q_BAR && quantNext(Q_BAR) == Q_OFF);
    CHECK(quantNext(Q_32) == Q_STEP && quantNext(Q_8) == Q_BEAT && quantNext(Q_HALF) == Q_BAR);
    // through the layer: the jog turns the setting, the audio side has it, the screen button label shows it
    FakePattern f;
    seqcut::Cut* c = freshCut(f);
    if (!c)
        return;
    seqsession::CutSessionLayer a;
    a.enter(0);
    cutQuantSet(Q_BEAT);
    cutUpdate();
    seqEngine.idleTick();
    char lab[12];
    CHECK(a.jog(1, false) && cutQuantMode() == Q_HALF);
    cutUpdate();
    seqEngine.idleTick();
    CHECK(c->quant() == Q_HALF && a.buttonLabel(kScreenButtonQuant, lab) && strcmp(lab, "Q 1/2") == 0);
    CHECK(a.jog(5, false) && cutQuantMode() == Q_BAR); // any size of detent = one step
    CHECK(a.jog(1, false) && cutQuantMode() == Q_BAR);
    for (int i = 0; i < 6; i++)
        a.jog(-3, false);
    CHECK(cutQuantMode() == Q_OFF);
    a.jog(-1, false);
    CHECK(cutQuantMode() == Q_OFF);
    CHECK(a.jog(1, false) && cutQuantMode() == Q_32);
    cutUpdate();
    seqEngine.idleTick();
    CHECK(c->quant() == Q_32 && c->playhead(0).quantise() == 1);
    cutQuantSet(Q_8);
    cutUpdate();
    seqEngine.idleTick();
    CHECK(c->quant() == Q_8);
    cutQuantSet(Q_BEAT);
    cutUpdate();
    seqEngine.idleTick();
    CHECK(a.jog(0, false) && cutQuantMode() == Q_BEAT); // 0 = nothing
    freshCut(f);
}

// ---- quantised jumps at the new rates -------------------------------------------------------------------------------------------------
static void test_p26_quantise_landing()
{
    using namespace seqcut;
    // 1/8 and 1/2: the jump waits for the grid of 2 / 8 steps (counted in the pattern)
    for (int qi = 0; qi < 2; qi++)
    {
        const uint8_t q = qi ? Q_HALF : Q_8;
        const int every = quantSteps(q);
        for (int start = 1; start < 20; start++)
        {
            Cut c;
            FakePattern f;
            plan26Cut(c, f, q, GRID_4);
            int g = 0;
            for (int i = 0; i < start; i++) stepOnce(c, f, g);
            c.apply(0, packArg(SUB_PAD, true, 6));
            c.apply(0, packArg(SUB_PAD, false, 6));
            int landed = -1;
            for (int i = 1; i <= 20 && landed < 0; i++)
            {
                const StepPlan p = stepOnce(c, f, g);
                if (p.override && p.pos == 24)
                    landed = i;
            }
            // it lands on the first boundary whose pattern step is a multiple of `every`
            int expect = 1;
            while (((start + expect) % every) != 0) expect++;
            CHECK(landed == expect);
        }
    }
    // 1/32: the jump lands at the middle of the step when that comes first
    {
        Cut c;
        FakePattern f;
        plan26Cut(c, f, Q_32, GRID_4);
        int g = 0;
        stepOnce(c, f, g);
        stepOnce(c, f, g);
        c.apply(0, packArg(SUB_PAD, true, 6));
        c.apply(0, packArg(SUB_PAD, false, 6));
        StepPlan m;
        CHECK(c.now(0, 64, m) && m.override && m.pos == 24); // the middle of the step (µstep 25): applied
        CHECK(!c.now(0, 64, m));                              // consumed
        f.pos = 24;
        const StepPlan p = stepOnce(c, f, g);
        CHECK(p.override && p.pos == 25); // carries on from there, no second jump
    }
    // 1/32: and at the step start when the pad comes after the middle
    {
        Cut c;
        FakePattern f;
        plan26Cut(c, f, Q_32, GRID_4);
        int g = 0;
        stepOnce(c, f, g);
        c.apply(0, packArg(SUB_PAD, true, 6));
        c.apply(0, packArg(SUB_PAD, false, 6));
        CHECK(stepOnce(c, f, g).pos == 24);
    }
    // 1/16 never applies in the middle of a step
    {
        Cut c;
        FakePattern f;
        plan26Cut(c, f, Q_STEP, GRID_4);
        int g = 0;
        stepOnce(c, f, g);
        c.apply(0, packArg(SUB_PAD, true, 6));
        StepPlan m;
        CHECK(!c.now(0, 64, m));
        CHECK(stepOnce(c, f, g).pos == 24);
    }
    // the ring byte of the new modes reaches the engine and the playheads
    {
        Cut c;
        FakePattern f;
        plan26Cut(c, f, Q_STEP, GRID_4);
        c.apply(0, packArg(SUB_PAD, true, 1));
        c.apply(0, packArg(SUB_PAD, false, 1));
        const uint8_t modes[6] = { Q_32, Q_8, Q_HALF, Q_BAR, Q_OFF, Q_BEAT };
        for (int i = 0; i < 6; i++)
        {
            c.apply(0, packQuant(modes[i]));
            CHECK(c.quant() == modes[i] && c.playhead(0).quantise() == quantStepsFor(modes[i], 64) && !c.playhead(0).ratcheting());
        }
    }
}

// ---- ratchet ---------------------------------------------------------------------------------------------------------------------------
static void test_p26_ratchet()
{
    using namespace seqcut;
    // 1/16: a held pad retriggers its position at every step; release -> normal playback from there
    {
        Cut c;
        FakePattern f;
        plan26Cut(c, f, Q_STEP, GRID_4);
        int g = 0;
        for (int i = 0; i < 3; i++) stepOnce(c, f, g);
        c.apply(0, packArg(SUB_PAD, true, 5)); // step 20
        StepPlan p = stepOnce(c, f, g);
        CHECK(p.override && p.pos == 20 && p.retrigger); // the jump
        for (int i = 0; i < 6; i++)
        {
            p = stepOnce(c, f, g);
            CHECK(p.override && p.pos == 20 && p.retrigger && p.noteStep == 0 && p.elapsed == 20); // the same position again, struck again
        }
        CHECK((c.view(0).flags & VF_RATCHET) != 0 && c.playhead(0).ratcheting());
        c.apply(0, packArg(SUB_PAD, false, 5));
        p = stepOnce(c, f, g);
        CHECK(p.override && p.pos == 21 && !p.retrigger); // released: back to normal, from where it was
        CHECK((c.view(0).flags & VF_RATCHET) == 0);
        p = stepOnce(c, f, g);
        CHECK(p.pos == 22);
    }
    // a tap shorter than the rate does not ratchet
    {
        Cut c;
        FakePattern f;
        plan26Cut(c, f, Q_STEP, GRID_4);
        int g = 0;
        stepOnce(c, f, g);
        c.apply(0, packArg(SUB_PAD, true, 5));
        c.apply(0, packArg(SUB_PAD, false, 5));
        CHECK(stepOnce(c, f, g).pos == 20);
        CHECK(stepOnce(c, f, g).pos == 21);
    }
    // 1/4: the pad's position repeats from the beat: 20 21 22 23 | 20 21 22 23 ...
    {
        Cut c;
        FakePattern f;
        plan26Cut(c, f, Q_BEAT, GRID_4);
        int g = 0;
        stepOnce(c, f, g);
        c.apply(0, packArg(SUB_PAD, true, 5));
        int seen[14];
        int n = 0, first = -1;
        for (int i = 0; i < 24 && n < 14; i++)
        {
            const StepPlan p = stepOnce(c, f, g);
            if (first < 0 && p.override && p.pos == 20) first = i;
            if (first >= 0) seen[n++] = p.pos;
        }
        CHECK(n == 14);
        for (int i = 0; i < n; i++) CHECK(seen[i] == 20 + (i % 4));
    }
    // 1/8: 2-step ratchet
    {
        Cut c;
        FakePattern f;
        plan26Cut(c, f, Q_8, GRID_4);
        int g = 0;
        stepOnce(c, f, g);
        c.apply(0, packArg(SUB_PAD, true, 5));
        int n = 0, first = -1;
        int seen[8];
        for (int i = 0; i < 20 && n < 8; i++)
        {
            const StepPlan p = stepOnce(c, f, g);
            if (first < 0 && p.override && p.pos == 20) first = i;
            if (first >= 0) seen[n++] = p.pos;
        }
        CHECK(n == 8);
        for (int i = 0; i < n; i++) CHECK(seen[i] == 20 + (i % 2));
    }
    // quantise off: no ratchet at all - the held pad plays on
    {
        Cut c;
        FakePattern f;
        plan26Cut(c, f, Q_OFF, GRID_4);
        int g = 0;
        stepOnce(c, f, g);
        c.apply(0, packArg(SUB_PAD, true, 5));
        StepPlan now;
        CHECK(c.now(0, 64, now) && now.pos == 20);
        f.pos = 20;
        CHECK(stepOnce(c, f, g).pos == 21);
        CHECK(stepOnce(c, f, g).pos == 22);
        CHECK(!c.playhead(0).ratcheting());
    }
    // 1/32: every step plays the held position and again in the middle of the step
    {
        Cut c;
        FakePattern f;
        plan26Cut(c, f, Q_32, GRID_4);
        int g = 0;
        stepOnce(c, f, g);
        c.apply(0, packArg(SUB_PAD, true, 5));
        StepPlan p = stepOnce(c, f, g);
        CHECK(p.pos == 20);
        StepPlan m;
        CHECK(c.mid(0, 64, m) && m.override && m.pos == 20 && m.retrigger); // the second strike of the step
        for (int i = 0; i < 4; i++)
        {
            p = stepOnce(c, f, g);
            CHECK(p.pos == 20 && p.retrigger);
            CHECK(c.mid(0, 64, m) && m.pos == 20 && m.retrigger);
        }
        c.apply(0, packArg(SUB_PAD, false, 5));
        p = stepOnce(c, f, g);
        CHECK(p.pos == 21 && !c.mid(0, 64, m));
    }
    // a second pad while ratcheting: the ratchet moves to the new pad; releasing the old one changes nothing
    {
        Cut c;
        FakePattern f;
        plan26Cut(c, f, Q_STEP, GRID_4);
        int g = 0;
        stepOnce(c, f, g);
        c.apply(0, packArg(SUB_PAD, true, 5));
        stepOnce(c, f, g);
        stepOnce(c, f, g);
        c.apply(0, packArg(SUB_PAD, true, 9)); // step 36
        CHECK(stepOnce(c, f, g).pos == 36);
        CHECK(stepOnce(c, f, g).pos == 36);
        c.apply(0, packArg(SUB_PAD, false, 5));
        CHECK(stepOnce(c, f, g).pos == 36 && c.playhead(0).ratcheting());
        c.apply(0, packArg(SUB_PAD, false, 9));
        CHECK(stepOnce(c, f, g).pos == 37);
    }
    // a loop (engaged) is not a ratchet; SYNC ALL ends a ratchet
    {
        Cut c;
        FakePattern f;
        plan26Cut(c, f, Q_STEP, GRID_4);
        int g = 0;
        stepOnce(c, f, g);
        c.apply(0, packArg(SUB_PAD, true, 2));
        c.apply(0, packArg(SUB_PAD, true, 4));
        c.apply(0, packEngage());
        for (int i = 0; i < 12; i++)
        {
            const StepPlan p = stepOnce(c, f, g);
            CHECK(p.pos >= 8 && p.pos <= 23 && !c.playhead(0).ratcheting());
        }
        c.apply(0, packArg(SUB_PAD, false, 2));
        c.apply(0, packArg(SUB_PAD, false, 4));
        c.apply(0, packArg(SUB_PAD, true, 12));
        stepOnce(c, f, g);
        stepOnce(c, f, g);
        CHECK(c.playhead(0).ratcheting());
        c.apply(0, packSyncAll());
        CHECK(!c.playhead(0).ratcheting());
    }
    // not deliberate (the earlier engine): a held pad plays on, there is no ratchet
    {
        Cut c;
        FakePattern f;
        f.steps = 64;
        install(c, f);
        c.setSectionMode(true);
        c.apply(0, packQuant(Q_STEP));
        c.apply(0, packGrid(GRID_4));
        int g = 0;
        stepOnce(c, f, g);
        c.apply(0, packArg(SUB_PAD, true, 5));
        CHECK(stepOnce(c, f, g).pos == 20);
        CHECK(stepOnce(c, f, g).pos == 21);
    }
}

// ---- the open device bug: pads acted on the first section after the page had moved on ------------------------------------------------------
static void test_p26_section_scroll_stale_hold()
{
    using namespace seqcut;
    int bad = 0, total = 0;
    for (int deliberate = 0; deliberate < 2; deliberate++)
    for (uint8_t q = Q_OFF; q < Q_COUNT; q++)   // (a held pad ratchets from 1/32 up: the page only scrolls on with quantise off)
    for (uint8_t g = 0; g < GRID_COUNT; g++)
    for (int stale = 0; stale < 3; stale++)
    for (int k = 0; k < 16; k += 3)
    {
        const int spp = gridSteps(g), ss = sectionSteps(spp);
        Cut c;
        FakePattern f;
        f.steps = 192;
        f.map[0] = 1;
        install(c, f);
        c.setSectionMode(true);
        c.setDeliberate(deliberate != 0);
        c.apply(0, packQuant(q));
        c.apply(0, packGrid(g));
        int gl = 0;
        // a pad of section 0 goes down and is never let go (a lost release / a finger that stays)
        c.apply(0, packArg(SUB_PAD, true, 2));
        for (int i = 0; i < ss + 9; i++) stepOnce(c, f, gl); // the page scrolls on into section 1
        if (stale == 1) c.apply(0, packSyncAll());           // the workaround of the report
        const View v = c.view(0);
        if (v.sec != 1 || k >= v.pads) continue;
        if (stale == 2) c.apply(0, packArg(SUB_PAD, false, 2)); // control: a clean release
        c.apply(0, packArg(SUB_PAD, true, (uint8_t)k));
        c.apply(0, packArg(SUB_PAD, false, (uint8_t)k));
        int landed = -1;
        for (int i = 0; i < 40 && landed < 0; i++)
        {
            const StepPlan p = stepOnce(c, f, gl);
            if (p.override && p.pos / ss == v.sec && p.pos % ss == k * spp) landed = p.pos;
        }
        total++;
        if (landed < 0) bad++;
    }
    CHECK(total > 200 && bad == 0);
    // the layer path: the same through cutPad
    FakePattern f;
    f.steps = 192;
    seqcut::Cut* c = freshCut(f);
    if (!c)
        return;
    for (uint8_t t = 0; t < 16; t++)
        seqEngine.postCut(t, packGrid(GRID_4));
    cutQuantSet(Q_OFF);
    cutUpdate();
    seqEngine.idleTick();
    int gl = 0;
    cutAssignSlot(0, 0);
    cutPad(2, 0, true);
    seqEngine.idleTick();
    for (int i = 0; i < 64 + 9; i++)
    {
        f.pos = (f.pos + 1) % f.steps;
        gl = (gl + 1) % f.steps;
        c->setStock(0, gl);
        StepPlan p = c->boundary(0, f.steps, gl);
        if (p.override) f.pos = p.pos;
    }
    CHECK(c->view(0).sec == 1);
    cutPad(5, 0, true);
    cutPad(5, 0, false);
    seqEngine.idleTick();
    int landed = -1;
    for (int i = 0; i < 40 && landed < 0; i++)
    {
        f.pos = (f.pos + 1) % f.steps;
        gl = (gl + 1) % f.steps;
        c->setStock(0, gl);
        StepPlan p = c->boundary(0, f.steps, gl);
        if (p.override) f.pos = p.pos;
        if (p.override && p.pos == 64 + 5 * 4) landed = p.pos;
    }
    CHECK(landed == 64 + 20);
    cutReleaseAll();
    freshCut(f);
}

// ---- sidecar: oldest version that fits ---------------------------------------------------------------------------------------------------
static void test_p26_sidecar_version()
{
    using namespace seqbr;
    uint8_t buf[BrParams::kFileMax + 8];
    BrParams p;
    p.step[BP_CHANCE] = 4;
    // only the beat repeat settings changed: version 1, 8 bytes payload (every earlier firmware reads it)
    size_t n = p.save(buf, sizeof(buf));
    CHECK(n == (size_t)BrParams::kPayload + seqsidecar::kOverhead && buf[4] == 1);
    BrParams q;
    bool newer = true;
    CHECK(q.load(buf, n, &newer) && !newer && q == p && q.mlrQuant == 2 && q.mlrSlotA == 0 && q.mlrSlotB == 1);
    // a changed MLR quantise or slot track: version 2 with the three extra bytes
    BrParams w = p;
    w.mlrQuant = 5; // 1/8
    w.mlrSlotA = 7;
    w.mlrSlotB = 3;
    CHECK(!w.isDefault() && w.hash() != p.hash());
    n = w.save(buf, sizeof(buf));
    CHECK(n == (size_t)BrParams::kPayload2 + seqsidecar::kOverhead && buf[4] == 2 && memcmp(buf, "SBRP", 4) == 0);
    BrParams r;
    CHECK(r.load(buf, n, &newer) && !newer && r == w && r.mlrQuant == 5 && r.mlrSlotA == 7 && r.mlrSlotB == 3 && r.hash() == w.hash());
    // only the quantise changed
    BrParams only;
    only.mlrQuant = 0;
    n = only.save(buf, sizeof(buf));
    CHECK(buf[4] == 2 && r.load(buf, n) && r.mlrQuant == 0 && r.mlrSlotA == 0 && r.mlrSlotB == 1 && r.step[BP_CHANCE] == 1);
    // back to the defaults: version 1 again
    BrParams back = w;
    back.mlrQuant = 2;
    back.mlrSlotA = 0;
    back.mlrSlotB = 1;
    n = back.save(buf, sizeof(buf));
    CHECK(buf[4] == 1 && back.mlrDefault());
    // an earlier firmware (reader of version 1 only) leaves a version 2 file alone: NEWER for max version 1
    n = w.save(buf, sizeof(buf));
    const uint8_t* pay = nullptr;
    size_t pn = 0;
    uint8_t pv = 0;
    CHECK(seqsidecar::frameRead(buf, n, "SBRP", 1, &pay, &pn, &pv) == seqsidecar::FRAME_NEWER);
    CHECK(seqsidecar::frameRead(buf, n, "SBRP", 2, &pay, &pn, &pv) == seqsidecar::FRAME_OK && pv == 2 && pn == BrParams::kPayload2);
    // a version 1 file written by an earlier firmware: the MLR settings are the defaults
    uint8_t old[32];
    uint8_t body[8] = { 3, 6, 1, 8, 1, 1, 1, 0 };
    const size_t on = seqsidecar::frameWrite(old, sizeof(old), "SBRP", 1, body, 8);
    BrParams ob;
    ob.mlrQuant = 6; // whatever was in memory: a load replaces everything
    CHECK(ob.load(old, on) && ob.mlrDefault());
    // an invalid MLR part keeps the defaults: out of range quantise, equal slot tracks; a short one too
    uint8_t b2[11] = { 3, 6, 1, 8, 1, 1, 1, 0, 9, 4, 4 };
    const size_t bn = seqsidecar::frameWrite(old, sizeof(old), "SBRP", 2, b2, 11);
    BrParams bad;
    CHECK(bad.load(old, bn) && bad.mlrDefault());
    uint8_t b3[9] = { 3, 6, 1, 8, 1, 1, 1, 0, 4 };
    const size_t cn = seqsidecar::frameWrite(old, sizeof(old), "SBRP", 2, b3, 9);
    CHECK(bad.load(old, cn) && bad.mlrDefault());
}

// ---- the layer saves what the jog and the Assign buttons change -------------------------------------------------------------------------
static void test_p26_settings_roundtrip()
{
    using namespace seqcut;
    FakePattern f;
    freshCut(f);
    seqbr::Ui& ui = seqBrUi();
    cutQuantSet(Q_BEAT);
    cutAssignSlot(0, 0);
    cutAssignSlot(1, 1);
    CHECK(ui.prm.mlrQuant == Q_BEAT && ui.prm.mlrSlotA == 0 && ui.prm.mlrSlotB == 1 && ui.prm.mlrDefault());
    const size_t acts = g_brActs.size();
    cutQuantDial(-1); // 1/8
    CHECK(cutQuantMode() == Q_8 && ui.prm.mlrQuant == Q_8 && !ui.prm.mlrDefault());
    CHECK(g_brActs.size() > acts && g_brActs.back().act == seqbr::BA_SETTINGS); // the autosave is asked for
    cutAssignSlot(1, 9);
    CHECK(ui.prm.mlrSlotB == 9 && cutSlotTrack(1) == 9);
    // a project load puts the saved values back (no write asked for)
    const size_t acts2 = g_brActs.size();
    cutMlrLoad(Q_HALF, 4, 5);
    CHECK(cutQuantMode() == Q_HALF && cutSlotTrack(0) == 4 && cutSlotTrack(1) == 5 && g_brActs.size() == acts2);
    cutMlrLoad(Q_BEAT, 4, 4); // two equal slot tracks are refused, the quantise is taken
    CHECK(cutQuantMode() == Q_BEAT && cutSlotTrack(0) == 4 && cutSlotTrack(1) == 5);
    cutMlrLoad(200, 0, 1);    // out of range: wrapped into the modes
    CHECK(cutQuantMode() < Q_COUNT);
    cutMlrLoad(Q_BEAT, 0, 1);
    cutUpdate();
    seqEngine.idleTick();
    freshCut(f);
}

// ---- the screen: 4 x 2 block, engage bar ----------------------------------------------------------------------------------------------
static void test_p26_screen_layout()
{
    using namespace seqcut;
    // the eight settings of a slot sit where their pads are: 4 columns x 2 rows (STOP REV RATE RSYNC / GRID SECT REC MUTE)
    static const uint8_t want[8] = { PR_STOP, PR_REV, PR_RATE, PR_RSYNC, PR_GRID, PR_HOLD, PR_REC, PR_MUTE };
    for (int i = 0; i < 8; i++)
    {
        const seqcut::PadInfo p = seqcut::padInfo((uint8_t)(kPosCols + (i & 3)), (uint8_t)(i >> 2));
        CHECK(p.role == want[i]);
        const int x = kMlrBlockX + (i & 3) * kMlrCellPitch, y = 4 + (i >> 2) * kMlrCellPitchY;
        CHECK(x + kMlrCellW <= 800 && y + kMlrCellH <= kMlrLaneH && x >= kMlrLaneX + kMlrLaneW);
    }
    // engage bar and its text are part of the lane (and fit the screen)
    MlrModel e = mlrScene(0);
    CountCtx plain, eng;
    drawMlr(plain, e);
    plain.finish();
    e.slot[0].engage = 100;
    drawMlr(eng, e);
    eng.finish();
    CHECK(eng.rects > plain.rects && eng.texts == plain.texts + 1 && eng.minX >= 0 && eng.maxX <= 800 && eng.maxY <= 424);
}

static void test_plan26()
{
    test_p26_engage_engine();
    test_p26_engage_timing_layer();
    test_p26_jog_quantise();
    test_p26_quantise_landing();
    test_p26_ratchet();
    test_p26_section_scroll_stale_hold();
    test_p26_sidecar_version();
    test_p26_settings_roundtrip();
    test_p26_screen_layout();
}
