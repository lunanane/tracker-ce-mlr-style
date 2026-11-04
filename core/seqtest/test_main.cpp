// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
#include "seqTracksCore.h"
#include <stdio.h>
#include <string.h>
#include <vector>

using namespace seqtracks;
using namespace seqlogic;

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                                    \
    do                                                                              \
    {                                                                               \
        g_checks++;                                                                 \
        if (!(c))                                                                   \
        {                                                                           \
            g_fail++;                                                               \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);                     \
        }                                                                           \
    } while (0)

struct Rec
{
    char kind; // B begin, E end, + on, - off
    uint8_t track, note;
    int8_t vol;
};
struct MockHost : Host
{
    std::vector<Rec> log;
    bool mute[16];
    MockHost()
    {
        for (int i = 0; i < 16; i++)
            mute[i] = false;
    }
    void takeoverBegin(uint8_t t) { log.push_back({ 'B', t, 0, 0 }); }
    void takeoverEnd(uint8_t t) { log.push_back({ 'E', t, 0, 0 }); }
    void noteOn(uint8_t t, uint8_t n, int8_t v) { log.push_back({ '+', t, n, v }); }
    void noteOff(uint8_t t, uint8_t n) { log.push_back({ '-', t, n, 0 }); }
    bool muted(uint8_t t) { return mute[t]; }
    seqrec::TrackPos tp = { 0, 1, 15 };
    bool sample = false;
    uint8_t instr = 3;
    seqrec::Params prm = { true, true };
    void trackPos(uint8_t, seqrec::TrackPos& o) { o = tp; }
    bool sampleTrack(uint8_t) { return sample; }
    uint8_t instrument(uint8_t) { return instr; }
    seqrec::Params recParams() { return prm; }
    int count(char k, int track = -1) const
    {
        int n = 0;
        for (size_t i = 0; i < log.size(); i++)
            if (log[i].kind == k && (track < 0 || log[i].track == track))
                n++;
        return n;
    }
};

static Engine* mk(MockHost& h)
{
    Engine* e = new Engine();
    e->setHost(&h);
    return e;
}

// run n uSteps of a playing transport (48 uSteps per step) starting at uStep 1
static void playUSteps(Engine& e, int n, int& uStep)
{
    for (int i = 0; i < n; i++)
    {
        e.stepClock((uint16_t)uStep);
        e.advanceUs(2604); // 120 bpm: 8 uSteps = 1 pulse = 20.8 ms
        uStep = uStep % 48 + 1;
    }
}
static void idle(Engine& e, int ticks)
{
    for (int i = 0; i < ticks; i++)
    {
        e.idleTick();
        if (i % 96 == 0)
            e.advanceUs(20833); // 120 bpm: 96 nano ticks = 1 pulse
    }
}

static void test_tap()
{
    TapDetector t;
    t.down(true);
    CHECK(t.up());
    t.down(true);
    t.other();
    CHECK(!t.up()); // Pattern + arrow
    t.down(false);
    CHECK(!t.up()); // another button was already held
    CHECK(!t.up()); // release without press
    t.other();
    t.down(true);
    CHECK(t.up()); // activity before the press does not matter
}

static void test_logic()
{
    CHECK(trackFromScreenButton(0, false) == 0 && trackFromScreenButton(7, false) == 7);
    CHECK(trackFromScreenButton(0, true) == 8 && trackFromScreenButton(7, true) == 15);
    CHECK(stepTypeSelection(0, -1) == 0 && stepTypeSelection(0, 3) == 1 && stepTypeSelection(2, 1) == 2);
    CHECK(velocityToVolume(127) == 100 && velocityToVolume(0) == 0 && velocityToVolume(64) == 50);
    CHECK(padCol(13) == 1 && padRow(13) == 1 && padCol(47) == 11 && padRow(47) == 3);
    CHECK(wantTakeover(TYPE_CHORDARP, 1, 0, false, false));
    CHECK(wantTakeover(TYPE_CHORDARP, 0, 2, false, true));
    CHECK(!wantTakeover(TYPE_CHORDARP, 0, 0, false, true));
    CHECK(wantTakeover(TYPE_MONO, 0, 0, true, true));
    CHECK(!wantTakeover(TYPE_MONO, 0, 0, true, false));
    CHECK(!wantTakeover(TYPE_MONO, 0, 0, false, true));
    CHECK(!wantTakeover(TYPE_OFF, 5, 5, true, true));
}

