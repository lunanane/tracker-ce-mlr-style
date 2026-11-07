// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Playhead tests (plan 002, S1). Included from test_main.cpp.

static Playhead::StepOut stp(Playhead& p) { return p.step(); }
static void steps(Playhead& p, int n) { for (int i = 0; i < n; ++i) p.step(); }

TEST(playhead_free_run_wraps_without_jump_flag) {
  Playhead p; p.setLength(4);
  const int exp[9] = {0, 1, 2, 3, 0, 1, 2, 3, 0};
  for (int i = 0; i < 9; ++i) { Playhead::StepOut o = stp(p); CHECK_EQ(o.pos, exp[i]); CHECK(!o.jumped); CHECK(!o.hold); CHECK(!o.has2); }
}

TEST(playhead_column_mapping_8_and_12) {
  const int lens[4] = {16, 64, 100, 256};
  const int cols[2] = {8, 12};
  for (int li = 0; li < 4; ++li) for (int ci = 0; ci < 2; ++ci) {
    for (int c = 0; c < cols[ci]; ++c) {
      Playhead p; p.setLength(lens[li]); p.setColumns(cols[ci]);
      CHECK_EQ(p.columnPosition(c), c * lens[li] / cols[ci]);
      p.press(c); Playhead::StepOut o = stp(p);
      CHECK_EQ(o.pos, c * lens[li] / cols[ci]);
      CHECK_EQ(stp(p).pos, (c * lens[li] / cols[ci] + 1) % lens[li]);  // continues forward
    }
  }
  Playhead p; p.setColumns(12); CHECK_EQ(p.columns(), 12); p.setColumns(7); CHECK_EQ(p.columns(), 8);
  p.press(11); p.press(8); CHECK_EQ(p.heldCount(), 0);  // 8 columns: cols 8..11 ignored
  p.press(7); p.press(3); p.press(3); CHECK_EQ(p.heldCount(), 2);
}

TEST(playhead_press_quantise_1_jumps_on_next_step) {
  Playhead p; p.setLength(64);
  steps(p, 3);  // 0 1 2
  p.press(5);   // position 40
  CHECK(p.pending());
  Playhead::StepOut o = stp(p);
  CHECK_EQ(o.pos, 40); CHECK(o.jumped); CHECK(!p.pending());
  o = stp(p); CHECK_EQ(o.pos, 41); CHECK(!o.jumped);
  p.release(5);  // releasing a single pad changes nothing
  o = stp(p); CHECK_EQ(o.pos, 42); CHECK(!o.jumped);
}

TEST(playhead_press_of_natural_position_is_not_a_jump) {
  Playhead p; p.setLength(16); steps(p, 2);  // next natural = 2 = column 1
  p.press(1); Playhead::StepOut o = stp(p);
  CHECK_EQ(o.pos, 2); CHECK(!o.jumped);
}

TEST(playhead_quantise_4_waits_for_the_beat) {
  Playhead p; p.setLength(64); p.setQuantise(4);
  steps(p, 2);  // step indices 0,1 done; next is 2
  p.press(4);   // position 32
  Playhead::StepOut o = stp(p); CHECK_EQ(o.pos, 2); CHECK(!o.jumped);  // index 2
  o = stp(p); CHECK_EQ(o.pos, 3); CHECK(p.pending());                  // index 3
  o = stp(p); CHECK_EQ(o.pos, 32); CHECK(o.jumped); CHECK(!p.pending());  // index 4 = boundary
  o = stp(p); CHECK_EQ(o.pos, 33);
  // press exactly before a boundary applies on that very step
  steps(p, 2);  // indices 6,7 -> next is 8
  p.press(1); o = stp(p); CHECK_EQ(o.pos, 8); CHECK(o.jumped);
  p.release(4);  // not held: ignored
  // the last press wins when several land before the boundary
  Playhead q; q.setLength(64); q.setQuantise(4); steps(q, 1);
  q.press(2); q.release(2); q.press(6); q.release(6);
  steps(q, 3); CHECK(q.pending());
  CHECK_EQ(stp(q).pos, 48);
}

TEST(playhead_two_pad_loop_both_press_orders) {
  for (int order = 0; order < 2; ++order) {
    Playhead p; p.setLength(64);  // columns 2..5 -> positions 16..47
    steps(p, 7);
    if (order == 0) { p.press(2); p.press(5); } else { p.press(5); p.press(2); }
    CHECK_EQ(p.heldCount(), 2);
    Playhead::StepOut o = stp(p);
    CHECK(p.looping()); CHECK_EQ(o.pos, 16); CHECK(o.jumped);
    for (int i = 17; i <= 47; ++i) { o = stp(p); CHECK_EQ(o.pos, i); CHECK(!o.jumped); }
    o = stp(p); CHECK_EQ(o.pos, 16); CHECK(o.jumped);  // wrap inside the loop: the engine would have gone on to 48
    steps(p, 10);   // 17..26
    p.release(order == 0 ? 2 : 5);
    CHECK(p.looping());  // plan 008 (MLR style): the loop latches, it stays after a pad is released
    o = stp(p); CHECK_EQ(o.pos, 27); CHECK(!o.jumped);
    steps(p, 19);   // 28..46
    o = stp(p); CHECK_EQ(o.pos, 47);
    o = stp(p); CHECK_EQ(o.pos, 16); CHECK(o.jumped);  // the loop wall is still there
    p.release(order == 0 ? 5 : 2);
    CHECK(p.looping() && p.loopLo() == 2 && p.loopHi() == 5);  // all pads up: still latched
    steps(p, 40);
    CHECK(p.position() >= 16 && p.position() <= 47);
    // a single pad in the row jumps and ends the loop
    p.press(0); o = stp(p); CHECK_EQ(o.pos, 0); CHECK(o.jumped); CHECK(!p.looping());
    o = stp(p); CHECK_EQ(o.pos, 1); CHECK(!o.jumped);
    p.release(0);
    o = stp(p); CHECK_EQ(o.pos, 2);
  }
}

