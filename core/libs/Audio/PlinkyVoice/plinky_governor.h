// Sequencer tracks mod (plan 019 A) - PolyForm Noncommercial 1.0.0, see seqgen/LICENSE.md. No Polyend includes, host-tested.
// CPU governor of the shared voice pool: from the measured load of the whole audio block (permille of the block period, from the
// DWT cycle counter) it lowers the number of voices the synth may sound, and raises it again after a quiet stretch. Pure integer
// compares, once per block. The control law (hysteresis: cut fast on an overrun, slowly on a high average, restore only after
// seconds of headroom) follows the description in plan 018 of the OpenTracker governor (Apache-2.0); written here from that
// description, no code copied. Constants are in blocks of 128 samples at 44.1 kHz (2.9 ms).
#pragma once
#include <stdint.h>

namespace plinkyvoice {

struct Governor
{
    enum
    {
        kHoldBlocks = 86,     // 250 ms after a cut nothing is cut again
        kRiskWindow = 345,    // 1 s: a second block >= kRiskLow within this window counts as risk
        kRestoreBlocks = 690, // 2 s of headroom per restored voice
        kOverrun = 1000,      // permille of the block period
        kRiskHigh = 980,
        kRiskLow = 950,
        kDeep = 880,
        kHigh = 800,
        kQuietAvg = 650,
        kQuietBlock = 900,
        kRiskFloor = 4        // risk and "high average" cuts keep at least this many voices
    };
    int budget = 8;    // pool size
    int cap = 8;       // voices the synth may sound now
    uint32_t hold = 0, riskAge = 100000, good = 0;
    uint32_t avg = 0; // average load in permille * 16 (one pole over about 32 blocks)

    void reset()
    {
        cap = budget;
        hold = good = 0;
        riskAge = 100000;
        avg = 0;
    }
    // Per block, after the render. load = the block's load in permille (0 = unknown / not measured), sounding = voices that sound.
    // Returns the new cap (the caller steals voices down to it).
    int update(uint32_t load, int sounding)
    {
        avg = (uint32_t)((int32_t)avg + (((int32_t)(load << 4) - (int32_t)avg) >> 5)); // avg16 += (L*16 - avg16) / 32 (signed: the load can fall)
        const uint32_t a = avg >> 4;
        if (hold)
            hold--;
        if (riskAge < 1000000)
            riskAge++;
        if (sounding > 0 && hold == 0)
        {
            int nc = cap;
            if (load >= kOverrun)
                nc = sounding - 1;
            else if (load >= kRiskHigh || (load >= kRiskLow && riskAge < kRiskWindow))
            {
                nc = (cap < sounding ? cap : sounding) - 1;
                if (nc < kRiskFloor)
                    nc = kRiskFloor < cap ? kRiskFloor : cap;
            }
            else if (a >= kDeep)
                nc = cap - 1;
            else if (a >= kHigh)
            {
                nc = cap - 1;
                if (nc < kRiskFloor)
                    nc = cap < kRiskFloor ? cap : kRiskFloor;
            }
            if (nc < 0)
                nc = 0;
            if (nc < cap)
            {
                cap = nc;
                hold = kHoldBlocks;
                good = 0;
            }
        }
        if (load >= kRiskLow)
            riskAge = 0;
        if (a < kQuietAvg && load < kQuietBlock)
        {
            if (++good >= kRestoreBlocks && cap < budget)
            {
                cap++;
                good = 0;
            }
        }
        else
            good = 0;
        return cap;
    }
};

} // namespace plinkyvoice
