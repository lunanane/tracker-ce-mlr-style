# Plan 016 — immediate MLR on long loops + time-stretch (warp) to BPM (owner 2026-11-06)

Owner: "We want to improve the sample syncing, warping and make MLR really immediate." Answers: warp = time-stretch
to BPM (pitch kept); MLR: "the sound starts late"; "the limitation of the pads is that the longer patterns I use most
only have 8 cue points in MLR, so the solution would be to use higher-resolution cue points and basically to limit to
two samples being chopped at once"; material: long loops (> 6 s, streamed from SD).

## A — MLR: 2 tracks × 16 cue points, cue cache for instant starts
- MLR layer: each track uses **two pad rows = 16 cue points** (row a = cues 1–8, row b = cues 9–16); 2 tracks per
  screen (groups 1–2, 3–4, … via screen buttons), pad columns 9–12 stay the per-track controls (stop/rev/rate/resync)
  on the track's first row; the second row's cols 9–12: loop/rec helpers or empty (decide, keep simple, label on
  screen). Lane screen: 2 lanes with 16 segments each. Two-pad loops, latch, rec takes, quantise, SYNC ALL keep working
  with 16 positions (positions become 1/16 of the loop).
- **Cue cache** for the (at most 2) active MLR tracks with streamed samples: preload ~0.25–0.4 s of audio at each of
  the 16 cue points into EXTERNAL_RAM (raw storage, set up in TrackerImpl::init / on MLR entry — no static ctors in
  EXTERNAL_RAM), filled in the background by the file manager (never blocking the audio ISR); a jump plays from the
  cache immediately and hands over to the stream once the VoiceStreamManager has refilled from the new position
  (seamless join). Budget ≈ 2 tracks × 16 × 0.3 s × 44.1 kHz × 2 B × channels; if EXTERNAL_RAM is short, take it from
  SAMPLE_MEMORY_SIZE (dev guide: up to 3.5 MB can be freed on MK66). Resident (non-streamed) samples need no cache.
- Measure: press-to-sound latency for streamed loops before/after (host model + device-test row).

