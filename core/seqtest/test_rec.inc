// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Plan 008 A: the Rec key (Play mode and MLR) replayed with the event sequence of the real keys driver, and the MLR jump quantise.
// Included from test_main.cpp after test_cut.inc (uses CHECK, MockHost, Head, playRec, drainOps, FakePattern, install, freshCut, stepOnce).
#include "seqRecLogic.h"
#include <algorithm>
#include <functional>

namespace
{
// ---- model of platforms/mk66/3rdparty/Buttons/Keys.cpp: what the firmware gets for ONE physical press that lasts downMs ----
// press at once; hold after 333 ms, then (333 - diff) with diff = 0, 100, 190, 235, 255, 270 (the step sticks at the last); release.
struct KeyEv
{
    int ms;
    uint8_t state; // buttonRelease 0 / buttonPress 1 / buttonHold 2
};
std::vector<KeyEv> keyDriver(int startMs, int downMs)
{
    std::vector<KeyEv> v;
    v.push_back({ startMs, seqrecui::KEY_PRESS });
    static const int diff[6] = { 0, 100, 190, 235, 255, 270 };
    int t = 0, f = 0;
    for (;;)
    {
        int next = t + 333 - diff[f];
        if (next >= downMs)
            break;
        t = next;
        if (f < 5)
            f++;
        v.push_back({ startMs + t, seqrecui::KEY_HOLD });
    }
    v.push_back({ startMs + downMs, seqrecui::KEY_RELEASE });
    return v;
}

// The v0.2.4 mapping of the Session host (functRecButton + CutSessionLayer::rec): cutRec(state == buttonPress).
// Kept only to prove that the key model reproduces the bug the owner saw.
void legacyRec(uint8_t state)
{
    cutRec(state == seqrecui::KEY_PRESS);
}
// The host as it is now.
void hostRec(seqsession::CutSessionLayer& a, uint8_t state)
{
    int8_t e = seqrecui::layerEvent(state);
    if (e >= 0)
        a.rec(e != 0);
}

// MLR timeline: one step = 125 ms (120 bpm), the step boundary k happens at 125 * k ms. The main loop events come first (they are
// posted), the audio side drains them at the boundary.
struct MlrRun
{
    seqcut::Cut* c;
    FakePattern* f;
    int g = 0;
    int stepsDone = 0;
    std::vector<int> played; // pos of every boundary where the cut track 0 overrode the position (-1: it did not)
    MlrRun(seqcut::Cut* cc, FakePattern* ff) : c(cc), f(ff) {}
    void runTo(int ms)
    {
        while ((stepsDone + 1) * 125 <= ms)
        {
            seqEngine.idleTick(); // the audio side drains the commands posted so far
            seqcut::StepPlan p = stepOnce(*c, *f, g);
            stepsDone++;
            played.push_back(p.override ? p.pos : -1);
        }
    }
};

// A timeline of main loop actions at given times (ms), run in order against an MlrRun.
struct Timeline
{
    typedef std::pair<int, std::function<void()> > Act;
    std::vector<Act> act;
    void at(int ms, std::function<void()> fn) { act.push_back(Act(ms, fn)); }
    void run(MlrRun& r)
    {
        std::stable_sort(act.begin(), act.end(), [](const Act& a, const Act& b) { return a.first < b.first; });
        for (size_t i = 0; i < act.size(); i++)
        {
            r.runTo(act[i].first);
            act[i].second();
        }
    }
};
} // namespace

