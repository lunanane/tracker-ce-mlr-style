# Plan 015 — higher-resolution views + Plinky wavetables in PolySynth (owner 2026-11-06, v0.3.6 hardware)

Owner: "Things look great." Then:

## V — pitch view and amp view need much higher resolution
- Both views: more points, finer lines. They now run as view switches (the pattern control is hidden while they show),
  so the frame budget is the view + title + strip — use it (measure `cDisplay::shownRamBytes()` worst cases; raise
  the per-view budget from ~4 KB accordingly, keep a safety margin under 8172 B).
- Pitch: sample the latched voice state every block (or every 2nd) instead of ~35 ms; more points per voice
  (e.g. 160–256 across the width), smooth glide/LFO.
- Amp: measure per track every audio block (2.9 ms) instead of 40 ms (block peak / RMS per track at the source, cheap),
  so the incoming stream is dense; draw each track as a contour band (average … peak) with thin outlines, many points.
- **Amp time axis is wrong**: values come in fast but are already compressed after ~10 % of their way to the left.
  The linear part near "now" must be much longer and the compression must build up gradually towards the left axis
  (like the reference). Retune the age→x mapping (larger time constant / blended linear+log / power curve) so roughly
  the right half shows the last few seconds almost linearly and the left half holds the long history compressed;
  optionally the jog wheel adjusts the compression on the view (shown as a label).

## W — Plinky wavetables in PolySynth
Owner: "where are the wavetables from Plinky? Currently we have saws and squares, which sound great, but I'd like the
wavetables in there as well." Port Plinky's wavetable oscillator mode (shape parameter morphs saw → square → the
wavetable set, as on Plinky) from plinky_public (MIT). Assess the wavetable data size first: embed in flash if it fits
(image must stay < 0xF0000, now 0xDCC44 ≈ 78 KB left; compress/decimate if needed), or generate at boot into
EXTERNAL_RAM if Plinky builds them procedurally (raw storage + init in TrackerImpl::init — never a static constructor
in EXTERNAL_RAM). CPU per voice must stay close to the current cost (interpolated table lookup, band-limited tables).

## V as built (v0.3.7, branch v037-viz from seq-tracks 018dd73 = v0.3.6; Original Tracker only)

**Frame budget.** Worst case frame model (`cDisplay::shownRamBytes`): title bar 600 + strip / labels 1720 + the view. With the pattern control hidden
(view switch) the view may use 8172 - 400 (margin) - 600 - 1720 = **5400 B** (was 4096): `pitchtrace::kDisplayBudget`, `ampview::kDefaultBudget`;
`test_frame_budget_model` checks `kDisplayBudget + strip + title <= 8172 - 400`. The amp view sits on the Global Mixer 3/3 page whose own controls stay
shown under the opaque panel, so it measures them: `cDisplay::shownRamBytesExcept(control)` (new, read only) gives what the others use and
`AmpView::update` sets `Follower::budget = 8172 - 400 - others` (clamped to 2600 .. 5800); the drawing picks the number of points per line from it
(`colsFor`), never above the budget (tests with budgets 3600 and 8000).

**Pitch view.** Engine edit limited to `kTraceEvery` 12 -> 2 (one sample per 2nd block = 5.8 ms, ring still 128 = 743 ms; latch / write logic unchanged,
a stab still gets its sample). Follower: 672 samples (3.9 s) per voice, raw storage in EXTERNAL_RAM (10.8 KB, placement new on the first show), idle
time axis by microseconds (5805 us per sample), minimum visible run 30 samples (175 ms). Drawing: **168 points per pitch line, 84 per envelope line**
(every second pitch point), y over 36 .. 420 (title and hint inside the plot), auto range: at least **12 semitones** (one octave), margin 1 semitone
(plus the 3 semitone envelope lift), grows at once, shrinks 6 % per refresh (about 0.6 s). Host: 4 full voices over a wide range 4580 B, worst on / off
pattern 4812 B (limit 5400). CPU: 4 float to int conversions per 2 blocks, negligible. The real engine test (chord, stabs, glide, LFO) is unchanged and passes.

