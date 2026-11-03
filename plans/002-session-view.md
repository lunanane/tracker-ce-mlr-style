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

## Open questions
- 8 columns + 4 control columns, or 12 position columns with controls on the screen buttons?
- Quantise default (next step / next beat).
- Does a retriggered track return to sync at pattern end, or stay offset until re-synced?

## Phases
- **S1 library** (DONE 2026-11-04: `seqgen/playhead.*`, 24 host tests, 99 total green; press-over-recording rule = override while held, resume in phase, see seqgen/README "Playhead") (`seqgen/playhead.*`, host tests, no hardware): quantised jumps, two-pad loop ranges, playhead
  recording + looping, the "as if at X" note/offset lookup, rate ½/2×, reverse.
- **S2 firmware glue**: Session view page, per-track playhead override in `inc_uStep`/`switchRowStep`, retrigger
  with offset, sidecar save. Needs `make original` (owner OK).
- **S3 cut cache** for streamed samples on the original Tracker.
