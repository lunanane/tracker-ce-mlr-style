// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Pure logic of the Play overlay (plan 005 R1): strip paging, track -> view window, the sequencer type list (M3), the Shift legend
// gate (m3), strip texts and the pad legend. No Polyend includes, host-testable (core/seqtest/test_play.cpp).
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hr.h"
#include "seqTracksLogic.h"

namespace seqplay {

enum { kStrip = 8, kTracks = 16 };

// ---- strip paging: the 8 screen buttons show tracks 1-8 (page 0) or 9-16 (page 1) ----
inline uint8_t stripFirst(uint8_t page) { return page ? 8 : 0; }
inline uint8_t stripPageOfTrack(uint8_t track) { return track >= 8 ? 1 : 0; }
inline uint8_t stripSlotTrack(uint8_t page, uint8_t button) { return (uint8_t)(stripFirst(page) + (button & 7)); }
// Left / Right: Left = tracks 1-8, Right = tracks 9-16
inline uint8_t stripPageForArrow(bool right) { return right ? 1 : 0; }

// ---- track -> window of the pattern view: blocks of columns (4 in the full view, 8 in the single-parameter view) ----
inline uint8_t viewFirstTrack(uint8_t track, uint8_t cols)
{
    if (cols == 0) cols = 4;
    if (track >= kTracks) track = kTracks - 1;
    uint8_t first = (uint8_t)((track / cols) * cols);
    if (first + cols > kTracks) first = (uint8_t)(kTracks - cols);
    return first;
}
inline bool trackInView(uint8_t track, uint8_t first, uint8_t cols) { return track >= first && track < first + cols; }

// ---- type list (hold the track's screen button + wheel) ----
// M3: the first detent only opens the list on the current type (a nudge of the wheel next to the buttons never changes a running
// track); the following detents move the selection; releasing the button on the type that was there at the start does nothing.
struct TypeList
{
    bool open = false;
    uint8_t sel = 0;
    uint8_t cell = 0; // strip cell (0..7) the list belongs to
    // true when the display changed
    bool wheel(uint8_t cellIdx, uint8_t curType, uint8_t pendingType, int delta)
    {
        if (delta == 0) return false;
        if (!open)
        {
            open = true;
            cell = (uint8_t)(cellIdx & 7);
            sel = (pendingType != 0xFF && pendingType < seqlogic::TYPE_COUNT) ? pendingType : curType;
            return true; // first detent: open only
        }
        uint8_t n = seqlogic::stepTypeSelection(sel, delta);
        bool changed = n != sel;
        sel = n;
        return changed;
    }
    // Button released (or the list closed on purpose): the type to post, or -1 when nothing is to be done.
    // Choosing the current type again while a change is pending cancels the pending change (the engine takes it as such).
    int release(uint8_t curType, uint8_t pendingType)
    {
        if (!open) return -1;
        open = false;
        uint8_t base = (pendingType != 0xFF && pendingType < seqlogic::TYPE_COUNT) ? pendingType : curType;
        if (sel == base) return -1;
        return sel;
    }
    void cancel() { open = false; }
};

// ---- screen buttons: which presses the overlay owns ----
// A screen button pressed while Shift is held is the stock mute button (not ours). A press the overlay took is the overlay's until the
// release, whatever happens in between (Shift pressed later, a popup) - the stock code never sees half a gesture.
struct ScreenHold
{
    enum Kind : uint8_t { PRESS = 0, HOLD = 1, RELEASE = 2 };
    enum Result : uint8_t { PASS = 0, TAKE_PRESS = 1, TAKE_HOLD = 2, TAKE_RELEASE = 3 };
    uint8_t mask = 0;     // owned screen buttons currently down
    int8_t listBtn = -1;  // the button the open type list belongs to
    Result event(uint8_t b, Kind kind, bool routable, bool shift)
    {
        b &= 7;
        uint8_t bit = (uint8_t)(1u << b);
        if (mask & bit)
        {
            if (kind != RELEASE) return TAKE_HOLD; // auto-repeat / hold events of an owned button
            mask &= (uint8_t)~bit; // listBtn stays until the owner of the list closes it (it needs to know the release was its button)
            return TAKE_RELEASE;
        }
        if (!routable || kind != PRESS || shift) return PASS;
        mask |= bit;
        return TAKE_PRESS;
    }
    // the button the wheel belongs to: the open list's, else the lowest held; -1 = none held (the wheel is the stock wheel)
    int wheelButton() const
    {
        if (listBtn >= 0 && (mask & (1u << listBtn))) return listBtn;
        for (int i = 0; i < 8; i++)
            if (mask & (1u << i)) return i;
        return -1;
    }
    void clear() { mask = 0; listBtn = -1; }
};

// ---- Shift legend gate (m3): Shift is also the stock mute gesture; the legend shows only after Shift was held for kDelayMs with
// no screen button pressed in between ----
struct LegendGate
{
    enum { kDelayMs = 250 };
    bool shift = false;
    bool blocked = false;
    uint32_t since = 0;
    void shiftChanged(bool down, uint32_t nowMs)
    {
        if (down == shift) return;
        shift = down;
        blocked = false;
        since = nowMs;
    }
    void screenButton() { if (shift) blocked = true; }
    bool visible(uint32_t nowMs) const { return shift && !blocked && (uint32_t)(nowMs - since) >= (uint32_t)kDelayMs; }
};

// ---- strip texts (two lines per cell, <= 11 characters each: a cell is 98 px wide in the 14 px bold font) ----
enum Tone : uint8_t { TONE_OFF = 0, TONE_ON = 1, TONE_SELECTED = 2, TONE_REC = 3 };

struct StripCell
{
    uint8_t track = 0;    // 0..15
    uint8_t type = 0;     // TYPE_*
    uint8_t pending = 0xFF;
    bool selected = false;
    bool takenOver = false;
    bool rec = false;      // this track is being recorded (armed, punch or loop)
    bool launched = false; // plays a launched Session clip
    bool cut = false;      // plays an MLR (CUT) jam
    const char* hint = nullptr; // hint of the last recording on this track (<= 11 characters), see recHintText
    const char* msg = nullptr;  // transient message (<= 11 characters)
};

// v0.1 recording hints on line 2 of the recorded track's cell (overflow beats MIDI-only beats reduced)
inline const char* recHintText(bool overflow, bool needMidi, bool reduced)
{
    return overflow ? "rec overflow" : needMidi ? "MIDI only" : reduced ? "reduced" : nullptr;
}

// The ported HYPER_RASP scripts (seqgen/hr.h order). "HR Chord/Arp" is HYPER_RASP chord_arp.lua: a different script from grid-composer Chord/Arp.
static const char* const kHrShort[] = { "ARPL", "STAB", "ARSQ", "HRCA", "BASS", "EUCL", "GRID", "STEP" };
static const char* const kHrName[] = { "Arp Lines", "Chord Stabs", "Arp Seq", "HR Chord/Arp", "Bassline Seq", "Euclid Drums", "Grid Seq", "Step Follow" };
static_assert(sizeof(kHrShort) / sizeof(kHrShort[0]) == seqlogic::kHrScripts && sizeof(kHrName) / sizeof(kHrName[0]) == seqlogic::kHrScripts,
              "one name per HYPER_RASP script");

inline const char* typeShort(uint8_t type)
{
    if (seqlogic::isHrType(type)) return kHrShort[type - seqlogic::TYPE_HR_FIRST];
    return type == seqlogic::TYPE_CHORDARP ? "CHRD" : type == seqlogic::TYPE_MONO ? "MONO" : "---";
}
inline const char* typeName(uint8_t type)
{
    if (seqlogic::isHrType(type)) return kHrName[type - seqlogic::TYPE_HR_FIRST];
    return type == seqlogic::TYPE_CHORDARP ? "Chord/Arp" : type == seqlogic::TYPE_MONO ? "Mono" : "Off";
}

// line 1: "6 MONO*"; the selected track in brackets: "[6 MONO*]"
inline void stripLine1(char* out, size_t n, const StripCell& c)
{
    uint8_t type = c.type < seqlogic::TYPE_COUNT ? c.type : 0;
    snprintf(out, n, "%s%d %s%s%s", c.selected ? "[" : "", c.track + 1, typeShort(type), c.takenOver ? "*" : "", c.selected ? "]" : "");
}

// line 2: the state, by priority (the most surprising first); a launched clip / MLR jam sounds different from the drawn pattern (M7)
inline void stripLine2(char* out, size_t n, const StripCell& c)
{
    uint8_t type = c.type < seqlogic::TYPE_COUNT ? c.type : 0;
    if (c.msg && c.msg[0]) snprintf(out, n, "%s", c.msg);
    else if (c.rec) snprintf(out, n, "REC");
    else if (c.hint && c.hint[0]) snprintf(out, n, "%s", c.hint);
    else if (c.pending != 0xFF && c.pending < seqlogic::TYPE_COUNT) snprintf(out, n, "next: %s", typeShort(c.pending));
    else if (c.launched) snprintf(out, n, "clip");
    else if (c.cut) snprintf(out, n, "cut");
    else if (type == seqlogic::TYPE_OFF) snprintf(out, n, "pattern");
    else if (c.takenOver) snprintf(out, n, "live");
    else snprintf(out, n, "ready");
}

inline Tone stripTone(const StripCell& c)
{
    if (c.rec) return TONE_REC;
    if (c.selected) return TONE_SELECTED;
    return c.type == seqlogic::TYPE_OFF ? TONE_OFF : TONE_ON;
}

// ---- pad legend: what the 12 x 4 pads do for the selected sequencer (plan 008 C; the pad maps live in seqgen/chordarp.cpp / mono.cpp) ----
// Real words, at most 6 characters per cell (a cell is 60 px wide in the 14 px bold font), ASCII only. A two-line caption names the
// page / view that is shown and says how to play it.
enum Zone : uint8_t
{
    Z_NONE = 0,
    Z_NOTE = 1,   // pads that play (chord roots)
    Z_STEP = 2,   // Mono step grid
    Z_PANEL = 3,  // selectors: chord type, Chord/Arp, speed, direction, groove, velocity, lane, range, size, pages, speeds
    Z_MODE = 4,   // page pad and page tabs, back to the notes
    Z_ADJ = 5,    // +/- pads (inversion, octave, swing, transpose, window, page, speed, velocity)
    Z_ACTION = 6, // one-shot actions (tie, mute, clear, gate, param)
    Z_HOLD = 7    // hold modifiers (Root, Loop, Locked)
};

struct LegendCell
{
    Zone zone;
    bool on;     // the pad that is active now (page tab, current speed / page / velocity, tie)
    char txt[8]; // up to 6 characters
};

// The legend of a HYPER_RASP script, as the script reports it (seqgen::HrScript::legend / title / help); filled by the overlay.
// Zones and the "on" flag are the HrZone / HrCell values of seqgen/hr.h (same numbers as Zone below).
struct HrSnap
{
    char title[96];
    const char* help; // static text of the script
    uint8_t zone[48];
    uint8_t on[48];
    char txt[48][7];
};

static_assert((int)seqgen::HZ_NONE == (int)Z_NONE && (int)seqgen::HZ_NOTE == (int)Z_NOTE && (int)seqgen::HZ_STEP == (int)Z_STEP &&
                  (int)seqgen::HZ_PANEL == (int)Z_PANEL && (int)seqgen::HZ_MODE == (int)Z_MODE && (int)seqgen::HZ_ADJ == (int)Z_ADJ &&
                  (int)seqgen::HZ_ACTION == (int)Z_ACTION && (int)seqgen::HZ_HOLD == (int)Z_HOLD,
              "seqgen::HrZone and seqplay::Zone must use the same numbers");

// What the legend needs to know about the selected sequencer (read from the engine, display only).
struct LegendState
{
    const HrSnap* hr = nullptr; // HYPER_RASP types: their whole legend
    uint8_t type = 0;
    uint8_t page = 0;   // Chord/Arp: 0 Play 1 Groove 2 Feel 3 Setup
    uint8_t pon = 0;    // Chord/Arp: parametric groove on (the Groove page block shows tiers / archetypes)
    uint8_t half = 0;   // Mono: 0 = steps 1-8, 1 = steps 9-16
    uint8_t menu = 0;   // Mono: the menu view is shown
    uint8_t ep = 1;     // Mono: edit page 1..8
    uint8_t np = 1;     // Mono: page count 1..8
    uint8_t speed = 8;  // Mono: speed 1..12
    uint8_t vel = 1;    // Mono: velocity pattern 1..72
    uint8_t tie = 0;    // Mono: tie
    int8_t win = 0;     // Mono: degree window offset
};

// Chord/Arp root pads (c31 camelot grid, copy of kCam in seqgen/chordarp.cpp): pad row = grid x - 1, pad column = grid y - 1.
// Odd grid rows (second and fourth pad row) are the minor roots.
inline const char* rootName(uint8_t row, uint8_t col, char* buf4)
{
    static const uint8_t kCam[4][6] = { { 64, 69, 62, 67, 60, 65 }, { 61, 66, 71, 64, 69, 62 },
                                        { 71, 66, 61, 68, 63, 70 }, { 68, 63, 70, 65, 60, 67 } };
    static const char* const kPc[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (row >= 4 || col >= 6) { buf4[0] = 0; return buf4; }
    const char* n = kPc[kCam[row][col] % 12];
    snprintf(buf4, 4, "%s%s", n, (row & 1) ? "m" : "");
    return buf4;
}

// Mono speed 1..12 (seqgen kSpd = pulses per step 96 48 32 24 16 12 8 6 4 3 2 1)
inline const char* speedName(uint8_t speed)
{
    static const char* const k[12] = { "1/1", "1/2", "1/2T", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64T" };
    return (speed >= 1 && speed <= 12) ? k[speed - 1] : "?";
}

inline const char* pageName(uint8_t page)
{
    return page == 1 ? "Groove" : page == 2 ? "Feel" : page == 3 ? "Setup" : "Play";
}
inline const char* panelName(uint8_t page) { return pageName(page); } // old name

inline LegendCell legendCell(const LegendState& st, uint8_t col, uint8_t row)
{
    LegendCell c;
    c.zone = Z_NONE;
    c.on = false;
    for (int i = 0; i < 8; i++) c.txt[i] = 0;
    if (col >= 12 || row >= 4) return c;
    if (seqlogic::isHrType(st.type))
    {
        if (!st.hr) return c;
        const int i = row * 12 + col;
        c.zone = (Zone)st.hr->zone[i];
        c.on = st.hr->on[i] != 0;
        for (int k = 0; k < 6; k++) c.txt[k] = st.hr->txt[i][k];
        return c;
    }
    auto set = [&c](Zone z, const char* t) {
        c.zone = z;
        for (int i = 0; i < 6 && t[i]; i++) c.txt[i] = t[i];
    };
    if (st.type == seqlogic::TYPE_CHORDARP)
    {
        static const char* const kTypes[8] = { "Root", "Sus2", "Triad", "Sus4", "5th", "6th", "7th", "9th" };
        static const char* const kSpeeds[4] = { "1/8", "1/8T", "1/16", "1/16T" };
        static const char* const kDirs[4] = { "Up", "Down", "UpDn", "Zigzag" };
        static const char* const kTabs[4] = { "Play", "Groove", "Feel", "Setup" };
        if (col <= 5)
        {
            char b[4];
            set(Z_NOTE, rootName(row, col, b));
            return c;
        }
        const uint8_t page = st.page > 3 ? 0 : st.page;
        if (col == 11 && (page != 0 || row == 0)) // page pad / tabs
        {
            if (page == 0) set(Z_MODE, "Page");
            else
            {
                set(Z_MODE, kTabs[row]);
                c.on = row == page;
            }
            return c;
        }
        char b[8] = { 0 };
        switch (page)
        {
            case 0: // Play
                if (col <= 7) set(col == 6 && row == 0 ? Z_HOLD : Z_PANEL, kTypes[(col - 6) * 4 + row]);
                else if (row == 0) set(Z_PANEL, col == 8 ? "Chord" : col == 9 ? "Arp" : "ArpOct");
                else if (row == 1) set(Z_PANEL, kSpeeds[col - 8]);
                else if (row == 2) set(Z_PANEL, kDirs[col - 8]);
                else
                {
                    static const char* const k[4] = { "Inv-", "Inv+", "Oct-", "Oct+" };
                    set(Z_ADJ, k[col - 8]);
                }
                break;
            case 1: // Groove
                if (col <= 9)
                {
                    if (st.pon && row < 3) snprintf(b, sizeof b, "%d", col - 6);
                    else if (st.pon) snprintf(b, sizeof b, "A%d", col - 5);
                    else snprintf(b, sizeof b, "G%d", row * 4 + col - 5);
                    set(Z_PANEL, b);
                }
                else
                {
                    static const char* const k[4] = { "Param", "Gate", "Swg-", "Swg+" };
                    set(row < 2 ? Z_ACTION : Z_ADJ, k[row]);
                }
                break;
            case 2: // Feel
                if (col == 10)
                {
                    snprintf(b, sizeof b, "Size%d", row == 0 ? 5 : row == 1 ? 4 : row == 2 ? 3 : 2);
                    set(Z_PANEL, b);
                }
                else
                {
                    if (row < 2) snprintf(b, sizeof b, "Vel %d", row * 4 + col - 5);
                    else if (row == 2) snprintf(b, sizeof b, "Lane%d", col - 5);
                    else snprintf(b, sizeof b, "Rng %d", col - 5);
                    set(Z_PANEL, b);
                }
                break;
            default: // Setup
                if (row == 0 && col <= 8) set(col == 7 ? Z_HOLD : Z_MODE, col == 6 ? "Key" : col == 7 ? "Locked" : "Grid");
                else if (row == 1 && col <= 8) set(Z_ADJ, col == 6 ? "Tran-" : col == 7 ? "Tran+" : "Tran 0");
                else if (row == 2 && col == 6) set(Z_ACTION, "Mute");
                else if (row == 2 && col == 7) set(Z_ACTION, "Off");
                break;
        }
    }
    else if (st.type == seqlogic::TYPE_MONO)
    {
        if (col == 11 && row == 3)
        {
            set(Z_MODE, st.menu ? "Notes" : "Menu");
            return c;
        }
        char b[8] = { 0 };
        if (!st.menu)
        {
            if (col < 8)
            {
                if (row == 0) snprintf(b, sizeof b, "%d", col + 1 + (st.half ? 8 : 0));
                set(Z_STEP, b);
                return c;
            }
            switch (col * 4 + row)
            {
                case 8 * 4 + 0: set(Z_ADJ, "Win up"); break;
                case 8 * 4 + 1: set(Z_ADJ, "Win dn"); break;
                case 8 * 4 + 2: set(Z_ACTION, "Half"); c.on = st.half != 0; break;
                case 8 * 4 + 3: set(Z_HOLD, "Loop"); break;
                case 9 * 4 + 0: set(Z_ADJ, "Page-"); break;
                case 9 * 4 + 1: set(Z_ADJ, "Page+"); break;
                case 9 * 4 + 2: set(Z_ACTION, "Tie"); c.on = st.tie != 0; break;
                case 10 * 4 + 0: set(Z_ADJ, "Slower"); break;
                case 10 * 4 + 1: set(Z_ADJ, "Faster"); break;
                case 10 * 4 + 2: set(Z_ADJ, "Vel-"); break;
                case 11 * 4 + 2: set(Z_ADJ, "Vel+"); break;
                default: break;
            }
            return c;
        }
        const uint8_t velLow = (uint8_t)((st.vel ? st.vel - 1 : 0) % 8 + 1);
        switch (row)
        {
            case 0:
                set(Z_PANEL, speedName((uint8_t)(col + 1)));
                c.on = st.speed == col + 1;
                break;
            case 1:
                if (col < 8)
                {
                    snprintf(b, sizeof b, "Pg %d", col + 1);
                    set(Z_PANEL, b);
                    c.on = st.ep == col + 1;
                }
                else if (col == 8) { set(Z_ACTION, "Tie"); c.on = st.tie != 0; }
                else if (col == 10) set(Z_ADJ, "Win up");
                else if (col == 11) set(Z_ADJ, "Win dn");
                break;
            case 2:
                if (col < 8) snprintf(b, sizeof b, "Vel %d", col + 1);
                else snprintf(b, sizeof b, "%d+1", col - 7);
                set(Z_PANEL, b);
                c.on = (st.vel == col + 1) || (st.vel <= 8 && velLow == col + 1);
                break;
            default:
                if (col == 0) set(Z_ADJ, "Tran-");
                else if (col == 1) set(Z_ADJ, "Tran+");
                else if (col == 2) set(Z_ACTION, "Mute");
                else if (col == 7) set(Z_ACTION, "Clear");
                break;
        }
    }
    return c;
}
inline LegendCell legendCell(uint8_t type, uint8_t col, uint8_t row)
{
    LegendState st;
    st.type = type;
    return legendCell(st, col, row);
}

// The legend of a HYPER_RASP script: its title, help and the 48 cells, as the script reports them
inline void fillHrSnap(HrSnap& s, const seqgen::HrScript& script)
{
    memset(&s, 0, sizeof(s));
    script.title(s.title, sizeof(s.title));
    s.help = script.help();
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 12; c++)
        {
            seqgen::HrCell cell;
            script.legend(c, r, cell);
            const int i = r * 12 + c;
            s.zone[i] = cell.zone;
            s.on[i] = cell.on ? 1 : 0;
            for (int k = 0; k < 6; k++)
                s.txt[i][k] = cell.txt[k];
        }
}

// Short heading, kept for callers that only know the type: empty for an Off track
inline const char* legendCaption(uint8_t type)
{
    if (seqlogic::isHrType(type)) return typeName(type);
    if (type == seqlogic::TYPE_CHORDARP) return "CHORD/ARP";
    if (type == seqlogic::TYPE_MONO) return "MONO";
    return "";
}

// Caption line 1: what is shown and the state ("CHORD/ARP  [Play] Groove Feel Setup", "MONO  Notes 1-8  Page 1/2  Speed 1/16  Vel 1  Tie off")
inline void legendTitle(const LegendState& st, char* out, size_t n)
{
    if (st.type == seqlogic::TYPE_CHORDARP)
    {
        const uint8_t p = st.page > 3 ? 0 : st.page;
        snprintf(out, n, "CHORD/ARP   Page:  %sPlay%s  %sGroove%s  %sFeel%s  %sSetup%s", p == 0 ? "[" : "", p == 0 ? "]" : "", p == 1 ? "[" : "",
                 p == 1 ? "]" : "", p == 2 ? "[" : "", p == 2 ? "]" : "", p == 3 ? "[" : "", p == 3 ? "]" : "");
    }
    else if (st.type == seqlogic::TYPE_MONO)
    {
        char view[16];
        if (st.menu) snprintf(view, sizeof view, "Menu");
        else snprintf(view, sizeof view, "Notes %d-%d", st.half ? 9 : 1, st.half ? 16 : 8);
        snprintf(out, n, "MONO   %s   Page %d/%d   Speed %s   Vel %d   Tie %s   Window %+d", view, (int)st.ep, (int)st.np, speedName(st.speed),
                 (int)((st.vel ? st.vel - 1 : 0) % 8 + 1), st.tie ? "on" : "off", (int)st.win);
    }
    else if (seqlogic::isHrType(st.type) && st.hr)
        snprintf(out, n, "%s", st.hr->title);
    else if (n)
        out[0] = 0;
}

// Caption line 2: how to play what is shown (<= 88 characters)
inline const char* legendHelp(const LegendState& st)
{
    if (st.type == seqlogic::TYPE_CHORDARP)
    {
        switch (st.page)
        {
            case 1: return "Groove: rhythm. Param: random groove on/off. Gate: note length. Swg: swing.";
            case 2: return "Vel: loudness (2 pads = blend). Lane: accent gates. Rng: arp octaves. Size: chord notes.";
            case 3: return "Key: follows root. Locked: hold + tap a root. Grid: scale pads. Tran: shift. Off: silent.";
            default: return "Hold a root. Type: chord shape (hold Root + type = more). 2 fingers on Speed/Dir: more.";
        }
    }
    if (st.type == seqlogic::TYPE_MONO)
    {
        if (st.menu) return "Speed: tap one. Pg: tap = edit page, hold Pg 1 + Pg N = N pages. Vel: loudness.";
        return "Tap a cell: note on/off. Hold Loop + tap first and last step = loop. Half: steps 9-16.";
    }
    if (seqlogic::isHrType(st.type) && st.hr && st.hr->help) return st.hr->help;
    return "";
}

// The side of the strip a selected track that is not on the shown strip page sits on: 0 = on the page, 1 = higher (Right), 2 = lower
inline uint8_t edgeSide(uint8_t selectedTrack, uint8_t stripPage)
{
    uint8_t p = stripPageOfTrack(selectedTrack);
    return p == stripPage ? 0 : (p > stripPage ? 1 : 2);
}

// ---- what a view change does to the overlay's state (plan 005 M5 survival table, Play-mode rows) ----
enum Event : uint8_t { EV_LEAVE = 0, EV_MODULE_SWITCH = 1, EV_STOP = 2, EV_POPUP = 3 };
struct Plan
{
    bool releasePads;   // held pads are released (note-off at the next step)
    bool endRecording;  // a running recording ends (one undo step)
    bool closeList;     // an open type list closes without applying
    bool clearHold;     // held screen-button gestures are forgotten
    bool swallowLate;   // the physical release of a pad released here is swallowed
    bool sequencersRun; // HOLD / takeover / launched clips keep running (nothing here stops them)
};
inline Plan planFor(Event e)
{
    Plan p = { true, true, true, true, true, true };
    if (e == EV_POPUP) p = { false, false, true, false, false, true }; // a popup only closes the list; pads and recording go on
    return p;
}

// Pads released by a plan: their physical release arrives later and must not reach the stock editor or release a newer press.
struct LateRelease
{
    uint64_t bits = 0;
    void released(uint8_t pad) { if (pad < 48) bits |= (uint64_t)1 << pad; }
    void pressed(uint8_t pad) { if (pad < 48) bits &= ~((uint64_t)1 << pad); }
    bool swallow(uint8_t pad) // true = the release is ours (consumed)
    {
        if (pad >= 48 || !((bits >> pad) & 1u)) return false;
        bits &= ~((uint64_t)1 << pad);
        return true;
    }
};

} // namespace seqplay
