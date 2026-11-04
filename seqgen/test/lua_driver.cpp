// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Script-driven event-stream dump, used by lua_compare.py to compare the C++ port with the c31 Lua pages.
// Usage: lua_driver script.txt   (see lua_compare.py for the script language)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../chordarp.h"
#include "../mono.h"

using namespace seqgen;

static ChordArp g_ca;
static Mono g_mono[3];
static SeqContext g_ctx;
static long g_pulses = 0, g_ticks = 0;

static void sink(void*, const Event& e) {
  printf("p%ld t%ld %s %d %d %d\n", g_pulses, g_ticks, e.kind == EV_NOTE_ON ? "on" : "off", e.chan, e.note, e.vel);
}

static void press(ChordArp& c, int col, int row) { c.pad(col, row, true); c.pad(col, row, false); }
static void pressM(Mono& m, int col, int row) { m.pad(col, row, true); m.pad(col, row, false); }

// Every gesture goes through the real pads (the same ones the Tracker has), exactly as the Lua side goes through the grid.
static void gotoPage(int p) {  // tabs: Play -> the page pad opens the last edit page; edit pages have the tab column (col 11)
  int cur = g_ca.getParam(ChordArp::P_PAGE);
  if (cur == p) return;
  if (p == 0) { press(g_ca, 11, 0); return; }
  if (cur == 0) press(g_ca, 11, 0);
  if (g_ca.getParam(ChordArp::P_PAGE) != p) press(g_ca, 11, p);
}

// c34 with_pon: run fn with the parametric groove switched on / off, then restore it
template <typename F> static void withPon(bool want, F fn) {
  gotoPage(ChordArp::PAGE_GROOVE);
  bool cur = g_ca.getParam(ChordArp::P_PON) != 0;
  if (cur != want) press(g_ca, 10, 0);
  fn();
  gotoPage(ChordArp::PAGE_GROOVE);
  if (cur != want) press(g_ca, 10, 0);
}

static void monoMenu(Mono& m, bool on) {
  if ((m.getParam(Mono::P_MENU) != 0) != on) pressM(m, 11, 3);
}

