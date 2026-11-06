// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Plinky parameter overlay (plan 010 B): sections, cursor, value steps, value texts and the drawing over a tiny context, so the same
// code runs on the FT81x display list (interface/seqPlay/seqPlayLegend.cpp) and in the host tests (display-list size, layout bounds).
// No Polyend includes.
//
// Layout: five section rows (Oscillator, Filter, Envelope, LFO, Voice), each with up to six parameter cells (name, bar, value).
// (A * after a name: the value differs from the inherited one, plan 012.) Left / Right move between the parameters of a section, Up / Down between the sections (the column is kept as far as the new
// section has it). Jog changes the value (Shift = fine), Default resets it, Instrument Parameters closes the overlay.
//
// Context C provides: void color(uint32_t); void rect(x0,y0,x1,y1) (x1 / y1 exclusive); void text(x,y,font,opt,const char*);
// void textPhase() (called once: the shapes are done, the texts follow); void finish().
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "PlinkyVoice/plinky_params.h"
#include "seqNotesViewLogic.h"
#include "seqPalette.h"
#include "seqPlinkyMorphLogic.h"

namespace seqplinky {

using namespace plinkyvoice;

// plan 012: the morph time is a project setting shown next to the parameters (a virtual parameter after the engine's), 0 .. 60 half seconds
enum { P_MORPH = P_COUNT, kUiParams = P_COUNT + 1 };
inline const ParamInfo& uiInfo(int p)
{
    static const ParamInfo m = { 0, plinkymorph::kHalfSecMax, plinkymorph::kHalfSecDefault, "Morph" };
    return p == P_MORPH ? m : paramInfo(p);
}

enum { kSections = 5, kMaxCells = 7 };
struct Section
{
    const char* name;
    uint8_t count;
    int8_t param[kMaxCells];
};
inline const Section& section(uint8_t i)
{
    static const Section k[kSections] = {
        { "Oscillator", 4, { P_SHAPE, P_DETUNE, P_INTERVAL, P_NOISE, -1, -1, -1 } },
        { "Filter", 3, { P_CUTOFF, P_RESO, P_DRIVE, -1, -1, -1, -1 } },
        { "Envelope", 4, { P_ATTACK, P_DECAY, P_SUSTAIN, P_RELEASE, -1, -1, -1 } },
        { "LFO", 3, { P_LFO_RATE, P_LFO_DEPTH, P_LFO_DEST, -1, -1, -1, -1 } },
        { "Voice", 7, { P_VOICES, P_GLIDE, P_WIDTH, P_LEVEL, P_REVERB, P_DELAY, P_MORPH } },
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
        case P_RESO: return "Reso";
        case P_MORPH: return "Morph";
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
    if (param == P_MORPH)
        return fine ? 1 : 2; // half seconds: 0.5 s fine, 1 s coarse
    const ParamInfo& pi = paramInfo(param);
    return (fine || pi.max - pi.min <= 24) ? 1 : 5;
}
inline int jogValue(int param, int cur, int delta, bool fine)
{
    int mag = delta < 0 ? -delta : delta;
    if (mag > 3) mag = 3;
    int v = cur + (delta < 0 ? -mag : mag) * jogUnit(param, fine);
    const ParamInfo& pi = uiInfo(param);
    return (uint8_t)(v < pi.min ? pi.min : (v > pi.max ? pi.max : v));
}

// Shift + dial (plan 019 A): the value one instance gets when the delta is added to ALL of them, clamped to its own range
inline int allValue(int param, int cur, int delta)
{
    const ParamInfo& pi = uiInfo(param);
    const int v = cur + delta;
    return v < pi.min ? pi.min : (v > pi.max ? pi.max : v);
}

inline void valueText(int p, int v, char* out, size_t n)
{
    static const char* const kDest[3] = { "Pitch", "Shape", "Cutoff" };
    switch (p)
    {
        case P_INTERVAL: snprintf(out, n, "%+d st", v - 12); break;
        case P_MORPH: plinkymorph::timeText(v, out, n); break;
        case P_LFO_DEST: snprintf(out, n, "%s", kDest[v > 2 ? 2 : v]); break;
        case P_SHAPE: shapeText(v, out, n); break;
        case P_WIDTH: if (v == 0) snprintf(out, n, "Mono"); else snprintf(out, n, "%d", v); break;
        default: snprintf(out, n, "%d", v); break;
    }
}

// the cell name; "Cutoff*" when the value differs from the inherited one (plan 012)
inline const char* markedName(int p, uint32_t mark, char* out)
{
    if (p >= P_COUNT || !((mark >> p) & 1u))
        return cellName(p);
    snprintf(out, 16, "%s*", cellName(p));
    return out;
}

struct Model
{
    uint8_t v[kUiParams];
    uint8_t sec, idx;
    uint8_t track;   // 1..16
    uint8_t morphPat;    // plan 012: a morph is running towards this pattern (0 = none)
    uint16_t morphMs;    // ... and this much of it is left
    uint32_t mark;       // bit per parameter whose value differs from the inherited one (plan 012)
    seqnotes::VoiceView voice[seqnotes::kVoices]; // the 4 voices (v0.3.3): note and envelope level
    uint8_t channel; // P_CHANNEL (not shown since v0.3.2)
    uint8_t cpu, peak;
    uint8_t newer;   // plinky.bin of a newer mod version: edits are not saved
    uint8_t inst;    // plan 019: the instance shown (0 .. 2)
    uint8_t all;     // plan 019: Shift is held: a dial edit goes to ALL instances
    uint8_t cap;     // plan 019: voices the pool may sound now (the governor), 0 .. kPool
    Model() { memset(this, 0, sizeof(*this)); for (int i = 0; i < seqnotes::kVoices; i++) voice[i].note = 0xFF; }
};

struct Palette
{
    uint32_t bg, panel, bar, text, section, accent, onAccent, fill;
};

// plan 019 A: tabs "SYNTH 1 | SYNTH 2 | SYNTH 3" at the top, each in the colour of its instance (also its lines in the pitch view)
enum { kTabY0 = 31, kTabY1 = 53, kTabX0 = 8, kTabStep = 120, kTabW = 114, kAllX0 = 722, kAllX1 = 792 };
inline uint32_t tabColor(int inst)
{
    return inst == 0 ? seqpal::kNote : inst == 1 ? seqpal::kInstr : seqpal::kFx1;
}

enum { kPanelY0 = 30, kPanelY1 = 424, kRowY0 = 56, kRowH = 62, kRowStep = 66, kBoxX0 = 8, kBoxX1 = 792, kCellX0 = 112, kCellStep = 96,
       kCellW = 92, kBarW = 80 }; // seven cells in the Voice row (plan 012: Morph)

template <class C> void draw(C& c, const Model& m, const Palette& p)
{
    char buf[72], nm[16];
    const int selSec = m.sec < kSections ? m.sec : 0;
    // ---- shapes ----
    c.color(p.bg);
    c.rect(0, kPanelY0, 800, kPanelY1);
    const int inst = m.inst < kInstances ? m.inst : 0;
    for (int i = 0; i < kInstances; i++)
    {
        const int x0 = kTabX0 + i * kTabStep, x1 = x0 + kTabW;
        c.color(i == inst ? tabColor(i) : seqpal::dim(tabColor(i)));
        c.rect(x0, kTabY0, x1, kTabY1);
        if (i != inst) // inactive: a dim outline
        {
            c.color(p.bg);
            c.rect(x0 + 2, kTabY0 + 2, x1 - 2, kTabY1 - 2);
        }
    }
    if (m.all)
    {
        c.color(p.accent);
        c.rect(kAllX0, kTabY0, kAllX1, kTabY1);
    }
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
                const ParamInfo& pi = uiInfo(par);
                const int w = pi.max > pi.min ? (m.v[par] - pi.min) * kBarW / (pi.max - pi.min) : 0;
                if (w > 0)
                    c.rect(kCellX0 + i * kCellStep + 6, kRowY0 + s * kRowStep + 24, kCellX0 + i * kCellStep + 6 + w, kRowY0 + s * kRowStep + 34);
            }
    }
    if (!m.newer)  // voice levels (the row sits on the footer line 2; the "newer file" notice needs that line)
    {
        c.color(p.fill);
        for (int i = 0; i < seqnotes::kVoices; i++)
            if (m.voice[i].note != 0xFF && m.voice[i].level > 0)
                c.rect(seqnotes::kVoiceX + i * seqnotes::kVoiceStep + 44, seqnotes::kVoiceY - 4,
                       seqnotes::kVoiceX + i * seqnotes::kVoiceStep + 44 + m.voice[i].level, seqnotes::kVoiceY + 4);
    }
    // ---- texts ----
    c.textPhase();
    for (int i = 0; i < kInstances; i++) // the tab labels: dark on the bright tab, the instance colour on the dim ones
    {
        c.color(i == inst ? p.onAccent : tabColor(i));
        snprintf(buf, sizeof buf, "SYNTH %d", i + 1);
        c.text(kTabX0 + i * kTabStep + kTabW / 2, (kTabY0 + kTabY1) / 2, 3, 1 | 2, buf);
    }
    if (m.all)
    {
        c.color(p.onAccent);
        c.text((kAllX0 + kAllX1) / 2, (kTabY0 + kTabY1) / 2, 3, 1 | 2, "ALL");
    }
    c.color(p.text);
    // m.track 0 = the instrument page (v0.3.6: same drawing, no track number). Right of the tabs: morph, or the track and the CPU.
    if (m.morphPat)
    {
        char mt[24];
        plinkymorph::titleText(mt, sizeof mt, m.morphMs, m.morphPat);
        if (m.track) snprintf(buf, sizeof buf, "T%d  %s", m.track, mt);
        else snprintf(buf, sizeof buf, "%s", mt);
    }
    else if (m.track)
        snprintf(buf, sizeof buf, "T%d CPU %d%% V%d", m.track, m.cpu, m.cap);
    else
        snprintf(buf, sizeof buf, "CPU %d%%  pk %d%%  V%d", m.cpu, m.peak, m.cap);
    c.text(kTabX0 + 3 * kTabStep + 8, 42, 3, 2, buf);
    c.text(12, 398, 1, 2, m.track ? "Jog edit, Shift+jog ALL, Del reset, Instr next SYNTH, FX2 pitch" : "Jog edit, Shift+jog ALL, Del reset, Params next SYNTH, FX2 pitch");
    if (m.newer)
        c.text(12, 412, 1, 2, "Newer synth file: edits are not saved");
    if (!m.newer)
        for (int i = 0; i < seqnotes::kVoices; i++)
            if (m.voice[i].note != 0xFF)
            {
                char nn[8], vt[12];
                seqnotes::noteName(m.voice[i].note, nn, sizeof nn);
                snprintf(vt, sizeof vt, m.voice[i].rel ? "~%s" : "%s", nn); // ~C3 = releasing
                c.text(seqnotes::kVoiceX + i * seqnotes::kVoiceStep, seqnotes::kVoiceY, 1, 2, vt);
            }
    for (int s = 0; s < kSections; s++)
        for (int i = 0; i < section((uint8_t)s).count; i++)
        {
            if (s == selSec && i == m.idx) continue;
            const int par = section((uint8_t)s).param[i];
            const int x = kCellX0 + i * kCellStep + 6, y = kRowY0 + s * kRowStep;
            valueText(par, m.v[par], buf, sizeof buf);
            c.text(x, y + 11, 3, 2, markedName(par, m.mark, nm));
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
        c.text(x, y + 11, 3, 2, markedName(par, m.mark, nm));
        c.text(x, y + 48, 3, 2, buf);
    }
    c.finish();
}

} // namespace seqplinky
