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

## C1 as built (2026-11-04, provisional where marked)
- Code: pure logic `core/source/seqSessionLogic.h` (Launcher, Window, NoteCache, quantise, LED states, pad map; tests in
  `core/seqtest/test_session.cpp`), firmware glue `core/source/seqLauncher.{h,cpp}`, module `core/interface/seqSession/`
  (`seqSession.h`, `fSeqSession.cpp`, `gSeqSession.cpp`, `clipsLayer.*`, `sessionLayer.h`). Hooks: `inc_uStep` (after
  `switchRowStep`), `play_microStep` (stopped check next to the takeover check), `switchRamPatternsNow` (pattern switched),
  `Tracker::update`; `cFileManager::getPatternTracksWithNotes`; `eventActivateSeqSession`; Play mode's Pattern button.
- Pattern tap cycle: Editor -> Play -> Session -> Editor. Pattern held combos in the editor are untouched.
- Group select: screen button 1 = tracks 1-8, button 2 = tracks 9-16, Shift + any button = 9-16 (decided: simpler than 8 buttons).
- Window: 4 patterns, jog wheel scrolls one pattern per detent (Shift = 4); on entering it shows the loaded pattern.
- Launch: the main loop reads the clip at once (`loadPerformanceTrackToBuffer`, performance mode is switched on for the per-track
  lengths); the audio path copies it (`switchPerformanceTrackNow`) at the track's step boundary when the quantise point hits:
  next bar = global step % 16 == 0, pattern end = global step 0, immediate = the track's next step. Next bar / pattern end restart
  the clip at its first step. Transport stopped: launches apply at once. File reads are refused ("Busy") while a pattern load or a
  queued pattern switch uses the pattern buffer. SCENE = stock `sequencialSwitch` (pattern end, whatever QUANT says); stopped: at once.
- Stop: per-track stopped mask checked in `play_microStep`; applies at the quantise point (closes the sounding note); a launch, a
  scene / pattern switch or a transport stop clears it. An empty clip pad stops the track (Ableton) [provisional].
- LEDs (PWM = level * 2): playing 15, queued blinks 15/0 (a pending stop blinks the playing clip), has notes 3, empty 0; the "has
  notes" cache of the window is read one row per main-loop pass (Workspace files; the loaded pattern from RAM).

### SessionLayer interface (for the CUT / MLR component)
`core/interface/seqSession/sessionLayer.h`, namespace `seqsession`:
```
struct SessionLayer {
  const char* name() const;                       // "CUT"
  void enter(uint8_t firstTrack);                 // layer becomes visible (default: setGroup)
  void leave();                                   // hidden / Session left: release held pads
  void setGroup(uint8_t firstTrack);              // 0 (tracks 1-8) or 8 (tracks 9-16)
  void pad(uint8_t col, uint8_t row, bool down);  // 0-based col 0..11, row 0..3; pad index = row * 12 + col
  void leds(uint8_t out[48]);                     // 0..15 per pad
  void update(uint32_t nowMs);                    // main loop, every pass while visible (optional)
  bool jog(int16_t value, bool shift);            // true = handled (optional)
  void info(char lines[4][48]);                   // four text lines under the title
};
```
The module keeps the pads (col 11, rows 1 + 2, 1-based) as the CLIPS / CUT switch in every layer: they never reach the layer, and
the module draws their LEDs. Everything else is the layer's. Plug in with `seqSession.setCutLayer(&myLayer)` (default
`CutStubLayer`: "CUT - coming"). A layer runs on the main loop only; the audio side is reached through the existing `seqEngine`
post calls or a new ring like `seqsession::Launcher`.

