// Second half of the tests (included by test_main.cpp, which holds the runner and the helpers).

TEST(arp_directions_up_down_updown_pattern) {
  int o[64];
  static const int up[6] = {60, 64, 67, 60, 64, 67};
  static const int dn[6] = {67, 64, 60, 67, 64, 60};
  static const int ud[6] = {60, 64, 67, 67, 64, 60};
  static const int pt[8] = {60, 67, 64, 60, 67, 64, 60, 67};
  const int* exp[4] = {up, dn, ud, pt};
  const int len[4] = {6, 6, 6, 8};
  for (int d = 1; d <= 4; ++d) {
    Rig r; armRig(r, d);
    r.pulses(24); clearEv();
    int n = arpNotes(r, o, len[d - 1]);
    CHECK_EQ(n, len[d - 1]);
    for (int i = 0; i < len[d - 1]; ++i) CHECK_EQ(o[i], exp[d - 1][i]);
  }
}
TEST(arp_plus_octave_alternates) {
  int o[64];
  static const int e[6] = {60, 76, 67, 72, 64, 79};
  Rig r; armRig(r, 1, 3, 1);
  r.pulses(24); clearEv();
  CHECK_EQ(arpNotes(r, o, 6), 6);
  for (int i = 0; i < 6; ++i) CHECK_EQ(o[i], e[i]);
}
TEST(arp_note_off_before_next_note_and_velocity) {
  int o[64];
  Rig r; armRig(r, 1);
  r.pulses(24); clearEv();
  arpNotes(r, o, 2);
  CHECK_EQ(g_ev[0].kind, EV_NOTE_ON); CHECK_EQ(g_ev[0].vel, 105);
  CHECK_EQ(g_ev[1].kind, EV_NOTE_OFF); CHECK_EQ(g_ev[1].note, 60);
  CHECK_EQ(g_ev[2].kind, EV_NOTE_ON); CHECK_EQ(g_ev[2].note, 64);
}
TEST(groove_masks_gate_steps) {
  Rig r;
  CHECK(r.ca.gbit(2, 1)); CHECK(!r.ca.gbit(2, 2)); CHECK(r.ca.gbit(2, 3));
  for (int i = 1; i <= 16; ++i) CHECK_EQ(r.ca.gbit(12, i), (i == 1 || i == 9));  // 257
  for (int i = 1; i <= 16; ++i) CHECK(r.ca.gbit(1, i));                         // 65535
  int o[64];
  Rig q; armRig(q, 1); q.ca.setParam(ChordArp::P_SEL_GROOVE, 2);
  q.pulses(24); clearEv();
  int n = arpNotes(q, o, 8);  // steps 5..12: odd steps 5,7,9,11 fire
  CHECK_EQ(n, 4);
  CHECK_EQ(o[0], 60); CHECK_EQ(o[1], 64); CHECK_EQ(o[2], 67); CHECK_EQ(o[3], 60);
}
TEST(chord_retrigger_with_speed_and_gate) {
  Rig r;
  r.ca.setParam(ChordArp::P_SEL_SPEED, 3);
  r.ca.setParam(ChordArp::P_SEL_MOD, 3);
  r.ca.start(); r.rootDown(1, 5); clearEv();
  r.pulses(24);          // queued root applies at the quarter
  clearEv();
  r.pulses(1);           // step at pulse 25
  CHECK_EQ(countOn(), 3);
  CHECK_EQ(g_ev[0].gateMs, 120);  // normal gate = 4 framework ticks
  clearEv();
  r.ticks(3); CHECK_EQ(countOff(), 0);
  r.ticks(1); CHECK_EQ(countOff(), 3);   // killed on the 4th tick
  r.ca.setParam(ChordArp::P_GATE, 1);    // short = 2 ticks
  r.pulses(5); clearEv(); r.pulses(1);
  CHECK_EQ(g_ev[g_nev - 1].gateMs, 60);
  clearEv(); r.ticks(1); CHECK_EQ(countOff(), 0); r.ticks(1); CHECK_EQ(countOff(), 3);
  r.ca.setParam(ChordArp::P_GATE, 2);    // legato: no timer
  r.pulses(5); clearEv(); r.pulses(1); CHECK_EQ(g_ev[g_nev - 1].gateMs, 0);
  clearEv(); r.ticks(10); CHECK_EQ(countOff(), 0);
}
TEST(external_gate_option_arms_no_timer) {
  Rig r; r.ca.setInternalGate(false);
  r.ca.setParam(ChordArp::P_SEL_SPEED, 3); r.ca.setParam(ChordArp::P_SEL_MOD, 3);
  r.ca.start(); r.rootDown(1, 5); r.pulses(25); clearEv();
  r.ticks(20); CHECK_EQ(countOff(), 0);
}
TEST(parametric_groove_pon_and_reroll) {
  Rig r;
  CHECK_EQ(r.ca.vgv(1, 100), 100);                // off: flat
  r.panel(ChordArp::PANEL_PARAM);
  CHECK_EQ(r.ca.getParam(ChordArp::P_PON), 0);    // first tap only opens the panel
  r.press(11, 1);                                 // re-tap PARAM = PON
  CHECK_EQ(r.ca.getParam(ChordArp::P_PON), 1);
  CHECK_EQ(r.ca.vgv(1, 100), 90); CHECK_EQ(r.ca.vgv(2, 100), 76); CHECK_EQ(r.ca.vgv(3, 100), 82);
  CHECK_EQ(r.ca.vgv(17, 100), 90);                // wraps every 16
  static const int on[4] = {1, 5, 11, 13};
  for (int i = 1; i <= 16; ++i) {
    bool e = false; for (int k = 0; k < 4; ++k) if (on[k] == i) e = true;
    CHECK_EQ(r.ca.gbit(1, i), e);
  }
  r.press(6, 3);                                  // archetype 1 active: re-tap rerolls seed 1 -> 36
  CHECK_EQ(r.ca.getParam(ChordArp::P_PSEED), 36);
  r.press(6, 3);                                  // -> (36*7+29)%251 = 30
  CHECK_EQ(r.ca.getParam(ChordArp::P_PSEED), 30);
  r.press(7, 3);                                  // archetype 2 loads weights {3,3,1}
  CHECK_EQ(r.ca.getParam(ChordArp::P_PARCH), 2);
  CHECK_EQ(r.ca.getParam(ChordArp::P_PW0), 3); CHECK_EQ(r.ca.getParam(ChordArp::P_PW1), 3); CHECK_EQ(r.ca.getParam(ChordArp::P_PW2), 1);
  r.press(6, 0);                                  // downbeat weight 0
  CHECK_EQ(r.ca.getParam(ChordArp::P_PW0), 0);
  CHECK_EQ(r.ca.vgv(1, 100), 76);
  Rig q; q.ca.setParam(ChordArp::P_PON, 1);       // same seed + weights -> same mask (deterministic hash)
  q.ca.setParam(ChordArp::P_PARCH, 2);
  q.ca.setParam(ChordArp::P_PSEED, 30);
  q.ca.setParam(ChordArp::P_PW0, 0); q.ca.setParam(ChordArp::P_PW1, 3); q.ca.setParam(ChordArp::P_PW2, 1);
  for (int i = 1; i <= 16; ++i) CHECK_EQ(q.ca.gbit(1, i), r.ca.gbit(1, i));
}
TEST(pon_velocity_used_for_live_notes) {
  Rig r; r.panel(ChordArp::PANEL_PARAM); r.press(11, 1);
  r.ca.setParam(ChordArp::P_SEL_SPEED, 3); r.ca.setParam(ChordArp::P_SEL_MOD, 1);
  r.ca.start(); r.rootDown(1, 5); r.pulses(24); clearEv(); r.pulses(1);
  CHECK_EQ(g_ev[0].vel, r.ca.vgv(5, 105));  // step 5 of the 1/16 pattern
}
TEST(inv_and_oct_pads_both_reset) {
  Rig r;
  r.press(11, 2); r.press(11, 2); CHECK_EQ(r.ca.getParam(ChordArp::P_INV), 2);
  r.press(10, 2); CHECK_EQ(r.ca.getParam(ChordArp::P_INV), 1);
  r.ca.pad(11, 2, true); r.ca.pad(10, 2, true);  // both held
  CHECK_EQ(r.ca.getParam(ChordArp::P_INV), 0);
  r.ca.pad(11, 2, false); r.ca.pad(10, 2, false);
  for (int i = 0; i < 9; ++i) r.press(11, 2);
  CHECK_EQ(r.ca.getParam(ChordArp::P_INV), 6);   // clamps at +6
  for (int i = 0; i < 15; ++i) r.press(10, 2);
  CHECK_EQ(r.ca.getParam(ChordArp::P_INV), -6);
  r.press(11, 3); CHECK_EQ(r.ca.getParam(ChordArp::P_KOCT), 1);
  r.press(11, 3); CHECK_EQ(r.ca.getParam(ChordArp::P_KOCT), 1);
  r.press(10, 3); r.press(10, 3); CHECK_EQ(r.ca.getParam(ChordArp::P_KOCT), -1);
  r.ca.pad(10, 3, true); r.ca.pad(11, 3, true);
  CHECK_EQ(r.ca.getParam(ChordArp::P_KOCT), 0);
}
TEST(grid_play_mode_degree_pads) {
  Rig r; r.ca.setParam(ChordArp::P_PMODE, 3);  // locked C major
  r.ca.pad(5, 0, true);   // x=1,y=6 -> degree 0
  CHECK_EQ(countOn(), 1); CHECK_EQ(g_ev[0].note, 60);
  clearEv();
  r.ca.pad(4, 0, true);   // degree 1 = D
  CHECK_EQ(countOn(), 1); CHECK_EQ(g_ev[0].note, 62);
  clearEv();
  r.ca.pad(5, 0, false);  // chord/speed1: individual note off
  CHECK_EQ(countOff(), 1); CHECK_EQ(g_ev[0].note, 60);
  clearEv();
  r.ca.pad(4, 0, false);
  CHECK(countOff() >= 1);
  clearEv();
  r.ca.pad(0, 0, true); CHECK_EQ(g_ev[0].note, 69);    // degree 5
  r.ca.pad(0, 0, false);
  clearEv();
  r.ca.pad(0, 3, true); CHECK_EQ(g_ev[0].note, 100);   // x=4,y=1 -> degree 23
}
TEST(grid_mode_arp_uses_held_notes) {
  Rig r; r.ca.setParam(ChordArp::P_PMODE, 3);
  r.ca.setParam(ChordArp::P_SEL_TYPE, 2); r.ca.setParam(ChordArp::P_SEL_SPEED, 3);
  r.ca.setParam(ChordArp::P_GATE, 2);
  r.ca.start();
  r.ca.pad(5, 0, true); r.ca.pad(3, 0, true);   // degree 0 (60) and degree 2 (64)
  r.pulses(24); clearEv();
  int o[16]; int n = arpNotes(r, o, 4);
  CHECK_EQ(n, 4); CHECK_EQ(o[0], 60); CHECK_EQ(o[1], 64); CHECK_EQ(o[2], 60); CHECK_EQ(o[3], 64);
}
TEST(lock_button_sets_lock_without_sounding) {
  Rig r;
  r.ca.lockButton(true);
  CHECK_EQ(r.ca.getParam(ChordArp::P_PMODE), 2);
  r.ca.pad(2, 3, true);   // cam[4][3] = 70, minor row
  r.ca.pad(2, 3, false);
  r.ca.lockButton(false);
  CHECK_EQ(g_nev, 0);
  CHECK_EQ(r.ca.getParam(ChordArp::P_LOCK_ROOT), 70); CHECK_EQ(r.ca.getParam(ChordArp::P_LOCK_MINOR), 1);
  int root; bool mn; r.ca.scaleSet(root, mn);
  CHECK_EQ(root, 58);  // 54 + ((70-54) % 12)
  CHECK(mn);
}
TEST(key_mode_root_press_sets_lock) {
  Rig r; r.rootDown(2, 5);  // A minor in key mode is also the lock tonic
  CHECK_EQ(r.ca.getParam(ChordArp::P_LOCK_ROOT), 69); CHECK_EQ(r.ca.getParam(ChordArp::P_LOCK_MINOR), 1);
}
TEST(pmode_change_clears_state_and_kills) {
  Rig r; r.type(3); r.rootDown(1, 5); clearEv();
  r.ca.setParam(ChordArp::P_PMODE, 2);
  CHECK_EQ(countOff(), 3);
}
TEST(stop_kills_live_notes) {
  Rig r; r.type(3); r.rootDown(1, 5); clearEv();
  r.ca.stop(); CHECK_EQ(countOff(), 3);
}
TEST(mute_kills_and_silences) {
  Rig r; r.type(3); r.rootDown(1, 5); clearEv();
  r.ca.setParam(ChordArp::P_MUTE, 1); CHECK_EQ(countOff(), 3);
  clearEv(); r.rootDown(2, 5); CHECK_EQ(countOn(), 0);
}
TEST(scale_follows_held_root) {
  Rig r;
  int root; bool mn;
  r.ca.scaleSet(root, mn); CHECK_EQ(root, 60); CHECK(!mn);       // nothing held: C major
  r.rootDown(2, 5);                                               // A minor 69 -> folded 57
  r.ca.scaleSet(root, mn); CHECK_EQ(root, 57); CHECK(mn);
  CHECK_EQ(r.ca.scaleDeg(0), 57); CHECK_EQ(r.ca.scaleDeg(2), 60); CHECK_EQ(r.ca.scaleDeg(-1), 55);
}
TEST(serialize_roundtrip_and_validation) {
  Rig r;
  r.ca.setParam(ChordArp::P_SEL_TYPE, 3); r.ca.setParam(ChordArp::P_SEL_SPEED, 2); r.ca.setParam(ChordArp::P_SEL_DIR, 4);
  r.ca.setParam(ChordArp::P_SEL_SIZE, 3); r.ca.setParam(ChordArp::P_SEL_GROOVE, 11); r.ca.setParam(ChordArp::P_SEL_MOD, 12);
  r.ca.setParam(ChordArp::P_SWING, 2); r.ca.setParam(ChordArp::P_GATE, 1); r.ca.setParam(ChordArp::P_KOCT, -1);
  r.ca.setParam(ChordArp::P_INV, -3); r.ca.setParam(ChordArp::P_PON, 1); r.ca.setParam(ChordArp::P_PARCH, 3);
  r.ca.setParam(ChordArp::P_PSEED, 77); r.ca.setParam(ChordArp::P_TPOSE, -2); r.ca.setParam(ChordArp::P_PMODE, 2);
  r.ca.setParam(ChordArp::P_LOCK_ROOT, 66);
  uint8_t buf[64];
  CHECK_EQ(r.ca.serialize(buf, 4), 0);
  CHECK_EQ(r.ca.serialize(buf, sizeof buf), ChordArp::kSerialSize);
  CHECK_EQ(buf[0], ChordArp::kSerialVersion);
  ChordArp b; CHECK(b.deserialize(buf, ChordArp::kSerialSize));
  for (int id = 0; id <= ChordArp::P_PANEL; ++id) CHECK_EQ(b.getParam(id), r.ca.getParam(id));
  for (int i = 1; i <= 16; ++i) CHECK_EQ(b.gbit(1, i), r.ca.gbit(1, i));
  buf[0] = 99; CHECK(!b.deserialize(buf, ChordArp::kSerialSize));
  buf[0] = ChordArp::kSerialVersion; buf[7] = 9; CHECK(!b.deserialize(buf, ChordArp::kSerialSize));
  CHECK(!b.deserialize(buf, 5));
}
TEST(leds_levels_in_range_and_roles) {
  Rig r; uint8_t l[kPads], ro[kPads];
  for (int st = 0; st < 40; ++st) {
    r.ca.setParam(ChordArp::P_PANEL, st % 4);
    r.ca.setParam(ChordArp::P_PMODE, 1 + st % 3);
    r.ca.setParam(ChordArp::P_INV, st % 7 - 3);
    r.ca.pad(st % 6, (st / 6) % 4, true);
    r.ticks(st);
    r.ca.leds(l, ro);
    for (int i = 0; i < kPads; ++i) CHECK(l[i] <= 15);
  }
  r.ca.setParam(ChordArp::P_PMODE, 1); r.ca.leds(l, ro);
  CHECK_EQ(ro[0], ROLE_ROOT_MAJOR); CHECK_EQ(ro[12], ROLE_ROOT_MINOR); CHECK_EQ(ro[6], ROLE_PANEL); CHECK_EQ(ro[10], ROLE_CONTROL);
}
TEST(tick_pulse_levels_match_c31_formulas) {
  Rig r;
  int minp = 99, maxp = 0, minn = 99, maxn = 0;
  for (int i = 0; i < 48; ++i) {
    r.ca.tick();
    int p = r.ca.groovePulseLevel(), n = r.ca.notePulseLevel();
    if (p < minp) minp = p;
    if (p > maxp) maxp = p;
    if (n < minn) minn = n;
    if (n > maxn) maxn = n;
  }
  CHECK_EQ(minp, 8); CHECK_EQ(maxp, 15); CHECK_EQ(minn, 5); CHECK_EQ(maxn, 15);
}
TEST(advanceMs_accumulates_30ms_ticks) {
  Rig r; r.ca.setParam(ChordArp::P_SEL_SPEED, 3); r.ca.setParam(ChordArp::P_SEL_MOD, 3);
  r.ca.start(); r.rootDown(1, 5); r.pulses(25); clearEv();
  r.ca.advanceMs(100); CHECK_EQ(countOff(), 0);   // 3 ticks
  r.ca.advanceMs(20);  CHECK_EQ(countOff(), 3);   // 4th tick
}