// ---- the toggle logic of the Play mode Rec key ----
static void test_rec_toggle_logic()
{
    using seqrecui::RecToggle;
    RecToggle t;
    // press starts, press stops
    CHECK(t.press(1000, false) == RecToggle::START && t.on());
    CHECK(!t.update(1010, true) && t.on()); // the audio side shows it
    CHECK(t.press(2000, true) == RecToggle::STOP && !t.on());
    CHECK(t.press(3000, false) == RecToggle::START);
    // a second press before the audio side published anything still toggles (the UI is not behind the audio side)
    RecToggle q;
    CHECK(q.press(100, false) == RecToggle::START);
    CHECK(q.press(300, false) == RecToggle::STOP);
    // a bounce within 60 ms is ignored, the same press 61 ms later is not
    RecToggle b;
    CHECK(b.press(500, false) == RecToggle::START);
    CHECK(b.press(501, true) == RecToggle::NONE && b.on());
    CHECK(b.press(559, true) == RecToggle::NONE && b.on());
    CHECK(b.press(561, true) == RecToggle::STOP);
    // a take that ended by itself (loop done, transport stop, track Off): update() says so once and the next press starts
    RecToggle e;
    CHECK(e.press(0, false) == RecToggle::START);
    CHECK(!e.update(5, true));
    CHECK(e.update(900, false) && !e.on());
    CHECK(!e.update(901, false));
    CHECK(e.press(1000, false) == RecToggle::START);
    // a start that never showed up on the audio side is dropped after 400 ms
    RecToggle s;
    CHECK(s.press(0, false) == RecToggle::START);
    CHECK(!s.update(399, false) && s.on());
    CHECK(!s.update(400, false) && !s.on());
    // refused start (Off track): nothing is on
    RecToggle r;
    CHECK(r.press(0, false) == RecToggle::START);
    r.cancel();
    CHECK(!r.on() && r.press(1000, false) == RecToggle::START);
    // the audio side shows a take the UI does not know (state left over): the press ends it
    RecToggle x;
    CHECK(x.press(0, true) == RecToggle::STOP);
    // hold and release are not toggles; the Session host passes press / release only
    CHECK(seqrecui::isToggle(seqrecui::KEY_PRESS) && !seqrecui::isToggle(seqrecui::KEY_HOLD) && !seqrecui::isToggle(seqrecui::KEY_RELEASE));
    CHECK(seqrecui::layerEvent(seqrecui::KEY_PRESS) == 1 && seqrecui::layerEvent(seqrecui::KEY_RELEASE) == 0 &&
          seqrecui::layerEvent(seqrecui::KEY_HOLD) == -1);
    CHECK(seqrecui::endText(seqrecui::END_USER) == 0 && seqrecui::endText(seqrecui::END_LOOP) == 0);
    CHECK(seqrecui::endText(seqrecui::END_STOPPED) && seqrecui::endText(seqrecui::END_OFF) && seqrecui::endText(seqrecui::END_RESET));
}

static void test_keys_driver_model()
{
    // a 0.1 s tap: press, release. Held 1 s: hold at 333, 566, 709, 807, 885, 948 (the interval sticks at 63 ms), release.
    std::vector<KeyEv> tap = keyDriver(0, 100);
    CHECK(tap.size() == 2 && tap[0].state == seqrecui::KEY_PRESS && tap[1].state == seqrecui::KEY_RELEASE && tap[1].ms == 100);
    std::vector<KeyEv> hold = keyDriver(1000, 1000);
    CHECK(hold.size() >= 8 && hold[0].ms == 1000 && hold[1].ms == 1333 && hold[1].state == seqrecui::KEY_HOLD);
    CHECK(hold[2].ms == 1566 && hold[3].ms == 1709 && hold[4].ms == 1807);
    CHECK(hold.back().ms == 2000 && hold.back().state == seqrecui::KEY_RELEASE);
}

// ---- MLR: Rec with the real event sequence ----
// v0.2.4 reproduction: the host mapped every non-press state to "Rec up", so the first HOLD event (333 ms after the press) closed
// the take: the loop a row recorded was two or three steps long.
static void test_mlr_rec_v024_bug_reproduced()
{
    using namespace seqcut;
    FakePattern f;
    for (int i = 0; i < 16; i += 4)
        f.map[i] = 1;
    seqcut::Cut* c = freshCut(f);
    CHECK(c != 0);
    if (!c)
        return;
    cutQuantSet(Q_STEP);
    cutUpdate();
    cutRec(false);
    MlrRun run(c, &f);
    std::vector<KeyEv> ev = keyDriver(0, 2000);
    bool takeAtHold = true;
    for (size_t i = 0; i < ev.size(); i++)
    {
        run.runTo(ev[i].ms);
        legacyRec(ev[i].state);
        if (i == 0)
        {
            cutPad(5, 0, true); // the first pad of the row starts its take
            cutPad(5, 0, false);
            seqEngine.idleTick();
            CHECK(c->view(0).flags & VF_REC);
        }
        seqEngine.idleTick();
        if (ev[i].state == seqrecui::KEY_HOLD && ev[i].ms == 333)
            takeAtHold = (c->view(0).flags & VF_REC) != 0;
    }
    run.runTo(2100);
    CHECK(!takeAtHold); // the take was closed by the first hold event: this is the v0.2.4 bug
    CHECK(c->view(0).flags & VF_RECORDED);
    CHECK(c->playhead(0).recordedLength() <= 3);
    cutRec(false);
    cutQuantSet(Q_BEAT);
    cutUpdate();
    freshCut(f);
}

