// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Plan 008 C: the 12x4 pad map of Chord/Arp (pages Play / Groove / Feel / Setup) and Mono (note view / menu view), pad by pad.
// Included by test_main.cpp after test_cases2.inc (uses Rig, MRig, the event recorder and CHECK macros).

// ======================= Chord/Arp =======================
TEST(pads_roots_play_the_same_on_every_page) {
  for (int p = 0; p < 4; ++p) {
    Rig r; r.page(p);
    r.ca.setParam(ChordArp::P_SEL_MOD, 1);   // root only
    r.ca.pad(5, 0, true);                    // x=1, y=6 -> cam[1][6] = 65 (F)
    CHECK_EQ(countOn(), 1); CHECK_EQ(g_ev[0].note, 65);
    r.ca.pad(5, 0, false); CHECK_EQ(countOff(), 1);
    clearEv();
    r.ca.pad(0, 1, true);                    // x=2, y=1 -> cam[2][1] = 61 (minor row)
    CHECK_EQ(g_ev[0].note, 61);
    r.ca.pad(0, 1, false);
  }
}
TEST(pads_chord_type_pads_1_to_8_and_hold_root_for_9_to_15) {
  Rig r;
  // cols 6-7, rows 0-3: pad m = (col-6)*4 + row + 1
  for (int m = 1; m <= 8; ++m) { r.press(6 + (m - 1) / 4, (m - 1) % 4); CHECK_EQ(r.ca.getParam(ChordArp::P_SEL_MOD), m); }
  // hold pad 1 (Root) and tap pad m (2..8) = type 8 + (m - 1)
  for (int m = 2; m <= 8; ++m) {
    r.ca.pad(6, 0, true);
    CHECK_EQ(r.ca.getParam(ChordArp::P_SEL_MOD), 1);                          // pressing Root selects type 1 at once (c34)
    r.press(6 + (m - 1) / 4, (m - 1) % 4);
    CHECK_EQ(r.ca.getParam(ChordArp::P_SEL_MOD), 8 + (m - 1));
    r.ca.pad(6, 0, false);
  }
  r.press(7, 0);                                                              // after the release the pads are plain types again
  CHECK_EQ(r.ca.getParam(ChordArp::P_SEL_MOD), 5);
  CHECK_EQ(strcmp(ChordArp::chordTypeName(3), "Triad"), 0); CHECK_EQ(strcmp(ChordArp::chordTypeName(15), "b9"), 0);
}
TEST(pads_chord_type_changes_the_sounding_chord_at_once) {
  Rig r; r.type(3); r.rootDown(1, 5); clearEv();      // F... x=1,y=5 -> 60 (C), triad
  r.press(6, 0);                                      // Root only: kill + re-sound a single note
  CHECK_EQ(countOff(), 3); CHECK_EQ(countOn(), 1); CHECK_EQ(g_ev[g_nev - 1].note, 60);
}
TEST(pads_play_page_type_speed_direction_rows) {
  Rig r;
  for (int v = 1; v <= 3; ++v) { r.press(7 + v, 0); CHECK_EQ(r.ca.getParam(ChordArp::P_Q_TYPE), v); }
  r.press(11, 0); r.press(11, 0);                      // (11,0) is the page pad, not a 4th type: out and back, the queue is unchanged
  CHECK_EQ(r.ca.getParam(ChordArp::P_Q_TYPE), 3);
  for (int v = 1; v <= 4; ++v) { r.press(7 + v, 1); CHECK_EQ(r.ca.getParam(ChordArp::P_Q_SPEED), v); }
  for (int v = 1; v <= 4; ++v) { r.press(7 + v, 2); CHECK_EQ(r.ca.getParam(ChordArp::P_Q_DIR), v); }
}
TEST(pads_inversion_and_octave_row) {
  Rig r;
  r.press(9, 3); r.press(9, 3); CHECK_EQ(r.ca.getParam(ChordArp::P_INV), 2);     // Inv+
  r.press(8, 3); CHECK_EQ(r.ca.getParam(ChordArp::P_INV), 1);                    // Inv-
  r.press(11, 3); CHECK_EQ(r.ca.getParam(ChordArp::P_KOCT), 1);                  // Oct+
  r.press(10, 3); r.press(10, 3); CHECK_EQ(r.ca.getParam(ChordArp::P_KOCT), -1); // Oct-
}
TEST(pads_page_pad_and_tabs) {
  Rig r;
  CHECK_EQ(r.ca.getParam(ChordArp::P_PAGE), ChordArp::PAGE_PLAY);
  r.press(11, 0); CHECK_EQ(r.ca.getParam(ChordArp::P_PAGE), ChordArp::PAGE_GROOVE);   // the page pad opens the first edit page
  r.press(11, 2); CHECK_EQ(r.ca.getParam(ChordArp::P_PAGE), ChordArp::PAGE_FEEL);
  r.press(11, 3); CHECK_EQ(r.ca.getParam(ChordArp::P_PAGE), ChordArp::PAGE_SETUP);
  r.press(11, 1); CHECK_EQ(r.ca.getParam(ChordArp::P_PAGE), ChordArp::PAGE_GROOVE);
  r.press(11, 0); CHECK_EQ(r.ca.getParam(ChordArp::P_PAGE), ChordArp::PAGE_PLAY);       // tab Play
  r.press(11, 0); CHECK_EQ(r.ca.getParam(ChordArp::P_PAGE), ChordArp::PAGE_GROOVE);     // ... and the page pad returns to the last edit page
  r.press(11, 3); r.press(11, 0); r.press(11, 0);
  CHECK_EQ(r.ca.getParam(ChordArp::P_PAGE), ChordArp::PAGE_SETUP);
  r.press(11, 3); CHECK_EQ(r.ca.getParam(ChordArp::P_PAGE), ChordArp::PAGE_SETUP);      // own tab: stays
}
TEST(pads_release_belongs_to_the_page_of_the_press) {
  Rig r;
  r.ca.pad(8, 1, true);                         // Play: first finger on speed 1 (the pair gesture's anchor)
  r.press(11, 0);                               // page changes while the finger is down (Groove)
  r.ca.pad(8, 1, false);                        // its release must not act on the Groove page (groove pad 5)
  CHECK_EQ(r.ca.getParam(ChordArp::P_Q_GROOVE), 0);
  r.press(11, 0);                               // back on Play: a fresh single tap is single (no leftover finger)
  r.press(9, 1); CHECK_EQ(r.ca.getParam(ChordArp::P_Q_SPEED), 2);
  r.ca.pad(6, 0, true);                         // Root type pad held while the page changes: the modifier ends
  r.press(11, 0); r.press(11, 0);
  r.ca.pad(6, 0, false);
  r.press(7, 0); CHECK_EQ(r.ca.getParam(ChordArp::P_SEL_MOD), 5);
}
TEST(pads_groove_page_block_param_gate_swing) {
  Rig r; r.page(ChordArp::PAGE_GROOVE);
  for (int g = 1; g <= 16; ++g) { r.press(6 + (g - 1) % 4, (g - 1) / 4); CHECK_EQ(r.ca.getParam(ChordArp::P_Q_GROOVE), g); }
  r.press(10, 0); CHECK_EQ(r.ca.getParam(ChordArp::P_PON), 1);
  r.press(7, 0); CHECK_EQ(r.ca.getParam(ChordArp::P_PW0), 1);                       // tier row 0
  r.press(9, 2); CHECK_EQ(r.ca.getParam(ChordArp::P_PW2), 3);
  r.press(8, 3); CHECK_EQ(r.ca.getParam(ChordArp::P_PARCH), 3);                     // archetype row
  r.press(10, 0); CHECK_EQ(r.ca.getParam(ChordArp::P_PON), 0);
  CHECK_EQ(r.ca.getParam(ChordArp::P_GATE), 0);
  r.press(10, 1); CHECK_EQ(r.ca.getParam(ChordArp::P_GATE), 1);
  r.press(10, 1); r.press(10, 1); CHECK_EQ(r.ca.getParam(ChordArp::P_GATE), 0);     // normal -> short -> legato -> normal
  r.press(10, 3); r.press(10, 3); r.press(10, 3); r.press(10, 3); CHECK_EQ(r.ca.getParam(ChordArp::P_SWING), 3);  // clamps
  r.press(10, 2); CHECK_EQ(r.ca.getParam(ChordArp::P_SWING), 2);
  for (int i = 0; i < 5; ++i) r.press(10, 2);
  CHECK_EQ(r.ca.getParam(ChordArp::P_SWING), 0);
}
TEST(pads_feel_page_velocity_lane_range_size) {
  Rig r; r.page(ChordArp::PAGE_FEEL);
  for (int k = 1; k <= 8; ++k) { r.press(6 + (k - 1) % 4, (k - 1) / 4); CHECK_EQ(r.ca.getParam(ChordArp::P_SEL_VG), k); }
  r.ca.pad(6, 0, true); r.ca.pad(8, 1, true);                     // patterns 1 and 7 together: blend 1 + 8*7
  CHECK_EQ(r.ca.getParam(ChordArp::P_SEL_VG), 1 + 8 * 7);
  r.ca.pad(8, 1, false); r.ca.pad(6, 0, false);
  r.press(9, 1); CHECK_EQ(r.ca.getParam(ChordArp::P_SEL_VG), 8);                   // single again
  r.press(7, 2); CHECK_EQ(r.ca.getParam(ChordArp::P_LANE), 2);
  r.press(7, 2); CHECK_EQ(r.ca.getParam(ChordArp::P_LANE), 0);                     // tap again = off
  r.ca.pad(6, 2, true); r.ca.pad(9, 2, true);                     // two lanes: 1 + 8*4
  CHECK_EQ(r.ca.getParam(ChordArp::P_LANE), 1 + 8 * 4);
  r.ca.pad(9, 2, false); r.ca.pad(6, 2, false);
  for (int o = 1; o <= 4; ++o) { r.press(6 + o - 1, 3); CHECK_EQ(r.ca.getParam(ChordArp::P_ORANGE), o); }
  static const int sz[4] = {5, 4, 3, 2};
  for (int i = 0; i < 4; ++i) { r.press(10, i); CHECK_EQ(r.ca.getParam(ChordArp::P_Q_SIZE), sz[i]); }
}
TEST(pads_setup_page_mode_lock_transpose_mute_off) {
  Rig r; r.page(ChordArp::PAGE_SETUP);
  r.press(8, 0); CHECK_EQ(r.ca.getParam(ChordArp::P_PMODE), 3);
  r.press(6, 0); CHECK_EQ(r.ca.getParam(ChordArp::P_PMODE), 1);
  r.ca.pad(7, 0, true);                                           // hold Locked ...
  CHECK_EQ(r.ca.getParam(ChordArp::P_PMODE), 2);
  r.ca.pad(1, 3, true); r.ca.pad(1, 3, false);                    // ... and tap root x=4,y=2 (cam[4][2] = 63, minor row): sets the lock
  CHECK_EQ(r.ca.getParam(ChordArp::P_LOCK_ROOT), 63); CHECK_EQ(r.ca.getParam(ChordArp::P_LOCK_MINOR), 1);
  r.ca.pad(7, 0, false);
  clearEv();
  r.ca.pad(0, 0, true); r.ca.pad(0, 0, false);                    // after the release a root is a normal chord again
  CHECK_EQ(r.ca.getParam(ChordArp::P_LOCK_ROOT), 63);
  r.press(7, 1); r.press(7, 1); CHECK_EQ(r.ca.getParam(ChordArp::P_TPOSE), 2);
  r.press(6, 1); CHECK_EQ(r.ca.getParam(ChordArp::P_TPOSE), 1);
  r.press(8, 1); CHECK_EQ(r.ca.getParam(ChordArp::P_TPOSE), 0);
  r.ca.setParam(ChordArp::P_SEL_MOD, 3); r.ca.setParam(ChordArp::P_PMODE, 1);
  r.rootDown(1, 5); clearEv();
  r.press(6, 2); CHECK_EQ(r.ca.getParam(ChordArp::P_MUTE), 1); CHECK_EQ(countOff(), 3);   // Mute kills the sounding chord
  r.press(6, 2); CHECK_EQ(r.ca.getParam(ChordArp::P_MUTE), 0);
  r.press(7, 2); CHECK_EQ(r.ca.getParam(ChordArp::P_Q_TYPE), 4);                          // Off (type 4), from the next bar
}
TEST(pads_leds_show_the_active_page_and_selection) {
  Rig r; uint8_t l[kPads], ro[kPads];
  r.ca.leds(l, ro);
  CHECK_EQ(l[0 * 12 + 11], 6); CHECK_EQ(ro[11], ROLE_CONTROL);                    // Play: the page pad
  CHECK_EQ(l[0 * 12 + 6], 15);                                                    // type 1 selected
  r.type(5); r.ca.leds(l, ro); CHECK_EQ(l[0 * 12 + 7], 15); CHECK_EQ(l[0 * 12 + 6], 3);
  for (int p = 1; p <= 3; ++p) {
    r.page(p); r.ca.leds(l, ro);
    for (int row = 0; row < 4; ++row) CHECK_EQ(l[row * 12 + 11], row == p ? 15 : 5);   // tab column: the active page is bright
  }
  r.page(ChordArp::PAGE_SETUP); r.ca.leds(l, ro);
  CHECK_EQ(l[0 * 12 + 6], 15);                                                    // Key mode is on
  r.page(ChordArp::PAGE_PLAY);
  r.press(9, 3); r.ca.leds(l, ro); CHECK(l[3 * 12 + 9] > 3);                      // Inv+ lights up with the inversion
}

