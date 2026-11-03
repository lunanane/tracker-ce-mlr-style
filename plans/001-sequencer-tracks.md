# Plan 001 — Sequencer tracks

Status: DRAFT (2026-11-03). Target hardware: **original Tracker (MK66, 48 pads = 12 columns × 4 rows)**; Tracker+
should work unchanged (same pads). Tracker Mini (no pads) is out of scope for now. The drum engine is dropped
(owner 2026-11-03).

## Idea
Every track can pick a **sequencer type**. The sequencers are the owner's grid-composer pages (monome iii,
`D:\OneDrive\CodeLibraries\iii\monome_iii_c31.lua`) and the groovebox scripts (`lnx-groovebox`), ported to C++.
A track's sequencer is used in two ways:

- **Record**: play the sequencer, and what it plays is written into the track's tracker steps (note, volume,
  micromove, roll). The result is plain tracker data: editable on the pattern page, saved in the normal `.mtp`,
  playable on stock firmware.
- **Live**: the sequencer drives the track in real time without writing the pattern ("take control live").
  Leaving Live returns the track to its pattern.

You perform a sequencer on the **Seq page**: it always shows the track the pattern-editor cursor is on, so moving to
another track takes over that track's sequencer. **The pads belong entirely to the sequencer**; track, type and
Off/Live/Rec are on the screen, the encoders and the existing buttons (owner 2026-11-03: the Tracker is the tracker,
so the grid-composer arranger/tracker part is dropped).

## Sequencer types
| Type | From | Size of the port |
|---|---|---|
| Off | — | normal tracker track |
| **Chord / Arp** | c31 page 3 without the arranger (lines 237–708 minus ~100 tracker lines) | ~370 Lua lines |
| **Mono** | c31 page 4 (lines 711–841); three mono sequencers = three tracks set to Mono | ~150 Lua lines |
| Steps | groovebox `grid_seq.lua` (hold steps = ratchet/stutter) | later |
| Euclid | groovebox `euclid_trigger.lua` (E(k,8), hold key 8 = bake) | later |

Shared, as in c31: the **scale** (`scale_set`: lock, else held root, else C major) and the **velocity groove**
`vgv()` (parametric groove when PON is on) come from the Chord/Arp track and are read by Mono tracks. Without a
Chord/Arp track: C major and flat velocity.

## Chord / Arp page (owner chose "roots turned + panel", 2026-11-03)
```
      1    2    3    4    5    6  | 7   8   9   10 | 11      12
r1    E    A    D    G    C    F  |                | TYPES   SETTINGS
r2    C#m  F#m  Bm   Em   Am   Dm |   4×4 PANEL    | GROOVE  PARAM
r3    B    F#   C#   Ab   Eb   Bb |                | INV−    INV+
r4    G#m  D#m  A#m  Fm   Cm   Gm |                | OCT−    OCT+
```
- **Roots (cols 1–6)**: the 24 Camelot roots of c31 (`cam`) turned sideways: rows 1 + 3 major, rows 2 + 4 minor,
  columns along the circle of fifths. Hold = chord sounds (no latch), release = off, exactly as c31. Hold-LCK combo:
  LCK is a screen/encoder action, or hold the root + press an LCK button (decide on hardware).
  In **grid play mode** (c31 pmode 3) the 24 pads are the scale-degree note grid `(col, row)` → degree, polyphonic
  set `cpool3`.
