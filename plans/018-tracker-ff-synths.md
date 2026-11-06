# Plan 018: what to borrow from tracker-ff for the one flexible PolySynth (read only)

Assessment 2026-11-06, branch `seq-tracks` (tag `seq-v0.3.8`, image end 0xEAD78 = 961,912 B). Refocused after the owner's
direction in plan 019: no wholesale engine import; keep ONE PolySynth engine with 3 instances and extend it (2 square sub
oscillators, filter types LPG / SVF LP-BP-HP / ladder + filter envelope, more LFO waveforms, chorus, shared voice pool + CPU
governor). Nothing was built or run. Sources: `github.com/sbialy/tracker-ff` `main` `ce14e1e` (release `tracker-ff-v1.1.0`),
cloned to the session scratchpad with `polyend/tracker-ce` as a second remote; release `.ptf` read as data only. Figures are
tracker-ff's README unless marked "estimate" (mine, unbuilt, from instruction counts) or "measured here".

## 0. Summary

- Everything worth borrowing is small, self-contained header code, Apache-2.0 (OpenTracker) or MIT (opentracker-synths /
  -fx): governor (87 + 80 lines, about 1.1 KB), SVF (zero-delay, 20 lines) and ladder (25 lines), LFO shapes (12 lines),
  BBD chorus (189 lines, 512 B RAM per instance), Dirt shapers (60 lines). None of it needs their framework.
- Their framework is NOT worth importing: about 75 to 85 KB of flash (estimate), mono output, patch-bank machinery, and 41
  files that conflict with ours. Their 16 engines cost 199 KB together; we only read them for technique.
- tracker-ff's release image ends at **0xFBB90 = 1,031,056 B** (measured here, highest address of the downloaded `.ptf`), cap
  0xFF000, "highest image known to boot" 0xFC2AC. Our rule is 0xF0000 ("v0.2 at 0xF04B0 was refused by the updater", commit
  `b5041e6`). The facts disagree; a device test of a padded image would move our headroom from 21 KB to 70+ KB. Worth doing before
  plan 019 sizes the new voice code (each item below is 0.5 to 3 KB, so 019 fits in 21 KB if it stays disciplined).

## 1. Borrow list (function, source, size, cost, license)

All paths are under `synth-open/` of tracker-ff. "Flash" is my estimate of -Os code for the piece, except where tracker-ff
gives a number.

