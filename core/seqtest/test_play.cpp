// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Host tests of the Play overlay logic (plan 005 R1): strip paging, track -> view window, type list (M3), legend gate (m3), strip
// texts (M7), pad legend.
#include "seqPlayLogic.h"
#include <stdio.h>
#include <string.h>

using namespace seqplay;

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

static void test_strip_paging()
{
    CHECK(stripFirst(0) == 0 && stripFirst(1) == 8);
    CHECK(stripSlotTrack(0, 0) == 0 && stripSlotTrack(0, 7) == 7);
    CHECK(stripSlotTrack(1, 0) == 8 && stripSlotTrack(1, 7) == 15);
    CHECK(stripPageOfTrack(7) == 0 && stripPageOfTrack(8) == 1 && stripPageOfTrack(15) == 1);
    CHECK(stripPageForArrow(false) == 0 && stripPageForArrow(true) == 1);
    // every strip cell maps to a distinct track 0..15 over the two pages
    bool seen[16] = {};
    for (int p = 0; p < 2; p++)
        for (int b = 0; b < 8; b++) seen[stripSlotTrack((uint8_t)p, (uint8_t)b)] = true;
    for (int t = 0; t < 16; t++) CHECK(seen[t]);
}

static void test_view_window()
{
    // full view: blocks of four tracks
    CHECK(viewFirstTrack(0, 4) == 0 && viewFirstTrack(3, 4) == 0);
    CHECK(viewFirstTrack(4, 4) == 4 && viewFirstTrack(5, 4) == 4 && viewFirstTrack(7, 4) == 4);
    CHECK(viewFirstTrack(8, 4) == 8 && viewFirstTrack(12, 4) == 12 && viewFirstTrack(15, 4) == 12);
    // single-parameter view: blocks of eight
    CHECK(viewFirstTrack(0, 8) == 0 && viewFirstTrack(7, 8) == 0 && viewFirstTrack(8, 8) == 8 && viewFirstTrack(15, 8) == 8);
    // out-of-range input never leaves the 16 tracks
    CHECK(viewFirstTrack(200, 4) == 12);
    for (int t = 0; t < 16; t++)
    {
        CHECK(trackInView((uint8_t)t, viewFirstTrack((uint8_t)t, 4), 4));
        CHECK(trackInView((uint8_t)t, viewFirstTrack((uint8_t)t, 8), 8));
    }
    CHECK(!trackInView(4, 0, 4) && trackInView(3, 0, 4));
}

static void test_type_list_first_detent_only_opens()
{
    TypeList l;
    // M3: a single nudge opens the list on the current type, nothing is selected
    CHECK(l.wheel(2, seqlogic::TYPE_CHORDARP, 0xFF, 1));
    CHECK(l.open && l.sel == seqlogic::TYPE_CHORDARP && l.cell == 2);
    // releasing on the same type is a no-op
    CHECK(l.release(seqlogic::TYPE_CHORDARP, 0xFF) == -1);
    CHECK(!l.open);
    // a nudge the other way: same
    CHECK(l.wheel(0, seqlogic::TYPE_MONO, 0xFF, -1));
    CHECK(l.sel == seqlogic::TYPE_MONO);
    CHECK(l.release(seqlogic::TYPE_MONO, 0xFF) == -1);
}

static void test_type_list_selection()
{
    TypeList l;
    l.wheel(5, seqlogic::TYPE_OFF, 0xFF, 1); // opens
    CHECK(l.sel == seqlogic::TYPE_OFF);
    CHECK(l.wheel(5, seqlogic::TYPE_OFF, 0xFF, 1)); // second detent: Chord/Arp
    CHECK(l.sel == seqlogic::TYPE_CHORDARP);
    CHECK(l.wheel(5, seqlogic::TYPE_OFF, 0xFF, 1)); // Mono
    CHECK(l.sel == seqlogic::TYPE_MONO);
    for (uint8_t t = seqlogic::TYPE_HR_FIRST; t < seqlogic::TYPE_COUNT; t++) // plan 008 D: the HYPER_RASP scripts follow Mono
    {
        CHECK(l.wheel(5, seqlogic::TYPE_OFF, 0xFF, 1));
        CHECK(l.sel == t);
    }
    CHECK(!l.wheel(5, seqlogic::TYPE_OFF, 0xFF, 1)); // no wrap, nothing changed
    CHECK(l.sel == seqlogic::TYPE_COUNT - 1);
    CHECK(l.release(seqlogic::TYPE_OFF, 0xFF) == seqlogic::TYPE_COUNT - 1);
    CHECK(!l.open);
    // a zero delta never opens
    TypeList z;
    CHECK(!z.wheel(1, 0, 0xFF, 0) && !z.open);
    // moving away and back = no-op
    TypeList b;
    b.wheel(1, seqlogic::TYPE_CHORDARP, 0xFF, 1);
    b.wheel(1, seqlogic::TYPE_CHORDARP, 0xFF, 1);  // Mono
    b.wheel(1, seqlogic::TYPE_CHORDARP, 0xFF, -1); // Chord/Arp again
    CHECK(b.release(seqlogic::TYPE_CHORDARP, 0xFF) == -1);
    // release without an open list does nothing
    TypeList n;
    CHECK(n.release(1, 0xFF) == -1);
}

