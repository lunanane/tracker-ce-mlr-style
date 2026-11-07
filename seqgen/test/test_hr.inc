// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Tests of the ported HYPER_RASP scripts (hr_*.cpp). Included by test_main.cpp. Expected values are derived by hand from the Lua scripts
// (E:\claude\_HYPER_RASP\scripts); hr_compare.py compares the whole behaviour against the real Lua.
#include "../hr_scripts.h"

namespace {
struct HrRig {
  HrBus bus; HrSlot slot; HrScript* s;
  explicit HrRig(int type) { s = slot.create((uint8_t)type); s->setBus(&bus); s->setSink(rec, 0); s->setChannel(3); clearEv(); }
  void tap(int col, int row) { s->pad(col, row, true); s->pad(col, row, false); }
  void pulses(int n) { for (int i = 0; i < n; ++i) s->pulse(); }
  void ticks(int n) { for (int i = 0; i < n; ++i) s->tick(); }
  int led(int col, int row) { uint8_t o[48]; s->leds(o); return o[row * 12 + col]; }
  // the last note-on of the recording
  int lastOn() const { for (int i = g_nev - 1; i >= 0; --i) if (g_ev[i].kind == EV_NOTE_ON) return i; return -1; }
};
}  // namespace

// ======================= Chord Stabs =======================
TEST(hr_stab_press_sounds_the_chord_and_events_carry_the_channel) {
  HrRig r(HR_CHORD_STABS);
  r.tap(0, 0);  // root 1 = C
  int n[8];
  CHECK_EQ(countOn(), 3);
  ons(n);
  CHECK_EQ(n[0], 60); CHECK_EQ(n[1], 64); CHECK_EQ(n[2], 67);
  CHECK_EQ(g_ev[0].vel, 78); CHECK_EQ(g_ev[0].chan, 3);  // revoice: ACCENT x = 0.78 of vel 100
  CHECK_EQ(r.s->getParam(HrStab::P_ROOT), 1);
  CHECK(r.s->live());
}
TEST(hr_stab_gate_is_counted_in_pulses_and_the_offbeat_hit_lands_on_step_3) {
  HrRig r(HR_CHORD_STABS);  // default pattern 3 "offbeat" ..X...x...X...x. gate 3
  r.tap(0, 0);
  clearEv();
  r.pulses(2);
  CHECK_EQ(countOff(), 0);
  r.pulses(1);
  CHECK_EQ(countOff(), 3);  // left 3 -> 0 on the third pulse
  clearEv();
  r.pulses(9);              // pulses 4..12: positions 3..11, rests
  CHECK_EQ(g_nev, 0);
  r.pulses(1);              // pulse 13: position 12 = step 3 'X'
  CHECK_EQ(countOn(), 3);
  CHECK_EQ(g_ev[0].vel, 100);
  CHECK_EQ(r.led(0, 0), 15);  // the blink: the root flashes for 3 pulses
  r.pulses(3);
  CHECK_EQ(r.led(0, 0), 8);
}
TEST(hr_stab_same_root_unlatches_and_stops_the_voices) {
  HrRig r(HR_CHORD_STABS);
  r.tap(0, 0);
  clearEv();
  r.tap(0, 0);
  CHECK_EQ(countOff(), 3); CHECK_EQ(countOn(), 0);
  CHECK_EQ(r.s->getParam(HrStab::P_ROOT), 0);
  CHECK(!r.s->live());
  CHECK_EQ(r.led(0, 0), 3);
}
TEST(hr_stab_quality_chord_and_extra_pads_revoice) {
  HrRig r(HR_CHORD_STABS);
  r.tap(0, 0);
  clearEv();
  r.tap(0, 3);  // MINOR (x 1, y 4)
  int n[8]; ons(n);
  CHECK_EQ(countOn(), 3); CHECK_EQ(n[1], 63);
  clearEv();
  r.tap(6, 0);  // chord 1 is the current one: nothing
  CHECK_EQ(g_nev, 0);
  r.tap(6, 2);  // chord 3 = 7th: C Eb G Bb
  CHECK_EQ(countOn(), 4);
  clearEv();
  r.tap(9, 0);  // Oct+ (extra pad col 9, row 0)
  ons(n);
  CHECK_EQ(r.s->getParam(HrStab::P_OCTAVE), 1);
  CHECK_EQ(n[0], 72);
  r.tap(8, 3);  // Oct 0
  CHECK_EQ(r.s->getParam(HrStab::P_OCTAVE), 0);
  clearEv();
  r.tap(11, 0);  // Tran+
  ons(n);
  CHECK_EQ(r.s->getParam(HrStab::P_TRANSP), 1); CHECK_EQ(n[0], 61);
  r.tap(10, 0);  // Tran-
  r.tap(11, 3);  // Tran 0
  CHECK_EQ(r.s->getParam(HrStab::P_TRANSP), 0);
  // vel and rate pads
  r.tap(11, 1);
  CHECK_EQ(r.s->getParam(HrStab::P_VEL), 108);
  r.tap(10, 3);
  CHECK_EQ(r.s->getParam(HrStab::P_VEL), 100);
  r.tap(9, 1);  // Faster: 6 -> 4 pulses
  CHECK_EQ(r.s->getParam(HrStab::P_RATE), 4);
  r.tap(8, 1);  // Slower: 4 -> 6
  r.tap(8, 1);  // 6 -> 8
  CHECK_EQ(r.s->getParam(HrStab::P_RATE), 8);
  r.tap(9, 3);  // reset
  CHECK_EQ(r.s->getParam(HrStab::P_RATE), 6);
}
TEST(hr_stab_publishes_scale_and_root_on_the_bus) {
  HrRig r(HR_CHORD_STABS);
  r.tap(0, 0);  // C major
  CHECK_EQ(r.bus.nScale, 7); CHECK_EQ(r.bus.scale[0], 0); CHECK_EQ(r.bus.scale[2], 4); CHECK_EQ(r.bus.scale[6], 11); CHECK_EQ(r.bus.root, 60);
  r.tap(0, 3);  // minor
  CHECK_EQ(r.bus.scale[2], 3); CHECK_EQ(r.bus.scale[6], 10);
  r.tap(3, 1);  // root 10 = A, minor
  CHECK_EQ(r.bus.scale[0], 9); CHECK_EQ(r.bus.scale[1], 11); CHECK_EQ(r.bus.root, 69);
}

