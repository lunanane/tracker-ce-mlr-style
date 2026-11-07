// Sequencer tracks mod (plan 024) - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Host tests of the Master fader pages: pad map, value tables, LEDs, the screen model (masterFaderView.h) and the display list of the real
// Perform fader screen control (seqPerform/faderView.cpp against masterstub/) in two stages, FT81x command FIFO bytes per stage <= 3840.
// SEQ_MASTER_PREVIEW=<dir> also writes the draw ops of every page (make master-preview rasterises them to PNG).
#include "../interface/seqPerform/faderView.cpp"
#include "../interface/masterParams/masterFaderView.h"

#include <stdlib.h>
#include <string>

FILE* g_ops = nullptr;
int g_bytes = 0;
cDisplayStub display;

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

using namespace seqmf;

static void test_pad_map()
{
    // the faders: columns 1-8, one row each, on every page
    for (uint8_t page = 0; page < MP_COUNT; page++)
        for (uint8_t row = 0; row < 4; row++)
        {
            for (uint8_t col = 0; col < 8; col++)
            {
                const seqmf::PadInfo p = mapPad((uint8_t)(row * 12 + col), page);
                if (page == MP_BR)
                    CHECK(p.cell == MC_BR && p.index == row);
                else
                    CHECK(p.cell == MC_FADER && p.index == row && p.step == col + 1);
            }
            // the page menu: column 9, row = page, on every page
            const seqmf::PadInfo m = mapPad((uint8_t)(row * 12 + 8), page);
            CHECK(m.cell == MC_PAGE && m.index == row);
        }
    CHECK(mapPad(9, MP_REVERB).cell == MC_NONE && mapPad(2 * 12 + 9, MP_REVERB).cell == MC_NONE && mapPad(3 * 12 + 9, MP_HP).cell == MC_NONE);
    CHECK(mapPad(2 * 12 + 9, MP_DELAY).cell == MC_PINGPONG && mapPad(3 * 12 + 9, MP_DELAY).cell == MC_SYNC && mapPad(12 + 9, MP_DELAY).cell == MC_NONE);
    for (uint8_t row = 0; row < 4; row++)
    {
        const seqmf::PadInfo r = mapPad((uint8_t)(row * 12 + 11), MP_HP);
        CHECK(r.cell == MC_RESET && r.index == row);
        CHECK(mapPad((uint8_t)(row * 12 + 10), MP_HP).cell == MC_NONE);
        CHECK(mapPad((uint8_t)(row * 12 + 10), MP_BR).cell == MC_BR && mapPad((uint8_t)(row * 12 + 11), MP_BR).cell == MC_BR);
    }
    CHECK(mapPad(48, MP_HP).cell == MC_NONE);
    // the beat repeat page hands columns 10-12 to the CUT page controller, which has its mode / latch / off pads there
    CHECK(seqbr::mapPad(9, 0).cell == seqbr::BC_MODE && seqbr::mapPad(10, 0).cell == seqbr::BC_LATCH && seqbr::mapPad(11, 0).cell == seqbr::BC_OFF);
    CHECK(perfPage(MP_DELAY) == seqperf::PAGE_PING && perfPage(MP_REVERB) == seqperf::PAGE_REV && seqmf::isFxPage(MP_DELAY) && !seqmf::isFxPage(MP_HP));
}

static void test_values()
{
    for (uint8_t st = 1; st <= 8; st++)
    {
        CHECK(levelStep(levelOf(st)) == st);
        if (st > 1)
            CHECK(levelOf(st) > levelOf((uint8_t)(st - 1)));
    }
    CHECK(levelOf(1) == 0 && levelOf(8) == 100 && levelStep(0) == 1 && levelStep(100) == 8 && levelStep(50) == 5);
    // the reused Perform tables: pad 1 = minimum, pad 8 = the cap
    CHECK(seqperf::paramValue(seqperf::P_FEEDBACK, 8) == seqfx::kDelayFeedbackCap && seqperf::paramValue(seqperf::P_SIZE, 1) == 0.0);
    // the high-pass: eight steps up the master, pad 1 = 60 Hz, pad 8 = 5.1 kHz, off below
    seqbr::BrParams p;
    CHECK(hpCutStep(p) == 0);
    p.hpCut = 9;
    CHECK(hpCutStep(p) == 0);
    p.hpCut = 5;
    CHECK(hpCutStep(p) == 5 && p.settings().hpIdx == seqbr::hpIdxOf(5));
    CHECK(fabsf(seqfx::hpHz(seqbr::hpIdxOf(1)) - 60.0f) < 1.0f && fabsf(seqfx::hpHz(seqbr::hpIdxOf(8)) - 5120.0f) < 100.0f);
    CHECK(nextPage(MP_BR) == MP_REVERB && nextPage(MP_REVERB) == MP_DELAY && nextSub(2) == 0 && nextSub(0) == 1);
}