## A - owner refinement (2026-11-06, supersedes "cues = loop/16" and "pairs of tracks" above)
Ableton-style clip meshing: **two slots (A, B)**, each holds one track you pick (default A = track 1, B = track 2); slot A = pad rows 1-2, slot B = rows 3-4.
- **Steps on a musical grid, not loop/16**: each pad = one step of the grid, default **1/4** (one beat = 4 pattern steps), selectable **1/8** (2 steps) per slot. 16 pads = one **section** (64 or 32 pattern steps). The pads address the 16 steps of the section the playhead is in (it moves on with the playhead) or, with HOLD, the held one. Steps behind the end of the pattern are dark and inactive.
- **Track pick**: screen buttons "Assign slot 1" / "Assign slot 2" turn the slot's two rows into a track picker (16 pads = tracks 1-16; lit = notes in the pattern, bright = assigned); a pad assigns and returns, the button again cancels; the other slot keeps meshing.
- **Pad map** (per slot): cols 1-8 x 2 rows = steps 1-16; cols 9-12 first row STOP REV RATE RSYNC, second row GRID (1/4 <-> 1/8) SECT (FOLLOW <-> HOLD) REC (a take of this slot) MUTE (project mute of the slot's track). Screen buttons: 1 Assign 1, 2 Assign 2, 3 Help (pad legend), 4 empty, 5 Q, 6 SYNC ALL, 7 [REC], 8 CLIPS/CUT.
- **Lane screen** (tracker key colours, slot A Note teal, slot B FX1 purple, loop Instrument yellow, ghost FX2 blue, plain dark ground): two lanes, 16 pads of the shown section, section index `sec 3/8`, grid label, the **waveform** of the section (152 column min/max overview), step lines, loop bracket, cache dot per cached cue, playhead, the eight extra pad names with their states; **Help** draws a 12 x 4 pad legend (words <= 6 chars, states highlighted, T1..T16 while picking) that stays open while playing.
- **Cue cache** follows: the cues (step starts) of the current section of both slots, the next section's cues are prefetched once the playhead is in the second half of the section.

## A - as built (v0.3.8, original Tracker only; branch v038-mlr)
**Engine (seqgen + core/source)**
- `seqgen/playhead.*`: grid mode (`setGrid(stepsPerPad)`, `setHoldSection`, `shownBase`, `activePads`, `press()` returns false for an inactive pad); loops keep the section they were set in (`loopBase`); the 8/12-column even split stays (legacy / tests), `kMaxColumns` 16, `deserialize` accepts 16.
- `core/source/seqMlrLogic.h` (pure): grid/section helpers, pad map, commands (`packGrid`, `packHold` in the SUB_RESET slot, cols 6-9), LED fill (`fillMlrLeds`), legend cells, waveform fold, lane geometry / models, latency model lives in `seqCueCache.h`.
- `seqCut.*`: `setSectionMode(true)` (set by `seqCutInit`; the default constructor stays legacy so the engine tests are unchanged), per-track grid and section hold (kept over SYNC ALL / hand-back, reset by RESET), `View` extended (`flags2`, `sec`, `nsec`, `pads`; `col` = pad or 0xFF), stock tracks show the section from the pattern position via the existing hooks.
- `cutLayer.*`: slots, picker, extras, legend flag; `cutSetGroup` is gone (`cutInit`, `cutAssignSlot`, `cutPickToggle`, `cutLegendToggle`); `seqTracks.cpp` glue for mute / "tracks with notes" and `Sequencer::seqCutFrameForPos`.
**Cue cache (A2)**
- `core/source/seqCueCache.h` (pure bookkeeping, tests) + `core/source/seqMlr.cpp` (firmware). Pool = raw `__NOINIT(EXTERNAL_RAM)` array 2 slots x 2 banks (current / next section) x 16 cues x 2 channels x **3072 frames** = **786,432 bytes** (70 ms per cue at 44.1 kHz). Build numbers (`make original`, SDRAM-ctor guard ok): FLASH 945,764 B (was 938,724; `_etext` 0xE6E64 = 36 KB below 0xF0000), internal RAM 85.29 % (was 84.34 %: +2.4 KB bookkeeping, views, lane model), EXTERNAL_RAM 98.07 % (16,453,464 B; ~316 KB free, was ~1.06 MB). **`SAMPLE_MEMORY_SIZE` is unchanged (the free ~1.06 MB sufficed; sample pool unchanged).** A bigger window (e.g. 6144 frames = 1.5 MB) needs the pool taken from `SAMPLE_MEMORY_SIZE` (developer guide: up to 3.5 MB free-able); the constant is `seqcue::kFrames`.
- Fill: `seqmlr::update()` (main loop, every 100 ms, only after the MLR layer was used) recomputes the 16 wanted cue frames per bank through `Sequencer::seqCutFrameForPos` - the SAME mapping the retrigger uses (instrument, Position FX, pitch, loop points, BPM), so a change of sample / loop region / tempo / pitch changes the wanted frames and the entry is reloaded (key = frame + sample hash + instrument). The audio data is read by `seqmlr::idle()`, called from `VoiceStreamManager::update()` only when no stream job is running or due, one blocking `readInstrumentFramesBlocking` of 6 KB per call (never in the audio ISR).
- Hit: `playLoopForward` / `playSingleShot` ask `seqmlr::findCue()` before `notifyNoteOn` and hand the result over through the EXISTING slice-cache path (`currentSliceCacheAddress/Frames`, `notifyNoteOn(..., sliceCachedFrames)`): the voice plays the first 3072 frames from RAM and the stream ring starts exactly behind them (sample-accurate join, no new mechanism). Reverse starts and starts inside the RAM pre-cache are not cached. A miss behaves as before.
**Waveform**: `seqMlr.cpp`: 152-column overview of the shown section (probe of 256 frames at each column's start - an approximation, not a full scan), rebuilt when track / section / grid / the step-to-frame mapping changes; resident samples from RAM in the main loop, streamed ones in `idle()` (2 columns per call); stored in `__NOINIT(EXTERNAL_RAM)` (608 bytes).
**Latency (A3)**: host model (`seqcue::latency*Ms`, test prints it): streamed jump before ~17.9 ms (1 ms tick + 8 ms main-loop gap + 6 ms first SD chunk + one block), cache hit ~3.9 ms (tick + one block). Device rows: test list 10b.
**Tests**: `seqgen` playhead grid tests (6); `core/seqtest/test_cut.inc` new: grid helpers / pad map / commands, engine sections (1/4, 1/8, short last section, loop clip, resolution switch, hold, RESET, legacy layout), LEDs, legend words, cue cache logic (wants, stale / reload, find windows, order, bank swap, prefetch), latency model, slots through the layer (assign, swap, picker, extras, buttons, legend), lane screen budget / bounds / head model / waveform helpers, PNG preview (`make preview`: mlr0-3.png).
Existing checks changed as direct consequences (equally strict): the layer-level tests of plans 002-008 run through a *legacy-row shim* (`tPrep`, `tCutPad`, `tAPad` in test_cut.inc: test row r = track g + r, put into a slot, grid 1/8 for 16 step patterns / 1/4 for 32 step patterns - which gives exactly the old 8 positions); `freshCut` sets that grid; `test_mlr_bar_follows_the_pattern` presses pad 3 instead of column 2 (24 steps, 2 steps per pad: position 6); the label check of buttons 0 / 7 and the group-arrow test follow the new button row (no groups); `CountCtx` measures font 5 (the small font) at 6 px per character. Old geometry / drawing tests (`laneDraw.h`, 8-segment lanes) are kept as they were (that code is no longer used by the firmware).
**Shared files touched (coordinate with the warp agent)**: `core/libs/Audio/play_memory_loop_forward.cpp` and `play_memory_single_shot.cpp` (a 5-line `findCue` block before `notifyNoteOn`, plus passing `currentSliceCacheFrames`); `core/AudioEngine/VoiceStreamManager.cpp` (include + one `seqmlr::idle()` call in `update()` when `scheduleNextVoiceJob` found nothing to do); `core/source/seqTracks.cpp`, `mtSequencer.h` (`seqCutFrameForPos`, mute / notes helpers, `setSectionMode`). **Warp must plug into `Sequencer::seqCutFrameForPos`** (the single step-to-sample-frame mapping used by cache and waveform; `startPoint16` there assumes the sample plays at its pitch speed) and, if its readout position differs from `start * length / 65535`, keep `seqmlr::findCue` keyed on the frame the voice actually starts from.
**Known limits / risks**: the cache window is 70 ms: at 4x playback speed the stream must deliver within ~17 ms (the model says the usual main-loop gap + SD chunk is ~14 ms) - device-test; cue audio can be rewritten while a voice still reads it (a stale entry is dropped first, the race window is one main-loop pass; at worst a glitch in the first 70 ms of a jump during a BPM change); the waveform probe is 256 frames per column; grid / hold / slots are not saved with the project; the Plus / Mini builds were not built (the glue is platform independent, the pool is EXTERNAL_RAM there too).

## B — Warp: time-stretch loops to the project tempo (pitch kept)
- Per track (or per instrument) **Warp** on/off + loop length in **bars** (auto-guess from sample length at the
  current BPM, editable); playback ratio = (bars × 4 × 60 / BPM) / sample duration; pitch unchanged.
- Algorithm for the Cortex-M4: granular overlap-add / WSOLA-lite — grains ~30–50 ms, 2 overlapping grains with
  equal-power or Hann crossfade, grain start positions advanced by the tempo ratio; optional transient-aware grain
  alignment later. Works with streamed samples (read ahead). Must coexist with MLR (cue positions are positions in the
  warped timeline) and launcher clips.
- CPU: measure per warped voice (readout like PolySynth); cap the number of simultaneously warped voices (start with 2)
  if needed. Sound: no clicks at grain joins, tempo change follows smoothly.

## B as built (v0.3.8, Original Tracker only; host-tested, not device-tested)

**What it is.** Per sample instrument: Warp Off / On and Bars (0 = Auto, 1 .. 32). Where: Instrument Parameters > Effects (the second page, the
screen button "Effects"), places 5 and 6 (labelled buttons right of "Instrument MIDI Mapping"; screen buttons 6 and 7, or Left / Right which skip the
MIDI Mapping button, Delete = default). The title shows the detected length (`sample = 3.9 bars @120`) and, while a warped voice plays, `CPU n% (voices)`.
Saved in the sidecar `seqwarp.bin` (frame of `seqSidecarLogic.h`, magic `SWRP`, version 1, 96 bytes payload = on / bars per instrument; same deferred
write, newer-file and project-copy rules as `seqfx.bin`); the stock project structs are untouched (`sizeof(strInstrument) == 372` stays).

**Files.**
* `core/libs/Audio/warp_core.h`: the DSP, header only, no Polyend includes (host-tested). `core/libs/Audio/play_memory_warp.cpp`: the glue
  (`AudioPlayMemory::warpPrepare / updateWarp / warpEnd / warpCpuPercent`).
* `core/source/seqWarpLogic.h` (settings, guess, ratio, sidecar frame; host-tested), `seqWarp.h/.cpp` (values + sidecar I/O).
* `core/interface/instrumentEditor/instrWarpUi.cpp` (UI members of `cInstrumentEditor`), small hooks in `fIinstrEdit.cpp`, `gInstrEdit.cpp`,
  `instrumentEditor.h`.
* `core/seqtest/test_warp.cpp` (569 checks) + a rule in `core/seqtest/Makefile`.

**Shared files touched (for the merge with the MLR agent / part A).**
`core/libs/Audio/play_memory.h` (include, member block `warpV / warpK / warpCounted / warpPrepare / warpEnd / updateWarp`, static `warpCpuPercent`),
`play_memory.cpp` (`update()`: `if (warpV.active) block = updateWarp(); else switch ...`; `warpEnd()` in `stop()`, `clean()`,
`refreshStartParameters()` and the two `playForPrev(addr, ...)`), `play_memory_loop_forward.cpp` and `play_memory_single_shot.cpp` (one line
`warpPrepare(instrIdx);` before `playing = 1;` - Backward and Ping-pong call `playLoopForward`), `core/AudioEngine/VoiceStreamManager.h`
(**`kLoopWrapFrames` 2048 -> 4096**, +32 KB EXTERNAL_RAM; nothing else of the stream manager changed), `core/AudioEngine/mtAudioEngine.h`
(`getTempoBpm()`), `core/source/seqSidecar.cpp` (4 dispatcher lines), `core/source/seqTracks.cpp` (`seqWarpUpdate()` in `seqTracksUpdate`).
The warp read path is `play_memory_warp.cpp` only; it reads through `readSampleAtRel()` (resident or streamed), so a cue-cache read hook inside
`readSampleAtRel()` serves warped voices as well.

**Algorithm (see the header comment of `warp_core.h`).** Two overlapping Hann grains (Q14 table, 257 points, constant overlap-add: the weights sum to 1
exactly), hop = 768 frames (grain 34.8 ms) at pitch <= 1.3, shorter for higher pitch (`hop = 1000 / pitch`, min 96) so that a grain's span stays
<= 2000 frames. A grain starts every hop at the TIMELINE, which moves `ratio` source frames per output frame; inside a grain the source is read with
the pitch increment (linear interpolation, 16.16 position) so tune / finetune / glide are independent of the tempo. `ratio = K x BPM` with
`K = region frames / (bars x 240 x sample rate)`, `region` = the instrument's own start .. end point (not a forced one: an MLR cue or `P` jump keeps the
stretch; its position is a position in the warped timeline); `ratio` is clamped to 0.25 .. 4 and follows the tempo (`engine.getTempoBpm()`, the
smoothed tempo the audio blocks use) through a one-pole per block (~30 ms), so tempo ramps never jump. The first hop of a note plays the source
unchanged (both grains start at frame 0): the attack keeps its transient; ratio 1 / pitch 1 is bit-exact. **WSOLA-lite** (added because a plain
overlap-add detunes a steady tone by up to +-hop-rate/2 = 29 Hz, measured 11 Hz at ratio 0.7 and 1.3, which the "constant pitch" test cannot accept):
a new grain starts at the best of +-192 frames around the timeline, the best correlation (dot^2 / energy, 32 points, stride ~16 pitch frames) with the
natural continuation of the grain that fades out; hierarchical (coarse 1:8, then step 2, then 1), skipped when ratio == pitch; measured detune
0.00 .. 0.03 Hz. Loop modes: Forward wraps loop2 -> loop1 (grain reads wrap too); Backward = forward to loop2, then backward and wrap loop1 -> loop2
(the stock behaviour); Ping-pong reflects; One-shot ends at the end point (the last grains fade out, ~2 hops, the last frame is held, no step).
Not warped (they play un-warped as before): Slice, Beat Slice, Granular, Wavetable, the reverse step FX (`R`), a note while 2 voices already warp.

**Streaming.** The voice reports to `VoiceStreamManager::updateVoicePlayhead` the lowest frame it will still read: the older grain, the newer
grain (a wrapped one counts one loop further), and the start of the next grain minus the search range; the ring (16384 frames, lookbehind 512)
covers it with room to spare (at most ~4000 frames ahead at stretch <= 4, pitch 1) and the loop-wrap cache (now 4096 frames) covers a grain that
wrapped. Tested in `test_stream_window` against a model of the ring. Limits: a streamed loop whose loop start is beyond the SDRAM pre-cache (0.74 s)
at ratio > 1.5 or pitch > 1.3 can run out of the wrap cache for a few ms at the loop edge (zeros + the stream's own fade-in); the Backward /
Ping-pong turn is not modelled by the ring test.

**Cap.** `seqwarp::kMaxVoices = 2`. A third voice does not refuse to play: it plays un-warped (silence would be worse than a wrong speed). The slots
are the 8 voices (`voiceIndex`), claimed in `warpPrepare` (inside `AudioNoInterrupts`), released in `warpEnd`.

**CPU.** Host model (`test_cpu_estimate`, counted source reads): 4.00 SDRAM reads per frame (2 grains x 2), +0.65 with the alignment search ->
about 18.3 k cycles per 128-frame block at ratio == pitch (3.5 % of 522 448) and 21.8 k (4.2 %) otherwise, using the audit's 22 cycles per SDRAM read
and ~55 cycles of other work per frame; plus the stock chain behind it (envelope, amp, mono->stereo, mixer: ~9 k = 1.7 %). So **a warped voice is
about 6 %** instead of 4.3 % (an estimate; the `CPU n% (voices)` readout in the Effects title measures it with the DWT counter like PolySynth's, per
voice an average of 32 blocks, summed). Two warped voices plus the rest of a busy project: write the numbers down on the device (test list 11l).

**Size.** Flash +9.4 KB (938 724 -> 948 124 B; `.text` 943 312 -> 952 712; ~30 KB left before 0xF0000): `updateWarp` 3.0 KB (hot, `AUDIO_CORE_RAM`),
the search 1.2 KB (`noinline`), the UI and the sidecar the rest; no -Os needed. Internal RAM +2.3 KB (85.23 %): 8 voices x 80 B (`warp::Voice`), the
window table 514 B, the shared search buffer 704 B, small statics. EXTERNAL_RAM +32 KB (wrap cache 8 x 2 x 4096 B); no object in `__NOINIT(EXTERNAL_RAM)`
got a constructor (`check_sdram_ctors.sh` ok).

**Known limits / decisions.** Transients are smeared (a grain start moves by up to +-4.4 ms to align the phase; a drum hit can shift by that);
stretches beyond 0.5 .. 2 repeat or skip material; Auto bars is the guess at the tempo of each note (it follows tempo changes between notes: set a number
for a fixed value); live loop-point edits do not reach a warped note that is already playing (they apply at the next note); the sample rate used is the
audio clock 44117.6 Hz (the same as the tempo clock). Warp settings belong to the instrument slot: copying an instrument does not copy them.
Device checks: `docs/seq-tracks-test-list.md` 11l. Possible later: transient-aware grain placement, a per-track (not per-instrument) setting.

**MLR and warp (merge with part A).** `seqWarpTimelineSpeed(instr)` (seqWarp.cpp) gives the stretch ratio of a warping instrument (0 = not warped);
`Sequencer::seqCutFrameForPos` and the retrigger start in seqTracks.cpp use it instead of the pitch speed in `seqcut::startPoint16`, so cue points,
the cue cache (`seqmlr::findCue`, keyed on the frame the voice starts from = `pointsInSamples.start`) and the section waveform map through the warped
timeline. The 2-voice cap is ignored there (a third voice that plays un-warped starts from a slightly different frame). Warped voices read the cue
cache through `readSampleAtRel()` like any other. No warp buffers in EXTERNAL_RAM (only the 32 KB of the larger loop-wrap cache).