**Amp view.** Taps at the source, one peak per audio block (2.9 ms), only while the view is open: `AudioAnalyzeRMS` (the per track analyzer, which already
sees every block) and `AudioEffectLimiter::tapPeak` write into `PeakRing` (`libs/Audio/peak_ring.h`, 64 values, buffer owned by the view; no buffer = nothing
measured, the RMS meter is untouched). `playerEngine::setPeakTap / peakTapHead`, `audioEngine::setMasterPeakRing / masterPeakHead`. `AmpView::update` (40 ms)
reads the new blocks of the 8 tracks (max over the channels) and the master (clock = master ring; lost blocks repeat the last value so the axis keeps
its length) and pushes them into the cascade. Cost of the tap: one pass over 128 samples per track channel and per master channel per block (about 1.5 % of
the block budget with all eight tracks, only while open); the cascade push is O(1) per block per lane (a 25 entry merge every 8th block). Cascade per lane
(`Cascade`, 1960 B, 9 lanes 17.6 KB in EXTERNAL_RAM): the newest 192 buckets of 8 blocks (23 ms, 4.4 s) as a ring, older ones an exponential histogram
(4 buckets per span of 16 .. 16384 blocks, the oldest absorbs), 231 buckets after 400000 blocks. Drawing: every lane is **resampled at M equal-width
columns** (weighted average / maximum of the buckets in the column, linear in dB between bucket centres inside a coarse bucket), so a line has 16 .. 200
points independent of the bucket structure: with 8 loud tracks + master 60 points per line (5080 B), with 2 tracks 176 (3408 B). Per track a dim peak line
and a normal average line (outlined band), the master one light line. Floor **-48 dB** (typical -36 .. 0 dB use 75 % of the height), plot y 36 .. 418 over the
full height between title bar and strip; dB labels left, time marks inside the plot at the bottom, header texts (pattern / step, BPM, hint) in the free
corner of the right column, title and the jog label at the top left of the plot.

**Time axis (owner: "compressed after only 10 %").** age(u) = H (c u + (1 - c) u^6), u = 0 at the right edge .. 1 at the left edge (H = 120 s), c chosen so
age(0.5) is the span of the right half. Default **4 s** (c = 0.037): the first 10 % of the width are 0.44 s, the first quarter about 1.1 s, 50 % = 4 s, 75 % =
24 s, the left edge 120 s; the speed grows gradually, no knee. Jog wheel (`AmpView::jog`, `functEncoder` on Global Mixer 3/3 while the view is open):
seven scales 2 / 3 / 4 / 6 / 10 / 20 / 60 s (60 = a straight line), label "Jog: time scale, right half = N s". Constant time (forward evaluation per column,
bisection only for the five time marks).

**Test changes (consequences of the new design, equally strict or stricter).** test_trace.cpp: the ring size check (`kTraceLen >= kWin + 8` described the old
window; now: the ring holds at least 400 ms, kWin samples are 3.9 s, `kSampleUs` equals two blocks); `firstRel < 255` (a single sample 40 back, an alignment
coincidence at 12 blocks per sample) became a scan of the release: the minimum is below 255 and the envelope never rises; the idle test advances 30 ms (5 sample
periods, was 5 x 35 ms); the decimation tests feed the 128 entry ring in chunks of 64 and expect kMaxPts points; the envelope line has half the points, so point i is
compared with pitch point 2 i + 1 (`envY.size() * 2 == pitY.size()` added); the worst on / off pattern uses runs of kMinRun with gaps of 5 (runs of two are
stretched by the follower since v0.3.6); the size limits 4096 became `kDisplayBudget` 5400. test_amp.cpp was rewritten for the block peak cascade and the new axis
(the old constants kLevels / kT0Ms / 40 ms columns no longer exist): same classes of checks (dB table, cascade spans / size / merge average and maximum,
monotone and compressing axis, marks in order, lane mapping, colours, bounds, budget), plus right half >= 3 s, left edge = full history, all jog scales,
resampling, budget adaptation, full height. test_cpu: `testPeakTap` (analyzer and limiter rings). No existing expectation was relaxed except as listed.

**Numbers.** `make original`: FLASH 902060 B (86.03 %, image end 0xDD398 < 0xF0000), internal RAM 221080 B = **84.34 %** (v0.3.6 had both views' follower
state in internal RAM; now about 3.7 KB less), EXTERNAL_RAM +31 KB (17.6 KB amp lanes, 10.8 KB pitch follower, 2.3 KB rings); `check_sdram_ctors.sh`: ok.
Previews (not committed): `core/seqtest/build/preview/pitch1..4*.png`, `amp1..6*.png` (before: `build/preview_before`).

