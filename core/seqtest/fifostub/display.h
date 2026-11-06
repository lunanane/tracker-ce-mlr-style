// Sequencer tracks mod -- PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Host stub of the display control base for the launcher preview (preview_grid.cpp) and the FIFO test (test_gridstages.cpp).
#pragma once
#include <stdint.h>
struct strControlProperties { int x, y, w, h; uint32_t style; void* data; };
struct strFont { int handle; };
static strFont fonts[8] = { { 0 }, { 1 }, { 2 }, { 3 }, { 4 }, { 5 }, { 6 }, { 7 } };
static const uint8_t displayFontCount = 8;
enum { controlIdSeqClipGrid = 1, controlIdSeqClipMarks = 2 };
struct strTheme { uint32_t songPlayerColors[10]; };
struct cDisplayStub { strTheme t; strTheme& getTheme() { return t; } };
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
