// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Pure logic of the heap / stack headroom readout (v0.3.1 M3, host-testable: core/seqtest/test_mod.cpp).
// The gap between the boot-time end of the heap and the stack is painted once. The heap grows into it from below (newlib/nano
// sbrk), the stack from above, so the stack's low-water mark is found by scanning from the STACK side down to the first long run of
// paint; scanning from the heap side stops as soon as the heap has eaten the first word.
#pragma once
#include <stdint.h>

namespace seqmem {

static const uint32_t kPaint = 0xAAAAAAAAu;
static const uint32_t kRunWords = 32; // a run of 128 paint bytes counts as untouched gap

// gapHigh: first address above the painted region (the stack side). Returns the lowest address the stack has reached (rounded to a
// word), or gapLow when no untouched run exists (heap and stack have met, or the heap ate everything painted).
inline const uint32_t* stackLow(const uint32_t* gapLow, const uint32_t* gapHigh)
{
    const uint32_t* p = gapHigh;
    uint32_t run = 0;
    while (p > gapLow)
    {
        if (*(p - 1) == kPaint)
        {
            if (++run >= kRunWords)
                return p - 1 + run; // the word above the run: the run itself is untouched
        }
        else
            run = 0;
        p--;
    }
    return gapLow;
}

// headroom = stack low-water minus the highest the heap ever was (0 when they crossed)
inline uint32_t headroom(uintptr_t stackLowAddr, uintptr_t maxBrk)
{
    return stackLowAddr > maxBrk ? (uint32_t)(stackLowAddr - maxBrk) : 0u;
}

} // namespace seqmem
