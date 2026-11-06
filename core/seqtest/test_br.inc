// Sequencer tracks mod (plan 021) - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Beat repeat on the CUT / MLR page: pad map (fader page layout), values, controller actions, LEDs, legend, the layer switch ("BeatRpt"
// screen button), the settings sidecar and the screen (display budgets, bounds). Included from test_main.cpp after test_cut.inc (CountCtx,
// the stand-ins of test_cut.inc). The DSP itself is tested in test_beatrepeat.cpp.
#include "brDraw.h"

namespace
{
using namespace seqbr;

// helpers --------------------------------------------------------------------------------------------------------------------------------
Result lastAct() { return g_brActs.empty() ? Result{ BA_NONE, 0 } : g_brActs.back(); }

int press(Ui& ui, uint8_t col, uint8_t row, bool down, Result* out = nullptr)
{
    Result r[2];
    const int n = ui.pad(col, row, down, r);
    if (out)
        for (int i = 0; i < n; i++)
            out[i] = r[i];
    return n;
}

LedState ledsOf(const Ui& ui, uint8_t state = beatrepeat::S_IDLE, uint8_t evGrid = 0)
{
    LedState ls;
    memset(&ls, 0, sizeof(ls));
    ls.page = ui.page;
    ls.mode = ui.prm.mode;
    ls.latch = ui.latch ? 1 : 0;
    ls.heldMask = ui.heldMask();
    ls.state = state;
    ls.eventGrid = evGrid;
    memcpy(ls.step, ui.prm.step, sizeof(ls.step));
    return ls;
}

BrModel brScene(int scene)
{
    BrModel m;
    memset(&m, 0, sizeof(m));
    Ui ui;
    m.page = scene == 1 ? BG_AUTO : BG_REPEAT;
    m.mode = scene == 2 ? beatrepeat::MODE_INSERT : beatrepeat::MODE_MIX;
    memcpy(m.step, ui.prm.step, sizeof(m.step));
    m.bpm10 = 1200;
    m.cpu10 = 12;
    if (scene >= 1)
    {
        for (int i = 0; i < BP_COUNT; i++)
            m.step[i] = (uint8_t)(8 - i % 3);
        m.state = beatrepeat::S_REPEAT;
        m.eventGrid = 2;
        m.setGrid = 0;
        m.clamped = 1;
        m.repeatIdx = 7;
        m.levelDb = 36;
        m.pitchSemi = 21;
        m.latch = 1;
        m.heldMask = 0x24;
        m.waveOk = 1;
        for (int j = 0; j < kBrWave; j++)
        {
            m.hi[j] = (int8_t)(127 - j);
            m.lo[j] = (int8_t)(-127 + j);
        }
    }
    if (scene == 3)
        m.page = BG_AUTO;
    return m;
}
} // namespace

// ---- values -----------------------------------------------------------------------------------------------------------------------------
static void test_br_values()
{
    using namespace seqbr;
    const uint8_t pct[8] = { 0, 14, 29, 43, 57, 71, 86, 100 };
    const uint8_t var[8] = { 0, 1, 3, 4, 6, 7, 9, 10 };
    for (uint8_t s = 1; s <= 8; s++)
    {
        CHECK(percentOf(s) == pct[s - 1]);
        CHECK(variationOf(s) == var[s - 1]);
    }
    CHECK(percentOf(0) == 0 && percentOf(9) == 100 && variationOf(9) == 10);
    BrParams d;
    CHECK(d.isDefault());
    const beatrepeat::Settings s = d.settings();
    CHECK(s.mode == beatrepeat::MODE_MIX);                   // the default mode is MIX
    CHECK(s.chance == 0 && s.decay == 0 && s.pitchDecay == 0 && s.variation == 0);
    CHECK(s.gate == 7 && s.interval == 5 && s.grid == 2);    // 16/16, 1 bar, 1/16
    BrParams p;
    p.step[BP_CHANCE] = 5;
    p.step[BP_DECAY] = 8;
    p.step[BP_PITCH] = 4;
    p.step[BP_INTERVAL] = 1;
    p.step[BP_AUTOGRID] = 8;
    p.step[BP_GATE] = 3;
    p.step[BP_VARIATION] = 8;
    p.mode = beatrepeat::MODE_GATE;
    const beatrepeat::Settings q = p.settings();
    CHECK(q.chance == 57 && q.decay == 100 && q.pitchDecay == 43 && q.interval == 0 && q.grid == 7 && q.gate == 2 && q.variation == 10 && q.mode == 2);
    CHECK(!p.isDefault());
    char buf[16];
    valueText(BP_DECAY, 8, buf, sizeof(buf));
    CHECK(strcmp(buf, "-6.0 dB") == 0);
    valueText(BP_PITCH, 8, buf, sizeof(buf));
    CHECK(strcmp(buf, "-3.0 st") == 0);
    valueText(BP_CHANCE, 1, buf, sizeof(buf));
    CHECK(strcmp(buf, "off") == 0);
    valueText(BP_CHANCE, 8, buf, sizeof(buf));
    CHECK(strcmp(buf, "100 %") == 0);
    valueText(BP_INTERVAL, 1, buf, sizeof(buf));
    CHECK(strcmp(buf, "1/32") == 0);
    valueText(BP_INTERVAL, 8, buf, sizeof(buf));
    CHECK(strcmp(buf, "4 bars") == 0);
    valueText(BP_GATE, 8, buf, sizeof(buf));
    CHECK(strcmp(buf, "16/16") == 0);
    CHECK(strcmp(modeName(beatrepeat::MODE_INSERT), "REPLACE") == 0 && strcmp(modeName(beatrepeat::MODE_MIX), "MIX") == 0);
    // the decay curve of the blocks: -6 dB per block at 100
    CHECK(blockLevel1000(100, 0) == 1000 && abs(blockLevel1000(100, 1) - 501) <= 1 && abs(blockLevel1000(100, 2) - 251) <= 1 && blockLevel1000(0, 5) == 1000);
}