TEST(playhead_loop_three_pads_uses_outer_columns_and_reverse_starts_at_end) {
  Playhead p; p.setLength(64);
  p.press(1); p.press(3); p.press(6);  // 8..55
  CHECK_EQ(stp(p).pos, 8);
  steps(p, 47);  // 9..55
  CHECK_EQ(p.position(), 55);
  CHECK_EQ(stp(p).pos, 8);
  p.release(3); p.release(6); p.release(1);
  Playhead r; r.setLength(64); r.setReverse(true);
  r.press(2); r.press(3);  // 16..31, reversed: starts at 31
  CHECK_EQ(stp(r).pos, 31); CHECK_EQ(stp(r).pos, 30);
  steps(r, 14);            // 29..16
  CHECK_EQ(r.position(), 16);
  CHECK_EQ(stp(r).pos, 31);
}

TEST(playhead_loop_one_column_when_track_is_shorter_than_columns) {
  Playhead p; p.setLength(4);  // 8 columns -> positions 0,0,1,1,2,2,3,3
  p.press(2); p.press(2);       // second press of a held column is ignored
  Playhead::StepOut o = stp(p); CHECK_EQ(o.pos, 1);
  Playhead q; q.setLength(4); q.press(4); q.press(5);  // both map to position 2
  CHECK_EQ(stp(q).pos, 2); CHECK_EQ(stp(q).pos, 2); CHECK_EQ(stp(q).pos, 2);
}

// Rec armed, then 32 steps of free running with the jumps col 6 at step 8, col 1 at step 14 and col 4 at step 25 (len 16). The take
// starts with the first applied gesture (step 8, plan 008): it holds the 24 steps from there, exp[0..23] (exp[0] = 12 = column 6).
static int takeThreeJumps(Playhead& p, uint8_t* exp, bool withHolds = false) {
  (void)withHolds;
  p.recDown();
  int n = 0;
  for (int i = 0; i < 32; ++i) {
    if (i == 8) { p.press(6); p.release(6); }
    if (i == 14) { p.press(1); p.release(1); }
    if (i == 25) { p.press(4); p.release(4); }
    uint8_t pos = p.step().pos;
    if (i >= 8) exp[n++] = pos;
  }
  p.recUp();
  return n;
}

TEST(playhead_recording_three_jumps_replays_identically) {
  Playhead p;  // length 16, 8 columns
  uint8_t exp[32]; int n = takeThreeJumps(p, exp);
  CHECK(p.recorded()); CHECK_EQ(p.recordedLength(), n); CHECK(!p.recording());
  CHECK_EQ(n, 24);
  CHECK_EQ(exp[0], 12); CHECK_EQ(exp[1], 13); CHECK_EQ(exp[6], 2); CHECK_EQ(exp[17], 8);
  for (int loop = 0; loop < 5; ++loop) for (int i = 0; i < n; ++i) {
    Playhead::StepOut o = stp(p);
    CHECK_EQ(o.pos, exp[i]);
    CHECK_EQ(o.jumped, o.pos != (i == 0 ? exp[n - 1] + 1 : exp[i - 1] + 1) % 16);
  }
}

TEST(playhead_recording_ignores_pads_outside_the_take_and_other_instances) {
  Playhead a, b; a.setLength(16); b.setLength(16);
  uint8_t exp[32]; takeThreeJumps(a, exp);
  // b is untouched: free running, no recording, unaffected by a's pads
  for (int i = 0; i < 40; ++i) { a.step(); CHECK_EQ(b.step().pos, i % 16); }
  CHECK(!b.recorded()); CHECK(a.recorded());
  b.press(3); a.step(); CHECK_EQ(b.step().pos, 6);
}