static void test_off_is_inert()
{
    MockHost h;
    Engine* e = mk(h);
    CHECK(!e->active());
    int u = 1;
    playUSteps(*e, 48 * 8, u);
    idle(*e, 500);
    CHECK(h.log.empty());
    e->postPad(0, 5, true); // pad on an Off track
    CHECK(e->active());     // queued
    e->idleTick();
    CHECK(!e->active() && h.log.empty() && !e->takenOver(0));
    delete e;
}

static void test_chord_takeover_stopped()
{
    MockHost h;
    Engine* e = mk(h);
    e->postType(2, TYPE_CHORDARP);
    e->idleTick();
    CHECK(e->type(2) == TYPE_CHORDARP);
    CHECK(!e->takenOver(2));
    e->postPad(2, 0, true); // root pad (col 0, row 0)
    idle(*e, 96 * 48);
    CHECK(h.count('+', 2) >= 1); // the root sounds
    CHECK(e->takenOver(2));
    CHECK(h.count('B', 2) == 1);
    e->postPad(2, 0, false);
    idle(*e, 96 * 48);
    idle(*e, 96 * 24);
    CHECK(!e->takenOver(2));
    CHECK(h.count('E', 2) == 1);
    CHECK(e->soundingCount(2) == 0);
    CHECK(h.count('+', 2) == h.count('-', 2)); // nothing left hanging
    delete e;
}

static void test_chord_takeover_playing_and_pattern_returns()
{
    MockHost h;
    Engine* e = mk(h);
    e->postType(0, TYPE_CHORDARP);
    int u = 1;
    playUSteps(*e, 10, u);
    e->postPad(0, 0, true);
    playUSteps(*e, 48 * 4, u);
    CHECK(e->takenOver(0));
    CHECK(h.count('+', 0) > 0);
    e->postPad(0, 0, false);
    for (int i = 0; i < 20; i++)
    {
        playUSteps(*e, 48, u);
        e->advanceUs(125000);
    }
    CHECK(!e->takenOver(0)); // pattern is back
    CHECK(h.count('+', 0) == h.count('-', 0));
    delete e;
}

static void test_mono_follows_transport()
{
    MockHost h;
    Engine* e = mk(h);
    e->postType(8, TYPE_MONO);
    e->idleTick();
    e->postPad(8, 0 + 3 * 12, true); // step 1, bottom degree row
    e->postPad(8, 0 + 3 * 12, false);
    e->idleTick();
    CHECK(e->mono(8).stepAt(1) != seqgen::Mono::kNoStep);
    CHECK(!e->takenOver(8)); // stopped: pattern track untouched
    int u = 1;
    playUSteps(*e, 48 * 2, u);
    CHECK(e->takenOver(8));
    CHECK(h.count('+', 8) >= 1);
    idle(*e, 10); // transport stops
    CHECK(!e->takenOver(8));
    CHECK(h.count('+', 8) == h.count('-', 8));
    // clearing the sequence ends the takeover while playing
    u = 1;
    playUSteps(*e, 48, u);
    CHECK(e->takenOver(8));
    e->postPad(8, 11 + 0 * 12, true); // CLEAR
    e->postPad(8, 11 + 0 * 12, false);
    playUSteps(*e, 48, u);
    CHECK(!e->takenOver(8));
    CHECK(h.count('+', 8) == h.count('-', 8));
    delete e;
}

static void test_type_change_next_bar()
{
    MockHost h;
    Engine* e = mk(h);
    e->postType(1, TYPE_CHORDARP);
    int u = 1;
    playUSteps(*e, 5, u); // playing, mid first bar
    e->postType(1, TYPE_MONO);
    playUSteps(*e, 48 * 4, u); // 4 steps = 1 beat of the bar
    CHECK(e->type(1) == TYPE_CHORDARP);
    CHECK(e->pendingType(1) == TYPE_MONO);
    playUSteps(*e, 48 * 16, u); // past the bar line
    CHECK(e->type(1) == TYPE_MONO && e->pendingType(1) == 0xFF);
    // choosing the current type again cancels a pending change
    e->postType(1, TYPE_OFF);
    playUSteps(*e, 3, u);
    CHECK(e->pendingType(1) == TYPE_OFF);
    e->postType(1, TYPE_MONO);
    playUSteps(*e, 3, u);
    CHECK(e->pendingType(1) == 0xFF);
    // stopped: immediate
    idle(*e, 5);
    e->postType(1, TYPE_OFF);
    idle(*e, 2);
    CHECK(e->type(1) == TYPE_OFF);
    delete e;
}