static void test_mlr_rec_toggle_keys_driver()
{
    using namespace seqcut;
    FakePattern f;
    for (int i = 0; i < 16; i += 4)
        f.map[i] = 1;
    seqcut::Cut* c = freshCut(f);
    CHECK(c != 0);
    if (!c)
        return;
    cutQuantSet(Q_STEP); // this test is about the take, not about the grid
    cutUpdate();
    seqEngine.idleTick();
    seqsession::CutSessionLayer a;
    a.enter(0);
    cutRec(false);
    MlrRun run(c, &f);
    Timeline tl;

    // t = 0: Rec pressed and held for 2 s (press, hold events, release). The jam: columns 5, 2, 6 tapped at 50, 650, 1250 ms.
    // Rec is armed by the press and stays armed through every hold event and the release.
    std::vector<KeyEv> k1 = keyDriver(0, 2000);
    bool armedThroughout = true;
    for (size_t i = 0; i < k1.size(); i++)
    {
        const uint8_t st = k1[i].state;
        tl.at(k1[i].ms, [&a, st, &armedThroughout]() {
            hostRec(a, st);
            if (!cutRecHeld())
                armedThroughout = false;
        });
    }
    const int tapAt[3] = { 50, 650, 1250 }, tapCol[3] = { 5, 2, 6 };
    for (int i = 0; i < 3; i++)
    {
        const int col = tapCol[i];
        tl.at(tapAt[i], [&a, col]() { a.pad((uint8_t)col, 0, true); });
        tl.at(tapAt[i] + 50, [&a, col]() { a.pad((uint8_t)col, 0, false); });
    }
    // 2.4 s later, long after the release of the Rec key: the take is still open and Rec armed
    tl.at(2400, [&]() {
        seqEngine.idleTick();
        CHECK(cutRecHeld());
        CHECK(c->view(0).flags & VF_REC);
        CHECK(!(c->view(0).flags & VF_RECORDED));
    });
    // t = 2600: the second press ends the take (a short tap)
    std::vector<KeyEv> k2 = keyDriver(2600, 90);
    for (size_t i = 0; i < k2.size(); i++)
    {
        const uint8_t st = k2[i].state;
        tl.at(k2[i].ms, [&a, st]() { hostRec(a, st); });
    }
    tl.run(run);
    CHECK(armedThroughout);
    seqEngine.idleTick();
    CHECK(!cutRecHeld());
    CHECK(!(c->view(0).flags & VF_REC) && (c->view(0).flags & VF_RECORDED));
    // the take: the boundaries after the first pad (50 ms) and before the closing press (2600 ms) = steps 1..20
    const int n = c->playhead(0).recordedLength();
    CHECK(n == 20);
    const int stepsAtClose = 2600 / 125; // the boundaries before the closing press (the tl ran on to the release, 2690 ms)
    CHECK(stepsAtClose == 20 && run.stepsDone >= stepsAtClose);
    // the gestures of the take loop on: the next two laps repeat what was played during it, step for step
    std::vector<int> take(run.played.begin(), run.played.begin() + std::min(n, (int)run.played.size()));
    run.runTo(2600 + 125 * 2 * n + 125);
    std::vector<int> replay(run.played.begin() + stepsAtClose, run.played.end());
    CHECK((int)take.size() == n && (int)replay.size() >= 2 * n);
    bool same = true;
    for (int i = 0; i < 2 * n && i < (int)replay.size(); i++)
        if (replay[i] != take[i % n] || replay[i] < 0)
            same = false;
    CHECK(same);
    // the jam really is in it: columns 5, 2, 6 are positions 10, 4, 12
    CHECK(std::find(take.begin(), take.end(), 10) != take.end() && std::find(take.begin(), take.end(), 4) != take.end() &&
          std::find(take.begin(), take.end(), 12) != take.end());

    // another press arms again (toggle); the next press closes it; with no pad touched the old loop stays
    hostRec(a, seqrecui::KEY_PRESS);
    hostRec(a, seqrecui::KEY_RELEASE);
    CHECK(cutRecHeld());
    hostRec(a, seqrecui::KEY_PRESS);
    CHECK(!cutRecHeld());
    seqEngine.idleTick();
    CHECK(c->view(0).flags & VF_RECORDED);
    cutRec(false);
    cutQuantSet(Q_BEAT);
    cutUpdate();
    freshCut(f);
}

