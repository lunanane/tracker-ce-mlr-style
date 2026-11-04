// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Tiny self-written test runner for seqgen (no frameworks). Expected values are derived by hand from c31.
#include <stdio.h>
#include <string.h>

#include "../chordarp.h"
#include "../mono.h"
#include "../playhead.h"

using namespace seqgen;

static int g_checks = 0, g_fail = 0, g_tests = 0, g_testFail = 0;
static const char* g_name = "";
static int g_failBefore = 0;

#define CHECK(cond)                                                                   \
  do {                                                                                \
    g_checks++;                                                                       \
    if (!(cond)) { g_fail++; printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)
#define CHECK_EQ(a, b)                                                                                    \
  do {                                                                                                    \
    long _a = (long)(a), _b = (long)(b);                                                                  \
    g_checks++;                                                                                           \
    if (_a != _b) { g_fail++; printf("    FAIL %s:%d  %s == %s  (%ld vs %ld)\n", __FILE__, __LINE__, #a, #b, _a, _b); } \
  } while (0)

struct TestReg { const char* name; void (*fn)(); };
static TestReg g_reg[256];
static int g_nreg = 0;
struct Reg { Reg(const char* n, void (*f)()) { g_reg[g_nreg].name = n; g_reg[g_nreg].fn = f; g_nreg++; } };
#define TEST(name) static void name(); static Reg reg_##name(#name, name); static void name()

// ---- event recorder ----
static Event g_ev[2048];
static int g_nev = 0;
static void rec(void*, const Event& e) { if (g_nev < 2048) g_ev[g_nev++] = e; }
static void clearEv() { g_nev = 0; }
static int countOn() { int n = 0; for (int i = 0; i < g_nev; ++i) if (g_ev[i].kind == EV_NOTE_ON) n++; return n; }
static int countOff() { return g_nev - countOn(); }
// notes of the note-ons since the last clear, in order
static int ons(int* out) { int n = 0; for (int i = 0; i < g_nev; ++i) if (g_ev[i].kind == EV_NOTE_ON) out[n++] = g_ev[i].note; return n; }

struct Rig {
  ChordArp ca; SeqContext ctx; Mono mono;
  Rig() { ca.setContext(&ctx); mono.setContext(&ctx); ca.setSink(rec, 0); mono.setSink(rec, 0); clearEv(); }
  void press(int col, int row) { ca.pad(col, row, true); ca.pad(col, row, false); }
  // c31 Camelot cell (x 1..4 row, y 1..6 column)
  void rootDown(int x, int y) { ca.pad(y - 1, x - 1, true); }
  void rootUp(int x, int y) { ca.pad(y - 1, x - 1, false); }
  void panel(int p) { if (ca.getParam(ChordArp::P_PANEL) != p) press(10 + (p % 2), p / 2); }
  void type(int n) { panel(ChordArp::PANEL_TYPES); press(6 + (n - 1) % 4, (n - 1) / 4); }
  void setT(int v) { panel(ChordArp::PANEL_SETTINGS); press(6 + v - 1, 0); }
  void setS(int v) { panel(ChordArp::PANEL_SETTINGS); press(6 + v - 1, 1); }
  void setD(int v) { panel(ChordArp::PANEL_SETTINGS); press(6 + v - 1, 2); }
  void setZ(int v) { panel(ChordArp::PANEL_SETTINGS); press(6 + v - 1, 3); }
  void groove(int g) { panel(ChordArp::PANEL_GROOVE); press(6 + (g - 1) % 4, (g - 1) / 4); }
  void pulses(int n) { for (int i = 0; i < n; ++i) { ca.pulse(); mono.pulse(); } }
  void ticks(int n) { for (int i = 0; i < n; ++i) { ca.tick(); mono.tick(); } }
  void toBar() { ca.start(); }  // meas = 0
};

// ======================= Chord/Arp: pools, types, inversion =======================
TEST(pool_major_triad_and_minor) {
  Rig r; int16_t p[8];
  r.ca.setParam(ChordArp::P_SEL_SIZE, 5);
  int n = r.ca.pool(60, 3, false, p);  // type 3 = {0,2,4}
  CHECK_EQ(n, 3); CHECK_EQ(p[0], 60); CHECK_EQ(p[1], 64); CHECK_EQ(p[2], 67);
  n = r.ca.pool(69, 3, true, p);  // A minor
  CHECK_EQ(n, 3); CHECK_EQ(p[0], 69); CHECK_EQ(p[1], 72); CHECK_EQ(p[2], 76);
}
TEST(pool_diatonic_types_degrees) {
  Rig r; int16_t p[8];
  CHECK_EQ(r.ca.pool(60, 1, false, p), 1); CHECK_EQ(p[0], 60);
  CHECK_EQ(r.ca.pool(60, 2, false, p), 3); CHECK_EQ(p[1], 62); CHECK_EQ(p[2], 67);   // {0,1,4}
  CHECK_EQ(r.ca.pool(60, 4, false, p), 3); CHECK_EQ(p[1], 65);                        // {0,3,4}
  CHECK_EQ(r.ca.pool(60, 5, false, p), 2); CHECK_EQ(p[1], 67);                        // {0,4}
  CHECK_EQ(r.ca.pool(60, 6, false, p), 4); CHECK_EQ(p[3], 69);                        // {0,2,4,5}
  CHECK_EQ(r.ca.pool(60, 7, false, p), 4); CHECK_EQ(p[3], 71);                        // {0,2,4,6}
  CHECK_EQ(r.ca.pool(60, 8, false, p), 5); CHECK_EQ(p[4], 74);                        // {0,2,4,6,8}: degree 8 = 2nd + octave
}
TEST(pool_extended_types_9_to_15) {
  Rig r; int16_t p[8];
  static const int8_t exp[7][4] = {{0, 4, 7, 10}, {0, 4, 7, 11}, {0, 3, 6, 0}, {0, 4, 8, 0}, {0, 4, 6, 10}, {0, 4, 8, 10}, {0, 1, 4, 7}};
  static const int len[7] = {4, 4, 3, 3, 4, 4, 4};
  for (int m = 9; m <= 15; ++m) {
    int n = r.ca.pool(60, m, false, p);
    CHECK_EQ(n, len[m - 9]);
    for (int i = 0; i < n; ++i) CHECK_EQ(p[i], 60 + exp[m - 9][i]);
  }
}
TEST(pool_size_truncates) {
  Rig r; int16_t p[8];
  r.ca.setParam(ChordArp::P_SEL_SIZE, 3);
  CHECK_EQ(r.ca.pool(60, 8, false, p), 3);
  r.ca.setParam(ChordArp::P_SEL_SIZE, 2);
  CHECK_EQ(r.ca.pool(60, 15, false, p), 2);
  CHECK_EQ(p[1], 61);
}
TEST(pool_inversion_up_down) {
  Rig r; int16_t p[8];
  r.ca.setParam(ChordArp::P_INV, 1);
  r.ca.pool(60, 3, false, p);  // lowest note up an octave
  CHECK_EQ(p[0], 72); CHECK_EQ(p[1], 64); CHECK_EQ(p[2], 67);
  r.ca.setParam(ChordArp::P_INV, 2);
  r.ca.pool(60, 3, false, p);
  CHECK_EQ(p[0], 72); CHECK_EQ(p[1], 76); CHECK_EQ(p[2], 67);
  r.ca.setParam(ChordArp::P_INV, -1);
  r.ca.pool(60, 3, false, p);  // highest note down an octave
  CHECK_EQ(p[0], 60); CHECK_EQ(p[1], 64); CHECK_EQ(p[2], 55);
  r.ca.setParam(ChordArp::P_INV, 6);
  r.ca.pool(60, 3, false, p);  // six steps: each octave-up moves the then-lowest voice
  CHECK_EQ(p[0] + p[1] + p[2], 60 + 64 + 67 + 72);
}
TEST(pool_koct) {
  Rig r; int16_t p[8];
  r.ca.setParam(ChordArp::P_KOCT, 1); r.ca.pool(60, 3, false, p); CHECK_EQ(p[0], 72);
  r.ca.setParam(ChordArp::P_KOCT, -1); r.ca.pool(60, 3, false, p); CHECK_EQ(p[0], 48);
}
TEST(pool_transpose_snaps_to_scale_degrees) {
  Rig r; int16_t p[8];
  // with no root held the scale is C major: 60,64,67 = degrees 0,2,4; +1 degree -> D F A
  r.ca.setParam(ChordArp::P_TPOSE, 1);
  int n = r.ca.pool(60, 3, false, p);
  CHECK_EQ(n, 3); CHECK_EQ(p[0], 62); CHECK_EQ(p[1], 65); CHECK_EQ(p[2], 69);
  r.ca.setParam(ChordArp::P_TPOSE, -2);
  n = r.ca.pool(60, 3, false, p);
  CHECK_EQ(p[0], 57); CHECK_EQ(p[1], 60); CHECK_EQ(p[2], 64);
  r.ctx.gtpose = 2; r.ca.setParam(ChordArp::P_TPOSE, 0);  // gtpose alone also transposes (c31 pages[4].gtpose)
  n = r.ca.pool(60, 3, false, p);
  CHECK_EQ(p[0], 64); CHECK_EQ(p[1], 67); CHECK_EQ(p[2], 71);
}
TEST(pool_locked_scale_snaps_chord) {
  Rig r; int16_t p[8];
  r.ca.setParam(ChordArp::P_LOCK_ROOT, 60);
  r.ca.setParam(ChordArp::P_PMODE, 2);
  // root E (64) in locked C major: degree 2; triad {0,2,4} -> E G B
  r.ca.pool(64, 3, false, p);
  CHECK_EQ(p[0], 64); CHECK_EQ(p[1], 67); CHECK_EQ(p[2], 71);
  // extended in lock: root snapped to a lock-scale note first (C# -> degree of nearest = C or D)
  int n = r.ca.pool(61, 9, false, p);
  CHECK_EQ(n, 4); CHECK_EQ(p[0], 60);  // dlk picks the first nearest (C) for a tie
}
TEST(pool_mute_empty) {
  Rig r; int16_t p[8];
  r.ca.setParam(ChordArp::P_MUTE, 1);
  CHECK_EQ(r.ca.pool(60, 3, false, p), 0);
}

// ======================= Chord/Arp: live play and queues =======================
TEST(chord_hold_sounds_release_stops) {
  Rig r;
  r.type(3);
  clearEv();
  r.rootDown(1, 5);  // C major
  int n[8]; int k = ons(n);
  CHECK_EQ(k, 3); CHECK_EQ(n[0], 60); CHECK_EQ(n[1], 64); CHECK_EQ(n[2], 67);
  CHECK_EQ(g_ev[0].vel, 105); CHECK_EQ(g_ev[0].chan, 3);
  clearEv();
  r.rootUp(1, 5);
  CHECK_EQ(countOff(), 3); CHECK_EQ(countOn(), 0);
}
TEST(chord_type_change_immediate_when_chord_speed1) {
  Rig r;
  r.type(3); r.rootDown(1, 5); clearEv();
  r.type(1);  // new type while held: kill + re-sound at once (no queue)
  CHECK_EQ(countOff(), 3); CHECK_EQ(countOn(), 1);
  CHECK_EQ(r.ca.getParam(ChordArp::P_SEL_MOD), 1);
}
TEST(chord_root_queued_to_next_quarter_when_arp) {
  Rig r;
  r.setT(2); r.pulses(96);   // arp queued to the bar
  CHECK_EQ(r.ca.getParam(ChordArp::P_SEL_TYPE), 2);
  r.setS(3); r.pulses(96);   // 1/16
  CHECK_EQ(r.ca.getParam(ChordArp::P_SEL_SPEED), 3);
  r.type(3);
  clearEv();
  r.rootDown(1, 5);
  CHECK_EQ(r.ca.getParam(ChordArp::P_Q_ROOT), 1);
  CHECK_EQ(g_nev, 0);        // nothing yet
}
TEST(settings_queue_applies_at_bar_not_before) {
  Rig r;
  r.ca.start();
  r.setT(2);
  CHECK_EQ(r.ca.getParam(ChordArp::P_Q_TYPE), 2);
  CHECK_EQ(r.ca.getParam(ChordArp::P_SEL_TYPE), 1);
  r.pulses(95);
  CHECK_EQ(r.ca.getParam(ChordArp::P_SEL_TYPE), 1);  // still queued after 95 pulses
  r.pulses(1);
  CHECK_EQ(r.ca.getParam(ChordArp::P_SEL_TYPE), 2);
  CHECK_EQ(r.ca.getParam(ChordArp::P_Q_TYPE), 0);
}
TEST(settings_all_rows_and_size_map) {
  Rig r;
  r.ca.start();
  r.setT(3); r.setS(4); r.setD(2); r.setZ(2); r.groove(9);
  CHECK_EQ(r.ca.getParam(ChordArp::P_Q_SIZE), 4);  // size row maps 5,4,3,2
  r.pulses(96);
  CHECK_EQ(r.ca.getParam(ChordArp::P_SEL_TYPE), 3);
  CHECK_EQ(r.ca.getParam(ChordArp::P_SEL_SPEED), 4);
  CHECK_EQ(r.ca.getParam(ChordArp::P_SEL_DIR), 2);
  CHECK_EQ(r.ca.getParam(ChordArp::P_SEL_SIZE), 4);
  CHECK_EQ(r.ca.getParam(ChordArp::P_SEL_GROOVE), 9);
}
TEST(quarter_queue_chord_in_arp_mode) {
  Rig r;
  r.ca.setParam(ChordArp::P_SEL_TYPE, 2); r.ca.setParam(ChordArp::P_SEL_SPEED, 3);
  r.ca.start();
  r.pulses(10);
  r.type(3); clearEv();
  r.rootDown(1, 5);              // chord type 3 and the root wait for the quarter
  CHECK_EQ(r.ca.getParam(ChordArp::P_Q_ROOT), 1); CHECK_EQ(r.ca.getParam(ChordArp::P_Q_MOD), 3);
  r.pulses(13);                  // pulses 11..23: type not applied yet (c31: held root already arps the old type)
  CHECK_EQ(r.ca.getParam(ChordArp::P_SEL_MOD), 1);
  r.pulses(1);                   // pulse 24: quarter boundary
  CHECK_EQ(r.ca.getParam(ChordArp::P_Q_ROOT), 0); CHECK_EQ(r.ca.getParam(ChordArp::P_SEL_MOD), 3);
  clearEv(); r.pulses(1);        // pulse 25: step 5 of 1/16, arp pointer was reset: first note of the new type
  CHECK_EQ(countOn(), 1); CHECK_EQ(g_ev[g_nev - 1].note, 60);
  r.pulses(6); CHECK_EQ(g_ev[g_nev - 1].note, 64);
}

TEST(gstep_positions_and_swing) {
  Rig r;
  r.ca.setParam(ChordArp::P_SEL_SPEED, 3);  // div 6
  // gstep depends on pulsesIntoBar: use setBarPos(n-1) + pulse() semantic: after pulse() meas=n
  struct { int meas; int step; } c[] = {{1, 1}, {7, 2}, {13, 3}, {91, 16}, {96, 0}, {2, 0}};
  for (int i = 0; i < 6; ++i) {
    // gstep reads meas_ as set by the pulse in flight; emulate via setBarPos
    r.ca.setBarPos((uint8_t)c[i].meas);
    CHECK_EQ(r.ca.gstep(3), c[i].step);
  }
  r.ca.setParam(ChordArp::P_SWING, 2);
  r.ca.setBarPos(7);  CHECK_EQ(r.ca.gstep(3), 0);   // odd step moved 2 pulses later
  r.ca.setBarPos(9);  CHECK_EQ(r.ca.gstep(3), 2);
  r.ca.setBarPos(13); CHECK_EQ(r.ca.gstep(3), 3);   // even step unmoved
  // 1/16T: 24 subdivisions, pattern length 12 plays twice
  r.ca.setParam(ChordArp::P_SWING, 0);
  r.ca.setBarPos(1 + 4 * 12);  CHECK_EQ(r.ca.gstep(4), 1);
  r.ca.setBarPos(1 + 4 * 11);  CHECK_EQ(r.ca.gstep(4), 12);
  // 1/8 : div 12, pattern 8
  r.ca.setBarPos(1 + 12 * 7);  CHECK_EQ(r.ca.gstep(1), 8);
  // 1/8T : div 8, pattern 12
  r.ca.setBarPos(1 + 8 * 11);  CHECK_EQ(r.ca.gstep(2), 12);
}
TEST(gstep_swing_conserved_over_bar) {
  Rig r;
  r.ca.setParam(ChordArp::P_SEL_SPEED, 3);
  for (int sw = 0; sw <= 2; ++sw) {
    r.ca.setParam(ChordArp::P_SWING, sw);
    int fired = 0;
    for (int m = 1; m <= 96; ++m) { r.ca.setBarPos((uint8_t)m); if (r.ca.gstep(3)) fired++; }
    CHECK_EQ(fired, 16);  // swing never drops or doubles a step
  }
}

static void armRig(Rig& r, int dirv, int type = 3, int alt = 0) {
  r.ca.setParam(ChordArp::P_SEL_TYPE, alt ? 3 : 2);
  r.ca.setParam(ChordArp::P_SEL_SPEED, 3);
  r.ca.setParam(ChordArp::P_SEL_DIR, dirv);
  r.ca.setParam(ChordArp::P_SEL_MOD, type);
  r.ca.setParam(ChordArp::P_GATE, 2);  // legato: no gate timer noise
  r.ca.start();
  r.rootDown(1, 5);
  clearEv();
}
static int arpNotes(Rig& r, int* out, int steps) {
  int n = 0;
  for (int i = 0; i < steps * 6 && n < 64; ++i) {
    int before = g_nev; r.ca.pulse();
    for (int j = before; j < g_nev; ++j) if (g_ev[j].kind == EV_NOTE_ON) out[n++] = g_ev[j].note;
  }
  return n;
}
#include "test_playhead.inc"
#include "test_cases2.inc"