static void test_type_list_pending()
{
    // a change to Mono is pending on a Chord/Arp track: the list opens on the pending type, releasing keeps it
    TypeList l;
    l.wheel(3, seqlogic::TYPE_CHORDARP, seqlogic::TYPE_MONO, 1);
    CHECK(l.sel == seqlogic::TYPE_MONO);
    CHECK(l.release(seqlogic::TYPE_CHORDARP, seqlogic::TYPE_MONO) == -1);
    // choosing the current type cancels the pending change (posted as the current type)
    TypeList c;
    c.wheel(3, seqlogic::TYPE_CHORDARP, seqlogic::TYPE_MONO, 1);
    c.wheel(3, seqlogic::TYPE_CHORDARP, seqlogic::TYPE_MONO, -1);
    CHECK(c.sel == seqlogic::TYPE_CHORDARP);
    CHECK(c.release(seqlogic::TYPE_CHORDARP, seqlogic::TYPE_MONO) == seqlogic::TYPE_CHORDARP);
    // cancel (Shift, leaving the page) forgets the list: no post
    TypeList k;
    k.wheel(0, 0, 0xFF, 1);
    k.wheel(0, 0, 0xFF, 1);
    k.cancel();
    CHECK(!k.open && k.release(0, 0xFF) == -1);
}

static void test_legend_gate()
{
    LegendGate g;
    CHECK(!g.visible(1000));
    g.shiftChanged(true, 1000);
    CHECK(!g.visible(1100)); // a mute gesture is shorter than 250 ms: no flash
    CHECK(g.visible(1250));
    // a screen button pressed while Shift is held (a mute) hides it until Shift is released
    g.screenButton();
    CHECK(!g.visible(2000));
    g.shiftChanged(false, 2100);
    CHECK(!g.visible(2500));
    g.shiftChanged(true, 3000);
    CHECK(!g.visible(3100) && g.visible(3300)); // a new hold starts fresh
    // a screen button without Shift does not block the next hold
    LegendGate h;
    h.screenButton();
    h.shiftChanged(true, 100);
    CHECK(h.visible(400));
    // timer wrap
    LegendGate w;
    w.shiftChanged(true, 0xFFFFFF00u);
    CHECK(!w.visible(0xFFFFFFF0u) && w.visible(0x100u));
}

static void test_screen_hold()
{
    using H = ScreenHold;
    H h;
    // a plain press is taken; hold events of it are taken; the release is taken
    CHECK(h.event(2, H::PRESS, true, false) == H::TAKE_PRESS);
    CHECK(h.mask == 4 && h.wheelButton() == 2);
    CHECK(h.event(2, H::HOLD, true, false) == H::TAKE_HOLD);
    CHECK(h.event(2, H::RELEASE, true, false) == H::TAKE_RELEASE);
    CHECK(h.mask == 0 && h.wheelButton() == -1);
    // Shift + screen button = the stock mute button: passes, press and release
    CHECK(h.event(3, H::PRESS, true, true) == H::PASS);
    CHECK(h.mask == 0);
    CHECK(h.event(3, H::RELEASE, true, false) == H::PASS);
    // a press when not routable (a popup is open) passes
    CHECK(h.event(1, H::PRESS, false, false) == H::PASS);
    // hold / release without a taken press pass (the stock code decides)
    CHECK(h.event(1, H::HOLD, true, false) == H::PASS);
    CHECK(h.event(1, H::RELEASE, true, false) == H::PASS);
    // a taken press stays ours when Shift or a popup comes up before the release (no half gesture for the stock code)
    CHECK(h.event(5, H::PRESS, true, false) == H::TAKE_PRESS);
    CHECK(h.event(5, H::RELEASE, false, true) == H::TAKE_RELEASE);
    // two buttons: the wheel goes to the open list's button, else the lowest held
    CHECK(h.event(6, H::PRESS, true, false) == H::TAKE_PRESS);
    CHECK(h.event(4, H::PRESS, true, false) == H::TAKE_PRESS);
    CHECK(h.wheelButton() == 4);
    h.listBtn = 6;
    CHECK(h.wheelButton() == 6);
    // the release of the list's button keeps listBtn for the owner (it must know it was the list's) but the wheel no longer uses it
    CHECK(h.event(6, H::RELEASE, true, false) == H::TAKE_RELEASE);
    CHECK(h.listBtn == 6 && h.wheelButton() == 4);
    h.clear();
    CHECK(h.mask == 0 && h.listBtn == -1);
    // button index is masked to 0..7
    CHECK(h.event(9, H::PRESS, true, false) == H::TAKE_PRESS && h.mask == 2);
}