// "in any group": a take started on a row of another group is closed by the same Rec press
static void test_mlr_rec_takes_in_any_group()
{
    using namespace seqcut;
    FakePattern f;
    f.map[0] = 1;
    seqcut::Cut* c = freshCut(f);
    if (!c)
        return;
    seqsession::CutSessionLayer a;
    a.enter(0);
    cutRec(false);
    hostRec(a, seqrecui::KEY_PRESS);
    hostRec(a, seqrecui::KEY_HOLD);
    hostRec(a, seqrecui::KEY_RELEASE);
    a.pad(3, 0, true); // track 0
    a.pad(3, 0, false);
    a.setGroup(8);
    a.pad(4, 2, true); // track 10
    a.pad(4, 2, false);
    seqEngine.idleTick();
    CHECK((c->view(0).flags & VF_REC) && (c->view(10).flags & VF_REC));
    a.setGroup(4); // looking at another group while the takes run
    int g = 0;
    f.pos = 0;
    for (int i = 0; i < 2; i++)
    {
        stepOnce(*c, f, g);
        c->setStock(10, g); // the second take's row steps too (a take with no step records nothing)
        c->boundary(10, 16, g);
    }
    hostRec(a, seqrecui::KEY_PRESS);
    seqEngine.idleTick();
    CHECK(!(c->view(0).flags & VF_REC) && !(c->view(10).flags & VF_REC));
    CHECK((c->view(0).flags & VF_RECORDED) && (c->view(10).flags & VF_RECORDED));
    // leaving the layer with Rec armed closes it too
    hostRec(a, seqrecui::KEY_PRESS);
    CHECK(cutRecHeld());
    a.leave();
    CHECK(!cutRecHeld());
    freshCut(f);
}

// ---- MLR: jumps, two-pad loop, reverse and rate from the layer, with pad holds in between ----
static void test_mlr_gestures_through_layer()
{
    using namespace seqcut;
    FakePattern f;
    for (int i = 0; i < 16; i += 2)
        f.map[i] = 1;
    seqcut::Cut* c = freshCut(f);
    if (!c)
        return;
    cutQuantSet(Q_STEP);
    cutUpdate();
    seqEngine.idleTick();
    seqsession::CutSessionLayer a;
    a.enter(0);
    MlrRun run(c, &f);
    run.runTo(250);
    // jump: a tap on column 6 of row 0 -> the next boundary plays position 12, then the track runs on from there
    a.pad(6, 0, true);
    a.pad(6, 0, false);
    run.runTo(250 + 125);
    CHECK(run.played.back() == 12);
    run.runTo(250 + 125 * 3);
    CHECK(run.played[run.played.size() - 1] == 14 && run.played[run.played.size() - 2] == 13);
    // two-pad loop (columns 1 and 3 of row 1 = positions 2..7), held for 1 s (the pad hold events are dropped by the host)
    a.pad(1, 1, true);
    run.runTo(run.stepsDone * 125 + 40);
    a.pad(3, 1, true);
    run.runTo(run.stepsDone * 125 + 1000);
    CHECK(c->view(1).loopLo == 1 && c->view(1).loopHi == 3);
    a.pad(1, 1, false);
    a.pad(3, 1, false);
    seqEngine.idleTick();
    CHECK(c->view(1).loopHi < c->view(1).loopLo); // released: back to the whole track
    // reverse (column 9) and rate (column 10) on row 2
    a.pad(COL_REVERSE, 2, true);
    a.pad(COL_REVERSE, 2, false);
    a.pad(COL_RATE, 2, true);
    a.pad(COL_RATE, 2, false);
    seqEngine.idleTick();
    CHECK(c->view(2).flags & VF_REVERSE);
    CHECK(viewRate(c->view(2)) == 2);
    cutQuantSet(Q_BEAT);
    cutUpdate();
    freshCut(f);
}