static void test_type_change_releases_notes_and_pads()
{
    MockHost h;
    Engine* e = mk(h);
    e->postType(3, TYPE_CHORDARP);
    idle(*e, 1);
    e->postPad(3, 0, true);
    idle(*e, 96 * 24);
    CHECK(e->takenOver(3));
    e->postType(3, TYPE_OFF); // stopped: immediate
    idle(*e, 2);
    CHECK(!e->takenOver(3));
    CHECK(h.count('+', 3) == h.count('-', 3));
    e->postPad(3, 0, false); // stray release is harmless
    idle(*e, 2);
    CHECK(!e->active());
    delete e;
}

static void test_muted_track_is_silent()
{
    MockHost h;
    h.mute[4] = true;
    Engine* e = mk(h);
    e->postType(4, TYPE_CHORDARP);
    idle(*e, 1);
    e->postPad(4, 0, true);
    idle(*e, 96 * 48);
    CHECK(h.count('+', 4) == 0);
    delete e;
}

static void test_scale_comes_from_first_chordarp()
{
    MockHost h;
    Engine* e = mk(h);
    e->postType(5, TYPE_MONO);
    e->postType(6, TYPE_CHORDARP);
    e->postType(2, TYPE_CHORDARP);
    idle(*e, 2);
    CHECK(e->context().chord == &e->chordArp(2));
    e->postType(2, TYPE_OFF);
    idle(*e, 2);
    CHECK(e->context().chord == &e->chordArp(6));
    e->postType(6, TYPE_OFF);
    idle(*e, 2);
    CHECK(e->context().chord == 0);
    delete e;
}

static void test_ring_overflow_drops()
{
    MockHost h;
    Engine* e = mk(h);
    int accepted = 0;
    for (int i = 0; i < 200; i++)
        if (e->postType(0, TYPE_OFF))
            accepted++;
    CHECK(accepted == 63);
    e->idleTick();
    CHECK(e->postType(0, TYPE_OFF));
    delete e;
}


// ---------------------------------------------------------------- record planner
struct OpLog
{
    std::vector<seqrec::Op> ops;
    static void sink(void* u, const seqrec::Op& o) { static_cast<OpLog*>(u)->ops.push_back(o); }
    int count(uint8_t kind) const
    {
        int n = 0;
        for (size_t i = 0; i < ops.size(); i++)
            if (ops[i].kind == kind)
                n++;
        return n;
    }
    // last op for a step
    const seqrec::Op* at(uint8_t step) const
    {
        for (size_t i = ops.size(); i-- > 0;)
            if (ops[i].step == step)
                return &ops[i];
        return 0;
    }
};

static void test_chord_lookup()
{
    uint8_t idx;
    uint8_t major[] = { 60, 64, 67 };
    CHECK(seqrec::chordLookup(major, 3, idx) == 3 && idx == 5);
    CHECK(chordDef[idx] == 0b10010001);
    uint8_t minor[] = { 60, 63, 67 };
    CHECK(seqrec::chordLookup(minor, 3, idx) == 3 && idx == 4);
    uint8_t five[] = { 60, 64, 67, 71, 74 }; // maj9: no 5-note entry, 60 64 67 71 exists
    CHECK(seqrec::chordLookup(five, 5, idx) == 4 && chordDef[idx] == 0b100010010001);
    uint8_t wide[] = { 60, 64, 67, 76 }; // 16 semitones: the top note drops
    CHECK(seqrec::chordLookup(wide, 4, idx) == 3 && idx == 5);
    uint8_t odd[] = { 60, 61 }; // minor second is not in the table: root only
    CHECK(seqrec::chordLookup(odd, 2, idx) == 1 && idx == seqrec::kNoChord);
    uint8_t one[] = { 72 };
    CHECK(seqrec::chordLookup(one, 1, idx) == 1 && idx == seqrec::kNoChord);
    uint8_t sus[] = { 60, 65, 67 }; // sus4 = 0,5,7 -> mask 0b10100001 (idx 7)
    CHECK(seqrec::chordLookup(sus, 3, idx) == 3 && idx == 7);
    // every table entry 1..MAX_CHORD is found again from its own intervals
    for (int c = 1; c <= MAX_CHORD; c++)
    {
        uint8_t n[16];
        int k = 0;
        for (int b = 0; b < 15; b++)
            if (chordDef[c] & (1u << b))
                n[k++] = (uint8_t)(48 + b);
        uint8_t i2;
        CHECK(seqrec::chordLookup(n, k, i2) == k && chordDef[i2] == chordDef[c]);
    }
}

