// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Small shared state machines of the v0.2 mod (plan 005 step 0). No Polyend includes, host-testable (core/seqtest).
#pragma once
#include <stdint.h>

namespace seqlogic {

// ---- performance-mode owners (plan 005 C1) ----
// The stock `player.performanceMode` flag is shared by the clip launcher (clips live in the performance buffer) and the perform
// FX (stock Performance page, alt fader page). Each claims the flag; performance mode may only be left when no owner remains.
enum PerfOwner : uint8_t { PERF_OWNER_LAUNCHER = 1, PERF_OWNER_FX = 2 };

struct PerfOwners
{
    volatile uint8_t mask = 0; // read from the audio interrupt, written from both contexts (single byte)
    // true: this claim switched the flag on (nobody owned it before)
    bool claim(uint8_t owner)
    {
        bool first = mask == 0;
        mask = (uint8_t)(mask | owner);
        return first;
    }
    // true: no owner is left, performance mode may be switched off now
    bool release(uint8_t owner)
    {
        mask = (uint8_t)(mask & ~owner);
        return mask == 0;
    }
    bool has(uint8_t owner) const { return (mask & owner) != 0; }
    bool othersThan(uint8_t owner) const { return (mask & ~owner) != 0; }
    bool any() const { return mask != 0; }
    void clear() { mask = 0; }
};

// ---- panic (plan 005 M6): transport Stop asks, the main loop runs the module hooks once ----
struct PanicLatch
{
    volatile bool pending = false;
    void request() { pending = true; }
    bool take()
    {
        if (!pending)
            return false;
        pending = false;
        return true;
    }
};

// ---- deferred writes (plan 005 M2): while the transport plays, writers only mark dirty ----
struct DirtyGate
{
    bool dirty = false;
    void mark() { dirty = true; }
    // true once when something is dirty and the transport does not play (the caller then writes / asks for the autosave)
    bool take(bool playing)
    {
        if (!dirty || playing)
            return false;
        dirty = false;
        return true;
    }
};

}  // namespace seqlogic
