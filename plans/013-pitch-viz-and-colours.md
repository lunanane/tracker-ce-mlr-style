# Plan 013 — PolySynth pitch visualisation + tracker-key colour theme for the clip launcher (owner 2026-11-05)

## V — pitch trace view for PolySynth
Owner: a pitch visualisation mode for PolySynth mirroring the viral pitch-trace look; fully visualise pitch incl.
glide and LFO modulation; as a second curve around the pitch values, the ADSR, moving close to the pitch curves
(simply offset / multiplied from the pitch xy); sleek: lines and a clear background; tracker key colours.
- x = time, scrolling right-to-left (~4 s window), y = pitch on a light semitone grid (octave lines stronger,
  note names at the left edge). Each of the 4 voices is one thin line in its own tracker key colour
  (Note teal, Instrument yellow, FX1 pink, FX2 blue — the physical key colours of the original Tracker).
- The drawn pitch is the actual voice pitch: glide shows as slopes, LFO-to-pitch as wiggles (pitch sampled at
  control rate from the voice: base note + glide position + LFO pitch offset).
- ADSR: a second, dimmer curve per voice hugging its pitch line: y = pitch + k·envelope (offset above the line,
  distance proportional to the envelope level), so attack lifts it away, release lets it fall back onto the line.
- Plain dark background, no fills, thin lines; current values at the right edge (note name per voice).
- Where: a view mode of the PolySynth overlay (toggle with a clearly labelled key, no long press) and on the
  PolySynth instrument page. Must keep live playing possible (Play-mode overlay). Display list: polylines with
  decimated points (e.g. 64 points per voice) — host-check ≤ ~4 KB; refresh ~25 fps max, only while visible.
- Data: a small per-voice ring of (pitch, env) samples written at control rate by the engine (cheap), read on the
  main loop.

## C — clip launcher colour overhaul
Restyle the clip launcher grid (clipGrid / clipsLayer marks) with the same tracker-key palette and the sleek line
style: clean dark background, thin outlines, state by colour (playing / queued / play-1x / empty / scene column),
frame of the 8x4 pad window in a key colour; keep the display-list budget (planGrid).

## M — Master amplitude view (owner addition 2026-11-05)
On the Master key's page a clearly labelled screen button (no long press) shows a dB waveform history of the master output:
scrolling right to left (~5 s), logarithmic amplitude axis (0 dB at the edges, -60 dB at the centre line), mirrored around
the centre, thin lines, dark clean background, faint dB grid (0 / -6 / -12 / -24 / -48), L / R in two tracker key colours.
Data: per audio block the peak of the final L / R output (one pass over 128 samples, only while the view is open), a ring of
columns on the main loop, dB by table (no log). Press the button again or leave the page to close; the other controls keep working.
Display list <= ~4 KB (decimated columns), <= 25 fps only while visible.

## As built (v0.3.4)
Files: `core/source/seqPalette.h` (colours), `seqPitchTraceLogic.h` (follower, decimation, auto range, layout, drawing template),
`seqAmpViewLogic.h` (dB table, ring, layout, drawing), `core/interface/seqPlay/traceControl.{h,cpp}` (one display control for both
views, id `controlIdSeqTraceView`), `pitchTrace.{h,cpp}` (entry point `pitchTraceView`: `createControl / forget / show / update`),
`core/interface/masterParams/ampView.{h,cpp}` (entry point `ampView`), tests `core/seqtest/test_trace.cpp` (`make test`).

**V pitch trace.** Engine (`plinky_voice.h`): every 12th block (~35 ms) `writeTrace` stores per voice (pitch * 32 incl. glide position,
LFO pitch offset and bend; envelope 0..255; sounding flag) in a ring of 128 (2 KB RAM; one release store of the head, the reader only
reads behind the head, a torn sample shows at worst one wrong point). Main loop (`pitchtrace::Follower`) copies new samples into a
112-sample window (3.9 s) and keeps the time axis running with silence when the engine is idle (it does not render then); at most 64
points per voice (last sample of each bucket); runs of fewer than two sounding points are not drawn. Auto range: min / max of the
sounding pitches (and pitch + envelope lift) -2 / +2 semitones, at least 14 semitones, grows at once, shrinks 10 % per refresh,
holds while nothing sounds. Grid: a line per semitone (only the octave lines when the range exceeds 40 semitones), note names at every
C and, while the range is <= 24 semitones, at the natural notes. Envelope curve: y = pitch + 3 semitones * envelope. Colours: Note
0x00E8BE, Instrument 0xFEF749, FX1 0xBB58F1, FX2 0x57F1FF = the stock theme pattern text colours (`patternTrackerColors`); the
envelope line is the same colour mixed 50 % into the background. Display list (host measured): 4 full voices over a wide range
2.6 KB, worst on / off pattern 2.3 KB (limit 4 KB). Refresh <= 25 fps, only while visible, nothing runs when hidden.
Toggle: Play-mode overlay: **FX2** while the PolySynth parameter overlay is open (hint line "Jog edit, Shift fine, Del reset, Instr
close, FX2 pitch"; to keep the overlay display-list test at 3600 B the overlay title lost the peak CPU, "PolySynth T<n> CPU ..%"); instrument
page: after v0.3.3 (Morph at page 3 place 7) all places of pages 1-3 are used, so there is a **page 4/4** reached with Next whose place 2 is
"Pitch view" / "Params" (other places empty). Any other screen button also returns.

**M amplitude view.** `AudioEffectLimiter::tapPeak` (after the limiter, i.e. the digital output before the codec volume): one pass of
`abs` over the 128 output samples per channel, only while `setPeakTap(true)`; `takePeak()` exchanges and clears the maximum.
`ampview::Follower`: a column every 40 ms (125 columns = 5 s), `dbx64` = 1/64 dB by a 65 entry table with interpolation (worst error
0.04 dB), decimation to <= 96 points by bucket maximum, L above / R below the centre line. Colours: L Note teal, R FX1 pink. Display list
1.3 KB. Placement decision: the Master page 1/3 has all eight screen buttons in use, so the button is the free eighth place of the
**Global Mixer 3/3** page ("Amp view", "Close" while open; the empty-space cover is narrowed). The view closes by itself when the page
changes.

**C clip launcher.** `clipGrid.cpp`: cells in three dark states (empty near black, notes dark grey, playing dark teal), the scene
column in dark blue states, pad window frame thin FX2 blue, armed follow action pink outline, Play 1x notch yellow, row-end notch pink;
`clipGrid` marks control: thin teal outline and progress line on playing clips, yellow blinking outline on queued ones, song row bar
blue. `planGrid` counts six cell batches instead of three (+48 B); worst case 5124 of 5500 B; new `marksCost` (<= 1600, worst
1196 B) for the marks control. All existing tests unchanged.

**Not verified on a device**: the on-screen look of all three (host tests cover geometry, size, data); display of the screen-button
labels on page 3/3 and the instrument page; ring write cost in the audio interrupt (12 stores per 12 blocks, negligible) and the
limiter tap (one 128 sample pass per channel while the amp view is open).
