# Plan 019 — one flexible PolySynth engine, three instances, Model:Samples-style multi-edit (owner 2026-11-06)

Owner: "The optimum for me would be to have 1 flexible synth engine and to be able to load 3 instances of it (in the
instrument list). Maybe extend our synth engine with some simple concepts, like 2 sub-octave square waves, some other
filter designs, and envelopes for them, more LFO waveforms … maybe borrow some concepts from the other synth engines as
well, so maybe a chorus also etc., to have a distinct sound but variations. Then since we have the same synth engine for
different instruments we can toggle between those and change parameters for one, but we can also hold Shift and dial a
parameter and then it adds the value to all other synth engine instances — basically the best performance workflow of
the Model:Samples."

## 0 — make room first (flash ~21 KB left, internal RAM 86 %, EXTERNAL_RAM 98 %)
Free flash (more -Os on control-rate audio code that isn't per-sample, dead code, data tables) and EXTERNAL_RAM
(SAMPLE_MEMORY_SIZE, developer guide) before adding features; keep the image < 0xF0000 with ≥ 16 KB margin.

### 0 as built (branch v039-room)
- **Flash limit corrected (coordinator, plan 018)**: the 0xF0000 rule came from a misreading; the working limit is now **image end < 0xFC000** (1,032,192 B), enforced by `platforms/mk66/check_image_size.sh` in the post-build step (prints end + margin, fails at the limit, warns below 16 KB margin). Keep the build lean anyway.
- **Freed 22,556 B of flash** (image end 0xEBFF8 = 961,912 B -> **0xE555C = 939,356 B**, margin to 0xFC000: **92,836 B**): (1) the whole MK66 platform (teensy core, FatFs, SDMMC, USB) at -Os except `3rdparty/Audio` (the I2S streams); (2) `effector` (offline sample editor), the seqgen generators (chordarp, mono, playhead), seqTracks / seqTracksCore / seqLauncher / seqSong / seqClips / seqCut / seqSidecar / scales and pad / keyboard / actions / mtTest at -Os (event-rate code, not per-sample; mtSequencer*, the audio engine, the DSP and seqFx stay -O2).
- **Word-wise memcpy / memset** (`platforms/mk66/source/fastmem.S`, own code): newlib-nano links byte loops (about 6 cycles per byte) and the Teensy `memcpy-armv7m.S` is empty in this build (assembled without -mcpu); every 256 byte block copy / clear in the audio interrupt gets several times faster (aligned path: ldm / stm, 16 bytes per loop; other alignments take the byte loop). tracker-ff's fastmem.S was NOT copied (provenance unclear). Not measured on hardware.
- **Not done, with reason**: the six double-to-float math call sites (sample-editor code, no CPU effect, small flash gain against 92 KB of room). **SAMPLE_MEMORY_SIZE is NOT reduced**: EXTERNAL_RAM is 98.27 % but 290 KB are free and plan 019 needs under 40 KB there (the chorus lines); lowering it would cut the resident sample pool (a project value, `max_memory`) for every user for nothing. If more is ever needed: 45 -> 41 * 256 KB frees 1 MB, see docs/developer_guide.md. Internal RAM unchanged (86.2 %); no big internal buffers are planned.
- Numbers: FLASH 934,796 B (89.15 %), RAM 225,976 B (86.20 %), EXTERNAL_RAM 16,486,232 B (98.27 %). Host: core/seqtest `make test` 0 failures (plinky 28,119 checks), seqgen 103,346 checks 0 failures.

## A — three instances
- Instrument list: **PolySynth 1 / 2 / 3** (three fixed slots, e.g. MIDI instruments 14/15/16 internally; v0.3.x
  projects with PolySynth on 16 keep it as instance 1 or as their slot — decide and migrate). Each instance: own
  parameters, own per-pattern snapshots + morph, own CPU readout share.
- **Shared voice pool + CPU governor** (concept from tracker-ff): e.g. 8 voices total across instances (max 4 per
  instance), steal by priority/age; a governor reduces voices when the audio block load gets high (measured), never
  glitches.
- PolySynth view (Play mode and Instrument page): **toggle between instances** with a clearly labelled key; the view
  shows which instance is edited. Pitch view per instance (colour per instance).
- **Shift + dial a parameter = relative change on ALL instances** (each keeps its own value, the same delta is added,
  clamped per instance) — the Model:Samples multi-edit. Clearly shown ("ALL" marker) while Shift is held.

## B — engine extensions (same engine for all instances)
- **2 sub-oscillators**: square, −1 and −2 octaves, each with a level.
- **Filter designs**: selectable type — the current Plinky low-pass gate, plus SVF low-pass / band-pass / high-pass
  (12 dB) and a 24 dB ladder-style LP if affordable; cutoff/resonance/drive shared; **filter envelope** (ADSR + amount)
  separate from the amp envelope.
- **More LFO waveforms**: sine, triangle, saw up/down, square, sample & hold, smooth random; LFO targets as now + filter.
- **Chorus**: per instance, cheap (one modulated delay line per side, EXTERNAL_RAM raw storage), mix + rate/depth.
- Borrow concepts from tracker-ff engines where cheap (see plan 018), e.g. drive/fold shapes.
- All new parameters in the section UI (new sections Sub, Filter env, Chorus), morph-able, saved in plinky.bin
  (versioned; old files load, older mod versions read what they can).
- CPU: keep the per-voice cost close to today's; sub-oscillators and SVF ~cheap; ladder optional.
