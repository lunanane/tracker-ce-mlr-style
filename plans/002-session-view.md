# Plan 002 — Session view (MLR-style track retrigger)

Status: APPROVED (owner 2026-11-04: "lets definitely implement that"). Builds on plan 001's Play mode (same
Pattern-tap toggle family).

## Can tracks run separately? (firmware findings)
- **One transport, one clock**: Play/Stop, tempo and the 48-µstep clock are global (`Sequencer::tick_uStep`,
  `mtSequencer.cpp:176`); every µstep all 16 tracks are advanced in a loop. Swing flips on the last track's step
  (`switchRowStep`, ~1705).
- **But every track has its own playhead**: `strPlayerTrack` (`mtSequencer.h:429`) holds its own `uStep`,
  `actual_pos`, `actual_pos_absolute`, `passCount`. Per-track step switching is `inc_uStep(row)` →
  `switchRowStep(row)`.
- **Performance mode already desyncs tracks**: per-track length (`performance.trackLength[]`), per-track play
  direction (forward/backward/random/custom, `performancePlayMode`), per-track pattern swap
  (`setTrackToLoadOnSwitch/Now`, `switchPerformanceTrackNow` ~3385), re-alignment (`alignToGlobalPos`,
  `reset_actual_pos(row, delta)`).
- So: **independent playheads = yes; independent start/stop and tempo = not built in**, but small glue can add
  them (a per-track "held" flag that skips `play_microStep`/`inc_uStep`; per-track rate by advancing `uStep` by
  2 or every 2nd tick).

## Idea
Another toggle page (Pattern tap cycle: Editor → Play → Session, provisional) where pads retrigger tracks
MLR-style:
- 4 rows = 4 tracks (screen buttons pick the group: 1–4, 5–8, … 13–16), 8 or 12 columns = positions in the track
  (track length / columns).
- Press a pad → that track's playhead jumps there (quantised to the next step, or 1/16 / beat — setting).
- Hold two pads in a row → loop that range; release → back to the full track.
- Row controls (if 8 position columns): stop/start track, reverse, rate ½ / 2×, re-sync to global.
- Plays the pattern data (and plan 001 sequencers). Playhead recording: see below.

## Playhead recording (owner 2026-11-04)
- After jamming position jumps on a row, **hold Rec while jamming**: the jumps are recorded (quantised, per
  step). **Releasing Rec** closes the loop: the track keeps playing that recorded playhead sequence, over and over.
- It keeps running when you switch to another group, and while you are on other pages. A new press on that row
  overrides it live; [provisional: a two-pad loop gesture or "resync" clears the recorded sequence].
- Storage: per track a table "position for each step of the recorded loop" (≤ 256 steps × 1 byte), ~4 KB for 16
  tracks; saved in the project sidecar with plan 001's state. Pattern data is never touched.

## What a jump does (works for drum patterns and for long loops)
A jump to position X re-plays the track **as if its playhead had been at X**:
1. the pattern playhead of that track is set to X (`actual_pos`, `uStep = 1`, quantised);
2. the last note at or before X on that track is re-triggered with a **sample offset** = time from that note's step
   to X (same mechanism as the Position FX). So a single long loop sample on step 1 is cut MLR-style, and a drum
   pattern simply jumps.

## Long samples / SD streaming (CE changelog item 6, `VoiceStreamManager`)
- CE streams samples ≥ 256 KB from the SD card; only the first 32,768 frames (~743 ms) are pre-cached in SDRAM.
- A jump **past the pre-cache** starts with an empty ring buffer that the main loop refills
  (`VoiceStreamManager.cpp` ~893); on the original Tracker the main loop can be busy 20–35 ms per step with
  redraws → audible late start / gap.
- Plan: a **cut cache** — when a track is in Session view, pre-fetch a short window (e.g. 4,096 frames) at every
  column's start point into SDRAM, like the slice pre-cache on Tracker+ (`refreshInstrumentSliceCache`). 8 or 12
  windows × 4 tracks × 8 KB ≈ 256–384 KB of the ~1.1 MB free SDRAM. Samples that fit in RAM (7.5 MB mono pool on
  the original) need nothing.

