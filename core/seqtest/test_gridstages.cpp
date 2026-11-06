// Sequencer tracks mod -- PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// v0.3.9: the clip launcher grid (clipGrid.cpp, the real file, compiled against fifostub/) in two display stages. Every stage is one SPI
// burst into the 4 KB FT81x command FIFO (seqTraceStages.h): each stage must stay under it, the two stages together must draw exactly
// what the single stage drawing draws (state carried over, a cut batch reopened), and both stages draw the View of stage 0 even when
// the session changes it in between. Also the clip marks control (single stage) against the FIFO.
#include "../interface/seqSession/clipGrid.cpp"
#include <stdlib.h>

FifoStub g_fs;
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

using namespace seqsession::clipgrid;

struct Run
{
    int single = 0, s0 = 0, s1 = 0;
    bool same = false, clean = true;
};

// one refresh as display.cpp runs it: stage 0, the session changes its View, stage 1; then the single stage reference of the snapshot
static Run refresh(cSeqClipGrid& grid, View& v)
{
    Run r;
    g_fs = FifoStub();
    grid.update();
    r.s0 = g_fs.bytes + 4; // + CMD_DLSTART
    View changed = v;
    for (uint8_t t = 0; t < kTracks; t++) v.cell[0][t] = CELL_PLAYING; // the session moves on before stage 1
    v.focusPattern = 0;
    const int before = g_fs.bytes;
    grid.update2();
    r.s1 = g_fs.bytes - before + 4;
    std::vector<std::string> staged = g_fs.ops;
    v = changed;
    g_fs = FifoStub();
    grid.drawStage(-1); // the whole drawing of the same snapshot in one
    r.single = g_fs.bytes + 4;
    r.same = staged == g_fs.ops;
    for (auto& s : staged) r.clean = r.clean && s != "V?";
    return r;
}

static void fillView(View& w, unsigned seed, int kind)
{
    memset(&w, 0, sizeof(w));
    unsigned rnd = seed;
    auto next = [&]() { rnd = rnd * 1664525u + 1013904223u; return rnd >> 16; };
    w.top = (uint8_t)(1 + next() % 200);
    w.winFirst = (uint8_t)(w.top + next() % 12);
    w.winTrack = (uint8_t)(next() % 9);
    w.focusPattern = (uint8_t)(w.top + next() % 12);
    w.focusTrack = (uint8_t)(next() % 16);
    w.quant = (uint8_t)(next() % 4);
    w.msg = (uint8_t)(next() % 3);
    w.modeHeld = (uint8_t)(next() & 1);
    w.stopHeld = (uint8_t)(next() & 1);
    for (uint8_t r = 0; r < kVisRows; r++)
    {
        for (uint8_t t = 0; t < kTracks; t++)
        {
            if (kind == 0) // the test_session worst case: everything lit
            {
                w.cell[r][t] = CELL_FOLLOW;
                w.flags[r][t] = F_ONEX | F_STOPMARK | F_END;
            }
            else
            {
                w.cell[r][t] = (uint8_t)(next() % 5);
                w.flags[r][t] = (uint8_t)(next() % 2 ? (next() % 2 ? F_ONEX : F_STOPMARK) : 0);
            }
        }
        w.scene[r] = (uint8_t)(next() % 3);
        w.sceneFlags[r] = F_ONEX;
        w.sceneColor[r] = kind == 2 ? (uint8_t)(1 + r % (seqpal::kSceneColors - 1)) : (kind == 3 ? (uint8_t)(next() % seqpal::kSceneColors) : 0);
    }
}

int main()
{
    printf("launcher grid stages (v0.3.9)\n");
    display.t.songPlayerColors[0] = 0xFFFFFF;
    display.t.songPlayerColors[4] = 0x6A6A6A;
    display.t.songPlayerColors[5] = 0xFF3030;
    static View v;
    strControlProperties pg = { 0, 30, 800, 394, 1, &v };
    cSeqClipGrid grid(&pg);
    int maxSingle = 0, maxStage = 0, over = 0, runs = 0;
    bool same = true, clean = true;
    for (int kind = 0; kind < 4; kind++)
        for (unsigned seed = 1; seed <= (kind == 0 ? 4u : 400u); seed++)
        {
            fillView(v, seed * 7919u + (unsigned)kind, kind);
            const Run r = refresh(grid, v);
            runs++;
            maxSingle = r.single > maxSingle ? r.single : maxSingle;
            maxStage = r.s0 > maxStage ? r.s0 : maxStage;
            maxStage = r.s1 > maxStage ? r.s1 : maxStage;
            over += r.single > tracestage::kFifoBytes - 4;
            same = same && r.same;
            clean = clean && r.clean;
        }
    printf("  launcher grid, %d views (worst case, random, 12 coloured rows): single stage up to %d FIFO bytes (%d views over the 4 KB "
           "FIFO), now stages <= %d\n",
           runs, maxSingle, over, maxStage);
    CHECK(maxStage <= tracestage::kStageBytes); // every burst fits the FIFO with margin
    CHECK(same);                                // stage 0 + stage 1 == the single stage drawing of the snapshot
    CHECK(clean);                               // no vertex outside a primitive (the cut batch is reopened)
    CHECK(over > 0);                            // the case this fixes: before, a single stage could overrun the FIFO

    // the marks control (single stage): everything queued and playing, the countdown
    static Marks m;
    memset(&m, 0, sizeof m);
    for (uint8_t t = 0; t < kTracks; t++)
    {
        m.playRow[t] = (uint8_t)(t % kVisRows);
        m.prog[t] = kCellW - 2;
        m.qRow[t] = (uint8_t)((t + 3) % kVisRows);
        m.qKind[t] = MK_BLINK;
    }
    m.sceneRow = 2;
    m.songRow = 1;
    m.blink = 1;
    m.countdown = 254;
    strControlProperties pm = { 0, 30, 800, 394, 1, &m };
    cSeqClipMarks marks(&pm);
    g_fs = FifoStub();
    marks.update();
    printf("  clip marks, everything playing and queued: %d FIFO bytes (one stage)\n", g_fs.bytes + 4);
    CHECK(g_fs.bytes + 4 <= tracestage::kStageBytes);
    printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
