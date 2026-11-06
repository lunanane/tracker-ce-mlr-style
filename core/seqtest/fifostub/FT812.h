// Sequencer tracks mod -- PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Host stub of the FT812 API for test_gridstages.cpp (v0.3.9): counts the bytes every call puts into the co-processor command FIFO
// (the sizes of core/libs/FT812/FT812.cpp) and records what the renderer would draw: rects and line strips with the colour and the
// line width that are set at that point, texts / numbers. The state (colour, width, open primitive) lives across calls like on the
// FT81x, where the DLs of two stages are concatenated.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
enum { LINE_STRIP = 1, RECTS = 2, OPT_CENTERX = 512, OPT_CENTERY = 1024 };
struct FifoStub
{
    int bytes = 0;
    uint32_t col = 0;
    int width = 0, prim = 0, nv = 0;
    int16_t vx[16];
    std::vector<std::string> ops;
};
extern FifoStub g_fs;
static inline void fsVertex(int16_t x, int16_t y)
{
    g_fs.bytes += 4;
    if (g_fs.prim == RECTS)
    {
        if (g_fs.nv == 0) { g_fs.vx[0] = x; g_fs.vx[1] = y; g_fs.nv = 1; }
        else
        {
            g_fs.ops.push_back("R " + std::to_string(g_fs.col) + " " + std::to_string(g_fs.width) + " " + std::to_string(g_fs.vx[0]) + " " +
                               std::to_string(g_fs.vx[1]) + " " + std::to_string(x) + " " + std::to_string(y));
            g_fs.nv = 0;
        }
    }
    else if (g_fs.prim == LINE_STRIP && g_fs.nv < 8) { g_fs.vx[2 * g_fs.nv] = x; g_fs.vx[2 * g_fs.nv + 1] = y; g_fs.nv++; }
    else g_fs.ops.push_back("V?"); // a vertex outside any primitive: a bug of the split
}
static inline void fsBegin(int p) { g_fs.bytes += 4; g_fs.prim = p; g_fs.nv = 0; }
static inline void fsEnd()
{
    g_fs.bytes += 4;
    if (g_fs.prim == LINE_STRIP)
    {
        std::string s = "S " + std::to_string(g_fs.col) + " " + std::to_string(g_fs.width);
        for (int i = 0; i < g_fs.nv * 2; i++) s += " " + std::to_string(g_fs.vx[i]);
        g_fs.ops.push_back(s);
    }
    g_fs.prim = 0;
}
static inline void fsText(int x, int y, int f, int o, const char* s)
{
    g_fs.bytes += 12 + (int)((strlen(s) + 1 + 3) / 4 * 4);
    g_fs.ops.push_back("T " + std::to_string(g_fs.col) + " " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(f) + " " +
                       std::to_string(o) + " " + s);
}
static inline void fsNumber(int x, int y, int f, int o, int n)
{
    g_fs.bytes += 16;
    g_fs.ops.push_back("N " + std::to_string(g_fs.col) + " " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(f) + " " +
                       std::to_string(o) + " " + std::to_string(n));
}
#define API_COLOR(c) (g_fs.bytes += 4, g_fs.col = (uint32_t)(c) & 0xFFFFFF)
#define API_LINE_WIDTH(w) (g_fs.bytes += 4, g_fs.width = (int)(w))
#define API_BEGIN(p) fsBegin(p)
#define API_END() fsEnd()
#define API_VERTEX2F(x, y) fsVertex((int16_t)(x), (int16_t)(y))
#define API_CMD_TEXT(x, y, f, o, s) fsText((int)(x), (int)(y), (int)(f), (int)(o), (s))
#define API_CMD_NUMBER(x, y, f, o, n) fsNumber((int)(x), (int)(y), (int)(f), (int)(o), (int)(n))