static void test_shift_mute_blocks_legend()
{
    // M-1: Shift held, a screen button pressed = a stock mute (PASS from ScreenHold); the overlay still has to tell the gate
    ScreenHold h;
    LegendGate g;
    g.shiftChanged(true, 1000);
    bool shift = true;
    // the overlay's rule: any screen-button press while Shift is held blocks the legend
    if (h.event(2, ScreenHold::PRESS, true, shift) == ScreenHold::PASS) g.screenButton();
    CHECK(h.mask == 0);
    CHECK(!g.visible(1400));
    g.shiftChanged(false, 1500);
    g.shiftChanged(true, 2000);
    CHECK(g.visible(2300)); // a fresh Shift hold without a button shows it again
}

// v0.3.2: the router's rules for the stock editing keys (Note / Instr / FX1 / FX2 / Copy / Insert / Delete) in Play mode
static void test_edit_keys()
{
    using K = EditKeys;
    for (uint8_t k = 0; k < K::K_COUNT; k++)
    {
        K e;
        if (k == K::K_INSTR)
        {
            // plain Instrument = the stock key (the owner change of v0.3.2); the edit mode is on while it is down
            CHECK(e.event(k, K::PRESS, true, false, false) == K::PASS_EDIT && e.editWanted());
            CHECK(e.event(k, K::HOLD, true, false, false) == K::PASS && e.editWanted());
            CHECK(e.event(k, K::RELEASE, true, false, false) == K::PASS && !e.editWanted());
            // a screen button held: the PolySynth overlay; the hold and release of that press are the overlay's too
            CHECK(e.event(k, K::PRESS, true, false, true) == K::TAKE_SYNTH && !e.editWanted());
            CHECK(e.event(k, K::HOLD, true, false, true) == K::TAKE);
            CHECK(e.event(k, K::RELEASE, true, false, false) == K::TAKE && e.taken == 0);
            // the synth overlay open: Instr closes it, with or without a screen button
            CHECK(e.event(k, K::PRESS, true, true, false) == K::TAKE_SYNTH);
            CHECK(e.event(k, K::RELEASE, true, false, false) == K::TAKE);
        }
        else
        {
            // a screen button held does not change the other keys
            CHECK(e.event(k, K::PRESS, true, false, true) == K::PASS_EDIT && e.editWanted());
            CHECK(e.event(k, K::RELEASE, true, false, false) == K::PASS && !e.editWanted());
            // under the synth overlay these keys belong to it (no stock edit under the covered pattern)
            CHECK(e.event(k, K::PRESS, true, true, false) == K::TAKE && !e.editWanted());
            CHECK(e.event(k, K::HOLD, true, true, false) == K::TAKE);
            CHECK(e.event(k, K::RELEASE, true, false, false) == K::TAKE && e.taken == 0);
        }
        // a popup of the editor owns the inputs: the stock handler gets everything, no edit mode is borrowed
        CHECK(e.event(k, K::PRESS, false, false, false) == K::PASS && !e.editWanted());
        CHECK(e.event(k, K::RELEASE, false, false, false) == K::PASS);
        // the press went to the stock handler, a popup opens, the release still clears the held key
        CHECK(e.event(k, K::PRESS, true, false, false) == K::PASS_EDIT && e.editWanted());
        CHECK(e.event(k, K::RELEASE, false, false, false) == K::PASS && !e.editWanted());
    }
    // the edit mode lasts until the last of several editing keys is up
    K e;
    CHECK(e.event(K::K_DELETE, K::PRESS, true, false, false) == K::PASS_EDIT);
    CHECK(e.event(K::K_FX1, K::PRESS, true, false, false) == K::PASS_EDIT);
    CHECK(e.event(K::K_DELETE, K::RELEASE, true, false, false) == K::PASS && e.editWanted());
    CHECK(e.event(K::K_FX1, K::RELEASE, true, false, false) == K::PASS && !e.editWanted());
    // not an editing key
    CHECK(e.event(K::K_COUNT, K::PRESS, true, false, false) == K::PASS && !e.editWanted());
    e.event(K::K_COPY, K::PRESS, true, false, false);
    e.clear();
    CHECK(!e.editWanted() && e.taken == 0);
}

