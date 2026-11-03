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

static void gotoPanel(int p) {
  if (g_ca.getParam(ChordArp::P_PANEL) != p) press(g_ca, 10 + (p % 2), p / 2);
}

static void monoHalf(Mono& m, int x) {
  int want = (x - 1) / 8;
  if (m.getParam(Mono::P_HALF) != want) pressM(m, 8, 0);
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
    else if (!strcmp(cmd, "type")) { gotoPanel(ChordArp::PANEL_TYPES); if (n[0] > 8) press(g_ca, 6, 0); /* c31 two-button gesture presses pad 1 first (side effect); the direct pad has none */ press(g_ca, 6 + (n[0] - 1) % 4, (n[0] - 1) / 4); }
    else if (!strcmp(cmd, "set")) {  // set T|S|D|Z N
      sscanf(line, "%*s %15s %d", a, &n[0]);
      int row = a[0] == 'T' ? 0 : a[0] == 'S' ? 1 : a[0] == 'D' ? 2 : 3;
      gotoPanel(ChordArp::PANEL_SETTINGS); press(g_ca, 6 + n[0] - 1, row);
    } else if (!strcmp(cmd, "groove")) { gotoPanel(ChordArp::PANEL_GROOVE); press(g_ca, 6 + (n[0] - 1) % 4, (n[0] - 1) / 4); }
    else if (!strcmp(cmd, "par")) { gotoPanel(ChordArp::PANEL_PARAM); press(g_ca, 6 + n[1], n[0] - 1); }
    else if (!strcmp(cmd, "arch")) { gotoPanel(ChordArp::PANEL_PARAM); press(g_ca, 6 + n[0] - 1, 3); }
    else if (!strcmp(cmd, "pon")) { gotoPanel(ChordArp::PANEL_PARAM); press(g_ca, 10 + 1, 1); }
    else if (!strcmp(cmd, "sw")) g_ca.setParam(ChordArp::P_SWING, n[0]);
    else if (!strcmp(cmd, "glen")) g_ca.setParam(ChordArp::P_GATE, n[0] - 1);
    else if (!strcmp(cmd, "pmode")) g_ca.setParam(ChordArp::P_PMODE, n[0]);
    else if (!strcmp(cmd, "lock")) { g_ca.lockButton(true); g_ca.pad(n[1] - 1, n[0] - 1, true); g_ca.pad(n[1] - 1, n[0] - 1, false); g_ca.lockButton(false); }
    else if (!strcmp(cmd, "koct")) {
      while (g_ca.getParam(ChordArp::P_KOCT) < n[0]) press(g_ca, 11, 3);
      while (g_ca.getParam(ChordArp::P_KOCT) > n[0]) press(g_ca, 10, 3);
    } else if (!strcmp(cmd, "inv")) {
      g_ca.pad(11, 2, true); g_ca.pad(10, 2, true); g_ca.pad(10, 2, false); g_ca.pad(11, 2, false);  // both = reset
      for (int i = 0; i < abs(n[0]); ++i) press(g_ca, n[0] > 0 ? 11 : 10, 2);
    } else if (!strcmp(cmd, "tpose")) g_ca.setParam(ChordArp::P_TPOSE, n[0]);
    else if (!strcmp(cmd, "gtpose")) g_ctx.gtpose = (int8_t)n[0];
    else if (!strcmp(cmd, "mute")) g_ca.setParam(ChordArp::P_MUTE, n[0]);
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
        while (m.getParam(Mono::P_EP) < n[1]) pressM(m, 10, 0);
        while (m.getParam(Mono::P_EP) > n[1]) pressM(m, 9, 0);
      } else if (!strcmp(cmd, "mspd")) {
        while (m.getParam(Mono::P_SPEED) < n[1]) pressM(m, 10, 1);
        while (m.getParam(Mono::P_SPEED) > n[1]) pressM(m, 9, 1);
      } else if (!strcmp(cmd, "mwin")) {
        while (m.getParam(Mono::P_WIN) < n[1]) pressM(m, 8, 2);
        while (m.getParam(Mono::P_WIN) > n[1]) pressM(m, 8, 3);
      } else if (!strcmp(cmd, "mloop")) {
        int A = n[1], B = n[2];
        m.pad(9, 2, true);
        monoHalf(m, A); m.pad((A - 1) % 8, 0, true);
        monoHalf(m, B); m.pad((B - 1) % 8, 0, true);
        m.pad((B - 1) % 8, 0, false); m.pad((A - 1) % 8, 0, false);
        m.pad(9, 2, false);
      } else if (!strcmp(cmd, "mclear")) pressM(m, 11, 0);
      else if (!strcmp(cmd, "mtie")) pressM(m, 8, 1);
      else if (!strcmp(cmd, "mmute")) m.setParam(Mono::P_MUTE, n[1]);
      else if (!strcmp(cmd, "mtpose")) m.setParam(Mono::P_TPOSE, n[1]);
    }
  }
  fclose(f);
  return 0;
}
