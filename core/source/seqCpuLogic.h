// Sequencer tracks mod - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// CPU monitor (plan 022 P1): the pure part. Counters in cycles (DWT cycle counter, 180 MHz), a window of ~0.5 s of audio blocks, and the
// text of the overlay. No hardware access, no Polyend includes: host-tested in core/seqtest (test_perf.cpp).
//
// Where the numbers come from (see seqCpu.h): the audio ISR adds the cycles of every AudioStream::update() to its stage (only while the
// monitor is open), the ISR total and the control / sequencer parts of the block, the main loop adds its pass time, the stream manager
// counts jobs, restarts and blocking reads. Every kWindowBlocks blocks the live counters are published as a Snap and cleared.
#pragma once
#include <stdint.h>
#include <stdio.h>

namespace seqcpu {

enum Stage : uint8_t
{
    S_SMP_RES,  // sample voices, resident samples
    S_SMP_STR,  // sample voices, streamed from the SD card (ring buffer reads)
    S_WARP,     // time-stretch (nested in the sample voice, subtracted from it)
    S_PLINKY,   // PolySynth (Plinky voice engine, 3 instances, governor)
    S_SYNTH,    // the stock synth voices (wavetable / waveform)
    S_DELAY,
    S_REVERB,
    S_BEAT,     // beat repeat
    S_LIMIT,    // the two limiters
    S_VOICE,    // per-voice chain: bit depth, filter, amplifier, mono to stereo
    S_MIX,      // mixers, meters (RMS), multiplexers
    S_IO,       // line in / out, USB, record queues
    S_CTRL,     // control rate: engine.update() (envelopes, parameter updates of the 8 voices)
    S_SEQ,      // MIDI + sequencer tick (our tracks / MLR / clip launcher audio-side code run in here)
    S_COUNT
};
enum Rd : uint8_t { R_CUE, R_WAVE, R_SYNC, R_OTHER, R_COUNT }; // blocking SD reads by caller

enum
{
    kBlockCycles = 522240, // 128 samples at 44 117.65 Hz in 180 MHz cycles
    kWindowBlocks = 172,   // ~0.5 s
    kCyclesPerMs = 180000
};

struct Snap
{
    uint32_t blocks;
    uint32_t isrSum, isrPeak;       // audio ISR (whole block) cycles
    uint32_t isrMax;                // highest block since the monitor was opened (not cleared per window)
    uint32_t stage[S_COUNT];        // cycles per stage in the window
    uint32_t passes, passSum, passPeak; // main loop passes
    uint32_t jobs;                  // async stream refills launched
    uint32_t syncFills;             // stream refills done as a blocking read (no CLMT)
    uint32_t ringMiss;              // samples that a streamed voice wanted but the ring did not have (an underrun: heard as a click)
    uint32_t jumps, restarts, reuses, cueHits; // note-ons of streamed voices: all / ring restarted / ring reused / started from a cue
    uint32_t rdCalls[R_COUNT], rdCycles[R_COUNT], rdPeak; // blocking reads: count, total cycles, longest
    uint32_t cueReads, waveReads;   // cue cache entries / waveform probes loaded
};

inline void snapClear(Snap& s, uint32_t keepMax)
{
    Snap z = {};
    z.isrMax = keepMax;
    s = z;
}

// ISR: one block done. Returns true when the window is complete (the caller publishes, then clears).
inline bool snapBlock(Snap& s, uint32_t isrCycles)
{
    s.blocks++;
    s.isrSum += isrCycles;
    if (isrCycles > s.isrPeak)
        s.isrPeak = isrCycles;
    if (isrCycles > s.isrMax)
        s.isrMax = isrCycles;
    return s.blocks >= (uint32_t)kWindowBlocks;
}
inline void snapPass(Snap& s, uint32_t cycles)
{
    s.passes++;
    s.passSum += cycles;
    if (cycles > s.passPeak)
        s.passPeak = cycles;
}
inline void snapRead(Snap& s, Rd kind, uint32_t cycles)
{
    s.rdCalls[kind]++;
    s.rdCycles[kind] += cycles;
    if (cycles > s.rdPeak)
        s.rdPeak = cycles;
}

// ---- the text ----
struct Extra
{
    int plinkyCap, plinkyVoices;      // governor cap and sounding voices (-1: no PolySynth)
    int plinkyCpu10, plinkyPeak10;    // the engine's own readout in 0.1 %
    uint32_t recWrites, recLongestMs, recRingPct, recDropped; // plan 030 A: master recording (last / running recording)
    uint32_t dispBytes, dispMax, dispSkipped, dispSkippedMax; // v0.4.7: display.cpp step3: list bytes of the last frame / highest, controls dropped by the 8172 B budget
};

// tenths of percent of the block budget
inline uint32_t pct10(const Snap& s, uint32_t cycles)
{
    if (!s.blocks)
        return 0;
    return (uint32_t)(((uint64_t)cycles * 1000u) / ((uint64_t)s.blocks * kBlockCycles));
}
inline uint32_t perSec(const Snap& s, uint32_t count)
{
    if (!s.blocks)
        return 0;
    return (uint32_t)(((uint64_t)count * 44118u) / ((uint64_t)s.blocks * 128u)); // blocks * 128 samples / 44.1 kHz = window seconds
}
inline uint32_t ms10(uint32_t cycles) { return (uint32_t)(((uint64_t)cycles * 10u) / kCyclesPerMs); } // tenths of ms

template <class... A> inline void appendf(char* out, int cap, int& n, const char* fmt, A... a)
{
    if (n >= cap - 1)
        return;
    n += snprintf(out + n, (size_t)(cap - n), fmt, (unsigned)a...);
    if (n > cap - 1)
        n = cap - 1;
}

// Fills out with the overlay text (lines joined by '\n'). Returns the length. Worst case stays far below the display budget
// (checked in test_perf.cpp against seqTraceStages.h: 12 + string per CMD_TEXT line).
inline int format(const Snap& s, const Extra& e, char* out, int cap)
{
    int n = 0;
#define SEQCPU_ADD(...) appendf(out, cap, n, __VA_ARGS__)
    if (cap > 0)
        out[0] = 0;
    uint32_t sum = 0;
    for (int i = 0; i < S_COUNT; i++)
        sum += s.stage[i];
    const uint32_t other = s.isrSum > sum ? s.isrSum - sum : 0;
    const uint32_t winMs10 = (uint32_t)(((uint64_t)s.blocks * 128u * 10000u) / 44118u);
    SEQCPU_ADD("CPU monitor (%% of the audio block)   window %u.%u s   Close: button 8\n", winMs10 / 10000u, (winMs10 / 1000u) % 10u);
    SEQCPU_ADD("Audio ISR  avg %u.%u%%   peak %u.%u%%   max %u.%u%%\n", pct10(s, s.isrSum) / 10u, pct10(s, s.isrSum) % 10u,
        (uint32_t)(((uint64_t)s.isrPeak * 1000u) / kBlockCycles) / 10u, (uint32_t)(((uint64_t)s.isrPeak * 1000u) / kBlockCycles) % 10u,
        (uint32_t)(((uint64_t)s.isrMax * 1000u) / kBlockCycles) / 10u, (uint32_t)(((uint64_t)s.isrMax * 1000u) / kBlockCycles) % 10u);
    SEQCPU_ADD("Samples: resident %u.%u   streamed %u.%u   warp %u.%u\n", pct10(s, s.stage[S_SMP_RES]) / 10u, pct10(s, s.stage[S_SMP_RES]) % 10u,
        pct10(s, s.stage[S_SMP_STR]) / 10u, pct10(s, s.stage[S_SMP_STR]) % 10u, pct10(s, s.stage[S_WARP]) / 10u,
        pct10(s, s.stage[S_WARP]) % 10u);
    SEQCPU_ADD("PolySynth %u.%u   stock synth %u.%u   delay %u.%u   reverb %u.%u\n", pct10(s, s.stage[S_PLINKY]) / 10u,
        pct10(s, s.stage[S_PLINKY]) % 10u, pct10(s, s.stage[S_SYNTH]) / 10u, pct10(s, s.stage[S_SYNTH]) % 10u,
        pct10(s, s.stage[S_DELAY]) / 10u, pct10(s, s.stage[S_DELAY]) % 10u, pct10(s, s.stage[S_REVERB]) / 10u,
        pct10(s, s.stage[S_REVERB]) % 10u);
    SEQCPU_ADD("Beat repeat %u.%u   limiter %u.%u   voice chain %u.%u   mixers %u.%u\n", pct10(s, s.stage[S_BEAT]) / 10u,
        pct10(s, s.stage[S_BEAT]) % 10u, pct10(s, s.stage[S_LIMIT]) / 10u, pct10(s, s.stage[S_LIMIT]) % 10u,
        pct10(s, s.stage[S_VOICE]) / 10u, pct10(s, s.stage[S_VOICE]) % 10u, pct10(s, s.stage[S_MIX]) / 10u,
        pct10(s, s.stage[S_MIX]) % 10u);
    SEQCPU_ADD("Line in/out %u.%u   control %u.%u   MIDI+sequencer %u.%u   other %u.%u\n", pct10(s, s.stage[S_IO]) / 10u,
        pct10(s, s.stage[S_IO]) % 10u, pct10(s, s.stage[S_CTRL]) / 10u, pct10(s, s.stage[S_CTRL]) % 10u,
        pct10(s, s.stage[S_SEQ]) / 10u, pct10(s, s.stage[S_SEQ]) % 10u, pct10(s, other) / 10u, pct10(s, other) % 10u);
    if (e.plinkyCap >= 0)
        SEQCPU_ADD("PolySynth voices: cap %u, sounding %u   own readout %u.%u%%  peak %u.%u%%\n", (uint32_t)e.plinkyCap, (uint32_t)e.plinkyVoices,
            (uint32_t)e.plinkyCpu10 / 10u, (uint32_t)e.plinkyCpu10 % 10u, (uint32_t)e.plinkyPeak10 / 10u, (uint32_t)e.plinkyPeak10 % 10u);
    const uint32_t passMs10 = s.passes ? ms10(s.passSum / s.passes) : 0;
    SEQCPU_ADD("Main loop: %u.%u ms avg   %u.%u ms peak   %u passes/s\n", passMs10 / 10u, passMs10 % 10u, ms10(s.passPeak) / 10u,
        ms10(s.passPeak) % 10u, perSec(s, s.passes));
    SEQCPU_ADD("SD stream: %u jobs/s   blocking fills %u   ring misses %u\n", perSec(s, s.jobs), s.syncFills, s.ringMiss);
    uint32_t calls = 0, cyc = 0;
    for (int i = 0; i < R_COUNT; i++)
    {
        calls += s.rdCalls[i];
        cyc += s.rdCycles[i];
    }
    SEQCPU_ADD("Blocking reads: %u, %u.%u ms total, longest %u.%u ms\n", calls, ms10(cyc) / 10u, ms10(cyc) % 10u, ms10(s.rdPeak) / 10u,
        ms10(s.rdPeak) % 10u);
    SEQCPU_ADD("   cue cache %u (%u.%u ms)   waveform %u (%u.%u ms)\n", s.rdCalls[R_CUE], ms10(s.rdCycles[R_CUE]) / 10u,
        ms10(s.rdCycles[R_CUE]) % 10u, s.rdCalls[R_WAVE], ms10(s.rdCycles[R_WAVE]) / 10u, ms10(s.rdCycles[R_WAVE]) % 10u);
    SEQCPU_ADD("   stream fill %u (%u.%u ms)   other %u (%u.%u ms)\n", s.rdCalls[R_SYNC], ms10(s.rdCycles[R_SYNC]) / 10u,
        ms10(s.rdCycles[R_SYNC]) % 10u, s.rdCalls[R_OTHER], ms10(s.rdCycles[R_OTHER]) / 10u, ms10(s.rdCycles[R_OTHER]) % 10u);
    SEQCPU_ADD("MLR jumps on streamed voices: %u   ring restarts %u   reused %u   from cue %u\n", s.jumps, s.restarts, s.reuses, s.cueHits);
    SEQCPU_ADD("Display: frame %u B (max %u)  skipped %u (max %u)\n", e.dispBytes, e.dispMax, e.dispSkipped, e.dispSkippedMax);
    SEQCPU_ADD("Master rec: %u writes   longest write %u ms   ring peak %u%%   dropped blocks %u", e.recWrites, e.recLongestMs, e.recRingPct, e.recDropped);
#undef SEQCPU_ADD
    return n;
}

} // namespace seqcpu
