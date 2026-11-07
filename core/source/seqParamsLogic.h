// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Plan 025: the settings of a track's sequencer as a parameter page (shown above the pad legend, navigated and changed like the PolySynth
// page: arrows move, the jog wheel changes the value, Delete resets it). Tables per sequencer type (section rows of up to seven cells),
// value texts, the cursor and the drawing over a tiny context (same as seqPlinkyUiLogic.h: color / rect / text / textPhase / finish), so the
// same code runs on the FT81x display list and in the host tests (display list size, layout bounds, preview). No Polyend includes.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "chordarp.h"
#include "mono.h"
#include "hr_scripts.h"
#include "seqNotesViewLogic.h"
#include "seqPlayLogic.h"
#include "seqPlinkySeqLogic.h"
#include "seqTracksLogic.h"

namespace seqparams {

using seqlogic::TYPE_CHORDARP;
using seqlogic::TYPE_MONO;
using seqlogic::TYPE_PLINKY;

// ids beyond the generators' own (engine settings)
enum : uint8_t { ID_LATCH = 0xF0, ID_PL_ROOT = 0xF1, ID_PL_SCALE = 0xF2, ID_PL_OCTAVE = 0xF3, ID_PL_INTERVAL = 0xF4 };
enum Fmt : uint8_t { F_NUM = 0, F_SIGN, F_LIST, F_NOTE, F_PC, F_CHORD, F_SCALE, F_RATE, F_MSPEED };
enum { kMaxCells = 7, kMaxSections = 4, kMaxParams = 24 };

// name, generator id, range, default, text format, list (index into lists(), shows list[v - mn])
struct Desc
{
    const char* name;
    uint8_t id;
    int8_t mn, mx, def;
    uint8_t fmt, list;
};
struct Sec
{
    const char* name;
    uint8_t n;
};
struct Tab
{
    uint8_t nSec, nTotal;
    const Sec* sec;
    const Desc* d;
};

enum List : uint8_t
{
    L_NONE = 0, L_ONOFF, L_CA_TYPE, L_CA_SPEED, L_CA_DIR, L_CA_MODE, L_GATE, L_QUAL, L_STAB_CHORD, L_ROLE, L_HR_TYPE, L_HR_MODE, L_HR_SPEED, L_HR_DIR,
    L_AS_DIR, L_OCT3, L_LOCK, L_BEAT, L_COUNT
};
inline const char* const* lists(uint8_t l)
{
    static const char* const k0[] = { "" };
    static const char* const kOnOff[] = { "Off", "On" };
    static const char* const kCaType[] = { "Chord", "Arp", "Arp+Oct", "Off" };
    static const char* const kCaSpeed[] = { "1/8", "1/8T", "1/16", "1/16T", "1/4", "1/4T", "1/32", "1/32T", "1/2", "1/2T" };
    static const char* const kCaDir[] = { "Up", "Down", "UpDn", "Patt", "UpDn2", "DnUp", "Random", "Conv", "InOut", "RootRt" };
    static const char* const kCaMode[] = { "Key", "Locked", "Grid" };
    static const char* const kGate[] = { "Normal", "Short", "Legato" };
    static const char* const kQual[] = { "Major", "Minor" };
    static const char* const kStabChord[] = { "Triad", "6th", "7th", "9th", "add9", "Sus2", "Sus4", "5th" };
    static const char* const kRole[] = { "Lead", "Follow" };
    static const char* const kHrType[] = { "maj", "min", "dom7", "sus4", "maj7", "min7", "dim", "aug", "6", "add9" };
    static const char* const kHrMode[] = { "Chord", "Arp" };
    static const char* const kHrSpeed[] = { "1/8", "1/8T", "1/16", "1/16T" };
    static const char* const kHrDir[] = { "Up", "Down", "UpDn", "Patt" };
    static const char* const kAsDir[] = { "Up", "Down", "UpDn", "Zigzag" };
    static const char* const kOct3[] = { "-1", "0", "+1" };
    static const char* const kLock[] = { "Free", "Lock" };
    static const char* const kBeat[] = { "Now", "Beat" };
    static const char* const* const k[L_COUNT] = { k0, kOnOff, kCaType, kCaSpeed, kCaDir, kCaMode, kGate, kQual, kStabChord, kRole, kHrType, kHrMode,
                                                   kHrSpeed, kHrDir, kAsDir, kOct3, kLock, kBeat };
    return k[l < L_COUNT ? l : 0];
}

#define D(name, id, mn, mx, def, fmt, list) { name, (uint8_t)(id), mn, mx, def, fmt, list }
// ---- Chord / Arp (c31 page 3) ----
inline const Tab& tabChordArp()
{
    using C = seqgen::ChordArp;
    static const Desc d[] = {
        D("Type", C::P_SEL_TYPE, 1, 4, 1, F_LIST, L_CA_TYPE), D("Speed", C::P_SEL_SPEED, 1, 10, 1, F_LIST, L_CA_SPEED), D("Dir", C::P_SEL_DIR, 1, 10, 1, F_LIST, L_CA_DIR),
        D("Size", C::P_SEL_SIZE, 2, 5, 5, F_NUM, 0), D("Chord", C::P_SEL_MOD, 1, 15, 1, F_CHORD, 0), D("Octaves", C::P_ORANGE, 1, 4, 1, F_NUM, 0),
        D("Latch", seqparams::ID_LATCH, 0, 1, 0, F_LIST, L_ONOFF),
        D("Oct", C::P_KOCT, -1, 1, 0, F_SIGN, 0), D("Inv", C::P_INV, -6, 6, 0, F_SIGN, 0), D("Transp", C::P_TPOSE, -24, 24, 0, F_SIGN, 0),
        D("Mode", C::P_PMODE, 1, 3, 1, F_LIST, L_CA_MODE), D("Lock key", C::P_LOCK_ROOT, 54, 65, 60, F_PC, 0), D("Minor", C::P_LOCK_MINOR, 0, 1, 0, F_LIST, L_ONOFF),
        D("Groove", C::P_SEL_GROOVE, 1, 16, 1, F_NUM, 0), D("Velocity", C::P_SEL_VG, 1, 72, 1, F_NUM, 0), D("Lane", C::P_LANE, 0, 36, 0, F_NUM, 0),
        D("Swing", C::P_SWING, 0, 3, 0, F_NUM, 0), D("Gate", C::P_GATE, 0, 2, 0, F_LIST, L_GATE),
        D("Param", C::P_PON, 0, 1, 0, F_LIST, L_ONOFF), D("Arch", C::P_PARCH, 1, 4, 1, F_NUM, 0), D("Down", C::P_PW0, 0, 3, 2, F_NUM, 0),
        D("8th", C::P_PW1, 0, 3, 1, F_NUM, 0), D("16th", C::P_PW2, 0, 3, 0, F_NUM, 0),
    };
    static const Sec s[] = { { "Play", 7 }, { "Pitch", 6 }, { "Feel", 5 }, { "Groove", 5 } };
    static const Tab t = { 4, 23, s, d };
    return t;
}
// ---- Mono (c31 page 4) ----
inline const Tab& tabMono()
{
    using M = seqgen::Mono;
    static const Desc d[] = {
        D("Speed", M::P_SPEED, 1, 12, 8, F_MSPEED, 0), D("Tie", M::P_TIE, 0, 1, 0, F_LIST, L_ONOFF), D("Pages", M::P_NP, 1, 8, 1, F_NUM, 0),
        D("Loop in", M::P_LSTART, 1, 16, 1, F_NUM, 0), D("Loop out", M::P_LEND, 1, 16, 16, F_NUM, 0),
        D("Window", M::P_WIN, -40, 40, 0, F_SIGN, 0), D("Transp", M::P_TPOSE, -24, 24, 0, F_SIGN, 0), D("Velocity", M::P_VG, 1, 72, 1, F_NUM, 0),
    };
    static const Sec s[] = { { "Time", 5 }, { "Pitch", 3 } };
    static const Tab t = { 2, 8, s, d };
    return t;
}
// ---- Strings (the PolySynth played from the pads) ----
inline const Tab& tabStrings()
{
    static const Desc d[] = {
        D("Root", ID_PL_ROOT, 0, 11, 0, F_PC, 0), D("Scale", ID_PL_SCALE, 0, 7, 1, F_SCALE, 0), D("Octave", ID_PL_OCTAVE, 1, 4, 2, F_NUM, 0),
        D("Interval", ID_PL_INTERVAL, 1, 7, 3, F_NUM, 0), D("Latch", ID_LATCH, 0, 1, 0, F_LIST, L_ONOFF),
    };
    static const Sec s[] = { { "Strings", 5 } };
    static const Tab t = { 1, 5, s, d };
    return t;
}
// ---- HYPER_RASP scripts (seqgen::HrType order) ----
inline const Tab& tabStabs(bool arp)
{
    using S = seqgen::HrStab;
    static const Desc dArp[] = {
        D("Octave", S::P_OCTAVE, -2, 2, 0, F_SIGN, 0), D("Transp", S::P_TRANSP, -12, 12, 0, F_SIGN, 0), D("Velocity", S::P_VEL, 1, 127, 100, F_NUM, 0),
        D("Rate", S::P_RATE, 1, 24, 6, F_RATE, 0),
        D("Pattern", S::P_PATTERN, 1, 10, 1, F_NUM, 0), D("Chord", S::P_CHORD, 1, 8, 1, F_LIST, L_STAB_CHORD), D("Quality", S::P_QUALITY, 1, 2, 1, F_LIST, L_QUAL),
        D("Role", S::P_ROLE, 1, 2, 1, F_LIST, L_ROLE), D("Latch", ID_LATCH, 0, 1, 1, F_LIST, L_ONOFF),
    };
    static const Desc dStab[] = {
        D("Octave", S::P_OCTAVE, -2, 2, 0, F_SIGN, 0), D("Transp", S::P_TRANSP, -12, 12, 0, F_SIGN, 0), D("Velocity", S::P_VEL, 1, 127, 100, F_NUM, 0),
        D("Rate", S::P_RATE, 1, 24, 6, F_RATE, 0),
        D("Pattern", S::P_PATTERN, 1, 10, 3, F_NUM, 0), D("Chord", S::P_CHORD, 1, 8, 1, F_LIST, L_STAB_CHORD), D("Quality", S::P_QUALITY, 1, 2, 1, F_LIST, L_QUAL),
        D("Latch", ID_LATCH, 0, 1, 1, F_LIST, L_ONOFF),
    };
    static const Sec sArp[] = { { "Sound", 4 }, { "Pattern", 5 } };
    static const Sec sStab[] = { { "Sound", 4 }, { "Pattern", 4 } };
    static const Tab ta = { 2, 9, sArp, dArp };
    static const Tab ts = { 2, 8, sStab, dStab };
    return arp ? ta : ts;
}
inline const Tab& tabArpSeq()
{
    using A = seqgen::HrArpSeq;
    static const Desc d[] = {
        D("Mode", A::P_MODE, 1, 2, 1, F_LIST, L_HR_MODE), D("Speed", A::P_SPEED, 1, 4, 3, F_LIST, L_HR_SPEED), D("Dir", A::P_DIR, 1, 4, 1, F_LIST, L_AS_DIR),
        D("Groove", A::P_GROOVE, 1, 12, 1, F_NUM, 0), D("Octave", A::P_OCT, 1, 3, 2, F_LIST, L_OCT3), D("Quality", A::P_QUALITY, 1, 2, 1, F_LIST, L_QUAL),
        D("Latch", ID_LATCH, 0, 1, 1, F_LIST, L_ONOFF),
    };
    static const Sec s[] = { { "Arp", 7 } };
    static const Tab t = { 1, 7, s, d };
    return t;
}
inline const Tab& tabHrChordArp()
{
    using H = seqgen::HrChordArp;
    static const Desc d[] = {
        D("Type", H::P_TYPE, 1, 10, 1, F_LIST, L_HR_TYPE), D("Size", H::P_SIZE, 2, 5, 5, F_NUM, 0), D("Octave", H::P_OCTAVE, -1, 1, 0, F_SIGN, 0),
        D("Inv", H::P_INV, -6, 6, 0, F_SIGN, 0), D("Transp", H::P_TRANSP, -12, 12, 0, F_SIGN, 0), D("Velocity", H::P_VEL, 1, 127, 100, F_NUM, 0),
        D("Latch", ID_LATCH, 0, 1, 1, F_LIST, L_ONOFF),
        D("Mode", H::P_MODE, 1, 2, 1, F_LIST, L_HR_MODE), D("Speed", H::P_SPEED, 1, 4, 1, F_LIST, L_HR_SPEED), D("Dir", H::P_DIR, 1, 4, 1, F_LIST, L_HR_DIR),
        D("Groove", H::P_GROOVE, 1, 12, 1, F_NUM, 0), D("Gate", H::P_GATE, 1, 16, 1, F_NUM, 0),
        D("Key lock", H::P_KEYLOCK, 1, 2, 1, F_LIST, L_LOCK), D("Key", H::P_KEY, 0, 11, 0, F_PC, 0), D("Scale", H::P_SCALE, 1, 2, 1, F_LIST, L_QUAL),
        D("Start", H::P_QSTART, 1, 2, 1, F_LIST, L_BEAT),
    };
    static const Sec s[] = { { "Chord", 7 }, { "Arp", 5 }, { "Key", 4 } };
    static const Tab t = { 3, 16, s, d };
    return t;
}
inline const Tab& tabBass()
{
    using B = seqgen::HrBass;
    static const Desc d[] = {
        D("Octave", B::P_OCTAVE, -2, 2, 0, F_SIGN, 0), D("Dir", B::P_DIR, 1, 4, 1, F_NUM, 0), D("Groove", B::P_GROOVE, 1, 12, 1, F_NUM, 0),
        D("Latch", ID_LATCH, 0, 1, 0, F_LIST, L_ONOFF),
    };
    static const Sec s[] = { { "Bass", 4 } };
    static const Tab t = { 1, 4, s, d };
    return t;
}
inline const Tab& tabEuclid()
{
    using E = seqgen::HrEuclid;
    static const Desc d[] = {
        D("Grouping", E::P_GROUPING, 1, 8, 1, F_NUM, 0), D("Velocity", E::P_VEL, 1, 127, 100, F_NUM, 0), D("Swing", E::P_SWING, 0, 60, 0, F_NUM, 0),
        D("Chance", E::P_CHANCE, 0, 100, 100, F_NUM, 0),
        D("Note 1", E::P_NOTE1, 24, 96, 36, F_NOTE, 0), D("Note 2", E::P_NOTE2, 24, 96, 38, F_NOTE, 0), D("Note 3", E::P_NOTE3, 24, 96, 46, F_NOTE, 0),
        D("Note 4", E::P_NOTE4, 24, 96, 42, F_NOTE, 0),
    };
    static const Sec s[] = { { "Euclid", 4 }, { "Notes", 4 } };
    static const Tab t = { 2, 8, s, d };
    return t;
}
inline const Tab& tabGridSeq()
{
    using G = seqgen::HrGridSeq;
    static const Desc d[] = {
        D("Grouping", G::P_GROUPING, 1, 8, 1, F_NUM, 0), D("Velocity", G::P_VEL, 1, 127, 100, F_NUM, 0), D("Transp", G::P_TRANSP, -24, 24, 0, F_SIGN, 0),
        D("Rate", G::P_DIV, 1, 24, 6, F_RATE, 0), D("Start", G::P_START, 1, 8, 1, F_NUM, 0), D("End", G::P_END, 1, 8, 8, F_NUM, 0),
    };
    static const Sec s[] = { { "Grid", 6 } };
    static const Tab t = { 1, 6, s, d };
    return t;
}
inline const Tab& tabStepFollow()
{
    using F = seqgen::HrStepFollow;
    static const Desc d[] = {
        D("Rate", F::P_DIV, 1, 24, 6, F_RATE, 0), D("Velocity", F::P_VEL, 1, 127, 100, F_NUM, 0), D("Octave", F::P_OCTAVE, -2, 2, 0, F_SIGN, 0),
        D("Start", F::P_START, 1, 8, 1, F_NUM, 0), D("End", F::P_END, 1, 8, 8, F_NUM, 0),
    };
    static const Sec s[] = { { "Follow", 5 } };
    static const Tab t = { 1, 5, s, d };
    return t;
}
#undef D

// the table of a sequencer type; nullptr for Off
inline const Tab* tab(uint8_t type)
{
    if (type == TYPE_CHORDARP) return &tabChordArp();
    if (type == TYPE_MONO) return &tabMono();
    if (type == TYPE_PLINKY) return &tabStrings();
    if (!seqlogic::isHrType(type)) return nullptr;
    switch (type - seqlogic::TYPE_HR_FIRST)
    {
        case seqgen::HR_ARP_LINES: return &tabStabs(true);
        case seqgen::HR_CHORD_STABS: return &tabStabs(false);
        case seqgen::HR_ARP_SEQ: return &tabArpSeq();
        case seqgen::HR_CHORD_ARP: return &tabHrChordArp();
        case seqgen::HR_BASS: return &tabBass();
        case seqgen::HR_EUCLID: return &tabEuclid();
        case seqgen::HR_GRIDSEQ: return &tabGridSeq();
        default: return &tabStepFollow();
    }
}
inline const Desc& desc(const Tab& t, uint8_t i) { return t.d[i < t.nTotal ? i : 0]; }
// Delete: the default of a parameter (the Latch default depends on the type)
inline int defaultOf(uint8_t type, const Desc& d) { return d.id == ID_LATCH ? (seqlogic::latchDefault(type) ? 1 : 0) : d.def; }
inline int clampTo(const Desc& d, int v) { return v < d.mn ? d.mn : (v > d.mx ? d.mx : v); }

// jog: one detent changes the value by 1 (4 on the wide ranges, fine = 1); the value stays inside the range
inline int jogValue(const Desc& d, int cur, int delta, bool fine)
{
    int mag = delta < 0 ? -delta : delta;
    if (mag > 3) mag = 3;
    const int unit = (!fine && d.mx - d.mn > 60) ? 4 : 1;
    return clampTo(d, cur + (delta < 0 ? -mag : mag) * unit);
}

inline void valueText(const Desc& d, int v, char* out, size_t n)
{
    v = clampTo(d, v);
    switch (d.fmt)
    {
        case F_SIGN: snprintf(out, n, "%+d", v); break;
        case F_LIST: snprintf(out, n, "%s", lists(d.list)[v - d.mn]); break;
        case F_NOTE: seqnotes::noteName(v, out, n); break;
        case F_PC: snprintf(out, n, "%s", seqgen::hrNoteName(((v % 12) + 12) % 12)); break;
        case F_CHORD: snprintf(out, n, "%s", seqgen::ChordArp::chordTypeName(v)); break;
        case F_SCALE: snprintf(out, n, "%s", seqplinky::scaleDef((uint8_t)v).name); break;
        case F_RATE:
        {
            const char* r = seqgen::hrRateName(v);
            if (r[0] == '?') snprintf(out, n, "%dp", v); // pulses per step without a note name
            else snprintf(out, n, "%s", r);
            break;
        }
        case F_MSPEED: snprintf(out, n, "%s", seqplay::speedName((uint8_t)v)); break;
        default: snprintf(out, n, "%d", v); break;
    }
}

// ---- cursor: Left / Right between the cells of a section, Up / Down between the sections (the column is kept as far as it exists) ----
struct Cursor
{
    uint8_t sec = 0, idx = 0;
    void fix(const Tab& t)
    {
        if (sec >= t.nSec) sec = 0;
        if (idx >= t.sec[sec].n) idx = (uint8_t)(t.sec[sec].n - 1);
    }
    uint8_t first(const Tab& t) const
    {
        uint8_t f = 0;
        for (uint8_t s = 0; s < sec && s < t.nSec; s++) f = (uint8_t)(f + t.sec[s].n);
        return f;
    }
    uint8_t flat(const Tab& t) const { return (uint8_t)(first(t) + idx); }
    bool horizontal(const Tab& t, int dir)
    {
        const Cursor b = *this;
        if (dir < 0 && idx > 0) idx--;
        else if (dir > 0 && idx + 1 < t.sec[sec].n) idx++;
        return !(b.sec == sec && b.idx == idx);
    }
    bool vertical(const Tab& t, int dir)
    {
        const Cursor b = *this;
        if (dir < 0 && sec > 0) sec--;
        else if (dir > 0 && sec + 1 < t.nSec) sec++;
        fix(t);
        return !(b.sec == sec && b.idx == idx);
    }
};

// What the page shows (written by the overlay, drawn by the control)
struct Model
{
    uint8_t on;     // 0 = hidden
    uint8_t type;
    uint8_t track;  // 1..16
    uint8_t sec, idx;
    int8_t v[kMaxParams];
    Model() { memset(this, 0, sizeof(*this)); }
};

struct Palette
{
    uint32_t bg, panel, bar, text, section, accent, onAccent, fill;
};

enum { kPanelY0 = 30, kPanelY1 = 424, kHeadY = 41, kRowY0 = 56, kRowH = 52, kRowStep = 56, kBoxX0 = 8, kBoxX1 = 792, kCellX0 = 112, kCellStep = 96,
       kCellW = 92, kBarW = 80 };

template <class C> void draw(C& c, const Model& m, const Palette& p)
{
    const Tab* tb = tab(m.type);
    if (!m.on || !tb) return;
    char buf[40];
    const int selSec = m.sec < tb->nSec ? m.sec : 0;
    // ---- shapes ----
    c.color(p.bg);
    c.rect(0, kPanelY0, 800, kPanelY1);
    c.color(p.panel);
    for (int s = 0; s < tb->nSec; s++)
        c.rect(kBoxX0, kRowY0 + s * kRowStep, kBoxX1, kRowY0 + s * kRowStep + kRowH);
    c.color(p.accent);
    c.rect(kBoxX0, kRowY0 + selSec * kRowStep, kBoxX0 + 4, kRowY0 + selSec * kRowStep + kRowH);
    c.rect(kCellX0 + m.idx * kCellStep, kRowY0 + selSec * kRowStep + 2, kCellX0 + m.idx * kCellStep + kCellW, kRowY0 + selSec * kRowStep + kRowH - 2);
    uint8_t flat = 0;
    c.color(p.bar);
    for (int s = 0; s < tb->nSec; s++)
        for (int i = 0; i < tb->sec[s].n; i++)
            c.rect(kCellX0 + i * kCellStep + 6, kRowY0 + s * kRowStep + 22, kCellX0 + i * kCellStep + 6 + kBarW, kRowY0 + s * kRowStep + 30);
    for (int pass = 0; pass < 2; pass++) // bar fills: the selected one in the dark colour
    {
        c.color(pass == 0 ? p.fill : p.onAccent);
        flat = 0;
        for (int s = 0; s < tb->nSec; s++)
            for (int i = 0; i < tb->sec[s].n; i++, flat++)
            {
                const bool sel = s == selSec && i == m.idx;
                if (sel != (pass == 1)) continue;
                const Desc& d = desc(*tb, flat);
                const int w = d.mx > d.mn ? (clampTo(d, m.v[flat]) - d.mn) * kBarW / (d.mx - d.mn) : 0;
                if (w > 0)
                    c.rect(kCellX0 + i * kCellStep + 6, kRowY0 + s * kRowStep + 22, kCellX0 + i * kCellStep + 6 + w, kRowY0 + s * kRowStep + 30);
            }
    }
    // ---- texts ----
    c.textPhase();
    c.color(p.text);
    snprintf(buf, sizeof buf, "T%d  %s", m.track, seqplay::typeName(m.type));
    c.text(kBoxX0 + 4, kHeadY, 3, 2, buf);
    c.text(kBoxX1, kHeadY, 1, 2 | 4, "Arrows move, jog edits, Del resets");
    flat = 0;
    for (int s = 0; s < tb->nSec; s++)
        for (int i = 0; i < tb->sec[s].n; i++, flat++)
        {
            if (s == selSec && i == m.idx) continue;
            const Desc& d = desc(*tb, flat);
            const int x = kCellX0 + i * kCellStep + 6, y = kRowY0 + s * kRowStep;
            valueText(d, m.v[flat], buf, sizeof buf);
            c.text(x, y + 10, 3, 2, d.name);
            c.text(x, y + 40, 3, 2, buf);
        }
    c.color(p.section);
    for (int s = 0; s < tb->nSec; s++)
        c.text(kBoxX0 + 12, kRowY0 + s * kRowStep + kRowH / 2, 3, 2, tb->sec[s].name);
    c.color(p.onAccent);
    {
        Cursor cur;
        cur.sec = (uint8_t)selSec;
        cur.idx = m.idx;
        const uint8_t f = cur.flat(*tb);
        const Desc& d = desc(*tb, f);
        const int x = kCellX0 + m.idx * kCellStep + 6, y = kRowY0 + selSec * kRowStep;
        valueText(d, m.v[f], buf, sizeof buf);
        c.text(x, y + 10, 3, 2, d.name);
        c.text(x, y + 40, 3, 2, buf);
    }
    c.finish();
}

} // namespace seqparams
