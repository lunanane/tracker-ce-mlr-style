// Sequencer tracks mod (plan 027) - PolyForm Noncommercial 1.0.0, see seqgen/LICENSE.md. No Polyend includes, host-tested.
// SRAM tiles for the SDRAM delay lines of the ring reverb. Every stage of the ring is a delay line that is walked backwards one sample
// per output sample, so the block of 128 samples touches a short run of consecutive addresses at each of a dozen places. On the device
// the ring is in SDRAM (uncached, 16 bit port: a random 16 bit read is about 20 cycles, a write about 10): 30 single accesses per
// sample. A tile set copies those runs into SRAM with word bursts (ldm / stm), the block runs on the copies and the changed runs are
// written back. The arithmetic is untouched, the result is bit-identical (test_fx.cpp compares ring memory and output with the
// untiled loop over many blocks).
//
// Offsets are relative to an index `base` of a ring with a power of two size; a tile is an inclusive run [lo, hi] of offsets. Runs
// are widened so that they start on an even and end on an odd ring index (4 byte aligned word copies), sorted and merged when they
// touch or overlap, so one ring cell is never in two tiles.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace ringverb {

struct TileSet
{
    struct Iv
    {
        int16_t lo, hi;
        bool dirty;
        int16_t* mem;
    };
    enum { kMax = 16 };
    Iv iv[kMax];
    int n = 0;
    int16_t* ring = nullptr;
    uint32_t base = 0, mask = 0;
    bool laidOut = false;

    void init(int16_t* ring_, uint32_t base_, uint32_t mask_)
    {
        ring = ring_;
        base = base_;
        mask = mask_;
        n = 0;
        laidOut = false;
    }
    // widen to whole words, insert in order, merge. False when the set is full.
    bool add(int lo, int hi, bool dirty)
    {
        if (hi < lo)
            return true;
        if (lo < -2100 || hi > 30100)
            return false; // runs stay within less than half a ring: no ring cell can be in two of them
        if ((base + (uint32_t)lo) & 1u)
            lo--;
        if (!((base + (uint32_t)hi) & 1u))
            hi++;
        int k = 0;
        while (k < n && iv[k].lo < lo)
            k++;
        if (n >= kMax)
            return false;
        for (int m = n; m > k; m--)
            iv[m] = iv[m - 1];
        iv[k].lo = (int16_t)lo;
        iv[k].hi = (int16_t)hi;
        iv[k].dirty = dirty;
        iv[k].mem = nullptr;
        n++;
        // merge neighbours that touch or overlap
        for (int m = 0; m + 1 < n;)
        {
            if (iv[m].hi + 1 >= iv[m + 1].lo)
            {
                if (iv[m + 1].hi > iv[m].hi)
                    iv[m].hi = iv[m + 1].hi;
                iv[m].dirty = iv[m].dirty || iv[m + 1].dirty;
                for (int q = m + 1; q + 1 < n; q++)
                    iv[q] = iv[q + 1];
                n--;
            }
            else
                m++;
        }
        return true;
    }
    int words() const
    {
        int w = 0;
        for (int k = 0; k < n; k++)
            w += (int)iv[k].hi - (int)iv[k].lo + 1;
        return w;
    }
    // hands out storage from `scratch` (advanced by what was used); false when it does not fit
    bool layout(int16_t*& scratch, int& room)
    {
        for (int k = 0; k < n; k++)
        {
            const int len = (int)iv[k].hi - (int)iv[k].lo + 1;
            if (len > room)
                return false;
            iv[k].mem = scratch;
            scratch += len;
            room -= len;
        }
        laidOut = true;
        return true;
    }
    // copy between the ring and a tile, wrapping at the end of the ring (both pieces stay word aligned: the ring size is even)
    void copy(const Iv& v, bool toTile) const
    {
        const uint32_t len = (uint32_t)((int)v.hi - (int)v.lo + 1);
        const uint32_t a = (base + (uint32_t)v.lo) & mask;
        const uint32_t first = a + len <= mask + 1 ? len : mask + 1 - a;
        if (toTile)
        {
            memcpy(v.mem, ring + a, first * sizeof(int16_t));
            if (first < len)
                memcpy(v.mem + first, ring, (len - first) * sizeof(int16_t));
        }
        else
        {
            memcpy(ring + a, v.mem, first * sizeof(int16_t));
            if (first < len)
                memcpy(ring, v.mem + first, (len - first) * sizeof(int16_t));
        }
    }
    void load() const
    {
        for (int k = 0; k < n; k++)
            copy(iv[k], true);
    }
    void store() const
    {
        for (int k = 0; k < n; k++)
            if (iv[k].dirty)
                copy(iv[k], false);
    }
    // the tile cell of offset x (which must lie inside a run); nullptr when it does not
    int16_t* ptr(int x) const
    {
        for (int k = 0; k < n; k++)
            if (x >= iv[k].lo && x <= iv[k].hi)
                return iv[k].mem + (x - iv[k].lo);
        return nullptr;
    }
};

} // namespace ringverb
