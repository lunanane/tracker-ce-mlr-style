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
    CHECK(!l.wheel(5, seqlogic::TYPE_OFF, 0xFF, 1)); // no wrap, nothing changed
    CHECK(l.sel == seqlogic::TYPE_MONO);
    CHECK(l.release(seqlogic::TYPE_OFF, 0xFF) == seqlogic::TYPE_MONO);
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

static void test_legend()
{
    // Chord/Arp: roots cols 0-5, 4x4 panel cols 6-9, controls cols 10-11
    for (int r = 0; r < 4; r++)
    {
        for (int c = 0; c <= 5; c++) CHECK(legendCell(seqlogic::TYPE_CHORDARP, (uint8_t)c, (uint8_t)r).zone == Z_NOTE);
        for (int c = 6; c <= 9; c++) CHECK(legendCell(seqlogic::TYPE_CHORDARP, (uint8_t)c, (uint8_t)r).zone == Z_PANEL);
    }
    CHECK(strcmp(legendCell(seqlogic::TYPE_CHORDARP, 10, 0).txt, "TYP") == 0);
    CHECK(strcmp(legendCell(seqlogic::TYPE_CHORDARP, 11, 1).txt, "PRM") == 0);
    CHECK(legendCell(seqlogic::TYPE_CHORDARP, 10, 0).zone == Z_MODE);
    CHECK(legendCell(seqlogic::TYPE_CHORDARP, 11, 3).zone == Z_ADJ);
    // Mono: steps 1-8 on cols 0-7, controls beyond
    CHECK(legendCell(seqlogic::TYPE_MONO, 0, 0).zone == Z_STEP && strcmp(legendCell(seqlogic::TYPE_MONO, 7, 0).txt, "8") == 0);
    CHECK(legendCell(seqlogic::TYPE_MONO, 3, 2).zone == Z_STEP && legendCell(seqlogic::TYPE_MONO, 3, 2).txt[0] == 0);
    CHECK(strcmp(legendCell(seqlogic::TYPE_MONO, 8, 0).txt, "HLF") == 0);
    CHECK(legendCell(seqlogic::TYPE_MONO, 9, 2).zone == Z_HOLD);
    CHECK(strcmp(legendCell(seqlogic::TYPE_MONO, 11, 0).txt, "CLR") == 0);
    CHECK(legendCell(seqlogic::TYPE_MONO, 9, 3).zone == Z_NONE);
    // Off and out-of-range cells have no legend
    CHECK(legendCell(seqlogic::TYPE_OFF, 0, 0).zone == Z_NONE);
    CHECK(legendCell(seqlogic::TYPE_MONO, 12, 0).zone == Z_NONE && legendCell(seqlogic::TYPE_MONO, 0, 4).zone == Z_NONE);
    CHECK(legendCaption(seqlogic::TYPE_OFF)[0] == 0 && legendCaption(seqlogic::TYPE_MONO)[0] != 0);
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
    test_strip_text();
    test_legend();
    printf("seqplay: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