TEST(playhead_press_overrides_recording_and_it_resumes_in_phase_after_release) {
  Playhead p;
  uint8_t exp[32]; int n = takeThreeJumps(p, exp);
  int k = 0;  // index in the loop of the next step
  for (; k < 5; ++k) CHECK_EQ(stp(p).pos, exp[k]);
  p.press(7);  // position 14, held
  Playhead::StepOut o = stp(p); ++k;
  CHECK_EQ(o.pos, 14); CHECK(o.jumped);
  o = stp(p); ++k; CHECK_EQ(o.pos, 15);
  o = stp(p); ++k; CHECK_EQ(o.pos, 0);   // pad still down: free running from the pad, recording is not played
  p.release(7);
  CHECK(p.recorded());                   // kept
  o = stp(p); CHECK_EQ(o.pos, exp[k % n]); ++k;  // back on the recorded clock, in phase
  for (int i = 0; i < 2 * n; ++i, ++k) CHECK_EQ(stp(p).pos, exp[k % n]);
}

TEST(playhead_tap_over_recording_is_one_step_then_back) {
  Playhead p;
  uint8_t exp[32]; int n = takeThreeJumps(p, exp);
  steps(p, 3);
  p.press(5); p.release(5);  // tap before the step: position 10 once
  Playhead::StepOut o = stp(p); CHECK_EQ(o.pos, 10); CHECK(o.jumped);
  for (int i = 4; i < n; ++i) CHECK_EQ(stp(p).pos, exp[i]);
}

TEST(playhead_two_pad_loop_over_recording_also_overrides_while_held) {
  Playhead p;
  uint8_t exp[32]; int n = takeThreeJumps(p, exp);
  p.press(0); p.press(1);  // positions 0..3
  for (int i = 0; i < 9; ++i) CHECK_EQ(stp(p).pos, i % 4);
  p.release(0); p.release(1);
  CHECK(p.recorded());
  CHECK_EQ(stp(p).pos, exp[9 % n]);
}

TEST(playhead_clear_recording_by_resync_and_by_an_empty_take) {
  Playhead p; uint8_t exp[32];
  takeThreeJumps(p, exp); CHECK(p.recorded());
  p.resync(0); CHECK(!p.recorded());
  takeThreeJumps(p, exp); CHECK(p.recorded());
  p.recDown(); steps(p, 5); p.recUp();  // plan 008: a take in which no gesture was played changes nothing, the old loop stays
  CHECK(p.recorded()); CHECK(!p.recording()); CHECK_EQ(p.recordedLength(), 24);
  p.clearRecording(); CHECK(!p.recorded());
  takeThreeJumps(p, exp);
  p.clearRecording(); CHECK(!p.recorded());
  // a new take replaces the old one (it starts with its first gesture)
  takeThreeJumps(p, exp);
  p.recDown(); p.press(2); p.release(2); steps(p, 3); p.recUp();
  CHECK_EQ(p.recordedLength(), 3);
}

TEST(playhead_full_take_closes_itself_at_256_steps) {
  Playhead p; p.setLength(256);
  p.recDown(); p.press(0); p.release(0); steps(p, 255); CHECK(p.recording());
  p.step();
  CHECK(!p.recording()); CHECK_EQ(p.recordedLength(), 256);
  p.recUp();  // no-op
  CHECK_EQ(stp(p).pos, 0); CHECK_EQ(stp(p).pos, 1);
}

TEST(playhead_recording_while_holding_a_loop_records_the_loop) {
  Playhead p; p.setLength(32);  // 8 columns -> 4 steps each
  p.press(1); p.press(2);       // positions 4..11
  p.recDown(); uint8_t exp[16];
  for (int i = 0; i < 16; ++i) exp[i] = p.step().pos;
  p.recUp(); p.release(1); p.release(2);
  CHECK_EQ(exp[0], 4); CHECK_EQ(exp[7], 11); CHECK_EQ(exp[8], 4);
  for (int i = 0; i < 32; ++i) CHECK_EQ(stp(p).pos, exp[i % 16]);
}

TEST(playhead_rate_half_holds_every_second_step) {
  Playhead p; p.setLength(8); p.setRate(Playhead::RATE_HALF);
  const int pos[8] = {0, 0, 1, 1, 2, 2, 3, 3};
  for (int i = 0; i < 8; ++i) { Playhead::StepOut o = stp(p); CHECK_EQ(o.pos, pos[i]); CHECK_EQ(o.hold, (i & 1) == 1); CHECK(!o.jumped); }
  // a jump restarts the 2-step phase, also when it lands on a held step
  p.press(5);  // position 5
  Playhead::StepOut o = stp(p); CHECK_EQ(o.pos, 5); CHECK(o.jumped); CHECK(!o.hold);
  o = stp(p); CHECK_EQ(o.pos, 5); CHECK(o.hold);
  o = stp(p); CHECK_EQ(o.pos, 6); CHECK(!o.hold);
}

