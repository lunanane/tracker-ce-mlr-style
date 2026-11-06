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
            tCutPad(5, 0, true); // the first pad of the row starts its take
            tCutPad(5, 0, false);
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
        tl.at(tapAt[i], [&a, col]() { tAPad(a, (uint8_t)col, 0, true); });
        tl.at(tapAt[i] + 50, [&a, col]() { tAPad(a, (uint8_t)col, 0, false); });
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
    tAPad(a, 3, 0, true); // track 0
    tAPad(a, 3, 0, false);
    tSetGroup(8);
    tAPad(a, 4, 2, true); // track 10
    tAPad(a, 4, 2, false);
    seqEngine.idleTick();
    CHECK((c->view(0).flags & VF_REC) && (c->view(10).flags & VF_REC));
    tSetGroup(4); // looking at another group while the takes run
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
    tAPad(a, 6, 0, true);
    tAPad(a, 6, 0, false);
    run.runTo(250 + 125);
    CHECK(run.played.back() == 12);
    run.runTo(250 + 125 * 3);
    CHECK(run.played[run.played.size() - 1] == 14 && run.played[run.played.size() - 2] == 13);
    // two-pad loop (columns 1 and 3 of row 1 = positions 2..7), held for 1 s (the pad hold events are dropped by the host)
    tAPad(a, 1, 1, true);
    run.runTo(run.stepsDone * 125 + 40);
    tAPad(a, 3, 1, true);
    run.runTo(run.stepsDone * 125 + 1000);
    CHECK(c->view(1).loopLo == 1 && c->view(1).loopHi == 3);
    tAPad(a, 1, 1, false);
    tAPad(a, 3, 1, false);
    seqEngine.idleTick();
    CHECK(c->view(1).loopHi < c->view(1).loopLo); // released: back to the whole track
    // reverse (column 9) and rate (column 10) on row 2
    tAPad(a, COL_REVERSE, 2, true);
    tAPad(a, COL_REVERSE, 2, false);
    tAPad(a, COL_RATE, 2, true);
    tAPad(a, COL_RATE, 2, false);
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

// ---- plan 008 A (owner review): latched loops, takes on the grid, quantise off = at once, bar follows the pattern ----
static void test_mlr_loop_latches()
{
    using namespace seqcut;
    FakePattern f;
    f.steps = 32;
    seqcut::Cut* c = freshCut(f);
    if (!c)
        return;
    c->apply(0, packQuant(Q_STEP));
    int g = 0;
    stepOnce(*c, f, g);
    stepOnce(*c, f, g);
    c->apply(0, packArg(SUB_PAD, true, 1)); // columns 1..3 = positions 4..15
    c->apply(0, packArg(SUB_PAD, true, 3));
    c->apply(0, packArg(SUB_PAD, false, 1)); // both up before the boundary: the loop is still wanted
    c->apply(0, packArg(SUB_PAD, false, 3));
    StepPlan p = stepOnce(*c, f, g);
    CHECK(p.override && p.pos == 4);
    for (int i = 0; i < 40; i++)
    {
        p = stepOnce(*c, f, g);
        CHECK(p.override && p.pos >= 4 && p.pos <= 15); // latched: it turns with no pad down
    }
    CHECK(c->playhead(0).heldCount() == 0 && c->view(0).loopLo == 1 && c->view(0).loopHi == 3);
    // one pad in the row jumps and ends the loop
    c->apply(0, packArg(SUB_PAD, true, 6));
    c->apply(0, packArg(SUB_PAD, false, 6));
    p = stepOnce(*c, f, g);
    CHECK(p.override && p.pos == 24 && c->view(0).loopLo > c->view(0).loopHi);
    p = stepOnce(*c, f, g);
    CHECK(p.pos == 25);
    // RESYNC ends a latched loop too
    c->apply(0, packArg(SUB_PAD, true, 1));
    c->apply(0, packArg(SUB_PAD, true, 3));
    c->apply(0, packArg(SUB_PAD, false, 1));
    c->apply(0, packArg(SUB_PAD, false, 3));
    stepOnce(*c, f, g);
    CHECK(c->view(0).loopLo == 1);
    c->apply(0, packArg(SUB_RESYNC, true, 0));
    p = stepOnce(*c, f, g);
    CHECK(c->view(0).loopLo > c->view(0).loopHi && !c->playhead(0).looping());
}