// ---- pad map --------------------------------------------------------------------------------------------------------------------------
static void test_br_pad_map()
{
    using namespace seqbr;
    int cells[7] = {};
    for (uint8_t row = 0; row < 4; row++)
        for (uint8_t col = 0; col < 12; col++)
        {
            const PadInfo p = mapPad(col, row);
            cells[p.cell]++;
            if (col < 8)
                CHECK(p.cell == BC_FADER && p.index == row && p.step == col + 1);
        }
    CHECK(cells[BC_FADER] == 32 && cells[BC_PAGE] == 2 && cells[BC_MODE] == 3 && cells[BC_LATCH] == 1 && cells[BC_OFF] == 1 && cells[BC_RESET] == 3);
    // columns 9-12 as documented
    CHECK(mapPad(8, 0).cell == BC_PAGE && mapPad(8, 0).index == BG_REPEAT && mapPad(8, 1).index == BG_AUTO && mapPad(8, 2).cell == BC_NONE && mapPad(8, 3).cell == BC_NONE);
    CHECK(mapPad(9, 0).cell == BC_MODE && mapPad(9, 0).index == beatrepeat::MODE_MIX);
    CHECK(mapPad(9, 1).index == beatrepeat::MODE_INSERT && mapPad(9, 2).index == beatrepeat::MODE_GATE && mapPad(9, 3).cell == BC_NONE);
    CHECK(mapPad(10, 0).cell == BC_LATCH && mapPad(10, 1).cell == BC_NONE);
    CHECK(mapPad(11, 0).cell == BC_OFF && mapPad(11, 1).cell == BC_RESET && mapPad(11, 3).index == 3);
    CHECK(mapPad(12, 0).cell == BC_NONE && mapPad(0, 4).cell == BC_NONE);
    // the rows per page
    CHECK(paramOf(BG_REPEAT, 0) == BP_GRIDPAD && paramOf(BG_REPEAT, 1) == BP_GATE && paramOf(BG_REPEAT, 2) == BP_DECAY && paramOf(BG_REPEAT, 3) == BP_PITCH);
    CHECK(paramOf(BG_AUTO, 0) == BP_INTERVAL && paramOf(BG_AUTO, 1) == BP_CHANCE && paramOf(BG_AUTO, 2) == BP_VARIATION && paramOf(BG_AUTO, 3) == BP_AUTOGRID);
    CHECK(kindOf(BP_GRIDPAD) == BK_MOMENTARY && kindOf(BP_INTERVAL) == BK_SELECT && kindOf(BP_AUTOGRID) == BK_SELECT && kindOf(BP_DECAY) == BK_BAR);
}