## Clip launcher — "like Ableton Live" (owner 2026-11-04)
The Session view gets two layers on the pads: **CLIPS** (launch pad, default) and **CUT** (the MLR layer above).
A clip = one track's part in one pattern; a scene = a whole pattern.
```
        1  2  3  4  5  6  7  8 |  9      10      11     12
  r1    clips: 8 tracks ×      |  SCENE  STOP-T  CLIPS  QUANT
  r2    4 patterns (window)    |  SCENE  —       CUT    —
  r3                           |  SCENE  —       —      —
  r4                           |  SCENE  STOP-A  —      —
```
- Columns 1–8 = tracks of the current group (screen buttons / Shift as elsewhere: tracks 1–8 or 9–16), rows =
  4 patterns; the **jog wheel scrolls** the pattern window.
- **Tap a clip** → that track plays its part from that pattern (queued, launched at the quantise point).
  LEDs: bright = playing, blinking = queued, dim = has notes, off = empty.
- **SCENE** (column 9) → launch the whole pattern (all tracks), stock pattern switch, quantised.
- **STOP-T** held + clip pad → stop that track (it goes silent until a clip is launched); **STOP-A** → stop all
  clips at the quantise point. [provisional]
- **CLIPS / CUT** switch the pad layer; CUT = the MLR page (jumps, two-pad loops, playhead recording) for the
  same group. Recorded playhead loops keep running in both layers.
- **QUANT** cycles launch quantise: next bar / next pattern end / immediate [provisional; default next bar].
- Screen: pattern numbers of the window, per-track "now playing pattern" and queued pattern.

### Firmware basis
- The stock Performance mode already plays a track from another pattern: `Sequencer::setTrackToLoadOnSwitch` /
  `setTrackToLoadNow` → `cFileManager::loadPerformanceTrackToBuffer` (reads that pattern file, copies one track
  into `fileManagerPerformancePatternBuffer`) → `switchPerformanceTrackNow` at the track's loop end.
- Gaps to fill: launch at a bar instead of the track's loop end; a per-track "stopped" state; the pattern file read
  is synchronous on the main loop (fine with quantised launch, must not happen in audio context); per-track
  lengths follow the source pattern's length. Check `kPerformanceTracks` covers all 16 tracks (MIDI tracks).
- Scene launch = stock pattern switch (`sequencialSwitch`) so behaviour matches the Pattern page.

## Open questions
- 8 columns + 4 control columns, or 12 position columns with controls on the screen buttons?
- Quantise default (next step / next beat).
- Does a retriggered track return to sync at pattern end, or stay offset until re-synced?

## Phases
- **S0 clip launcher** (CLIPS layer, on top of the Performance-mode track loading) — first, it reuses the most.
- **S1 library** (DONE 2026-11-04: `seqgen/playhead.*`, 24 host tests, 99 total green; press-over-recording rule = override while held, resume in phase, see seqgen/README "Playhead") (`seqgen/playhead.*`, host tests, no hardware): quantised jumps, two-pad loop ranges, playhead
  recording + looping, the "as if at X" note/offset lookup, rate ½/2×, reverse.
- **S2 firmware glue**: Session view page, per-track playhead override in `inc_uStep`/`switchRowStep`, retrigger
  with offset, sidecar save. Needs `make original` (owner OK).
- **S3 cut cache** for streamed samples on the original Tracker.

## S2 CUT layer as built (c2-mlr, 2026-11-04, provisional where marked)
- Code: `core/source/seqCut.*` (16 Playheads + per-track runtime, no Polyend includes), `seqCutLogic.h` (pure helpers: command packing,
  group/column mapping, Position FX value, LED fill), `core/interface/seqSession/cutLayer.*` (pad layer: `cutPad`, `cutLeds`,
  `cutSetGroup`, `cutRec`, `cutReleaseAll`), glue in `seqTracks.cpp` (`Sequencer::seqCut*`), hooks in `mtSequencer.cpp`. Tests: `core/seqtest`.