// ---- MLR: jump quantise ----
static void test_mlr_quantise_screen_button()
{
    using namespace seqcut;
    FakePattern f;
    seqcut::Cut* c = freshCut(f);
    if (!c)
        return;
    seqsession::CutSessionLayer a;
    a.enter(0);
    char lab[12];
    CHECK(kScreenButtonQuant == 4); // screen button 5
    cutQuantSet(Q_BEAT);
    cutUpdate();
    seqEngine.idleTick();
    CHECK(cutQuantMode() == Q_BEAT && c->quant() == Q_BEAT);
    CHECK(a.buttonLabel(kScreenButtonQuant, lab) && strcmp(lab, "Q beat") == 0);
    // a tap cycles: beat -> bar -> off -> step -> beat; the release does nothing; the label shows the value
    const char* want[4] = { "Q bar", "Q off", "Q step", "Q beat" };
    const uint8_t mode[4] = { Q_BAR, Q_OFF, Q_STEP, Q_BEAT };
    for (int i = 0; i < 4; i++)
    {
        CHECK(a.button(kScreenButtonQuant, true));
        CHECK(a.button(kScreenButtonQuant, false));
        seqEngine.idleTick();
        CHECK(a.buttonLabel(kScreenButtonQuant, lab) && strcmp(lab, want[i]) == 0);
        CHECK(cutQuantMode() == mode[i] && c->quant() == mode[i]); // the audio side has it
    }
    // [REC] on button 7 while a take is armed
    cutRec(false);
    CHECK(a.buttonLabel(kScreenButtonRec, lab) && lab[0] == 0);
    a.rec(true);
    CHECK(a.buttonLabel(kScreenButtonRec, lab) && strcmp(lab, "[REC]") == 0);
    a.rec(true);
    CHECK(a.buttonLabel(kScreenButtonRec, lab) && lab[0] == 0);
    CHECK(!a.button(kScreenButtonRec, true) && !a.button(7, true)); // labels, not buttons of the layer
    // packing
    for (uint8_t m = 0; m < Q_COUNT; m++)
    {
        uint8_t arg = packQuant(m);
        CHECK(argIsQuant(arg) && argQuant(arg) == m && !argIsSyncAll(arg) && argSub(arg) < SUB_COUNT);
    }
    CHECK(!argIsQuant(packSyncAll()) && !argIsQuant(packArg(SUB_RESET, false, 0)) && !argIsQuant(packArg(SUB_PAD, false, 3)));
    CHECK(quantSteps(Q_OFF) == 1 && quantSteps(Q_STEP) == 1 && quantSteps(Q_BEAT) == 4 && quantSteps(Q_BAR) == 16);
    // the setting is not lost when the queue is full: kept and sent by the next pass
    for (int i = 0; i < 70; i++)
        seqEngine.postCut(9, packArg(SUB_RESYNC, true, 0)); // fills the queue (inactive track: ignored)
    cutQuantSet(Q_BAR);
    cutQuantSet(Q_OFF);
    seqEngine.idleTick();
    seqEngine.idleTick();
    cutUpdate();
    seqEngine.idleTick();
    CHECK(c->quant() == Q_OFF);
    cutQuantSet(Q_BEAT);
    cutUpdate();
    seqEngine.idleTick();
    CHECK(c->quant() == Q_BEAT);
    freshCut(f);
}

namespace
{
// A pad pressed after `pressAt` boundaries of a 32-step pattern (track 0 not cut-active before). Returns k = the number of boundaries
// after the press at which position `target` was played (1 = the very next boundary), -1 when it never was within 20 boundaries.
// Cut::activate aligns the grid counter to the track's pattern position (the stock position = the global step here).
int landsAt(uint8_t mode, int pressAt, bool releaseEarly, int col = 7, int col2 = -1)
{
    using namespace seqcut;
    FakePattern f;
    f.steps = 32;
    seqcut::Cut* c = freshCut(f);
    if (!c)
        return -2;
    c->apply(0, packQuant(mode));
    int g = 0;
    for (int i = 0; i < pressAt; i++)
        stepOnce(*c, f, g);
    c->apply(0, packArg(SUB_PAD, true, (uint8_t)col));
    if (col2 >= 0)
        c->apply(0, packArg(SUB_PAD, true, (uint8_t)col2));
    if (releaseEarly)
    {
        c->apply(0, packArg(SUB_PAD, false, (uint8_t)col));
        if (col2 >= 0)
            c->apply(0, packArg(SUB_PAD, false, (uint8_t)col2));
    }
    const int target = (col * 32) / 8; // Playhead::columnPosition with 8 columns
    int at = -1;
    for (int k = 1; k <= 20 && at < 0; k++)
    {
        seqcut::StepPlan p = stepOnce(*c, f, g);
        if (p.override && p.pos == target)
            at = k;
    }
    return at;
}
} // namespace