// ---- controller ---------------------------------------------------------------------------------------------------------------------
static void test_br_controller()
{
    using namespace seqbr;
    Ui ui;
    Result r[2];
    // a grid pad: hold = repeat at that grid; the grids in the order of the pads
    for (uint8_t c = 0; c < 8; c++)
    {
        CHECK(press(ui, c, 0, true, r) == 1 && r[0].act == BA_DOWN && r[0].arg == c);
        CHECK(beatrepeat::gridTicks(r[0].arg) == beatrepeat::gridTicks(c));
        CHECK(press(ui, c, 0, false, r) == 1 && r[0].act == BA_UP);
    }
    CHECK(strcmp(beatrepeat::gridName(0), "1/4") == 0 && strcmp(beatrepeat::gridName(3), "1/32") == 0 && strcmp(beatrepeat::gridName(4), "1/4T") == 0 && strcmp(beatrepeat::gridName(7), "1/32T") == 0);
    CHECK(ui.nHeld == 0 && ui.heldMask() == 0);
    // jam: another pad while one is held takes over; releasing it falls back to the pad still held; the last release ends it
    CHECK(press(ui, 1, 0, true, r) == 1 && r[0].act == BA_DOWN && r[0].arg == 1);
    CHECK(press(ui, 3, 0, true, r) == 1 && r[0].act == BA_DOWN && r[0].arg == 3);
    CHECK(ui.heldMask() == ((1u << 1) | (1u << 3)));
    CHECK(press(ui, 3, 0, false, r) == 1 && r[0].act == BA_DOWN && r[0].arg == 1);
    CHECK(press(ui, 1, 0, false, r) == 1 && r[0].act == BA_UP);
    CHECK(press(ui, 1, 0, true, r) == 1 && press(ui, 3, 0, true, r) == 1);
    CHECK(press(ui, 1, 0, false, r) == 0);                            // the older pad let go while the newer is held: nothing changes
    CHECK(press(ui, 3, 0, false, r) == 1 && r[0].act == BA_UP);
    CHECK(press(ui, 2, 0, false, r) == 0);                            // a release without a press
    // values: a fader pad sets the step of its row; the bar row keeps the step
    CHECK(press(ui, 4, 1, true, r) == 1 && r[0].act == BA_SETTINGS && ui.prm.step[BP_GATE] == 5);
    CHECK(press(ui, 4, 1, false, r) == 0);
    CHECK(press(ui, 7, 2, true, r) == 1 && ui.prm.step[BP_DECAY] == 8 && ui.prm.settings().decay == 100);
    CHECK(press(ui, 2, 3, true, r) == 1 && ui.prm.step[BP_PITCH] == 3 && ui.prm.settings().pitchDecay == 29);
    // RESET of a row
    CHECK(press(ui, 11, 2, true, r) == 1 && r[0].act == BA_SETTINGS && ui.prm.step[BP_DECAY] == 1 && ui.prm.settings().decay == 0);
    CHECK(press(ui, 11, 1, true, r) == 1 && ui.prm.step[BP_GATE] == 8);                   // GATE back to 16/16
    CHECK(press(ui, 11, 3, true, r) == 1 && ui.prm.step[BP_PITCH] == 1);
    CHECK(press(ui, 11, 2, false, r) == 0);
    // pages
    CHECK(press(ui, 8, 1, true, r) == 0 && ui.page == BG_AUTO);
    CHECK(press(ui, 0, 0, true, r) == 1 && r[0].act == BA_SETTINGS && ui.prm.step[BP_INTERVAL] == 1 && ui.prm.settings().interval == 0);   // INTERVAL 1/32
    CHECK(press(ui, 7, 0, true, r) == 1 && ui.prm.settings().interval == 7);                                                              // 4 bars
    CHECK(press(ui, 5, 1, true, r) == 1 && ui.prm.settings().chance == 71);                                                               // CHANCE
    CHECK(press(ui, 7, 2, true, r) == 1 && ui.prm.settings().variation == 10);
    CHECK(press(ui, 6, 3, true, r) == 1 && ui.prm.settings().grid == 6);                                                                  // AUTO GRID 1/16T
    CHECK(press(ui, 11, 0, true, r) == 1 && r[0].act == BA_OFF);                                                                           // OFF on any page
    CHECK(press(ui, 11, 1, true, r) == 1 && ui.prm.step[BP_CHANCE] == 1 && ui.prm.settings().chance == 0);
    CHECK(press(ui, 8, 0, true, r) == 0 && ui.page == BG_REPEAT);
    // row 1 of the AUTO page is a select, not a momentary: a press does not repeat
    ui.page = BG_AUTO;
    CHECK(press(ui, 2, 0, true, r) == 1 && r[0].act == BA_SETTINGS);
    ui.page = BG_REPEAT;
    // modes: MIX is the default; REPLACE is INSERT; GATE
    CHECK(ui.prm.mode == beatrepeat::MODE_MIX);
    CHECK(press(ui, 9, 1, true, r) == 1 && r[0].act == BA_SETTINGS && ui.prm.mode == beatrepeat::MODE_INSERT && ui.prm.settings().mode == beatrepeat::MODE_INSERT);
    CHECK(press(ui, 9, 2, true, r) == 1 && ui.prm.mode == beatrepeat::MODE_GATE);
    CHECK(press(ui, 9, 0, true, r) == 1 && ui.prm.mode == beatrepeat::MODE_MIX);
    // latch toggles
    CHECK(press(ui, 10, 0, true, r) == 1 && r[0].act == BA_LATCH && r[0].arg == 1 && ui.latch);
    CHECK(press(ui, 10, 0, false, r) == 0);
    CHECK(press(ui, 10, 0, true, r) == 1 && r[0].arg == 0 && !ui.latch);
    // dead pads
    CHECK(press(ui, 9, 3, true, r) == 0 && press(ui, 8, 3, true, r) == 0 && press(ui, 10, 2, true, r) == 0 && press(ui, 11, 3, false, r) == 0);
    // releaseAll / panic
    press(ui, 4, 0, true, r);
    CHECK(ui.releaseAll() && ui.nHeld == 0 && !ui.releaseAll());
    ui.latch = true;
    press(ui, 4, 0, true, r);
    ui.panic();
    CHECK(!ui.latch && ui.nHeld == 0);
}