static void test_mlr_take_snaps_to_the_grid()
{
    using namespace seqcut;
    FakePattern f;
    seqcut::Cut* c = freshCut(f);
    if (!c)
        return;
    c->apply(0, packQuant(Q_BEAT));
    int g = 0;
    stepOnce(*c, f, g); // global 1
    c->apply(0, packArg(SUB_REC, true, 0));
    c->apply(0, packArg(SUB_PAD, true, 6)); // position 12, waits for the beat at 4
    c->apply(0, packArg(SUB_PAD, false, 6));
    StepPlan p = stepOnce(*c, f, g); // 2
    CHECK(p.pos == 2 && (c->view(0).flags & VF_REC) && c->playhead(0).recordedLength() == 0);
    p = stepOnce(*c, f, g); // 3
    CHECK(p.pos == 3);
    p = stepOnce(*c, f, g); // 4: applied, the take starts
    CHECK(p.pos == 12);
    p = stepOnce(*c, f, g); // 13
    CHECK(p.pos == 13);
    c->apply(0, packArg(SUB_REC, false, 0)); // Rec off in the middle of a beat: the take runs on to the grid
    CHECK(c->view(0).flags & VF_REC);
    p = stepOnce(*c, f, g); // 14
    CHECK(p.pos == 14 && (c->view(0).flags & VF_REC));
    p = stepOnce(*c, f, g); // 15 and the next step is the grid: closed
    CHECK(p.pos == 15);
    CHECK(!(c->view(0).flags & VF_REC) && (c->view(0).flags & VF_RECORDED) && c->playhead(0).recordedLength() == 4);
    // the replay starts with the take's first step and keeps its phase
    for (int lap = 0; lap < 3; lap++)
        for (int i = 0; i < 4; i++)
        {
            p = stepOnce(*c, f, g);
            CHECK(p.override && p.pos == 12 + i);
        }
}

static void test_mlr_quantise_off_is_at_once()
{
    using namespace seqcut;
    FakePattern f;
    seqcut::Cut* c = freshCut(f);
    if (!c)
        return;
    c->apply(0, packQuant(Q_OFF));
    int g = 0;
    for (int i = 0; i < 3; i++)
        stepOnce(*c, f, g);
    StepPlan pl;
    c->apply(0, packArg(SUB_PAD, true, 6));
    CHECK(c->now(0, 16, pl) && pl.override && pl.pos == 12); // between two steps, no boundary needed
    CHECK(!c->now(0, 16, pl));                                // consumed
    f.pos = 12;
    StepPlan p = stepOnce(*c, f, g);                          // the boundary carries on from there, no second jump
    CHECK(p.override && p.pos == 13);
    // a two-pad loop starts at once as well
    c->apply(0, packArg(SUB_PAD, false, 6));
    c->apply(0, packArg(SUB_PAD, true, 1));
    c->apply(0, packArg(SUB_PAD, true, 3));
    CHECK(c->now(0, 16, pl) && pl.pos == 2 && c->view(0).loopLo == 1 && c->view(0).loopHi == 3);
    // step = the next boundary, never in between
    freshCut(f);
    c->apply(0, packQuant(Q_STEP));
    c->apply(0, packArg(SUB_PAD, true, 6));
    CHECK(!c->now(0, 16, pl));
    p = stepOnce(*c, f, g);
    CHECK(p.pos == 12);
    // a stopped track does not play it
    freshCut(f);
    c->apply(0, packQuant(Q_OFF));
    c->apply(0, packArg(SUB_STOP, true, 0));
    c->apply(0, packArg(SUB_PAD, true, 6));
    CHECK(!c->now(0, 16, pl));
    c->apply(0, packQuant(Q_BEAT));
}