// v0.3.2: the Shift legend on the key / tick flow of the hardware: Shift down, ticks of the main loop (update() every ~5 ms), screen
// buttons (the stock mute buttons while Shift is down) and the view the overlay control gets.
struct ShiftFlow
{
    LegendGate gate;
    ScreenHold hold;
    bool shift = false;
    bool listOpen = false;
    bool synthOpen = false;
    uint8_t type = seqlogic::TYPE_MONO;
    uint32_t now = 0;
    uint8_t view = 0;
    void tick(uint32_t ms)
    {
        for (uint32_t i = 0; i < ms; i += 5)
        {
            now += 5;
            gate.shiftChanged(shift, now); // update(): sampled from the key state
            view = viewKind(synthOpen, true, listOpen, gate.visible(now), type == seqlogic::TYPE_OFF);
        }
    }
    void shiftKey(bool down) { shift = down; }
    void screenButton(uint8_t b, ScreenHold::Kind kind)
    {
        // the overlay's button(): a press with Shift down that the overlay does not take is a stock mute press: no legend
        if (kind == ScreenHold::PRESS && shift) gate.screenButton();
        hold.event(b, kind, true, shift);
    }
};

static void test_shift_legend_flow()
{
    // hold Shift 400 ms with a sequencer track selected, no screen button: the legend is there after 250 ms and not before
    ShiftFlow f;
    f.tick(1000);
    f.shiftKey(true);
    f.tick(240);
    CHECK(f.view == 0);
    f.tick(20);
    CHECK(f.view == 2);
    f.tick(500);
    CHECK(f.view == 2);
    f.shiftKey(false);
    f.tick(10);
    CHECK(f.view == 0);
    // a quick tap (the stock mute gesture) never shows it
    f.shiftKey(true);
    f.tick(150);
    f.shiftKey(false);
    f.tick(400);
    CHECK(f.view == 0);
    // Shift + screen button (mute) hides it until Shift is released; the next hold shows it again
    f.shiftKey(true);
    f.tick(300);
    CHECK(f.view == 2);
    f.screenButton(3, ScreenHold::PRESS);
    f.tick(10);
    CHECK(f.view == 0);
    f.screenButton(3, ScreenHold::RELEASE);
    f.tick(400);
    CHECK(f.view == 0);
    f.shiftKey(false);
    f.tick(10);
    f.shiftKey(true);
    f.tick(300);
    CHECK(f.view == 2);
    CHECK(f.hold.mask == 0); // the mute presses were not the overlay's
    // an Off track has no legend; the type list and the synth overlay win over it
    f.type = seqlogic::TYPE_OFF;
    f.tick(10);
    CHECK(f.view == 0);
    f.type = seqlogic::TYPE_CHORDARP;
    f.listOpen = true;
    f.tick(10);
    CHECK(f.view == 1);
    f.listOpen = false;
    f.synthOpen = true;
    f.tick(10);
    CHECK(f.view == 3);
    // the legend panel is above the bottom label row (stock mute labels start at y 424; the panel ends at 424)
    CHECK(334 - 38 + 4 * 22 + 40 <= 424);
}

// v0.3.2: the strip hint line
static void test_strip_shift_hint()
{
    char b[16];
    StripCell c;
    c.type = seqlogic::TYPE_MONO;
    c.selected = true;
    c.shiftHint = true;
    stripLine2(b, sizeof b, c);
    CHECK(strcmp(b, "Shift: pads") == 0 && strlen(b) <= 11);
    c.type = seqlogic::TYPE_PLINKY;
    stripLine2(b, sizeof b, c);
    CHECK(strcmp(b, "Shift: pads") == 0);
    // state and messages win over the hint; an Off track has none
    c.takenOver = true;
    stripLine2(b, sizeof b, c);
    CHECK(strcmp(b, "live") == 0);
    c.takenOver = false;
    c.rec = true;
    stripLine2(b, sizeof b, c);
    CHECK(strcmp(b, "REC") == 0);
    c.rec = false;
    c.msg = "pick a type";
    stripLine2(b, sizeof b, c);
    CHECK(strcmp(b, "pick a type") == 0);
    c.msg = nullptr;
    c.type = seqlogic::TYPE_OFF;
    stripLine2(b, sizeof b, c);
    CHECK(strcmp(b, "pattern") == 0);
    // a selected track that plays the PolySynth instrument alternates the hint with the way to its parameters (hold the track + Instr)
    c.type = seqlogic::TYPE_MONO;
    c.synthHint = true;
    c.alt = false;
    stripLine2(b, sizeof b, c);
    CHECK(strcmp(b, "Shift: pads") == 0);
    c.alt = true;
    stripLine2(b, sizeof b, c);
    CHECK(strcmp(b, "Hold+Instr") == 0);
    c.synthHint = false;
    stripLine2(b, sizeof b, c);
    CHECK(strcmp(b, "Shift: pads") == 0); // no synth: nothing to open
    // the Strings legend is a pad interface for the track's own instrument
    LegendState st;
    st.type = seqlogic::TYPE_PLINKY;
    CHECK(strstr(legendHelp(st), "instrument") != nullptr && strlen(legendHelp(st)) <= 88);
    st.plMenu = 1;
    CHECK(strlen(legendHelp(st)) <= 88);
}