// ======================= Arp Lines =======================
TEST(hr_arp_runner_plays_one_note_of_the_chord_per_step) {
  HrRig r(HR_ARP_LINES);
  r.tap(0, 0);
  CHECK_EQ(countOn(), 1); CHECK_EQ(g_ev[0].note, 60); CHECK_EQ(g_ev[0].vel, 100);  // the root, at the beat level
  clearEv();
  r.pulses(1);  // step 1 begins: the voice is replaced
  CHECK_EQ(g_nev, 2); CHECK_EQ(g_ev[0].kind, EV_NOTE_OFF); CHECK_EQ(g_ev[1].note, 60); CHECK_EQ(g_ev[1].vel, 100);
  clearEv();
  r.pulses(6);  // pulse 7 = step 2: degree 2 = the third, offbeat level 0.78
  int i = r.lastOn();
  CHECK(i >= 0); CHECK_EQ(g_ev[i].note, 64); CHECK_EQ(g_ev[i].vel, 78);
  clearEv();
  r.pulses(12);  // steps 3 and 4: G, then the octave of the root ... degree 4 of a triad = root + 12
  int n[8]; ons(n);
  CHECK_EQ(n[0], 67); CHECK_EQ(n[1], 72);
}
TEST(hr_arp_follower_plays_the_degrees_of_the_published_scale) {
  HrRig r(HR_ARP_LINES);
  const uint8_t cmaj[7] = {0, 2, 4, 5, 7, 9, 11};
  r.bus.setScale(cmaj, 7); r.bus.root = 60;
  r.s->setParam(HrStab::P_ROLE, 2);
  CHECK_EQ(r.led(0, 0), 3);
  r.tap(0, 0);  // key 1 = degree 1 of the scale: C
  CHECK_EQ(g_ev[0].note, 60);
  clearEv();
  r.tap(1, 0);  // key 2 = D: the triad built on the second degree is D F A
  CHECK_EQ(g_ev[g_nev - 1].note, 62);
  r.pulses(7);  // step 2 = the chord third (F), the follower never published anything
  int i = r.lastOn();
  CHECK_EQ(g_ev[i].note, 65);
  CHECK_EQ(r.bus.root, 60);
  // a leader publishes
  r.s->setParam(HrStab::P_ROLE, 1);
  r.tap(2, 0);
  CHECK_EQ(r.bus.root, 62 + 0);  // root key 3 = pitch class 3 -> note 62
}
TEST(hr_arp_pattern_pads_and_euclid_patterns) {
  HrRig r(HR_ARP_LINES);
  r.tap(0, 0);
  r.tap(5, 3);  // pattern 10 = "seven": euclid 7 of 16, degrees 1 2 3 4 3 2 5
  CHECK_EQ(r.s->getParam(HrStab::P_PATTERN), 10);
  r.s->start();  // pulse_position = 0
  clearEv();
  r.pulses(1);  // step 1 is an onset (E(7,16) is rotated to start on one): degree 1
  CHECK_EQ(g_ev[g_nev - 1].note, 60);
  CHECK_EQ(g_ev[g_nev - 1].vel, 100);  // index 1 of a euclid pattern = 'X'
}
TEST(hr_stab_arp_serialize_roundtrip_and_validation) {
  HrRig a(HR_ARP_LINES), b(HR_ARP_LINES);
  a.tap(2, 3); a.tap(7, 3); a.s->setParam(HrStab::P_OCTAVE, -2); a.s->setParam(HrStab::P_RATE, 3); a.s->setParam(HrStab::P_VEL, 77);
  a.s->setParam(HrStab::P_TRANSP, -7); a.s->setParam(HrStab::P_ROLE, 2);
  uint8_t buf[HrScript::kMaxSerial];
  size_t n = a.s->serialize(buf, sizeof buf);
  CHECK_EQ(n, HrStab::kSerialSize);
  CHECK_EQ(a.s->serialize(buf, 3), 0);
  CHECK(b.s->deserialize(buf, n));
  CHECK_EQ(b.s->getParam(HrStab::P_PATTERN), 7); CHECK_EQ(b.s->getParam(HrStab::P_CHORD), 8); CHECK_EQ(b.s->getParam(HrStab::P_OCTAVE), -2);
  CHECK_EQ(b.s->getParam(HrStab::P_RATE), 3); CHECK_EQ(b.s->getParam(HrStab::P_VEL), 77); CHECK_EQ(b.s->getParam(HrStab::P_TRANSP), -7);
  CHECK_EQ(b.s->getParam(HrStab::P_ROLE), 2);
  uint8_t bad[HrScript::kMaxSerial];
  for (size_t i = 1; i < n; ++i) {
    for (size_t k = 0; k < n; ++k) bad[k] = buf[k];
    bad[i] = 200;
    HrRig c(HR_ARP_LINES);
    CHECK(!c.s->deserialize(bad, n));
  }
  CHECK(!b.s->deserialize(buf, n - 1));
  HrRig st(HR_CHORD_STABS);
  CHECK(!st.s->deserialize(buf, n));  // an arp block is not a stab block
}