static void test_mlr_bar_follows_the_pattern()
{
    using namespace seqcut;
    CHECK(quantStepsFor(Q_BAR, 16) == 16 && quantStepsFor(Q_BAR, 32) == 16 && quantStepsFor(Q_BAR, 128) == 16);
    CHECK(quantStepsFor(Q_BAR, 24) == 24 && quantStepsFor(Q_BAR, 12) == 12 && quantStepsFor(Q_BAR, 100) == 64);
    CHECK(quantStepsFor(Q_BEAT, 24) == 4 && quantStepsFor(Q_STEP, 24) == 1 && quantStepsFor(Q_OFF, 24) == 1);
    // a 24 step pattern: the bar is the whole pattern, so a jump waits for its start (stock position 0), not for a drifting 16
    FakePattern f;
    f.steps = 24;
    seqcut::Cut* c = freshCut(f);
    if (!c)
        return;
    c->apply(0, packQuant(Q_BAR));
    int g = 0;
    for (int i = 0; i < 10; i++)
        stepOnce(*c, f, g);
    c->apply(0, packArg(SUB_PAD, true, 3)); // pads of 2 steps: 3 -> position 6 (was column 2 of 8 over 24 steps)
    c->apply(0, packArg(SUB_PAD, false, 3));
    CHECK(c->playhead(0).quantise() == 24);
    int landed = -1;
    for (int k = 1; k <= 20; k++)
    {
        StepPlan p = stepOnce(*c, f, g);
        if (p.override && p.pos == 6)
        {
            landed = k;
            break;
        }
    }
    CHECK(landed == 14); // global positions 11..23 pass, 0 is the pattern start
    c->apply(0, packQuant(Q_BEAT));
}

// ---- v0.3.3: a live note sounds like the step it records ("the sequencers are always too loud, recorded they sound right") ----
// The stock option "record velocity" decides whether a recorded step carries a volume. Off: the step has none (instrument default,
// -1) and the live note must have none either; on: the live note's volume is the recorded one (a chord: the loudest of the group).
// Every sequencer type plays through Engine::onEvent, so the pad-driven ones (Chord/Arp, Strings) stand for all of them.
static void test_live_velocity_matches_recorded()
{
    // the rule itself
    CHECK(liveVolume(79, true) == 79 && liveVolume(79, false) == -1 && liveVolume(0, false) == -1 && liveVolume(0, true) == 0);
    static const uint8_t kTypes[2] = { TYPE_CHORDARP, TYPE_PLINKY };
    for (int ti = 0; ti < 2; ti++)
        for (int on = 0; on < 2; on++)
        {
            MockHost h;
            h.prm.velocity = on != 0;
            Engine* e = mk(h);
            Head hd;
            e->postType(2, kTypes[ti]);
            e->idleTick();
            for (int i = 0; i < 48 * 3; i++) { e->blockTick(true, 2604); playRec(*e, h, hd, 1); }
            e->postRec(2, Engine::REC_PUNCH);
            for (int i = 0; i < 6; i++) { e->blockTick(true, 2604); playRec(*e, h, hd, 1); }
            e->postPad(2, 0, true, 37);
            for (int i = 0; i < 48 * 4; i++) { e->blockTick(true, 2604); playRec(*e, h, hd, 1); }
            e->postPad(2, 0, false);
            for (int i = 0; i < 48 * 2; i++) { e->blockTick(true, 2604); playRec(*e, h, hd, 1); }
            std::vector<seqrec::Op> ops = drainOps(*e);
            int live = 0, rec = 0;
            for (size_t i = 0; i < h.log.size(); i++)
            {
                if (h.log[i].kind != '+' || h.log[i].track != 2)
                    continue;
                live++;
                if (!on)
                    CHECK(h.log[i].vol == -1); // the instrument's default, like the recorded step
                else
                    CHECK(h.log[i].vol >= 0 && (kTypes[ti] != TYPE_PLINKY || h.log[i].vol == velocityToVolume(37)));
            }
            for (size_t i = 0; i < ops.size(); i++)
            {
                if (ops[i].kind != seqrec::OP_NOTE)
                    continue;
                rec++;
                bool found = false;
                for (size_t k = 0; k < h.log.size(); k++)
                    if (h.log[k].kind == '+' && h.log[k].track == 2 && h.log[k].vol == ops[i].volume)
                        found = true;
                CHECK(found);                    // the recorded volume is one a live note had (a chord step: the loudest of its notes)
                CHECK(on ? ops[i].volume >= 0 : ops[i].volume == -1);
            }
            CHECK(live > 0 && rec > 0);
            delete e;
        }
}