static seqrec::Params P(bool quantize = true, bool velocity = true)
{
    seqrec::Params p = { quantize, velocity };
    return p;
}
static seqrec::TrackPos TP(uint8_t pos, uint8_t u, uint8_t last = 15)
{
    seqrec::TrackPos t = { pos, u, last };
    return t;
}

static void test_planner_punch_replace()
{
    OpLog log;
    seqrec::Planner pl;
    pl.setSink(&OpLog::sink, &log);
    pl.begin(2, false, 2);
    pl.setTrackInfo(false, 7);
    CHECK(pl.state() == seqrec::Planner::PUNCH);
    pl.enter(3);
    CHECK(log.count(seqrec::OP_CLEAR) == 1 && log.at(3)->kind == seqrec::OP_CLEAR);
    pl.noteOn(60, 80, TP(3, 1), P());
    pl.flush();
    CHECK(log.at(3)->kind == seqrec::OP_NOTE && log.at(3)->note == 60 && log.at(3)->volume == 80 && log.at(3)->instrument == 7);
    CHECK(log.at(3)->micromove == seqrec::kNoMicro && log.at(3)->chord == seqrec::kNoChord);
    pl.enter(4); // passed without a note: cleared
    CHECK(log.at(4)->kind == seqrec::OP_CLEAR);
    // a late note (uStep > 24) lands on the next step; entering that step must not clear it again
    size_t before = log.ops.size();
    pl.noteOn(62, 100, TP(4, 30), P());
    pl.flush();
    CHECK(log.ops.size() == before + 1 && log.at(5)->kind == seqrec::OP_NOTE && log.at(5)->note == 62);
    pl.enter(5);
    CHECK(log.ops.size() == before + 1);
    // next lap: the claim of the previous lap is gone
    for (uint8_t s = 6; s <= 15; s++)
        pl.enter(s);
    pl.enter(0);
    pl.enter(1);
    pl.enter(2);
    pl.enter(3);
    pl.enter(4);
    pl.enter(5);
    CHECK(log.at(5)->kind == seqrec::OP_CLEAR);
    // a punch does not clear the step that is already playing when it starts
    OpLog l2;
    seqrec::Planner p2;
    p2.setSink(&OpLog::sink, &l2);
    p2.begin(0, false, 9);
    CHECK(l2.ops.empty());
    p2.stop();
    CHECK(!p2.active() && p2.ends() == 1);
    p2.noteOn(60, 1, TP(9, 1), P());
    p2.flush();
    CHECK(l2.ops.empty()); // stopped: nothing is written
}

static void test_planner_microtiming_and_velocity()
{
    OpLog log;
    seqrec::Planner pl;
    pl.setSink(&OpLog::sink, &log);
    pl.begin(0, false, 0);
    pl.enter(1);
    pl.noteOn(60, 90, TP(1, 25), P(false, true)); // not quantised: stays on step 1, micromove from uStep
    pl.flush();
    CHECK(log.at(1)->kind == seqrec::OP_NOTE && log.at(1)->micromove == (24 * 100 / 47));
    pl.enter(2);
    pl.noteOn(61, 90, TP(2, 1), P(true, false)); // no velocity recording: default volume
    pl.flush();
    CHECK(log.at(2)->volume == seqrec::kNoVolume && log.at(2)->micromove == seqrec::kNoMicro);
    // uStep 24 still belongs to this step, 25 to the next
    pl.enter(3);
    pl.noteOn(62, 50, TP(3, 24), P());
    pl.flush();
    CHECK(log.at(3)->kind == seqrec::OP_NOTE);
    pl.noteOn(63, 50, TP(3, 25), P());
    pl.flush();
    CHECK(log.at(4)->kind == seqrec::OP_NOTE && log.at(4)->note == 63);
}