TEST(playhead_rate_double_gives_two_positions_per_step) {
  Playhead p; p.setLength(5); p.setRate(Playhead::RATE_DOUBLE);
  Playhead::StepOut o = stp(p); CHECK_EQ(o.pos, 0); CHECK_EQ(o.pos2, 1); CHECK(o.has2); CHECK(!o.jumped); CHECK(!o.jumped2);
  o = stp(p); CHECK_EQ(o.pos, 2); CHECK_EQ(o.pos2, 3); CHECK(!o.jumped);
  o = stp(p); CHECK_EQ(o.pos, 4); CHECK_EQ(o.pos2, 0); CHECK(!o.jumped); CHECK(!o.jumped2);  // wrap is natural
  o = stp(p); CHECK_EQ(o.pos, 1); CHECK_EQ(o.pos2, 2);
  // loop wrap inside a 2-position loop is flagged on the second half-step
  Playhead q; q.setLength(16); q.setRate(Playhead::RATE_DOUBLE); q.press(0); q.press(1);  // positions 0..3
  o = stp(q); CHECK_EQ(o.pos, 0); CHECK_EQ(o.pos2, 1);
  o = stp(q); CHECK_EQ(o.pos, 2); CHECK_EQ(o.pos2, 3); CHECK(!o.jumped2);
  o = stp(q); CHECK_EQ(o.pos, 0); CHECK(o.jumped); CHECK_EQ(o.pos2, 1);
  Playhead r; r.setLength(16); r.setRate(Playhead::RATE_DOUBLE); r.press(0); r.press(1);
  steps(r, 1); o = stp(r);  // 2,3
  Playhead s; s.setLength(3); s.setRate(Playhead::RATE_DOUBLE); stp(s); o = stp(s);  // 2 then 0
  CHECK_EQ(o.pos, 2); CHECK_EQ(o.pos2, 0); CHECK(!o.jumped2);
}

TEST(playhead_reverse_runs_backwards) {
  Playhead p; p.setLength(4); p.setReverse(true);
  const int exp[8] = {2, 1, 0, 3, 2, 1, 0, 3};
  for (int i = 0; i < 8; ++i) { Playhead::StepOut o = stp(p); CHECK_EQ(o.pos, exp[i]); CHECK(o.jumped); }  // never the engine's +1
  p.setLength(64); p.press(4);  // jump to 32, then backwards
  CHECK_EQ(stp(p).pos, 32); CHECK_EQ(stp(p).pos, 31);
  p.setReverse(false); CHECK_EQ(stp(p).pos, 32);  // forward again from where it is
}

TEST(playhead_resync_aligns_to_the_global_step_and_clears_state) {
  Playhead p; p.setLength(16);
  p.press(2); p.press(3); steps(p, 5);
  CHECK(p.looping());
  p.resync(37);  // 37 % 16 = 5
  CHECK(!p.looping()); CHECK_EQ(p.heldCount(), 0);
  Playhead::StepOut o = stp(p); CHECK_EQ(o.pos, 5); CHECK(o.jumped);
  o = stp(p); CHECK_EQ(o.pos, 6); CHECK(!o.jumped);
  p.release(2); p.release(3);  // stray releases are harmless
  CHECK_EQ(stp(p).pos, 7);
  p.resync(); CHECK_EQ(stp(p).pos, 0);
  // quantise follows the global step counter after a resync
  Playhead q; q.setLength(64); q.setQuantise(4); q.resync(6);  // next step has index 6, boundary at 8
  q.press(7);
  CHECK_EQ(stp(q).pos, 6); CHECK_EQ(stp(q).pos, 7); CHECK_EQ(stp(q).pos, 56);
}

TEST(playhead_two_instances_are_independent) {
  Playhead a, b; a.setLength(16); b.setLength(64); b.setQuantise(4);
  a.press(4); b.press(2); a.press(5);
  for (int i = 0; i < 8; ++i) { a.step(); b.step(); }
  CHECK(a.looping()); CHECK(!b.looping()); CHECK_EQ(a.heldCount(), 2); CHECK_EQ(b.heldCount(), 1);
  CHECK_EQ(b.position(), 16 + 7 - 0);  // jumped at index 0 to 16, 8 steps -> 23
  a.resync(0); CHECK(b.position() == 23);
  a.recDown(); CHECK(!b.recording());
}

TEST(playhead_note_offset_lookup) {
  uint8_t has[16] = {0}; has[2] = 1; has[10] = 1;
  int s = -1, d = -1;
  CHECK(Playhead::noteOffsetFor(10, has, 16, s, d)); CHECK_EQ(s, 10); CHECK_EQ(d, 0);
  CHECK(Playhead::noteOffsetFor(12, has, 16, s, d)); CHECK_EQ(s, 10); CHECK_EQ(d, 2);
  CHECK(Playhead::noteOffsetFor(2, has, 16, s, d)); CHECK_EQ(s, 2); CHECK_EQ(d, 0);
  CHECK(Playhead::noteOffsetFor(5, has, 16, s, d)); CHECK_EQ(s, 2); CHECK_EQ(d, 3);
  CHECK(Playhead::noteOffsetFor(1, has, 16, s, d)); CHECK_EQ(s, 10); CHECK_EQ(d, 7);   // wraps to the end of the track
  CHECK(Playhead::noteOffsetFor(0, has, 16, s, d)); CHECK_EQ(s, 10); CHECK_EQ(d, 6);
  CHECK(Playhead::noteOffsetFor(15, has, 16, s, d)); CHECK_EQ(s, 10); CHECK_EQ(d, 5);
  uint8_t none[16] = {0};
  CHECK(!Playhead::noteOffsetFor(3, none, 16, s, d));
  CHECK(!Playhead::noteOffsetFor(3, 0, 16, s, d));
  uint8_t one[1] = {1};
  CHECK(Playhead::noteOffsetFor(0, one, 1, s, d)); CHECK_EQ(s, 0); CHECK_EQ(d, 0);
  uint8_t last[256] = {0}; last[255] = 1;
  CHECK(Playhead::noteOffsetFor(3, last, 256, s, d)); CHECK_EQ(s, 255); CHECK_EQ(d, 4);
  Playhead p; p.setLength(16);
  CHECK(p.noteOffsetFor(12, has, s, d)); CHECK_EQ(s, 10); CHECK_EQ(d, 2);
}