// ---- Play mode: the Rec key against the engine and a model of the pattern ----
// Plan 008 (owner intent "Rec should just record"): a take replaces a step only while the sequencer is played on the track; steps
// the take passes while nothing is played keep what the pattern holds, lap after lap.
namespace
{
struct StepM
{
    int note;  // -1 empty, -2 note-off, >= 0 note
    int mark;  // which take wrote it last (0 = was there before)
};
struct PlayFlow
{
    MockHost h;
    Engine* e;
    Head hd;
    seqrecui::RecToggle key;
    std::vector<StepM> pat;
    int writes[16];
    int takeId = 0;
    int ms = 0;
    uint8_t lastWhy = 0xFF;
    bool stopPending = false;
    PlayFlow() : e(mk(h)), pat(16)
    {
        e->postType(2, TYPE_CHORDARP);
        e->idleTick();
        for (int i = 0; i < 16; i++)
        {
            pat[i] = { 40 + i, 0 }; // a busy pattern: every step holds a note
            writes[i] = 0;
        }
    }
    ~PlayFlow() { delete e; }
    // the audio side runs `n` uSteps (2.604 ms each) the way Sequencer::tick does (advanceUs, blockTick, stepClock); the main loop
    // then takes the recorded steps over (seqTracksUpdate)
    void run(int uSteps)
    {
        for (int i = 0; i < uSteps; i++)
        {
            e->blockTick(true, 2604); // the transport runs: drains the commands, no idle logic
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
            writes[op.step]++;
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
    void pad(bool down) { e->postPad(2, 0, down); }
    int changed() const
    {
        int n = 0;
        for (int i = 0; i < 16; i++)
            if (pat[i].mark != 0)
                n++;
        return n;
    }
    int totalWrites() const
    {
        int n = 0;
        for (int i = 0; i < 16; i++)
            n += writes[i];
        return n;
    }
};
} // namespace

// The take of a Rec press: starts, survives the hold events and the release of the key, ends with the next press. A chord is
// played in the middle of it: exactly the steps that passed while it was played are replaced.
static void test_play_rec_key_flow()
{
    PlayFlow p;
    p.run(48 * 3 + 20); // the transport runs, position 3
    p.takeId = 1;
    p.keyDown(400); // Rec pressed and held for 0.4 s: press, one hold event (333 ms), release
    CHECK(p.e->recState() == seqrec::Planner::PUNCH);
    CHECK(p.key.on());
    CHECK(p.totalWrites() == 0); // nothing played yet: the take has not touched the pattern
    p.pad(true);                 // the chord is played for about 5 steps ...
    p.run(48 * 5);
    p.pad(false);
    p.run(48 * 2); // ... and let go; the take goes on, idle
    CHECK(p.e->recState() == seqrec::Planner::PUNCH && p.key.on());
    p.keyDown(80); // the second press ends it
    p.run(2);
    CHECK(p.e->recState() == seqrec::Planner::IDLE && !p.key.on());
    CHECK(p.e->recWhy() == seqrecui::END_USER && p.lastWhy == 0xFF); // asked for: no message
    // steps 0-3 are before the take, steps 14-15 after the played part: untouched; the played part is written
    for (int i = 0; i <= 3; i++)
        CHECK(p.pat[i].mark == 0 && p.pat[i].note == 40 + i);
    int ch = p.changed();
    CHECK(ch >= 4 && ch <= 8);
    for (int i = 0; i < 16; i++)
        if (p.pat[i].mark == 0)
            CHECK(p.pat[i].note == 40 + i && p.writes[i] == 0);
    CHECK(p.pat[14].mark == 0 && p.pat[15].mark == 0);
}

// A take that plays nothing changes nothing, lap after lap (the v0.2.4 take cleared every step it entered: "Rec just doesn't do its
// job, a new recording overwrites the old one completely")
static void test_play_rec_idle_take_changes_nothing()
{
    PlayFlow p;
    p.run(48 * 3 + 20);
    p.takeId = 1;
    p.recPress();
    p.run(48 * 16 * 3); // three laps
    CHECK(p.e->recState() == seqrec::Planner::PUNCH);
    p.recPress();
    p.run(2);
    CHECK(p.e->recState() == seqrec::Planner::IDLE);
    CHECK(p.changed() == 0 && p.totalWrites() == 0);
}

// Notes that cross the loop wrap are kept when the next lap is idle ("from the 2nd lap on a step is only rewritten if it gets live input")
static void test_play_rec_wrap_keeps_lap1()
{
    PlayFlow p;
    p.run(48 * 12 + 10); // position 12
    p.takeId = 1;
    p.recPress();
    p.run(2);
    p.pad(true); // played from step 13 over the wrap into step 1-2
    p.run(48 * 5);
    p.pad(false);
    p.run(10);
    int w1[16];
    int tot1 = p.totalWrites();
    CHECK(tot1 > 0);
    StepM snap[16];
    for (int i = 0; i < 16; i++)
    {
        w1[i] = p.writes[i];
        snap[i] = p.pat[i];
    }
    CHECK(w1[13] > 0 || w1[14] > 0);       // before the wrap
    CHECK(w1[0] > 0 || w1[1] > 0);         // after the wrap
    p.run(48 * 16 * 2);                    // two idle laps with the take still on
    CHECK(p.e->recState() == seqrec::Planner::PUNCH);
    for (int i = 0; i < 16; i++)
        CHECK(p.writes[i] == w1[i] && p.pat[i].note == snap[i].note && p.pat[i].mark == snap[i].mark);
}

// A gap inside live playing is replaced (the arp pauses, the pattern's old note must not sound through it)
static void test_play_rec_live_gap_replaced()
{
    PlayFlow p;
    p.run(48 * 2 + 10);
    p.takeId = 1;
    p.recPress();
    p.run(2);
    p.pad(true); // hold a pad for ten steps: every step that passes is part of the take
    p.run(48 * 10);
    p.pad(false);
    p.recPress();
    p.run(2);
    int replaced = 0;
    for (int i = 3; i <= 11; i++)
        if (p.pat[i].mark == 1)
            replaced++;
    CHECK(replaced >= 8); // steps with a note and the steps in between (a slow arp leaves gaps) were all replaced
    CHECK(p.pat[0].mark == 0 && p.pat[1].mark == 0); // (step 2 was playing at the press: a note may land on it)
    CHECK(p.pat[14].mark == 0 && p.pat[15].mark == 0);
}

// Muting is a monitoring choice: a muted track still records what the pads play (and sounds nothing)
static void test_play_rec_muted_track_records()
{
    PlayFlow p;
    p.h.mute[2] = true;
    p.run(48 * 2 + 10);
    p.takeId = 1;
    p.recPress();
    p.run(2);
    p.pad(true);
    p.run(48 * 4);
    p.pad(false);
    p.run(48);
    p.recPress();
    p.run(2);
    CHECK(p.h.count('+', 2) == 0); // silent
    int notes = 0;
    for (int i = 0; i < 16; i++)
        if (p.pat[i].mark == 1 && p.pat[i].note >= 0)
            notes++;
    CHECK(notes >= 1);
    CHECK(p.pat[0].mark == 0 && p.pat[1].mark == 0); // (step 2 was playing at the press: a note may land on it)
}

// The pattern changes under a take: it ends with its own reason, "pattern changed"
static void test_play_rec_pattern_changed_reason()
{
    PlayFlow p;
    p.run(48 * 2 + 10);
    p.recPress();
    p.run(2);
    CHECK(p.e->recState() == seqrec::Planner::PUNCH);
    p.e->postRec(2, Engine::REC_STOP_PATTERN); // what seqTracksUpdate posts when mtProject.values.actualPattern moved
    p.run(2);
    CHECK(p.e->recState() == seqrec::Planner::IDLE && p.e->recWhy() == seqrecui::END_PATTERN);
    CHECK(!p.key.on() && p.lastWhy == seqrecui::END_PATTERN);
    CHECK(seqrecui::endText(seqrecui::END_PATTERN) != 0);
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

// Shift + Rec: exactly one loop. Idle: nothing written. Played through the whole loop: every step once.
static void test_play_rec_shift_loop()
{
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
        CHECK(p.changed() == 0); // nothing was played: the loop is as it was
        CHECK(!p.key.on() && p.lastWhy == seqrecui::END_LOOP && seqrecui::endText(p.lastWhy) == 0);
    }
    {
        PlayFlow p;
        p.run(48 * 5 + 10);
        p.takeId = 1;
        p.keyDown(300, true);
        p.run(2);
        p.pad(true); // a chord held across the whole loop
        p.run(48 * 40);
        CHECK(p.e->recState() == seqrec::Planner::IDLE && p.e->recWhy() == seqrecui::END_LOOP);
        CHECK(p.changed() == 16); // exactly the loop: every step replaced
    }
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