// ======================= Mono =======================
TEST(pads_mono_note_view_controls) {
  MRig r;
  r.press(8, 0); r.press(8, 0); CHECK_EQ(r.m.getParam(Mono::P_WIN), 2);           // window up
  r.press(8, 1); CHECK_EQ(r.m.getParam(Mono::P_WIN), 1);                          // window down
  r.press(8, 2); CHECK_EQ(r.m.getParam(Mono::P_HALF), 1);                         // steps 9-16
  r.press(8, 2); CHECK_EQ(r.m.getParam(Mono::P_HALF), 0);
  r.press(9, 2); CHECK_EQ(r.m.getParam(Mono::P_TIE), 1);                          // tie
  r.press(10, 0); CHECK_EQ(r.m.getParam(Mono::P_SPEED), 7);                       // slower
  r.press(10, 1); r.press(10, 1); CHECK_EQ(r.m.getParam(Mono::P_SPEED), 9);       // faster
  r.press(11, 2); CHECK_EQ(r.m.getParam(Mono::P_VG), 2);                          // velocity +
  r.press(10, 2); CHECK_EQ(r.m.getParam(Mono::P_VG), 1);                          // velocity -
  r.press(9, 1); CHECK_EQ(r.m.getParam(Mono::P_EP), 2); CHECK_EQ(r.m.getParam(Mono::P_NP), 2);   // page +
  r.press(9, 0); CHECK_EQ(r.m.getParam(Mono::P_EP), 1);                           // page -
  CHECK_EQ(r.m.getParam(Mono::P_MENU), 0);
}
TEST(pads_mono_menu_view_direct_selectors) {
  MRig r;
  r.press(11, 3); CHECK_EQ(r.m.getParam(Mono::P_MENU), 1);
  for (int x = 1; x <= 12; ++x) { r.press(x - 1, 0); CHECK_EQ(r.m.getParam(Mono::P_SPEED), x); }   // speed row
  CHECK_EQ(r.m.getParam(Mono::P_DIV), 1);
  for (int x = 1; x <= 12; ++x) { r.press(x - 1, 2); CHECK_EQ(r.m.getParam(Mono::P_VG), x); }      // velocity row
  r.press(8, 1); CHECK_EQ(r.m.getParam(Mono::P_TIE), 1); r.press(8, 1); CHECK_EQ(r.m.getParam(Mono::P_TIE), 0);
  r.press(10, 1); r.press(10, 1); CHECK_EQ(r.m.getParam(Mono::P_WIN), 2);
  r.press(11, 1); CHECK_EQ(r.m.getParam(Mono::P_WIN), 1);
  r.press(0, 3); r.press(0, 3); CHECK_EQ(r.m.getParam(Mono::P_TPOSE), -2);
  r.press(1, 3); CHECK_EQ(r.m.getParam(Mono::P_TPOSE), -1);
  r.press(2, 3); CHECK_EQ(r.m.getParam(Mono::P_MUTE), 1); r.press(2, 3); CHECK_EQ(r.m.getParam(Mono::P_MUTE), 0);
  r.press(11, 3); CHECK_EQ(r.m.getParam(Mono::P_MENU), 0);
}
TEST(pads_mono_menu_pads_do_not_edit_steps) {
  MRig r;
  r.press(11, 3);
  for (int c = 0; c < 8; ++c) for (int row = 0; row < 4; ++row) if (row != 1) r.press(c, row);
  r.press(11, 3);
  for (int i = 1; i <= 16; ++i) CHECK_EQ(r.m.stepAt(i), Mono::kNoStep);
}
TEST(pads_mono_menu_page_pads_tap_and_hold_gesture) {
  MRig r;
  r.m.setParam(Mono::P_NP, 3);
  r.press(11, 3);
  r.m.pad(1, 1, true); CHECK_EQ(r.m.getParam(Mono::P_EP), 1);                    // a tap acts on release
  r.m.pad(1, 1, false); CHECK_EQ(r.m.getParam(Mono::P_EP), 2);
  r.press(5, 1); CHECK_EQ(r.m.getParam(Mono::P_EP), 2);                          // page 6 does not exist (np = 3): not selectable
  r.m.pad(0, 1, true); r.m.pad(4, 1, true);                                      // hold 1 + tap 5 = five pages
  CHECK_EQ(r.m.getParam(Mono::P_NP), 5);
  r.m.pad(4, 1, false); r.m.pad(0, 1, false);
  CHECK_EQ(r.m.getParam(Mono::P_EP), 2);                                         // the gesture does not change the edit page
  r.m.pad(0, 1, true); r.m.pad(2, 1, true); r.m.pad(2, 1, false); r.m.pad(0, 1, false);
  CHECK_EQ(r.m.getParam(Mono::P_NP), 3);
  r.m.pad(0, 1, true);                                                           // a finger down on a page pad ...
  r.press(11, 3);                                                                // ... the menu closes ...
  r.m.pad(0, 1, false);                                                          // ... its release must not leave the gesture half done
  CHECK_EQ(r.m.getParam(Mono::P_EP), 2);
  r.press(11, 3); r.press(2, 1); CHECK_EQ(r.m.getParam(Mono::P_EP), 3);          // and the next tap works
}
TEST(pads_mono_menu_clear_all_and_loop_gesture_ends_with_the_view) {
  MRig r; r.m.setStep(1, 0); r.m.setStep(20, 2); r.m.setParam(Mono::P_NP, 3);
  r.m.pad(8, 3, true);                      // Loop held in the note view ...
  r.press(11, 3);                           // ... the menu opens: the gesture is over
  r.press(11, 3);
  r.press(3, 0);                            // a plain tap edits again (step 4, row 0)
  CHECK_EQ(r.m.stepAt(4), 3);
  r.m.pad(8, 3, false);
  r.press(11, 3); r.press(7, 3);            // Clear all (menu row 3, col 7)
  r.press(11, 3);
  CHECK_EQ(r.m.stepAt(1), Mono::kNoStep); CHECK_EQ(r.m.stepAt(20), Mono::kNoStep); CHECK_EQ(r.m.getParam(Mono::P_NP), 1);
}
TEST(pads_mono_leds_menu_view_and_note_view_controls) {
  MRig r; uint8_t l[kPads], ro[kPads];
  r.m.leds(l, ro);
  CHECK_EQ(l[3 * 12 + 11], 6); CHECK_EQ(ro[3 * 12 + 11], ROLE_CONTROL);          // note view: Menu pad
  r.press(11, 3); r.m.leds(l, ro);
  CHECK_EQ(l[3 * 12 + 11], 15);                                                  // menu view: the same pad, lit
  CHECK(l[0 * 12 + 7] >= 8);                                                     // speed 8 (the default 1/16) is the bright one (blink phase)
  for (int i = 0; i < kPads; ++i) CHECK(l[i] <= 15);
  CHECK_EQ(l[1 * 12 + 0] >= 9, true);                                            // edit page 1 blinks bright
}
