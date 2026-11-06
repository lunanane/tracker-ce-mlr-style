# Plan 021 — Ableton-style Beat Repeat, jammable from the CUT / MLR page (owner 2026-11-06)

Owner: "One more important feature: Ableton-style beat repeat that's directly jammable on the CUT page. We implemented
that in the Model:Samples repo as well. Basically a toggle in MLR/CUT, then it switches to beat-repeat control. It is
basically a send effect and gets mixed in; we want it as a master effect but do the chopping from that page — a
convenient toggle when chopping up anyway." (The Model:Samples repo is private and not on this PC — build from scratch.)

## Effect (master insert)
- Position: on the master bus before the limiter (after the reverb/delay returns), stereo. Off = zero cost (bypass).
- Capture: a rolling stereo buffer of the master input (≥ 1 bar at the slowest supported tempo, e.g. 4 s;
  EXTERNAL_RAM raw storage — take the space from SAMPLE_MEMORY_SIZE, document; never a static constructor in
  EXTERNAL_RAM).
- **Instant jam**: on press, the repeat slice is the audio from the **most recent grid boundary** of the chosen grid
  (so the response is immediate, no waiting); the slice loops while the pad is held; release returns to the live
  signal (short crossfades, no clicks). Grid boundaries from the sequencer's global position.
- Parameters (Ableton-like, kept simple): **Grid** (1/4, 1/8, 1/16, 1/32 + triplets 1/4T 1/8T 1/16T 1/32T),
  **Gate** (how much of each repeat sounds), **Decay** (level drop per repeat), **Pitch** (pitch down per repeat,
  tape-ish, resampling), **Filter** (LP/HP sweep on the repeats), **Mix mode**: Insert (repeats replace the signal)
  / Mix (repeats on top), **Latch** (keeps repeating after release until pressed again or Stop).
- Stop = panic ends it.

## UI on the CUT / MLR page
- A clearly labelled screen button (e.g. the free button 4: "BeatRpt") toggles the pad grid between MLR and
  Beat-Repeat control; MLR keeps playing underneath; the lane screen shows the beat-repeat state (captured buffer
  waveform, current slice, grid, repeat count, decay/pitch curve) in tracker colours.
- Pad layout (12 x 4), provisional:
  row 1: grid pads 1/4 1/8 1/16 1/32 (hold = repeat at that grid, jam between them while held)
  row 2: triplet grid pads 1/4T 1/8T 1/16T 1/32T
  rows 1-2 cols 9-12: Latch, Mix mode, Insert/Mix, Off
  row 3: Gate steps (8 pads, 1/8 … 8/8)
  row 4: Decay (cols 1-4), Pitch (cols 5-8), Filter LP/HP (cols 9-12)
- Help legend (screen button Help) covers the beat-repeat layout too.
- Settings saved with the project (sidecar, oldest-version rule).

## Limits
CPU: one stereo buffer write per block + repeat read with optional resampling/filter — measure (readout).
Display: keep every stage ≤ 3840 FIFO bytes (seqTraceStages / fifostub tests).

## Owner decisions + reference (2026-11-06)
- Reference: the owner's Model:Samples spec `port/mfx2/SPEC.md` §2.3 / §7 "Beat repeat" in github.com/lunanane/custom-modelsamples
  (private; read-only clone in the session scratchpad `custom_modelsamples`): INTERVAL (1/32 … 4 bars), CHANCE, GRID,
  GATE, VARIATION (v/10 chance of a grid up to 1 + v/4 positions away), PITCH DECAY (100 = −3 semitones per repeat,
  floor −2 oct, resampled read, rhythm stays on grid), DECAY (100 = −6 dB per repeat), modes MIX / INSERT / GATE,
  momentary pads force a repeat at the pad's grid while held; trigger and slice starts sample-exact to the clock.
- **Capture on press (as the spec)**: no continuous recording; the slice is captured when the repeat triggers (audio
  passes meanwhile) and repeated from then on — zero cost while idle; ring = one slice (2 × 131072 stereo frames, tempo
  clamped 30 … 300 BPM) → supersedes "instant from the past" above.
- **Default mode MIX** (repeats on top of the running audio); INSERT and GATE selectable on a pad.
- Master insert as planned; the CUT/MLR page toggle (screen button 4 "BeatRpt") switches the pads to beat-repeat
  control; the spec's knob params (interval, chance, variation, gate, decay, pitch decay) on pads/rows as in this plan.
