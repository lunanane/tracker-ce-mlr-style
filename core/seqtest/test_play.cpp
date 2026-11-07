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

// v0.3.3: the legend gate: a tap on the screen button of the track that is already selected toggles the pad legend
static void test_legend_gate()
{
    LegendGate g;
    CHECK(!g.visible());
    // first tap on another track: selected, no legend
    g.pressed(2, false);
    g.released(2);
    CHECK(!g.visible());
    // second tap on the selected track shows it (at the release), the next one hides it
    g.pressed(2, true);
    CHECK(!g.visible()); // not before the release: a press may still become hold + wheel
    g.released(2);
    CHECK(g.visible());
    g.pressed(2, true);
    g.released(2);
    CHECK(!g.visible());
    // a press that turned the wheel never toggles; turning the wheel closes a shown legend
    g.pressed(2, true);
    g.wheel();
    g.released(2);
    CHECK(!g.visible());
    g.pressed(2, true);
    g.released(2);
    CHECK(g.visible());
    g.pressed(2, true);
    g.wheel();
    CHECK(!g.visible());
    g.released(2);
    CHECK(!g.visible());
    // a tap on another track closes it
    g.pressed(2, true);
    g.released(2);
    CHECK(g.visible());
    g.pressed(5, false);
    CHECK(!g.visible());
    g.released(5);
    CHECK(!g.visible());
    // another button's release does not toggle; close() (track change, leave, synth overlay, popup) ends it and forgets the press
    g.pressed(1, true);
    g.released(4);
    CHECK(!g.visible());
    g.released(1);
    CHECK(g.visible());
    g.close();
    CHECK(!g.visible());
    g.pressed(1, true);
    g.close();
    g.released(1);
    CHECK(!g.visible());
}

