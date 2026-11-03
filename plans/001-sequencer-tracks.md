# Plan 001 — Sequencer tracks

Status: DRAFT (2026-11-03). Target hardware: **original Tracker (MK66, 48 pads = 12 × 4)**; Tracker+ should work
unchanged (same pads). Tracker Mini (no pads) is out of scope for now. The drum engine is dropped (owner 2026-11-03).

## Idea
Every track can pick a **sequencer type**. The sequencer is the owner's iii / grid-composer sequencers (from the
groovebox `lnx-groovebox` Lua scripts), ported to C++. A track's sequencer can be used in two ways:

- **Record**: play the sequencer, and what it plays is written into the track's tracker steps (note, volume,
  micromove, roll). The result is plain tracker data: editable on the pattern page, saved in the normal `.mtp`,
  playable on stock firmware.
- **Live**: the sequencer drives the track in real time, without writing the pattern ("take control live"). Leaving
  Live returns the track to its pattern.

You perform a sequencer on the **Seq page**: it always shows the track the pattern-editor cursor is on, so moving to
another track takes over that track's sequencer.

## Sequencer types (first set, from the groovebox scripts)
| Type | From | Pads (left 8 × 4) |
|---|---|---|
| Off | — | normal tracker track |
| Steps | `grid_seq.lua` | 4 drum rows × 8 steps; tap toggles; hold steps = ratchet/stutter (leftmost held = anchor, spread = rate {6,12,8,6,4,3} pulses); chance, div |
| Euclid | `euclid_trigger.lua` | 4 rows; keys 1–7 = E(k,8) live; hold key 8 = bake into the chain (up to 8 × 8 steps); swing, chance, velocity |
| Follower | `step_follower.lua` | scale-degree piano roll, 4 pitch rows, page up/down by 2 degrees, follows the project scale |
| Chords / Arp | `chord_arp.lua` (later) | roots, chord types, groove masks (`glib`), velocity groove `vgv()` |

Drum-row sequencers (Steps, Euclid) map their 4 rows to 4 notes on the track (per-row note parameter), or — later —
to 4 tracks (kit grouping as in euclid_trigger).

## Seq page (pads)
- Columns 1–8: the sequencer's own 8 × 4 grid, exactly as in the groovebox (same gestures, same LED roles).
- Columns 9–12 (controls, same on every sequencer):
  - row 1: sequencer type (cycle) · Off / **Live** / **Rec** for this track
  - row 2: track − / track + (moves the shared track cursor)
  - row 3–4: sequencer parameter pages / sequencer-specific keys (e.g. Follower page up/down)
- Screen: track name, type, mode, the 4 parameters of the current page on the 4 encoders' positions; the generated
  steps scrolling like the pattern page, so Record shows what lands in the pattern.
- Page lives in `Interface::modules[11]` (the free slot), reached by a button combo (to decide on hardware; no new
  letter hotkeys elsewhere).

## Engine design
1. **`seqgen/` — pure C++ library, the owner's code.** No Polyend includes, no heap, fixed arrays, float-free where
   possible. Each sequencer implements:
   `pad(x, y, z)`, `tick(pulse)` at 24 PPQN → events `{note, velocity, rowMask, ratchetRate, offset}`,
   `leds(buf)`, `param(i, v)`, `serialize/deserialize`. Own license chosen by the owner (it can be reused in the
   groovebox); only the glue inside Polyend files is PolyForm Noncommercial.
   A **host test harness** (desktop g++ + a tiny test runner) replays pad gestures and clocks and checks the events,
   because the Tracker has no simulator.
2. **Glue in the firmware (Polyend files, kept small):**
   - per-track `SeqTrackState { type, mode, generator state }` (16 tracks), saved in a project sidecar
     `seqtracks.bin` with a version byte — the pattern struct and `.mtp` format stay untouched (ABI-locked by
     `static_assert`).
   - clock: the sequencer µstep (48 per step, 4 steps per beat = 192 PPQN) feeds `tick()` every 8 µsteps (24 PPQN).
   - **Live**: in `play_microStep` (`core/source/mtSequencer.cpp` ~280) a Live track ignores its pattern step and
     sends the generator's events; ratchets trigger at µstep resolution through the existing note path.
   - **Record**: same events, also written into `currentPattern` at the quantised step (`getDestRecordingStep`
     rules), ratchets as `FX_TYPE_ROLL`, off-grid timing as `micromove`, velocity as `volume`; one undo step per
     record pass; the pattern is marked changed and saved by the main loop (never SD from audio context).
   - everything in the audio callback stays O(µs): no allocation, no SD, no UI.
3. **Pad velocity** for the existing live recording (pads currently always send 127, `fPatternEdit.cpp:4683`).

## Phases
- **P0 Build check**: `make original` builds on this PC; note the free flash/RAM (MK66 has ~153 KB flash left).
  Owner flashes the unchanged CE build once to confirm the toolchain output works.
- **P1 seqgen + host tests**: Steps (grid_seq) ported 1:1 from Lua with tests for every gesture. No hardware needed.
- **P2 Live**: SeqTrackState, the µstep hook, Seq page with Steps; take over by moving the track cursor.
- **P3 Record** into the pattern + undo + sidecar save/load.
- **P4** Euclid and Follower; pad velocity.
- **P5** Chords/Arp (chord_arp), kit grouping across tracks, Tracker+ check.

Each phase: build for `original` (and `plus` to keep it compiling), host tests green, a short on-device test list
for the owner (what to press, what should happen), and a `.ptf` in `build/`.

## Open questions (provisional defaults in brackets)
- Per track or per pattern sequencer state? [per track, per project; pattern-chained later]
- Drum-row sequencers on one track (4 notes) or across 4 tracks? [one track first]
- Record: replace or overdub the track's steps? [replace the steps the sequencer plays over, during the pass]
- Button combo to open the Seq page. [decide on hardware]
- License for `seqgen/`. [owner decides; MIT would allow reuse everywhere]