static void monoHalf(Mono& m, int x) {
  int want = (x - 1) / 8;
  monoMenu(m, false);
  if (m.getParam(Mono::P_HALF) != want) pressM(m, 8, 2);
}

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  FILE* f = fopen(argv[1], "r");
  if (!f) return 2;
  g_ca.setContext(&g_ctx);
  g_ca.setSink(sink, 0);
  for (int k = 0; k < 3; ++k) { g_mono[k].setContext(&g_ctx); g_mono[k].setChannel((uint8_t)(4 + k)); g_mono[k].setSink(sink, 0); }
  for (int i = 0; i < 25; ++i) { g_ca.tick(); for (int k = 0; k < 3; ++k) g_mono[k].tick(); }  // boot ticks, as in Lua
  char line[256];
  while (fgets(line, sizeof line, f)) {
    char cmd[32] = {0}; char a[16] = {0}; int n[4] = {0, 0, 0, 0};
    int cnt = sscanf(line, "%31s %d %d %d %d", cmd, &n[0], &n[1], &n[2], &n[3]);
    if (cnt < 1) continue;
    if (!strcmp(cmd, "root")) {  // root X Y d|u
      char dir = 'd'; sscanf(line, "%*s %*d %*d %c", &dir);
      g_ca.pad(n[1] - 1, n[0] - 1, dir == 'd');
    } else if (!strcmp(cmd, "start")) { g_ca.start(); for (int k = 0; k < 3; ++k) g_mono[k].start(); }
    else if (!strcmp(cmd, "stop")) { g_ca.stop(); for (int k = 0; k < 3; ++k) g_mono[k].stop(); }
    else if (!strcmp(cmd, "pulse")) { for (int i = 0; i < n[0]; ++i) { g_pulses++; g_ca.pulse(); for (int k = 0; k < 3; ++k) g_mono[k].pulse(); } }
    else if (!strcmp(cmd, "tick")) { for (int i = 0; i < n[0]; ++i) { g_ticks++; g_ca.tick(); for (int k = 0; k < 3; ++k) g_mono[k].tick(); } }
    else if (!strcmp(cmd, "type")) {  // type N: pads 1..8, or hold pad 1 + tap the pad (N-7) for the extended types
      gotoPage(ChordArp::PAGE_PLAY);
      if (n[0] <= 8) press(g_ca, 6 + (n[0] - 1) / 4, (n[0] - 1) % 4);
      else { int m = n[0] - 7; g_ca.pad(6, 0, true); press(g_ca, 6 + (m - 1) / 4, (m - 1) % 4); g_ca.pad(6, 0, false); }
    }
    else if (!strcmp(cmd, "set")) {  // set T|S|D|Z N
      sscanf(line, "%*s %15s %d", a, &n[0]);
      if (a[0] == 'Z') { gotoPage(ChordArp::PAGE_FEEL); press(g_ca, 10, n[0] - 1); }
      else if (a[0] == 'T' && n[0] == 4) { gotoPage(ChordArp::PAGE_SETUP); press(g_ca, 7, 2); }  // type "off" (Setup page)
      else { gotoPage(ChordArp::PAGE_PLAY); press(g_ca, 7 + n[0], a[0] == 'T' ? 0 : a[0] == 'S' ? 1 : 2); }
    } else if (!strcmp(cmd, "groove")) { withPon(false, [&] { gotoPage(ChordArp::PAGE_GROOVE); press(g_ca, 6 + (n[0] - 1) % 4, (n[0] - 1) / 4); }); }
    else if (!strcmp(cmd, "par")) { withPon(true, [&] { gotoPage(ChordArp::PAGE_GROOVE); press(g_ca, 6 + n[1], n[0] - 1); }); }
    else if (!strcmp(cmd, "arch")) { withPon(true, [&] { gotoPage(ChordArp::PAGE_GROOVE); press(g_ca, 6 + n[0] - 1, 3); }); }
    else if (!strcmp(cmd, "pon")) { gotoPage(ChordArp::PAGE_GROOVE); press(g_ca, 10, 0); }
    else if (!strcmp(cmd, "sw")) {
      gotoPage(ChordArp::PAGE_GROOVE);
      while (g_ca.getParam(ChordArp::P_SWING) < n[0]) press(g_ca, 10, 3);
      while (g_ca.getParam(ChordArp::P_SWING) > n[0]) press(g_ca, 10, 2);
    }
    else if (!strcmp(cmd, "glen")) {
      gotoPage(ChordArp::PAGE_GROOVE);
      int cur = g_ca.getParam(ChordArp::P_GATE) + 1;
      for (int i = ((n[0] - cur) % 3 + 3) % 3; i > 0; --i) press(g_ca, 10, 1);
    }
    else if (!strcmp(cmd, "pmode")) { gotoPage(ChordArp::PAGE_SETUP); press(g_ca, 5 + n[0], 0); }
    else if (!strcmp(cmd, "lock")) {  // hold Locked, tap the root
      gotoPage(ChordArp::PAGE_SETUP);
      g_ca.pad(7, 0, true); g_ca.pad(n[1] - 1, n[0] - 1, true); g_ca.pad(n[1] - 1, n[0] - 1, false); g_ca.pad(7, 0, false);
    }
    else if (!strcmp(cmd, "koct")) {
      gotoPage(ChordArp::PAGE_PLAY);
      while (g_ca.getParam(ChordArp::P_KOCT) < n[0]) press(g_ca, 11, 3);
      while (g_ca.getParam(ChordArp::P_KOCT) > n[0]) press(g_ca, 10, 3);
    } else if (!strcmp(cmd, "inv")) {
      gotoPage(ChordArp::PAGE_PLAY);
      g_ca.pad(9, 3, true); g_ca.pad(8, 3, true); g_ca.pad(8, 3, false); g_ca.pad(9, 3, false);  // both = reset
      for (int i = 0; i < abs(n[0]); ++i) press(g_ca, n[0] > 0 ? 9 : 8, 3);
    } else if (!strcmp(cmd, "pair")) {  // pair S|D a b : two fingers on the speed/direction row
      sscanf(line, "%*s %15s %d %d", a, &n[0], &n[1]);
      int row = a[0] == 'S' ? 1 : 2;
      gotoPage(ChordArp::PAGE_PLAY);
      g_ca.pad(7 + n[0], row, true); g_ca.pad(7 + n[1], row, true);
      g_ca.pad(7 + n[1], row, false); g_ca.pad(7 + n[0], row, false);
    } else if (!strcmp(cmd, "vel")) {  // vel A [B]: hold pattern A, a second finger on B blends (lo + 8*hi)
      int A = n[0], B = (cnt > 2) ? n[1] : 0;
      gotoPage(ChordArp::PAGE_FEEL);
      g_ca.pad(6 + (A - 1) % 4, (A - 1) / 4, true);
      if (B) { g_ca.pad(6 + (B - 1) % 4, (B - 1) / 4, true); g_ca.pad(6 + (B - 1) % 4, (B - 1) / 4, false); }
      g_ca.pad(6 + (A - 1) % 4, (A - 1) / 4, false);
    } else if (!strcmp(cmd, "lane")) {  // lane A [B]: tap = lane (again = off), a second finger = both
      int A = n[0], B = (cnt > 2) ? n[1] : 0;
      gotoPage(ChordArp::PAGE_FEEL);
      g_ca.pad(6 + A - 1, 2, true);
      if (B) { g_ca.pad(6 + B - 1, 2, true); g_ca.pad(6 + B - 1, 2, false); }
      g_ca.pad(6 + A - 1, 2, false);
    } else if (!strcmp(cmd, "orng")) { gotoPage(ChordArp::PAGE_FEEL); press(g_ca, 6 + n[0] - 1, 3); }
    else if (!strcmp(cmd, "tpose")) {
      gotoPage(ChordArp::PAGE_SETUP);
      while (g_ca.getParam(ChordArp::P_TPOSE) < n[0]) press(g_ca, 7, 1);
      while (g_ca.getParam(ChordArp::P_TPOSE) > n[0]) press(g_ca, 6, 1);
    }
    else if (!strcmp(cmd, "gtpose")) g_ctx.gtpose = (int8_t)n[0];
    else if (!strcmp(cmd, "mute")) {  // the Lua side re-runs the kill on every "mute 1": mute on again = off, on
      gotoPage(ChordArp::PAGE_SETUP);
      if (g_ca.getParam(ChordArp::P_MUTE) != n[0]) press(g_ca, 6, 2);
      else if (n[0]) { press(g_ca, 6, 2); press(g_ca, 6, 2); }
    }
    else if (!strcmp(cmd, "sync"))
      printf("S %d %d %d\n", g_ca.groovePulseLevel(), g_ca.notePulseLevel(), g_ca.blinkState() ? 1 : 0);
    else if (cmd[0] == 'm' && cmd[1]) {  // mono commands: first number is the sequencer 1..3
      Mono& m = g_mono[n[0] - 1];
      if (!strcmp(cmd, "mstep")) {  // mstep K X DEG
        int x = n[1], deg = n[2];
        monoHalf(m, x);
        int row = 3 - (deg - m.getParam(Mono::P_WIN));
        if (row >= 0 && row <= 3) pressM(m, (x - 1) % 8, row);
      } else if (!strcmp(cmd, "mnp")) m.setParam(Mono::P_NP, n[1]);
      else if (!strcmp(cmd, "mep")) {
        if (n[1] > m.getParam(Mono::P_NP)) continue;
        monoMenu(m, false);
        while (m.getParam(Mono::P_EP) < n[1]) pressM(m, 9, 1);
        while (m.getParam(Mono::P_EP) > n[1]) pressM(m, 9, 0);
      } else if (!strcmp(cmd, "mspd")) { monoMenu(m, true); pressM(m, n[1] - 1, 0); monoMenu(m, false); }  // menu row 0: speed pads
      else if (!strcmp(cmd, "mwin")) {
        monoMenu(m, false);
        while (m.getParam(Mono::P_WIN) < n[1]) pressM(m, 8, 0);
        while (m.getParam(Mono::P_WIN) > n[1]) pressM(m, 8, 1);
      } else if (!strcmp(cmd, "mloop")) {
        int A = n[1], B = n[2];
        monoMenu(m, false);
        m.pad(8, 3, true);
        monoHalf(m, A); m.pad((A - 1) % 8, 0, true);
        monoHalf(m, B); m.pad((B - 1) % 8, 0, true);
        m.pad((B - 1) % 8, 0, false); m.pad((A - 1) % 8, 0, false);
        m.pad(8, 3, false);
      } else if (!strcmp(cmd, "mclear")) { monoMenu(m, true); pressM(m, 7, 3); monoMenu(m, false); }
      else if (!strcmp(cmd, "mtie")) { monoMenu(m, false); pressM(m, 9, 2); }
      else if (!strcmp(cmd, "mmute")) m.setParam(Mono::P_MUTE, n[1]);
      else if (!strcmp(cmd, "mtpose")) m.setParam(Mono::P_TPOSE, n[1]);
      else if (!strcmp(cmd, "mvg")) { if (n[1] <= 12) { monoMenu(m, true); pressM(m, n[1] - 1, 2); monoMenu(m, false); } else m.setParam(Mono::P_VG, n[1]); }
    }
  }
  fclose(f);
  return 0;
}
