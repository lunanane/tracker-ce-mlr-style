// Sequencer tracks mod -- PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Host stub of the display control base for test_masterfaders.cpp (the real seqPerform/faderView.cpp is compiled against it).
#pragma once
#include <stdint.h>
#include <string.h>
#include "Themes.h"
struct strControlProperties { int x = 0, y = 0, w = 0, h = 0; uint32_t style = 0; const uint32_t* colors = nullptr; const char* text = nullptr; int value = 0; void* data = nullptr; };
struct strFont { int handle; };
static strFont fonts[8] = { { 0 }, { 1 }, { 2 }, { 3 }, { 4 }, { 5 }, { 6 }, { 7 } };
enum { controlIdSeqFaderView = 1 };
// character widths of the stock fonts, roughly: F14 / F14B 7 px, F17 / F17B 8 px, F12 6 px
static inline uint16_t getMaxTextLengthThatFitSpecificWidth(const strFont* f, const char* s, uint16_t w)
{
    const int cw = f->handle == 4 ? 6 : (f->handle == 1 || f->handle == 3 ? 8 : 7);
    const uint16_t n = (uint16_t)strlen(s);
    return (uint16_t)(n * cw <= w ? n : w / cw);
}
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
    bool autoRefresh = false;
};
