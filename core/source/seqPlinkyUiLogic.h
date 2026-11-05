// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Plinky parameter overlay (plan 010 B): sections, cursor, value steps, value texts and the drawing over a tiny context, so the same
// code runs on the FT81x display list (interface/seqPlay/seqPlayLegend.cpp) and in the host tests (display-list size, layout bounds).
// No Polyend includes.
//
// Layout: five section rows (Oscillator, Filter, Envelope, LFO, Voice), each with up to six parameter cells (name, bar, value).
// Left / Right move between the parameters of a section, Up / Down between the sections (the column is kept as far as the new
// section has it). Jog changes the value (Shift = fine), Default resets it, Instrument Parameters closes the overlay.
//
// Context C provides: void color(uint32_t); void rect(x0,y0,x1,y1) (x1 / y1 exclusive); void text(x,y,font,opt,const char*);
// void textPhase() (called once: the shapes are done, the texts follow); void finish().
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "PlinkyVoice/plinky_params.h"

namespace seqplinky {

using namespace plinkyvoice;

enum { kSections = 5, kMaxCells = 6 };
struct Section
{
    const char* name;
    uint8_t count;
    int8_t param[kMaxCells];
};
inline const Section& section(uint8_t i)
{
    static const Section k[kSections] = {
        { "Oscillator", 4, { P_SHAPE, P_DETUNE, P_INTERVAL, P_NOISE, -1, -1 } },
        { "Filter", 3, { P_CUTOFF, P_RESO, P_DRIVE, -1, -1, -1 } },
        { "Envelope", 4, { P_ATTACK, P_DECAY, P_SUSTAIN, P_RELEASE, -1, -1 } },
        { "LFO", 3, { P_LFO_RATE, P_LFO_DEPTH, P_LFO_DEST, -1, -1, -1 } },
        { "Voice", 6, { P_VOICES, P_GLIDE, P_WIDTH, P_LEVEL, P_REVERB, P_DELAY } },
    };
    return k[i < kSections ? i : 0];
}

// short names of the cells (the section already says "LFO")
inline const char* cellName(int p)
{
    switch (p)
    {
        case P_LFO_RATE: return "Rate";
        case P_LFO_DEPTH: return "Depth";
        case P_LFO_DEST: return "Target";
        case P_RESO: return "Resonance";
        default: return paramInfo(p).name;
    }
}

struct Cursor
{
    uint8_t sec = 0, idx = 0;
    int param() const { return section(sec).param[idx]; }
    void fix() { if (sec >= kSections) sec = 0; if (idx >= section(sec).count) idx = (uint8_t)(section(sec).count - 1); }
    // Left / Right: next parameter in the section (no wrap). true when the cursor moved
    bool horizontal(int dir)
    {
        const Cursor b = *this;
        if (dir < 0 && idx > 0) idx--;
        else if (dir > 0 && idx + 1 < section(sec).count) idx++;
        return !(b.sec == sec && b.idx == idx);
    }
    // Up / Down: next section (no wrap), the column is kept as far as it exists
    bool vertical(int dir)
    {
        const Cursor b = *this;
        if (dir < 0 && sec > 0) sec--;
        else if (dir > 0 && sec + 1 < kSections) sec++;
        fix();
        return !(b.sec == sec && b.idx == idx);
    }
};

// jog step: fine = 1; coarse = 5 on the 0..100 parameters, 1 on the short ranges (interval, voices, LFO target)
inline int jogUnit(int param, bool fine)
{
    const ParamInfo& pi = paramInfo(param);
    return (fine || pi.max - pi.min <= 24) ? 1 : 5;
}
inline int jogValue(int param, int cur, int delta, bool fine)
{
    int mag = delta < 0 ? -delta : delta;
    if (mag > 3) mag = 3;
    int v = cur + (delta < 0 ? -mag : mag) * jogUnit(param, fine);
    return clampParam(param, v);
}

inline void valueText(int p, int v, char* out, size_t n)
{
    static const char* const kDest[3] = { "Pitch", "Shape", "Cutoff" };
    switch (p)
    {
        case P_INTERVAL: snprintf(out, n, "%+d st", v - 12); break;
        case P_LFO_DEST: snprintf(out, n, "%s", kDest[v > 2 ? 2 : v]); break;
        case P_SHAPE: if (v == 0) snprintf(out, n, "Saws"); else snprintf(out, n, "Pulse %d", v); break;
        case P_WIDTH: if (v == 0) snprintf(out, n, "Mono"); else snprintf(out, n, "%d", v); break;
        default: snprintf(out, n, "%d", v); break;
    }
}

struct Model
{
    uint8_t v[P_COUNT];
    uint8_t sec, idx;
    uint8_t track;   // 1..16
    uint8_t channel; // P_CHANNEL
    uint8_t cpu, peak;
    Model() { memset(this, 0, sizeof(*this)); }
};

struct Palette
{
    uint32_t bg, panel, bar, text, section, accent, onAccent, fill;
};

enum { kPanelY0 = 30, kPanelY1 = 424, kRowY0 = 56, kRowH = 62, kRowStep = 66, kBoxX0 = 8, kBoxX1 = 792, kCellX0 = 112, kCellStep = 113,
       kCellW = 108, kBarW = 96 };

template <class C> void draw(C& c, const Model& m, const Palette& p)
{
    char buf[48];
    const int selSec = m.sec < kSections ? m.sec : 0;
    // ---- shapes ----
    c.color(p.bg);
    c.rect(0, kPanelY0, 800, kPanelY1);
    c.color(p.panel);
    for (int s = 0; s < kSections; s++)
        c.rect(kBoxX0, kRowY0 + s * kRowStep, kBoxX1, kRowY0 + s * kRowStep + kRowH);
    c.color(p.accent); // current section strip + selected cell
    c.rect(kBoxX0, kRowY0 + selSec * kRowStep, kBoxX0 + 4, kRowY0 + selSec * kRowStep + kRowH);
    {
        const int cx = kCellX0 + m.idx * kCellStep, cy = kRowY0 + selSec * kRowStep;
        c.rect(cx, cy + 2, cx + kCellW, cy + kRowH - 2);
    }
    c.color(p.bar);
    for (int s = 0; s < kSections; s++)
        for (int i = 0; i < section((uint8_t)s).count; i++)
            c.rect(kCellX0 + i * kCellStep + 6, kRowY0 + s * kRowStep + 24, kCellX0 + i * kCellStep + 6 + kBarW, kRowY0 + s * kRowStep + 34);
    for (int pass = 0; pass < 2; pass++) // bar fills: the selected one in the dark colour
    {
        c.color(pass == 0 ? p.fill : p.onAccent);
        for (int s = 0; s < kSections; s++)
            for (int i = 0; i < section((uint8_t)s).count; i++)
            {
                const bool sel = s == selSec && i == m.idx;
                if (sel != (pass == 1)) continue;
                const int par = section((uint8_t)s).param[i];
                const ParamInfo& pi = paramInfo(par);
                const int w = pi.max > pi.min ? (m.v[par] - pi.min) * kBarW / (pi.max - pi.min) : 0;
                if (w > 0)
                    c.rect(kCellX0 + i * kCellStep + 6, kRowY0 + s * kRowStep + 24, kCellX0 + i * kCellStep + 6 + w, kRowY0 + s * kRowStep + 34);
            }
    }
    // ---- texts ----
    c.textPhase();
    c.color(p.text);
    snprintf(buf, sizeof buf, "Plinky  T%d  MIDI %d  CPU %d%% pk %d%%", m.track, m.channel, m.cpu, m.peak);
    c.text(12, 42, 3, 2, buf);
    c.text(12, 398, 1, 2, "Left/Right: param  Up/Down: section  Jog: value (Shift: fine)  Default: reset");
    c.text(12, 412, 1, 2, "Instr: close");
    for (int s = 0; s < kSections; s++)
        for (int i = 0; i < section((uint8_t)s).count; i++)
        {
            if (s == selSec && i == m.idx) continue;
            const int par = section((uint8_t)s).param[i];
            const int x = kCellX0 + i * kCellStep + 6, y = kRowY0 + s * kRowStep;
            valueText(par, m.v[par], buf, sizeof buf);
            c.text(x, y + 11, 3, 2, cellName(par));
            c.text(x, y + 48, 3, 2, buf);
        }
    c.color(p.section);
    for (int s = 0; s < kSections; s++)
        c.text(kBoxX0 + 12, kRowY0 + s * kRowStep + kRowH / 2, 3, 2, section((uint8_t)s).name);
    c.color(p.onAccent);
    {
        const int par = section((uint8_t)selSec).param[m.idx];
        const int x = kCellX0 + m.idx * kCellStep + 6, y = kRowY0 + selSec * kRowStep;
        valueText(par, m.v[par], buf, sizeof buf);
        c.text(x, y + 11, 3, 2, cellName(par));
        c.text(x, y + 48, 3, 2, buf);
    }
    c.finish();
}

} // namespace seqplinky