// The boundary logic of the audio side: a queued jump waits for the grid of the mode.
static void test_mlr_quantise_grid()
{
    using namespace seqcut;
    // step / off: the very next boundary, wherever the press was
    for (int pressAt = 1; pressAt <= 6; pressAt++)
    {
        CHECK(landsAt(Q_STEP, pressAt, false) == 1);
        CHECK(landsAt(Q_OFF, pressAt, false) == 1);
    }
    // beat (4 steps): the grid counter is the pattern step; the first step after the press is pressAt + 1
    //   press after step 1 -> steps 2, 3 natural, step 4 jumps (k = 3)
    CHECK(landsAt(Q_BEAT, 1, false) == 3);
    CHECK(landsAt(Q_BEAT, 2, false) == 2);
    CHECK(landsAt(Q_BEAT, 3, false) == 1);
    CHECK(landsAt(Q_BEAT, 4, false) == 4); // just after the beat step: a whole beat to wait
    CHECK(landsAt(Q_BEAT, 5, false) == 3);
    // bar (16 steps)
    CHECK(landsAt(Q_BAR, 1, false) == 15);
    CHECK(landsAt(Q_BAR, 14, false) == 2);
    CHECK(landsAt(Q_BAR, 15, false) == 1);
    // a tap (release before the grid) still jumps: the queued gesture is kept
    CHECK(landsAt(Q_BEAT, 1, true) == 3);
    CHECK(landsAt(Q_BAR, 2, true) == 14);
    // a two-pad loop waits for the grid too: columns 1 and 3 = positions 4..15, the loop starts at its first step
    {
        using namespace seqcut;
        FakePattern f;
        f.steps = 32;
        seqcut::Cut* c = freshCut(f);
        c->apply(0, packQuant(Q_BEAT));
        int g = 0;
        stepOnce(*c, f, g);
        c->apply(0, packArg(SUB_PAD, true, 1));
        c->apply(0, packArg(SUB_PAD, true, 3));
        seqcut::StepPlan p2 = stepOnce(*c, f, g); // g = 2
        seqcut::StepPlan p3 = stepOnce(*c, f, g); // g = 3
        seqcut::StepPlan p4 = stepOnce(*c, f, g); // g = 4: the beat
        CHECK(p2.override && p2.pos == 2 && p3.pos == 3);
        CHECK(p4.override && p4.pos == 4); // loop start (column 1 = position 4)
        CHECK(c->view(0).loopLo == 1 && c->view(0).loopHi == 3);
    }
    // changing the mode while a jump waits: it waits for the new grid (bar -> step: lands at once)
    {
        FakePattern f;
        f.steps = 32;
        seqcut::Cut* c = freshCut(f);
        c->apply(0, packQuant(Q_BAR));
        int g = 0;
        stepOnce(*c, f, g);
        c->apply(0, packArg(SUB_PAD, true, 7));
        stepOnce(*c, f, g);
        c->apply(0, packQuant(Q_STEP));
        seqcut::StepPlan p = stepOnce(*c, f, g);
        CHECK(p.override && p.pos == 28);
    }
    // a track that becomes cut-active later uses the current mode (Cut keeps it, not the playhead)
    {
        FakePattern f;
        seqcut::Cut* c = freshCut(f);
        c->apply(0, packQuant(Q_BAR));
        c->apply(5, packArg(SUB_PAD, true, 2));
        CHECK(c->playhead(5).quantise() == 16);
        c->apply(0, packQuant(Q_BEAT));
        CHECK(c->playhead(5).quantise() == 4 && c->quant() == Q_BEAT);
    }
    // the mode survives a project load (RESET deactivates the rows, not the setting)
    {
        FakePattern f;
        seqcut::Cut* c = freshCut(f);
        c->apply(0, packQuant(Q_BAR));
        seqEngine.resetState();
        seqEngine.idleTick();
        CHECK(c->quant() == Q_BAR);
        c->apply(0, packQuant(Q_STEP));
    }
    cutQuantSet(Q_BEAT);
    cutUpdate();
    seqEngine.idleTick();
    static FakePattern idleFake;
    freshCut(idleFake);
}

