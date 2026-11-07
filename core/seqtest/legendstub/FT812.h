// Sequencer tracks mod -- PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Host stub of the FT812 display list macros for the Play legend / settings page preview (preview_legend.cpp): every call is dumped as one
// draw op line (C colour, W line width, R rect, S line strip, T text) that legend_preview.py rasterises.
#pragma once
#include <stdint.h>
#include <stdio.h>
extern FILE* g_ops;
extern int g_prim;
enum { LINE_STRIP = 1, RECTS = 2, OPT_CENTERX = 512, OPT_CENTERY = 1024, OPT_RIGHTX = 2048 };
enum { SRC_ALPHA = 1, ZERO = 2, ONE_MINUS_SRC_ALPHA = 3 };
static int16_t g_vx[64];
static int g_vn = 0;
static inline void stubColor(uint32_t c) { fprintf(g_ops, "C %06X\n", (unsigned)(c & 0xFFFFFF)); }
static inline void stubBegin(int p) { g_prim = p; g_vn = 0; }
static inline void stubEnd()
{
    if (g_prim == LINE_STRIP)
    {
        fprintf(g_ops, "S");
        for (int i = 0; i < g_vn; i++) fprintf(g_ops, " %d %d", g_vx[2 * i], g_vx[2 * i + 1]);
        fprintf(g_ops, "\n");
    }
    g_prim = 0;
}
static inline void stubVertex(int16_t x, int16_t y)
{
    if (g_prim == RECTS)
    {
        if (g_vn == 0) { g_vx[0] = x; g_vx[1] = y; g_vn = 1; }
        else { fprintf(g_ops, "R %d %d %d %d\n", g_vx[0], g_vx[1], x, y); g_vn = 0; }
    }
    else if (g_vn < 32) { g_vx[2 * g_vn] = x; g_vx[2 * g_vn + 1] = y; g_vn++; }
}
#define API_COLOR(c) stubColor(c)
#define API_LINE_WIDTH(w) fprintf(g_ops, "W %d\n", (int)(w))
#define API_BEGIN(p) stubBegin(p)
#define API_END() stubEnd()
#define API_BLEND_FUNC(a, b) ((void)0)
#define API_VERTEX2F(x, y) stubVertex((int16_t)(x), (int16_t)(y))
#define API_CMD_TEXT(x, y, f, o, s) fprintf(g_ops, "T %d %d %d %d %s\n", (int)(x), (int)(y), (int)(f), (int)(o), (s))