// ======================= Arp Seq =======================
TEST(hr_arpseq_root_arps_the_chord_and_cycle_buttons_change_it) {
  HrRig r(HR_ARP_SEQ);
  r.tap(0, 0);  // C: arp up at 1/16, first note at once
  CHECK_EQ(countOn(), 1); CHECK_EQ(g_ev[0].note, 60); CHECK_EQ(g_ev[0].vel, 100);
  clearEv();
  r.pulses(5);
  CHECK_EQ(g_nev, 0);
  r.pulses(1);  // 6 pulses per step at speed 3
  CHECK_EQ(g_nev, 2); CHECK_EQ(g_ev[1].note, 64);
  clearEv();
  r.tap(1, 2);  // MODE cycle -> chord: the whole voicing
  CHECK_EQ(countOn(), 3);
  clearEv();
  r.tap(0, 2);  // quality -> minor
  int n[8]; ons(n);
  CHECK_EQ(n[1], 63);
  CHECK_EQ(r.led(0, 2), 12);  // the fade starts bright in the other colour (fade_left 16 -> 12 * 8 / 8) ...
  r.ticks(8);
  CHECK_EQ(r.led(0, 2), 0);   // ... dims to dark at the half ...
  r.ticks(8);
  CHECK_EQ(r.led(0, 2), 12);  // ... and comes back in the new colour
}
TEST(hr_arpseq_chord_keys_overlapping_hold_picks_the_type) {
  HrRig r(HR_ARP_SEQ);
  r.tap(1, 2);  // chord mode
  r.tap(0, 0);
  clearEv();
  r.s->pad(6, 0, true);   // key 1: maj
  r.s->pad(6, 1, true);   // + key 2: "1,2" = maj7
  int n[8]; ons(n);
  CHECK_EQ(r.s->getParam(HrArpSeq::P_CHORD_TYPE), 3);
  CHECK_EQ(countOn(), 3 + 4);  // maj (key 1 alone) re-voiced, then maj7
  r.s->pad(6, 1, false); r.s->pad(6, 0, false);
  CHECK_EQ(r.led(6, 0), 15); CHECK_EQ(r.led(6, 1), 10);  // the entered order stays on the pads (LEVEL_CHORD_ORDER 15, 10, 6)
  r.s->pad(6, 0, true); r.s->pad(6, 0, false);  // key 1 again = maj: not the current type, so selected
  CHECK_EQ(r.s->getParam(HrArpSeq::P_CHORD_TYPE), 1);
}
TEST(hr_arpseq_programs_steps_loops_and_plays) {
  HrRig r(HR_ARP_SEQ);
  r.tap(0, 0);          // latch C (manual)
  r.tap(0, 3);          // store step 1 = C major arp
  r.tap(0, 0);          // unlatch C
  r.tap(2, 0);          // latch D (manual, root 3)
  r.tap(1, 3);          // store step 2 = D
  CHECK_EQ(r.led(0, 3), 10); CHECK_EQ(r.led(1, 3), 10); CHECK_EQ(r.led(2, 3), 1);
  r.s->pad(1, 3, true); r.s->pad(3, 3, true); r.s->pad(3, 3, false); r.s->pad(1, 3, false);  // hold step 2 + step 4 = loop 2..4
  CHECK_EQ(r.s->getParam(HrArpSeq::P_START), 2); CHECK_EQ(r.s->getParam(HrArpSeq::P_END), 4);
  CHECK_EQ(r.led(0, 3), 10); CHECK_EQ(r.led(2, 3), 1); CHECK_EQ(r.led(7, 3), 0);
  r.tap(0, 0);          // another manual press (C): the sequencer will not take over
  r.tap(7, 2);          // PLAY
  CHECK_EQ(r.s->getParam(HrArpSeq::P_PLAYING), 1);
  CHECK_EQ(r.led(7, 2), 15);
  r.tap(0, 0);          // unlatch: manual off
  clearEv();
  r.pulses(384);        // step 2 -> 3 (empty, nothing applied) ... the current step moves 1 -> 2 (loop start is 2)
  CHECK_EQ(r.s->getParam(HrArpSeq::P_CURRENT), 2);
  CHECK(g_nev > 0);     // step 2 (D) took over and sounds
  r.tap(7, 2);
  CHECK_EQ(r.s->getParam(HrArpSeq::P_PLAYING), 0);
}
TEST(hr_arpseq_serialize_keeps_the_steps) {
  HrRig a(HR_ARP_SEQ), b(HR_ARP_SEQ);
  a.tap(2, 0); a.tap(4, 2); a.tap(5, 2); a.s->pad(6, 1, true); a.s->pad(6, 1, false); a.tap(3, 3);
  uint8_t buf[HrScript::kMaxSerial];
  size_t n = a.s->serialize(buf, sizeof buf);
  CHECK_EQ(n, HrArpSeq::kSerialSize);
  CHECK(b.s->deserialize(buf, n));
  uint8_t buf2[HrScript::kMaxSerial];
  CHECK_EQ(b.s->serialize(buf2, sizeof buf2), n);
  for (size_t i = 0; i < n; ++i) CHECK_EQ(buf[i], buf2[i]);
  CHECK_EQ(b.led(3, 3), 10);
  for (size_t i = 1; i < n; ++i) {
    uint8_t bad[HrScript::kMaxSerial];
    for (size_t k = 0; k < n; ++k) bad[k] = buf[k];
    bad[i] = 0xFF;
    HrRig c(HR_ARP_SEQ);
    CHECK(!c.s->deserialize(bad, n) || i > 13);  // only the step area can hold bytes that are all ranges; everything in the header is range checked
  }
}

