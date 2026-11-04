// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Script-driven event / LED dump of a ported HYPER_RASP script, used by hr_compare.py to compare it with the real Lua script.
// Usage: hr_driver <script name> commands.txt
//   grid X Y Z | pulse N | start | stop | tick N | param NAME VALUE | sync
//   busscale PC... | busnone | busroot N | busclear      (the shared scale / root another track published)
// Output: one line per note event ("p<pulse> on|off <note> <vel>"), and after every command one line "L" + the 8 x 4 LED levels.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../hr_scripts.h"

using namespace seqgen;

static long g_pulses = 0;
static void sink(void*, const Event& e) {
  if (e.kind == EV_NOTE_ON) printf("p%ld on %d %d\n", g_pulses, e.note, e.vel);
  else printf("p%ld off %d 0\n", g_pulses, e.note);
}

struct Names { const char* script; int type; const char* names[24]; };
static const Names kNames[] = {
    {"arp_lines", HR_ARP_LINES, {"octave", "rate", "vel", "transp", "role", 0}},
    {"chord_stabs", HR_CHORD_STABS, {"octave", "rate", "vel", "transp", 0}},
    {"arp_seq", HR_ARP_SEQ, {0}},
    {"chord_arp", HR_CHORD_ARP, {"type", "size", "octave", "inv", "mode", "speed", "dir", "groove", "vel", "gate", "transp", "qstart", "keylock", "key", "scale", "latch", 0}},
    {"bassline_seq", HR_BASS, {"octave", "direction", "groove", 0}},
    {"euclid_trigger", HR_EUCLID, {"grouping", "vel", "swing", "chance", "note1", "note2", "note3", "note4", 0}},
    {"grid_seq", HR_GRIDSEQ, {"grouping", "vel", "transp", "div", "range", "note1", "note2", "note3", "note4", 0}},
};

int main(int argc, char** argv) {
  if (argc < 3) return 2;
  const Names* nm = 0;
  for (const Names& n : kNames) if (!strcmp(n.script, argv[1])) nm = &n;
  if (!nm) { fprintf(stderr, "unknown script %s\n", argv[1]); return 2; }
  FILE* f = fopen(argv[2], "r");
  if (!f) return 2;
  static HrSlot slot;
  HrBus bus;
  HrScript* s = slot.create((uint8_t)nm->type);
  if (!s) return 2;
  s->setBus(&bus);
  s->setSink(sink, 0);
  char line[256];
  while (fgets(line, sizeof line, f)) {
    char cmd[32] = {0};
    int a = 0, b = 0, c = 0;
    if (line[0] == '#' || sscanf(line, "%31s", cmd) < 1) continue;
    if (!strcmp(cmd, "grid")) {
      sscanf(line, "%*s %d %d %d", &a, &b, &c);
      s->grid(a, b, c);
    } else if (!strcmp(cmd, "pulse")) {
      sscanf(line, "%*s %d", &a);
      for (int i = 0; i < a; ++i) { ++g_pulses; s->pulse(); }
    } else if (!strcmp(cmd, "start")) s->start();
    else if (!strcmp(cmd, "stop")) s->stop();
    else if (!strcmp(cmd, "tick")) {
      sscanf(line, "%*s %d", &a);
      for (int i = 0; i < a; ++i) s->tick();
    } else if (!strcmp(cmd, "param")) {
      char name[32] = {0};
      sscanf(line, "%*s %31s %d", name, &a);
      int id = -1;
      for (int i = 0; i < 24 && nm->names[i]; ++i) if (!strcmp(name, nm->names[i])) id = i;
      if (id < 0) { fprintf(stderr, "unknown param %s\n", name); return 2; }
      s->setParam(id, a);
    } else if (!strcmp(cmd, "busscale")) {
      uint8_t pcs[12];
      int n = 0, v, off = 0, used;
      const char* p = line + 8;
      while (n < 12 && sscanf(p + off, "%d%n", &v, &used) == 1) { pcs[n++] = (uint8_t)v; off += used; }
      bus.setScale(pcs, n);
    } else if (!strcmp(cmd, "busnone")) bus.nScale = 0;
    else if (!strcmp(cmd, "busroot")) { sscanf(line, "%*s %d", &a); bus.root = (int16_t)a; }
    else if (!strcmp(cmd, "busclear")) bus.reset();
    else if (strcmp(cmd, "sync")) { fprintf(stderr, "unknown command %s\n", cmd); return 2; }
    uint8_t lv[32], rl[32];
    for (int i = 0; i < 32; ++i) lv[i] = rl[i] = 0;
    s->paint(lv, rl);
    printf("L");
    for (int i = 0; i < 32; ++i) printf(" %d", lv[i]);
    printf("\n");
  }
  return 0;
}