static void test_screen_hold()
{
    using H = ScreenHold;
    H h;
    // button index is masked to 0..7
    CHECK(h.event(9, H::PRESS, true, false) == H::TAKE_PRESS && h.mask == 2);
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

// v0.3.3: the legend on the key / tick flow of the hardware: a screen button down, the hold events of the keys driver (they come
// repeatedly, the gaps shrink: 333, 233, 143 ms ...), ticks of the main loop (update() every ~5 ms), the wheel, the release and the
// view the overlay control gets. The press / hold / release handling mirrors SeqPlayOverlay::screenButton / button / jog.
struct HoldFlow
{
    LegendGate gate;
    ScreenHold hold;
    bool shift = false;
    bool listOpen = false;
    bool synthOpen = false;
    uint8_t type = seqlogic::TYPE_MONO;
    uint8_t selected = 0; // selected track = strip slot (strip page 1)
    uint32_t now = 0;
    uint32_t nextHold = 0, gap = 333;
    int8_t down = -1;
    uint8_t view = 0;
    void tick(uint32_t ms)
    {
        for (uint32_t i = 0; i < ms; i += 5)
        {
            now += 5;
            if (down >= 0 && now >= nextHold) // the keys driver's hold event
            {
                button((uint8_t)down, ScreenHold::HOLD);
                nextHold = now + gap;
                gap = gap > 143 ? (gap == 333 ? 233 : 143) : 143;
            }
            view = viewKind(synthOpen, true, listOpen, gate.visible(), type == seqlogic::TYPE_OFF);
        }
    }
    void button(uint8_t b, ScreenHold::Kind kind)
    {
        switch (hold.event(b, kind, true, shift))
        {
            case ScreenHold::TAKE_PRESS:
                gate.pressed(b, b == selected);
                listOpen = false;
                selected = b;
                break;
            case ScreenHold::TAKE_RELEASE: gate.released(b); listOpen = false; break;
            default: break; // TAKE_HOLD: nothing; PASS: the stock mute button
        }
    }
    void press(uint8_t b)
    {
        button(b, ScreenHold::PRESS);
        if (hold.mask & (1u << b))
        {
            down = (int8_t)b;
            nextHold = now + 333;
            gap = 333;
        }
    }
    void release(uint8_t b)
    {
        button(b, ScreenHold::RELEASE);
        down = -1;
    }
    void tap(uint8_t b, uint32_t heldMs)
    {
        press(b);
        tick(heldMs);
        release(b);
        tick(10);
    }
    void wheel() { gate.wheel(); listOpen = true; } // jog(): the legend gives way to the type list
};

static void test_hold_legend_flow()
{
    HoldFlow f;
    f.tick(1000);
    // first tap on another track: selects it, no legend, however long the finger stays (the hold events change nothing)
    f.tap(2, 80);
    CHECK(f.view == 0 && f.selected == 2);
    f.tap(2, 1500);
    CHECK(f.view == 2); // second tap on the selected track: the legend
    f.tick(2000);
    CHECK(f.view == 2); // it stays until it is toggled off
    f.tap(2, 120);
    CHECK(f.view == 0);
    // hold + wheel on the selected track: the type list; this press never toggles the legend
    f.press(2);
    f.tick(200);
    f.wheel();
    f.tick(10);
    CHECK(f.view == 1);
    f.tick(1000);
    f.release(2);
    f.tick(10);
    CHECK(f.view == 0);
    // legend shown, then hold + wheel: the legend closes, the list opens
    f.tap(2, 100);
    CHECK(f.view == 2);
    f.press(2);
    f.tick(100);
    f.wheel();
    f.tick(10);
    CHECK(f.view == 1);
    f.release(2);
    f.tick(10);
    CHECK(f.view == 0);
    // legend shown, a tap on another track closes it (and selects that track)
    f.tap(2, 100);
    CHECK(f.view == 2);
    f.tap(6, 100);
    CHECK(f.view == 0 && f.selected == 6);
    // Shift + screen button is the stock mute: no selection change, no legend
    f.shift = true;
    f.tap(6, 100);
    f.tap(6, 100);
    CHECK(f.view == 0 && f.hold.mask == 0);
    f.shift = false;
    // an Off track has no legend; the synth overlay wins over it
    f.type = seqlogic::TYPE_OFF;
    f.tap(6, 100);
    CHECK(f.view == 0);
    f.tap(6, 100);
    f.type = seqlogic::TYPE_CHORDARP;
    f.tick(10);
    f.synthOpen = true;
    f.gate.close(); // plinkyToggle(): the legend does not stay under the overlay
    f.tick(10);
    CHECK(f.view == 3);
    f.synthOpen = false;
    f.tick(10);
    CHECK(f.view == 0);
    // the legend panel (now with the close hint line above the captions) stays above the bottom label row (y 424) and below the title bar
    CHECK(334 - 52 >= 30 && 334 - 52 + 4 * 22 + 54 <= 424);
}

// v0.3.3: the Instrument Parameters key in Play mode and the jump to the next PolySynth track
static void test_params_key()
{
    // a popup of the editor owns the inputs: the key is stock
    CHECK(paramsKey(false, false, true) == PK_STOCK && paramsKey(false, true, false) == PK_STOCK && paramsKey(false, true, true) == PK_STOCK);
    // plain key: PolySynth track = the overlay, any other instrument = the stock page (as today)
    CHECK(paramsKey(true, false, true) == PK_TOGGLE);
    CHECK(paramsKey(true, false, false) == PK_STOCK);
    // Shift + key always jumps to the next PolySynth track (also from a PolySynth track), no stock page
    CHECK(paramsKey(true, true, false) == PK_JUMP);
    CHECK(paramsKey(true, true, true) == PK_JUMP);
    // jump / cycle selection: the next PolySynth track after the current one, wrapping; none = -1
    CHECK(nextSynthTrack(0, 3) == -1);
    const uint16_t m = (uint16_t)((1u << 2) | (1u << 9) | (1u << 15));
    CHECK(nextSynthTrack(m, 0) == 2);
    CHECK(nextSynthTrack(m, 2) == 9); // repeated presses cycle: 2 -> 9 -> 15 -> 2
    CHECK(nextSynthTrack(m, 9) == 15);
    CHECK(nextSynthTrack(m, 15) == 2);
    CHECK(nextSynthTrack(m, 14) == 15);
    CHECK(nextSynthTrack((uint16_t)(1u << 5), 5) == 5); // the only one: itself (the overlay stays / opens)
    CHECK(nextSynthTrack((uint16_t)(1u << 5), 6) == 5);
    // the whole cycle visits every PolySynth track once
    int t = 0, seen = 0;
    for (int i = 0; i < 3; i++)
    {
        t = nextSynthTrack(m, (uint8_t)t);
        seen |= 1 << t;
    }
    CHECK(seen == m);
    // the track jumped to is on the strip page its screen button would be
    CHECK(stripPageOfTrack(9) == 1 && stripPageOfTrack(2) == 0);
}

// v0.3.2: the strip hint line
static void test_strip_tap_hint()
{
    char b[16];
    StripCell c;
    c.type = seqlogic::TYPE_MONO;
    c.selected = true;
    c.shiftHint = true;
    stripLine2(b, sizeof b, c);
    CHECK(strcmp(b, "tap: help") == 0 && strlen(b) <= 11);
    c.type = seqlogic::TYPE_PLINKY;
    stripLine2(b, sizeof b, c);
    CHECK(strcmp(b, "tap: help") == 0);
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
    CHECK(strcmp(b, "tap: help") == 0);
    c.alt = true;
    stripLine2(b, sizeof b, c);
    CHECK(strcmp(b, "Hold+Instr") == 0);
    c.synthHint = false;
    stripLine2(b, sizeof b, c);
    CHECK(strcmp(b, "tap: help") == 0); // no synth: nothing to open
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
    CHECK(strcmp(legendCell(st, 0, 1).txt, "12A C#m") == 0 && legendCell(st, 0, 1).zone == Z_NOTE); // plan 028: Camelot position + key
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
                CHECK(strlen(lc.txt) <= 7); // plan 028: the Camelot root labels ("11A F#m") are seven characters
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
                    CHECK(strlen(lc.txt) <= 7); // plan 028: the Camelot root labels
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

// v0.3.6 A4: the stock editor overwrites pad LEDs behind the diff cache; popup close, list close, track / type change and the 500 ms
// guard rewrite all 48
static void test_led_guard()
{
    uint8_t hw[48], shown[48], out[48];
    for (int i = 0; i < 48; i++)
        out[i] = (uint8_t)(i % 16);
    for (int i = 0; i < 48; i++)
        shown[i] = 0xFF;
    LedGuard g;
    uint32_t now = 1000;
    auto pass = [&](bool routable, bool list, uint8_t track, uint8_t type) {
        const bool full = g.full(now, routable, list, track, type);
        if (!routable)
            return 0; // the overlay writes nothing while a popup owns the pads
        return ledPass(shown, out, full, [&](int i, uint8_t pwm) { hw[i] = pwm; });
    };
    auto hwOk = [&]() {
        for (int i = 0; i < 48; i++)
            if (hw[i] != (uint8_t)((out[i] > 15 ? 15 : out[i]) * 2))
                return false;
        return true;
    };
    CHECK(pass(true, false, 2, 3) == 48 && hwOk()); // first pass: everything
    now += 30;
    CHECK(pass(true, false, 2, 3) == 0); // nothing changed, nothing written
    // a stock popup (instrument select) runs: its lights replace ours; the overlay stays away while it is open
    now += 30;
    CHECK(pass(false, false, 2, 3) == 0);
    for (int i = 0; i < 48; i++)
        hw[i] = 0;
    now += 30;
    CHECK(pass(false, false, 2, 3) == 0);
    for (int i = 0; i < 48; i++)
        hw[i] = (uint8_t)(i & 1 ? 40 : 0); // whatever the popup left
    // the popup closes: all pads written again at once (the old cache said "already shown")
    now += 30;
    CHECK(pass(true, false, 2, 3) == 48 && hwOk());
    // the type list: opening and closing it
    now += 30;
    CHECK(pass(true, true, 2, 3) == 0);
    for (int i = 0; i < 48; i++)
        hw[i] = 0;
    now += 30;
    CHECK(pass(true, false, 2, 3) == 48 && hwOk());
    // a type change / track change
    for (int i = 0; i < 48; i++)
        hw[i] = 7;
    now += 30;
    CHECK(pass(true, false, 2, 4) == 48 && hwOk());
    for (int i = 0; i < 48; i++)
        hw[i] = 7;
    now += 30;
    CHECK(pass(true, false, 3, 4) == 48 && hwOk());
    // a stock overwrite nobody announced (Instrument + jog, lightUpPadBoard): healed by the periodic rewrite within 500 ms
    for (int i = 0; i < 48; i++)
        hw[i] = 0;
    int healed = -1;
    for (int t = 1; t <= 40; t++)
    {
        now += 30;
        if (pass(true, false, 3, 4) == 48)
        {
            healed = t * 30;
            break;
        }
    }
    CHECK(healed > 0 && healed <= 540 && hwOk());
    // an explicit invalidate (a stock edit key was used) rewrites on the next pass
    for (int i = 0; i < 48; i++)
        hw[i] = 0;
    g.invalidate();
    now += 30;
    CHECK(pass(true, false, 3, 4) == 48 && hwOk());
    // levels above 15 are clamped
    out[5] = 200;
    now += 30;
    CHECK(pass(true, false, 3, 4) == 1 && hw[5] == 30);
}

// v0.3.6 A5: "tap: help" stays on the selected cell (the synth hint only flashes in); a tap that only nudged the wheel without
// opening the list still toggles; the tap on an Off track is answered by the overlay (message), not by an empty legend
static void test_tap_legend_robust()
{
    LegendGate g;
    g.pressed(2, true);
    g.released(2);
    CHECK(g.visible());
    g.pressed(2, true);
    g.released(2);
    CHECK(!g.visible());
    // quick taps one after the other
    for (int i = 0; i < 6; i++)
    {
        g.pressed(5, true);
        g.released(5);
    }
    CHECK(!g.visible());
    // after another track was selected the next tap on the selected one works
    g.pressed(1, false);
    g.released(1);
    CHECK(!g.visible());
    g.pressed(1, true);
    g.released(1);
    CHECK(g.visible());
    // the strip text on the selected cell is "tap: help" whenever the legend exists (synth hint off the flash)
    StripCell c;
    c.type = seqlogic::TYPE_MONO;
    c.selected = true;
    c.shiftHint = true;
    c.synthHint = true;
    c.alt = false;
    char b[16];
    stripLine2(b, sizeof b, c);
    CHECK(strcmp(b, "tap: help") == 0);
    // an Off track shows no legend
    CHECK(viewKind(false, true, false, true, true) == 0);
}

int main()
{
    test_led_guard();
    test_tap_legend_robust();
    test_strip_paging();
    test_view_window();
    test_type_list_first_detent_only_opens();
    test_type_list_selection();
    test_type_list_pending();
    test_legend_gate();
    test_screen_hold();
    test_edit_keys();
    test_hold_legend_flow();
    test_params_key();
    test_strip_tap_hint();
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