**Not verified on a device**: the look, the tap cost in the audio interrupt, the budget measured from `shownRamBytesExcept` on the real page, the jog feel.

**V follow-up (owner review of the previews).** Lines are clipped to the floor and BROKEN where a level falls below -48 dB (no joins across silence; a lane may be several strips, `resample` marks breaks with y = -1); the average lines are smoothed over 3 to 5 columns in the right half (triangular), the peak lines stay raw and thin. The long straight lines in the amp4 preview are real: a slow track (3.76 s period) decays exponentially, which is a straight line in dB. Per line allowance in `colsFor` is 40 B for the extra strips.


**V device fix (v0.3.9, owner on the original Tracker with v0.3.8: amp view "doesn't reach the left axis, just to the centre", pitch view "doesn't work at all").** Root cause, the same for both: `cDisplay::step1` writes one stage of a control as ONE SPI burst into the FT81x command FIFO (RAM_CMD, a 4096 byte ring; `beginCoProListNoCheck`, no free space check) and moves `REG_CMD_WRITE` once at the end, modulo 4096 (`EVE_IncCMDOffset`). A stage of 4092 bytes or more overruns the ring and the co-processor executes only `bytes mod 4096` of it. The views were single stage controls and v0.3.7 raised their budget to 5400 / 5800 bytes (checked against the 8172 byte frame, never against the 4 KB FIFO): the pitch view crossed 4 KB as soon as the four voices had lines in the window (host: 4096 .. 4580 bytes), the amp view as soon as its lines covered more than about two thirds of the width, because its list grows with the history (host, device feed: 5064 .. 5324 bytes after 2 minutes, 4 KB is passed after about 20 .. 30 s at x ~ 200 .. 230). v0.3.6 (2.3 .. 2.8 KB) never got there. The cascade, the axis and the feed were correct (a host replay of `AmpView::update` with the 40 ms jittery cadence, the 64 value PeakRings and uint16 heads reaches the left axis after 2 minutes). Fix: `cSeqTraceControl` draws in **two stages** (`seqTraceStages.h`: the same template drawing, every command counted with its exact FIFO size, stage 0 up to 3840 bytes, the rest in stage 1, anything beyond both dropped, never an overrun; the stage DLs are concatenated in the control's RAM_G slot as in the other multi stage controls), and the views do not change their follower while a frame is half built (`TraceViewData::busyMs`, at most 200 ms). Budgets, points and looks unchanged. DEBUG builds print the two stage sizes and the dropped bytes at the bottom left. Test: `test_stages.cpp` (`seq_stage_tests`): FT81x FIFO model, device feed of both views, stage 0 + stage 1 == the single stage drawing, data frozen between the stages, amp lines at about the centre after 5 s, further left after 10 / 30 s, at the left axis after 125 s, never older than the real history; pitch: all four voices drawn. **Follow-up (audit of every mod control, exact FIFO bytes per stage, limit 4092):** launcher grid `cSeqClipGrid` single stage up to **5028** (typical 2452, 12 coloured rows 3028; over 4 KB with many flags / follow outlines / help) -> two stages through a byte gate in `clipGrid.cpp` (a cut RECTS batch is reopened, both stages draw a POD snapshot of the View in EXTERNAL_RAM), stages <= 3836 (`test_gridstages.cpp`, the real clipGrid.cpp against `seqtest/fifostub/`, 1204 views); MLR lanes `cSeqLanes` up to **4124** (scene 1) -> two stages (`tracestage::BatchSplit`, the lanes model is not replaced while a frame is half built), stages 3820 + 328; all others fit one stage: CUT lanes model 3256, playheads 244, clip marks 1144, PolySynth section view (legend control, PLINKY) 1556 + edge marker, note band 176, legend (48 cells, captions) about 1.8 KB, type list about 1.4 KB, fader view stage 0 about 0.95 KB / stage 1 about 1.7 KB (bounded by its string sizes). glue tests check the FIFO size of every template drawing (CountCtx / PlCtx `fifo()`).

**V time axis (v0.4.1, branch v041-amp from seq-tracks 2680f73 = v0.4; Original Tracker only).** Owner on the device (v0.4): "the amp viz still doesn't fit
right: it starts and then goes only until the centre; it slowly moves to the very left over ~10 min. I would like audio coming in smoothly until 75 %
distance travelled from the right side, and have the curve perfectly align and be cut off at the y axis." Cause: the v0.3.7 axis was `age(u) = H (c u + (1 - c) u^6)`
with H = 120 s, so the left edge was the full 120 s history: a view that has 30 s of history ends in the middle of the width and the lines only reach the
axis after 2 minutes (and the exponential histogram blurred what was left of 4.4 s). New design:
- **Axis** (`ageMsOfU`, `xOfAge`): the right 75 % of the width (495 px) is LINEAR in time (newest at the right edge, every column moves at the same speed,
  default 8 s = 16.16 ms per px), the left 25 % (165 px) is a parabola in x with the same slope at the 75 % point (continuous slope, no kink, the compression
  grows smoothly) that ends at the total age at the left axis. The line is CUT there: `resample` draws a column when the exact age of its x is inside the
  history, the oldest column is exactly x = kX0 (the y axis, now drawn as a line), nothing older is drawn or kept. Four jog scales (label "Jog: time
  scale, linear N s / total M s"): linear 2 / 4 / 8 / 16 s, total 8 / 15 / 30 / 60 s, default 8 / 30. Time marks 1, 2, 4, 8, 15, 30, 60 s up to the total
  age, thinned to at least 40 px apart, the total age always labelled at the axis.
- **History** (`Cascade`): the 120 s exponential histogram is gone. Three tiers of absolutely aligned buckets (bucket j covers blocks j*S .. (j+1)*S - 1)
  with 8 / 32 / 128 blocks (23 / 93 / 372 ms) and 512 / 256 / 256 buckets (11.9 s / 23.8 s / 95 s), each tier its own ring fed by the block peaks (nothing
  merged, a finished bucket never changes, the unfinished newest one is read from the accumulator). A column reads the coarsest tier whose buckets are at
  most half a column wide (fallback to a coarser tier where the finer ring does not reach back), so the left end is reached at full display resolution after
  exactly the total age: the screen is full after 30 s (default) instead of 10 minutes. 4120 B per lane (was 1960 B), 37 KB for the nine lanes (was 17.6 KB)
  for 60 s at the largest scale instead of 120 s of blur; O(1) work per block (three adds and compares). EXTERNAL_RAM after the build: 98.8 % of 16 MB.
- **Smooth scrolling**: values are read at the exact age of each column against the block count at draw time (absolute bucket alignment, overlap weighted
  averages / interpolation between bucket centres, the unfinished bucket included), so the contour moves at a constant speed, not in bucket jumps.
- **Taps run while the Master module is open** (option b of three): `AmpView::begin()` from `cMasterParams::start` (idempotent), `forget()` from `stop`;
  `toggle()` only opens / closes the view; `update()` feeds the history every 40 ms and draws / refreshes only while the view is shown. The view therefore
  shows its history at once (up to the time since the module was entered). Cost: the taps are about 1.5 % of the block budget with eight tracks (one pass
  over 128 samples per channel per block); (a) always on would pay that outside the Master module too where nobody looks, (c) only while open (v0.4)
  is what left the screen empty on opening.
- Tests: `test_amp.cpp` (axis: linear for u <= 0.75 within 0.02 px, continuous slope at 75 %, monotone, slope never shrinks, total age = left axis, inverse,
  marks; cascade: every held bucket of every tier equals the brute force average / maximum, rings, reach, peak survival, memory; resample: oldest point at
  x = kX0 for every scale and column count, a short history starts at its exact x; `test_scroll`: a pulse edge moves 105.8 px in 600 blocks against 107.8
  expected, never a step back), `test_stages.cpp` device replay (40 ms jittery cadence, 64 value rings): after 5 / 10 / 20 s the left end is where the axis
  puts that age, after 30 s it is x = kX0 in every one of the 2184 following frames, nothing is ever left of the axis, the newest value is at the right
  edge in every frame, every stage <= 3840 FIFO bytes, nothing dropped. Changed as direct consequences of the owner change (equally strict, stated):
  the cascade test (new structure: exact per bucket instead of the merge invariants), the axis test (new formulas instead of the six-power blend), the
  device replay expectations (centre / 2 minutes replaced by the 30 s left axis), the jog scale count, the preview scenes. Previews (`make amp-preview`):
  `core/seqtest/build/preview/amp1 .. amp8`.

## W as built (2026-11-06, branch v037-wt, from seq-tracks 018dd73 = v0.3.6; original Tracker MK66 only)

**How Plinky does it (plinky_public, MIT, read only).** Plinky has no separate "wavetable mode": its one **Shape** parameter (`P_PWM`, "Shape" in the
UI) is bipolar. At 0 the voice plays four polyBLEP saws per string; **negative values** flip and shift the phase of two of them (the pulse, the sound
this mod already had as Shape 1..100); **positive values** (`pwm > 0` branch of `RunVoice`, `sw/Core/Src/plinky.c`) switch to the wavetable oscillator:
`pwm >> 12` picks the table (0..15) and `pwm & 4095` blends it linearly with the next one. 17 tables, `short wavetable[17][1031]` in
`sw/Core/Src/wavetable.h` (generated by a tool that is not in the repository; `wtenum.h` names the first twelve, the data has 17 real tables of
which only table 0 is a saw and table 1 a sine). Per table 9 **octave levels** of one cycle: 512, 256, 128 ... 2 samples, each + 1 guard point (= the
first sample), 1031 values; the level comes from the phase increment (`16 - clz(max(dphase, 1<<22))`, i.e. every octave of pitch halves the table;
level 0 is used below 86 Hz). Lookup: `phase >> shift` gives a 16 bit fraction, linear interpolation inside a table, then the two tables are blended
(`SMUAD` with the 12 bit blend weight). In wavetable mode one table oscillator replaces the pair of each side (the two phase increments are averaged).
Data size: **17 x 1031 x int16 = 35 054 bytes**. The tables are plain numbers (not procedural), so they cannot be generated at boot; they are `const`
data in **flash** (no EXTERNAL_RAM, no init, no static constructor; the SDRAM guard is not involved).

**What was built.**
* `plinky_wavetable.h` (new): the 35 054 numbers as `static const int16_t` in an inline function (one copy in the image), produced from Plinky's
  `wavetable.h` by `wavetable_import.py` (only the numbers are read; nothing of the clone is run). MIT notice kept (plinky_voice.h, NOTICE.md).
* **Shape range 0..229** (`plinky_params.h`): 0 = Saws, 1..100 = Pulse (**the v0.3.6 values, the same code path: bit-exact**, see the test), 101 + 8 k
  = table k exactly (k = 0..16), the 7 values between are the blend (`kWtSteps` = 8; 101 + 16 * 8 = 229 = `kShapeMax`). `plinky.bin` is still version 1
  and 24 bytes of payload; a v0.3.6 firmware reading a file with Shape > 100 takes the default for that value (documented, no loss for the other
  parameters); a v0.3.6 file loads unchanged. CC 70 still maps 0..127 to Shape 0..100 (unchanged sounds).
* Voice (`plinky_voice.h`, only the oscillator path and the parameter derivation): a third oscillator branch next to saws and pulse. Left = oscillator 0
  (pitch - detune), right = oscillator 1 (pitch + interval) like the pulse; mono renders the left only. The level per side per block comes from the
  louder end of the block's pitch ramp (so a rising glide never aliases); the table pair is blended in float; the table position is smoothed like the
  other parameters (a jump into the mode starts at the target) and the LFO "to Shape" swings it by +-4 tables at full depth. Inside a block the weight
  ramps linearly; when the position crosses a table the block renders the old and the new pair and crossfades them (rare, only that block costs
  twice). Back to saws or pulse the delayed saw samples are restarted at the phases (no stale-sample click). Output scaled so a +-16384 table is
  +-1.0 (a square table would be as loud as the pulse; loudness check below).
* UI (`seqPlinkyUiLogic.h` / `plinky_params.h` `shapeText`): the Shape cell reads `Saws`, `Pulse 37`, `WT3 Buzz` on a table, `WT3+50%` between two
  tables (nine characters at most, fits the 92 px cell). Names are by spectrum (Plinky's own are placeholders): Saw, Sine, Buzz, Soft, Reed, Glass,
  Pipe, Hiss, Hole, Bell, Pad, Vox, Warm, Mild, Comb, Duo, Hash. They are unverified by ear: **listening check in 11k**.
* The Shape morph (plan 012) is linear on the stored value, so a morph from Pulse 50 to WT10 passes the rest of the pulse range and then crosses the
  tables one step at a time (tested); the existing "parameter jumps to its extreme" click test now also covers Shape 40 -> 229 (its `max` grew).

**Measured on the host (`core/seqtest` `make test`, `test_plinky.cpp`: 28 119 checks, 0 failures; seqgen `make test` unchanged, 0 failures).**
* old Shape values: FNV hash of the 16 bit output of 11 scenarios (shapes 0, 1, 17, 50, 99, 100, mono / stereo, all three LFO targets, a shape sweep,
  note changes) equals the hash recorded with the v0.3.6 engine *before* the change (recorded first, then the code was written): bit-exact.
* lookup: 20 000 random phases / levels / weights against a double reference: error 1.3e-7; guard points, level offsets, the largest index of every
  level (never beyond the guard point, also for the pair of the next table), all values inside +-20000.
* mip selection: for all 128 notes the chosen level has no harmonic above Nyquist and the next richer level would alias; the low harmonics of
  levels 1..5 equal level 0 within 0.7 % of the table rms.
* alias (energy off the harmonics, same measure as the polyBLEP saw 0.0005): saw table 0.0000 (C2) .. 0.0030 (C8), all tables below 0.004 up to C4,
  worst of 42 cases 0.038 (the "Comb" table at C6). This is the image energy of the linear interpolation in the short top-octave tables (a 32 sample
  table has 16 harmonics), inherent in Plinky's tables; no harmonic folds back.
* no clicks: a sweep of Shape over all tables (one step per 3 blocks) has the largest step 786 against 2389 for the steady tables; jumps across 10
  tables 1332; mode changes wavetable / saws / pulse 738; a glide through all octave levels makes no step beyond any steady note (a hard level
  switch only changes the sharpness of the edge, it adds no transient; a level crossfade was written, measured not to help, and removed).
* stability: 108 combinations of table, blend, width, LFO target at extreme noise / drive / reso / detune / interval and edge notes: finite, inside 16 bit.
* loudness at the same note (rms): saws 1467, pulse 50 2918, table Saw 1941, table 9 1573.

**Size.** FLASH 899 792 -> **936 088 B** (+36 296: the tables 35 054 + 1 242 code), RAM unchanged (224 592 B, 85.68 %), EXTERNAL_RAM unchanged. Image end
(`arm-none-eabi-size` text): 0xDCC44 -> **0xE5A0C**; below 0xF0000 by **42 484 B**. The trace / amp work running in parallel needs "a little"; the
margin that remains is above the 20 KB asked. `make original` guard: "static constructors: no EXTERNAL_RAM access (ok)".

**CPU estimate (Cortex-M4F, 180 MHz; no hardware number yet).** Instruction count of the compiled sample loop of `renderVoice` (stereo, no table
crossing): saw pair 126, wavetable 130 instructions per voice-sample (the low pass gates are the same 43). The wavetable path has no `udiv` (the
saw pair pays one per cycle and per oscillator on a wrap, 2..12 cycles), but 8 flash reads (4 per side) where the saws read none: with the flash
cache hitting for sequential phases and a few misses at high pitch, the estimate is **-3 % .. +15 % of the saw pair per voice**, mono the same
ratio; the 128 sample block costs about 3.5-4 % of the audio budget per stereo voice either way (CPU readout in the PolySynth title). Host x86
ratio (4 voices): stereo 1.3x, mono 1.0x of the saw pair (x86 float conversions are slower than the M4's single cycle `vcvt`). The first number on
the device is the CPU readout with four held notes at WT6 against Saws.

**Not done / open.** (1) Table names and the loudness of the brightest tables have not been heard (listening check 11k). (2) CC 70 reaches only the
Saws / Pulse part (an extra CC for the table position would be one line). (3) The wavetable mode has no detuned pair per side (as Plinky): Detune and
Interval act like in the pulse mode. (4) A sidecar with Shape > 100 read by a v0.3.6 firmware shows the default Shape.
