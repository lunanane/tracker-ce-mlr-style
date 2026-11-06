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
//
// v0.4.3 (owner, device: "before v0.4 there was no governor and no audible problems at high load"): the governor acts ONLY on a real overrun
// risk, never on a high but safe average. A cut needs a block at >= 97 % of the block period (one voice, not below kRiskFloor) or an
// actual overrun (>= 100 %: down to sounding - 1, not below kMinCap). The average-based cuts (>= 80 % / >= 88 %) and the "two blocks >= 95 %
// within a second" rule are gone. Restore is prompt: one voice per kRestoreBlocks (0.25 s) of blocks below 93 % while the predicted
// average with one more voice stays below 90 % (under the 97 % cut, so a restored voice does not cut itself again).
#pragma once
#include <stdint.h>

namespace plinkyvoice {

struct Governor
{
    enum
    {
        kHoldBlocks = 86,      // 250 ms after a cut nothing is cut again
        kRestoreBlocks = 86,   // 0.25 s of headroom per restored voice (v0.4.3; was 230 = 0.67 s)
        kOverrun = 1000,       // permille of the block period: the block took longer than its period (an audible overrun)
        kRisk = 970,           // a block this close to the period is a real overrun risk: one voice goes (v0.4.3; replaces 980 / 2 x 950)
        kNearBlock = 960,      // a block at or above this restarts the restore count
        kQuietBlock = 930,     // restore counts only blocks below this (between the two the count loses a quarter)
        kRestoreTarget = 900,  // ... and only while the predicted average with one more voice stays below this (kRisk - 70)
        kSpikeClamp = 1100,    // a block counts at most this much in the average
        kDefaultVoice = 40,    // permille one sounding voice costs when the synth does not tell
        kRiskFloor = 4,        // a risk cut keeps at least this many voices (an overrun may go lower)
        kMinCap = 1            // the governor never silences the synth
    };
    int budget = 8;    // pool size
    int cap = 8;       // voices the synth may sound now
    uint32_t hold = 0, good = 0;
    uint32_t avg = 0; // average load in permille * 16 (one pole over about 32 blocks)

    void reset()
    {
        cap = budget;
        hold = good = 0;
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
        if (sounding > 0 && hold == 0)
        {
            int nc = cap;
            if (load >= kOverrun)
                nc = sounding - 1;
            else if (load >= kRisk)
            {
                nc = (cap < sounding ? cap : sounding) - 1;
                if (nc < kRiskFloor)
                    nc = kRiskFloor < cap ? kRiskFloor : cap;
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
        if (cap < budget)
        {
            // the average if the cap were one higher and every voice it allows sounded
            const uint32_t vc = voiceCost ? voiceCost : (uint32_t)kDefaultVoice;
            int extra = cap + 1 - sounding;
            if (extra < 1)
                extra = 1;
            const uint32_t predicted = a + vc * (uint32_t)extra;
            if (load >= kNearBlock)
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