- Hooks (mtSequencer.cpp): `inc_uStep` -> `seqCutBoundary(row)` after `switchRowStep` (Playhead decides `actual_pos`); tick loop ->
  `seqCutMicroStep(row)` (rate 2: at uStep 25 the second half-step is played as a step of its own, uStep temporarily 1);
  `play_microStep`: `holding(row)` early return (stopped track / second half of a rate-1/2 position, same place as the takeover
  return), `seqCutRetrigger` after the step copy (replaces the step by the last note step before the position and adds the Position FX);
  `play()` -> `cutPlayStarted()` (restart the playheads at step 1 on the first tick, recorded loops kept).
- A track is "cut-active" from its first pad/Rec/STOP/REVERSE/RATE command; the playhead starts at the track's current position (no jump).
  RESYNC (and START after STOP) aligns to the global position at the next step and, when nothing special is left (rate 1, forward,
  no loop/recording), hands the track back to the stock sequencer.
- Retrigger: sample tracks get `FX_TYPE_POSITION` = elapsed steps * step time / sample length (percent, 0..100), added to a Position FX the
  note step already has; a free FX slot is used, else slot 2 is overwritten. Slice / beat-slice / granular / wavetable instruments and MIDI
  tracks: the note is replayed without offset. Jump to a position behind a note-off or on a track without notes: the sounding note stops.
- Threads: pads -> `Engine::postCut` -> ring (CMD_CUT) -> `Cut::apply` in `drain()`; every Playhead mutation is in audio context; views for the
  LEDs are 4-byte snapshots read by the main loop. Playheads (5.8 KB) live in `.noinit.$EXTERNAL_RAM`, constructed by `seqCutInit()`.
- Saving: **not in v0.1.** `seqtracks.bin` (version 1, fixed `kSidecarMax` read buffer) is left alone; a separate optional `seqcut.bin`
  (`Playhead::serialize`, 296 B x 16) can follow in v0.2.

### CUT layer review fixes (c2-mlr, 2026-11-04)
- Order inside a tick: `inc_uStepGlobal()` runs before the track loop, so at a step boundary `globalPos` is already the step that plays
  next. RESYNC therefore aligns to `globalPos` (not +1), and the cut overrides are applied in one pass **after** the step switch of all
  tracks (`seqCutApply`), so a song / pattern switch at the end of the pattern is already done.
- Each cut track keeps a **shadow stock position** (`Cut::stock`): `inc_uStep` restores it into `actual_pos` before `switchRowStep` and saves
  it afterwards, so wraps, `endOfPattern`, `passCount`, performance track switches and the row-0 duties (pattern preload, sequential
  switch, metronome) follow the real pattern and not the cut position.
- Rate 1/2: the held half does not start a step but the rest of `play_microStep` (micromove, roll, arp) keeps running. STOP drops
  micromove / roll / arp and silences the note unless the user holds it while live-recording (`recOpen`).
- Retrigger start: instrument start point + elapsed time x pitch ratio (note, tune, fine tune), wrapped into the loop for loop play modes,
  handed to the audio engine at full 16-bit resolution through the Position FX path (`playerEngine::pendingSeqStart`), so a RANDOM_VALUE
  FX cannot randomise it; slice, granular, reverse and wavetable instruments / steps are not touched.
- MIDI / synth tracks: a gap is not retriggered when the previous note has a gate FX, reversed running is not a jump at every step.
- Project load / new project posts SUB_RESET (all cut rows back to stock). CUT is still not saved.
- **For the C1 merge:** a clip launch must call the track's `Playhead::restart()` (through a new `Cut` command; the playhead would otherwise
  keep running the old pattern position), and **C1-STOP wins over CUT-STOP**: the track is silent and its playhead frozen until a clip is launched.