// ---- LEDs and the legend --------------------------------------------------------------------------------------------------------------
static void test_br_leds_and_legend()
{
    using namespace seqbr;
    Ui ui;
    uint8_t out[48];
    fillLeds(ledsOf(ui), false, out);
    // REPEAT page: the grid pads are dim, the bars show their steps, page / mode / latch / off
    for (int c = 0; c < 8; c++)
        CHECK(out[c] == LED_DIM);                                      // idle: no grid pad lit
    CHECK(out[12 + 7] == LED_BRIGHT && out[12 + 6] == LED_MID && out[12] == LED_MID);           // GATE 8: the full bar
    CHECK(out[24] == LED_BRIGHT && out[25] == LED_DIM);                                         // DECAY step 1
    CHECK(out[8] == LED_BRIGHT && out[12 + 8] == LED_DIM && out[24 + 8] == LED_OFF && out[36 + 8] == LED_OFF);     // page pads
    CHECK(out[9] == LED_BRIGHT && out[12 + 9] == LED_DIM && out[24 + 9] == LED_DIM && out[36 + 9] == LED_OFF);     // mode pads: MIX
    CHECK(out[10] == LED_DIM && out[12 + 10] == LED_OFF);                                                          // latch off
    CHECK(out[11] == LED_DIM);                                                                                      // OFF, nothing to end
    CHECK(out[12 + 11] == LED_DIM && out[24 + 11] == LED_DIM && out[36 + 11] == LED_DIM);                          // RESET dim while every row is at its default
    press(ui, 3, 2, true);
    fillLeds(ledsOf(ui), false, out);
    CHECK(out[24 + 11] == LED_MID && out[12 + 11] == LED_DIM);                                                     // lit where the row is not at its default
    press(ui, 11, 2, true);
    // a held pad and a running repeat
    press(ui, 2, 0, true);
    fillLeds(ledsOf(ui, beatrepeat::S_REPEAT, 2), false, out);
    CHECK(out[2] == LED_BRIGHT && out[1] == LED_DIM && out[11] == LED_MID);
    fillLeds(ledsOf(ui, beatrepeat::S_REPEAT, 2), true, out);
    CHECK(out[11] == LED_BRIGHT);                                                                                  // OFF blinks while a repeat runs
    fillLeds(ledsOf(ui, beatrepeat::S_PENDING, 5), false, out);
    CHECK(out[5] == LED_MID);                                                                                      // waiting for the boundary: blinks
    press(ui, 2, 0, false);
    // latch + mode REPLACE
    press(ui, 10, 0, true);
    press(ui, 9, 1, true);
    fillLeds(ledsOf(ui), false, out);
    CHECK(out[10] == LED_BRIGHT && out[9] == LED_DIM && out[12 + 9] == LED_BRIGHT);
    // AUTO page: selects light one pad
    press(ui, 8, 1, true);
    press(ui, 3, 0, true);
    fillLeds(ledsOf(ui), false, out);
    CHECK(out[3] == LED_BRIGHT && out[2] == LED_DIM && out[4] == LED_DIM);
    CHECK(out[8] == LED_DIM && out[12 + 8] == LED_BRIGHT);
    CHECK(out[12 + 11] == LED_DIM);                                                                                // CHANCE at its default: RESET dim
    // legend words: every pad of both pages has a word, none is longer than the cell
    for (int page = 0; page < BG_COUNT; page++)
    {
        ui.page = (uint8_t)page;
        const LedState ls = ledsOf(ui);
        for (uint8_t row = 0; row < 4; row++)
            for (uint8_t col = 0; col < 12; col++)
            {
                const LegendCell c = legendCell(col, row, ls);
                const PadInfo p = mapPad(col, row);
                CHECK(strlen(c.word) <= 6);
                CHECK((p.cell == BC_NONE) == (c.word[0] == 0));
            }
    }
    ui.page = BG_REPEAT;
    LedState ls = ledsOf(ui);
    CHECK(strcmp(legendCell(0, 0, ls).word, "1/4") == 0 && strcmp(legendCell(3, 0, ls).word, "1/32") == 0 && strcmp(legendCell(4, 0, ls).word, "1/4T") == 0 && strcmp(legendCell(7, 0, ls).word, "1/32T") == 0);
    CHECK(strcmp(legendCell(8, 0, ls).word, "REPT") == 0 && strcmp(legendCell(8, 1, ls).word, "AUTO") == 0 && strcmp(legendCell(9, 1, ls).word, "REPL") == 0);
    CHECK(strcmp(legendCell(10, 0, ls).word, "LATCH") == 0 && strcmp(legendCell(11, 0, ls).word, "OFF") == 0 && strcmp(legendCell(11, 2, ls).word, "RESET") == 0);
    CHECK(strcmp(legendCell(0, 1, ls).word, "1/16") == 0 && strcmp(legendCell(7, 1, ls).word, "16/16") == 0);
    ui.page = BG_AUTO;
    ls = ledsOf(ui);
    CHECK(strcmp(legendCell(0, 0, ls).word, "1/32") == 0 && strcmp(legendCell(7, 0, ls).word, "4 bars") == 0);
}

