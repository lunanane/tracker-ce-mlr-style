// Host tests for the sample pool accounting decisions (plan 023).
#include "sampleMemLogic.h"
#include <cstdio>

using namespace sampmem;

static int fails = 0;
#define CHECK(c)                                        \
    do                                                  \
    {                                                   \
        if (!(c))                                       \
        {                                               \
            printf("FAIL line %d: %s\n", __LINE__, #c); \
            fails++;                                    \
        }                                               \
    } while (0)

int main()
{
    const uint32_t MB = 1024 * 1024;
    const uint32_t free7 = 7 * MB; // roughly what is left of the MK66 pool in a fresh project

    // Reproduces the refusal: a 6 minute mono sample (44.1 kHz) is 31 MB.
    uint32_t longFrames = 6 * 60 * 44100;
    CHECK(!legacyImportFits(longFrames, 1, free7));                          // before: refused
    CHECK(decideImport(longFrames, 1, false, free7) == Placement::Streamed); // now: streamed
    CHECK(importResidentBytes(longFrames, 1) == 65536); // only the pre-cache counts
    CHECK(importResidentBytes(longFrames, 2) == 131072);

    // fits -> old resident path, unchanged
    CHECK(legacyImportFits(1000000, 1, free7));
    CHECK(decideImport(1000000, 1, false, free7) == Placement::Resident);

    // small sample in a full pool is still refused
    CHECK(decideImport(1000, 1, false, 500) == Placement::Refused);
    CHECK(decideImport(100000, 1, false, 100000) == Placement::Refused); // 200 KB < threshold

    // sample >= threshold, pool has room only for the pre-cache (+ the importer's safety slack)
    CHECK(decideImport(300000, 1, false, 70000) == Placement::Refused);
    CHECK(decideImport(300000, 1, false, 80000) == Placement::Streamed);
    CHECK(decideImport(300000, 2, false, 80000) == Placement::Refused); // stereo pre-cache 128 KB
    CHECK(decideImport(300000, 2, false, 145000) == Placement::Streamed);

    // existing slot in wavetable/granular mode: streamed but forced to one-shot
    CHECK(decideImport(longFrames, 1, true, free7) == Placement::StreamedOneShot);

    // safety margin
    CHECK(decideImport(100000, 1, false, 215000) == Placement::Resident);
    CHECK(decideImport(100000, 1, false, 200000 + kImportSafetyBytes) == Placement::Refused);

    // project load
    CHECK(decideLoad(longFrames, 1, true, false, free7) == Placement::Streamed); // as before
    CHECK(decideLoad(longFrames, 1, true, false, 1000) == Placement::Refused);
    // slice mode on MK66 is "not streamable"; a 20 MB sample used to be dropped on load
    CHECK(decideLoad(10 * 1000 * 1000, 1, false, false, free7) == Placement::Streamed);
    CHECK(decideLoad(1000000, 1, false, false, free7) == Placement::Resident);
    // wavetable / granular that cannot be resident
    CHECK(decideLoad(10 * 1000 * 1000, 1, false, true, free7) == Placement::StreamedOneShot);
    CHECK(decideLoad(1000, 1, false, true, 10) == Placement::Refused);
    CHECK(decideLoad(1000, 1, false, true, 2000) == Placement::Resident);

    // promote to resident
    CHECK(canPromote(1000000, 1, 32768, free7));
    CHECK(!canPromote(longFrames, 1, 32768, free7));
    CHECK(canPromote(1000, 1, 1000, 0));

    // a streamed sample costs the same regardless of its length
    CHECK(importResidentBytes(10 * 1000 * 1000, 1) == importResidentBytes(100 * 1000 * 1000, 1));

    // plan 048: pool guards never wrap (the unsigned `max - used` of the old load check did)
    CHECK(poolFree(100, 40) == 60);
    CHECK(poolFree(100, 100) == 0);
    CHECK(poolFree(100, 101) == 0);         // used > max: no free bytes, not 4 GB
    CHECK(poolFree(0, 0xFFFFFFFFu) == 0);
    const uint32_t pool = 45u * 256u * 1024u;
    CHECK(fitsPool(0, pool, pool));         // exactly the whole pool
    CHECK(!fitsPool(0, pool + 2, pool));
    CHECK(fitsPool(pool - 1000, 1000, pool));
    CHECK(!fitsPool(pool - 1000, 1002, pool));
    CHECK(!fitsPool(pool + 2, 0, pool));    // starts past the end
    CHECK(!fitsPool(pool - 10, 0xFFFFFFF0u, pool)); // offset + bytes would wrap a 32-bit sum
    // a project that filled the 45 unit pool (bytes of 11,25 MB) must still load; 44 units would drop its last sample
    CHECK(fitsPool(11u * 1024u * 1024u, 256u * 1024u, 45u * 256u * 1024u));
    CHECK(!fitsPool(11u * 1024u * 1024u, 256u * 1024u, 44u * 256u * 1024u));

    if (fails)
    {
        printf("sampmem tests: %d FAILED\n", fails);
        return 1;
    }
    printf("sampmem tests: all passed\n");
    return 0;
}
