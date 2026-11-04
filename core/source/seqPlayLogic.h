// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Pure logic of the Play overlay (plan 005 R1): strip paging, track -> view window, the sequencer type list (M3), the Shift legend
// gate (m3), strip texts and the pad legend. No Polyend includes, host-testable (core/seqtest/test_play.cpp).
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

inline const char* typeShort(uint8_t type)
{
    return type == seqlogic::TYPE_CHORDARP ? "CHRD" : type == seqlogic::TYPE_MONO ? "MONO" : "---";
}
inline const char* typeName(uint8_t type)
{
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

// ---- pad legend: what the 12 x 4 pads do for the selected sequencer (plan 001 layouts) ----
enum Zone : uint8_t
{
    Z_NONE = 0,
    Z_NOTE = 1,   // pads that play (chord roots)
    Z_STEP = 2,   // Mono step grid
    Z_PANEL = 3,  // Chord/Arp 4x4 panel (content chosen with the mode pads)
    Z_MODE = 4,   // panel mode pads (TYPES SET GRV PRM)
    Z_ADJ = 5,    // +/- pads (inversion, octave, page, speed, velocity, window)
    Z_ACTION = 6, // one-shot actions (half, tie, clear)
    Z_HOLD = 7    // hold modifiers (LOOP)
};

struct LegendCell
{
    Zone zone;
    bool on;     // the pad that is active now (the visible panel's mode pad)
    char txt[4]; // up to 3 characters, ' ' terminated
};

// What the legend needs to know about the selected sequencer (read from the engine, display only).
struct LegendState
{
    uint8_t type = 0;
    uint8_t panel = 0; // Chord/Arp: 0 TYPES 1 SETTINGS 2 GROOVE 3 PARAM
    uint8_t half = 0;  // Mono: 0 = steps 1-8, 1 = steps 9-16
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

inline LegendCell legendCell(const LegendState& st, uint8_t col, uint8_t row)
{
    LegendCell c;
    c.zone = Z_NONE;
    c.on = false;
    c.txt[0] = 0;
    c.txt[1] = 0;
    c.txt[2] = 0;
    c.txt[3] = 0;
    if (col >= 12 || row >= 4) return c;
    auto set = [&c](Zone z, const char* t) {
        c.zone = z;
        for (int i = 0; i < 3 && t[i]; i++) c.txt[i] = t[i];
    };
    if (st.type == seqlogic::TYPE_CHORDARP)
    {
        if (col <= 5)
        {
            char b[4];
            set(Z_NOTE, rootName(row, col, b));
        }
        else if (col <= 9) set(Z_PANEL, "");
        else
        {
            static const char* const k[4][2] = { { "TYP", "SET" }, { "GRV", "PRM" }, { "I-", "I+" }, { "O-", "O+" } };
            set(row < 2 ? Z_MODE : Z_ADJ, k[row][col - 10]);
            if (row < 2) c.on = st.panel == (uint8_t)(row * 2 + (col - 10)); // TYP SET GRV PRM = panels 0 1 2 3
        }
    }
    else if (st.type == seqlogic::TYPE_MONO)
    {
        if (col < 8)
        {
            char b[4] = { 0, 0, 0, 0 };
            if (row == 0) snprintf(b, sizeof b, "%d", col + 1 + (st.half ? 8 : 0));
            set(Z_STEP, b);
        }
        else
        {
            switch (col * 4 + row)
            {
                case 8 * 4 + 0: set(Z_ACTION, "HLF"); break;
                case 8 * 4 + 1: set(Z_ACTION, "TIE"); break;
                case 8 * 4 + 2: set(Z_ADJ, "W+"); break;
                case 8 * 4 + 3: set(Z_ADJ, "W-"); break;
                case 9 * 4 + 0: set(Z_ADJ, "P-"); break;
                case 9 * 4 + 1: set(Z_ADJ, "S-"); break;
                case 9 * 4 + 2: set(Z_HOLD, "LP"); break;
                case 10 * 4 + 0: set(Z_ADJ, "P+"); break;
                case 10 * 4 + 1: set(Z_ADJ, "S+"); break;
                case 10 * 4 + 2: set(Z_ADJ, "V-"); break;
                case 11 * 4 + 0: set(Z_ACTION, "CLR"); break;
                case 11 * 4 + 2: set(Z_ADJ, "V+"); break;
                default: break;
            }
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

inline const char* legendCaption(uint8_t type)
{
    if (type == seqlogic::TYPE_CHORDARP) return "CHORD/ARP   panel:";
    if (type == seqlogic::TYPE_MONO) return "MONO   window:";
    return "";
}
inline const char* panelName(uint8_t panel)
{
    return panel == 1 ? "SETTINGS" : panel == 2 ? "GROOVE" : panel == 3 ? "PARAM" : "TYPES";
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