TEST(playhead_serialize_round_trip) {
  Playhead p; p.setLength(100); p.setColumns(12); p.setQuantise(4); p.setRate(Playhead::RATE_HALF); p.setReverse(true);
  p.setRate(Playhead::RATE_NORMAL);
  p.recDown(); p.press(3); steps(p, 5); p.release(3); steps(p, 6); p.recUp();
  for (int i = 0; i < 4 && p.recording(); ++i) p.step();  // quantise 4: Rec off closes the take on the next grid boundary
  CHECK(!p.recording());
  CHECK(p.recorded());
  uint8_t buf[Playhead::kSerialSize + 4];
  CHECK_EQ(p.serialize(buf, sizeof buf), Playhead::kSerialSize);
  CHECK_EQ(p.serialize(buf, Playhead::kSerialSize - 1), 0);
  Playhead q; CHECK(q.deserialize(buf, sizeof buf));
  CHECK_EQ(q.length(), 100); CHECK_EQ(q.columns(), 12); CHECK_EQ(q.quantise(), 4); CHECK(q.reverse());
  CHECK_EQ(q.rate(), Playhead::RATE_NORMAL); CHECK_EQ(q.recordedLength(), p.recordedLength());
  // the two replay identically (the original has already advanced its recorded clock: both restart from the loop start)
  Playhead r; CHECK(r.deserialize(buf, sizeof buf));
  for (int i = 0; i < 33; ++i) { Playhead::StepOut a = q.step(), b = r.step(); CHECK_EQ(a.pos, b.pos); CHECK_EQ(a.jumped, b.jumped); }
  // bad data is rejected and leaves the target untouched
  Playhead t; t.setLength(7);
  buf[0] = 99; CHECK(!t.deserialize(buf, sizeof buf)); buf[0] = Playhead::kSerialVersion;
  CHECK(!t.deserialize(buf, Playhead::kSerialSize - 1));
  buf[3] = 9; CHECK(!t.deserialize(buf, sizeof buf)); buf[3] = 12;
  buf[1] = 0; buf[2] = 0; CHECK(!t.deserialize(buf, sizeof buf));
  CHECK_EQ(t.length(), 7);
  Playhead d; d.deserialize(buf, 0);  // no crash
  printf("    sizeof(Playhead) = %d, kSerialSize = %d\n", (int)sizeof(Playhead), (int)Playhead::kSerialSize);
}

TEST(playhead_settings_clamp) {
  Playhead p; p.setLength(0); CHECK_EQ(p.length(), 1); p.setLength(9999); CHECK_EQ(p.length(), 256);
  p.setQuantise(0); CHECK_EQ(p.quantise(), 1);
  p.setLength(1); CHECK_EQ(stp(p).pos, 0); CHECK_EQ(stp(p).pos, 0);
  p.setLength(16); steps(p, 10); p.setLength(4); CHECK(p.position() < 4);
}

TEST(playhead_restart_continues_without_jump_and_keeps_gestures) {
  Playhead p; p.setLength(16); steps(p, 3);
  p.restart(9);                       // next step is 9, natural
  p.press(0);                         // queued jump to 0 survives a restart
  Playhead::StepOut o = stp(p); CHECK_EQ(o.pos, 0); CHECK(o.jumped);
  Playhead q; q.setLength(16); q.restart(9);
  o = stp(q); CHECK_EQ(o.pos, 9); CHECK(!o.jumped);
  o = stp(q); CHECK_EQ(o.pos, 10); CHECK(!o.jumped);
  Playhead r; r.setLength(8); r.setReverse(true); r.restart(3);
  o = stp(r); CHECK_EQ(o.pos, 3);
  o = stp(r); CHECK_EQ(o.pos, 2);
}

TEST(playhead_restart_keeps_recorded_loop_and_rewinds_it) {
  Playhead p; p.setLength(16); steps(p, 2);
  p.recDown(); p.press(4); steps(p, 3); p.recUp();  // take of 3 steps: 8 9 10
  CHECK(p.recorded());
  p.release(4);
  p.restart(3);                                      // the take began at step 2: loop index (3 - 2) % 3 = 1
  Playhead::StepOut o = stp(p); CHECK_EQ(o.pos, 9);
  o = stp(p); CHECK_EQ(o.pos, 10); CHECK(p.recorded());
  p.restart(1);                                      // before the take's start: (1 - 2) mod 3 = 2
  o = stp(p); CHECK_EQ(o.pos, 10);
}

