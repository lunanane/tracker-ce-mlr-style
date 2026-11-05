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

## P — CPU audit (report)

Static audit, 2026-11-05, branch `seq-tracks` at `cd36cf7`. `make original` (-O2, arm-none-eabi-gcc 10.3) was built in a detached worktree and
read with `arm-none-eabi-objdump`. **No hardware numbers**: every cycle count is an instruction count of the -O2 disassembly plus an
estimate for memory stalls, good to roughly +-30 %. Calibrate with the two readouts the device already has: Config > audio usage
(`Profiler::startMeasuringAudio`, `platforms/mk66/source/Profiler.cpp:217`) and the Reverb CPU readout
(`effect_polyverb.cpp`, `cpuAvg` / `cpuPeak`). Nothing was changed; this section is the only edit.

### P.1 Where the time goes (facts)

- **Budget**: one block = 128 samples at 44 117.6 Hz = 2.90 ms = **522 000 cycles at 180 MHz; 1 % = 5 200 cycles.**
- **Audio ISR** (`TrackerImpl::updateAudioBuffers`, `core/source/Tracker.cpp:178-202`, called from the I2S DMA ISR at priority 5,
  `platforms/mk66/config/interruptsConfig.h:20`): `midiUpdate()`, `sequencer.tick()` (our `seqEngine.blockTick/advanceUs`),
  `engine.update()` (control rate: `playerEngine::update` x 8, `core/AudioEngine/mtAudioUpdateHandle.cpp:7`), then the whole
  AudioStream graph (`AudioStream::update_all`, `AudioStream.cpp:~330`). One flat ISR; SDHC (6) and USB (7) cannot preempt it, the
  input DMA (4) and the MIDI UART (0) can. The main loop (`TrackerImpl::update`, `Tracker.cpp:152`: display, pad LEDs, file manager,
  our `seqTracksUpdate`) only gets what the ISR leaves, and shares the SDRAM bus.
- **MK66 graph per voice** (`AudioVoice.cpp:5-12`): `AudioPlayMemory` (+ amp envelope in `envelopeUpdate`) -> `AudioBitDepth` ->
  `AudioFilterStateVariable` -> `AudioAmplifier` -> `MonoToStereo` (+ `AudioAnalyzeRMS` and the limiter-sidechain mux on the
  amplifier output); the filter output also feeds the reverb and delay pre-mixers (`mtAudioEngine.cpp:337-357`). Per project: 8 voices,
  2 pre-FX mixers (10 inputs), 2 track mixers (8), 2 track+sends mixers (4), delay, reverb, 2 limiters, final mixers, output stream,
  line-in chain (stereoToMono, record RMS, 2 record queues, 2 gates, USB-input zero stream, 2 USB amps), 14 RMS meters in total.
- **Memory**: samples, the 53 KB of stock-reverb delay lines (**`internalRamReverbBuff` is `__NOINIT(EXTERNAL_RAM)` on MK66**,
  `effect_polyverb.cpp:30-32`, despite its name), the Plinky ring, the delay lines and the streaming ring buffers are all in SDRAM.
  SDRAM is on FlexBus/SDRAMC at **90 MHz, 16-bit port, CAS 3** (`fsl_sdram.cpp:76,80`, `sdram.cpp:37`) and is **not cached**: the
  only cache the firmware configures is the LMEM *code-bus* cache (`mk20dx128.c:748`, `LMEM_PCCCR`); the K66 header in the repo has no
  system-bus cache registers and SDRAM (0x70000000) is on the system bus. A random 16-bit read costs about 20 cycles, a write about 10.
  Internal SRAM has ~100 KB free (`RAM: 154 920 B used of 256 KB` in this build).
- **Code runs from flash**: `AUDIO_CORE_RAM` (`core/config/config.h:9`) puts 36 KB of audio code in `.audio_ram_functions`, but
  `mk66fx1m0.ld:54` links that section into FLASH (only the IMX build puts it in fast RAM). Tight loops are fine in the code cache; the
  large branchy per-block glue (control updates, graph traversal) is not.
