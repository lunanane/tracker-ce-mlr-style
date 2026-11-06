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

## Owner refinement of the pad UI (2026-11-06, later)
Same layout as the fader page (plans 004 / 005 R3): four horizontal 8-step faders on rows 1-4, columns 1-8, page pads in columns 9-10, LATCH / DISENGAGE style pads
in columns 11-12; pages like the fader page groups. Page REPEAT: row 1 = grid as a momentary fader (each of the 8 steps a grid, hold = repeat), row 2 GATE, row 3 DECAY,
row 4 PITCH DECAY; page AUTO: INTERVAL, CHANCE, VARIATION, (the spare row = the grid of the automatic repeats); the output mode is MIX by default with a REPLACE toggle (INSERT:
the original is muted while repeating), GATE optional. The toggle into the view stays the CUT page's screen button 4 "BeatRpt". This **supersedes the provisional pad layout above**
(rows of grid pads / triplet pads / gate steps / decay-pitch-filter); the Filter is not built (not in the owner's spec 2.3 parameter list).

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

## As built (v0.4, branch v040-br from seq-tracks 3691a2e; Original Tracker only, `make original`)
**Audio.** `core/libs/Audio/beat_repeat_core.h` (header only, host tested) is the DSP, `effect_beatRepeat.{h,cpp}` the stereo `AudioStream` wrapper. It is a member of the audio engine
declared before `AudioOutput`, so it updates after the mixers and before the limiter: tracks + sends mixer (reverb / delay returns included) -> beat repeat -> limiter
(`connectMixerToOutput`, `FEATURE_BEAT_REPEAT`, defined with `FEATURE_PLINKY` for MK66; Tracker+ / Mini are unchanged). Idle (no pad held, chance 0, no event, audio gain settled) the
wrapper passes the two blocks on untouched (`bypassable()`): no capture, no copy, no clock read. Exports run the same stage (a repeat in progress is in the master stem).
- **Clock**: integer ticks, 192 per beat = 48 per step (one uStep). `Sequencer::getBeatClock` gives the transport position (global position * 48 + uStep, fraction of the next
  uStep); `tick()` has run to the end of the block's interval before the audio blocks update, so the glue (`seqBeatRepeat.cpp`) steps one block back. Inside a block the stage advances the tick by the
  tempo per frame; a tick fires at the first frame whose start position has reached it, so a trigger and every slice start lands on the exact frame of the clock (host test: the repeat starts
  equal the model at 97.3 / 120 / 211.7 BPM within one frame, which is the float rounding of the sub tick fraction; the stock sequencer itself starts notes at the start of the block, <= 2.9 ms). A
  clock jump larger than 3 ticks (pattern wrap, new Play) is taken silently, a smaller catch up fires the skipped ticks, a clock behind is ignored; the very first block only sets the position.
  Transport stopped: the stage free-runs on the tempo, pads still work. Tempo 30 .. 300 BPM.
- **Events**: a repeat is triggered by a momentary pad (at the next boundary of the pad's grid) or automatically (every INTERVAL with probability CHANCE, from the interval boundary).
  From the trigger the slice (one grid) is captured while the audio passes; at the next boundary the repeats start, one per grid, each starting at a boundary of the clock. A pad press while
  another repeat runs stops that repeat (32 frame fade) and waits for its own boundary (jam). Automatic events last `max(GATE, 2 grids)` from the trigger (GATE 1/16 .. 16/16 in sixteenths); the
  automatic trigger only fires while idle. GATE / variation: with probability v/10 the automatic grid moves up to 1 + v/4 positions (in the order of the grid lengths) up or down.
- **Per repeat k (0 = the first)**: level `0.5012^(DECAY/100 * k)` (100 = -6 dB), pitch `-3 st * PITCHDECAY/100 * k` (floor -24 st), a linear-interpolated resampled read of the slice
  for the grid length (the rhythm stays on the grid, a lowered repeat plays only the first part of the slice).
- **Modes**: MIX (default), INSERT ("REPLACE": the audio is muted while repeats sound, the captured slice passes), GATE (muted while chance > 0, a pad is held or an event runs, also while capturing).
- **Click freedom**: every repeat has a 32 frame fade in and out (0 at both ends), the audio gain moves 1/64 per frame, a repeat cut by the next slice start or by the event end fades out
  over 32 frames (two voices). The slice is shorter than the grid when the tempo dropped: the last frame (0 after the fade) is held until the next slice start (the spec's "hold last frame").
- **PRNG**: xorshift32, seed constant at boot (deterministic, the test replicates it).
- **Memory**: the slice ring is `s_ring[2 * 32768]` int16 (128 KiB, interleaved stereo) in `__NOINIT(EXTERNAL_RAM)` raw storage (no constructor; `check_sdram_ctors.sh` passes), attached in
  `audioEngine::init`. **Not** taken from `SAMPLE_MEMORY_SIZE` (unchanged). Build numbers (`make original`): FLASH 973,512 B (+14.6 KB, 92.84 %), RAM 226,652 B (+608 B, 86.46 %),
  EXTERNAL_RAM 16,617,736 B (+131,072, 99.05 %; **159,480 B (156 KB) free**, the PolySynth x3 branch needs about 70 KB), image end 0xEECBC < 0xFC000. The ring size sets the longest grid per tempo:
  1/4 down to 81 BPM, 1/4T to 54, 1/8 to 40, 1/8T to 27, 1/16 below (`clampGrid`; the screen shows "grid shortened for this tempo"). A bigger ring (e.g. 44117 frames = 172 KB for 1/4 down to 60 BPM)
  does not fit next to the PolySynth branch; if the owner wants it, the sample pool or another buffer has to give the space (reported, not done).
- **CPU (estimate, M4 at 180 MHz, block = 522 k cycles)**: capture 2 SDRAM stores per frame, about 1.5 %; repeating with a resampled read 4 SDRAM reads per frame, about 3 .. 5 %; idle 0 (bypass).
  The stage measures itself with the DWT cycle counter (like Warp) and the BeatRpt screen shows "CPU n.n %".
**UI.** `seqBeatRepeatLogic.h` (pad map, values, controller, LEDs, legend words, screen model, sidecar `BrParams`), `brDraw.h` (screen + legend, templates like `mlrDraw.h`), `cutLayer.cpp` /
`cutSessionLayer.h` (the layer switch, button 4 "BeatRpt", pad routing, panic, leaving), `laneView.cpp` + `seqMlrLogic.h` / `mlrDraw.h` (the model and the drawing; `MlrModel::brMode`),
`seqBeatRepeat.{h,cpp}` (glue, commands to the audio side through a 16 entry SPSC ring, screen model with the slice waveform, the sidecar).
- Pads: columns 1-8 four 8-step faders; column 9 rows 1-2 page REPEAT / AUTO; column 10 rows 1-3 output MIX / REPLACE / GATE; column 11 row 1 LATCH; column 12 row 1 OFF (= DISENGAGE, ends any repeat),
  rows 2-4 RESET of that row's value. Values per step 1..8: GATE 1/16 .. 16/16; DECAY / PITCH DECAY / CHANCE 0 14 29 43 57 71 86 100 %; VARIATION 0 1 3 4 6 7 9 10; INTERVAL and AUTO GRID are selects.
  Jam: the newest held grid pad rules; letting go of it falls back to the one still held, an older pad let go changes nothing.
- Screen (tracker colours, `seqPalette.h`): header (title, page chips, mode chip, state "idle / waiting / capturing / REPEAT 1/16", repeat number, level, pitch), the four bands (name, value, the 8 pad cells as lit),
  LATCH / OFF / RESET cells, the captured slice as a waveform with the live playhead (red while capturing, white while repeating), the repeats as 8 blocks (height = decay curve, colour teal to purple = pitch
  decay, the running repeat highlighted), BPM and the stage CPU. Help shows the 12 x 4 legend. Stage budgets (host, FIFO bytes): scene 0 2096, busiest 2740 of 3840, one stage; legend 2632; whole frame <= 5.3 KB display list.
- Sidecar `seqbrpt.bin` (magic `SBRP`, version 1, 8 byte payload: auto grid, interval, chance, gate, variation, pitch decay, decay steps + mode), deferred write after Stop like `seqwarp.bin`; a project with all defaults has no file;
  short / long / out-of-range payloads read as far as known; a newer file is never rewritten (oldest-version rule: version 1 is the only layout, a later layout appends bytes). Latch, page and a running repeat are not saved.
**Tests** (host, `make test`): `test_beatrepeat.cpp` (new, 86,823 checks) and `test_br.inc` in the glue tests (new). One existing check changed as a direct consequence, equally strict: `test_cut.inc` asserted that screen button 4
was empty (`lab[0] == 0`); it now asserts the label `BeatRpt`. Everything else unchanged. Previews: `make preview` also writes `br0..br4.png` (idle, AUTO with a running repeat, REPLACE page, legends).
**Not done / not device-tested**: the filter (LP/HP on the repeats; not in the owner's spec parameter list); everything on hardware (listening, CPU readout, the SDRAM timing of the ring); a ring for 1/4 below 81 BPM.