// ---- the layer: the BeatRpt screen button, the pads, leaving, Stop ------------------------------------------------------------------------
static void test_br_layer()
{
    using namespace seqsession;
    g_brActs.clear();
    g_brUi = seqbr::Ui();
    g_brPanics = 0;
    g_brHaveStatus = false;
    CutSessionLayer layer;
    layer.enter(0);
    char lbl[12];
    CHECK(layer.buttonLabel(seqcut::kScreenButtonBeatRpt, lbl) && strcmp(lbl, "BeatRpt") == 0);   // screen button 4, free before
    CHECK(seqcut::kScreenButtonBeatRpt == 3 && !cutBrActive());
    // MLR pads in the hand before the switch are let go, then the pads are the beat repeat controls
    layer.pad(2, 0, true);
    CHECK(layer.button(seqcut::kScreenButtonBeatRpt, true));
    layer.button(seqcut::kScreenButtonBeatRpt, false);        // the release does nothing
    CHECK(cutBrActive());
    CHECK(layer.buttonLabel(seqcut::kScreenButtonBeatRpt, lbl) && strcmp(lbl, "[BeatRpt]") == 0);
    CHECK(g_brActs.empty());
    layer.pad(2, 0, false);                                   // the MLR release arrives late: ignored by the beat repeat layer
    CHECK(g_brActs.empty() || lastAct().act != BA_DOWN);
    g_brActs.clear();
    layer.pad(2, 0, true);                                    // grid 1/16, hold = repeat
    CHECK(g_brActs.size() == 1 && g_brActs[0].act == seqbr::BA_DOWN && g_brActs[0].arg == 2);
    layer.pad(2, 0, false);
    CHECK(g_brActs.size() == 2 && g_brActs[1].act == seqbr::BA_UP);
    // LEDs follow the layer
    uint8_t leds[48];
    layer.leds(leds);
    CHECK(leds[12 + 7] == seqcut::LED_BRIGHT && leds[9] == seqcut::LED_BRIGHT);           // GATE full bar, mode MIX
    g_brStatus = beatrepeat::Status();
    g_brStatus.state = beatrepeat::S_REPEAT;
    g_brStatus.grid = 6;
    g_brHaveStatus = true;
    layer.leds(leds);
    CHECK(leds[6] == seqcut::LED_BRIGHT);                                                 // the repeat in progress lights its grid pad
    g_brHaveStatus = false;
    // the MLR layer keeps its own state: back to MLR gives the MLR pad map again
    g_brActs.clear();
    layer.pad(5, 0, true);                                    // a grid pad in the hand ...
    layer.button(seqcut::kScreenButtonBeatRpt, true);         // ... and the switch back: the pad is let go
    CHECK(!cutBrActive());
    CHECK(g_brActs.size() == 2 && g_brActs[0].act == seqbr::BA_DOWN && g_brActs[1].act == seqbr::BA_UP);
    layer.leds(leds);
    CHECK(leds[0] != seqcut::LED_OFF || leds[8] != seqcut::LED_OFF);                      // MLR LEDs (STOP pad ...)
    // leaving the layer lets the grid pads go (a latched repeat goes on); Stop is the panic
    layer.button(seqcut::kScreenButtonBeatRpt, true);
    g_brActs.clear();
    layer.pad(1, 0, true);
    layer.leave();
    CHECK(g_brActs.size() == 2 && g_brActs[1].act == seqbr::BA_UP);
    CHECK(cutBrActive());                                      // the mode itself is kept for the next visit
    cutPanic();
    CHECK(g_brPanics == 1 && !g_brUi.latch);
    CHECK(layer.button(3, false) && layer.button(4, true));    // Q still cycles
    layer.button(seqcut::kScreenButtonBeatRpt, true);
    CHECK(!cutBrActive());
    // the other screen buttons stay what they were
    CHECK(layer.buttonLabel(seqcut::kButtonHelp, lbl) && strcmp(lbl, "Help") == 0);
    CHECK(layer.buttonLabel(seqcut::kScreenButtonQuant, lbl) && strncmp(lbl, "Q ", 2) == 0);
    CHECK(layer.buttonLabel(seqcut::kScreenButtonSyncAll, lbl));
    // the Help legend works in the beat repeat layer too
    layer.button(seqcut::kScreenButtonBeatRpt, true);
    layer.button(seqcut::kButtonHelp, true);
    CHECK(cutLegendShown());
    layer.button(seqcut::kButtonHelp, true);
    layer.button(seqcut::kScreenButtonBeatRpt, true);
    layer.leave();
}

