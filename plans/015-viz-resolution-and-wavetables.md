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
