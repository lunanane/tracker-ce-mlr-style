// Sequencer tracks mod - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// CPU monitor hooks (plan 022 P1). Config > Info > CPU monitor opens the overlay; while it is open (seqcpu::active()) the audio ISR
// times every AudioStream::update() with the DWT cycle counter. Hidden, only a few counters are touched (the ISR total, main loop pass,
// stream jobs / blocking reads), no per-stream timing. The pure part (counters, text) is in seqCpuLogic.h.
#pragma once
#include "seqCpuLogic.h"

namespace seqcpu {

extern volatile bool g_on; // the monitor is open: time every stream
extern uint32_t g_nestedCycles;
extern Snap g_live;        // written by the ISR / main loop
extern volatile Rd g_rdKind;

inline uint32_t cyc() { return *(volatile uint32_t*)0xE0001004; } // DWT_CYCCNT (enabled by init())
inline bool active() { return g_on; }

void init();               // main loop, once: enables the cycle counter
void setActive(bool on);   // opens / closes the monitor; clears the windows
bool snapshot(Snap& out);  // main loop: the last complete window; false until one exists
int buildText(char* out, int cap); // main loop: the overlay text (waits for the first window while it is empty)

// ISR
inline void stageAdd(uint8_t stage, uint32_t cycles) { g_live.stage[stage] += cycles; }
// a stage nested inside a stream update (warp inside a sample voice): counted on its own, subtracted from the stream's cycles
inline void nestedAdd(uint8_t stage, uint32_t cycles)
{
    g_live.stage[stage] += cycles;
    g_nestedCycles += cycles;
}
void blockEnd(uint32_t isrCycles); // end of the audio ISR: rolls the window

// main loop
inline void passEnd(uint32_t cycles) { snapPass(g_live, cycles); }

// stream manager (any context, plain counters)
inline void job() { g_live.jobs++; }
inline void syncFill() { g_live.syncFills++; }
inline void ringMiss() { g_live.ringMiss++; }
inline void jump(bool restarted, bool cue)
{
    g_live.jumps++;
    if (restarted)
        g_live.restarts++;
    else
        g_live.reuses++;
    if (cue)
        g_live.cueHits++;
}
inline void readKind(Rd k) { g_rdKind = k; }
inline Rd takeKind()
{
    const Rd k = g_rdKind;
    g_rdKind = R_OTHER;
    return k;
}
inline void readEnd(Rd k, uint32_t cycles) { snapRead(g_live, k, cycles); }

} // namespace seqcpu
