// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
#include "seqTracksCore.h"
#include <stdio.h>
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
    printf("%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