static void test_planner_note_off()
{
    OpLog log;
    seqrec::Planner pl;
    pl.setSink(&OpLog::sink, &log);
    pl.begin(0, false, 0);
    pl.enter(1);
    pl.noteOn(60, 90, TP(1, 1), P());
    pl.flush();
    pl.noteOff(TP(1, 10), P()); // same step as the note: the off goes to the next step
    CHECK(log.at(2) && log.at(2)->kind == seqrec::OP_OFF);
    pl.enter(2); // owned by the off: not cleared again
    CHECK(log.count(seqrec::OP_CLEAR) == 1); // only step 1
    pl.noteOn(64, 90, TP(2, 1), P()); // a following note replaces the off
    pl.flush();
    CHECK(log.at(2)->kind == seqrec::OP_NOTE && log.at(2)->note == 64);
    pl.noteOff(TP(2, 40), P()); // late off: step 3 (quantised), pre-claimed
    CHECK(log.at(3)->kind == seqrec::OP_OFF);
    size_t n = log.ops.size();
    pl.noteOff(TP(2, 42), P()); // second off for the same step: nothing new
    CHECK(log.ops.size() == n);
    // an off must not overwrite a step that already got a note
    pl.enter(3);
    pl.noteOn(65, 90, TP(3, 2), P());
    pl.flush();
    n = log.ops.size();
    pl.noteOff(TP(2, 30), P()); // dest 3 (claimed by the note): the off moves on to step 4, step 3 stays a note
    CHECK(log.at(3)->kind == seqrec::OP_NOTE && log.at(3)->note == 65);
    (void)n;
}

static void test_planner_one_loop()
{
    OpLog log;
    seqrec::Planner pl;
    pl.setSink(&OpLog::sink, &log);
    pl.begin(1, true, 7);
    CHECK(pl.state() == seqrec::Planner::ARMED);
    pl.enter(8);
    pl.noteOn(60, 90, TP(8, 1), P()); // waiting: ignored
    pl.flush();
    pl.noteOff(TP(8, 20), P());
    for (uint8_t s = 9; s <= 15; s++)
        pl.enter(s);
    CHECK(log.ops.empty() && pl.state() == seqrec::Planner::ARMED);
    // loop start: the note on the downbeat comes after enter(0) in the same tick
    pl.enter(0);
    CHECK(pl.state() == seqrec::Planner::LOOP);
    pl.noteOn(60, 90, TP(0, 1), P());
    pl.flush();
    CHECK(log.at(0)->kind == seqrec::OP_NOTE);
    for (uint8_t s = 1; s <= 15; s++)
        pl.enter(s);
    CHECK(pl.state() == seqrec::Planner::LOOP);
    // a late note in the last step wraps to step 0: that belongs to the next lap, dropped
    size_t n = log.ops.size();
    pl.noteOn(72, 90, TP(15, 40), P());
    pl.flush();
    CHECK(log.ops.size() == n);
    CHECK(log.at(0)->note == 60);
    pl.enter(0); // exactly one loop is over
    CHECK(pl.state() == seqrec::Planner::IDLE && pl.ends() == 1);
    CHECK(log.count(seqrec::OP_CLEAR) == 16); // every step is cleared first, a note then replaces step 0
    n = log.ops.size();
    pl.noteOn(60, 90, TP(0, 1), P());
    pl.flush();
    pl.enter(1);
    CHECK(log.ops.size() == n);
}