// ======================= HR Chord/Arp =======================
TEST(hr_chordarp_view1_root_sounds_the_chord_and_the_mod_keys_pick_types) {
  HrRig r(HR_CHORD_ARP);
  r.tap(0, 0);  // ROOTS[1][1] = 64: E major, chord mode, vel 100
  int n[8];
  CHECK_EQ(countOn(), 3); ons(n);
  CHECK_EQ(n[0], 64); CHECK_EQ(n[1], 68); CHECK_EQ(n[2], 71); CHECK_EQ(g_ev[0].vel, 100);
  CHECK_EQ(r.led(0, 0), 15); CHECK_EQ(r.led(1, 0), 3);
  clearEv();
  r.tap(6, 1);  // mod key 2 alone = min: the latched chord is re-sounded
  CHECK_EQ(countOff(), 3); CHECK_EQ(countOn(), 3);
  ons(n); CHECK_EQ(n[1], 67);
  clearEv();
  r.s->pad(6, 0, true); r.s->pad(6, 1, true);  // keys 1+2 together = maj7
  CHECK_EQ(r.s->getParam(HrChordArp::P_TYPE), 5);
  CHECK_EQ(countOn(), 4);
  r.s->pad(6, 0, false); r.s->pad(6, 1, false);
  CHECK_EQ(r.s->getParam(HrChordArp::P_TYPE), 5);  // releasing a combo selects nothing more
  CHECK_EQ(r.led(6, 0), 15); CHECK_EQ(r.led(6, 1), 15); CHECK_EQ(r.led(6, 2), 3);
  CHECK_EQ(r.s->getParam(HrChordArp::P_ROOT_X), 1);
  clearEv();
  r.tap(0, 0);  // latched: the same root again stops it
  CHECK_EQ(r.s->getParam(HrChordArp::P_ROOT_X), 0); CHECK_EQ(countOff(), 4); CHECK(!r.s->live());
}
TEST(hr_chordarp_function_column_switches_views_and_runs_the_combos) {
  HrRig r(HR_CHORD_ARP);
  CHECK_EQ(r.led(7, 0), 15); CHECK_EQ(r.led(7, 1), 3);
  r.tap(7, 1);
  CHECK_EQ(r.s->getParam(HrChordArp::P_VIEW), 2);
  CHECK_EQ(r.led(7, 1), 15); CHECK_EQ(r.led(7, 0), 3);
  CHECK_EQ(r.led(1, 2), 3); CHECK_EQ(r.led(0, 2), 15);  // view 2: chord mode lit, arp dim
  r.tap(1, 2);   // Arp
  r.tap(2, 0);   // direction 3
  r.tap(2, 1);   // speed 3 = 1/16 (6 pulses)
  CHECK_EQ(r.s->getParam(HrChordArp::P_MODE), 2); CHECK_EQ(r.s->getParam(HrChordArp::P_DIR), 3); CHECK_EQ(r.s->getParam(HrChordArp::P_SPEED), 3);
  r.tap(0, 0);   // still view 2: size 5 pad (no sound)
  r.s->pad(7, 2, true); r.s->pad(7, 3, true); r.s->pad(7, 3, false); r.s->pad(7, 2, false);  // keys 3+4: page toggle
  CHECK_EQ(r.s->getParam(HrChordArp::P_PAGE), 2);
  CHECK_EQ(r.s->getParam(HrChordArp::P_VIEW), 2);  // the combo did not switch the view
  r.tap(7, 0);
  CHECK_EQ(r.s->getParam(HrChordArp::P_VIEW), 1);
  r.tap(0, 0);   // E, arp up-down at 1/16
  clearEv();
  r.pulses(6);
  int i = r.lastOn();
  CHECK(i >= 0); CHECK_EQ(g_ev[i].note, 68);  // the arp moved to the second chord note
}
TEST(hr_chordarp_latch_off_extra_pad_and_momentary_roots) {
  HrRig r(HR_CHORD_ARP);
  r.tap(0, 0);
  CHECK_EQ(countOn(), 3);
  clearEv();
  r.tap(11, 1);  // Latch pad: momentary
  CHECK_EQ(r.s->getParam(HrChordArp::P_LATCH), 1);
  CHECK_EQ(countOff(), 3);  // the chord stops with the latch
  clearEv();
  r.s->pad(1, 0, true);     // a momentary root sounds while held ...
  CHECK_EQ(countOn(), 3);
  clearEv();
  r.s->pad(1, 0, false);    // ... and stops on release
  CHECK_EQ(countOff(), 3);
  CHECK(!r.s->live());
}
TEST(hr_chordarp_queued_start_waits_for_the_quarter_note) {
  HrRig r(HR_CHORD_ARP);
  r.s->setParam(HrChordArp::P_QSTART, 2);
  r.tap(0, 0);
  CHECK_EQ(g_nev, 0);  // queued
  CHECK_EQ(r.led(0, 0), 10);
  r.pulses(1);         // pulse_index 0 is a beat: the queued root starts
  CHECK_EQ(countOn(), 3);
  CHECK_EQ(r.led(0, 0), 15);
}
TEST(hr_chordarp_tracker_records_and_plays_steps) {
  HrRig r(HR_CHORD_ARP);
  r.tap(7, 3);          // view 4
  r.tap(0, 1);          // nothing to store yet (no root played)
  CHECK_EQ(r.led(0, 1), 1);
  r.tap(7, 0);
  r.tap(0, 0);          // E major
  r.tap(7, 3);
  r.tap(0, 1);          // step 1, duration 2
  r.tap(2, 3);          // step 3, duration 4
  CHECK_EQ(r.led(0, 0), 3); CHECK_EQ(r.led(0, 1), 10); CHECK_EQ(r.led(0, 2), 2);
  r.s->pad(5, 0, true); r.tap(2, 3); r.s->pad(5, 0, false);  // clear key held + the cell: empty again
  CHECK_EQ(r.led(2, 3), 1);
  r.tap(7, 0);
  r.tap(0, 0);          // unlatch (the chord of the root key stops)
  clearEv();
  r.s->pad(7, 0, true); r.s->pad(7, 1, true); r.s->pad(7, 1, false); r.s->pad(7, 0, false);  // keys 1+2: tracker play
  CHECK_EQ(r.s->getParam(HrChordArp::P_PLAYING), 1);
  CHECK_EQ(countOn(), 3);   // step 1 plays at once
  clearEv();
  r.pulses(12);             // duration 2 = 12 pulses, then step 2 (empty): the chord ends
  CHECK_EQ(countOff(), 3);
  r.s->silence();
  CHECK_EQ(r.s->getParam(HrChordArp::P_PLAYING), 0);
}
TEST(hr_chordarp_serialize_roundtrip) {
  HrRig a(HR_CHORD_ARP), b(HR_CHORD_ARP);
  a.s->setParam(HrChordArp::P_TYPE, 7); a.s->setParam(HrChordArp::P_INV, -3); a.s->setParam(HrChordArp::P_GROOVE, 9);
  a.s->setParam(HrChordArp::P_KEYLOCK, 2); a.s->setParam(HrChordArp::P_KEY, 5); a.s->setParam(HrChordArp::P_VEL, 90);
  a.tap(7, 3); a.tap(7, 0); a.tap(1, 1); a.tap(7, 3); a.tap(3, 2);
  uint8_t buf[HrScript::kMaxSerial];
  size_t n = a.s->serialize(buf, sizeof buf);
  CHECK_EQ(n, HrChordArp::kSerialSize);
  CHECK(b.s->deserialize(buf, n));
  uint8_t buf2[HrScript::kMaxSerial];
  b.s->serialize(buf2, sizeof buf2);
  for (size_t i = 0; i < n; ++i) CHECK_EQ(buf[i], buf2[i]);
  CHECK_EQ(b.s->getParam(HrChordArp::P_KEYLOCK), 2); CHECK_EQ(b.s->getParam(HrChordArp::P_INV), -3);
  for (size_t i = 1; i < 17; ++i) {
    uint8_t bad[HrScript::kMaxSerial];
    for (size_t k = 0; k < n; ++k) bad[k] = buf[k];
    bad[i] = 0xFF;
    HrRig c(HR_CHORD_ARP);
    CHECK(!c.s->deserialize(bad, n));
  }
}

