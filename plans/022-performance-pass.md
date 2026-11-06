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
