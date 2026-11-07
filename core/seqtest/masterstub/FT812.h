// Sequencer tracks mod -- PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Host stub of the FT812 display list calls for the Master fader screen (plan 024): counts the bytes every call puts into the co-processor
// command FIFO (the sizes of core/libs/FT812/FT812.cpp) and, when g_ops is open, dumps the draw ops (C colour, A alpha, W line width, R rect,
// T text) that master_preview.py rasterises.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
enum { LINE_STRIP = 1, RECTS = 2, OPT_CENTERX = 512, OPT_CENTERY = 1024, OPT_RIGHTX = 2048 };
extern FILE* g_ops;
extern int g_bytes;
static int g_prim = 0, g_nv = 0;
static int16_t g_vx[4];
static inline void API_COLOR(uint32_t c) { g_bytes += 4; if (g_ops) fprintf(g_ops, "C %06X\n", (unsigned)(c & 0xFFFFFF)); }
static inline void API_COLOR_A(uint8_t a) { g_bytes += 4; if (g_ops) fprintf(g_ops, "A %d\n", (int)a); }
static inline void API_LINE_WIDTH(int w) { g_bytes += 4; (void)w; }
static inline void API_SAVE_CONTEXT() { g_bytes += 4; }
static inline void API_RESTORE_CONTEXT() { g_bytes += 4; }
static inline void API_BEGIN(int p) { g_bytes += 4; g_prim = p; g_nv = 0; }
static inline void API_END() { g_bytes += 4; g_prim = 0; }
static inline void API_VERTEX2F(int x, int y)
{
    g_bytes += 4;
    if (g_prim != RECTS)
        return;
    if (g_nv == 0) { g_vx[0] = (int16_t)x; g_vx[1] = (int16_t)y; g_nv = 1; }
    else { if (g_ops) fprintf(g_ops, "R %d %d %d %d\n", g_vx[0], g_vx[1], x, y); g_nv = 0; }
}
static inline void API_CMD_TEXT(int x, int y, int f, int o, const char* s, uint32_t len = 0)
{
    const int n = len ? (int)(strlen(s) < len ? strlen(s) : len) : (int)strlen(s);
    g_bytes += 12 + ((n + 1 + 3) & ~3);
    if (g_ops) fprintf(g_ops, "T %d %d %d %d %.*s\n", x, y, f, o, n, s);
}