// ======================= Bassline Seq =======================
TEST(hr_bass_held_keys_are_notes_in_press_order_and_the_euclid_row_is_the_rhythm) {
  HrRig r(HR_BASS);
  r.s->pad(0, 2, true);  // lowest row, first key = degree 0 = D# (tonic of the default scale, in the octave of the default root 60) = 51
  r.s->pad(1, 2, true);  // degree 1 = F = 53
  r.tap(0, 3);           // E(1,8): one onset in eight steps
  r.s->start();          // the first step sounds at once: the first held key
  CHECK_EQ(countOn(), 1); CHECK_EQ(g_ev[0].note, 51); CHECK_EQ(g_ev[0].vel, 100); CHECK_EQ(g_ev[0].chan, 3);
  clearEv();
  r.pulses(6);           // step 2: no onset, the note ends
  CHECK_EQ(g_nev, 1); CHECK_EQ(g_ev[0].kind, EV_NOTE_OFF); CHECK_EQ(g_ev[0].note, 51);
  clearEv();
  r.pulses(42);          // 7 more steps: the pattern wraps, the second held key follows (press order)
  CHECK_EQ(countOn(), 1); CHECK_EQ(g_ev[g_nev - 1].note, 53);
  CHECK_EQ(r.led(0, 3), 15);
  r.s->pad(0, 2, false); r.s->pad(1, 2, false);
  CHECK_EQ(countOff(), 1);   // the last key up stops the ringing note
  r.s->stop();
  CHECK_EQ(r.led(0, 3), 1);  // the live preset is gone: a plain preset pad
}
TEST(hr_bass_root_scale_octave_and_out_of_range_keys) {
  HrRig r(HR_BASS);
  r.bus.root = 62; r.bus.nScale = 0;  // no scale: chromatic from the root
  r.s->setParam(HrBass::P_OCTAVE, 1);
  r.tap(0, 2); r.s->pad(0, 2, true);
  r.tap(0, 3); r.s->start();
  CHECK_EQ(g_ev[g_nev - 1].note, 62 + 12);
  r.bus.root = 127;                                     // the top keys leave MIDI: dark pads, dropped (never clamped)
  CHECK_EQ(r.led(7, 0), 0);
  r.s->silence();
  CHECK(r.s->live());  // a key is still down
}
TEST(hr_bass_record_stores_the_preset_and_serialize_keeps_it) {
  HrRig a(HR_BASS), b(HR_BASS);
  a.tap(2, 3);                  // E(3,8)
  a.s->start();
  a.s->pad(7, 3, true);         // hold Rec
  a.pulses(6 * 16);             // two chains of the live preset are stored
  a.s->pad(7, 3, false);
  CHECK_EQ(a.s->getParam(HrBass::P_CHAINS), 2);
  a.s->stop();
  uint8_t buf[HrScript::kMaxSerial];
  size_t n = a.s->serialize(buf, sizeof buf);
  CHECK_EQ(n, HrBass::kSerialSize);
  CHECK(b.s->deserialize(buf, n));
  CHECK_EQ(b.s->getParam(HrBass::P_CHAINS), 2);
  uint8_t buf2[HrScript::kMaxSerial];
  CHECK_EQ(b.s->serialize(buf2, sizeof buf2), n);
  for (size_t i = 0; i < n; ++i) CHECK_EQ(buf[i], buf2[i]);
  buf[3] = 99;                  // groove out of range
  CHECK(!b.s->deserialize(buf, n));
}

// ======================= Euclid Trigger =======================
TEST(hr_euclid_rows_are_drums_tap_again_mutes_and_chance_gates_the_hits) {
  HrRig r(HR_EUCLID);
  r.tap(0, 0);   // row 1 (bass drum, kit "bd sd oh ch" = note 36): E(1,8)
  r.tap(2, 1);   // row 2 (snare, 38): E(3,8)
  r.s->start();  // step 1: both rows have an onset
  CHECK_EQ(countOn(), 2); CHECK_EQ(g_ev[0].note, 36); CHECK_EQ(g_ev[1].note, 38); CHECK_EQ(g_ev[0].vel, 100);
  CHECK_EQ(r.led(0, 0), 15); CHECK_EQ(r.led(2, 1), 15); CHECK_EQ(r.led(1, 0), 1);
  clearEv();
  r.pulses(6);   // step 2: nothing hits, both notes end
  CHECK_EQ(countOff(), 2); CHECK_EQ(countOn(), 0);
  CHECK_EQ(r.led(0, 0), 6);
  r.tap(0, 0);   // the lit preset again: the row is off
  CHECK_EQ(r.led(0, 0), 3);
  r.s->setParam(HrEuclid::P_CHANCE, 0);
  clearEv();
  r.pulses(6 * 8);
  CHECK_EQ(countOn(), 0);  // chance 0: nothing sounds (snare row included)
  r.s->setParam(HrEuclid::P_GROUPING, 4);  // "bd toms": row 2 = low tom 41
  CHECK_EQ(r.s->getParam(HrEuclid::P_NOTE2), 41);
  r.s->stop();
  CHECK_EQ(r.led(2, 1), 1);
}
TEST(hr_euclid_record_and_serialize_per_row) {
  HrRig a(HR_EUCLID), b(HR_EUCLID);
  a.tap(3, 2);                 // row 3: E(4,8)
  a.s->start();
  a.s->pad(7, 2, true);        // hold Rec of row 3
  a.pulses(6 * 8);             // one chain
  a.s->pad(7, 2, false);
  a.s->stop();
  uint8_t buf[HrScript::kMaxSerial];
  size_t n = a.s->serialize(buf, sizeof buf);
  CHECK_EQ(n, HrEuclid::kSerialSize);
  CHECK(b.s->deserialize(buf, n));
  clearEv();
  b.s->start();                // the stored pattern of row 3 plays (step 1 of E(4,8) is an onset): row 3 of the default kit = open hat 46
  CHECK_EQ(countOn(), 1); CHECK_EQ(g_ev[0].note, 46);
  uint8_t buf2[HrScript::kMaxSerial];
  CHECK_EQ(b.s->serialize(buf2, sizeof buf2), n);
  for (size_t i = 0; i < n; ++i) CHECK_EQ(buf[i], buf2[i]);
  buf[1] = 9;
  CHECK(!b.s->deserialize(buf, n));
}