// ======================= Mono =======================
struct MRig {
  SeqContext ctx; Mono m; ChordArp ca;
  MRig() { ca.setContext(&ctx); m.setContext(&ctx); m.setSink(rec, 0); ca.setSink(rec, 0); clearEv(); }
  void press(int c, int r) { m.pad(c, r, true); m.pad(c, r, false); }
  void pulses(int n) { for (int i = 0; i < n; ++i) m.pulse(); }
  void step(int x, int deg) { press(x - 1, 3 - (deg - m.getParam(Mono::P_WIN))); }  // x 1..8 on the visible half
};
TEST(mono_speed_table) {
  static const int e[12] = {96, 48, 32, 24, 16, 12, 8, 6, 4, 3, 2, 1};
  for (int i = 1; i <= 12; ++i) CHECK_EQ(Mono::speedPulses(i), e[i - 1]);
  MRig r; CHECK_EQ(r.m.getParam(Mono::P_SPEED), 8); CHECK_EQ(r.m.getParam(Mono::P_DIV), 6);
  r.press(10, 1); CHECK_EQ(r.m.getParam(Mono::P_DIV), 4);   // SPD+
  r.press(9, 1); r.press(9, 1); CHECK_EQ(r.m.getParam(Mono::P_DIV), 8);
  for (int i = 0; i < 20; ++i) r.press(9, 1);
  CHECK_EQ(r.m.getParam(Mono::P_SPEED), 1);
}
TEST(mono_tap_toggles_step_and_start_fires) {
  MRig r;
  r.step(1, 0); CHECK_EQ(r.m.stepAt(1), 0);
  r.step(1, 0); CHECK_EQ(r.m.stepAt(1), Mono::kNoStep);
  r.step(1, 0); r.step(3, 2);
  r.m.start();
  CHECK_EQ(countOn(), 1); CHECK_EQ(g_ev[0].note, 60); CHECK_EQ(g_ev[0].vel, 100);
  clearEv(); r.pulses(5); CHECK_EQ(g_nev, 0);
  r.pulses(1);                       // step 2 = rest kills the note
  CHECK_EQ(g_nev, 1); CHECK_EQ(g_ev[0].kind, EV_NOTE_OFF); CHECK_EQ(g_ev[0].note, 60);
  clearEv(); r.pulses(6);            // step 3: degree 2 = E
  CHECK_EQ(g_ev[0].kind, EV_NOTE_ON); CHECK_EQ(g_ev[0].note, 64);
}
TEST(mono_scale_degrees_octaves_and_negative) {
  MRig r;
  r.m.setStep(1, 7); r.m.setStep(2, -1); r.m.setStep(3, 9); r.m.setStep(4, -8);
  r.m.setParam(Mono::P_SPEED, 12);
  r.m.start(); CHECK_EQ(g_ev[0].note, 72);
  r.pulses(1); CHECK_EQ(g_ev[g_nev - 1].note, 59);
  r.pulses(1); CHECK_EQ(g_ev[g_nev - 1].note, 64 + 12);   // degree 9 = E + octave
  r.pulses(1); CHECK_EQ(g_ev[g_nev - 1].note, 47);        // degree -8 = B - octave
}
TEST(mono_new_note_kills_previous) {
  MRig r; r.m.setStep(1, 0); r.m.setStep(2, 1); r.m.setParam(Mono::P_SPEED, 12);
  r.m.start(); clearEv(); r.pulses(1);
  CHECK_EQ(g_nev, 2); CHECK_EQ(g_ev[0].kind, EV_NOTE_OFF); CHECK_EQ(g_ev[0].note, 60);
  CHECK_EQ(g_ev[1].kind, EV_NOTE_ON); CHECK_EQ(g_ev[1].note, 62);
}
TEST(mono_tie_holds_identical_resolved_note) {
  MRig r; r.m.setStep(1, 0); r.m.setStep(2, 0); r.m.setStep(3, 0); r.m.setParam(Mono::P_SPEED, 12);
  r.press(8, 1);   // TIE on
  CHECK_EQ(r.m.getParam(Mono::P_TIE), 1);
  r.m.start(); clearEv(); r.pulses(2);
  CHECK_EQ(g_nev, 0);                // tied: no retrigger
  r.pulses(1);                       // step 4 = rest breaks the tie
  CHECK_EQ(countOff(), 1);
  r.press(8, 1); r.m.start(); clearEv(); r.pulses(1);   // tie off: every step re-attacks
  CHECK_EQ(g_nev, 2);
}
TEST(mono_tie_breaks_on_transpose_change) {
  MRig r; r.m.setStep(1, 0); r.m.setStep(2, 0); r.m.setParam(Mono::P_SPEED, 12); r.m.setParam(Mono::P_TIE, 1);
  r.m.start(); clearEv();
  r.m.setParam(Mono::P_TPOSE, 1);    // resolved note changes -> retrigger
  r.pulses(1);
  CHECK_EQ(countOff(), 1); CHECK_EQ(countOn(), 1); CHECK_EQ(g_ev[g_nev - 1].note, 62);
}
TEST(mono_loop_gesture_and_wrap) {
  MRig r;
  r.m.pad(9, 2, true);                // LOOP held
  r.m.pad(2, 0, true); r.m.pad(4, 0, true);   // steps 3 and 5
  r.m.pad(4, 0, false); r.m.pad(2, 0, false); r.m.pad(9, 2, false);
  CHECK_EQ(r.m.getParam(Mono::P_LSTART), 3); CHECK_EQ(r.m.getParam(Mono::P_LEND), 5);
  CHECK_EQ(r.m.stepAt(3), Mono::kNoStep);   // loop pads do not edit notes
  r.m.pad(4, 0, true); r.m.pad(4, 0, false);  // a normal tap edits again: step 5, row 0 = degree 3
  CHECK_EQ(r.m.stepAt(5), 3);
  r.m.setParam(Mono::P_SPEED, 12);
  r.m.start(); CHECK_EQ(r.m.getParam(Mono::P_POS), 3);
  r.pulses(3); CHECK_EQ(r.m.getParam(Mono::P_POS), 3);   // 4, 5, wrap to 3
  r.m.pad(9, 2, true); r.m.pad(6, 0, true); r.m.pad(1, 0, true);   // reversed order: end first
  r.m.pad(1, 0, false); r.m.pad(6, 0, false); r.m.pad(9, 2, false);
  CHECK_EQ(r.m.getParam(Mono::P_LSTART), 2); CHECK_EQ(r.m.getParam(Mono::P_LEND), 7);
}
TEST(mono_loop_across_halves_uses_press_time_step) {
  MRig r;
  r.m.pad(9, 2, true);
  r.m.pad(1, 0, true);            // step 2 (first half)
  r.press(8, 0);                  // HALF while LOOP held
  r.m.pad(3, 0, true);            // step 12 (second half)
  r.press(8, 0);                  // flip back before releasing
  r.m.pad(3, 0, false); r.m.pad(1, 0, false); r.m.pad(9, 2, false);
  CHECK_EQ(r.m.getParam(Mono::P_LSTART), 2); CHECK_EQ(r.m.getParam(Mono::P_LEND), 12);
}
TEST(mono_pages_chain_np_ep_pg) {
  MRig r;
  r.press(10, 0);                    // PAGE+ extends the chain to 2 and edits page 2
  CHECK_EQ(r.m.getParam(Mono::P_EP), 2); CHECK_EQ(r.m.getParam(Mono::P_NP), 2);
  r.step(1, 0); CHECK_EQ(r.m.stepAt(17), 0);   // absolute index (page-1)*16 + x
  r.press(9, 0);                     // PAGE-
  CHECK_EQ(r.m.getParam(Mono::P_EP), 1);
  r.m.setStep(1, 1);
  r.m.setParam(Mono::P_LEND, 2); r.m.setParam(Mono::P_SPEED, 12);
  r.m.start(); CHECK_EQ(g_ev[0].note, 62);        // page 1 step 1 = degree 1
  clearEv(); r.pulses(1);                          // step 2 rest on page 1
  CHECK_EQ(countOff(), 1); clearEv();
  r.pulses(1);                                     // wraps onto page 2 step 1 (degree 0)
  CHECK_EQ(r.m.getParam(Mono::P_PG), 2); CHECK_EQ(countOn(), 1); CHECK_EQ(g_ev[g_nev - 1].note, 60);
  clearEv(); r.pulses(2);                          // rest, then back to page 1 (kill first)
  CHECK_EQ(r.m.getParam(Mono::P_PG), 1);
  CHECK_EQ(g_ev[g_nev - 1].note, 62);
}
TEST(mono_np_clamps_ep_pg) {
  MRig r; r.m.setParam(Mono::P_NP, 4); r.m.setParam(Mono::P_EP, 4);
  r.m.setParam(Mono::P_NP, 2); CHECK_EQ(r.m.getParam(Mono::P_EP), 2);
  r.m.setParam(Mono::P_NP, 0); CHECK_EQ(r.m.getParam(Mono::P_NP), 1);
}
TEST(mono_half_and_window) {
  MRig r;
  r.press(8, 0); CHECK_EQ(r.m.getParam(Mono::P_HALF), 1);
  r.step(1, 0); CHECK_EQ(r.m.stepAt(9), 0);      // second half = steps 9..16
  r.press(8, 0);
  r.press(8, 2); r.press(8, 2); CHECK_EQ(r.m.getParam(Mono::P_WIN), 2);   // WIN+
  r.step(2, 5);                                  // window +2: top row = degree 5
  CHECK_EQ(r.m.stepAt(2), 5);
  r.press(8, 3); r.press(8, 3); r.press(8, 3); CHECK_EQ(r.m.getParam(Mono::P_WIN), -1);
  r.step(4, -1); CHECK_EQ(r.m.stepAt(4), -1);
  for (int i = 0; i < 100; ++i) r.press(8, 2);
  CHECK_EQ(r.m.getParam(Mono::P_WIN), Mono::kWinLimit);
}
TEST(mono_clear_resets_sequence) {
  MRig r; r.m.setStep(1, 0); r.m.setStep(20, 2);
  r.m.setParam(Mono::P_NP, 3); r.m.setParam(Mono::P_LSTART, 4);
  r.press(11, 0);
  CHECK_EQ(r.m.stepAt(1), Mono::kNoStep); CHECK_EQ(r.m.stepAt(20), Mono::kNoStep);
  CHECK_EQ(r.m.getParam(Mono::P_NP), 1); CHECK_EQ(r.m.getParam(Mono::P_LSTART), 1); CHECK_EQ(r.m.getParam(Mono::P_LEND), 16);
  CHECK_EQ(r.m.getParam(Mono::P_SPEED), 8);      // speed is not part of CLEAR (c31)
}
TEST(mono_follows_chord_scale_and_vgv) {
  MRig r;
  r.m.setStep(1, 0); r.m.setStep(2, 2); r.m.setParam(Mono::P_SPEED, 12);
  r.ca.pad(4, 1, true);                          // root A minor (cam[2][5]) held
  clearEv();
  r.m.start(); CHECK_EQ(g_ev[0].note, 57);
  r.pulses(1); CHECK_EQ(g_ev[g_nev - 1].note, 60);  // degree 2 of A minor (folded tonic 57) = 60
  r.ca.setParam(ChordArp::P_PANEL, ChordArp::PANEL_PARAM);
  r.ca.pad(11, 1, true); r.ca.pad(11, 1, false);   // re-tap PARAM = PON
  clearEv(); r.m.start();
  CHECK_EQ(g_ev[g_nev - 1].vel, 90);               // pvt[1] with default weights
}
TEST(mono_without_chord_is_c_major_flat) {
  Mono m; m.setSink(rec, 0); clearEv();   // no context at all
  m.setStep(1, 0); m.start();
  CHECK_EQ(g_ev[0].note, 60); CHECK_EQ(g_ev[0].vel, 100);
}
TEST(mono_mute_tpose_gtpose) {
  MRig r; r.m.setStep(1, 0); r.m.setStep(2, 0); r.m.setParam(Mono::P_SPEED, 12);
  r.m.setParam(Mono::P_MUTE, 1); r.m.start(); CHECK_EQ(g_nev, 0);
  r.m.setParam(Mono::P_MUTE, 0); r.m.setParam(Mono::P_TPOSE, 2); r.ctx.gtpose = 1;
  r.m.start(); CHECK_EQ(g_ev[0].note, 65);       // degree 0+2+1 = 3 = F
}
TEST(mono_events_use_assigned_channel_tag) {
  MRig r; r.m.setChannel(9); r.m.setStep(1, 0); r.m.start();
  CHECK_EQ(g_ev[0].chan, 9);
  Mono d; CHECK_EQ(d.channel(), 0);               // no channel of its own by default
}
TEST(mono_stop_kills_note) {
  MRig r; r.m.setStep(1, 0); r.m.start(); clearEv(); r.m.stop();
  CHECK_EQ(countOff(), 1); CHECK_EQ(r.m.getParam(Mono::P_PLAYING), -1);
}
TEST(mono_serialize_roundtrip) {
  MRig r; r.m.setStep(1, 3); r.m.setStep(100, -2); r.m.setStep(17, 0);
  r.m.setParam(Mono::P_SPEED, 5); r.m.setParam(Mono::P_TIE, 1); r.m.setParam(Mono::P_WIN, -4);
  r.m.setParam(Mono::P_NP, 5); r.m.setParam(Mono::P_EP, 3); r.m.setParam(Mono::P_LSTART, 2); r.m.setParam(Mono::P_LEND, 9);
  r.m.setParam(Mono::P_TPOSE, 3); r.m.setParam(Mono::P_MUTE, 1);
  uint8_t buf[Mono::kSerialSize];
  CHECK_EQ(r.m.serialize(buf, 10), 0);
  CHECK_EQ(r.m.serialize(buf, sizeof buf), Mono::kSerialSize);
  CHECK_EQ(buf[0], Mono::kSerialVersion);
  Mono b; CHECK(b.deserialize(buf, sizeof buf));
  for (int id = 0; id <= Mono::P_MUTE; ++id) if (id != Mono::P_PG && id != Mono::P_POS) CHECK_EQ(b.getParam(id), r.m.getParam(id));
  for (int i = 1; i <= 128; ++i) CHECK_EQ(b.stepAt(i), r.m.stepAt(i));
  buf[0] = 7; CHECK(!b.deserialize(buf, sizeof buf));
  buf[0] = 1; buf[1] = 0; CHECK(!b.deserialize(buf, sizeof buf));
  CHECK(!b.deserialize(buf, 20));
}
TEST(mono_leds_playhead_and_notes) {
  MRig r; r.m.setStep(1, 0); r.m.setParam(Mono::P_SPEED, 12); r.m.start();
  uint8_t l[kPads], ro[kPads];
  r.m.leds(l, ro);
  CHECK_EQ(l[3 * 12 + 0], 15);           // playhead on a note
  CHECK_EQ(ro[3 * 12 + 0], ROLE_PLAYHEAD);
  CHECK_EQ(l[3 * 12 + 1], 2);            // degree 0 row = faint root bar
  for (int i = 0; i < kPads; ++i) CHECK(l[i] <= 15);
}