// ---- Play mode: the Rec key against the engine and a model of the pattern ----
namespace
{
struct StepM
{
    int note; // -1 empty, -2 note-off, >= 0 note
    int mark; // which take wrote it (0 = was there before)
};
struct PlayFlow
{
    MockHost h;
    Engine* e;
    Head hd;
    seqrecui::RecToggle key;
    std::vector<StepM> pat;
    int takeId = 0;
    int ms = 0;
    uint8_t lastWhy = 0xFF;
    bool stopPending = false;
    PlayFlow() : e(mk(h)), pat(16)
    {
        e->postType(2, TYPE_CHORDARP);
        e->idleTick();
        for (int i = 0; i < 16; i++)
            pat[i] = { 40 + i, 0 }; // a busy pattern: every step holds a note
    }
    ~PlayFlow() { delete e; }
    // the audio side runs `n` uSteps (2.604 ms each); the main loop then takes the recorded steps over (seqTracksUpdate)
    void run(int uSteps)
    {
        for (int i = 0; i < uSteps; i++)
        {
            playRec(*e, h, hd, 1);
            ms += 3;
            drain();
        }
    }
    void drain()
    {
        seqrec::Op op;
        while (e->popRecOp(op))
        {
            StepM& s = pat[op.step];
            if (op.kind == seqrec::OP_OFF_SOFT)
            {
                if (s.note == -1)
                    s = { -2, takeId };
                continue;
            }
            s = { -1, takeId };
            if (op.kind == seqrec::OP_NOTE)
                s.note = op.note;
            else if (op.kind == seqrec::OP_OFF)
                s.note = -2;
        }
        // the overlay's update(): follow the audio side
        if (stopPending && e->postRec(2, Engine::REC_STOP))
            stopPending = false;
        if (key.update((uint32_t)ms, e->recState() != seqrec::Planner::IDLE))
            lastWhy = e->recWhy();
    }
    // the Rec key press as SeqPlayOverlay::recPressed does it
    void recPress(bool shift = false)
    {
        switch (key.press((uint32_t)ms, e->recState() != seqrec::Planner::IDLE))
        {
            case seqrecui::RecToggle::STOP:
                stopPending = !e->postRec(2, Engine::REC_STOP);
                break;
            case seqrecui::RecToggle::START:
                if (!e->postRec(2, shift ? Engine::REC_LOOP : Engine::REC_PUNCH))
                    key.cancel();
                break;
            default: break;
        }
    }
    // the keys driver: one press of downMs, the Play mode router sees every event, only the press does something
    void keyDown(int downMs, bool shift = false)
    {
        std::vector<KeyEv> ev = keyDriver(0, downMs);
        int t = 0;
        for (size_t i = 0; i < ev.size(); i++)
        {
            run((ev[i].ms - t) / 3);
            t = ev[i].ms;
            if (seqrecui::isToggle(ev[i].state))
                recPress(shift);
        }
    }
    int changed() const
    {
        int n = 0;
        for (int i = 0; i < 16; i++)
            if (pat[i].mark != 0)
                n++;
        return n;
    }
};
} // namespace

// The take of a Rec press: starts, survives the hold events and the release of the key, ends with the next press.
static void test_play_rec_key_flow()
{
    PlayFlow p;
    p.run(48 * 3 + 20); // the transport runs, position 3
    p.takeId = 1;
    p.keyDown(1000); // Rec pressed and held for 1 s: press, hold events, release
    CHECK(p.e->recState() == seqrec::Planner::PUNCH);
    CHECK(p.key.on());
    p.run(48 * 3); // 3 more steps: the take is still on
    CHECK(p.e->recState() == seqrec::Planner::PUNCH && p.key.on());
    p.keyDown(80); // the second press ends it
    p.run(2);
    CHECK(p.e->recState() == seqrec::Planner::IDLE && !p.key.on());
    CHECK(p.e->recWhy() == seqrecui::END_USER && p.lastWhy == 0xFF); // asked for: no message
    // the take lasted about 10 steps: the steps that passed were replaced, the rest of the track was not touched
    int ch = p.changed();
    CHECK(ch >= 8 && ch <= 12);
    for (int i = 0; i <= 3; i++)
        CHECK(p.pat[i].mark == 0 && p.pat[i].note == 40 + i); // before the take
}