static void test_planner_chord_groups()
{
    OpLog log;
    seqrec::Planner pl;
    pl.setSink(&OpLog::sink, &log);
    pl.begin(0, false, 0);
    pl.setTrackInfo(false, 4); // MIDI track
    pl.enter(1);
    pl.noteOn(67, 90, TP(1, 1), P()); // order of arrival does not matter
    pl.noteOn(60, 70, TP(1, 1), P());
    pl.noteOn(64, 80, TP(1, 1), P());
    pl.flush();
    CHECK(log.at(1)->note == 60 && log.at(1)->chord == 5 && log.at(1)->volume == 90 && pl.hints() == 0);
    pl.enter(2);
    pl.noteOn(60, 90, TP(2, 1), P());
    pl.noteOn(64, 90, TP(2, 1), P());
    pl.noteOn(67, 90, TP(2, 1), P());
    pl.noteOn(71, 90, TP(2, 1), P());
    pl.noteOn(74, 90, TP(2, 1), P());
    pl.flush();
    CHECK(log.at(2)->note == 60 && log.at(2)->chord != seqrec::kNoChord && (pl.hints() & seqrec::HINT_REDUCED));
    // sample track: lowest note only
    pl.setTrackInfo(true, 2);
    pl.enter(3);
    pl.noteOn(64, 90, TP(3, 1), P());
    pl.noteOn(60, 90, TP(3, 1), P());
    pl.noteOn(67, 90, TP(3, 1), P());
    pl.flush();
    CHECK(log.at(3)->note == 60 && log.at(3)->chord == seqrec::kNoChord && (pl.hints() & seqrec::HINT_NEED_MIDI));
    // notes of different steps are separate groups
    pl.setTrackInfo(false, 4);
    pl.enter(4);
    pl.noteOn(60, 90, TP(4, 1), P());
    pl.noteOn(72, 90, TP(4, 30), P()); // dest 5
    pl.flush();
    CHECK(log.at(4)->note == 60 && log.at(5)->note == 72);
    // begin resets the hints
    pl.begin(0, false, 0);
    CHECK(pl.hints() == 0);
}

// ---------------------------------------------------------------- record through the engine
// playhead of the recorded track, advanced as the firmware does (stepClock before play_microStep, then inc_uStep)
struct Head
{
    int pos = 0, u = 1, last = 15;
};
static void playRec(Engine& e, MockHost& h, Head& hd, int ticks)
{
    for (int i = 0; i < ticks; i++)
    {
        h.tp.pos = (uint8_t)hd.pos;
        h.tp.uStep = (uint8_t)hd.u;
        h.tp.last = (uint8_t)hd.last;
        e.stepClock((uint16_t)hd.u);
        e.advanceUs(2604);
        hd.u++;
        if (hd.u > 48)
        {
            hd.u = 1;
            hd.pos = (hd.pos + 1) % (hd.last + 1);
        }
    }
}
static std::vector<seqrec::Op> drainOps(Engine& e)
{
    std::vector<seqrec::Op> v;
    seqrec::Op o;
    while (e.popRecOp(o))
        v.push_back(o);
    return v;
}

static void test_engine_rec_punch()
{
    MockHost h;
    Engine* e = mk(h);
    e->postType(2, TYPE_CHORDARP);
    e->idleTick();
    Head hd;
    playRec(*e, h, hd, 5); // transport running
    e->postRec(2, Engine::REC_PUNCH);
    playRec(*e, h, hd, 1);
    CHECK(e->recState() == seqrec::Planner::PUNCH && e->recTrack() == 2);
    e->postPad(2, 0, true); // hold a root pad
    playRec(*e, h, hd, 48 * 4);
    e->postPad(2, 0, false);
    playRec(*e, h, hd, 48 * 2);
    std::vector<seqrec::Op> ops = drainOps(*e);
    int notes = 0, clears = 0, chords = 0;
    for (size_t i = 0; i < ops.size(); i++)
    {
        CHECK(ops[i].track == 2);
        if (ops[i].kind == seqrec::OP_NOTE)
        {
            notes++;
            if (ops[i].chord != seqrec::kNoChord)
                chords++;
            CHECK(ops[i].instrument == 3);
        }
        else if (ops[i].kind == seqrec::OP_CLEAR)
            clears++;
    }
    CHECK(notes >= 1);
    CHECK(clears >= 1); // steps passed without a note are cleared
    printf("  punch: %d ops, %d notes (%d with chord FX), %d clears\n", (int)ops.size(), notes, chords, clears);
    unsigned ends = e->recEnds();
    e->postRec(2, Engine::REC_STOP);
    playRec(*e, h, hd, 1);
    CHECK(e->recState() == seqrec::Planner::IDLE && e->recEnds() == (uint8_t)(ends + 1));
    // an Off track cannot record
    e->postRec(5, Engine::REC_PUNCH);
    playRec(*e, h, hd, 2);
    CHECK(e->recState() == seqrec::Planner::IDLE);
    delete e;
}