| # | Piece | Source | Size | Cost | License / notice |
|---|---|---|---|---|---|
| 1 | **Voice pool + steal order** | `open_synth.cpp` (pool in the shim, not a separate file) | pool logic ~150 of its 787 lines; `Voice` 168 B, engine state 128 B, slot scratch 384 B | free voice, else quietest released voice, else oldest; with the pool full a new note takes the best victim over all slots (a voice in another slot fades over its last block); with 1 voice a new note reuses the held voice (legato/glide) | tracker-ff own code (tracker-ce MIT + clone restriction); re-write instead of copy, it is trivial |
| 2 | **CPU governor** | `ot/audio/governor.{h,cpp}` + `cpu_meter.{h,cpp}` | 110 + 87 + 80 + 41 lines; ~1 to 1.5 KB flash, ~80 B RAM, integer compares only, runs once per block after render | see 1.1 | OpenTracker, Apache-2.0, unmodified in tracker-ff (provenance line only) |
| 3 | **Zero-delay SVF (LP/BP/HP)** | `ot/audio/va_dsp.h` `svfCoef/svfStep`, `tanPade`, `svfH` | 20 lines; 2 floats state | ~12 flops per sample, ~20 to 25 cycles (estimate); coefficient once per block: `g = tanPade(pi*fc/fs)`, `h = 1/(1+g(g+k))` or one Newton step `svfHNext` | Apache-2.0 (OpenTracker, 3.x line `v3_fixes` `789d55a4c5e8`) |
| 4 | **Ladder 4-pole ZDF** | `va_dsp.h` `ladderStep<kClip>`, `tanhR`, `clip3` | 25 lines; 4 floats state | 4 trapezoidal one-poles + rational tanh on the loop input (one divide) ~60 to 80 cycles per sample (estimate) | Apache-2.0 |
| 5 | **Chamberlin SVF, 2x oversampled** | `ot/audio/dsp.h` `Svf` (PJRC AudioFilterStateVariable port) | 40 lines; LP/HP/BP via template | ~30 cycles per sample (estimate), same family as our stock `filter_variable` | Apache-2.0 + embedded PJRC MIT notice (Paul Stoffregen) in `dsp.h`: keep it |
| 6 | **LFO waveforms** | `ot/audio/engines/vap.cpp` `lfoShape` (tri, parabolic sine, ramp up/down, square, sample&hold) | 12 lines | per block, not per sample, a few cycles; sine is `y=4u(1-|u|)` then `y+0.225(y|y|-y)` (no libm) | Apache-2.0 (VAP is OpenTracker's) |
| 7 | **BBD stereo chorus** (the JU6 one) | `ot/audio/bbd_chorus.h` (+ `audio_tables.h` for `kSampleRate/kBlock`) | 189 lines; ~1.5 KB flash; **512 B int16 line + ~48 B state per instance** (tracker-ff: 1,536 B for 3 slots) | per sample: three one-pole low-passes, soft clip, 1 line write, 4 reads, 2 lerps, gain ramps ~60 to 70 cycles per sample = ~8.5 k cycles/block (about 1.6 % of 522,240) (estimate); skips work when Off and faded out; JU6 4 voices with chorus 21 / 24 % total | Apache-2.0 (header says "textbook technique", our own design), copyright via OpenTracker 3.x |
| 8 | **Dirt shapers** | `ff_dirt.cpp` (`tanhU`, `fold`, `clamp1`, quantiser, S&H, 1-pole tone) | ~160 lines (+ `ff_dirt_ctl.cpp` 122 for presets); 2,200 B flash in total | whole master insert 1.6 to 2.6 points of CPU (emulated); the shapers alone per sample are 5 to 15 cycles | MIT, copyright 2026 Marcin Spoczynski (opentracker-fx `6e5a7da`); tracker-ff's port keeps that header |
| 9 | **Cheap math** | `va_dsp.h` `fastExp2`, `blockCoef`, `onePole`, `flush`, `tanPade`; `chip_dsp.h` `fastLog2` | ~40 lines | replaces `powf/expf/sinf` (newlib float libm is ~380 cycles per `powf`; CHIP's first device run went red because of per-sample `powf/sinf/fmodf`) | Apache-2.0 |
| 10 | **Envelope contour (RC curves, ADSR per block)** | `va_dsp.h` `Contour/setContour/step` | ~40 lines | per block | Apache-2.0 |
| 11 | **Patch ideas** | `open_patchio.cpp`, `open_patchdata.h` | codec 501 lines (do not take) | see 1.4 | tracker-ff own |

### 1.1 Voice pool and governor, algorithm

- Budget B (our case: pool across 3 instances, default 4, max 8 in tracker-ff; their `kMaxBudget` is 16). Cap C starts at B.
  Block load L = cycles of the last audio block in permille of 522,240 (128 frames at 44,117.6 Hz, 180 MHz); A = average of
  `CpuMeter` (about 32 blocks). tracker-ff takes L from the existing `Profiler::stopMeasuringAudio` DWT readout (our Profiler
  already has the same function; plan 009 P added its own readout).
- Per block after render, only if a synth voice sounds and the 250 ms hold-off (86 blocks) is over:
  - overrun L >= 1000: C := sounding - 1.
  - risk: one block >= 980, or a second >= 950 within 1 s (345 blocks): C := max(F, min(C, sounding) - 1), floor F = B - 1.
  - average A >= 880 ("deep"): C := C - 1 (down to 0); A >= 800: C := C - 1 but not below F.
  - every cut starts the hold-off and steals voices until sounding <= C (steal order: release phase quietest first, then
    quietest, then oldest; fade over 64 frames / the last block, no click).
  - restore: A < 650 and every block < 900 for 690 blocks (2 s) in a row: C := C + 1, restart the count.
- Hysteresis is wide on purpose: their first version cut on any block >= 90 % and lost three of four voices on a normal song.
  Off during export (`setOffline`), reset on Stop and project load. Sample voices are never cut.
- For us: with the voice pool shared by 3 instances this is the simplest part to borrow. Estimate: 1 KB flash, no RAM worth
  naming, one call at the end of `TrackerImpl::updateAudioBuffers`. We would feed it the same DWT cycle delta as the
  readout and an `IVoicePool` of our pool (`sounding()`, `stealOne()`).

### 1.2 Filter notes

- tracker-ff's engines use three flavours: SVF (zero-delay trapezoidal), "OB" (Oberheim-style SVF with its own damping) and
  "MG" (Moog ladder), times 12/24 dB poles, in VAP's 15 filter types (`kFTypes`). VAP with those 15 types costs 13 to 19 %
  for 4 voices, i.e. a filter type is not what makes an engine expensive.
- For plan 019's list: LPG (we have it), SVF LP/BP/HP = item 3 (one `svfStep` gives all three outputs, selecting is free), ladder
  = item 4. Both need a per-block coefficient (`tanPade`, one divide) and ramping `g,k,h` across the block is where bugs live:
  tracker-ff solves it with `svfHNext/svfHTrack` (one Newton step of 1/D per sample instead of a divide) and a self-oscillation
  guard `svfKMin(g) = -0.004/g`. Take those too, they fix the instability when g moves a lot in a block.
- Filter envelope: their contour is the per-block RC ADSR of item 10; cutoff = `cutoffHz(cutoff, oct)`, with the envelope in octaves
  (`fastExp2(oct)`), which is the right unit for key tracking too.

### 1.3 LFO, chorus, drive

- LFO: item 6, phase in cycles 0..1 per block; their S&H keeps one held value per cycle; "Free / One Shot / Note" retrigger modes
  exist (VAP). Rates are tempo-synced through a table of quarter-note divisions (`LFO Sync`).
- Chorus: item 7 is a mono-sum BBD, two taps with opposite-phase triangle LFO, modes I (0.5 Hz, 2.0 to 5.0 ms) and II (0.8 Hz,
  1.5 to 5.5 ms), Tone = 3 one-poles at 6 to 12 kHz, one int16 line of 256 frames per instance. One chorus per instance (3 x 512 B)
  is the memory cost; processing only when a voice or a tail is active (`kTailFrames` = line + block). A stereo, 2-mode chorus
  at about 1.6 % of CPU and 1.5 KB flash is cheaper than anything we could invent from scratch.
- Drive/fold: item 8: pre-gain `1 + 15 d^2`, shapers Soft (rational tanh `u(3+u^2)/(1+3u^2)`), Hard (clip), Fold (triangle fold,
  period 4), Asym (biased tanh + 10 Hz DC block), make-up `1/(1 + 0.5d)`, then optional bit quantiser, sample&hold, tone low-pass.
  Per voice we want only the shaper (Soft/Fold/Asym); tanhU and fold are 10 lines.

### 1.4 Patch file format ideas

- tracker-ff reads/writes the stock CE `.patch` (protobuf: field 1 engine id, field 6 repeated `ParamValue{fixed32 float}`, field 7
  four names, field 9 macros, up to 84 params, 8 macros x 6 targets, 8 KB max file). Values are floats in engineering units,
  absent = default (NaN), unknown fields kept as raw bytes and written back (forward compatible); engine id numbers keyed
  by a canonical table so projects survive a build with other engines.
- Take as ideas, not code: (a) store named floats in engineering units with "absent = default", so adding a parameter never
  breaks old patches (our `plinky.bin` is a fixed 24-byte payload with CRC and version; plan 019 adds parameters, so move to
  tag/value records); (b) keep unknown tags verbatim; (c) a Patches/<ENGINE>/ folder per engine and a 16-slot bank per part
  are what they built, 25 of their last 30 commits are bank hardening (replace-with-recover `.tmp/.bak`, bank copy at project open);
  not worth it for us, we have the project sidecar and morph store.
- Not compatible with sharing Polyend factory patches unless our parameter list matches an engine's `ParamDef`; do not chase this.

## 2. How they freed flash and RAM on the MK66

No linker memory-layout change, no `SAMPLE_MEMORY_SIZE` change, no feature dropped. Linker only gets an `ASSERT(image <=
0xFF000)` (we can add the same with our own limit). Everything is the `TRIM=1` build, which frees about 157 KB of the clean
upstream image (902,032 B, ends 0xDC390) in six steps:

1. `-specs=nano.specs -u _printf_float` at link (full newlib printf 6.9 KB, scanf 6.0, strtod/dtoa 13, libstdc++ demangler
   28 KB). **We already do this** (`platforms/mk66/makefile:75`).
2. Float instead of double math at 6 sites (`cosf/tanf` in `mtEffectEQ`, `expf` in `mtEffectOverdrive`, `floorf` in `fSongEdit`, `powf`
   in `control_sgtl5000`): drops pow/exp/fmod/remainder/trig kernels. Not sized separately; check our map (estimate: a few KB).
3. `-Os` for the UI, file manager, display, exporter, effector, `source/`, **the whole MK66 platform** (teensy core, FatFs, SDMMC,
   USB, I2S) and 8 once-per-event AudioEngine files (-3.5 KB). **We already have most of this; our platform is still -O2**
   (`platforms/mk66/makefile:50`) and `effector` is -O2: the two remaining candidates.
4. `std::map` for the instrument registry, PERC icons only with PERC (n/a for us).
5. `fastmem.S`: word-wise `memcpy/memset` (+98 B). Under nano the byte-loop `memcpy` of a 256-byte audio block costs 2,063 cycles
   against 295; they measured 8 playing tracks costing about 8 points of CPU more than stock because of it. **We link nano and
   have no `fastmem.S`** (nothing in `platforms/mk66/source`): check our map; if it is nano's `memcpy`, adding this file is the
   cheapest CPU win in this whole document, independent of synths.
6. Not done by them (levers left): smaller printf (~7.5 KB), font recompression, LTO.
RAM: they moved reverb buffer E (32 KB) into SRAM (`REVERB_SRAM=1`) and keep buffer E2 and the patch bank (43 KB) in SDRAM; their
experiment with both buffers in SRAM left 57 KB of heap+stack and was judged too little. **We keep both (54.8 KB) in SRAM and
have about 35 KB for heap+stack**: before adding three chorus lines (1.5 KB), a governor and wider voice state we should keep the
margin in mind; moving `sramReverbBuff2` (22 KB) back to SDRAM costs about 3 points of CPU. SDRAM guard: their
`check_static_init.py` refuses an image whose static constructors touch SDRAM (same rule as ours).

## 3. Licenses and notices

- **Apache-2.0** (OpenTracker: governor, cpu_meter, `va_dsp.h`, `dsp.h`, `chip_dsp.h`, `bbd_chorus.h`, `audio_tables.h`, VAP LFO shapes):
  ship the license text; keep the provenance line each file carries (4b: changed files carry a notice); upstream has **no
  NOTICE file**, so nothing to propagate under 4d; `dsp.h` embeds a PJRC/Teensy MIT notice (keep it). If we only re-implement the
  algorithm from the description (e.g. the governor law, a 12-line LFO) no license obligation arises, but crediting is good practice.
- **MIT** (opentracker-synths JU6 etc.; opentracker-fx Dirt): "Copyright (c) 2026 Marcin Spoczynski" plus the permission text.
- **tracker-ff's own code** (shim, PHZ): tracker-ce license (MIT + Wholesale Commercial Clone Restriction); our repo already
  carries it. Our additions are PolyForm Noncommercial 1.0.0 with a Required Notice (`seqgen/LICENSE.md`); Apache/MIT code can sit
  inside it.
- To do on adoption: add a `licenses/` folder (we have none) with `LICENSE-OpenTracker-Apache-2.0.txt` and
  `LICENSE-OpenTracker-synths-MIT.txt` / `-fx-MIT.txt`, items in `NOTICE.md` (tracker-ff items 10, 11, 17 are a template), and ship
  `LICENSE`, `NOTICE.md`, `licenses/` in the release zip.

## 4. If anything bigger is ever wanted (brief)

- Their 16 engines (flash B, 4-voice CPU % default/worst): JU6 6,108 21/24; FM4 7,291 33/33; ACD 9,374 13/25; B-Type 9,740 14/22;
  D-Type 9,868 27/43; A-Type 9,989 27/37; CS-Type 10,507 47/75; CHIP 10,685 58/82; FAT 12,952 14/23; Arc-Type 13,939 33/42;
  T-Type 14,025 11; WTFM 14,793 16/52; PHZ 15,154 17/24; 8-Type 15,474 24/61; VAP 15,514 13/19; PERC 23,655 14. Sum 199,068 B.
  None of the OpenTracker 3.x engines has been heard on a device (their statement); CPU is emulated without flash wait states.
- Framework import (their `synth-open` + CE Synth screens) costs about 75 to 85 KB (base 824.8 KB without engines minus
  trimmed stock ~745 KB, estimate), outputs mono, and 41 files conflict with ours (build files by hand; the idle-CPU audio files:
  take ours, plan 009 P did the same work; 5 to 8 small hand merges in sequencer/UI files; `Synth.cpp`, `SynthEditor`,
  `ChiptuneSynthVoice` do not conflict). Merge base with tracker-ce: `7fd6c2c` (upstream is 31 commits ahead; ours `9b9a13b`, 8
  commits behind that base). Not recommended given plan 019.

## 5. Recommendation

1. **Borrow in this order**: (a) governor + cpu_meter + our own pool (items 1, 2; 1 to 1.5 KB); (b) SVF + ladder with the
   `svfHNext/svfKMin` guards and `tanhR` (items 3, 4, 9; ~2 KB); (c) LFO shapes (item 6, 12 lines); (d) BBD chorus (item 7;
   1.5 KB, 512 B x 3, ~1.6 % CPU when on); (e) Soft/Fold/Asym shaper for Drive (item 8, tiny).
2. **First, free the cheap things**: `fastmem.S` (CPU), platform and `effector` at -Os (flash), float math sites; and settle the
   flash limit with one device test (tracker-ff's `.ptf` or a padded copy of ours ending near 0xF8000).
3. Keep parameter storage extensible (tag/value, absent = default) when plan 019 grows the 21-parameter list.
4. Ship the license texts and `NOTICE.md` entries with the first release that contains borrowed code.