// ---- the sidecar ------------------------------------------------------------------------------------------------------------------------------
static void test_br_sidecar()
{
    using namespace seqbr;
    BrParams d;
    CHECK(d.isDefault());
    uint8_t buf[BrParams::kFileMax + 8];
    BrParams p;
    p.step[BP_CHANCE] = 6;
    p.step[BP_DECAY] = 3;
    p.step[BP_PITCH] = 8;
    p.step[BP_GATE] = 2;
    p.step[BP_INTERVAL] = 4;
    p.step[BP_AUTOGRID] = 7;
    p.step[BP_VARIATION] = 5;
    p.mode = beatrepeat::MODE_INSERT;
    const size_t n = p.save(buf, sizeof(buf));
    CHECK(n == (size_t)BrParams::kFileMax && memcmp(buf, "SBRP", 4) == 0 && buf[4] == 1);     // oldest version that fits: 1
    BrParams q;
    bool newer = true;
    CHECK(q.load(buf, n, &newer) && !newer && q == p && q.hash() == p.hash());
    // too small a buffer
    CHECK(p.save(buf, 5) == 0);
    // a short payload (an older / shorter file): the rest stays default
    uint8_t pay[4] = { 7, 2, 8, 1 };
    uint8_t fr[32];
    const size_t fn = seqsidecar::frameWrite(fr, sizeof(fr), "SBRP", 1, pay, 4);
    BrParams s;
    CHECK(s.load(fr, fn) && s.step[BP_AUTOGRID] == 7 && s.step[BP_INTERVAL] == 2 && s.step[BP_CHANCE] == 8 && s.step[BP_GATE] == 1 && s.step[BP_DECAY] == 1 && s.mode == beatrepeat::MODE_MIX);
    // a longer payload (a later version appends bytes): the 8 known bytes are read
    uint8_t pay2[12] = { 1, 2, 3, 4, 5, 6, 7, 2, 99, 99, 99, 99 };
    const size_t fn2 = seqsidecar::frameWrite(fr, sizeof(fr), "SBRP", 1, pay2, 12);
    BrParams l;
    CHECK(l.load(fr, fn2) && l.step[BP_DECAY] == 7 && l.mode == beatrepeat::MODE_GATE);
    // values out of range fall back to their own default, one by one
    uint8_t pay3[8] = { 0, 9, 3, 200, 4, 5, 6, 7 };
    const size_t fn3 = seqsidecar::frameWrite(fr, sizeof(fr), "SBRP", 1, pay3, 8);
    BrParams o;
    CHECK(o.load(fr, fn3) && o.step[BP_AUTOGRID] == 3 && o.step[BP_INTERVAL] == 6 && o.step[BP_CHANCE] == 3 && o.step[BP_GATE] == 8 && o.step[BP_VARIATION] == 4 && o.mode == beatrepeat::MODE_MIX);
    // a newer file is flagged and never read; a corrupt one and another magic are refused; nothing changes on a failure
    uint8_t nf[32];
    memcpy(nf, buf, n);
    nf[4] = 2;
    uint16_t c = seqsidecar::crc16(nf, seqsidecar::kHeader + BrParams::kPayload);
    nf[seqsidecar::kHeader + BrParams::kPayload] = (uint8_t)(c & 255);
    nf[seqsidecar::kHeader + BrParams::kPayload + 1] = (uint8_t)(c >> 8);
    BrParams keep = p;
    newer = false;
    CHECK(!keep.load(nf, n, &newer) && newer && keep == p);
    memcpy(nf, buf, n);
    nf[9] ^= 0x10;
    newer = true;
    CHECK(!keep.load(nf, n, &newer) && !newer && keep == p);
    memcpy(nf, buf, n);
    nf[0] = 'X';
    CHECK(!keep.load(nf, n) && !keep.load(buf, 3) && keep == p);
    // a default set is not written (the glue removes / skips the file: isDefault)
    BrParams w;
    w.step[BP_GATE] = 8;
    CHECK(w.isDefault());
    w.mode = beatrepeat::MODE_INSERT;
    CHECK(!w.isDefault());
    // the hash differs for a different setting (the autosave only asks for a write on a real change)
    BrParams a, b;
    b.step[BP_DECAY] = 2;
    CHECK(a.hash() != b.hash());
}

