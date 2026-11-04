// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// c34 additions (included by test_cases2.inc after MRig is defined).

TEST(mono_velocity_pattern_per_instance) {
  MRig r; r.m.setStep(1, 0); r.m.setStep(2, 0); r.m.setParam(Mono::P_SPEED, 12);
  r.m.setParam(Mono::P_VG, 2); r.m.start();
  CHECK_EQ(g_ev[0].vel, 100);                      // step 1 of pattern 2
  r.pulses(1); CHECK_EQ(g_ev[g_nev - 1].vel, 69);  // step 2
  Mono b; b.setContext(&r.ctx); b.setSink(rec, 0); b.setStep(1, 0); clearEv(); b.start();
  CHECK_EQ(g_ev[0].vel, 100);                      // a second instance has its own (flat) pattern
  r.press(11, 2); r.press(11, 2); CHECK_EQ(r.m.getParam(Mono::P_VG), 4);   // VEL+
  r.press(10, 2); CHECK_EQ(r.m.getParam(Mono::P_VG), 3);                   // VEL-
  for (int i = 0; i < 20; ++i) r.press(11, 2);
  CHECK_EQ(r.m.getParam(Mono::P_VG), 12);                                  // walks on through the blends 9..12 (menu row 2), then stops
  r.press(10, 2); CHECK_EQ(r.m.getParam(Mono::P_VG), 11);                  // and back down: not stuck in a blend
  for (int i = 0; i < 20; ++i) r.press(10, 2);
  CHECK_EQ(r.m.getParam(Mono::P_VG), 1);
}
TEST(mono_pattern_8_follows_shared_bar_counter) {
  MRig r; r.m.setStep(1, 0); r.m.setParam(Mono::P_VG, 8);
  r.ctx.gb = 0; clearEv(); r.m.start(); CHECK_EQ(g_ev[g_nev - 1].vel, 100);
  r.ctx.gb = 3; clearEv(); r.m.start(); CHECK_EQ(g_ev[g_nev - 1].vel, 84);
}
TEST(mono_leds_brightness_follows_velocity_45_to_100) {
  MRig r; r.m.setStep(2, 0); r.m.setParam(Mono::P_VG, 2);
  uint8_t l[kPads]; r.m.leds(l);
  CHECK_EQ(l[3 * 12 + 1], 5 + (69 - 45) / 6);      // pattern 2, step 2 -> velocity 69
  r.m.setParam(Mono::P_VG, 1); r.m.leds(l);
  CHECK_EQ(l[3 * 12 + 1], 5 + (100 - 45) / 6);
}
TEST(pool_transpose_in_chord_key_c34) {
  Rig r; int16_t p[8];
  // c34 applies T while building, in the chord's own key: A minor triad {0,2,4}+1 degree = B, D, F
  r.ca.setParam(ChordArp::P_TPOSE, 1);
  int n = r.ca.pool(69, 3, true, p);
  CHECK_EQ(n, 3); CHECK_EQ(p[0], 71); CHECK_EQ(p[1], 74); CHECK_EQ(p[2], 77);
  r.ca.setParam(ChordArp::P_TPOSE, 7);               // a full octave
  n = r.ca.pool(69, 3, true, p); CHECK_EQ(p[0], 81);
  r.ca.setParam(ChordArp::P_KOCT, 1); r.ca.setParam(ChordArp::P_TPOSE, 24);   // way up: notes above 127 are dropped
  n = r.ca.pool(71, 8, false, p);
  CHECK(n < 5);
  for (int i = 0; i < n; ++i) CHECK(p[i] >= 0 && p[i] <= 127);
}
TEST(pool_extended_types_follow_transpose) {
  Rig r; int16_t p[8];
  r.ca.setParam(ChordArp::P_TPOSE, 2);               // root 60 -> degree 2 = E, then the dom7 shape
  int n = r.ca.pool(60, 9, false, p);
  CHECK_EQ(n, 4); CHECK_EQ(p[0], 64); CHECK_EQ(p[1], 68); CHECK_EQ(p[2], 71); CHECK_EQ(p[3], 74);
}