- **Panel (cols 7–10)**, chosen with the TYPES / SETTINGS / GROOVE / PARAM pads (re-tap PARAM = PON on/off):
  - TYPES: the 8 diatonic types + the 7 extended types (dom7, maj7, dim, aug, 7b5, 7#5, add-b9) as direct pads —
    no M1 combo needed; one pad spare.
  - SETTINGS: rows T (chord / arp / arp+oct / off), S (1/8, 1/8T, 1/16, 1/16T), D (up / down / up-down / pattern),
    Z (size 5/4/3/2) — 1:1 the c31 block x5–8 y1–4, queued to the next bar.
  - GROOVE: the 16 `glib` gate masks — 1:1 c31 x5–8 y5–8.
  - PARAM: tier weights downbeat / 8th / 16th + archetype row (re-tap = reroll seed) — 1:1 c31 x13–16 y1–4.
- **INV− / INV+** (both = reset, range ±6), **OCT− / OCT+** (both = 0).
- **Screen + encoders**: play mode (key / locked / grid), key lock root, swing 0/1/2, gate (normal / short / legato),
  current chord, queued changes, the groove steps.
- LED levels and pulses as in c31 (selected 15, queued pulses, locked tonic blinks …), mapped to the pad RGB.
- Timing rules unchanged: root/type changes on the next quarter (immediate when type = chord and speed 1),
  settings/groove on the next bar; `gstep` derives the step from the bar position (no drift).

## Mono page (owner chose "8 steps + controls", 2026-11-03)
```
      1  2  3  4  5  6  7  8 | 9     10    11    12
r1    degree window +3       | HALF  PAGE− PAGE+ CLEAR
r2    degree window +2       | TIE   SPD−  SPD+  —
r3    degree window +1       | WIN+  LOOP  —     —
r4    degree window +0       | WIN−  —     —     —
```
- Steps 1–8 / 9–16 of the current 16-step page, flipped with HALF; the screen shows all 16 steps and the pages.
- Tap = toggle the note (scale degree) at that step; LOOP + start step + end step sets the loop (c31: hold start pad
  + end pad on row 8).
- WIN± scrolls the 4-degree window (c31 showed 7). SPD± walks the c31 `SPD` table {96 … 1} pulses per step
  (default 1/16). PAGE± edit page; pages 1–8 chain (`np`); TIE holds the same resolved note across steps; CLEAR
  resets the sequence.
- Engine 1:1 c31: note = `scale_deg(d + tpose + gtpose)`, velocity `vgv(pos, 100)`, previous note held until the next
  step, a rest kills it.

## Engine design
1. **`seqgen/` — pure C++ library, the owner's code.** No Polyend includes, no heap, fixed arrays. Each sequencer:
   `pad(col, row, down)`, `pulse()` at 24 PPQN, `start()/stop()`, events `{note, velocity, gateTicks, offset}`,
   `leds(buf[48])`, `param(i, v)`, `serialize/deserialize`. Shared `SeqContext {scale, vgv}`. Own license chosen by
   the owner (reusable in the groovebox); only the glue inside Polyend files is PolyForm Noncommercial.
   A **host test harness** (desktop g++) replays pad gestures and MIDI-clock pulses and compares the note events
   with the c31 Lua behaviour, because the Tracker has no simulator.
2. **Glue in the firmware (Polyend files, kept small):**
   - per-track `SeqTrackState { type, mode, generator state }` (16 tracks), saved in a project sidecar
     `seqtracks.bin` with a version byte; the pattern struct and `.mtp` stay untouched (ABI-locked).
   - clock: the sequencer µstep (48 per step, 192 PPQN) calls `pulse()` every 8 µsteps (24 PPQN).
   - **Live**: in `play_microStep` (`core/source/mtSequencer.cpp` ~280) a Live track ignores its pattern step and
     plays the generator's events through the normal note path (instrument of the track).
   - **Record**: the same events are also written into `currentPattern` at the quantised step
     (`getDestRecordingStep` rules), velocity → `volume`, off-grid timing → `micromove`; one undo step per pass;
     saved by the main loop (never SD from audio context).
   - Audio-callback work stays O(µs): no allocation, no SD, no UI.
3. **Pad velocity** for the existing live recording (pads always send 127, `fPatternEdit.cpp:4683`).

## Recording chords (a tracker track plays one note per step)
- Arp → one note per step on the track: fits directly.
- **Chord mode → the lowest note + `FX_TYPE_MIDI_CHORD`** (owner 2026-11-03). The Tracker's chord FX
  (`core/source/mtChordDefs.h`) is a table of 29 interval bitmasks (at most 4 notes within 15 semitones of the bass
  note) and is played only on **MIDI tracks** (`Sequencer::sendMidiChordOn`, `mtSequencer.cpp:2344`).
  - The glue (firmware side, not seqgen) turns the sounding chord into intervals above its lowest note and looks the
    bitmask up in `chordDef`. Exact match → note + chord FX.
  - No exact match (size-5 chords, wide inversions, extended types beyond the table): drop the highest notes until
    it matches, and show "chord reduced" on the screen. [provisional; owner may prefer spreading over tracks]
  - Chord mode Record on a sample track: only the lowest note is written, with a screen hint "chords need a MIDI
    track". [provisional]
- Live mode has no such limit: it sends all chord notes.

## Phases
- **P0 Build check**: `make original` builds on this PC (needs the owner's OK: it runs downloaded code); note free
  flash/RAM (MK66 has ~153 KB flash left). Owner flashes the unchanged CE build once to confirm the toolchain.
- **P1 seqgen + host tests**: Mono and Chord/Arp ported from c31, tests against the Lua behaviour. No hardware.
- **P2 Live**: SeqTrackState, the µstep hook, Seq page with Mono and Chord/Arp; take over by moving the track cursor.
- **P3 Record** into the pattern + undo + sidecar save/load.
- **P4** Steps and Euclid; pad velocity.
- **P5** Tracker+ check; kit grouping across tracks.

Each phase: build for `original` (and `plus` to keep it compiling), host tests green, a short on-device test list
for the owner (what to press, what should happen), and a `.ptf` in `build/`.

## Open questions (provisional defaults in brackets)
- Per track or per pattern sequencer state? [per track, per project; pattern-chained later]
- Record: replace or overdub? [replace the steps the sequencer plays over, during the pass]
- Button combo to open the Seq page, LCK gesture. [decide on hardware]
- License for `seqgen/`. [owner decides; MIT would allow reuse everywhere]