static void test_rec_hints_and_priority()
{
    char b[16];
    CHECK(recHintText(false, false, false) == nullptr);
    CHECK(strcmp(recHintText(true, true, true), "rec overflow") == 0);
    CHECK(strcmp(recHintText(false, true, true), "MIDI only") == 0);
    CHECK(strcmp(recHintText(false, false, true), "reduced") == 0);
    StripCell c;
    c.type = seqlogic::TYPE_MONO;
    c.hint = "MIDI only";
    c.pending = 1;
    stripLine2(b, sizeof b, c);
    CHECK(strcmp(b, "MIDI only") == 0); // after REC, before the pending type
    c.rec = true;
    stripLine2(b, sizeof b, c);
    CHECK(strcmp(b, "REC") == 0);
    CHECK(strlen(recHintText(true, false, false)) <= 12);
}

static void test_legend_state()
{
    LegendState st;
    st.type = seqlogic::TYPE_CHORDARP;
    char nb[4];
    CHECK(strcmp(rootName(0, 0, nb), "E") == 0);   // kCam[0][0] = 64
    CHECK(strcmp(rootName(1, 0, nb), "C#m") == 0); // kCam[1][0] = 61, minor row
    CHECK(strcmp(rootName(2, 5, nb), "A#") == 0);   // 70, major row
    CHECK(rootName(4, 0, nb)[0] == 0);
    CHECK(strcmp(legendCell(st, 0, 1).txt, "C#m") == 0 && legendCell(st, 0, 1).zone == Z_NOTE);
    // Play page: the page pad (no tab is on); an edit page: its tab in column 11 is the one pad that is on
    int onPlay = 0;
    for (uint8_t r = 0; r < 4; r++)
        for (uint8_t c = 0; c < 12; c++) onPlay += legendCell(st, c, r).on ? 1 : 0;
    CHECK(onPlay == 0);
    CHECK(strcmp(legendCell(st, 11, 0).txt, "Page") == 0 && legendCell(st, 11, 0).zone == Z_MODE);
    for (uint8_t page = 1; page < 4; page++)
    {
        st.page = page;
        int on = 0;
        for (uint8_t r = 0; r < 4; r++)
            for (uint8_t c = 0; c < 12; c++) on += legendCell(st, c, r).on ? 1 : 0;
        CHECK(on == 1);
        CHECK(legendCell(st, 11, page).on);
        CHECK(strcmp(legendCell(st, 11, 0).txt, "Play") == 0 && strcmp(legendCell(st, 11, page).txt, pageName(page)) == 0);
    }
    // Mono: second half shows steps 9-16
    LegendState m;
    m.type = seqlogic::TYPE_MONO;
    CHECK(strcmp(legendCell(m, 0, 0).txt, "1") == 0);
    m.half = 1;
    CHECK(strcmp(legendCell(m, 0, 0).txt, "9") == 0 && strcmp(legendCell(m, 7, 0).txt, "16") == 0);
    CHECK(strcmp(pageName(0), "Play") == 0 && strcmp(pageName(1), "Groove") == 0 && strcmp(pageName(2), "Feel") == 0 && strcmp(pageName(3), "Setup") == 0);
    // Mono menu: the current speed / edit page / velocity pattern / tie are on
    m.half = 0;
    m.menu = 1;
    m.speed = 6;
    m.ep = 2;
    m.vel = 3;
    m.tie = 1;
    CHECK(legendCell(m, 5, 0).on && !legendCell(m, 4, 0).on && strcmp(legendCell(m, 5, 0).txt, "1/8") == 0);
    CHECK(legendCell(m, 1, 1).on && !legendCell(m, 0, 1).on && strcmp(legendCell(m, 1, 1).txt, "Pg 2") == 0);
    CHECK(legendCell(m, 2, 2).on && strcmp(legendCell(m, 2, 2).txt, "Vel 3") == 0);
    CHECK(legendCell(m, 8, 1).on && strcmp(legendCell(m, 8, 1).txt, "Tie") == 0);
    CHECK(strcmp(legendCell(m, 11, 3).txt, "Notes") == 0);
    m.menu = 0;
    CHECK(strcmp(legendCell(m, 11, 3).txt, "Menu") == 0 && legendCell(m, 9, 2).on);
}

static void test_edge_side()
{
    CHECK(edgeSide(2, 0) == 0 && edgeSide(9, 1) == 0);
    CHECK(edgeSide(9, 0) == 1 && edgeSide(2, 1) == 2);
}

