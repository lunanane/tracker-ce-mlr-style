# Plan 009 — Plinky synth voice on the original Tracker + CPU savings (owner 2026-11-05)

Owner: "Let's do it, and let's try to optimise the Plinky code even more for low CPU usage on the way, with 4 voices
max; also let's identify if there's any CPU saving in the current Tracker code that can help after all."
Target: ORIGINAL Tracker (MK66) only.

## S — Plinky synth engine (spike → instrument)
- Enable the CE synth framework (`core/synth`, `Synth.cpp`, registry, engines; today `-DFEATURE_SYNTH` only on IMX)
  on MK66, and add a **Plinky** engine: the base synth voice of github.com/plinkysynth/plinky_public (software MIT —
  keep the notice; credit, not a product name). No sampler/arp/sequencer/effects of Plinky — just the voice
  (oscillators, noise, filter, envelopes, LFO/mod as far as it is part of the voice).
- **4 voices max**, polyphonic, played by a Tracker track (and by our sequencer types) like the Chiptune engine on IMX.
- Optimise for CPU on the way: block processing, fixed-point or single-precision float with the M4F FPU, no per-sample
  divides/transcendentals (tables, polynomial approximations), shared LFOs, skip silent voices, control-rate
  parameter updates, oscillator anti-aliasing chosen for cost (polyBLEP / table), consider an internal rate below
  44.1 kHz only if it sounds acceptable.
- **CPU readout** on the instrument page (average + peak share of the audio block, like Reverb CPU), so the owner can
  measure 1–4 voices with a normal project.
- Flash: image must stay < 0xF0000 (check the last `:02000002` record); report cost; free more space with -Os of
  further non-audio code if needed.

## P — CPU savings in the existing Tracker code (audit, then safe changes)
Find where the audio interrupt and the main loop spend time on MK66 (sample voice mixing, interpolation, envelopes,
filters, FX chain incl. our delay bandpass / reverb types, master processing, display/SD work competing with audio),
rank savings by size and risk, and implement only the safe ones (bit-exact or inaudible) after the owner sees the list.

---

## S as built (2026-11-05, branch v03-plinky)

**Decision: no CE synth framework on MK66, a minimal engine hook instead.** The framework (`core/synth/Synth.cpp`, `registry.cpp`,
engines) cannot be enabled on the Original Tracker: it is only a thin layer on `libPolySynth.a` / `libPolyDSP.a` (proprietary,
precompiled for the Cortex-M7 of Tracker+/Mini, `synth/lib/LICENSE-PROPRIETARY.txt`), it needs `std::function`, `std::map`, shared
pointers, a patch store on the SD card, a MIDI adapter and `kSynthParts`-sized mixer inputs in the audio engine, mixer RMS taps, FX
routing, 3 synth instrument slots in the instrument list / pattern editor / title bar / file manager (about 40 `#ifdef FEATURE_SYNTH`
sites) and about 100 KB+ of flash. The image has 37 KB left. So the engine is written as a small independent module:

- **Engine** `core/libs/Audio/PlinkyVoice/plinky_voice.h` (header only, no Polyend includes, host-tested) + `plinky_params.h`
  (parameter table, sidecar `plinky.bin`), **audio stream** `core/libs/Audio/synth_plinky.{h,cpp}` (2 outputs, DWT cycle counter),
  **glue** `core/source/seqPlinky.{h,cpp}` (MIDI routing, values, sidecar, deferred write like `seqfx.bin`), **UI** three pages
  under Master > Limiter > "Plinky" (`gMasterParams.cpp` / `fMasterParams.cpp`), enabled by `-DFEATURE_PLINKY` for MK66 only.
- **How a track plays it**: the voice owns one MIDI channel (parameter Channel). `sendMidiNoteOn/Off`, `sendCC` and `sendPitchbend`
  (the single choke point of the stock sequencer, our Chord/Arp / Mono tracks, clips, pads, external MIDI keyboard) hand
  non-synth messages of that channel to the engine instead of the MIDI outputs. MIDI instrument N = channel N, so every stock
  feature of a MIDI instrument (velocity, note length, chords, Send CC FX, pitch bend FX) works on it. Channel 0 = off (default).
  No change of the instrument list, the pattern format or the project struct.
- **Audio graph**: one dry input (tracks mixer left / right, input 8) and two send inputs (reverb and delay pre-FX mixers,
  inputs 10 / 11, gains from the Reverb / Delay parameters). `AudioMixer<9>` and `<12>` are instantiated (`mixer.cpp`).

**What of Plinky was ported** (`RunVoice`, MIT, notice kept in the file, `NOTICE.md` section 11): the four-oscillator voice with
pitch offsets `(i-2)*detune` and `interval` on the odd ones (left pair 0/2, right pair 1/3), the integer-phase polyBLEP saw, the
pulse made of two saws with a phase offset, the noise source, the two-pole "low pass gate" recursion whose coefficient follows the
envelope (with the 0.999 damping), the block-rate envelope and the per-block linear ramps of volume, noise and phase increment
(quadratic phase = linear glide). Not used: sampler / grains, wavetable shapes, arpeggiator, sequencer, effects, touch input.
Written for the Tracker: voice allocation and stealing, lock-free event queue, exp2 polynomial, LFO, ADSR times, soft clip, parameters.

