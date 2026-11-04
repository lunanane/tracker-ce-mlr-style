// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Host tests of the alt Performance page mapping logic (plans/004-perform-faders.md).
#include "seqPerformLogic.h"
#include <stdio.h>

using namespace seqperf;

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

static void test_pad_map()
{
    for (int row = 0; row < 4; row++)
        for (int col = 0; col < 8; col++)
        {
            PadInfo p = mapPad(padOf(row, col));
            CHECK(p.cell == CELL_FADER && p.index == row && p.step == col + 1);
        }
    for (int row = 0; row < 3; row++)
    {
        PadInfo p = mapPad(padOf(row, 8));
        CHECK(p.cell == CELL_GROUP && p.index == row);
    }
    CHECK(mapPad(padOf(3, 8)).cell == CELL_NONE); // dark on purpose
    CHECK(mapPad(padOf(0, 9)).cell == CELL_PING);
    CHECK(mapPad(padOf(1, 9)).cell == CELL_REV);
    CHECK(mapPad(padOf(2, 9)).cell == CELL_NONE);
    CHECK(mapPad(padOf(3, 9)).cell == CELL_NONE);
    for (int row = 0; row < 4; row++)
    {
        PadInfo l = mapPad(padOf(row, 10)), r = mapPad(padOf(row, 11));
        CHECK(l.cell == CELL_LATCH && l.index == row);
        CHECK(r.cell == CELL_RESET && r.index == row);
    }
    CHECK(mapPad(48).cell == CELL_NONE);
    CHECK(mapPad(255).cell == CELL_NONE);
}

static void test_bar()
{
    CHECK(barMask(0) == 0x00);
    CHECK(barMask(1) == 0x01);
    CHECK(barMask(3) == 0x07);
    CHECK(barMask(7) == 0x7F);
    CHECK(barMask(8) == 0xFF);
    CHECK(barMask(200) == 0xFF);
}

static void test_pages_and_slots()
{
    CHECK(slotOf(PAGE_FX1, 0) == 0 && slotOf(PAGE_FX1, 3) == 3);
    CHECK(slotOf(PAGE_FX2, 0) == 4 && slotOf(PAGE_FX2, 3) == 7);
    CHECK(slotOf(PAGE_FX3, 0) == 8 && slotOf(PAGE_FX3, 3) == 11);
    CHECK(isFxPage(PAGE_FX3) && !isFxPage(PAGE_PING) && !isFxPage(PAGE_REV));
    CHECK(pageForCell(CELL_GROUP, 1, PAGE_PING) == PAGE_FX2);
    CHECK(pageForCell(CELL_PING, 0, PAGE_FX1) == PAGE_PING);
    CHECK(pageForCell(CELL_REV, 0, PAGE_FX1) == PAGE_REV);
    CHECK(pageForCell(CELL_FADER, 0, PAGE_REV) == PAGE_REV);
    CHECK(pageForCell(CELL_LATCH, 0, PAGE_FX3) == PAGE_FX3);
}

static void test_fx_value()
{
    CHECK(fxValue(0, -100, 100) == 0);
    CHECK(fxValue(8, -100, 100) == 100);
    CHECK(fxValue(4, -100, 100) == 0); // middle step of a signed range = neutral
    CHECK(fxValue(1, -100, 100) == -75);
    CHECK(fxValue(8, 0, 100) == 100);
    CHECK(fxValue(1, 0, 100) == 13);
    CHECK(fxValue(8, 0, 7) == 7);
    CHECK(fxValue(9, 0, 100) == 100); // clamped
    int16_t last = -32768;
    for (int s = 1; s <= 8; s++)
    {
        int16_t v = fxValue((uint8_t)s, -24, 24);
        CHECK(v >= last);
        last = v;
    }
    CHECK(fxValue(8, -24, 24) == 24);
}

static void test_send_value()
{
    CHECK(sendValue(0) == 0);
    CHECK(sendValue(8) == 100);
    CHECK(sendValue(4) == 50);
    CHECK(sendValue(1) == 13);
    CHECK(sendValue(99) == 100);
}

static void test_params()
{
    CHECK(paramValue(P_TIME, 1) == 54);
    CHECK(paramValue(P_TIME, 8) == 3500);
    CHECK(paramValue(P_RATE, 1) == 0);
    CHECK(paramValue(P_RATE, 8) == 18);
    CHECK(paramValue(P_FEEDBACK, 1) == 0);
    CHECK(paramValue(P_FEEDBACK, 8) == 99);
    CHECK(paramValue(P_FILTER, 1) == 194);
    CHECK(paramValue(P_FILTER, 8) == 20000);
    CHECK(paramValue(P_SIZE, 1) == 0.0);
    CHECK(paramValue(P_PREDELAY, 8) == 1.0);
    CHECK(paramValue(P_DAMP, 99) == 1.0 && paramValue(P_DAMP, 0) == 0.0); // clamped
    for (int p = 0; p < P_COUNT; p++)
    {
        double last = -1;
        for (int s = 1; s <= 8; s++)
        {
            double v = paramValue((Param)p, s);
            CHECK(v >= last);
            last = v;
        }
    }
    CHECK(paramValue(P_TIME, 1) >= 1 && paramValue(P_TIME, 8) <= 3500);
    CHECK(paramValue(P_FILTER, 1) >= 100 && paramValue(P_FILTER, 8) <= 20000);
    CHECK(paramValue(P_RATE, 8) <= 18);
}

static void test_nearest()
{
    for (int p = 0; p < P_COUNT; p++)
        for (int s = 1; s <= 8; s++)
            CHECK(nearestStep((Param)p, paramValue((Param)p, s)) == s);
    CHECK(nearestStep(P_TIME, 500) == 3);     // project default 500 ms
    CHECK(nearestStep(P_FEEDBACK, 50) == 5);  // default 50 (57 is nearer than 42)
    CHECK(nearestStep(P_FILTER, 20000) == 8); // default
    CHECK(nearestStep(P_SIZE, 0.45) == 4 && nearestStep(P_SIZE, 0.55) == 5); // default 0.5 sits between
    CHECK(nearestStep(P_TIME, 1) == 1);
    CHECK(nearestStep(P_TIME, 99999) == 8);
}

static void test_pad_lit()
{
    PadInfo g1 = mapPad(padOf(1, 8)), ping = mapPad(padOf(0, 9)), rev = mapPad(padOf(1, 9));
    PadInfo lat = mapPad(padOf(2, 10)), rst = mapPad(padOf(2, 11)), dark = mapPad(padOf(3, 8));
    CHECK(padIsLit(g1, PAGE_FX2, false) && !padIsLit(g1, PAGE_FX1, false));
    CHECK(padIsLit(ping, PAGE_PING, false) && !padIsLit(ping, PAGE_REV, false));
    CHECK(padIsLit(rev, PAGE_REV, false) && !padIsLit(rev, PAGE_PING, false));
    CHECK(padIsLit(lat, PAGE_FX1, true) && !padIsLit(lat, PAGE_FX1, false));
    CHECK(padIsLit(rst, PAGE_FX1, false));
    CHECK(!padIsLit(dark, PAGE_FX1, true) && !padIsLit(dark, PAGE_PING, true));
}

int main()
{
    test_pad_map();
    test_bar();
    test_pages_and_slots();
    test_fx_value();
    test_send_value();
    test_params();
    test_nearest();
    test_pad_lit();
    printf("seqperf: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
