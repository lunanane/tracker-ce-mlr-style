# Plan 002 — Session view (MLR-style track retrigger)

Status: IDEA (2026-11-04). Investigation only; builds on plan 001's Play mode (same Pattern-tap toggle family).

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
- Plays the pattern data (and plan 001 sequencers). Recording a jump performance into the pattern: later.

## Open questions
- 8 columns + 4 control columns, or 12 position columns with controls on the screen buttons?
- Quantise default (next step / next beat).
- Does a retriggered track return to sync at pattern end, or stay offset until re-synced?
