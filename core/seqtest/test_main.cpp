// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
#include "seqTracksCore.h"
#include <stdio.h>
#include <string.h>
#include <vector>

using namespace seqtracks;
using namespace seqlogic;

// stand-ins for the firmware glue (seqTracks.cpp) used by the CUT layer
static seqcut::Cut g_cutObj;
seqtracks::Engine seqEngine;
seqlogic::TapDetector seqPatternTap;
void seqCutInit() { g_cutObj.setSectionMode(true); seqEngine.setCut(&g_cutObj); } // as seqTracks.cpp: MLR slots on the musical grid
static uint16_t g_mutedMask = 0;
bool seqCutTrackMuted(uint8_t t) { return ((g_mutedMask >> t) & 1u) != 0; }
void seqCutToggleMute(uint8_t t) { g_mutedMask ^= (uint16_t)(1u << t); }
uint16_t seqCutTracksWithNotes() { return 0x00FF; }

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
    int16_t pib = -1; // pulseInBar() answer
    int critDepth = 0, critEnters = 0;
    uint32_t critEnter() { critDepth++; critEnters++; return 7; }
    void critExit(uint32_t t) { critDepth--; tokenOk = tokenOk && t == 7; }
    bool tokenOk = true;
    int16_t pulseInBar() { return pib; }
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
    t.down(true, 1000);
    CHECK(t.up(1100));
    t.down(true, 1000);
    t.other();
    CHECK(!t.up(1100)); // Pattern + arrow
    t.down(false, 1000);
    CHECK(!t.up(1100)); // another button was already held / the press closed a popup
    CHECK(!t.up(1100)); // release without press
    t.other();
    t.down(true, 1000);
    CHECK(t.up(1100)); // activity before the press does not matter
    // a long hold is not a tap (400 ms at most), also across the 32-bit wrap
    t.down(true, 1000);
    CHECK(t.up(1400));
    t.down(true, 1000);
    CHECK(!t.up(1401));
    t.down(true, 0xFFFFFF00u);
    CHECK(t.up(0x40u));
    t.down(true, 0xFFFFFF00u);
    CHECK(!t.up(0x300u));
}

static void test_perform_key_route()
{
    using namespace seqlogic;
    // (onFaderPage, onStockPage, shift)
    CHECK(performKeyRoute(false, false, false) == PERFORM_TO_FADER);        // Pattern / Song / Master / ... : plain = fader page
    CHECK(performKeyRoute(false, false, true) == PERFORM_STOCK_SWITCH);     // Shift = the stock Performance page
    CHECK(performKeyRoute(true, false, false) == PERFORM_STAY);             // fader page: plain does nothing
    CHECK(performKeyRoute(true, false, true) == PERFORM_TO_STOCK_PAGE);     // fader page: Shift = stock page
    CHECK(performKeyRoute(false, true, false) == PERFORM_FLIP_TRACKS);      // stock page: plain flips the tracks
    CHECK(performKeyRoute(false, true, true) == PERFORM_TO_FADER);          // stock page: Shift = fader page
}

// Song key toggle (replaces the double tap): a plain press + release is a toggle; a hold, another button before the press or between
// press and release, never. No timing: a slow press without a hold event still toggles, the hold event decides.
static void test_song_toggle()
{
    SongToggle t;
    t.down(true);
    CHECK(t.up());                       // plain press + release
    t.down(true);
    CHECK(t.up());                       // and again: no state left over, no timing window
    t.down(true);
    t.hold();
    CHECK(!t.up());                      // the stock hold (momentary peek, or a hold on the Song page itself)
    t.down(true);
    CHECK(t.up());                       // the next plain press is fine again
    t.down(false);
    CHECK(!t.up());                      // another button was already held at the press
    t.down(true);
    t.other();
    CHECK(!t.up());                      // another button used while Song is down (Song + arrow)
    t.other();
    t.down(true);
    CHECK(t.up());                       // activity before the press does not matter
    CHECK(!t.up());                      // a release without a press
    t.down(true);
    t.hold();
    t.hold();
    t.other();
    CHECK(!t.up());
    t.down(true);
    CHECK(t.up());
    // decision of the Song page release: the press must have begun on the page (the page that switched here does not count)
    struct Page { static bool opensLauncher(bool toggle, bool pressedHere, bool exitOnRelease) { return toggle && pressedHere && !exitOnRelease; } };
    CHECK(Page::opensLauncher(true, true, false));
    CHECK(!Page::opensLauncher(true, false, false));   // Song pressed on another page, released here
    CHECK(!Page::opensLauncher(false, true, false));   // held / combined
    CHECK(!Page::opensLauncher(true, true, true));     // stock peek
}