// ---- plan 008 A (owner review): latched loops, takes on the grid, immediate mode, x2 takes ----
TEST(playhead_pending_loop_survives_release_before_the_grid) {
  Playhead p; p.setLength(64); p.setQuantise(4); steps(p, 1);  // the next step has index 1, the grid is at 4
  p.press(2); p.press(5); p.release(2); p.release(5);
  CHECK(p.pending()); CHECK_EQ(p.heldCount(), 0);
  steps(p, 3);
  Playhead::StepOut o = stp(p);
  CHECK(p.looping()); CHECK_EQ(o.pos, 16); CHECK(o.jumped);
  for (int i = 17; i <= 47; ++i) CHECK_EQ(stp(p).pos, i);
  CHECK_EQ(stp(p).pos, 16);  // latched: no pad is down and the loop still turns
}

TEST(playhead_take_starts_on_the_grid_and_closes_on_it) {
  Playhead p; p.setLength(16); p.setQuantise(4); steps(p, 1);
  p.recDown(); p.press(6); p.release(6);   // the jump waits for index 4
  steps(p, 3);
  CHECK(p.recording()); CHECK(!p.recorded());
  Playhead::StepOut o = stp(p);            // index 4: the gesture is applied, the take starts here
  CHECK_EQ(o.pos, 12);
  steps(p, 2);                             // 13, 14
  p.recUp();                               // not on the grid: the take runs on to it
  CHECK(p.recording());
  steps(p, 1);                             // 15, then index 8 is the grid
  CHECK(!p.recording()); CHECK(p.recorded()); CHECK_EQ(p.recordedLength(), 4);
  for (int lap = 0; lap < 3; ++lap) for (int i = 0; i < 4; ++i) CHECK_EQ(stp(p).pos, 12 + i);
  // Rec off before any gesture was applied: nothing recorded, the loop that replays stays
  p.recDown(); p.recUp(); CHECK(!p.recording()); CHECK(p.recorded());
}

TEST(playhead_rate_double_take_keeps_its_second_half_steps) {
  for (int rev = 0; rev < 2; ++rev) {
    Playhead p; p.setLength(16); p.setRate(Playhead::RATE_DOUBLE); p.setReverse(rev != 0);
    p.recDown(); p.press(4); p.release(4);  // position 8
    uint8_t pos[4], pos2[4];
    for (int i = 0; i < 4; ++i) { Playhead::StepOut o = stp(p); CHECK(o.has2); pos[i] = o.pos; pos2[i] = o.pos2; }
    p.recUp();
    CHECK(p.recorded());
    for (int lap = 0; lap < 3; ++lap) for (int i = 0; i < 4; ++i) {
      Playhead::StepOut o = stp(p);
      CHECK(o.has2); CHECK_EQ(o.pos, pos[i]); CHECK_EQ(o.pos2, pos2[i]);
    }
  }
}

TEST(playhead_immediate_mode_applies_between_steps) {
  Playhead p; p.setLength(16); p.setImmediate(true); steps(p, 3);
  Playhead::StepOut o;
  CHECK(!p.applyNow(o));
  p.press(6);
  CHECK(p.applyNow(o)); CHECK_EQ(o.pos, 12); CHECK(o.jumped);
  CHECK(!p.applyNow(o));  // consumed: the next boundary does not jump again
  o = stp(p); CHECK_EQ(o.pos, 13); CHECK(!o.jumped);
  Playhead q; q.press(6); CHECK(!q.applyNow(o)); CHECK_EQ(stp(q).pos, 12);  // not immediate: waits for the boundary
  Playhead r; r.setLength(64); r.setImmediate(true); r.press(2); r.press(5);
  CHECK(r.applyNow(o)); CHECK_EQ(o.pos, 16); CHECK(r.looping()); CHECK_EQ(r.loopLo(), 2); CHECK_EQ(r.loopHi(), 5);
  // while a take waits for its first gesture the immediate jump starts it
  Playhead t; t.setLength(16); t.setImmediate(true); t.recDown(); t.press(3);
  CHECK(t.applyNow(o)); stp(t); stp(t); t.recUp();
  CHECK(t.recorded()); CHECK_EQ(t.recordedLength(), 2);
}

TEST(playhead_grid_position_decides_the_quantise_boundary) {
  Playhead p; p.setLength(8); p.setQuantise(8);  // a bar of an 8 step pattern
  p.setGridPos(5); p.press(4);
  Playhead::StepOut o = stp(p); CHECK_EQ(o.pos, 0); CHECK(p.pending());   // not on the grid (and no jump yet)
  p.setGridPos(6); o = stp(p); CHECK_EQ(o.pos, 1);
  p.setGridPos(0); o = stp(p); CHECK_EQ(o.pos, 4); CHECK(o.jumped);       // the pattern start is the grid
}

