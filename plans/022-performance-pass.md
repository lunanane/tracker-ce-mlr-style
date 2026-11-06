# Plan 022 — performance pass on the (almost complete) feature set (owner 2026-11-06)

Owner (device rehearsal, v0.4.x): "MLR with streaming longer loops very easily brings the CPU down; also the synth
engine got more easy to drop out, and then it can't come back. At this point we can make heavy optimisations on the
almost complete feature set."

## P1 — measure on the device (first, cheap)
A CPU overlay reachable at any time (e.g. on the Config page or a Shift combo, labelled): audio-block load avg/peak and a
breakdown by stage measured with the DWT cycle counter — sample voices (resident / streamed), warp, PolySynth (+ voices,
governor cap), delay, reverb (stock / shimmer), beat repeat, limiter/master, mixers/meters, our sequencer/MLR audio-side
code — plus main-loop time and SD/stream activity (stream jobs per second, cue-cache reads, waveform reads, blocking read
time). So the owner can report real numbers from the same rehearsal.

## P2 — fix the dropouts
- **Governor**: voices must come back. Restore when there is real headroom relative to the current load (hysteresis
  on the measured load *with the current voice count*, not a fixed 65 % average); never lock the cap at 0; restore one
  voice per ~0.5–1 s when blocks stay under a threshold; and don't count load spikes caused by SD/stream catch-up
  against the synth forever. Show the cap on screen.