// A small model of the overlay state that planFor() acts on: the survival table, one check per row.
struct Model
{
    int padsHeld = 2;
    bool rec = true;
    bool list = true;
    bool hold = true;
    LateRelease late;
    void apply(Event e)
    {
        Plan p = planFor(e);
        if (p.closeList) list = false;
        if (p.releasePads)
        {
            for (int i = 0; i < 2; i++)
                if (p.swallowLate) late.released((uint8_t)(5 + i));
            padsHeld = 0;
        }
        if (p.endRecording) rec = false;
        if (p.clearHold) hold = false;
    }
};

static void test_survival_table()
{
    const Event all[] = { EV_LEAVE, EV_MODULE_SWITCH, EV_STOP };
    for (Event e : all)
    {
        Model m;
        m.apply(e);
        CHECK(m.padsHeld == 0 && !m.rec && !m.list && !m.hold);
        CHECK(planFor(e).sequencersRun); // takeovers, latched arps and launched clips are not ours to stop
        CHECK(m.late.swallow(5) && m.late.swallow(6));
    }
    // a popup: only the list closes; pads and the recording go on
    Model p;
    p.apply(EV_POPUP);
    CHECK(!p.list && p.padsHeld == 2 && p.rec && p.hold && p.late.bits == 0);
}

static void test_late_release()
{
    LateRelease l;
    l.released(7);
    CHECK(l.swallow(7) && !l.swallow(7)); // once
    l.released(8);
    l.pressed(8); // a new press before the old release: the release belongs to the new press
    CHECK(!l.swallow(8));
    CHECK(!l.swallow(99));
}

static void test_strip_text()
{
    char a[16], b[16];
    StripCell c;
    c.track = 5;
    c.type = seqlogic::TYPE_MONO;
    c.selected = true;
    c.takenOver = true;
    stripLine1(a, sizeof a, c);
    stripLine2(b, sizeof b, c);
    CHECK(strcmp(a, "[6 MONO*]") == 0);
    CHECK(strcmp(b, "live") == 0);
    // Off, not selected
    StripCell o;
    o.track = 2;
    stripLine1(a, sizeof a, o);
    stripLine2(b, sizeof b, o);
    CHECK(strcmp(a, "3 ---") == 0 && strcmp(b, "pattern") == 0);
    CHECK(stripTone(o) == TONE_OFF);
    // pending change visible before the bar (M3)
    StripCell p;
    p.track = 1;
    p.type = seqlogic::TYPE_CHORDARP;
    p.pending = seqlogic::TYPE_MONO;
    stripLine2(b, sizeof b, p);
    CHECK(strcmp(b, "next: MONO") == 0);
    // launched clip / MLR jam marked (M7)
    StripCell l;
    l.type = seqlogic::TYPE_CHORDARP;
    l.launched = true;
    stripLine2(b, sizeof b, l);
    CHECK(strcmp(b, "clip") == 0);
    StripCell m;
    m.type = seqlogic::TYPE_OFF;
    m.cut = true;
    stripLine2(b, sizeof b, m);
    CHECK(strcmp(b, "cut") == 0);
    // rec wins over everything but a message; the message wins
    StripCell r;
    r.type = seqlogic::TYPE_MONO;
    r.rec = true;
    r.launched = true;
    r.pending = 1;
    stripLine2(b, sizeof b, r);
    CHECK(strcmp(b, "REC") == 0 && stripTone(r) == TONE_REC);
    r.msg = "pick a type";
    stripLine2(b, sizeof b, r);
    CHECK(strcmp(b, "pick a type") == 0);
    // widest case fits 11 characters
    StripCell w;
    w.track = 15;
    w.type = seqlogic::TYPE_CHORDARP;
    w.takenOver = true;
    w.selected = true;
    stripLine1(a, sizeof a, w);
    CHECK(strlen(a) <= 11);
    w.selected = false;
    w.takenOver = false;
    w.pending = seqlogic::TYPE_CHORDARP;
    stripLine2(b, sizeof b, w);
    CHECK(strlen(b) <= 11);
    // a bad type value never crashes
    StripCell bad;
    bad.type = 77;
    stripLine1(a, sizeof a, bad);
    CHECK(strcmp(a, "1 ---") == 0);
}