// ======================= Grid Seq =======================
TEST(hr_gridseq_tap_toggles_steps_and_the_kit_plays_in_columns) {
  HrRig r(HR_GRIDSEQ);
  r.tap(0, 0); r.tap(0, 1); r.tap(2, 0);  // bass drum + snare on step 1, bass drum on step 3
  CHECK_EQ(r.led(0, 0), 15); CHECK_EQ(r.led(1, 0), 1); CHECK_EQ(r.led(1, 1), 1);
  r.s->start();
  CHECK_EQ(countOn(), 2); CHECK_EQ(g_ev[0].note, 36); CHECK_EQ(g_ev[1].note, 38); CHECK_EQ(g_ev[0].vel, 100);
  CHECK_EQ(r.led(0, 3), 4);  // the playhead marks the empty step it sits on
  clearEv();
  r.pulses(6);   // step 2: the notes end
  CHECK_EQ(countOff(), 2); CHECK_EQ(countOn(), 0);
  clearEv();
  r.pulses(6);   // step 3: the bass drum
  CHECK_EQ(countOn(), 1); CHECK_EQ(g_ev[g_nev - 1].note, 36);
  r.tap(2, 0);   // the same cell again: step off
  CHECK_EQ(r.led(2, 0), 4);
  r.s->setParam(HrGridSeq::P_RANGE, 24);  // loop 2..4: the playhead moves in at once
  CHECK_EQ(r.s->getParam(HrGridSeq::P_START), 2); CHECK_EQ(r.s->getParam(HrGridSeq::P_END), 4);
  CHECK_EQ(r.led(0, 0), 15);  // a step outside the loop keeps its light
  CHECK_EQ(r.led(7, 3), 0);
}
TEST(hr_gridseq_holding_a_cell_ratchets_the_row_and_does_not_edit) {
  HrRig r(HR_GRIDSEQ);
  r.tap(3, 0);                    // bass drum on step 4 (the loop is at step 1)
  r.s->start();
  clearEv();
  r.s->pad(3, 0, true);           // hold it: after the one-quarter grace (24 pulses) the voice retriggers every 6 pulses
  r.pulses(23);
  int before = countOn();
  CHECK_EQ(before, 1);            // only the sequencer's own hit when the loop passed step 4
  r.pulses(1);                    // the grace ends: the first retrigger
  CHECK_EQ(countOn(), before + 1);
  r.pulses(6);
  CHECK_EQ(countOn(), before + 2);
  r.s->pad(3, 0, false);          // released after it ratcheted: the step is not toggled
  CHECK_EQ(r.led(3, 0), 15);
  r.s->pad(3, 0, true); r.s->pad(3, 0, false);  // a plain tap does
  CHECK(r.led(3, 0) != 15);
}
TEST(hr_gridseq_serialize_keeps_the_pattern_and_settings) {
  HrRig a(HR_GRIDSEQ), b(HR_GRIDSEQ);
  a.tap(1, 2); a.tap(5, 3);
  a.s->setParam(HrGridSeq::P_GROUPING, 4); a.s->setParam(HrGridSeq::P_TRANSP, -3); a.s->setParam(HrGridSeq::P_DIV, 3); a.s->setParam(HrGridSeq::P_RANGE, 27);
  uint8_t buf[HrScript::kMaxSerial];
  size_t n = a.s->serialize(buf, sizeof buf);
  CHECK_EQ(n, HrGridSeq::kSerialSize);
  CHECK(b.s->deserialize(buf, n));
  CHECK_EQ(b.led(1, 2), 15); CHECK_EQ(b.led(5, 3), 15);
  CHECK_EQ(b.s->getParam(HrGridSeq::P_GROUPING), 4); CHECK_EQ(b.s->getParam(HrGridSeq::P_DIV), 3); CHECK_EQ(b.s->getParam(HrGridSeq::P_NOTE2), 41 - 3);
  uint8_t buf2[HrScript::kMaxSerial];
  CHECK_EQ(b.s->serialize(buf2, sizeof buf2), n);
  for (size_t i = 0; i < n; ++i) CHECK_EQ(buf[i], buf2[i]);
  buf[4] = 0;
  CHECK(!b.s->deserialize(buf, n));
}

