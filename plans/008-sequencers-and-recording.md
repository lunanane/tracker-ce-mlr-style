# Plan 008 — usable sequencers, recording and MLR (owner 2026-11-04, after the v0.2.x hardware tests)

Owner: "none of the sequencer scripts is usable; the Shift tutorial only shows unknown three-letter descriptions;
the chord sequencer is not a good translation of grid composer; Mono is not close either; we completely lack the
HYPER_RASP sequencers." Recording: "Rec just doesn't do its job — it only works for a millisecond, and a new
recording overwrites the old one completely." MLR: "unusable so far — I can only retrigger a position; record holds
only ~10 ms, no sequence recording possible, no quantisation snapping."

## A — recording and MLR (bugs first, highest priority)
- **Play-mode Rec**: Rec is a toggle (press = take starts, press again = take ends; no holding). During a take the
  steps that pass are replaced by what the sequencer plays (punch region); steps outside the take stay untouched — a
  new take never wipes the whole track. Shift+Rec: exactly one loop (loop start → loop end), as planned in plan 001.
  Find why takes last ~1 ms on hardware (button event flow, immediate release, take ended by the router/panic/
  takeover logic, …) — reproduce in a host test of the real event sequence first.
- **MLR**: jumps, two-pad loops, reverse, rate must work as in plan 002 (owner can only retrigger a position).
  Playhead recording: Rec toggles a take (not hold); the gestures played during the take loop on as a sequence after
  the take ends, in any group. Same ~10 ms root cause likely. **Quantisation**: pad jumps snap to a grid (off / step /
  beat / bar, default beat), set on the MLR screen (screen button, shown on screen).
- Host tests that model the real hardware event sequence (press/hold/release timing as the keys driver sends it).

## B — flash room
Owner: build all non-audio stock code with -Os on MK66 (screens, file manager, display lib, …); audio path and
sequencer timing code stay -O2. Report the freed space and the image end (< 0xF0000).

## C — grid composer c34, faithful (owner priority 1 and 2)
Rework Chord/Arp (c34 page 3 without arranger) and Mono (c34 page 4) so they behave like c34 on the iii grid
(E:\claude\iii-grid-composer\monome_iii_c34.lua is the reference; the seqgen port + lua_compare exist — find where
behaviour and especially the pad layout / interaction diverge from c34 on a 16x8 grid vs the Tracker's 12x4 pads, and
choose a mapping that keeps c34's playing feel). The Shift legend must use real words (e.g. "Root", "Chord type",
"Octave", "Hold"), not three-letter codes; the legend explains how to play the script.

## D — HYPER_RASP sequencers (owner priority 3 then 4)
Port from E:\claude\_HYPER_RASP\scripts (8x4 pad scripts — map to the Tracker's 12x4 with 4 extra columns):
3. arp_lines, arp_seq, chord_arp, chord_stabs; 4. bassline_seq, euclid_trigger, grid_seq, step_follower.
Prefer porting HYPER_RASP's pad-script API once (C++) and the scripts onto it 1:1, with differential tests against
the Lua (as seqgen/test/lua_compare does for c34). Each becomes a sequencer type in the type list.