static void test_engine_rec_loop_and_stop()
{
    MockHost h;
    Engine* e = mk(h);
    e->postType(1, TYPE_CHORDARP);
    e->idleTick();
    Head hd;
    hd.last = 3;                     // 4-step pattern
    playRec(*e, h, hd, 48 * 1 + 10); // inside step 1
    e->postRec(1, Engine::REC_LOOP);
    playRec(*e, h, hd, 1);
    CHECK(e->recState() == seqrec::Planner::ARMED);
    e->postPad(1, 0, true);
    playRec(*e, h, hd, 48 * 2 - 20); // reaches step 3, still armed
    CHECK(e->recState() == seqrec::Planner::ARMED);
    drainOps(*e);
    unsigned ends = e->recEnds();
    for (int g = 0; g < 48 * 8 && e->recState() == seqrec::Planner::ARMED; g++)
        playRec(*e, h, hd, 1);
    CHECK(e->recState() == seqrec::Planner::LOOP); // the loop starts at step 0
    int ticks = 0;
    while (e->recState() == seqrec::Planner::LOOP && ticks < 48 * 8)
    {
        playRec(*e, h, hd, 1);
        ticks++;
    }
    CHECK(ticks == 48 * 4); // exactly one loop of 4 steps, then done by itself
    CHECK(e->recState() == seqrec::Planner::IDLE && e->recEnds() == (uint8_t)(ends + 1));
    std::vector<seqrec::Op> ops = drainOps(*e);
    CHECK(!ops.empty());
    bool seen[4] = { false, false, false, false };
    for (size_t i = 0; i < ops.size(); i++)
    {
        CHECK(ops[i].step < 4);
        if (ops[i].step < 4)
            seen[ops[i].step] = true;
    }
    CHECK(seen[0] && seen[1] && seen[2] && seen[3]);
    // transport stop ends a punch-in
    e->postRec(1, Engine::REC_PUNCH);
    playRec(*e, h, hd, 2);
    CHECK(e->recState() == seqrec::Planner::PUNCH);
    e->idleTick();
    CHECK(e->recState() == seqrec::Planner::IDLE);
    // taking the track Off ends it too
    playRec(*e, h, hd, 2);
    e->postRec(1, Engine::REC_PUNCH);
    playRec(*e, h, hd, 2);
    CHECK(e->recState() == seqrec::Planner::PUNCH);
    playRec(*e, h, hd, 48); // a few pulses into the bar
    e->postType(1, TYPE_OFF);
    playRec(*e, h, hd, 2);
    CHECK(e->recState() == seqrec::Planner::PUNCH); // the change applies on the next bar
    playRec(*e, h, hd, 96 * 8);
    CHECK(e->recState() == seqrec::Planner::IDLE);
    delete e;
}

// ---------------------------------------------------------------- sidecar
static uint16_t testCrc(const uint8_t* b, size_t n)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; i++)
    {
        crc = (uint16_t)(crc ^ ((uint16_t)b[i] << 8));
        for (int k = 0; k < 8; k++)
            crc = (uint16_t)((crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1));
    }
    return crc;
}

