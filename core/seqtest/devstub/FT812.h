// Sequencer tracks mod -- PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Host stub of the FT812 API for test_pitchdevice.cpp: every call adds its FIFO bytes (the sizes of core/libs/FT812/FT812.cpp) to the
// display list that display.h is building, and the line strips are recorded with the colour set at that point, so the test can say
// what the composed frame (display.cpp step3) really shows.
#pragma once
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>

enum { RECTS = 9, LINES = 3, LINE_STRIP = 4, SRC_ALPHA = 2, ZERO = 0, ONE_MINUS_SRC_ALPHA = 4 };
enum { OPT_CENTERX = 512, OPT_CENTERY = 1024, OPT_RIGHTX = 2048 };

struct DlOp
{
    char kind;      // 'S' strip, 'L' lines, 'R' rect, 'T' text, 'X' anything else
    uint32_t col;
    int n;          // points / segments
    int minY, maxY;
    std::string text;
};
struct DlBuild
{
    int bytes = 0;
    uint32_t col = 0;
    int prim = -1;
    DlOp cur;
    std::vector<DlOp> ops;
};
extern DlBuild g_dl;

static inline void dlBegin(int p)
{
    g_dl.bytes += 4;
    g_dl.prim = p;
    g_dl.cur = DlOp();
    g_dl.cur.kind = p == LINE_STRIP ? 'S' : (p == LINES ? 'L' : 'R');
    g_dl.cur.col = g_dl.col;
    g_dl.cur.n = 0;
    g_dl.cur.minY = 1 << 30;
    g_dl.cur.maxY = -(1 << 30);
}
static inline void dlVertex(int, int y)
{
    g_dl.bytes += 4;
    g_dl.cur.n++;
    if (y < g_dl.cur.minY) g_dl.cur.minY = y;
    if (y > g_dl.cur.maxY) g_dl.cur.maxY = y;
}
static inline void dlEnd()
{
    g_dl.bytes += 4;
    if (g_dl.prim >= 0) g_dl.ops.push_back(g_dl.cur);
    g_dl.prim = -1;
}
static inline void dlText(const char* s)
{
    g_dl.bytes += 12 + (int)((strlen(s) + 1 + 3) / 4 * 4);
    DlOp o;
    o.kind = 'T';
    o.col = g_dl.col;
    o.n = 0;
    o.minY = o.maxY = 0;
    o.text = s;
    g_dl.ops.push_back(o);
}
#define API_COLOR(c) (g_dl.bytes += 4, g_dl.col = (uint32_t)(c) & 0xFFFFFF)
#define API_LINE_WIDTH(w) (g_dl.bytes += 4, (void)(w))
#define API_BLEND_FUNC(a, b) (g_dl.bytes += 4, (void)(a), (void)(b))
#define API_VERTEX_FORMAT(f) (g_dl.bytes += 4, (void)(f)) // v0.4.3: the amp view's sub-pixel lines (traceControl.cpp)
#define API_BEGIN(p) dlBegin(p)
#define API_END() dlEnd()
#define API_VERTEX2F(x, y) dlVertex((int)(x), (int)(y))
#define API_CMD_TEXT(x, y, f, o, s) ((void)(x), (void)(y), (void)(f), (void)(o), dlText(s))