// "a new take never wipes the whole track": punch a region, then a second region; everything else is untouched
static void test_play_rec_punch_region_only()
{
    PlayFlow p;
    p.run(48 * 4 + 10); // position 4, mid step
    p.takeId = 1;
    p.recPress();
    p.run(2);
    CHECK(p.e->recState() == seqrec::Planner::PUNCH);
    p.run(48 * 4 + 20); // steps 5, 6, 7, 8 pass (position is now 8)
    p.recPress();
    p.run(2);
    CHECK(p.e->recState() == seqrec::Planner::IDLE);
    for (int i = 0; i < 16; i++)
    {
        bool inside = i >= 5 && i <= 8; // the step already playing at the press (4) is left alone, steps 5-8 were entered
        if (inside)
            CHECK(p.pat[i].mark == 1);
        else
            CHECK(p.pat[i].mark == 0 && p.pat[i].note == 40 + i);
    }
    // a second take elsewhere does not touch the first one
    p.run(48 * 6 + 10); // position 14
    p.takeId = 2;
    p.recPress();
    p.run(2);
    p.run(40); // step 15 passes
    p.recPress();
    p.run(2);
    CHECK(p.e->recState() == seqrec::Planner::IDLE);
    for (int i = 5; i <= 8; i++)
        CHECK(p.pat[i].mark == 1);
    CHECK(p.pat[15].mark == 2);
    int untouched = 0;
    for (int i = 0; i < 16; i++)
        if (p.pat[i].mark == 0)
            untouched++;
    CHECK(untouched >= 10);
}

// Rec started from a stopped transport and a bounce of the key (a second press 1 ms later) neither ends nor restarts it
static void test_play_rec_bounce_and_transport()
{
    PlayFlow p;
    p.takeId = 1;
    // key bounce: two presses 1 ms apart
    p.run(48 * 2 + 5);
    p.recPress();
    p.ms += 1;
    p.recPress(); // the bounce
    p.run(48 * 3);
    CHECK(p.e->recState() == seqrec::Planner::PUNCH && p.key.on());
    // the transport stops: the take ends, the UI is told why
    p.e->blockTick(false, 3000);
    CHECK(p.e->recState() == seqrec::Planner::IDLE);
    CHECK(p.e->recWhy() == seqrecui::END_STOPPED);
    p.drain();
    CHECK(!p.key.on() && p.lastWhy == seqrecui::END_STOPPED);
    CHECK(seqrecui::endText(p.lastWhy) != 0);
}

// Shift + Rec: exactly one loop
static void test_play_rec_shift_loop()
{
    PlayFlow p;
    p.run(48 * 5 + 10);
    p.takeId = 1;
    p.keyDown(300, true);
    p.run(2);
    CHECK(p.e->recState() == seqrec::Planner::LOOP || p.e->recState() == seqrec::Planner::ARMED);
    p.run(48 * 40); // well over one lap
    CHECK(p.e->recState() == seqrec::Planner::IDLE);
    CHECK(p.e->recWhy() == seqrecui::END_LOOP);
    CHECK(p.changed() == 16); // exactly the loop: every step once
    CHECK(!p.key.on() && p.lastWhy == 1 /* loop done: expected, the UI shows nothing */);
}

// Why a take ended: the audio side tells
static void test_rec_end_reasons()
{
    MockHost h;
    Engine* e = mk(h);
    e->postType(2, TYPE_CHORDARP);
    e->postType(3, TYPE_CHORDARP);
    e->idleTick();
    Head hd;
    playRec(*e, h, hd, 5);
    e->postRec(2, Engine::REC_PUNCH);
    playRec(*e, h, hd, 2);
    e->postRec(2, Engine::REC_STOP);
    playRec(*e, h, hd, 2);
    CHECK(e->recState() == seqrec::Planner::IDLE && e->recWhy() == seqrecui::END_USER);
    e->postRec(2, Engine::REC_PUNCH);
    playRec(*e, h, hd, 2);
    CHECK(e->recState() == seqrec::Planner::PUNCH);
    e->blockTick(false, 3000);
    CHECK(e->recState() == seqrec::Planner::IDLE && e->recWhy() == seqrecui::END_STOPPED);
    playRec(*e, h, hd, 2);
    e->postRec(2, Engine::REC_PUNCH);
    playRec(*e, h, hd, 2);
    CHECK(e->recState() == seqrec::Planner::PUNCH);
    e->postType(2, TYPE_OFF);
    playRec(*e, h, hd, 48 * 16 * 2); // the change applies at the next bar start of the running transport
    CHECK(e->recState() == seqrec::Planner::IDLE && e->recWhy() == seqrecui::END_OFF);
    e->postRec(3, Engine::REC_PUNCH);
    playRec(*e, h, hd, 2);
    CHECK(e->recState() == seqrec::Planner::PUNCH);
    e->resetState();
    CHECK(e->recState() == seqrec::Planner::IDLE && e->recWhy() == seqrecui::END_RESET);
    delete e;
}