// ======================= Step Follower =======================
TEST(hr_stepfollow_steps_are_scale_degrees_of_the_shared_scale_and_root) {
  HrRig r(HR_STEPFOLLOW);
  r.tap(0, 3); r.tap(1, 2); r.tap(1, 3);  // window base 0: row 4 = degree 0, row 3 = degree 1; step 1 = degree 0, step 2 = degrees 1 + 0
  CHECK_EQ(r.led(0, 3), 15); CHECK_EQ(r.led(1, 2), 15);
  r.s->start();  // default scale D# minor, default root 60: degree 0 = 51, degree 1 = 53
  CHECK_EQ(countOn(), 1); CHECK_EQ(g_ev[0].note, 51); CHECK_EQ(g_ev[0].vel, 100);
  clearEv();
  r.pulses(6);   // step 2: the first column ends, the second sounds degree 0 and 1
  CHECK_EQ(countOff(), 1); CHECK_EQ(countOn(), 2);
  int n[8]; ons(n);
  CHECK_EQ(n[0], 51); CHECK_EQ(n[1], 53);        // sorted by degree
  r.bus.nScale = 0; r.bus.root = 40;            // another track published "no scale" and a root: chromatic from 40
  clearEv();
  r.pulses(48);                                  // the loop wraps to step 1
  ons(n);
  CHECK_EQ(g_ev[g_nev - 1].kind == EV_NOTE_ON ? 1 : 0, 1);
  CHECK_EQ(n[0], 40);
}
TEST(hr_stepfollow_window_pages_and_the_octave_shifts_the_notes_not_the_degrees) {
  HrRig r(HR_STEPFOLLOW);
  CHECK_EQ(r.s->getParam(HrStepFollow::P_BASE), 0);
  r.tap(7, 2);                                   // nav up: two degrees
  CHECK_EQ(r.s->getParam(HrStepFollow::P_BASE), 2);
  r.tap(7, 3); r.tap(7, 3);
  CHECK_EQ(r.s->getParam(HrStepFollow::P_BASE), -2);
  r.s->setParam(HrStepFollow::P_OCTAVE, 2);
  r.bus.nScale = 0; r.bus.root = 100;            // chromatic: window base -2 + 24 octave-up notes would pass 120: the up key is dim
  CHECK_EQ(r.led(7, 2), 3);
}
TEST(hr_stepfollow_serialize_keeps_the_lit_degrees_and_window) {
  HrRig a(HR_STEPFOLLOW), b(HR_STEPFOLLOW);
  a.tap(0, 3); a.tap(5, 0); a.tap(7, 2); a.tap(2, 1);
  a.s->setParam(HrStepFollow::P_RANGE, 25);
  uint8_t buf[HrScript::kMaxSerial];
  size_t n = a.s->serialize(buf, sizeof buf);
  CHECK_EQ(n, HrStepFollow::kSerialSize);
  CHECK(b.s->deserialize(buf, n));
  CHECK_EQ(b.s->getParam(HrStepFollow::P_BASE), 2); CHECK_EQ(b.s->getParam(HrStepFollow::P_RANGE), 25);
  uint8_t buf2[HrScript::kMaxSerial];
  CHECK_EQ(b.s->serialize(buf2, sizeof buf2), n);
  for (size_t i = 0; i < n; ++i) CHECK_EQ(buf[i], buf2[i]);
  buf[8] = 99;
  CHECK(!b.s->deserialize(buf, n));
}

// ======================= helpers, bus, slots, legends =======================
TEST(hr_euclid_and_velocity_helpers) {
  uint8_t o[16];
  hrEuclid(3, 8, o);  // x..x..x. rotated to start on an onset
  CHECK_EQ(o[0], 1); CHECK_EQ(o[3], 1); CHECK_EQ(o[6], 1); CHECK_EQ(o[1] + o[2] + o[4] + o[5] + o[7], 0);
  hrEuclid(7, 16, o);
  int sum = 0; for (int i = 0; i < 16; ++i) sum += o[i];
  CHECK_EQ(sum, 7); CHECK_EQ(o[0], 1);
  hrEuclid(0, 5, o); CHECK_EQ(o[0] + o[4], 0);
  hrEuclid(9, 5, o); CHECK_EQ(o[0] + o[4], 2);
  CHECK_EQ(hrScaleVel(100, 78), 78); CHECK_EQ(hrScaleVel(1, 45), 1); CHECK_EQ(hrScaleVel(127, 100), 127); CHECK_EQ(hrScaleVel(25, 78), 20);
  CHECK_EQ(hrGrooveVel(1, 5, 90), 90); CHECK_EQ(hrGrooveVel(2, 1, 90), 108); CHECK_EQ(hrGrooveVel(2, 2, 90), 81);
  CHECK_EQ(hrGrooveVel(10, 1, 90), 92);  // swell 1 at phase 0: sin(0) = 0 -> 92
  CHECK_EQ(hrRateStep(6, 1), 4); CHECK_EQ(hrRateStep(6, -1), 8); CHECK_EQ(hrRateStep(1, 1), 1); CHECK_EQ(hrRateStep(24, -1), 24);
}
TEST(hr_slot_creates_every_type_and_leds_are_in_range) {
  HrSlot slot;
  for (int t = 0; t < HR_COUNT; ++t) {
    HrScript* s = slot.create((uint8_t)t);
    CHECK(s != 0); CHECK_EQ(s->hrType(), t);
    uint8_t out[48], roles[48];
    s->leds(out, roles);
    for (int i = 0; i < 48; ++i) CHECK(out[i] <= 15);
  }
  CHECK(slot.create(HR_COUNT) == 0);
}
TEST(hr_legend_words_fit_the_cells_and_describe_every_script) {
  HrSlot slot;
  for (int t = 0; t < HR_COUNT; ++t) {
    HrScript* s = slot.create((uint8_t)t);
    char title[160];
    s->title(title, sizeof title);
    CHECK(strlen(title) > 8 && strlen(title) <= 84);
    CHECK(strlen(s->help()) > 20 && strlen(s->help()) <= 88);
    int words = 0;
    for (int row = 0; row < 4; ++row)
      for (int col = 0; col < 12; ++col) {
        HrCell c;
        s->legend(col, row, c);
        CHECK(strlen(c.txt) <= 7); // plan 028: Camelot root labels ("11A F#m")
        for (const char* p = c.txt; *p; ++p) CHECK(*p >= 32 && *p < 127);
        if (c.txt[0]) { ++words; CHECK(c.zone >= HZ_NOTE && c.zone <= HZ_HOLD); }
        else CHECK_EQ(c.zone, HZ_NONE);
      }
    CHECK(words >= 24);
  }
}

