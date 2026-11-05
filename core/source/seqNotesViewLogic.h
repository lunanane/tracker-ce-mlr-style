// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Which notes sound (v0.3.3): the note band of Play mode (a one-octave mini keyboard + note names above the strip) and the voice row of the
// PolySynth overlay. Pure logic and drawing over a tiny context (the same one seqPlinkyUiLogic.h uses), so the display list size and the
// layout bounds are host-tested. No Polyend includes.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace seqnotes {

// ---- note names: "C3" = MIDI 60 (the Tracker's default middle C), "D#4" ----
inline void noteName(int note, char* out, size_t n)
{
    static const char* const kNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (note < 0 || note > 127)
    {
        snprintf(out, n, "---");
        return;
    }
    snprintf(out, n, "%s%d", kNames[note % 12], note / 12 - 2);
}

// 128-bit mask of the sounding notes (bit n of byte n/8) -> the pitch classes that sound (bit 0 = C)
inline uint16_t pitchClasses(const uint8_t mask[16])
{
    uint16_t pc = 0;
    for (int n = 0; n < 128; n++)
        if (mask[n >> 3] & (1u << (n & 7)))
            pc = (uint16_t)(pc | (1u << (n % 12)));
    return pc;
}

enum { kNamesMax = 20, kNameChars = 11 }; // + " +n": at most 14 characters = 126 px

// The sounding notes from low to high as "C3 D#3 G3"; when the line would be longer than kNameChars the rest is "+n".
inline void noteList(const uint8_t mask[16], char* out, size_t cap)
{
    if (cap == 0)
        return;
    out[0] = 0;
    int count = 0, shown = 0;
    size_t len = 0;
    for (int n = 0; n < 128; n++)
        if (mask[n >> 3] & (1u << (n & 7)))
            count++;
    for (int n = 0; n < 128 && shown < count; n++)
    {
        if (!(mask[n >> 3] & (1u << (n & 7))))
            continue;
        char t[8];
        noteName(n, t, sizeof t);
        const size_t tl = strlen(t);
        const size_t need = len + (len ? 1 : 0) + tl;
        if (need > (size_t)kNameChars)
            break;
        if (len)
            out[len++] = ' ';
        memcpy(out + len, t, tl + 1);
        len += tl;
        shown++;
    }
    if (shown < count)
    {
        char rest[8];
        snprintf(rest, sizeof rest, " +%d", count - shown);
        if (len + strlen(rest) < cap)
            strcpy(out + len, rest);
    }
}

// ---- the band of Play mode ----
struct Band
{
    uint8_t show;      // draw it (a sequencer track is selected)
    int8_t root;       // pitch class of the root (Strings setup), -1 = the sequencer has none
    uint16_t pc;       // pitch classes sounding
    char names[kNamesMax];
    Band() { memset(this, 0, sizeof(*this)); root = -1; }
};

inline void fillBand(Band& b, bool show, const uint8_t mask[16], int root)
{
    memset(&b, 0, sizeof(b));
    b.root = (int8_t)(root >= 0 && root < 12 ? root : -1);
    b.show = show ? 1 : 0;
    if (!show)
        return;
    b.pc = pitchClasses(mask);
    noteList(mask, b.names, sizeof b.names);
}

struct Palette
{
    uint32_t bg, off, lit, root, text;
};

enum { kBandX = 296, kKeyStep = 22, kKeyW = 20, kBandY0 = 402, kBandY1 = 424, kKeyY0 = 405, kKeyYNat = 419, kKeyYSharp = 413,
       kNamesX = 572, kNamesY = 412, kBandX2 = 700 };

inline bool isSharp(int pc) { return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }

// c: color(), rect(x0,y0,x1,y1) (exclusive), textPhase(), text(), finish()
template <class C> void drawBand(C& c, const Band& b, const Palette& p)
{
    if (!b.show)
        return;
    c.color(p.bg);
    c.rect(kBandX - 4, kBandY0, kBandX2, kBandY1);
    for (int pass = 0; pass < 2; pass++) // idle keys, then the sounding ones
    {
        c.color(pass == 0 ? p.off : p.lit);
        for (int k = 0; k < 12; k++)
            if ((((b.pc >> k) & 1u) != 0) == (pass == 1))
                c.rect(kBandX + k * kKeyStep, kKeyY0, kBandX + k * kKeyStep + kKeyW, isSharp(k) ? kKeyYSharp : kKeyYNat);
    }
    if (b.root >= 0)
    {
        c.color(p.root);
        c.rect(kBandX + b.root * kKeyStep + 4, kKeyYNat + 1, kBandX + b.root * kKeyStep + kKeyW - 4, kKeyYNat + 4);
    }
    c.textPhase();
    if (b.names[0])
    {
        c.color(p.text);
        c.text(kNamesX, kNamesY, 3, 2, b.names);
    }
    c.finish();
}

// ---- the voice row of the PolySynth overlay (4 voices): note name + envelope level bar ----
enum { kVoices = 4, kVoiceX = 200, kVoiceStep = 150, kVoiceBar = 60, kVoiceY = 412 };
struct VoiceView
{
    uint8_t note;  // 0xFF = idle
    uint8_t level; // 0 .. kVoiceBar
    uint8_t rel;   // releasing
    VoiceView() : note(0xFF), level(0), rel(0) {}
};

}  // namespace seqnotes