static void test_leds()
{
    LedIn in;
    memset(&in, 0, sizeof(in));
    uint8_t out[48];
    in.page = MP_REVERB;
    in.step[0] = 3;
    in.step[2] = 8;
    in.engaged[2] = 1;
    in.pageDot = 1u << MP_HP;
    fillLeds(in, out);
    CHECK(out[0] == kBar && out[2] == kBar && out[3] == 0);
    CHECK(out[2 * 12 + 7] == kBar && out[2 * 12 + 11] == kBright && out[11] == kFaint);
    CHECK(out[8] == kBright && out[12 + 8] == kDimPad && out[24 + 8] == kMid && out[36 + 8] == kDimPad); // page menu
    CHECK(out[2 * 12 + 9] == 0 && out[3 * 12 + 9] == 0);
    in.page = MP_DELAY;
    in.pingpong = 1;
    fillLeds(in, out);
    CHECK(out[12 + 8] == kBright && out[8] == kDimPad && out[2 * 12 + 9] == kPing && out[3 * 12 + 9] == 0);
    in.page = MP_HP;
    in.step[0] = 4;
    in.step[1] = 1;
    in.step[2] = 8; // rows 3 and 4 are not used: dark
    in.step[3] = 8;
    fillLeds(in, out);
    CHECK(out[3] == kBar && out[4] == 0 && out[12] == kBar && out[13] == 0);
    CHECK(out[24] == 0 && out[36 + 7] == 0 && out[24 + 11] == 0 && out[36 + 11] == 0 && out[11] == kFaint);
    CHECK(out[24 + 8] == kBright);
    // the beat repeat page shows the controller LEDs (0 .. 15 -> pwm 0 .. 30) and the menu on top
    in.page = MP_BR;
    for (int i = 0; i < 48; i++)
        in.br[i] = (uint8_t)(i % 16);
    fillLeds(in, out);
    CHECK(out[5] == 10 && out[15] == 30 && out[10] == 20);
    CHECK(out[3 * 12 + 8] == kBright && out[8] == kDimPad);
}

// ---- the screen ---------------------------------------------------------------------------------------------------------------------------
struct Stages
{
    int s0 = 0, s1 = 0;
};

static Stages draw(const strFaderView& vw, const char* name, FILE* ops)
{
    Stages st;
    cSeqFaderView ctl(nullptr);
    ctl.setData((void*)&vw);
    g_ops = ops;
    if (ops)
        fprintf(ops, "# %s\n", name);
    g_bytes = 4; // CMD_DLSTART of display.cpp
    ctl.update();
    st.s0 = g_bytes;
    g_bytes = 4;
    ctl.update2();
    st.s1 = g_bytes;
    g_ops = nullptr;
    return st;
}

static void fillFx(Snap& s, bool delay, bool dialled)
{
    s.page = delay ? MP_DELAY : MP_REVERB;
    snprintf(s.name[0], sizeof(s.name[0]), "%s", delay ? "Delay return" : "Reverb return");
    snprintf(s.value[0], sizeof(s.value[0]), "86 %%");
    s.step[0] = 7;
    if (delay)
    {
        snprintf(s.name[1], sizeof(s.name[1]), "Time");
        snprintf(s.value[1], sizeof(s.value[1]), "1531 ms");
        snprintf(s.name[2], sizeof(s.name[2]), "Feedback");
        snprintf(s.value[2], sizeof(s.value[2]), "63");
        snprintf(s.name[3], sizeof(s.name[3]), "BP center");
        snprintf(s.value[3], sizeof(s.value[3]), "2.7 kHz");
        s.pingpong = 1;
    }
    else
    {
        snprintf(s.name[1], sizeof(s.name[1]), "Size");
        snprintf(s.value[1], sizeof(s.value[1]), "64");
        snprintf(s.name[2], sizeof(s.name[2]), "Damp");
        snprintf(s.value[2], sizeof(s.value[2]), "43");
        snprintf(s.name[3], sizeof(s.name[3]), "Predelay");
        snprintf(s.value[3], sizeof(s.value[3]), "0");
    }
    s.step[1] = 6;
    s.step[2] = 4;
    s.step[3] = delay ? 5 : 1;
    if (dialled)
    {
        s.dialled[0] = s.dialled[2] = 1;
        s.pageDot = (uint8_t)(1u << s.page);
    }
}