// ---- the screen: budgets, bounds ------------------------------------------------------------------------------------------------------------
static void test_br_screen_budget_and_bounds()
{
    using namespace seqcut;
    using namespace seqbr;
    CHECK(brdraw::kBandTop >= kHeaderY1 + 4 && brdraw::kBandTop + 3 * brdraw::kBandStride + brdraw::kBandH < brdraw::kWaveTop);
    CHECK(brdraw::kBlockTop + brdraw::kBlockH <= 424 && brdraw::kViewX + brdraw::kViewW <= 800 && kBrWave * 8 == brdraw::kViewW);
    CHECK(brdraw::kCellX + 7 * brdraw::kCellStride + brdraw::kCellW < brdraw::kSideX && brdraw::kSideX + 80 + 74 < 794);
    int worst = 0;
    for (int scene = 0; scene < 5; scene++)
    {
        MlrModel mm;
        memset(&mm, 0, sizeof(mm));
        mm.brMode = 1;
        mm.br = brScene(scene == 4 ? 3 : scene);
        mm.legend = scene >= 3 ? 1 : 0;
        CountCtx w;
        drawMlr(w, mm);
        w.finish();
        MlrHeadModel hm;
        memset(&hm, 0, sizeof(hm));
        hm.brMode = 1;
        hm.br = makeBrHead(beatrepeat::S_REPEAT, 987, 7, kBrViewW);
        hm.bar = 8;
        hm.beat = 4;
        hm.legend = mm.legend;
        CountCtx hw;
        drawMlrHeads(hw, hm);
        hw.finish();
        printf("  BeatRpt screen scene %d: %d bytes static (%d rects, %d texts), FIFO %d, %d bytes playheads\n", scene, w.bytes(), w.rects, w.texts, w.fifo(), hw.bytes());
        CHECK(w.minY >= 30 && w.maxY <= 424 && w.minX >= 0 && w.maxX <= 800);
        CHECK(hw.minY >= 30 && hw.maxY <= 424 && hw.minX >= 0 && hw.maxX <= 800);
        CHECK(hw.fifo() <= 3840);
        CountCtx s0, s1;
        tracestage::BatchSplit<CountCtx> c0(s0, 0), c1(s1, 1);
        drawMlr(c0, mm);
        c0.finish();
        s0.finish();
        drawMlr(c1, mm);
        c1.finish();
        s1.finish();
        const int f0 = s0.fifo() + 4 + 4 + (s1.rects + s1.discs ? 8 : 0), f1 = s1.fifo() + 4 + 8 + 4;
        printf("  BeatRpt screen scene %d: stages %d + %d FIFO bytes (dropped %d)\n", scene, f0, f1, c0.dropped);
        CHECK(f0 <= 3840 && f1 <= 3840 && c0.dropped == 0 && c1.dropped == 0);   // every display stage <= 3840 FIFO bytes
        CHECK(s0.rects + s1.rects == w.rects && s0.texts + s1.texts == w.texts);  // nothing lost between the stages
        CHECK(c0.used[0] == c1.used[0] && c0.used[1] == c1.used[1]);
        if (w.bytes() > worst)
            worst = w.bytes();
    }
    CHECK(worst + 400 <= 8000);                                                    // the whole frame stays below the display list of 8172 bytes
    // no playhead outside the slice view when nothing runs
    MlrHeadModel gone;
    memset(&gone, 0, sizeof(gone));
    gone.brMode = 1;
    gone.br = makeBrHead(beatrepeat::S_IDLE, 500, 0, kBrViewW);
    CountCtx gc;
    drawMlrHeads(gc, gone);
    gc.finish();
    CHECK(gone.br.x == -1 && gc.rects == 0);
    BrHead h = makeBrHead(beatrepeat::S_REPEAT, 500, 3, kBrViewW);
    CHECK(h.x == kBrViewW / 2);
    h = makeBrHead(beatrepeat::S_CAPTURE, 1000, 0, kBrViewW);
    CHECK(h.x == kBrViewW);
    h = makeBrHead(beatrepeat::S_PENDING, 500, 0, kBrViewW);
    CHECK(h.x == -1);
    // the waveform fold of a slice: a loud slice fills the columns, silence is flat
    {
        std::vector<int16_t> ring(2 * 5000, 0);
        for (int i = 0; i < 5000; i++)
        {
            ring[(size_t)(2 * i)] = (int16_t)(i < 2500 ? 20000 : 0);
            ring[(size_t)(2 * i + 1)] = 0;
        }
        int8_t lo[kBrWave], hi[kBrWave];
        foldWave(ring.data(), 5000, lo, hi);
        CHECK(hi[0] > 70 && hi[kBrWave - 1] == 0 && lo[kBrWave - 1] == 0);
        foldWave(ring.data(), 0, lo, hi);
        CHECK(hi[5] == 0 && lo[5] == 0);
    }
    if (const char* dir = getenv("SEQ_LANE_PREVIEW")) // `make preview`: the BeatRpt scenes next to the MLR ones
    {
        char path[512];
        for (int scene = 0; scene < 5; scene++)
        {
            snprintf(path, sizeof(path), "%s/br%d.ops", dir, scene);
            FILE* fp = fopen(path, "w");
            if (!fp)
                continue;
            MlrModel mm;
            memset(&mm, 0, sizeof(mm));
            mm.brMode = 1;
            mm.br = brScene(scene == 4 ? 3 : scene);
            mm.legend = scene >= 3 ? 1 : 0;
            MlrHeadModel hm;
            memset(&hm, 0, sizeof(hm));
            hm.brMode = 1;
            hm.br = makeBrHead(scene == 0 ? beatrepeat::S_IDLE : beatrepeat::S_REPEAT, 420, 3, kBrViewW);
            hm.bar = 3;
            hm.beat = 2;
            hm.legend = mm.legend;
            CountCtx d;
            d.dump = fp;
            drawMlr(d, mm);
            drawMlrHeads(d, hm);
            d.finish();
            fclose(fp);
        }
    }
}

static void test_br_all()
{
    test_br_values();
    test_br_pad_map();
    test_br_controller();
    test_br_leds_and_legend();
    test_br_layer();
    test_br_sidecar();
    test_br_screen_budget_and_bounds();
}