static void armDir(Rig& r, int dirv, int orng = 1) {
  r.ca.setParam(ChordArp::P_SEL_TYPE, 2); r.ca.setParam(ChordArp::P_SEL_SPEED, 3);
  r.ca.setParam(ChordArp::P_SEL_DIR, dirv); r.ca.setParam(ChordArp::P_SEL_MOD, 3);
  r.ca.setParam(ChordArp::P_GATE, 2); r.ca.setParam(ChordArp::P_ORANGE, orng);
  r.ca.start(); r.rootDown(1, 5); r.pulses(24); clearEv();
}
TEST(arp_new_directions_5_to_10) {
  static const int e[6][8] = {{60, 64, 67, 64, 60, 64, 67, 64},   // 5 up-down, ends not repeated
                              {67, 64, 60, 64, 67, 64, 60, 64},   // 6 down-up
                              {67, 67, 64, 67, 67, 60, 0, 0},     // 7 random: xorshift32 values 3,3,2,3,3,1 (the first 4 were used by steps before the quarter)
                              {60, 67, 64, 60, 67, 64, 60, 67},   // 8 converge
                              {64, 67, 60, 64, 67, 60, 64, 67},   // 9 inside out
                              {60, 64, 60, 67, 60, 64, 60, 67}};  // 10 root return
  for (int d = 5; d <= 10; ++d) {
    Rig r; armDir(r, d);
    int o[16]; int n = arpNotes(r, o, d == 7 ? 6 : 8);
    CHECK_EQ(n, d == 7 ? 6 : 8);
    for (int i = 0; i < n; ++i) CHECK_EQ(o[i], e[d - 5][i]);
  }
}
TEST(arp_octave_range_expands_pool) {
  Rig r; armDir(r, 1, 2);
  int o[16]; int n = arpNotes(r, o, 8);
  static const int e[8] = {60, 64, 67, 72, 76, 79, 60, 64};
  CHECK_EQ(n, 8);
  for (int i = 0; i < n; ++i) CHECK_EQ(o[i], e[i]);
  Rig q; armDir(q, 2, 4);                  // down over 4 octaves: starts on the highest note
  n = arpNotes(q, o, 3);
  CHECK_EQ(o[0], 67 + 36); CHECK_EQ(o[1], 64 + 36); CHECK_EQ(o[2], 60 + 36);
}
TEST(arp_octave_range_drops_notes_above_127) {
  Rig r; armDir(r, 1, 4);
  r.ca.setParam(ChordArp::P_KOCT, 1);
  int o[64]; int n = arpNotes(r, o, 40);
  for (int i = 0; i < n; ++i) CHECK(o[i] <= 127);
}
TEST(speed_and_direction_pad_pairs) {
  Rig r; r.page(ChordArp::PAGE_PLAY);
  static const int a[6] = {1, 1, 1, 2, 2, 3}, b[6] = {2, 3, 4, 3, 4, 4};
  for (int k = 0; k < 6; ++k) {
    for (int order = 0; order < 2; ++order) {
      int x = order ? b[k] : a[k], y = order ? a[k] : b[k];
      r.ca.pad(7 + x, 1, true); r.ca.pad(7 + y, 1, true);
      CHECK_EQ(r.ca.getParam(ChordArp::P_Q_SPEED), 5 + k);
      r.ca.pad(7 + y, 1, false); r.ca.pad(7 + x, 1, false);
      r.ca.pad(7 + x, 2, true); r.ca.pad(7 + y, 2, true);
      CHECK_EQ(r.ca.getParam(ChordArp::P_Q_DIR), 5 + k);
      r.ca.pad(7 + y, 2, false); r.ca.pad(7 + x, 2, false);
    }
  }
  r.press(8, 1); CHECK_EQ(r.ca.getParam(ChordArp::P_Q_SPEED), 1);   // fingers released: a single tap is single again
}
TEST(extra_speeds_gstep_positions) {
  Rig r;
  struct Case { int sp, meas, step; };
  static const Case c[] = {{5, 1, 1}, {5, 25, 2}, {5, 73, 4}, {5, 13, 0}, {6, 17, 2}, {6, 81, 6}, {7, 4, 2}, {7, 94, 16},
                           {8, 3, 2}, {9, 49, 2}, {9, 2, 0}, {10, 33, 2}, {10, 65, 3}};
  for (unsigned i = 0; i < sizeof c / sizeof c[0]; ++i) {
    r.ca.setBarPos((uint8_t)c[i].meas);
    CHECK_EQ(r.ca.gstep(c[i].sp), c[i].step);
  }
}
TEST(swing_0_to_3_capped_below_step_length) {
  Rig r; r.ca.setParam(ChordArp::P_SEL_SPEED, 7);   // 1/32: div 3, so swing 3 is capped to 2
  r.ca.setParam(ChordArp::P_SWING, 3);
  r.ca.setBarPos(4);  CHECK_EQ(r.ca.gstep(7), 0);   // odd step 2 would be at 4, moved to 6
  r.ca.setBarPos(6);  CHECK_EQ(r.ca.gstep(7), 2);
  r.ca.setBarPos(7 + 2); CHECK_EQ(r.ca.gstep(3), 0);   // 1/16 (div 6) swing 3: odd step at +3
  r.ca.setBarPos(7 + 3); CHECK_EQ(r.ca.gstep(3), 2);
  CHECK_EQ(r.ca.getParam(ChordArp::P_SWING), 3);
}
TEST(gate_lanes_force_long_gate_on_masked_steps) {
  Rig r;
  r.ca.setParam(ChordArp::P_SEL_TYPE, 2); r.ca.setParam(ChordArp::P_SEL_SPEED, 3); r.ca.setParam(ChordArp::P_SEL_MOD, 1);
  r.ca.setParam(ChordArp::P_GATE, 2);                // legato: steps outside the lane have no timer
  r.ca.setParam(ChordArp::P_LANE, 1);                // lane 1 = steps 3, 7, 11, 15
  r.ca.start(); r.rootDown(1, 5); r.pulses(24); clearEv();
  int gate[16] = {0}; int seen[16] = {0};
  for (int pul = 25; pul <= 24 + 96; ++pul) {
    int before = g_nev; r.pulses(1);
    for (int j = before; j < g_nev; ++j) if (g_ev[j].kind == EV_NOTE_ON) {
      int step = ((pul - 1) % 96) / 6 + 1; gate[step - 1] = g_ev[j].gateMs; seen[step - 1] = 1;
    }
  }
  int n = 0;
  for (int i = 0; i < 16; ++i) if (seen[i]) { n++; CHECK_EQ(gate[i], (i == 2 || i == 6 || i == 10 || i == 14) ? 210 : 0); }
  CHECK(n >= 8);
  // the gate timer follows: a masked step ends after 7 ticks
  r.ca.setParam(ChordArp::P_LANE, 0);
}
TEST(gate_lane_pair_unions_masks) {
  Rig r;
  r.ca.setParam(ChordArp::P_SEL_TYPE, 2); r.ca.setParam(ChordArp::P_SEL_SPEED, 3); r.ca.setParam(ChordArp::P_SEL_MOD, 1);
  r.ca.setParam(ChordArp::P_GATE, 2);
  r.ca.setParam(ChordArp::P_LANE, 1 + 8 * 2);        // lanes 1 and 2: 0x4444 | 0x8888 -> steps 3,4,7,8,11,12,15,16
  r.ca.start(); r.rootDown(1, 5); r.pulses(24); clearEv();
  for (int pul = 25; pul <= 24 + 96; ++pul) {
    int before = g_nev; r.pulses(1);
    for (int j = before; j < g_nev; ++j) if (g_ev[j].kind == EV_NOTE_ON) {
      int step = ((pul - 1) % 96) / 6 + 1;
      CHECK_EQ(g_ev[j].gateMs, ((step - 1) % 4 >= 2) ? 210 : 0);
    }
  }
}
TEST(c34_params_clamp_and_serialize) {
  Rig r;
  r.ca.setParam(ChordArp::P_SEL_VG, 99); CHECK_EQ(r.ca.getParam(ChordArp::P_SEL_VG), 72);
  r.ca.setParam(ChordArp::P_LANE, 99); CHECK_EQ(r.ca.getParam(ChordArp::P_LANE), 36);
  r.ca.setParam(ChordArp::P_ORANGE, 9); CHECK_EQ(r.ca.getParam(ChordArp::P_ORANGE), 4);
  r.ca.setParam(ChordArp::P_SWING, 7); CHECK_EQ(r.ca.getParam(ChordArp::P_SWING), 3);
  r.ca.setParam(ChordArp::P_SEL_SPEED, 10); r.ca.setParam(ChordArp::P_SEL_DIR, 10);
  r.ca.setParam(ChordArp::P_SEL_VG, 19); r.ca.setParam(ChordArp::P_LANE, 12); r.ca.setParam(ChordArp::P_ORANGE, 3);
  uint8_t buf[64]; r.ca.serialize(buf, sizeof buf);
  ChordArp b; CHECK(b.deserialize(buf, sizeof buf));
  for (int id = 0; id < ChordArp::P_COUNT; ++id) CHECK_EQ(b.getParam(id), r.ca.getParam(id));
  buf[25] = 0; CHECK(!b.deserialize(buf, sizeof buf));
}
TEST(arp_with_every_note_out_of_range_is_silent_not_a_crash) {
  // c34 raises 'n%0' in c_arp here (octave range 2, all notes pushed above 127 by octave + transpose + inversion)
  Rig r; armDir(r, 1, 2);
  r.ca.setParam(ChordArp::P_SEL_MOD, 1); r.ca.setParam(ChordArp::P_KOCT, 1);
  r.ca.setParam(ChordArp::P_TPOSE, 24); r.ca.setParam(ChordArp::P_INV, 1);
  r.rootUp(1, 5); r.rootDown(4, 1);
  clearEv(); r.pulses(96);
  CHECK_EQ(countOn(), 0);
}