// HYPER_RASP script legends (plan 008 D): the overlay hands the script's own cells to the display through an HrSnap
static void test_hr_legend()
{
    seqgen::HrSlot slot;
    static HrSnap snap;
    for (uint8_t t = 0; t < seqgen::HR_COUNT; t++)
    {
        seqgen::HrScript* s = slot.create(t);
        CHECK(s != nullptr);
        fillHrSnap(snap, *s);
        LegendState st;
        st.type = (uint8_t)(seqlogic::TYPE_HR_FIRST + t);
        st.hr = &snap;
        int words = 0;
        for (uint8_t r = 0; r < 4; r++)
            for (uint8_t c = 0; c < 12; c++)
            {
                LegendCell lc = legendCell(st, c, r);
                CHECK(strlen(lc.txt) <= 6);
                CHECK(lc.zone <= Z_HOLD);
                if (lc.txt[0]) words++;
                CHECK(strcmp(lc.txt, snap.txt[r * 12 + c]) == 0 && lc.zone == snap.zone[r * 12 + c]);
            }
        CHECK(words >= 24);
        char title[96];
        legendTitle(st, title, sizeof title);
        CHECK(strlen(title) > 8 && strlen(title) <= 84);
        CHECK(strlen(legendHelp(st)) > 20 && strlen(legendHelp(st)) <= 88);
        CHECK(strcmp(legendCaption(st.type), typeName(st.type)) == 0);
    }
    // without a snapshot (no script) there is nothing to show, and no crash
    LegendState none;
    none.type = seqlogic::TYPE_HR_FIRST;
    CHECK(legendCell(none, 0, 0).zone == Z_NONE && legendHelp(none)[0] == 0);
    // a lit cell is the white one: the stab pads that is latched
    seqgen::HrScript* s = slot.create(seqgen::HR_CHORD_STABS);
    s->pad(2, 0, true);
    s->pad(2, 0, false);
    fillHrSnap(snap, *s);
    LegendState st;
    st.type = seqlogic::TYPE_HR_FIRST + seqgen::HR_CHORD_STABS;
    st.hr = &snap;
    CHECK(strcmp(legendCell(st, 2, 0).txt, "D") == 0 && legendCell(st, 2, 0).on && !legendCell(st, 3, 0).on);
    CHECK(strcmp(legendCell(st, 0, 2).txt, "Major") == 0 && legendCell(st, 0, 2).on);
}

static void test_legend()
{
    // Chord/Arp, Play page: roots cols 0-5, chord types cols 6-7, type / speed / direction rows, inversion + octave row
    for (int r = 0; r < 4; r++)
    {
        for (int c = 0; c <= 5; c++) CHECK(legendCell(seqlogic::TYPE_CHORDARP, (uint8_t)c, (uint8_t)r).zone == Z_NOTE);
        for (int c = 6; c <= 7; c++) CHECK(legendCell(seqlogic::TYPE_CHORDARP, (uint8_t)c, (uint8_t)r).txt[0] != 0);
    }
    CHECK(strcmp(legendCell(seqlogic::TYPE_CHORDARP, 6, 0).txt, "Root") == 0 && legendCell(seqlogic::TYPE_CHORDARP, 6, 0).zone == Z_HOLD);
    CHECK(strcmp(legendCell(seqlogic::TYPE_CHORDARP, 6, 2).txt, "Triad") == 0 && strcmp(legendCell(seqlogic::TYPE_CHORDARP, 7, 3).txt, "9th") == 0);
    CHECK(strcmp(legendCell(seqlogic::TYPE_CHORDARP, 8, 0).txt, "Chord") == 0 && strcmp(legendCell(seqlogic::TYPE_CHORDARP, 10, 0).txt, "ArpOct") == 0);
    CHECK(strcmp(legendCell(seqlogic::TYPE_CHORDARP, 9, 1).txt, "1/8T") == 0 && strcmp(legendCell(seqlogic::TYPE_CHORDARP, 11, 2).txt, "Zigzag") == 0);
    CHECK(strcmp(legendCell(seqlogic::TYPE_CHORDARP, 8, 3).txt, "Inv-") == 0 && strcmp(legendCell(seqlogic::TYPE_CHORDARP, 11, 3).txt, "Oct+") == 0);
    CHECK(legendCell(seqlogic::TYPE_CHORDARP, 11, 3).zone == Z_ADJ);
    CHECK(legendCell(seqlogic::TYPE_CHORDARP, 11, 1).txt[0] != 0);
    // Mono: steps 1-8 on cols 0-7, controls beyond
    CHECK(legendCell(seqlogic::TYPE_MONO, 0, 0).zone == Z_STEP && strcmp(legendCell(seqlogic::TYPE_MONO, 7, 0).txt, "8") == 0);
    CHECK(legendCell(seqlogic::TYPE_MONO, 3, 2).zone == Z_STEP && legendCell(seqlogic::TYPE_MONO, 3, 2).txt[0] == 0);
    CHECK(strcmp(legendCell(seqlogic::TYPE_MONO, 8, 2).txt, "Half") == 0);
    CHECK(legendCell(seqlogic::TYPE_MONO, 8, 3).zone == Z_HOLD && strcmp(legendCell(seqlogic::TYPE_MONO, 8, 3).txt, "Loop") == 0);
    CHECK(strcmp(legendCell(seqlogic::TYPE_MONO, 8, 0).txt, "Win up") == 0 && strcmp(legendCell(seqlogic::TYPE_MONO, 10, 1).txt, "Faster") == 0);
    CHECK(strcmp(legendCell(seqlogic::TYPE_MONO, 11, 3).txt, "Menu") == 0 && legendCell(seqlogic::TYPE_MONO, 11, 3).zone == Z_MODE);
    CHECK(legendCell(seqlogic::TYPE_MONO, 9, 3).zone == Z_NONE);
    // Off and out-of-range cells have no legend
    CHECK(legendCell(seqlogic::TYPE_OFF, 0, 0).zone == Z_NONE);
    CHECK(legendCell(seqlogic::TYPE_MONO, 12, 0).zone == Z_NONE && legendCell(seqlogic::TYPE_MONO, 0, 4).zone == Z_NONE);
    CHECK(legendCaption(seqlogic::TYPE_OFF)[0] == 0 && legendCaption(seqlogic::TYPE_MONO)[0] != 0);
}

