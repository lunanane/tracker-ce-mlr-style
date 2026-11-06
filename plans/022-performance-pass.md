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