static void test_logic()
{
    CHECK(trackFromScreenButton(0, false) == 0 && trackFromScreenButton(7, false) == 7);
    CHECK(trackFromScreenButton(0, true) == 8 && trackFromScreenButton(7, true) == 15);
    // plan 008 D: the HYPER_RASP script types follow Mono in the list (was: the list ended at Mono = 2); same clamping, checked at both ends
    CHECK(stepTypeSelection(0, -1) == 0 && stepTypeSelection(0, 3) == 1 && stepTypeSelection(2, 1) == TYPE_HR_FIRST);
    CHECK(stepTypeSelection(TYPE_COUNT - 1, 1) == TYPE_COUNT - 1 && stepTypeSelection(TYPE_COUNT - 1, -1) == TYPE_COUNT - 2);
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

static void test_sidecar_policy()
{
    CHECK(sidecarAction(true, true) == SIDE_WRITE && sidecarAction(true, false) == SIDE_WRITE);
    CHECK(sidecarAction(false, true) == SIDE_KEEP);   // same project, unknown file: preserved
    CHECK(sidecarAction(false, false) == SIDE_REMOVE); // Save as over another project: stale file goes
    CHECK(projectId("/Projects", "a") == projectId("/Projects", "a"));
    CHECK(projectId("/Projects", "a") != projectId("/Projects", "b"));
    CHECK(projectId("/Projects", "ab") != projectId("/Projectsa", "b"));
}

// Command queue full: the pad release must be kept and retried, the chord must not stay on.
static void test_pending_release_retry()
{
    MockHost h;
    Engine* e = mk(h);
    e->postType(2, TYPE_CHORDARP);
    e->idleTick();
    CHECK(e->postPad(2, 0, true));
    e->idleTick();
    CHECK(e->takenOver(2));
    for (int i = 0; i < 80; i++)
        e->postType(5, TYPE_OFF); // fill the ring (harmless commands)
    PendingReleases pr;
    if (!e->postPad(2, 0, false))
        pr.add(2, 0);
    CHECK(pr.has(2, 0) && pr.any());
    pr.retry([&](uint8_t t, uint8_t p) { return e->postPad(t, p, false); });
    CHECK(pr.has(2, 0)); // still full
    e->idleTick();       // audio drains the ring
    pr.retry([&](uint8_t t, uint8_t p) { return e->postPad(t, p, false); });
    CHECK(!pr.has(2, 0) && !pr.any());
    idle(*e, 96 * 72);
    CHECK(!e->takenOver(2) && e->soundingCount(2) == 0);
    CHECK(h.count('+', 2) == h.count('-', 2));
    delete e;
}

static void test_panel_pads_do_not_take_over()
{
    CHECK(isNotePad(TYPE_CHORDARP, 0) && isNotePad(TYPE_CHORDARP, 5 + 12 * 3) && !isNotePad(TYPE_CHORDARP, 6) && !isNotePad(TYPE_CHORDARP, 11));
    CHECK(isNotePad(TYPE_MONO, 7 + 12) && !isNotePad(TYPE_MONO, 8) && !isNotePad(TYPE_OFF, 0));
    MockHost h;
    Engine* e = mk(h);
    e->postType(2, TYPE_CHORDARP);
    e->idleTick();
    e->postPad(2, 6, true);  // panel pad (types)
    e->postPad(2, 10, true); // control pad
    idle(*e, 96 * 8);
    CHECK(!e->takenOver(2) && h.count('B', 2) == 0);
    e->postPad(2, 0, true); // a root pad takes over; the panel pad does not keep it
    idle(*e, 96 * 8);
    CHECK(e->takenOver(2));
    e->postPad(2, 0, false);
    idle(*e, 96 * 72);
    CHECK(!e->takenOver(2)); // panel pads are still held
    e->postPad(2, 6, false);
    e->postPad(2, 10, false);
    idle(*e, 2);
    CHECK(h.count('+', 2) == h.count('-', 2));
    delete e;
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
    e->postPad(8, 11 + 3 * 12, true); // Menu view ...
    e->postPad(8, 11 + 3 * 12, false);
    e->postPad(8, 7 + 3 * 12, true); // ... CLEAR (plan 008 C: clear lives in the menu view)
    e->postPad(8, 7 + 3 * 12, false);
    e->postPad(8, 11 + 3 * 12, true); // back to the notes
    e->postPad(8, 11 + 3 * 12, false);
    playUSteps(*e, 48, u);
    CHECK(!e->takenOver(8));
    CHECK(h.count('+', 8) == h.count('-', 8));
    delete e;
}

// run n uSteps with a position (16-step pattern) so the host can answer pulseInBar(); absU = uSteps since the pattern start
static void playPos(Engine& e, MockHost& h, int n, long& absU)
{
    for (int i = 0; i < n; i++)
    {
        int uStep = (int)(absU % 48) + 1;
        int pos = (int)((absU / 48) % 16);
        h.tp.pos = (uint8_t)pos;
        h.tp.uStep = (uint8_t)uStep;
        h.pib = (int16_t)((pos * 6 + (uStep - 1) / 8) % 96);
        e.stepClock((uint16_t)uStep);
        e.advanceUs(2604);
        absU++;
    }
}

// Generators started mid-playback start on a pulse boundary, in phase with the bar of the pattern.
static void test_midplay_start_is_on_grid()
{
    MockHost h;
    Engine* e = mk(h);
    long u = 5 * 48 + 19; // step 5, uStep 20: not a pulse boundary ((20-1)%8 = 3)
    playPos(*e, h, 1, u); // transport "playing" with nothing active
    e->postType(4, TYPE_MONO);
    e->postPad(4, 0 + 3 * 12, true);
    e->postPad(4, 0 + 3 * 12, false);
    size_t before = h.log.size();
    playPos(*e, h, 4, u); // uSteps 21..24 of step 5: the boundary is uStep 25
    for (size_t i = before; i < h.log.size(); i++)
        CHECK(h.log[i].kind != '+'); // no note off the grid
    CHECK(e->type(4) == TYPE_MONO);
    playPos(*e, h, 1, u); // uStep 25
    CHECK(h.count('+', 4) >= 1);
    delete e;

    // Chord/Arp: its bar phase follows the pattern (pulse 33 + 1 after the first pulse)
    MockHost h2;
    Engine* e2 = mk(h2);
    long u2 = 5 * 48 + 19;
    playPos(*e2, h2, 1, u2);
    e2->postType(1, TYPE_CHORDARP);
    playPos(*e2, h2, 5, u2); // uSteps 21..25: the pulse at uStep 25 = pulse 5*6+3 = 33 of the bar
    CHECK(e2->chordArp(1).pulsesIntoBar() == 34);
    delete e2;

    // next-bar type change counts the pattern's bars, not the time since the transport started
    MockHost h3;
    Engine* e3 = mk(h3);
    long u3 = 0;
    e3->postType(1, TYPE_CHORDARP);
    playPos(*e3, h3, 48 * 3, u3);
    long u3b = u3; // mid bar 0 (step 3)
    e3->postType(1, TYPE_MONO);
    playPos(*e3, h3, 48 * 13 - 1, u3); // until the last uStep of step 15
    CHECK(e3->type(1) == TYPE_CHORDARP && e3->pendingType(1) == TYPE_MONO);
    playPos(*e3, h3, 8, u3);
    CHECK(e3->type(1) == TYPE_MONO && (u3b > 0));
    delete e3;
}

// External MIDI clock: with the transport stopped no idleTick / stepClock runs; blockTick (once per audio block) must still
// drain queued pad releases and silence a stopped transport.
static void test_block_tick_without_clock()
{
    MockHost h;
    Engine* e = mk(h);
    e->postType(2, TYPE_CHORDARP);
    e->blockTick(false, 3000);
    CHECK(e->type(2) == TYPE_CHORDARP);
    e->postPad(2, 0, true);
    e->blockTick(false, 3000);
    CHECK(e->takenOver(2) && h.count('+', 2) >= 1);
    e->postPad(2, 0, false); // release without any clock pulse
    e->blockTick(false, 3000);
    e->advanceUs(125000);
    e->blockTick(false, 3000);
    CHECK(!e->takenOver(2));
    CHECK(h.count('+', 2) == h.count('-', 2));
    // recording ends when the transport is stopped, also without clock pulses
    e->postRec(2, Engine::REC_PUNCH);
    e->blockTick(true, 3000);
    CHECK(e->recState() == seqrec::Planner::PUNCH);
    e->blockTick(false, 3000);
    CHECK(e->recState() == seqrec::Planner::IDLE);
    delete e;
}

// Playing, then the (external) clock goes silent: Mono is silenced like on a transport stop and starts again with the clock.
static void test_clock_timeout_silences_mono()
{
    MockHost h;
    Engine* e = mk(h);
    e->postType(8, TYPE_MONO);
    e->blockTick(false, 3000);
    e->postPad(8, 0 + 3 * 12, true);
    e->postPad(8, 0 + 3 * 12, false);
    e->blockTick(false, 3000);
    int u = 1;
    playUSteps(*e, 48 * 2, u);
    CHECK(e->takenOver(8) && h.count('+', 8) >= 1);
    e->blockTick(true, 100000); // short gaps are fine
    CHECK(e->takenOver(8));
    for (int i = 0; i < 5; i++)
        e->blockTick(true, 100000);
    e->blockTick(true, 100000); // > 500 ms without a clock pulse
    CHECK(!e->takenOver(8));
    CHECK(h.count('+', 8) == h.count('-', 8));
    u = 1;
    playUSteps(*e, 48 * 2, u); // the clock returns
    CHECK(e->takenOver(8));
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

// The record queue holds 127 operations; more are dropped and counted ("Rec overflow" hint), and a new recording starts clean.
static void test_rec_queue_overflow_hint()
{
    MockHost h;
    Engine* e = mk(h);
    e->postType(2, TYPE_CHORDARP);
    e->idleTick();
    e->postRec(2, Engine::REC_PUNCH);
    e->postPad(2, 0, true); // (plan 008: only steps that pass while the pad is played are written, so a pad is held)
    int u = 1;
    playUSteps(*e, 3, u);
    CHECK(e->recState() == seqrec::Planner::PUNCH && !(e->recHints() & seqrec::HINT_OVERFLOW));
    playUSteps(*e, 250, u); // the mock position stays at uStep 1: every uStep enters a step and clears it; nobody drains
    CHECK(e->recHints() & seqrec::HINT_OVERFLOW);
    int got = 0;
    seqrec::Op o;
    while (e->popRecOp(o))
        got++;
    CHECK(got == 127);
    e->postRec(2, Engine::REC_STOP);
    playUSteps(*e, 2, u);
    CHECK(e->recHints() & seqrec::HINT_OVERFLOW); // sticky until the next recording
    e->postRec(2, Engine::REC_PUNCH);
    playUSteps(*e, 2, u);
    CHECK(!(e->recHints() & seqrec::HINT_OVERFLOW));
    delete e;
}

// Rec from a stopped transport: step 0 is part of the pass (cleared) when a pad is played; Rec while playing leaves the playing
// step alone. Plan 008: an idle take (no pad) writes nothing at all.
static void test_rec_from_stop_clears_step0()
{
    MockHost h;
    Engine* e = mk(h);
    e->postType(2, TYPE_CHORDARP);
    e->blockTick(false, 3000);
    e->postPad(2, 0, true);
    e->postRec(2, Engine::REC_PUNCH); // the main loop starts the transport and posts Rec
    long u = 0;
    playPos(*e, h, 2, u);
    std::vector<seqrec::Op> ops = drainOps(*e);
    CHECK(!ops.empty() && ops[0].kind == seqrec::OP_CLEAR && ops[0].step == 0);
    delete e;

    MockHost h3;
    Engine* e3 = mk(h3);
    e3->postType(2, TYPE_CHORDARP);
    e3->blockTick(false, 3000);
    e3->postRec(2, Engine::REC_PUNCH);
    long u3 = 0;
    playPos(*e3, h3, 48 * 20, u3); // more than a lap, nothing played
    CHECK(e3->recState() == seqrec::Planner::PUNCH);
    CHECK(drainOps(*e3).empty()); // an idle take changes nothing
    delete e3;

    MockHost h2;
    Engine* e2 = mk(h2);
    e2->postType(2, TYPE_CHORDARP);
    long u2 = 3 * 48 + 10;
    playPos(*e2, h2, 1, u2);
    e2->postRec(2, Engine::REC_PUNCH);
    playPos(*e2, h2, 30, u2);
    e2->postPad(2, 0, true); // played from the second half of step 3 on
    playPos(*e2, h2, 12, u2); // step 4 starts (uStep 1 of step 4 is 38 uSteps after uStep 11 of step 3)
    ops = drainOps(*e2);
    CHECK(!ops.empty());
    for (size_t i = 0; i < ops.size(); i++)
        CHECK(ops[i].step != 3); // mid-step punch-in: the playing step is not cleared
    delete e2;
}

// Two Rec presses before the audio side drained the first one: the second ends the recording (toggle), no restart.
static void test_double_rec_toggles()
{
    MockHost h;
    Engine* e = mk(h);
    e->postType(2, TYPE_CHORDARP);
    e->blockTick(false, 3000);
    long u = 0;
    playPos(*e, h, 2, u);
    unsigned ends = e->recEnds();
    CHECK(e->postRec(2, Engine::REC_PUNCH));
    CHECK(e->postRec(2, Engine::REC_PUNCH)); // UI still saw IDLE
    playPos(*e, h, 2, u);
    CHECK(e->recState() == seqrec::Planner::IDLE);
    CHECK(e->recEnds() != (uint8_t)ends);
    // a third press starts again
    e->postRec(2, Engine::REC_PUNCH);
    playPos(*e, h, 2, u);
    CHECK(e->recState() == seqrec::Planner::PUNCH);
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


static void test_planner_punch_out_note_off()
{
    // punch-out while the last note is open: a soft note-off on the next step without a note
    OpLog log;
    seqrec::Planner pl;
    pl.setSink(&OpLog::sink, &log);
    pl.begin(0, false, 0);
    pl.enter(1);
    pl.noteOn(60, 90, TP(1, 1), P());
    pl.flush();
    pl.enter(2);
    pl.stop();
    CHECK(log.at(2)->kind == seqrec::OP_OFF_SOFT && log.count(seqrec::OP_OFF_SOFT) == 1);
    // note on the very step where it stops: the off goes one step later
    OpLog l2;
    seqrec::Planner p2;
    p2.setSink(&OpLog::sink, &l2);
    p2.begin(0, false, 0);
    p2.enter(4);
    p2.noteOn(60, 90, TP(4, 1), P());
    p2.flush();
    p2.stop();
    CHECK(l2.at(5)->kind == seqrec::OP_OFF_SOFT);
    // already closed by a note-off: nothing extra
    OpLog l3;
    seqrec::Planner p3;
    p3.setSink(&OpLog::sink, &l3);
    p3.begin(0, false, 0);
    p3.enter(1);
    p3.noteOn(60, 90, TP(1, 1), P());
    p3.flush();
    p3.noteOff(TP(1, 10), P());
    p3.enter(2);
    p3.stop();
    CHECK(l3.count(seqrec::OP_OFF_SOFT) == 0);
    // nothing recorded: nothing written
    OpLog l4;
    seqrec::Planner p4;
    p4.setSink(&OpLog::sink, &l4);
    p4.begin(0, false, 3);
    p4.stop();
    CHECK(l4.ops.empty());
    // end of a one-loop recording: the last note is closed at the end of the loop
    OpLog l5;
    seqrec::Planner p5;
    p5.setSink(&OpLog::sink, &l5);
    p5.begin(0, true, 0);
    p5.enter(0);
    for (uint8_t s = 1; s <= 15; s++)
    {
        p5.enter(s);
        if (s == 14)
        {
            p5.noteOn(60, 90, TP(14, 1), P());
            p5.flush();
        }
    }
    p5.enter(0);
    CHECK(p5.state() == seqrec::Planner::IDLE);
    CHECK(l5.count(seqrec::OP_OFF_SOFT) == 1 && l5.at(15)->kind == seqrec::OP_OFF_SOFT);
}

// a real multi-note chord through the engine: panel TYPES pad "triad" (pad 8), then a root pad
static void test_engine_rec_chord()
{
    MockHost h;
    Engine* e = mk(h);
    e->postType(2, TYPE_CHORDARP);
    e->idleTick();
    e->postPad(2, 2 * 12 + 6, true); // Play page, chord type pad 3 (col 6, row 2): triad
    e->postPad(2, 2 * 12 + 6, false);
    Head hd;
    playRec(*e, h, hd, 5);
    e->postRec(2, Engine::REC_PUNCH);
    playRec(*e, h, hd, 1);
    e->postPad(2, 0, true); // root pad: chord of three notes
    playRec(*e, h, hd, 48 * 3);
    std::vector<seqrec::Op> ops = drainOps(*e);
    int chords = 0;
    for (size_t i = 0; i < ops.size(); i++)
        if (ops[i].kind == seqrec::OP_NOTE && ops[i].chord != seqrec::kNoChord)
        {
            chords++;
            CHECK(ops[i].instrument == 3);
            // the written note is the lowest sounding note and the chord FX matches the intervals
            int notes = 0;
            for (size_t k = 0; k < h.log.size(); k++)
                if (h.log[k].kind == '+' && h.log[k].track == 2)
                    notes++;
            CHECK(notes == 3);
            CHECK(ops[i].chord >= 1 && ops[i].chord <= MAX_CHORD);
        }
    CHECK(chords == 1);
    CHECK(e->recHints() == 0);
    // the same on a sample track: lowest note only plus the hint
    e->postRec(2, Engine::REC_STOP);
    playRec(*e, h, hd, 1);
    h.sample = true;
    e->postRec(2, Engine::REC_PUNCH);
    e->postPad(2, 0, false);
    playRec(*e, h, hd, 48);
    e->postPad(2, 0, true);
    playRec(*e, h, hd, 48 * 3);
    ops = drainOps(*e);
    int single = 0;
    for (size_t i = 0; i < ops.size(); i++)
        if (ops[i].kind == seqrec::OP_NOTE)
        {
            single++;
            CHECK(ops[i].chord == seqrec::kNoChord);
        }
    CHECK(single >= 1 && (e->recHints() & seqrec::HINT_NEED_MIDI));
    delete e;
}

// Sample track = one voice: of a chord only the lowest note is sent (no churn), and it is released cleanly.
static void test_sample_track_chord_lowest_only()
{
    MockHost h;
    h.sample = true;
    Engine* e = mk(h);
    e->postType(2, TYPE_CHORDARP);
    e->idleTick();
    e->postPad(2, 2 * 12 + 6, true); // triad
    e->postPad(2, 2 * 12 + 6, false);
    e->idleTick();
    e->postPad(2, 0, true);
    idle(*e, 96 * 8);
    CHECK(h.count('+', 2) == 1 && h.count('-', 2) == 0);
    CHECK(e->soundingCount(2) == 1);
    e->postPad(2, 0, false);
    idle(*e, 96 * 72);
    CHECK(h.count('+', 2) == 1 && h.count('-', 2) == 1 && !e->takenOver(2));
    // the same chord on a MIDI track sounds with all three notes
    MockHost h2;
    Engine* e2 = mk(h2);
    e2->postType(2, TYPE_CHORDARP);
    e2->idleTick();
    e2->postPad(2, 2 * 12 + 6, true);
    e2->postPad(2, 2 * 12 + 6, false);
    e2->idleTick();
    e2->postPad(2, 0, true);
    idle(*e2, 96 * 8);
    CHECK(h2.count('+', 2) == 3);
    delete e;
    delete e2;
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

static void fixCrc(uint8_t* b, size_t n)
{
    size_t end = n - 2;
    uint16_t crc = testCrc(b, end);
    b[end] = (uint8_t)(crc & 0xFF);
    b[end + 1] = (uint8_t)(crc >> 8);
}

#include "seqPlayLogic.h"
#include "seqSidecarLogic.h"
#include "test_hr.inc"

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
    CHECK(c->type(7) == TYPE_MONO && c->stateHash() == hashC); // refused files change nothing
    // a valid frame with an invalid block (type TYPE_COUNT, the first number past the last type) and a fixed-up CRC: only that track falls back to Off, the rest loads
    // (owner review 2026-11-04: a single bad block must not throw away the whole file)
    memcpy(bad, buf, n);
    bad[8] = TYPE_COUNT; // track 0
    fixCrc(bad, n);
    CHECK(c->loadState(bad, n));
    CHECK(c->type(0) == TYPE_OFF && c->type(3) == TYPE_CHORDARP && c->type(9) == TYPE_MONO && c->type(7) == TYPE_OFF);
    // a Chord/Arp block with an out-of-range value in track 3: track 3 Off, track 0 (Mono) and 9 survive
    {
        size_t q = 8;
        size_t caOff[kTracks];
        for (int i = 0; i < kTracks; i++)
        {
            q++; // type
            caOff[i] = q + 1;
            q += 1 + buf[q];
            q += 1 + buf[q];
        }
        memcpy(bad, buf, n);
        bad[caOff[3] + 7] = 99; // selT_ out of range
        fixCrc(bad, n);
        CHECK(c->loadState(bad, n));
        CHECK(c->type(3) == TYPE_OFF && c->type(0) == TYPE_MONO && c->type(9) == TYPE_MONO);
        CHECK(c->mono(0).stepAt(1) == a->mono(0).stepAt(1));
        // a broken length chain is a bad frame: nothing changes
        memcpy(bad, buf, n);
        bad[8 + 1] = 200; // CA length of track 0
        fixCrc(bad, n);
        uint16_t hc = c->stateHash();
        CHECK(!c->loadState(bad, n));
        CHECK(c->stateHash() == hc);
    }
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

// Performing must not mark the project changed: lock root (key mode), visible panel / half / edit page are not settings.
static void test_hash_ignores_performance()
{
    MockHost h;
    Engine* e = mk(h);
    e->applyType(2, TYPE_CHORDARP);
    e->applyType(4, TYPE_MONO);
    uint16_t h0 = e->stateHash();
    e->applyPad(2, 0, true); // key mode: the root pad moves the lock root
    e->applyPad(2, 0, false);
    e->applyPad(2, 3, true);
    e->applyPad(2, 3, false);
    e->applyPad(2, 11, true); // panel switch (control pad)
    e->applyPad(2, 11, false);
    e->applyPad(4, 8 + 2 * 12, true); // Mono HALF (col 8, row 2)
    e->applyPad(4, 8 + 2 * 12, false);
    CHECK(e->stateHash() == h0);
    // real settings change it
    e->applyPad(4, 0, true); // Mono step
    e->applyPad(4, 0, false);
    uint16_t h1 = e->stateHash();
    CHECK(h1 != h0);
    e->applyPad(2, 11, true); // tab Play (the page pad / tab column)
    e->applyPad(2, 11, false);
    e->applyPad(2, 2 * 12 + 6, true); // Chord/Arp chord type 3
    e->applyPad(2, 2 * 12 + 6, false);
    CHECK(e->stateHash() != h1);
    // saving still stores the full state (lock root included): save before and after a root press differ
    uint8_t b1[Engine::kSidecarMax], b2[Engine::kSidecarMax];
    e->applyType(3, TYPE_CHORDARP); // a second Chord/Arp (own scale source is track 2)
    size_t n1 = e->saveState(b1, sizeof(b1));
    e->applyPad(3, 5 + 12, true);
    e->applyPad(3, 5 + 12, false);
    size_t n2 = e->saveState(b2, sizeof(b2));
    CHECK(n1 == n2 && memcmp(b1, b2, n1) != 0);
    delete e;
}

// State is copied out inside short critical sections (balanced, one per track block pair)
static void test_save_uses_critical_sections()
{
    MockHost h;
    Engine* e = mk(h);
    e->applyType(1, TYPE_MONO);
    uint8_t buf[Engine::kSidecarMax];
    h.critEnters = 0;
    CHECK(e->saveState(buf, sizeof(buf)) > 0);
    CHECK(h.critEnters == kTracks && h.critDepth == 0 && h.tokenOk);
    h.critEnters = 0;
    e->stateHash();
    CHECK(h.critEnters == 2 * kTracks && h.critDepth == 0);
    delete e;
}
int sessionTests(int& checks);
#include "test_cut.inc"
#include "test_rec.inc"
#include "test_plinkyseq.inc"

int main()
{
    test_tap();
    test_song_toggle();
    test_perform_key_route();
    test_logic();
    test_sidecar_policy();
    test_pending_release_retry();
    test_panel_pads_do_not_take_over();
    test_off_is_inert();
    test_chord_takeover_stopped();
    test_chord_takeover_playing_and_pattern_returns();
    test_mono_follows_transport();
    test_midplay_start_is_on_grid();
    test_block_tick_without_clock();
    test_clock_timeout_silences_mono();
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
    test_rec_queue_overflow_hint();
    test_rec_from_stop_clears_step0();
    test_double_rec_toggles();
    test_engine_rec_loop_and_stop();
    test_planner_punch_out_note_off();
    test_engine_rec_chord();
    test_sample_track_chord_lowest_only();
    test_sidecar_roundtrip_and_corruption();
    test_hr_type_numbers_and_names();
    test_hr_pads_sound_and_take_over();
    test_hr_clock_runs_only_while_the_transport_plays();
    test_hr_arranger_chord_ends_on_transport_stop();
    test_hr_idle_script_does_not_take_over();
    test_hr_load_publishes_key_lock_and_snapshot_is_protected();
    test_hr_type_change_silences_and_releases();
    test_hr_tracks_share_the_scale_bus();
    test_hr_sidecar_roundtrip_and_old_files();
    test_hr_state_hash_follows_the_settings_only();
    test_state_change_detection();
    test_hash_ignores_performance();
    test_save_uses_critical_sections();
    g_fail += sessionTests(g_checks);
    test_cut_logic();
    test_cut_jump_and_retrigger();
    test_cut_loop_reverse_rate();
    test_cut_stop_resync_transport();
    test_cut_review_fixes();
    test_cut_engine_ring();
    test_cut_layer();
    test_cut_session_freeze_and_launch();
    test_cut_session_layer_adapter();
    test_layer_handover_display_queue();
    test_layer_handover_lifecycle();
    test_syncall_encoding();
    test_syncall_one_pass_mixed_state();
    test_syncall_edge_cases();
    test_syncall_layer_paths();
    test_syncall_session_layer_adapter();
    test_syncall_ring_full_is_retried();
    test_survival_and_panic();
    test_group_arrows();
    test_lane_geometry();
    test_lane_palette();
    test_lane_draw_budget_and_bounds();
    test_mlr_grid_helpers();
    test_mlr_engine_sections();
    test_mlr_leds();
    test_mlr_legend_words();
    test_cue_cache_logic();
    test_cue_latency_model();
    test_mlr_slots_layer();
    test_mlr_screen_budget_and_bounds();
    test_r4_handback_retrigger_and_stock_phase();
    test_rec_toggle_logic();
    test_keys_driver_model();
    test_mlr_rec_v024_bug_reproduced();
    test_mlr_rec_toggle_keys_driver();
    test_mlr_rec_takes_in_any_group();
    test_mlr_gestures_through_layer();
    test_mlr_quantise_screen_button();
    test_mlr_quantise_grid();
    test_mlr_loop_latches();
    test_mlr_take_snaps_to_the_grid();
    test_mlr_quantise_off_is_at_once();
    test_mlr_bar_follows_the_pattern();
    test_live_velocity_matches_recorded();
    test_play_rec_key_flow();
    test_play_rec_idle_take_changes_nothing();
    test_play_rec_wrap_keeps_lap1();
    test_play_rec_live_gap_replaced();
    test_play_rec_muted_track_records();
    test_play_rec_pattern_changed_reason();
    test_play_rec_bounce_and_transport();
    test_play_rec_shift_loop();
    test_rec_end_reasons();
    test_plinky_pad_map();
    test_plinky_type_and_pads();
    test_plinky_pending_and_stop();
    test_plinky_record();
    test_plinky_sidecar();
    test_plinky_overlay_logic();
    test_plinky_overlay_draw_budget();
    test_note_band();
    printf("%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