- **Build flags** (`core/makefile:42-46`): -O2 for all audio code (-Os only for UI; correct), no -ffast-math, `-ffreestanding -fno-builtin`
  (so `abs`, `fabsf`, `sqrtf` stay real calls, see #8 and #16). DSP intrinsics are already used in the gain / mix / envelope / filter / RMS
  loops (`smulwb`, `ssat`, `pkhbt`, `smmulr`, `smlal*`); not used in the sample-playback loop, the limiter, the delay and both reverbs.
- **Float vs double**: `-Wdouble-promotion` finds no per-sample double in the DSP loops. Soft-double (no FPU for double) sits in
  control-rate code that runs *per block*: `envelopeSyncTrackerSeq` (12 soft-double ops), `Sequencer::tick` (9), `getTimerPeriod_us` (4),
  `envelopeMilliseconds2count` via `SAMPLES_PER_MSEC` (a `double`, `effect_envelope.cpp:35`).

### P.2 Estimated cost per block (cycles; budget 522 000)

| Object (MK66) | cycles / block | % budget | note |
| :--- | ---: | ---: | :--- |
| Sounding voice, resident sample, filter off | ~22 000 | 4.3 % | play loop 7.4k + SDRAM reads 5.6k, envelope 1.4k, amp 0.9k + 0.45k copy, mono->stereo 2.7k, RMS 0.3k, track mixers 1.9k, alloc/release 0.6k, control update ~1k |
| ... with the filter on | ~28 000 | 5.3 % | `update_fixed` ~40 cycles/sample = 5.1k, + 0.45k copy |
| **Idle voice** (envelope idle, filter off) | **~5 500** | **1.05 %** | the silent zero block still runs the whole chain (finding 2) |
| Streamed voice (SD), on top | ~+10 000 | +2 % | out-of-line `VoiceStreamManager::getSample` call for every sample read (`play_memory.h:337`) |
| Limiter x2 (L+R) | ~23 000 | 4.4 % | ~90 cycles/sample incl. a `vdiv` (`effect_limiter.cpp:154-190`) |
| Delay (`process`) | ~25 000 | 4.8 % | ~95 instr + 6 SDRAM accesses per sample, all 3 lines (`effect_shortDelay.cpp:203-295`); about +10k with the bandpass on (ping-pong: 2 SVFs + `vdiv` per sample) |
| **Stock reverb** (`Reverb::Process`) | **~100 000 (+-30k)** | **~19 %** | ~300 instr + 16 SDRAM reads + 13 writes per sample, never skipped |
| Plinky ring reverb | ~60 000-80 000 | 12-15 % | 24 reads + 16 writes per sample, has a silence bypass |
| Line-in chain (always on) | ~5 000 | 1 % | de-interleave, USB zero stream, RMS, queues, gates |
| 14 x RMS meters | ~4 200 | 0.8 % | always accumulate, displayed rarely |
| Control updates in the ISR (8 x `playerEngine::update`) | ~8 000-30 000 | 1.5-6 % | +~1k per active LFO-sync envelope (soft double) |

Typical worst case (8 voices sounding, 2 with the filter, delay + stock reverb used, limiter): 8 x 22k + 12k + 23k + 25k + 100k + 25k = ~360k =
**~70 %**. That is why four more voices do not fit without savings of the order of 10 % or more.

### P.3 Ranked findings

Risk: **E** = bit-exact (same output samples), **I** = inaudible (not bit-exact, below any audible threshold), **A** = audible / changes the
sound. Effort: S = hours, M = a day, L = several days. Saving is per audio block.

| # | Finding and change | Saving | Risk | Effort | Where |
| :-: | :--- | :--- | :-: | :-: | :--- |
| **1** | **Stock-reverb delay lines live in SDRAM.** Put the 53 KB the loop really uses (`Memory` 31 KB + `Memory2` 22 KB, of the 2 x 32 KB allocated) in internal SRAM; 29 uncached SDRAM accesses (~500 stall cycles) per sample become 1-cycle accesses. Check heap + stack headroom first (RAM 59 % used statically, ~100 KB free; add a min-free-stack probe). The Plinky ring (64 KB + 16 KB) cannot follow; give it a smaller ring only if the owner accepts a different tail. | **~55-65k = 10-12 %** (reverb type Stock) | E | S | `effect_polyverb.cpp:30-32` (`__NOINIT(EXTERNAL_RAM)` -> `.noinit`), `reverb.h:76-103` |
| **2** | **Idle voices run the whole chain on a silent block.** `AudioPlayMemory::update` ends in `envelopeUpdate(nullptr)` which allocates, memsets and *transmits* a zero block (`play_memory.cpp:133-139`, `effect_envelope.cpp:445-457`); bit-depth, filter (full SVF if the last note had it on), amplifier, `MonoToStereo` (memcpy + 2 gains), RMS and both track mixers then process zeros. Do not transmit; every downstream `update` already returns on a null input. Keep feeding zeros for one more block while the SVF state is non-zero (or zero the state once when the voice goes idle) so the filter tail and the next note start cleanly. | **5.5k per idle voice (1.05 %); 8 idle = 8.4 %; 4 idle = 4.2 %**; +5k each for idle voices with the filter connected | E (no filter) / I (filter tail) | S-M | `play_memory.cpp:133-139`, `effect_envelope.cpp:445-457`, `filter_variable.cpp:208-215`, `monoToStereo.cpp:28-55`, `analyze_rms.cpp:28-32` |
| **3** | **Sample playback inner loop.** Per output sample ~58 cycles: the state (`iPitchCounter`, `currentSampelValue`, `interpolationDif`, ...) is stored to / reloaded from the object every sample (a possible streamed `getSample()` call in the loop forces it), the `currentIsStreamed` test and a bound test per sample, plus **two separate SDRAM reads** (`in[i]`, `in[i+1]`, ~22 cycles each). Split a resident fast path with locals, and fetch the sample window of the block once as a burst (`ldm` / DMA, ~2 cycles per sample) into an SRAM scratch of `128*pitch+2` samples, then loop on SRAM; same for backward / ping-pong / loop modes (split at the wrap). | **~8-10k per voice (1.6-1.9 %)**; 8 voices ~7-8 % | E | M-L (7 loop variants, one shared helper) | `play_memory_single_shot.cpp:96-296`, `play_memory_loop_forward.cpp:129-`, `..._backward.cpp`, `..._pingpong.cpp`, `play_memory.h:317-342` |
| **4** | **Delay processes lines nobody hears.** `process()` always updates the left, right *and* mono delay lines (3 SDRAM reads + 3 writes + 3 soft-limits per sample) although ping-pong uses 2 and mono uses 1. Keep only the audible lines (clear the other set when `isPingpong` toggles). Also skip `process` when the input block is empty and `silentSamplesCount >= timeInSamples` (the line is all zero; `isSilent()` exists, used only by the exporter). | 1/3 to 2/3 of ~25k = **8-16k (1.6-3 %)** always; **up to 25k (4.8 %)** when idle | E (steady state; clear on mode switch) | S | `effect_shortDelay.cpp:203-295`, `effect_shortDelay.h:41` |
| **5** | **Limiter loop.** ~90 cycles per sample x 256: an FP `vdiv` every sample (`threshold / xpeak`), int<->float conversions, a 5-deep shift register moved with 10 stores per sample (use a ring index), `uint8_t` loop counter. Bit-exact first step: do the divide only when `xpeak > threshold` (otherwise `f = 1` exactly) and index-ring the delay: about -25 cycles per sample. Cheaper rewrite (smoothed peak, reciprocal per 8 samples, or an integer follower): about -60. | **6k (1.2 %) E**; **15k (2.9 %) I** | E / I | S | `effect_limiter.cpp:76-190` (`processSidechain` and `processMainStream`) |
| **6** | **The voice output stage does the same work three times.** Amplifier `applyGain` (the output has several readers, so `receiveWritable` allocates and copies 256 B) -> `MonoToStereo` (`receiveWritable` copy again, a third `memcpy` for the right channel, two more `applyGain`) -> two track-mixer inputs. Fuse amp x pan into one pass per channel that reads the input read-only and writes the two output blocks (`ssat(smulwb(ssat(smulwb(x,a)),p))` keeps the rounding), skip the amplifier block when sidechain and RMS are off. | **~2k per voice (0.4 %); 8 voices ~3 %** | E | M | `AudioVoice.cpp:5-12`, `amplifier.cpp:30-50`, `monoToStereo.cpp:28-55`, `dspApplyGain.cpp:8-37` |
| **7** | **Skip the stock reverb when it has nothing to do.** `Reverb::IsActive()` (mean abs delta above 5 LSB) is computed every sample and never used; `AudioEffectPolyverb::isActive()` has no caller. Skip `runType` for a block when the input block is all zero and `last_update_delta_ <= threshold` (clear the LP state once). | **~100k = ~19 %** while sends are 0 and the tail is gone; 0 while fed | I (tail cut below ~ -76 dBFS; threshold 1.0 gives ~ -90 dBFS) | S | `effect_polyverb.cpp:65-160,249-257`, `reverb.h:106-165` |
| **8** | **Dead work in the reverb loop**: `delta_sum += abs(delta)` runs two libc `abs()` calls (integer abs of a float, because of `-fno-builtin`) per sample only to feed the unused `IsActive` (finding 7). Remove when not skipping (or `__builtin_fabsf`). | 3.8k (0.7 %) | E | S | `reverb.h:143,158`, `core/makefile:42` |
| **9** | **Amp envelope inner loop** (`envelopeUpdate`) keeps every temporary in the object (`envelope.tmp1/tmp2/sample12..78`, `mult_hires`, `count`): ~90 cycles per 8 samples because the `uint32_t*` stores alias the struct fields. Use locals, one state write-back per block; in sustain with a constant gain use the `applyGain` loop. | **~0.9k per sounding voice; 8 voices 7k (1.4 %)** | E | S | `effect_envelope.cpp:439-776` |
| **10** | **Control-rate soft-double.** `envelopeSyncTrackerSeq` runs every block for every active LFO-looping envelope (`mtAudioUpdateHandle.cpp:149-157`) and recomputes `seqSpeed/15.0`, `1000/lfoFrequency`, `SAMPLES_PER_MSEC` in double (~1k cycles each). Cache `periodMs` per tempo change, use `float` / integer ticks. Same for `Sequencer::tick` / `getTimerPeriod_us` (9 + 4 soft-double ops per block). | ~1k per active LFO envelope (0.2 %): 2 LFO envelopes on 4 voices = 8k (1.5 %); `Sequencer::tick` ~1.2k (0.2 %) | I | S | `effect_envelope.cpp:826-860,35`, `mtSequencer.cpp:81-86,2323-2333` |
| 11 | **Always-on meters and line-in chain.** The 14 RMS objects accumulate every block although only some screens read them: add an "enabled" flag set by the screen. The line-in chain (USB zero-fill stream, USB amps, record RMS) is built and run on MK66 too: gate it with "recording or monitoring". | 4k (0.8 %) + 3-5k (0.6-1 %) | E | S | `analyze_rms.cpp:28-53`, `AudioInput.cpp:4-52`, `Input.cpp:25-33` |
| 12 | **Filter**: `update_fixed` is already tight (6 `smmulr` per sample) but tests the filter type inside the loop and spills the loop bound to the stack, `sdiv` x2 per call; hoist the type test into 3 loops. Skip when input and `state_*` are all zero (bit-exact; subsumed by #2 for idle voices). | ~0.6k per filtered voice (0.1 %) | E | S | `filter_variable.cpp:118-170` |
| 13 | **Bandpass / highpass mix ramp**: `seqfx::mixAt` has a float divide per sample (`(s+1)/n`, n is a run-time argument): 128 `vdiv` per block per filter (delay) and per HP (reverb). Step the mix incrementally or multiply by `1/n`. (Our own mod code.) | 1.8k delay (0.35 %) + 1.8k reverb HP (0.35 %) when on | I | S | `seqFxDsp.h:84`, `effect_shortDelay.cpp:217`, `effect_polyverb.cpp:92` |
| 14 | **Run the audio code from SRAM.** The 36 KB of `.audio_ram_functions` execute from flash through the small code cache; link them into SRAM (SRAM_L at 0x1FFF0000 is on the code bus, zero wait) and copy at startup like `.data`. Competes with #1 for RAM (36 + 53 KB of ~100 KB free): decide by measurement, or move only the glue (`playerEngine::update`, `AudioStream`, mixers, ~12 KB). | 0-4 % (cache-miss dependent, unmeasured) | E | M | `mk66fx1m0.ld:54`, `config.h:9` |
| 15 | **Streamed voices**: per-sample out-of-line `getSample()` (~50 cycles, twice per sample). Fetch the window per block from the ring (as in #3). | ~10k per streamed voice (1.9 %) | E | M | `play_memory.h:317-342`, `VoiceStreamManager.cpp:1011-1072` |
| 16 | **Compiler flags**: no `-ffast-math` (gain only on the limiter / reverb constants, ~0.2 %), no -O3 (filter type unswitching, ~0.1 %). Dropping `-fno-builtin` for `libs/Audio` and `AudioEngine` inlines `fabsf` / `abs` / `sqrtf` (9 `fabsf` calls per delay block, etc.). | ~0.3-0.5 % | E | S | `core/makefile:42` |

Checked, no finding: the mod's audio-side code (`Engine::blockTick`, `advanceUs`, `drain`, `pulseAll`, seqCut / seqLauncher / seqFx hooks:
event-rate, < 500 cycles per block, `advanceUs` returns at once with no active track); sidecar hashing (`seqClipsUpdate`, `seqFxUpdate`: only
when the dirty flag is set); `AudioStream::allocate/release` (bit-mask pool, ~90 cycles per pair, ~100 pairs per block = ~1.7 %, shrinks with
#2 / #6); master chain (codec hardware EQ / bass / surround, nothing in software except the two limiters and the final mixers);
`AudioBitDepth` (word mask, skipped at 16 bit), `AudioAmplifier` (unity pass-through exists), `AudioMixer` (zero-gain inputs are skipped by
`applyGain*`); the `applyGain*` and `saturate` loops (6-8 cycles per sample, already SIMD-style). Main loop (display list, pad LEDs, file
manager): not measured; it never delays audio (the audio ISR preempts it) and only costs SDRAM bus time; its -Os build (plan 008 B) does not
touch audio. Interrupt priorities / time slicing: no change recommended (everything in one ISR is simplest; splitting the reverb into a
lower-priority software interrupt would not reduce the total).

### P.4 Recommendation: what to do first to make room for 4 synth voices

Assume a Plinky voice costs ~2-3 % (to be measured with the instrument-page CPU readout), so four voices need **8-12 %**. In order:

1. **#1 reverb buffers to SRAM** (a few lines, bit-exact, ~10-12 % with the stock reverb): alone it pays for the whole synth. Verify heap /
   stack headroom with a min-free probe in the same commit, and read the Reverb CPU readout before / after to calibrate every other number here.
2. **#2 idle voices produce no block** (4-8 % depending on how many tracks sound). The synth voices will also be idle most of the time:
   design the Plinky voice so it emits no block while silent.
3. **#4 delay dead lines** (1.6-3 %) and **#7 / #8 reverb skip when silent** (up to 19 % at rest; the owner picks the -76 dBFS or -90 dBFS tail
   threshold).
4. **#3 playback loop + burst fetch** (7-8 % at 8 voices; the largest remaining item and the largest change: one shared helper with a
   bit-exact golden-output test per play mode, host-testable).
5. Then #5 limiter (bit-exact step first), #6 output stage fusion, #9 envelope, #10 soft-double, #11 meters.

#1, #2 (no filter), #4, #8, #9 are bit-exact and small: together roughly **14-25 %** of the budget (scenario dependent) before any audible
change. No hotkey or UI change is needed. Next step: the owner approves the list; no code was changed.
