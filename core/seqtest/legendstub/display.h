// Sequencer tracks mod -- PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Host stub of the display control base for the legend preview (preview_legend.cpp).
#pragma once
#include <stdint.h>
struct strControlProperties { int x, y, w, h; uint32_t style; void* data; const char* text; };
struct strFont { int handle; };
static strFont fonts[8] = { { 0 }, { 1 }, { 2 }, { 3 }, { 4 }, { 5 }, { 6 }, { 7 } };
enum { controlIdSeqPlayLegend = 1 };
// the default theme (Themes.cpp themeDefault)
struct Theme
{
    uint32_t backgroundColor = 0x000000, white = 0xFFFFFF, red = 0xcd1818, orange = 0xD67022, yellow = 0xfef749, green = 0x30cb3f, cyan = 0x3FFFFF, grey_22 = 0x222222;
    uint32_t highlightedButtonLabelsColors[4] = { 0x777777, 0x222222, 0xcd1818, 0x777777 };
    uint32_t inactiveButtonLabelsColors[4] = { 0x333333, 0x323132, 0xcd1818, 0x333333 };
};
struct cDisplayStub { Theme t; Theme& getTheme() { return t; } };
extern cDisplayStub display;
class cDisplayControl
{
  public:
    virtual void update() = 0;
    virtual ~cDisplayControl() {}
    cDisplayControl(int, uint8_t) {}
    int posX = 0, posY = 0, width = 0, height = 0, value = 0;
    uint32_t style = 0;
    const char* text = nullptr;
    const uint32_t* colors = nullptr;
    int colorsCount = 0;
    bool autoRefresh = false;
};
