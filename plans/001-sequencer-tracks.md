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
  Always on while you play it; when you stop, the track's pattern comes back instantly.

You perform a sequencer in **Play mode** (tap Pattern again on the Pattern page, see Workflow): the screen buttons
jump to a track, its sequencer takes over the track while you play it, Rec punches in. **The pads belong entirely
to the sequencer**; track and type are on the screen buttons, Instr + jog wheel and the existing buttons (owner 2026-11-03: the Tracker is the tracker,
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
   - per-track `SeqTrackState { type, takenOver, one generator state per type }` (16 tracks), saved in a project sidecar
     `seqtracks.bin` with a version byte; the pattern struct and `.mtp` stay untouched (ABI-locked).
   - clock: the sequencer µstep (48 per step, 192 PPQN) calls `pulse()` every 8 µsteps (24 PPQN).
   - **Live / takeover**: in `play_microStep` (`core/source/mtSequencer.cpp` ~280) a taken-over track ignores its
     pattern step and plays the generator's events through the normal note path (instrument of the track); when
     the takeover ends, the pattern plays again from the current step.
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

## Workflow: Play mode (owner 2026-11-04, replaces the Seq-page proposal)
Built around immediate playability; no separate Seq page, no Off/Live/Rec modes.

- **Pattern** enters the Pattern page as now. **Pressing Pattern again** (a tap with no other button) toggles
  between the normal pattern editor and **Play mode**; every further tap toggles again. Pattern held + arrows etc.
  keep their current meaning (a tap is only a press + release with nothing else pressed in between —
  `functPattern`, `fPatternEdit.cpp:2843`, today only resets the view mode on release).
- **Jump to a track**: the 8 buttons under the screen jump straight to tracks 1–8, **Shift +** a screen button to
  tracks 9–16. The pads become that track's sequencer at once; all other tracks keep running.
- **Pick the sequencer type**: hold **Instr** and turn the **jog wheel**. A list (Off / Chord-Arp / Mono / Steps /
  Euclid) opens on screen in that track's column, "slotting in" the selection; releasing Instr applies it. While
  the transport runs, a change takes effect on the next bar. Every track keeps one state per type, so switching
  away and back restores the previous sequence (~1 KB per track, saved with the project).
- **Live is always on**: while you play a track's sequencer it **takes over** the track — the track's pattern
  notes are muted and the sequencer plays through the track's instrument. When you stop playing, the pattern comes
  back **instantly**. Nothing is written in Live.
  [provisional "stop playing": Chord/Arp — all chord pads released (and not locked); Mono — sequence cleared or
  stopped; to be tuned on hardware]
- **Rec** (in Play mode): **instant punch-in** — recording starts immediately at the current position and the
  sequencer's notes **replace** the steps it passes on that track (overdub over the pattern, not layered on top).
  Rec again ends it. One undo step.
- **Shift + Rec**: records **exactly one loop** — waits for the loop start, records until the loop end, stops by
  itself. One undo step.
- Both use whatever sequencer the track has selected. Chord recording follows "Recording chords" above.
- Screen in Play mode: the track columns with their type letter (C/M/S/E/–) and a marker on the track that is
  currently taken over; the 8 screen-button labels show the track numbers (1–8, or 9–16 while Shift is held);
  below, the current sequencer's parameters (jog wheel / encoder).
- Copy / Paste in Play mode copy a track's sequencer (type + state) to another track.
- No new letter hotkeys, only combinations of existing buttons.

## Script sequencers — parked (owner 2026-11-03)
The owner decided against an embedded script engine on the original Tracker (flash too tight): all sequencers are
built in (C++). The notes below stay for a possible Tracker+ version.

## Script sequencers (Lua from the SD card, owner 2026-11-03)
- Scripts live on the SD card in `/Scripts/*.lua`. A track's **sequencer type list** shows the built-ins (Chord/Arp,
  Mono, Steps, Euclid) and every script found there; picking a script loads it **for that track** (each script track
  has its own Lua state, so the same script can run on several tracks).
- **Tracker script API v1** (iii-like, so iii scripts port easily): callbacks `init()`, `pad(x, y, z)` (12 × 4),
  `pulse()` (24 PPQN), `start()` / `stop()`, `param(i, v)`; calls `note_on(note, vel, gate)`, `note_off(note)`,
  `led(x, y, level)`, `grid_size()`, `scale()` / `vgv()` (the shared SeqContext), `save()` / `load(str)` for the
  per-track state stored in the project sidecar.
- Live / Record work exactly as for the built-ins: the glue takes the script's notes.
- **Timing**: Lua never runs in the audio callback (garbage collection pauses). Scripts run in the main loop one
  pulse ahead; their notes are queued with a timestamp and played by the audio-side sequencer on time.
- Limits: a heap cap per script in external RAM, a time budget per pulse (a script that overruns is stopped with a
  screen message, the track falls back to its pattern), a cap on script tracks (e.g. 4) on the original Tracker.
- **Spike first (P-S0)**: compile a minimal Lua 5.4 (core + table/math/string, no io/os) for the MK66 and measure
  flash (≈ 100–130 KB estimated vs ~153 KB free) and the time per pulse of a c34-sized script. If it does not fit
  the original Tracker: a smaller Lua build, or scripts on Tracker+ only. Needs the owner's OK to download the Lua
  sources.

## Phases
- **P0 Build check**: `make original` builds on this PC (needs the owner's OK: it runs downloaded code); note free
  flash/RAM (MK66 has ~153 KB flash left). Owner flashes the unchanged CE build once to confirm the toolchain.
- **P1 seqgen + host tests**: Mono and Chord/Arp ported from c31, tests against the Lua behaviour. No hardware.
- **P2 Live**: SeqTrackState, the µstep hook, Play mode (Pattern tap toggle, screen buttons jump to tracks,
  Instr + jog wheel type list) with Mono and Chord/Arp; takeover while playing.
- **P3 Record**: Rec punch-in (replace) and Shift + Rec one-loop record + undo + sidecar save/load.
- **P4** Steps and Euclid; pad velocity.
- **P5** Tracker+ check; kit grouping across tracks.
- ~~P6 Script sequencers~~ — parked (owner 2026-11-03: built in only).

Each phase: build for `original` (and `plus` to keep it compiling), host tests green, a short on-device test list
for the owner (what to press, what should happen), and a `.ptf` in `build/`.

## Open questions (provisional defaults in brackets)
- Per track or per pattern sequencer state? [per track, per project; pattern-chained later]
- Record: replace or overdub? Decided (owner 2026-11-04): replace the steps it passes; Shift + Rec = one exact loop.
- When does a takeover end ("stop playing")? [Chord/Arp: pads released and not locked; Mono: cleared or stopped]
- Chord LCK gesture. [decide on hardware]
- License for `seqgen/`. [owner decides; MIT would allow reuse everywhere]