// ---- plan 016 A: grid mode (16 pads of `stepsPerPad` steps address one section, the one the playhead is in) ----
TEST(playhead_grid_pads_address_the_current_section) {
  for (int spp = 2; spp <= 4; spp += 2) {
    const int ss = 16 * spp;
    Playhead p; p.setLength(4 * ss); p.setGrid(spp);
    CHECK_EQ(p.columns(), 16); CHECK_EQ(p.gridSteps(), spp); CHECK_EQ(p.activePads(), 16);
    for (int sec = 0; sec < 4; ++sec) {
      Playhead q; q.setLength(4 * ss); q.setGrid(spp); q.restart((uint32_t)(sec * ss + 3));
      CHECK_EQ(q.shownBase(), sec * ss);
      for (int c = 0; c < 16; c += 5) {
        Playhead r = q; CHECK(r.press(c));
        Playhead::StepOut o = r.step(); CHECK_EQ(o.pos, sec * ss + c * spp);
        CHECK(o.pos == sec * ss + c * spp);
      }
    }
  }
}

TEST(playhead_grid_inactive_pads_behind_the_pattern_end) {
  Playhead p; p.setLength(70); p.setGrid(4);   // section 0: steps 0..63, section 1: 64..69 = pads 0, 1 (the 2nd partial)
  p.restart(66);
  CHECK_EQ(p.shownBase(), 64); CHECK_EQ(p.activePads(), 2);
  CHECK(p.press(1)); CHECK(!p.press(2)); CHECK(!p.press(15));
  CHECK_EQ(p.heldCount(), 1);
  Playhead::StepOut o = p.step(); CHECK_EQ(o.pos, 68);
  p.release(1);
  Playhead q; q.setLength(64); q.setGrid(4); q.restart(5);   // a pattern of exactly one section: all 16 pads
  CHECK_EQ(q.activePads(), 16);
  Playhead s; s.setLength(5); s.setGrid(2); CHECK_EQ(s.activePads(), 3);  // 5 steps, 2 per pad: 3 pads
}

TEST(playhead_grid_loop_across_pads_stays_in_its_section_and_clips_to_the_end) {
  Playhead p; p.setLength(70); p.setGrid(4); p.setQuantise(1); p.restart(64); p.step();
  p.press(0); p.press(1);   // pads 0..1 of section 1 = steps 64..71 -> clipped to 64..69
  Playhead::StepOut o = p.step();
  CHECK(p.looping()); CHECK_EQ(p.loopBase(), 64); CHECK_EQ(o.pos, 64);
  p.release(0); p.release(1);
  const int exp[7] = {65, 66, 67, 68, 69, 64, 65};
  for (int i = 0; i < 7; ++i) CHECK_EQ(p.step().pos, exp[i]);
}

TEST(playhead_grid_section_advances_with_the_playhead_unless_held) {
  Playhead p; p.setLength(128); p.setGrid(2); p.setQuantise(1);   // sections of 32 steps
  p.restart(0);
  for (int i = 0; i < 32; ++i) p.step();   // steps 0..31 played
  CHECK_EQ(p.shownBase(), 0);
  p.step();                         // step 32: section 1
  CHECK_EQ(p.shownBase(), 32);
  p.setHoldSection(1);
  for (int i = 0; i < 40; ++i) p.step();   // the playhead is in section 2 now
  CHECK_EQ(p.shownBase(), 32);
  CHECK(p.press(3)); Playhead::StepOut o = p.step(); CHECK_EQ(o.pos, 32 + 6); CHECK(o.jumped);   // the held section is addressed
  p.release(3); p.setHoldSection(-1);
  CHECK_EQ(p.shownBase(), 32);
}

TEST(playhead_grid_loop_set_in_a_section_survives_the_playhead_leaving_via_a_jump) {
  Playhead p; p.setLength(128); p.setGrid(4); p.setQuantise(1); p.restart(0); p.step();
  p.press(2); p.press(3); p.step(); p.release(2); p.release(3);   // loop steps 8..15 of section 0
  for (int i = 0; i < 20; ++i) { int pos = p.step().pos; CHECK(pos >= 8 && pos <= 15); }
  CHECK_EQ(p.shownBase(), 0);       // a loop keeps the section from advancing
  p.setHoldSection(1);
  p.press(0); Playhead::StepOut o = p.step(); CHECK_EQ(o.pos, 64); CHECK(!p.looping());   // a single pad ends the loop
}

TEST(playhead_grid_back_to_even_split_and_serialize_16) {
  Playhead p; p.setLength(100); p.setGrid(4); p.setGrid(0);
  CHECK_EQ(p.gridSteps(), 0); CHECK_EQ(p.columns(), 16);
  CHECK_EQ(p.columnPosition(5), 5 * 100 / 16);
  p.setColumns(16); CHECK_EQ(p.columns(), 16);
  uint8_t buf[Playhead::kSerialSize]; CHECK_EQ(p.serialize(buf, sizeof(buf)), (size_t)Playhead::kSerialSize);
  Playhead q; CHECK(q.deserialize(buf, sizeof(buf))); CHECK_EQ(q.columns(), 16);
}