static void test_screens()
{
    const char* dir = getenv("SEQ_MASTER_PREVIEW");
    int worst0 = 0, worst1 = 0;
    auto scene = [&](const char* name, const Snap& s) {
        strFaderView vw;
        fillView(vw, s);
        FILE* f = nullptr;
        if (dir)
        {
            char path[512];
            snprintf(path, sizeof path, "%s/mf_%s.ops", dir, name);
            f = fopen(path, "w");
        }
        const Stages st = draw(vw, name, f);
        if (f)
            fclose(f);
        worst0 = st.s0 > worst0 ? st.s0 : worst0;
        worst1 = st.s1 > worst1 ? st.s1 : worst1;
        // each stage is one SPI burst into the 4 KB FIFO: under kStageBytes (3840) including the blend call of the control (12)
        CHECK(st.s0 + 12 <= 3840);
        CHECK(st.s1 + 12 <= 3840);
        printf("  master faders %-16s stage 0: %4d B   stage 1: %4d B\n", name, st.s0, st.s1);
        return vw;
    };
    Snap s;
    fillFx(s, false, false);
    strFaderView vw = scene("reverb", s);
    CHECK(vw.master == 1 && vw.page == MP_REVERB && vw.band[0].step == 7 && strcmp(vw.band[0].name, "Reverb return") == 0 && vw.band[0].status == seqperf::RS_PROJECT);
    CHECK(strcmp(vw.band[1].state, "PROJECT") == 0 && vw.band[0].canLatch == 0);
    s = Snap();
    fillFx(s, false, true);
    vw = scene("reverb_dialled", s);
    CHECK(vw.band[0].status == seqperf::RS_DIALLED && vw.band[0].engaged == 1 && vw.band[1].engaged == 0 && vw.pageMask == 1u << MP_REVERB);
    s = Snap();
    fillFx(s, true, true);
    vw = scene("delay", s);
    CHECK(vw.page == MP_DELAY && strstr(vw.summary, "ping-pong ON") && strstr(vw.summary, "sync off"));
    s = Snap();
    s.page = MP_HP;
    vw = scene("hp_off", s);
    CHECK(strcmp(vw.band[0].value, "off") == 0 && vw.band[0].status == seqperf::RS_OFF && vw.band[2].usable == 0 && vw.band[3].usable == 0);
    s.hpCut = 5;
    s.hpRes = 4;
    s.pageDot = 1u << MP_HP;
    vw = scene("hp_on", s);
    CHECK(strcmp(vw.band[0].value, "1.7 kHz") == 0 || strstr(vw.band[0].value, "kHz") || strstr(vw.band[0].value, "Hz"));
    CHECK(vw.band[0].engaged == 1 && vw.band[0].status == seqperf::RS_LIVE && strcmp(vw.band[1].value, "43 %") == 0 && vw.band[1].engaged == 1);
    // the beat repeat pages, idle and with a running repeat, every sub page
    for (uint8_t sub = 0; sub < seqbr::BG_COUNT; sub++)
    {
        s = Snap();
        s.page = MP_BR;
        s.brSub = sub;
        seqbr::BrParams d;
        memcpy(s.brStep, d.step, sizeof(s.brStep));
        char nm[24];
        snprintf(nm, sizeof nm, "br%d_idle", sub);
        vw = scene(nm, s);
        CHECK(vw.master == 1 && vw.page == MP_BR && strstr(vw.summary, seqbr::pageName(sub)));
        s.brState = beatrepeat::S_REPEAT;
        s.brEventGrid = 2;
        s.brLatch = 1;
        s.repeatIdx = 7;
        s.cpu10 = 42;
        for (int i = 0; i < seqbr::BP_COUNT; i++)
            s.brStep[i] = (uint8_t)(8 - i % 5);
        s.pageDot = 1u << MP_BR;
        snprintf(nm, sizeof nm, "br%d_repeat", sub);
        vw = scene(nm, s);
        if (sub == seqbr::BG_FILTER)
            CHECK(vw.band[3].usable == 0 && strcmp(vw.band[0].name, "MIX LEVEL") == 0 && strcmp(vw.band[1].name, "BP CENTRE") == 0);
        else if (sub == seqbr::BG_REPEAT)
            CHECK(vw.band[0].canLatch == 1 && vw.band[0].latched == 1 && strcmp(vw.band[0].dis, "OFF") == 0 && strcmp(vw.band[0].state, "REPEAT") == 0);
    }
    // the Perform page's own view is unchanged by the master fields: master = 0 draws its five chips
    strFaderView pv;
    memset(&pv, 0, sizeof(pv));
    pv.page = seqperf::PAGE_REV;
    snprintf(pv.tracks, sizeof(pv.tracks), "affects tracks 1-4 7");
    for (int r = 0; r < 4; r++)
    {
        pv.band[r].usable = 1;
        snprintf(pv.band[r].name, sizeof(pv.band[r].name), "Name %d", r);
    }
    const Stages ps = draw(pv, "perform", nullptr);
    CHECK(ps.s0 + 12 <= 3840 && ps.s1 + 12 <= 3840);
    printf("  Perform fader screen (master = 0): stage 0: %d B   stage 1: %d B\n", ps.s0, ps.s1);
    printf("  Master fader screen worst: stage 0 %d B, stage 1 %d B of 3840\n", worst0, worst1);
    // every master snapshot is padding free: two equal snapshots compare equal with memcmp (the screen refreshes only on a change)
    Snap a, b;
    fillFx(a, true, true);
    fillFx(b, true, true);
    CHECK(memcmp(&a, &b, sizeof(a)) == 0);
}

int main()
{
    test_pad_map();
    test_values();
    test_leds();
    test_screens();
    printf("master faders: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
