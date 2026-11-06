// Sequencer tracks mod - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Host tests of the CPU monitor (plan 022 P1): window counters, percentages, and the overlay text against the display budget.
#include "seqCpuLogic.h"
#include "seqTraceStages.h"
#include <stdio.h>
#include <string.h>

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                \
    do                                                          \
    {                                                           \
        g_checks++;                                             \
        if (!(c))                                               \
        {                                                       \
            g_fail++;                                           \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
        }                                                       \
    } while (0)

using namespace seqcpu;

static void test_window()
{
    Snap s = {};
    // 172 blocks at 40 % each: the window completes on the 172nd block
    bool done = false;
    for (int i = 0; i < kWindowBlocks; i++)
    {
        CHECK(!done);
        s.stage[S_PLINKY] += kBlockCycles / 10; // 10 %
        done = snapBlock(s, (uint32_t)(kBlockCycles * 0.4));
    }
    CHECK(done && s.blocks == (uint32_t)kWindowBlocks);
    CHECK(pct10(s, s.isrSum) == 400 || pct10(s, s.isrSum) == 399);
    CHECK(pct10(s, s.stage[S_PLINKY]) == 100 || pct10(s, s.stage[S_PLINKY]) == 99);
    snapClear(s, s.isrMax);
    CHECK(s.blocks == 0 && s.isrSum == 0 && s.isrMax > 0); // the maximum survives the window
    snapBlock(s, 100);
    CHECK(s.isrMax > 100 && s.isrPeak == 100);
    // per second: 172 blocks = 0.4967 s
    Snap w = {};
    w.blocks = kWindowBlocks;
    CHECK(perSec(w, 100) >= 199 && perSec(w, 100) <= 202);
    // reads and passes
    snapRead(w, R_CUE, 90000);
    snapRead(w, R_CUE, 180000);
    CHECK(w.rdCalls[R_CUE] == 2 && w.rdCycles[R_CUE] == 270000 && w.rdPeak == 180000);
    CHECK(ms10(180000) == 10 && ms10(1800000) == 100);
    snapPass(w, 5000);
    snapPass(w, 9000);
    CHECK(w.passes == 2 && w.passSum == 14000 && w.passPeak == 9000);
    // an empty window never divides by zero
    Snap z = {};
    CHECK(pct10(z, 123) == 0 && perSec(z, 5) == 0);
}

// The overlay is ONE cTextBox in the Info screen (450 px tall, 780 wide): it is drawn as one stage of CMD_TEXT lines (12 bytes + the
// string, padded to 4). Worst case = every number at its widest.
static void test_text_budget()
{
    Snap s = {};
    s.blocks = kWindowBlocks;
    s.isrSum = 4000000000u;
    s.isrPeak = s.isrMax = 4000000000u;
    for (int i = 0; i < S_COUNT; i++)
        s.stage[i] = 4000000000u;
    s.passes = 99999;
    s.passSum = 4000000000u;
    s.passPeak = 4000000000u;
    s.jobs = s.syncFills = s.ringMiss = s.jumps = s.restarts = s.reuses = s.cueHits = 4000000000u;
    for (int i = 0; i < R_COUNT; i++)
        s.rdCalls[i] = s.rdCycles[i] = 4000000000u;
    s.rdPeak = 4000000000u;
    Extra e = { 8, 8, 1000, 1000 };
    char buf[1100];
    const int n = format(s, e, buf, sizeof(buf));
    CHECK(n > 0 && n == (int)strlen(buf) && n < (int)sizeof(buf) - 1);
    int lines = 0, fifo = 8, longest = 0;
    const char* p = buf;
    while (*p)
    {
        const char* q = strchr(p, '\n');
        char line[200];
        int len = q ? (int)(q - p) : (int)strlen(p);
        memcpy(line, p, (size_t)len);
        line[len] = 0;
        fifo += tracestage::textBytes(line);
        if (len > longest)
            longest = len;
        lines++;
        p = q ? q + 1 : p + len;
    }
    printf("cpu monitor: %d chars, %d lines, longest %d chars, %d FIFO bytes (limit %d)\n", n, lines, longest, fifo, (int)tracestage::kStageBytes);
    CHECK(fifo <= tracestage::kStageBytes);
    CHECK(lines <= 15); // the text box shows (450 / line step) - 1 lines
    CHECK(longest <= 112); // absurd 10-digit numbers; real lines are under 80 (the box wraps at 775 px anyway)
    // typical numbers
    Snap t = {};
    t.blocks = kWindowBlocks;
    t.isrSum = (uint32_t)(0.41 * kBlockCycles * kWindowBlocks);
    t.stage[S_SMP_STR] = (uint32_t)(0.081 * kBlockCycles * kWindowBlocks);
    format(t, e, buf, sizeof(buf));
    CHECK(strstr(buf, "streamed 8.1") != nullptr || strstr(buf, "streamed 8.0") != nullptr);
    CHECK(strstr(buf, "avg 41.0%") != nullptr || strstr(buf, "avg 40.9%") != nullptr);
    CHECK(strstr(buf, "cap 8, sounding 8") != nullptr);
    Extra none = { -1, 0, 0, 0 };
    format(t, none, buf, sizeof(buf));
    CHECK(strstr(buf, "cap") == nullptr);
    // a tiny buffer never overruns
    char tiny[24];
    format(s, e, tiny, sizeof(tiny));
    CHECK(strlen(tiny) < sizeof(tiny));
}

int main()
{
    test_window();
    test_text_budget();
    printf("seqcpu monitor: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
