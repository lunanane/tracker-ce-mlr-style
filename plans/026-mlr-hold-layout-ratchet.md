# Plan 026 — MLR: deliberate engage, slot layout matching the pads, jog quantise + ratchet (owner 2026-11-07)

Owner: "The MLR often triggers the loop accidentally, so maybe we need a bit of a long press for two buttons to engage
it. Also the settings on screen for each slot don't match the 4x2 layout of the buttons, so it's confusing; adapt the
layout to match the buttons. Also I'd like to use the jog wheel to dial in quantisation and, based on it, bring in a
ratchet when holding at a position."

- **Engage**: the accidental trigger is the one the owner means — find which pad/button press starts a loop by
  accident (the two-button combo that engages MLR / a slot) and require both held ~0.3–0.4 s to engage, with an
  on-screen progress/label ("hold both"). Owner exception to the no-long-press rule, only for this engage. Normal pad
  jamming inside a running loop stays instant.
- **Slot settings layout**: the on-screen per-slot settings are drawn in the same 4x2 arrangement as the physical
  buttons that change them, so each setting sits where its button is.
- **Jog quantise**: on the MLR page the jog wheel dials the jump quantisation (off, 1/32 … 1 bar, shown on screen).
- **Ratchet**: holding a step pad retriggers that position at the quantise rate (ratchet) while held; release returns
  to normal playback. Off when quantise is off. Stop = panic.
- Saved with the project (seqclips/seqwarp or the MLR sidecar, oldest-version rule).

Limits: flash ~30 KB left — report the size delta; display stages ≤ 3840 B.

## As built (v0.4.5, original Tracker only; branch v045-mlr; host-tested, not device-tested)

**Which press starts a loop by accident.** `Playhead::press`: when a pad of a slot goes down while another pad of the same slot is still down, the two make a
loop (range between them, starts at the first pad, **latches** after the release; only a single pad ends it). Two fingers that overlap while jamming or rolling
across neighbouring pads (both rows of a slot are one track), or a resting finger plus a jam, therefore start a loop at once. A single pad, the other slot,
or pads that are not down together never do. This is the press that now needs the hold.

**Engage (deliberate loop).** `Cut::setDeliberate(true)` (set by `seqCutInit`; the engine default stays the old instant loop so the plan 002-016 engine tests
are unchanged): a second pad while another is held is only a JUMP to that pad (instant, as every jam). `cutLayer.cpp` times the pair: the clock starts when the
second pad of a slot goes down (`s_eStart`, from `cutUpdate(nowMs)`, called every main loop pass from `seqCutUpdate` with an `elapsedMillis`), after
`seqcut::kEngageMs` = **350 ms** with both still down it posts `packEngage()` (SUB_RESET col 13) and the playhead builds the loop over the held range
(`Playhead::engage()`, applied at the quantise grid). A third pad while engaged extends the loop at once. Released early = nothing. A first pad that has been
down for ever and a second pad tapped never engages (the timer belongs to the second press). Panic / SYNC ALL / `cutReleaseAll` clear the timer.
Feedback: `cutEngagePercent(slot)` -> `SlotInfo::engage` (steps of 10 %): the lane shows a bar and the words "hold both" while the timer runs.
Pressing a pad at the wrong moment still costs nothing: no long press anywhere else (owner exception only for the loop).