// ======================= Euclid helper (m3: iterative, bit-exact with the recursive original) =======================
namespace {
struct RefBj {  // the original recursive form of pad_script._bjorklund
  uint8_t counts[40], rem[40]; uint8_t* out; int n;
  void build(int lvl) {
    if (lvl == 0) out[n++] = 0;
    else if (lvl == -1) out[n++] = 1;
    else { for (int i = 0; i < counts[lvl]; ++i) build(lvl - 1); if (rem[lvl] != 0) build(lvl - 2); }
  }
};
void refEuclid(int k, int n, uint8_t* out) {
  if (k <= 0) { for (int i = 0; i < n; ++i) out[i] = 0; return; }
  if (k >= n) { for (int i = 0; i < n; ++i) out[i] = 1; return; }
  RefBj b; uint8_t raw[128]; b.out = raw; b.n = 0;
  for (int i = 0; i < 40; ++i) b.counts[i] = b.rem[i] = 0;
  b.rem[1] = (uint8_t)k;
  int divisor = n - k, level = 1;
  while (b.rem[level] > 1) { b.counts[level] = (uint8_t)(divisor / b.rem[level]); b.rem[level + 1] = (uint8_t)(divisor % b.rem[level]); divisor = b.rem[level]; ++level; }
  b.counts[level] = (uint8_t)divisor;
  b.build(level);
  int first = 0;
  for (int i = 0; i < b.n; ++i) if (raw[i]) { first = i; break; }
  for (int i = 0; i < n; ++i) out[i] = raw[(i + first) % b.n];
}
}  // namespace

TEST(hr_euclid_helper_matches_the_recursive_original_for_every_k_and_n) {
  for (int n = 1; n <= 64; ++n)
    for (int k = -1; k <= n + 1; ++k) {
      uint8_t a[128], b[128];
      hrEuclid(k, n, a);
      refEuclid(k, n, b);
      for (int i = 0; i < n; ++i) CHECK_EQ(a[i], b[i]);
    }
}

// ======================= plan 025: Latch (hold mode) and the Arp Seq parameters =======================
TEST(hr_stab_latch_off_sounds_only_while_the_root_is_held) {
  HrRig r(HR_CHORD_STABS);
  CHECK(r.s->hasLatch() && r.s->latch());                 // default: a tap latches, as ever
  r.s->setLatch(false);
  CHECK(!r.s->latch());
  r.s->pad(0, 0, true);
  CHECK(r.s->live() && countOn() == 3);
  clearEv();
  r.s->pad(0, 0, false);                                  // lifted: it ends and the voices stop
  CHECK(!r.s->live() && countOff() == 3 && countOn() == 0);
  r.s->pad(0, 0, true); r.s->pad(2, 0, true);             // two roots: the last one counts
  r.s->pad(0, 0, false);
  CHECK(r.s->live() && r.s->getParam(HrStab::P_ROOT) == 3);
  r.s->pad(2, 0, false);
  CHECK(!r.s->live());
  r.s->setLatch(true);                                    // back to latching
  r.tap(0, 0);
  CHECK(r.s->live());
  r.s->setLatch(false);                                   // Latch off ends a latched sequence
  CHECK(!r.s->live());
}
TEST(hr_arpseq_latch_off_and_parameters) {
  HrRig r(HR_ARP_SEQ);
  CHECK(r.s->hasLatch() && r.s->latch());
  r.tap(0, 0);
  CHECK(r.s->live() && r.s->getParam(HrArpSeq::P_ROOT) == 1);
  r.s->setParam(HrArpSeq::P_SPEED, 1);
  r.s->setParam(HrArpSeq::P_DIR, 4);
  r.s->setParam(HrArpSeq::P_OCT, 3);
  r.s->setParam(HrArpSeq::P_MODE, 2);
  r.s->setParam(HrArpSeq::P_GROOVE, 12);
  r.s->setParam(HrArpSeq::P_QUALITY, 2);
  CHECK_EQ(r.s->getParam(HrArpSeq::P_SPEED), 1); CHECK_EQ(r.s->getParam(HrArpSeq::P_DIR), 4); CHECK_EQ(r.s->getParam(HrArpSeq::P_OCT), 3);
  CHECK_EQ(r.s->getParam(HrArpSeq::P_MODE), 2); CHECK_EQ(r.s->getParam(HrArpSeq::P_GROOVE), 12); CHECK_EQ(r.s->getParam(HrArpSeq::P_QUALITY), 2);
  r.s->setParam(HrArpSeq::P_SPEED, 99);                   // clamped
  CHECK_EQ(r.s->getParam(HrArpSeq::P_SPEED), 4);
  r.s->setLatch(false);                                   // ends the latched root
  CHECK(!r.s->live());
  r.s->pad(1, 0, true);
  CHECK(r.s->live());
  r.s->pad(1, 0, false);
  CHECK(!r.s->live());
  uint8_t b[HrScript::kMaxSerial];                        // the parameters are part of the script's settings
  CHECK(r.s->serialize(b, sizeof b) > 0);
  HrRig q(HR_ARP_SEQ);
  CHECK(q.s->deserialize(b, sizeof b));
  CHECK_EQ(q.s->getParam(HrArpSeq::P_DIR), 4); CHECK_EQ(q.s->getParam(HrArpSeq::P_QUALITY), 2);
}
TEST(hr_chordarp_latch_is_the_script_latch_param) {
  HrRig r(HR_CHORD_ARP);
  CHECK(r.s->hasLatch() && r.s->latch() && r.s->getParam(HrChordArp::P_LATCH) == 2);
  r.s->setLatch(false);
  CHECK(!r.s->latch() && r.s->getParam(HrChordArp::P_LATCH) == 1);
  HrRig b(HR_BASS), e(HR_EUCLID);
  CHECK(!b.s->hasLatch() && !e.s->hasLatch());            // the Bass latch is the engine's; the step sequencers have none
}