- **MLR streaming**: every jump on a streamed loop restarts the SD stream (seek + refill) — rate-limit/coalesce jumps
  (a jump within the same cached cue window reuses the stream; rapid repeated jumps don't each trigger a full refill),
  make the cue cache fill and the waveform probes yield to stream refills (lower priority, smaller chunks), measure the
  blocking-read time in the main loop and the audio-ISR side of streamed playback.

## P3 — heavy optimisations (keep behaviour identical; bit-exact where possible)
From the CPU audit (plan 009 P, still open): #3 sample-playback fast path (state in locals, one burst read per block,
no per-sample out-of-line `VoiceStreamManager::getSample` for streamed voices — read from the ring with an inline fast
path), the remaining fused output-stage work, line-in chain gating, `AUDIO_CORE_RAM` hot loops into RAM if internal RAM
allows (it's at 87.9 % — measure first), PolySynth voice loop and warp loop micro-optimisations, beat repeat idle path,
anything P1 shows as hot. Each change measured (host cycle counts + device readout) and tested (bit-exact / bounded).

## P1 as built (branch v042-perf, from seq-tracks 94d6a50; original Tracker MK66 only)
- **Where**: Config > Info > **CPU monitor** (the Info text screen, live numbers every 0.5 s, Close = button 8). Code: `core/source/seqCpuLogic.h` (pure counters + text, host-tested), `seqCpu.h/.cpp` (hooks, DWT), `core/seqtest/test_cpu_monitor.cpp`.
- **Stages** (cycles of the DWT counter, percent of the 522 240 cycle block): sample voices resident / streamed (stage set per block in `AudioPlayMemory::update`), warp (nested, subtracted from the voice), PolySynth, stock synth, delay, reverb, beat repeat, limiter, voice chain (bit depth / filter / amplifier / mono to stereo), mixers + meters, line in/out + USB + record queues, control rate (`engine.update`), MIDI + sequencer tick (our tracks / MLR audio-side code run in `sequencer.tick`), "other" = ISR total - sum. `AudioStream::update_all` times every stream **only while the monitor is open** (`perfStage` byte per stream, +1 byte each).
- **Always on, counters only**: audio ISR total (avg / window peak / max since open), main loop pass (avg / peak / passes per second), stream jobs, blocking fills, ring misses (underruns), jumps of streamed voices (ring restarted / reused / started from a cue), blocking SD reads by caller (cue cache / waveform / stream fill / other: count, total ms, longest ms). Cost hidden: about 15 cycles per block plus a few per stream job.
- **Display**: one text box (the Info box), 13 lines, worst case 1.1 KB of FIFO text (limit 3840, checked in `test_cpu_monitor.cpp` with `seqTraceStages.h::textBytes`).
- **Image**: FLASH 961 996 -> 966 628 B, RAM 230 304 -> 231 772 B (88.4 %), EXTERNAL_RAM unchanged (99.58 %), image end 0xED138 (limit 0xFC000), `check_sdram_ctors.sh` ok.
- Also in this branch, ahead of P1 (coordinator request): commit "MLR section mapping" (pads after SYNC ALL / RSYNC act on the section shown on the screen; host sweeps in `test_cut.inc`).

## P2 as built
**Governor** (`plinky_governor.h`, host tests `test_plinky3.inc` `test_x3_governor_recovery`): the old rule restored a voice only after 2 s with the *average* below a fixed 65 % - an MLR stream or a reverb that alone uses that much made the cap permanent, and a cut to 0 voices (no floor on an overrun) could not come back at all.
- Restore on the **predicted** average with one more voice (`avg + voiceCost x (extra voices the new cap allows)`, the synth measures the per-voice cost: its own cycle readout / sounding voices, clamped 1.5-12 %, default 4 %) below 77 %, i.e. below the 80 % cut threshold: cut load and restored load never meet (no oscillation), a steady background load settles at the voices that fit.
- One voice per **230 blocks (0.67 s)** while blocks stay under 90 %; a block between 90 and 95 % takes a quarter off the count (delays), a block >= 95 % starts it again; a spike reading is clamped to 110 % before it enters the average (SD / stream catch-up, a stalled timer); `kMinCap = 1`: never 0.
- Existing test changes (direct consequences of the new rule, same strictness): restore time 690-900 blocks -> 230-290 (`kRestoreBlocks + 60`); "a block at 90 % resets the count" -> a 96 % block resets it (and, within a second of an overrun, is the second risk block: one more voice goes, stated in the test) and a 91 % block delays it by a quarter. All other governor checks unchanged.
- Estimated effect: after a spike the cap is back in 0.67 s per voice (was 2 s per voice and only below 65 % average); under 45 % background load all 8 voices stay available (old rule: cap stuck as soon as avg > 65 %).

**MLR streaming** (no hardware numbers; use the monitor, section 11p of the test list):
- *Measure*: jumps of streamed voices counted as ring restarted / reused / started from a cue, SD jobs per second, ring misses (underruns), blocking reads by caller (cue cache, waveform, stream fill) with total and longest ms - all in the CPU monitor.
- *Coalesce*: a jump whose first stream frame (after the cue cache window) lies inside the ring that is already buffered reuses the running stream (`VoiceStreamManager::notifyNoteOn`, existing since plan 016, now counted); a restart discards the in-flight read only through the generation check as before. No new restart rule was added blind: the "restarted vs reused" ratio on the device decides whether more is needed.
- *Priority*: the cue cache fill and the waveform probes (blocking SD reads in the main loop) run only when no voice needs a refill, **not within 40 ms of a note-on of a streamed voice**, and **at most once per 6 ms**; a cue is now read in pieces of 1024 frames (2 KB; one piece per call, restart from the top if the entry went stale) instead of 3072 frames (6 KB) in one call, the waveform probes one column per call instead of two. A voice's refill never waits behind more than one 2 KB read. Cost: the cache of a section is complete after about 0.3 s instead of 0.1 s.
- *Per sample*: `VoiceStreamManager::getSampleFast` (inline: RAM pre-cache hit, or ring hit once the fade-in is over; everything else calls `getSample`) replaces the out-of-line call for every streamed sample read in `readSampleAtRel`; value identical by construction (same compares, same ring index; the fade-in, loop-wrap cache and miss bookkeeping stay in `getSample`). Estimated: ~40 cycles x 2 reads x 128 samples = ~10k cycles per streamed voice per block (about 1.9 %, plan 009 P #15). Not host-tested (the class needs the SD/sample memory); verify with the monitor "streamed" stage before/after (v0.4.1 vs this build).
- Ring buffers became a class member (`VoiceStreamManager::ring_`, same `__NOINIT(EXTERNAL_RAM)` raw storage, no constructor; `check_sdram_ctors.sh` ok).

## P3 as built
Only changes that are bit-exact or already covered by a host test were made; the rest is listed as not done with the reason.
| # | Item | Status | Estimated saving per block | Test |
| :-: | :--- | :--- | :--- | :--- |
| 1 | Streamed sample read inline (`getSampleFast`), plan 009 #15 | **done in P2** | ~10k cycles per streamed voice (1.9 %) | identical by construction; verify with the monitor "streamed" stage |
| 2 | Sample playback fast path (#3) for resident samples, single shot and loop forward (normal direction): state in locals (`play_memory_kernel.h`), no per-sample streamed / bound test, used when no glide runs in the block | **done (partly)** | 0.5-1k cycles per resident voice (ARM -O2 instruction count of a model of the loop: 28 -> ~22 instructions per sample, 6 fewer member stores/loads); 8 voices ~0.1-0.2 % | `test_kernel.cpp`: 45 000 random blocks bit-identical (output, counter, fraction, last samples, playing), single shot and loop, interpolation on/off, pitch 0.2-4.2 |
| 3 | Burst read of the sample window into an SRAM scratch (the SDRAM reads of #3) and the backward / ping-pong / reverse / slice / granular variants | **not done** | ~8-10k per voice (plan 009 estimate) | needs a scratch of 128 x pitch + 2 samples per voice (up to 1 KB) and six more loops; the monitor's "resident" stage on the device decides whether it is worth it |
| 4 | Remaining amp / pan fusion, PolySynth voice loop and warp loop micro-optimisations | **not done** | - | no measurement yet; the PolySynth / warp stages of the monitor show what is hot |
| 5 | Line-in chain gating | **not done** | ~3-5k (0.6-1 %) | needs the recording / monitoring state of the input; the "Line in/out" stage of the monitor shows the cost |
| 6 | Beat repeat idle path | **nothing to do** | - | already 0 cost when idle (plan 021 test: idle = 0) |
| 7 | AUDIO_CORE_RAM hot loops into RAM | **not done, measured** | 0-4 % (plan 009 #14) | RAM is 231 804 of 262 144 B (88.4 %): 30 340 B are left for heap AND stack together; the audio code section is about 36 KB (does not fit); the ~12 KB glue would leave ~18 KB without a stack reading. Read "stack N, headroom M" on the Config page (dev mode) in a long session first |

Image after P3: FLASH 964 988 B, RAM 231 804 B (88.43 %), EXTERNAL_RAM 16 706 008 B (99.58 %, unchanged), image end 0xECAE4 (limit 0xFC000, margin 62 748 B). Baseline v0.4.1 + plan: FLASH 961 996, RAM 230 304, image end 0xEBF1C.

### For the owner: verify with the CPU monitor (Config > Info > CPU monitor), same rehearsal as before, v0.4.1 vs this build
1. MLR streaming loop, pads jumping: "Samples ... streamed" % (expect lower by ~2 % per streamed voice), "ring misses" (expect 0), "Blocking reads" total ms and longest (cue cache pieces are 1/3 as long), "jumps: restarted / reused / from cue" (how often the stream is restarted), "SD stream jobs/s".
2. PolySynth chords + sample voices + reverb: watch "PolySynth voices: cap N" - after a dropout it must climb back by one voice per ~0.7 s while the load stays under ~77 % with one more voice.
3. "Samples ... resident" with 8 voices: expect about 0.1-0.2 % lower than v0.4.1.
4. "other" in the ISR lines: if it is above ~10 % something is not accounted for.
5. Idle: all stage lines near 0, ISR avg of a few percent.

## v0.4.3 regression fix (branch v043-regress, from seq-tracks 8ecf4dd = v0.4.2)
Owner (device): v0.3.x played PolySynth with lots of sends smoothly and the UI was responsive; since v0.4 the same actions drop PolySynth
voices and "the interface is mostly lagging", even without MLR. Causes found (v0.3.10 .. v0.4.2) and fixes, ranked by estimated cost:
| # | Cause | Est. cost (v0.4.2) | Fix |
| :-: | :--- | :--- | :--- |
| 1 | PolySynth: the v0.4 sub / SVF / ladder branches and their state inside the one per-sample voice loop (more branches, register spills), and a per-instance mix loop with per-sample chorus / send switches and branchy saturation | 1 instance, 4 voices, sends: 125.5k cycles/block (24.0 %) vs 106.4k in v0.3.10; 3 instances 8 voices + sends: 268k (51.3 %) | voice loop compiled per oscillator kind and width for the default path (no sub, LPG), shared set-up / write-back; mix loop per chorus / reverb / delay combination; `ssat` saturation. Bit-exact (same ARM hashes, host golden tests). Now 98.8k (18.9 %, below v0.3.10) and 208k (39.9 %); chorus 309k -> 246k; mono 190k -> 125k; flash +8 KB |
| 2 | Plan 019 step 0: the whole MK66 platform at -Os (FatFs / SDHC, SPI, I2C pads, LED driver, FT812 display transfer, USB, MIDI UART, DMA, interrupt handlers) and the sequencer sources that run in the audio interrupt (seqTracks / seqTracksCore block tick, seqCut apply, seqLauncher drain, chordarp / mono / playhead per pulse, seqSong, seqClips, seqSidecar, scales) plus pad / keyboard / actions / pad backlight / wavetable envelopes | not measurable on the host (needs the device); typical -Os penalty 10-30 % on loops | -O2 again as in v0.3.x; -Os only for genuinely cold code: effector, mtTest, clock set-up, string / print helpers, EEPROM, ADC, battery, radio, sleep, unused UARTs (Serial5 = MIDI stays -O2). Cost 15 KB of flash |
| 3 | Governor (new in v0.4): cut at an average >= 80 % / 88 % and on two blocks >= 95 % within 1 s - a high but safe load dropped voices; restore 0.67 s per voice | voices lost at 80-95 % load where v0.3.x played fine | cuts only on a block >= 97 % (one voice, floor 4) or an overrun >= 100 % (down to sounding - 1, floor 1); restore one voice per 0.25 s of blocks < 93 % while the predicted average with one more voice is < 90 %; a block >= 96 % restarts the count |
| 4 | Instrument page (v0.4.1 fix: the update runs every pass): the PolySynth view redrew 20 times a second while voices sound (voice meters, CPU in the model) | display / main loop load on that page | 10 per second, as the Play-mode view |

Checked and not a cause: fastmem.S (correct: aligned 16-byte ldm / stm, unaligned byte loop = the old nano behaviour); CPU monitor counters (a few cycles per block / pass when closed); live pitch sampler (8 plain reads per pass, refresh <= 25 fps only while visible); instrument-order mapping (event-driven only); plinky.bin state hash (only when the dirty flag is taken after a stop, not per pass); morph controllers x3 (~300 cycles each per pass); pad LEDs (no change since v0.3.10); beat repeat bypassed (no processing).
Governor test changes (direct consequences of the owner-driven rule, same strictness): 86 % sustained -> all 8 voices stay (was 5..7); "a high average >= 88 % cuts" -> never cuts (also 96 % sustained); risk rule "two blocks >= 95 %" -> one block >= 97 % (two 96 % blocks cut nothing); the 96 % block after an overrun restarts the count but cuts nothing (was: second risk block, one more voice); the "delays by a quarter" block is 94 % (91 % is now a quiet block); restore time comments 0.67 s -> 0.25 s. All other checks unchanged.
Measurement tool: `core/seqtest/armbench` (unicorn + capstone, approximate M4 cycles of the real ARM code; not part of `make test`).
Image: end 0xF3A18 (margin 34 280 B; v0.4.2 0xED174, margin 61 068 B), RAM 231 868 B (88.45 %), `check_sdram_ctors.sh` ok.

### For the owner: CPU monitor (Config > Info > CPU monitor), same project and actions on v0.3.10 (where readable), v0.4.2 and this build
1. Idle, nothing playing: ISR avg a few percent; main loop passes per second (note the number) - expect clearly more than v0.4.2.
2. PolySynth 1 alone, 4-note chords, Reverb / Delay sends up (the v0.3.x case): "PolySynth" stage % - expect about 20 % lower than v0.4.2 (host: 24.0 % -> 18.9 % of the block for the synth alone); cap stays 8, no dropout.
3. PolySynth 1 + 2 + 3, 8 voices, sends: PolySynth % (host: 51 % -> 40 %), ISR avg / peak; cap must stay at 8 unless the ISR peak reaches ~97 %.
4. Same with Chorus Mix > 0 on all three: PolySynth % (host: 59 % -> 47 %).
5. Push the load (add sample tracks / reverb) until a voice drops: the cap must only fall when ISR peak >= ~97 % and come back within ~0.25 s per voice once the load is lower.
6. UI responsiveness with the PolySynth playing: open / scroll the instrument page, pattern editor, file browser (SD listing speed), pad response; compare with v0.4.2 by feel and "main loop pass" avg / peak.
7. MLR streaming loop: "Samples ... streamed", "ring misses" (expect 0), blocking reads ms - SD path is -O2 again.
8. Sub oscillator + SVF / ladder on 8 voices is still expensive (host: 74 % / 93 % of the block for the synth alone): expect the governor to act there.
