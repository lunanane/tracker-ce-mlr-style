// Sequencer tracks mod -- PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Host preview of the clip launcher with scene colours (plan 017): compiles the real clipGrid.cpp against a stub of the display list
// macros (gridstub/) and dumps its draw ops (the grid, then the marks control on top) for grid_preview.py.
//   usage: preview_grid <dir>      writes <dir>/grid_plain.ops and <dir>/grid_colors.ops      (make grid-preview)
#include "../interface/seqSession/clipGrid.cpp"
#include <string.h>

FILE* g_ops = nullptr;
int g_prim = 0;
cDisplayStub display;

static void scene(const char* dir, const char* name, bool colors)
{
    using namespace seqsession::clipgrid;
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ops", dir, name);
    g_ops = fopen(path, "w");
    if (!g_ops)
        return;
    display.t.songPlayerColors[0] = 0xFFFFFF;
    display.t.songPlayerColors[3] = 0x101010;
    display.t.songPlayerColors[4] = 0x6A6A6A;
    display.t.songPlayerColors[5] = 0xFF3030;

    static View v;
    static Marks m;
    memset(&v, 0, sizeof v);
    memset(&m, 0xFF, sizeof m);
    memset(m.prog, 0, sizeof m.prog);
    memset(m.qKind, 0, sizeof m.qKind);
    m.blink = 1;
    m.countdown = 0xFF;
    v.top = 1;
    v.winFirst = 3;
    v.winTrack = 0;
    v.focusPattern = 4;
    v.focusTrack = 2;
    v.quant = 1;
    // content: a different shape per row
    for (uint8_t r = 0; r < kVisRows; r++)
    {
        for (uint8_t t = 0; t < kTracks; t++)
            if (((r * 5 + t * 3) % 7) < 3 + (r % 3) && r < 10)
                v.cell[r][t] = CELL_NOTES;
        v.scene[r] = (r < 10) ? CELL_NOTES : CELL_EMPTY;
        if (colors)
            v.sceneColor[r] = (uint8_t)(r % 12 < 9 ? r % 12 : 0); // none, then the 8 colours, then none again
    }
    // rows: 0..8 colours 0,1..8 -> row 0 = none; keep the demo readable: the same colour on two rows
    v.sceneFlags[2] = F_ONEX;
    v.flags[3][1] = F_ONEX;
    v.cell[4][5] = CELL_FOLLOW;
    // playing: row 3 on tracks 0, 1, 2 (scene 4 plays); queued: track 4 -> row 6; queued scene row 7
    for (uint8_t t = 0; t < 3; t++)
        v.cell[3][t] = CELL_PLAYING;
    v.scene[3] = CELL_PLAYING;
    v.cell[6][4] = CELL_QUEUED;
    v.scene[6] = CELL_QUEUED;
    m.playRow[0] = m.playRow[1] = m.playRow[2] = 3;
    m.prog[0] = 20;
    m.prog[1] = 9;
    m.prog[2] = 14;
    m.qRow[4] = 6;
    m.qKind[4] = MK_BLINK;
    m.sceneRow = 6;
    m.songRow = 3;
    m.countdown = 8;

    GridPlan plan = planGrid(v);
    fprintf(g_ops, "# grid cost %u bytes of %d, marks cost %u of %d\n", (unsigned)plan.cost, (int)kGridBudget, (unsigned)marksCost(m), (int)kMarksBudget);
    strControlProperties pg = { 0, 29, 800, 394, 0, &v };
    cSeqClipGrid grid(&pg);
    strControlProperties pm = { 0, 29, 800, 394, 0, &m };
    cSeqClipMarks marks(&pm);
    grid.update();
    marks.update();
    fclose(g_ops);
    printf("%s: grid %u bytes, marks %u bytes\n", name, (unsigned)plan.cost, (unsigned)marksCost(m));
}

int main(int argc, char** argv)
{
    const char* dir = argc > 1 ? argv[1] : ".";
    scene(dir, "grid_plain", false);
    scene(dir, "grid_colors", true);
    return 0;
}
