// Sequencer tracks mod (plan 019 A, plan 022 P2) - PolyForm Noncommercial 1.0.0, see seqgen/LICENSE.md. No Polyend includes, host-tested.
// CPU governor of the shared voice pool: from the measured load of the whole audio block (permille of the block period, from the
// DWT cycle counter) it lowers the number of voices the synth may sound, and raises it again when there is real headroom. Pure integer
// compares, once per block. The control law (hysteresis: cut fast on an overrun, slowly on a high average) follows the description in
// plan 018 of the OpenTracker governor (Apache-2.0); written here from that description, no code copied.
// Constants are in blocks of 128 samples at 44.1 kHz (2.9 ms).
//
// Plan 022 P2 (owner, device: "the synth engine got more easy to drop out, and then it can't come back"): the old restore rule wanted the
// average load below a fixed 65 % for 2 s - with an MLR stream or a reverb already using that much it never came back, and a cut down to
// 0 voices could not recover at all. Now:
//  - restore compares the load PREDICTED for one more voice (average + the cost of the voices the new cap allows, measured per voice by
//    the synth, a default when unknown) with a target that lies below the cut threshold, so a cut load and a restored load never meet
//    (no oscillation), and a steady background load (MLR, reverb) simply settles at the number of voices that fit;
//  - one voice per kRestoreBlocks (0.67 s) while the blocks stay quiet; a single bad block only delays the count (it does not reset it),
//    a block near the limit resets it; a spike is clamped before it enters the average (SD / stream catch-up must not poison it);
//  - the cap never goes below kMinCap: the synth is never silenced by the governor.
#pragma once
#include <stdint.h>

namespace plinkyvoice {

struct Governor
{
    enum
    {
        kHoldBlocks = 86,      // 250 ms after a cut nothing is cut again
        kRiskWindow = 345,     // 1 s: a second block >= kRiskLow within this window counts as risk
        kRestoreBlocks = 230,  // 0.67 s of headroom per restored voice
        kOverrun = 1000,       // permille of the block period
        kRiskHigh = 980,
        kRiskLow = 950,
        kDeep = 880,
        kHigh = 800,
        kQuietBlock = 900,     // restore counts only blocks below this
        kRestoreTarget = 770,  // ... and only while the predicted average with one more voice stays below this (kHigh - 30)
        kSpikeClamp = 1100,    // a block counts at most this much in the average
        kDefaultVoice = 40,    // permille one sounding voice costs when the synth does not tell
        kRiskFloor = 4,        // risk and "high average" cuts keep at least this many voices (an overrun may go lower)
        kMinCap = 1            // the governor never silences the synth
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
    // Per block, after the render. load = the block's load in permille (0 = unknown / not measured), sounding = voices that sound,
    // voiceCost = permille one voice costs (0 = unknown). Returns the new cap (the caller steals voices down to it).
    int update(uint32_t load, int sounding, uint32_t voiceCost = 0)
    {
        const uint32_t lc = load > (uint32_t)kSpikeClamp ? (uint32_t)kSpikeClamp : load;
        avg = (uint32_t)((int32_t)avg + (((int32_t)(lc << 4) - (int32_t)avg) >> 5)); // avg16 += (L*16 - avg16) / 32 (signed: the load can fall)
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
            if (nc < kMinCap)
                nc = kMinCap < cap ? kMinCap : cap;
            if (nc < cap)
            {
                cap = nc;
                hold = kHoldBlocks;
                good = 0;
            }
        }
        if (load >= kRiskLow)
            riskAge = 0;
        if (cap < budget)
        {
            // the average if the cap were one higher and every voice it allows sounded
            const uint32_t vc = voiceCost ? voiceCost : (uint32_t)kDefaultVoice;
            int extra = cap + 1 - sounding;
            if (extra < 1)
                extra = 1;
            const uint32_t predicted = a + vc * (uint32_t)extra;
            if (load >= kRiskLow)
                good = 0; // near the limit: start the count again
            else if (load < kQuietBlock && predicted < kRestoreTarget)
            {
                if (++good >= kRestoreBlocks)
                {
                    cap++;
                    good = 0;
                }
            }
            else
                good -= good >> 2; // a worse block or no headroom: the count loses a quarter (steady no-headroom lets it run down)
        }
        else
            good = 0;
        return cap;
    }
};

} // namespace plinkyvoice