**Slot layout.** `mlrDraw.h`: the eight settings of a slot are a **4 x 2 block** exactly like the pads (cols 9-12 of the slot's two rows):
`STOP REV RATE RSYNC` / `GRID SECT REC MUTE`. To make room the 16 step pads of the lane are 32 px wide (was 38; `kMlrPadW`, lane 512 px, block at x 604, cells 47 x 76,
pitch 49 x 80). The waveform columns are mapped onto the new width. Preview: `core/seqtest/build/preview/mlr0.png` .. `mlr4.png` (`make preview`).

**Jog quantise.** `CutSessionLayer::jog` -> `cutQuantDial(dir)`: one detent = one step through **off, 1/32, 1/16, 1/8, 1/4, 1/2, 1 bar** (stops at the ends; any
detent size is one step; not while the beat repeat page is shown). New `Quant` values `Q_32 = 4`, `Q_8 = 5`, `Q_HALF = 6` (Q_COUNT 7, the old four keep their numbers); the
ring byte of the new ones lives in SUB_RESET cols 10-12 (`packQuant` / `argQuant`). `quantSteps`: 1/32 and 1/16 = 1, 1/8 = 2, 1/4 = 4, 1/2 = 8, 1 bar = 16 (a pattern
that is not whole bars keeps the old bar rule). **1/32** = the jump waits for the next step start or the middle of the step (µstep 25): `Cut::now()` is also called at
µstep 25 for it (`seqCutMicroStep`), the playhead is in "immediate" mode like Q off. The screen button 5 still taps off -> 1/16 -> 1/4 -> 1 bar; its label and the
header (`Q 1/16 (jog)`) show the value in the new names (`quantName`: the old "step / beat / bar" are now "1/16 / 1/4 / 1 bar").

**Ratchet.** `Playhead::setRatchet` (Cut sets it with deliberate mode and quantise != off): when a single-pad jump has been played and its pad is still down, every
following quantise boundary plays that position again (a jump, so `planJump` retriggers the note of the step / the last note before it); 1/32: the position also plays
in the middle of the step (second half-step, like rate x2: `has2`, `pos2 = pos`). 1/4 = the beat from the pad repeats, 1/8 = two steps, 1/2 = eight. Release returns to
normal playback from that position (no jump). A tap shorter than the rate never ratchets; quantise off = no ratchet; an engaged loop is not a ratchet; Stop (panic) and
SYNC ALL end it (held pads are dropped). The lane label shows `RATCHET` (`VF_RATCHET`, view flag bit 7).

**Saved with the project.** Jump quantise and the tracks of slots A / B ride in the beat repeat sidecar `seqbrpt.bin` (it is the file of this page; `BrParams::mlrQuant /
mlrSlotA / mlrSlotB`). **Oldest-version rule**: defaults (beat, 1, 2) = version 1 with the 8-byte payload exactly as before (every earlier firmware reads it); any change =
version 2 (11 bytes); the reader accepts 1 and 2, a version 3+ file is `FRAME_NEWER` (left alone). A project load calls `seqCutLoadSettings` -> `cutMlrLoad` (no write asked for);
jog / Assign changes call `seqBrAct(BA_SETTINGS)` (the existing dirty gate / write after stop). Test: `test_p26_sidecar_version`. Grid / hold per track are still not saved.

**Open device bug (pads triggered the first section after the page moved on).** Reproduced on the host (`playhead_stale_hold_from_another_section_is_forgotten`,
`test_p26_section_scroll_stale_hold`): `Playhead::press` took the section of a pad that was ALREADY held (`heldBase_`) instead of the section the screen shows, so a pad of
section 0 that stayed down (a resting finger, a lost release) made the next pad act in section 0 (as a loop with it) although section 1 was on the screen; SYNC ALL cured it
because it releases the held pads - exactly the report. Fix: a hold that belongs to another section than the shown one is forgotten when a pad goes down. The cue cache was
checked and is NOT the cause (wants follow `view().sec`, a hit is keyed on the frame, a stale entry only costs the 70 ms window). Not proven: how the pad got stuck on the
device (a lost release was not found in the layer; a finger resting is enough). With the ratchet a pad held on purpose repeats, so the page then stays where the pad is.

**Tests.** `core/seqtest/test_mlr26.inc` (9 tests: engage engine + layer timing, jog / dial order / names, landing at 1/8 and 1/2 and 1/32, ratchet at 1/16, 1/4, 1/8, 1/32, off, loop vs
ratchet, SYNC ALL, stale hold sweep (engine + layer), sidecar versions, settings round trip, 4 x 2 layout bounds + engage budget); `seqgen/test/test_playhead.inc` +6.
Existing checks changed as direct consequences (equally strict): the layer-level two-pad loops of `test_cut.inc` / `test_rec.inc` now engage (`tHoldBoth()` / `packEngage()`);
the label strings "Q beat / bar / step" -> "Q 1/4 / 1 bar / 1/16"; the geometry assertion (`kMlrPadW * 4 == 152` and the 2 x 49 block) -> the 4 x 2 block; the sidecar frame size /
"newer version" byte (version 3) in `test_br.inc`; `mlrScene` gained a quantise and an engage scene (budget loop runs 5 scenes; every stage still <= 3840 B).

**Size.** Image end 0xF5260 (v0.4.4 base 0xF48B8): **+2,472 B** (1,004,128 B, margin 28,064 B below 0xFC000). No -Os added; `check_sdram_ctors.sh` ok. Playhead +12 B each (16 in OCRAM).

**Device checklist.** 1 jam: tap pads in both rows quickly, overlap two fingers: always a jump, never a loop. 2 hold two pads: bar + "hold both" fills in ~0.35 s, then the LOOP label and
the loop range; release both: loop latched; one pad ends it. 3 lift one pad before the bar is full: no loop. 4 jog on the MLR page: header / button show 1/32 .. 1 bar, stops at the ends.
5 hold one pad at 1/16: stutters its step every step, RATCHET label, release: carries on forward; at 1/4 the beat repeats; at 1/32 two strikes per step; quantise off: no ratchet.
6 scroll into the next section while a pad of the first stays down, press a pad in the new section: it acts on the new section. 7 save, reload the project: quantise and slot tracks are back.
8 the 4 x 2 settings block matches the pads (STOP REV RATE RSYNC / GRID SECT REC MUTE). 9 audio with ratchet on a streamed loop: no dropouts / clicks beyond the normal cue start.

## As built (v0.4.5, original Tracker only; branch v045-mlr; host-tested, not device-tested)

**Which press starts a loop by accident.** `Playhead::press`: when a pad of a slot goes down while another pad of the same slot is still down, the two make a
loop (range between them, starts at the first pad, **latches** after the release; only a single pad ends it). Two fingers that overlap while jamming or rolling
across neighbouring pads (both rows of a slot are one track), or a resting finger plus a jam, therefore start a loop at once. A single pad, the other slot,
or pads that are not down together never do. This is the press that now needs the hold.

**Engage (deliberate loop).** `Cut::setDeliberate(true)` (set by `seqCutInit`; the engine default stays the old instant loop so the plan 002-016 engine tests
are unchanged): a second pad while another is held is only a JUMP to that pad (instant, as every jam). `cutLayer.cpp` times the pair: the clock starts when the
second pad of a slot goes down (`s_eStart`, from `cutUpdate(nowMs)`, called every main loop pass from `seqCutUpdate` with an `elapsedMillis`), after
`seqcut::kEngageMs` = **350 ms** with both still down it posts `packEngage()` (SUB_RESET col 13) and the playhead builds the loop over the held range
(`Playhead::engage()`, applied at the quantise grid). A third pad while engaged extends the loop at once. Released early = nothing. A first pad that has been
down for ever and a second pad tapped never engages (the timer belongs to the second press). Panic / SYNC ALL / `cutReleaseAll` clear the timer.
Feedback: `cutEngagePercent(slot)` -> `SlotInfo::engage` (steps of 10 %): the lane shows a bar and the words "hold both" while the timer runs.
No long press anywhere else (owner exception only for the loop).

**Slot layout.** `mlrDraw.h`: the eight settings of a slot are a **4 x 2 block** exactly like the pads (cols 9-12 of the slot two rows):
`STOP REV RATE RSYNC` / `GRID SECT REC MUTE`. To make room the 16 step pads of the lane are 32 px wide (was 38; `kMlrPadW`, lane 512 px, block at x 604, cells 47 x 76,
pitch 49 x 80). The waveform columns are mapped onto the new width. Preview: `core/seqtest/build/preview/mlr0.png` .. `mlr4.png` (`make preview`).

**Jog quantise.** `CutSessionLayer::jog` -> `cutQuantDial(dir)`: one detent = one step through **off, 1/32, 1/16, 1/8, 1/4, 1/2, 1 bar** (stops at the ends; any
detent size is one step; not while the beat repeat page is shown). New `Quant` values `Q_32 = 4`, `Q_8 = 5`, `Q_HALF = 6` (Q_COUNT 7, the old four keep their numbers); the
ring byte of the new ones lives in SUB_RESET cols 10-12 (`packQuant` / `argQuant`). `quantSteps`: 1/32 and 1/16 = 1, 1/8 = 2, 1/4 = 4, 1/2 = 8, 1 bar = 16 (a pattern
that is not whole bars keeps the old bar rule). **1/32** = the jump waits for the next step start or the middle of the step (micro step 25): `Cut::now()` is also called there
for it (`seqCutMicroStep`), the playhead is in "immediate" mode like Q off. The screen button 5 still taps off -> 1/16 -> 1/4 -> 1 bar; its label and the
header (`Q 1/16 (jog)`) show the value in the new names (`quantName`: the old "step / beat / bar" are now "1/16 / 1/4 / 1 bar").

**Ratchet.** `Playhead::setRatchet` (Cut sets it with deliberate mode and quantise != off): when a single-pad jump has been played and its pad is still down, every
following quantise boundary plays that position again (a jump, so `planJump` retriggers the note of the step / the last note before it); 1/32: the position also plays
in the middle of the step (second half-step, like rate x2: `has2`, `pos2 = pos`). 1/4 = the beat from the pad repeats, 1/8 = two steps, 1/2 = eight. Release returns to
normal playback from that position (no jump). A tap shorter than the rate never ratchets; quantise off = no ratchet; an engaged loop is not a ratchet; Stop (panic) and
SYNC ALL end it (held pads are dropped). The lane label shows `RATCHET` (`VF_RATCHET`, view flag bit 7).

**Saved with the project.** Jump quantise and the tracks of slots A / B ride in the beat repeat sidecar `seqbrpt.bin` (it is the file of this page; `BrParams::mlrQuant /
mlrSlotA / mlrSlotB`). **Oldest-version rule**: defaults (1/4, tracks 1 and 2) = version 1 with the 8-byte payload exactly as before (every earlier firmware reads it); any change =
version 2 (11 bytes); the reader accepts 1 and 2, a version 3+ file is `FRAME_NEWER` (left alone). A project load calls `seqCutLoadSettings` -> `cutMlrLoad` (no write asked for);
jog / Assign changes call `seqBrAct(BA_SETTINGS)` (the existing dirty gate / write after stop). Test: `test_p26_sidecar_version`. Grid / hold per track are still not saved.

**Open device bug (pads triggered the first section after the page moved on).** Reproduced on the host (`playhead_stale_hold_from_another_section_is_forgotten`,
`test_p26_section_scroll_stale_hold`): `Playhead::press` took the section of a pad that was ALREADY held (`heldBase_`) instead of the section the screen shows, so a pad of
section 0 that stayed down (a resting finger, a lost release) made the next pad act in section 0 (as a loop with it) although section 1 was on the screen; SYNC ALL cured it
because it releases the held pads - exactly the report. Fix: a hold that belongs to another section than the shown one is forgotten when a pad goes down. The cue cache was
checked and is NOT the cause (wants follow `view().sec`, a hit is keyed on the frame, a stale entry only costs the 70 ms window). Not proven: how the pad got stuck on the
device (a lost release was not found in the layer; a resting finger is enough). With the ratchet a pad held on purpose repeats, so the page then stays where the pad is.

**Tests.** `core/seqtest/test_mlr26.inc` (9 tests: engage engine + layer timing, jog / dial order / names, landing at 1/8, 1/2 and 1/32, ratchet at 1/16, 1/4, 1/8, 1/32, off, loop vs
ratchet, SYNC ALL, stale hold sweep (engine + layer), sidecar versions, settings round trip, 4 x 2 layout bounds + engage budget); `seqgen/test/test_playhead.inc` +6.
Existing checks changed as direct consequences (equally strict): the layer-level two-pad loops of `test_cut.inc` / `test_rec.inc` now engage (`tHoldBoth()` / `packEngage()`);
the label strings "Q beat / bar / step" -> "Q 1/4 / 1 bar / 1/16"; the geometry assertion (`kMlrPadW * 4 == 152` and the 2 x 49 block) -> the 4 x 2 block; the sidecar frame size /
"newer version" byte (version 3) in `test_br.inc`; `mlrScene` gained a quantise and an engage scene (budget loop runs 5 scenes; every stage still <= 3840 B).
Note: `core/seqtest/Makefile` does not track the `.inc` files as dependencies; touch `test_main.cpp` after editing one.

**Size.** Image end 0xF5260 (v0.4.4 base 0xF48B8): **+2,472 B** (1,004,128 B, margin 28,064 B below 0xFC000). No -Os added; `check_sdram_ctors.sh` ok. Playhead +12 B each (16 in OCRAM).

**Device checklist.** 1 jam: tap pads in both rows quickly, overlap two fingers: always a jump, never a loop. 2 hold two pads: bar + "hold both" fills in about 0.35 s, then the LOOP label and
the loop range; release both: loop latched; one pad ends it. 3 lift one pad before the bar is full: no loop. 4 jog on the MLR page: header / button show 1/32 .. 1 bar, stops at the ends.
5 hold one pad at 1/16: stutters its step every step, RATCHET label, release: carries on forward; at 1/4 the beat repeats; at 1/32 two strikes per step; quantise off: no ratchet.
6 scroll into the next section while a pad of the first stays down, press a pad in the new section: it acts on the new section. 7 save, reload the project: quantise and slot tracks are back.
8 the 4 x 2 settings block matches the pads (STOP REV RATE RSYNC / GRID SECT REC MUTE). 9 audio with ratchet on a streamed loop: no dropouts / clicks beyond the normal cue start.