// ---- plan 026: deliberate loops, ratchet, stale holds ----
TEST(playhead_gate_second_pad_is_a_jump_until_engage) {
  Playhead p; p.setLength(64); p.setGrid(4); p.setDeliberate(true);
  steps(p, 2);
  p.press(2); Playhead::StepOut o = stp(p); CHECK_EQ(o.pos, 8);
  p.press(5);  // the roll: while pad 2 is down
  CHECK(!p.engaged());
  o = stp(p); CHECK_EQ(o.pos, 20); CHECK(o.jumped); CHECK(!p.looping());   // instant jump to the new pad, no loop
  CHECK(p.engage()); CHECK(p.engaged());
  o = stp(p); CHECK_EQ(o.pos, 8); CHECK(p.looping()); CHECK_EQ(p.loopLo(), 2); CHECK_EQ(p.loopHi(), 5);  // loop from the first pad
  p.release(2); p.release(5);
  CHECK(!p.engaged()); CHECK(p.looping());                                  // latched
  for (int i = 0; i < 40; ++i) { int q = stp(p).pos; CHECK(q >= 8 && q <= 23); }
  // with the gate off the second pad makes the loop at once (the earlier behaviour)
  Playhead u; u.setLength(64); u.setGrid(4);
  steps(u, 2); u.press(2); u.press(5); stp(u); CHECK(u.looping());
}

TEST(playhead_engage_needs_two_pads) {
  Playhead p; p.setLength(64); p.setGrid(4); p.setDeliberate(true);
  CHECK(!p.engage());
  p.press(3); CHECK(!p.engage()); CHECK(!p.engaged());
  p.press(4); CHECK(p.engage());
  p.release(3); CHECK(!p.engaged());   // one pad left: the hands are no longer building a loop (the loop itself stays latched)
  p.release(4); CHECK(!p.engaged());
}

TEST(playhead_ratchet_retriggers_the_held_pad_at_the_quantise_rate) {
  Playhead p; p.setLength(64); p.setGrid(4); p.setDeliberate(true); p.setRatchet(true, false);
  steps(p, 3);
  p.press(5);
  Playhead::StepOut o = stp(p); CHECK_EQ(o.pos, 20); CHECK(o.jumped);
  for (int i = 0; i < 5; ++i) { o = stp(p); CHECK_EQ(o.pos, 20); CHECK(o.jumped); CHECK(p.ratcheting()); }
  p.release(5);
  o = stp(p); CHECK_EQ(o.pos, 21); CHECK(!o.jumped); CHECK(!p.ratcheting());   // normal playback from there
  // quantise 4: the pad's beat repeats
  Playhead q; q.setLength(64); q.setGrid(4); q.setDeliberate(true); q.setRatchet(true, false); q.setQuantise(4);
  q.press(5);
  int first = -1, n = 0, seen[10];
  for (int i = 0; i < 20 && n < 10; ++i) { int pos = stp(q).pos; if (first < 0 && pos == 20) first = i; if (first >= 0) seen[n++] = pos; }
  CHECK_EQ(n, 10);
  for (int i = 0; i < n; ++i) CHECK_EQ(seen[i], 20 + i % 4);
  // off: no ratchet, a held pad plays on
  Playhead r; r.setLength(64); r.setGrid(4); r.setDeliberate(true); r.setRatchet(false, false);
  r.press(5); stp(r); CHECK_EQ(stp(r).pos, 21); CHECK_EQ(stp(r).pos, 22);
}

TEST(playhead_ratchet_half_step_plays_the_position_twice) {
  Playhead p; p.setLength(64); p.setGrid(4); p.setDeliberate(true); p.setRatchet(true, true);
  p.press(5);
  Playhead::StepOut o = stp(p); CHECK_EQ(o.pos, 20); CHECK(o.has2); CHECK_EQ(o.pos2, 20); CHECK(o.jumped2);
  o = stp(p); CHECK_EQ(o.pos, 20); CHECK(o.has2); CHECK_EQ(o.pos2, 20); CHECK(o.jumped); CHECK(o.jumped2);
  p.release(5);
  o = stp(p); CHECK_EQ(o.pos, 21); CHECK(!o.has2);
}

TEST(playhead_applynow_arms_the_ratchet_too) {
  Playhead p; p.setLength(64); p.setGrid(4); p.setDeliberate(true); p.setRatchet(true, true); p.setImmediate(true);
  steps(p, 2);
  p.press(5);
  Playhead::StepOut o;
  CHECK(p.applyNow(o)); CHECK_EQ(o.pos, 20); CHECK(p.ratcheting());
  o = stp(p); CHECK_EQ(o.pos, 20); CHECK(o.has2);
}

TEST(playhead_stale_hold_from_another_section_is_forgotten) {
  Playhead p; p.setLength(192); p.setGrid(4);
  steps(p, 10);
  p.press(2);                       // a pad of section 0 that is never let go
  steps(p, 80);                     // the playhead is in section 1 now
  CHECK_EQ(p.shownBase(), 64);
  p.press(5);
  Playhead::StepOut o = stp(p);
  CHECK_EQ(o.pos, 64 + 20);         // acts on the section the screen shows, not on the old one
  CHECK(!p.looping());
}