static void test_sidecar_roundtrip_and_corruption()
{
    MockHost h1, h2;
    Engine* a = mk(h1);
    a->applyType(0, TYPE_MONO);
    a->applyType(3, TYPE_CHORDARP);
    a->applyType(9, TYPE_MONO);
    a->applyPad(0, 0, true); // mono steps
    a->applyPad(0, 0, false);
    a->applyPad(0, 5, true);
    a->applyPad(0, 5, false);
    a->applyPad(9, 26, true);
    a->applyPad(9, 26, false);
    a->applyPad(3, 33, true); // chord/arp panel pad
    a->applyPad(3, 33, false);
    uint8_t buf[Engine::kSidecarMax];
    size_t n = a->saveState(buf, sizeof(buf));
    CHECK(n > 100 && n <= Engine::kSidecarMax);
    printf("  sidecar: %d bytes (max %d)\n", (int)n, (int)Engine::kSidecarMax);
    CHECK(a->saveState(buf, 10) == 0); // too small
    uint16_t hashA = a->stateHash();

    Engine* b = mk(h2);
    CHECK(b->loadState(buf, n));
    CHECK(b->type(0) == TYPE_MONO && b->type(3) == TYPE_CHORDARP && b->type(9) == TYPE_MONO && b->type(1) == TYPE_OFF);
    int stepsSet = 0;
    for (int i = 1; i <= seqgen::Mono::kMaxSteps; i++)
    {
        CHECK(a->mono(0).stepAt(i) == b->mono(0).stepAt(i));
        CHECK(a->mono(9).stepAt(i) == b->mono(9).stepAt(i));
        if (b->mono(0).stepAt(i) != seqgen::Mono::kNoStep)
            stepsSet++;
    }
    CHECK(stepsSet >= 1);
    CHECK(b->stateHash() == hashA);
    uint8_t buf2[Engine::kSidecarMax];
    size_t n2 = b->saveState(buf2, sizeof(buf2));
    CHECK(n2 == n && memcmp(buf, buf2, n) == 0); // stable
    CHECK(b->active());

    // corrupt / short / wrong files change nothing
    Engine* c = mk(h2);
    c->applyType(7, TYPE_MONO);
    uint16_t hashC = c->stateHash();
    uint8_t bad[Engine::kSidecarMax];
    memcpy(bad, buf, n);
    bad[40] ^= 0x55; // payload bit flip -> CRC
    CHECK(!c->loadState(bad, n));
    memcpy(bad, buf, n);
    bad[0] = 'X';
    CHECK(!c->loadState(bad, n));
    memcpy(bad, buf, n);
    bad[4] = 9; // version
    CHECK(!c->loadState(bad, n));
    CHECK(!c->loadState(buf, n - 1)); // short
    CHECK(!c->loadState(buf, 0));
    CHECK(!c->loadState(buf, 5));
    CHECK(!c->loadState(0, 100));
    memset(bad, 0, sizeof(bad)); // empty file read into a zeroed buffer
    CHECK(!c->loadState(bad, sizeof(bad)));
    // a valid frame with an invalid block (type 7) and a fixed-up CRC is refused as well
    memcpy(bad, buf, n);
    bad[8] = 7;
    {
        size_t end = n - 2;
        uint16_t crc = testCrc(bad, end);
        bad[end] = (uint8_t)(crc & 0xFF);
        bad[end + 1] = (uint8_t)(crc >> 8);
    }
    CHECK(!c->loadState(bad, n));
    CHECK(c->type(7) == TYPE_MONO && c->stateHash() == hashC); // untouched
    // a good file replaces everything
    CHECK(c->loadState(buf, n));
    CHECK(c->type(7) == TYPE_OFF && c->type(0) == TYPE_MONO);
    c->resetState();
    CHECK(c->type(0) == TYPE_OFF && !c->active());
    delete a;
    delete b;
    delete c;
}

static void test_state_change_detection()
{
    MockHost h;
    Engine* e = mk(h);
    uint16_t h0 = e->stateHash();
    e->applyType(2, TYPE_MONO);
    uint16_t h1 = e->stateHash();
    CHECK(h0 != h1);
    e->applyPad(2, 3, true);
    e->applyPad(2, 3, false);
    CHECK(e->stateHash() != h1);
    delete e;
}

int sessionTests(int& checks);

int main()
{
    test_tap();
    test_logic();
    test_off_is_inert();
    test_chord_takeover_stopped();
    test_chord_takeover_playing_and_pattern_returns();
    test_mono_follows_transport();
    test_type_change_next_bar();
    test_type_change_releases_notes_and_pads();
    test_muted_track_is_silent();
    test_scale_comes_from_first_chordarp();
    test_ring_overflow_drops();
    test_chord_lookup();
    test_planner_punch_replace();
    test_planner_microtiming_and_velocity();
    test_planner_note_off();
    test_planner_one_loop();
    test_planner_chord_groups();
    test_engine_rec_punch();
    test_engine_rec_loop_and_stop();
    test_sidecar_roundtrip_and_corruption();
    test_state_change_detection();
    g_fail += sessionTests(g_checks);
    printf("%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