// Plan 008 C: the Shift legend uses words, not three-letter codes; every pad that does something has a label, and the two caption
// lines name the page / view and say how to play it.
static void test_legend_words_and_captions()
{
    const char* const kBad[] = { "TYP", "SET", "GRV", "PRM", "HLF", "CLR", "LP", "W+", "W-", "P-", "P+", "S-", "S+", "V-", "V+", "I-", "I+", "O-", "O+", "TIE" };
    for (uint8_t type = seqlogic::TYPE_CHORDARP; type <= seqlogic::TYPE_MONO; type++)
    {
        for (int variant = 0; variant < 8; variant++)
        {
            LegendState st;
            st.type = type;
            st.page = (uint8_t)(variant & 3);
            st.menu = (uint8_t)((variant >> 2) & 1);
            st.pon = (uint8_t)(variant & 1);
            int labels = 0;
            for (uint8_t r = 0; r < 4; r++)
            {
                for (uint8_t c = 0; c < 12; c++)
                {
                    LegendCell lc = legendCell(st, c, r);
                    CHECK(strlen(lc.txt) <= 6);
                    for (const char* bad : kBad) CHECK(strcmp(lc.txt, bad) != 0);
                    if (lc.txt[0]) labels++;
                }
            }
            CHECK(labels >= 20);
            char title[96];
            legendTitle(st, title, sizeof title);
            CHECK(strlen(title) > 8 && strlen(title) < 80);
            CHECK(strlen(legendHelp(st)) > 20 && strlen(legendHelp(st)) <= 90);
        }
    }
    // the title names the active page / view and the state
    LegendState st;
    st.type = seqlogic::TYPE_CHORDARP;
    st.page = 2;
    char t[96];
    legendTitle(st, t, sizeof t);
    CHECK(strstr(t, "[Feel]") != nullptr && strstr(t, "[Play]") == nullptr && strstr(t, "Groove") != nullptr);
    LegendState m;
    m.type = seqlogic::TYPE_MONO;
    m.ep = 2;
    m.np = 3;
    m.speed = 8;
    m.half = 1;
    legendTitle(m, t, sizeof t);
    CHECK(strstr(t, "Notes 9-16") != nullptr && strstr(t, "Page 2/3") != nullptr && strstr(t, "Speed 1/16") != nullptr && strstr(t, "Tie off") != nullptr);
    m.menu = 1;
    legendTitle(m, t, sizeof t);
    CHECK(strstr(t, "Menu") != nullptr && strstr(t, "Notes") == nullptr);
    CHECK(strcmp(legendHelp(m), legendHelp(LegendState())) != 0);
    CHECK(strcmp(speedName(1), "1/1") == 0 && strcmp(speedName(8), "1/16") == 0 && strcmp(speedName(0), "?") == 0);
    // an Off track has no legend text at all
    LegendState off;
    CHECK(legendHelp(off)[0] == 0);
    legendTitle(off, t, sizeof t);
    CHECK(t[0] == 0);
}

int main()
{
    test_strip_paging();
    test_view_window();
    test_type_list_first_detent_only_opens();
    test_type_list_selection();
    test_type_list_pending();
    test_legend_gate();
    test_screen_hold();
    test_shift_mute_blocks_legend();
    test_edit_keys();
    test_shift_legend_flow();
    test_strip_shift_hint();
    test_rec_hints_and_priority();
    test_legend_state();
    test_edge_side();
    test_survival_table();
    test_late_release();
    test_strip_text();
    test_legend();
    test_hr_legend();
    test_legend_words_and_captions();
    printf("seqplay: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
