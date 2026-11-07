// Sequencer tracks mod -- PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Host stub of the theme: the colours the Master fader screen uses (approximations of the default theme, for the preview only).
#pragma once
#include <stdint.h>
struct Theme
{
    uint32_t white = 0xFFFFFF, grey_22 = 0x222222, green = 0x00E060, orange = 0xFF9020, cyan = 0x20D0FF, red = 0xFF3030;
    uint32_t activeLabelsColors[5] = { 0xFFFFFF, 0x3A3A3A, 0x4A4A4A, 0x2A2A2A, 0x1A1A1A };
    uint32_t defaultTextColors[5] = { 0xFFFFFF, 0xCCCCCC, 0x999999, 0x666666, 0x333333 };
};