TEST(mute_keeps_pending_gate_timer_like_c31) {
  // c31 mute calls k_c/k_l only; a gate timer armed by the last arp step still fires and cuts a later note early.
  Rig r; armRig(r, 1); r.pulses(24);
  r.ca.setParam(ChordArp::P_GATE, 1);         // short: 2 ticks
  r.pulses(1);                                // arp note at step 5 arms the timer
  r.ca.setParam(ChordArp::P_MUTE, 1);
  r.ca.setParam(ChordArp::P_PMODE, 3);        // grid mode, then a held grid note (not subject to mute)
  r.ca.setParam(ChordArp::P_SEL_TYPE, 1); r.ca.setParam(ChordArp::P_SEL_SPEED, 1);
  clearEv(); r.ca.pad(5, 0, true);            // sounds 60
  CHECK_EQ(countOn(), 1); clearEv();
  r.ticks(1); CHECK_EQ(countOff(), 0);
  r.ticks(1); CHECK_EQ(countOff(), 1);        // the stale timer ended it after 2 ticks
}

int main() {
  for (int i = 0; i < g_nreg; ++i) {
    g_name = g_reg[i].name; g_failBefore = g_fail;
    g_reg[i].fn();
    g_tests++;
    if (g_fail != g_failBefore) { g_testFail++; printf("FAIL  %s\n", g_name); }
  }
  printf("%d tests, %d checks, %d failed tests, %d failed checks\n", g_tests, g_checks, g_testFail, g_fail);
  return g_fail ? 1 : 0;
}