**Parameters (21, 0 .. 100 unless noted)**: Channel (0 .. 16), Voices (1 .. 4), Level, Reverb, Delay, Glide, Width (0 = mono),
Shape (0 = detuned saws, else pulse), Detune, Interval (-12 .. +12), Noise, Cutoff, Reso, Drive, Attack, Decay, Sustain, Release,
LFO rate, LFO depth, LFO to (Pitch / Shape / Cutoff). Velocity scales level and brightness. CC map for automation: 1 vibrato depth,
5, 7, 70 .. 77, 91, 93 (see the test list). Saved in `plinky.bin` (version 1, 24 byte payload, CRC); a project without it = Channel 0.

**CPU design**: block processing (128 samples = the audio block), float single precision on the FPv4 FPU, phase accumulators in
integers; no divide or transcendental per sample (one integer divide per saw cycle in the BLEP wrap, the exp2 polynomial at
control rate only); parameters read once per block, smoothed per block (about 12 ms) and ramped linearly inside the block (no
zipper noise, no clicks); one shared LFO; idle voices cost nothing, an idle engine allocates no audio block and costs a counter read;
Width 0 renders one oscillator pair and one filter (about 60% of the stereo cost); Voices limits the allocation; the sample loop
is one function with per-block constant branches (4 copies of the loop were 1.5 KB larger for 2 to 3% less CPU).

**Numbers**
- Flash: **+10,192 bytes** (baseline image end 0xE6EE8 -> **0xE96B8**; last `:02000002` record unchanged `E000`, i.e. below 0xF0000;
  26,952 bytes left for the parallel HYPER_RASP work). Biggest parts: sample loop 2.4 KB, control rate (`derive`, events, CC) 1.5 KB,
  glue + sidecar 1.4 KB, UI about 1.2 KB, `Engine::render` 0.5 KB. RAM: +1,920 bytes (the engine with its two 512 byte buses).
  Control-rate code, the sidecar glue and the UI are built with -Os; the sample loop is -O2.
- CPU, **estimate** (no hardware measurement possible here): the generated Cortex-M4 code of the sample loop is about 100 to 120
  instructions per stereo voice sample and about 70 for mono (static count of the hot path in the disassembly), i.e. roughly
  **130 to 160 cycles per stereo voice sample, 80 to 100 mono**. At 44.1 kHz and 180 MHz that is about **3.5 to 4% of the CPU per
  stereo voice** (about 2.3% mono); 4 stereo voices about 14 to 16% (+1% for the block overhead: control rate maths, clear,
  soft clip and 16 bit conversion of the two buses). The host build of the same code costs 12 to 19 ns per voice sample at -O2
  (printed by `test_plinky`), which says the structure is fine but is not an M4 number. **The CPU readout on the page
  (average + peak, DWT cycle counter) is the real measurement: please report 1 / 2 / 3 / 4 voices.**
- Host tests (`core/seqtest/test_plinky.cpp`, in `make test`): 4,829 checks: parameter table and sidecar (round trip, CRC, range
  fallback, newer version), exp2 accuracy and measured pitch of rendered notes, stability over the whole parameter grid (shape x
  reso x cutoff x width, all LFO targets, 5 notes at once, max noise / drive / detune: finite, bounded, no NaN), silence / idle
  (voice freed, states exactly 0, no denormal during the longest release, sustain 0 frees the voice), voice stealing (oldest,
  releasing first, same note re-uses its voice, Voices limit, 4000 random events), no clicks (largest sample step when any of 9
  parameters jumps to its extreme, when a note starts, when a voice is stolen, first and last block ramp from / to zero),
  anti-aliasing (energy off the harmonics 0.05% for the polyBLEP saw against 5.8% for a naive saw at C7), event queue and CC map,
  determinism, level, brightness vs cutoff, vibrato.

**Risks / open points**: (1) untested on hardware: the sound itself (levels, envelope feel, default patch) was only checked by
measurements; the CPU numbers above are estimates. (2) The voice is on one MIDI channel only; that channel no longer sends MIDI
(by design). (3) The sample loop runs from flash; if the pk value shows spikes, the loop can be moved to `AUDIO_CORE_RAM` (about
2.4 KB of RAM) or the 4 voices reduced by default. (4) Not built here: Tracker+ / Mini (`FEATURE_PLINKY` is off there; the shared
edits are guarded). (5) The page is on the limiter screen because the Master page and the instrument page have no free slot;
a nicer place needs a UI decision. (6) A mode change Shape 0 to 1 while a note sounds can tick once. (7) The voice is not on the
track faders / mute / solo / stems; its sends are separate (page 1).
