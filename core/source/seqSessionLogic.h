// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Session view / clip launcher: pure logic (no Polyend includes), host-tested in core/seqtest.
// Threads: the Launcher is written by the main loop through a ring buffer (post*), consumed in the audio
// context (drain / onStep). The getters are safe to call from the main loop (single bytes / masks).
#pragma once
#include "seqClipsLogic.h"
#include <stdint.h>

namespace seqsession {

enum { kTracks = 16, kWinRows = 4, kPatternMax = 255, kCols = 12, kRows = 4, kPads = 48, kVisRows = 12 };

// ---- launch quantise -------------------------------------------------------------------------
// Q_BAR / Q_PATTERN / Q_NOW are the global setting (screen button 2 taps through them). Q_CLIPEND is never global: it is the
// quantise of a follow action (plan 005 R2, Play 1x): the command applies when the clip that plays on the track has run once.
enum Quant : uint8_t { Q_BAR = 0, Q_PATTERN = 1, Q_NOW = 2, Q_COUNT = 3, Q_CLIPEND = 4 };
enum { kQuantGlobal = 0xFF }; // command quantise byte: "use the global setting"

inline Quant nextQuant(Quant q) { return (Quant)((q + 1) % Q_COUNT); }
inline const char* quantName(Quant q) { return q == Q_BAR ? "next bar" : q == Q_PATTERN ? "pattern end" : q == Q_CLIPEND ? "clip end" : "immediate"; }
inline const char* quantShort(Quant q) { return q == Q_BAR ? "bar" : q == Q_PATTERN ? "pattern" : q == Q_CLIPEND ? "clip end" : "now"; }

// globalPos = position (0-based step) of the global playhead. A bar = 16 steps; pattern end = the wrap to 0.
// Called at a step boundary, so Q_NOW fires at the next step of the track (never mid-step).
inline bool quantHit(Quant q, uint16_t globalPos)
{
    switch (q)
    {
        case Q_BAR: return (globalPos & 15u) == 0;
        case Q_PATTERN: return globalPos == 0;
        default: return true;
    }
}

// Steps until the next launch point of a global quantise, counted from the current step `pos` of a pattern of `len` steps
// (>= 1: the current step is over). The wrap of the pattern is a launch point of every quantise.
inline uint16_t stepsToQuant(Quant q, uint16_t pos, uint16_t len)
{
    if (len == 0) len = 1;
    if (pos >= len) pos = 0;
    uint16_t toEnd = (uint16_t)(len - pos);
    if (q == Q_NOW) return 1;
    if (q == Q_PATTERN) return toEnd;
    uint16_t toBar = (uint16_t)(16 - (pos & 15u));
    return toBar < toEnd ? toBar : toEnd;
}

// ---- scene quantise (plan 007 B2) -------------------------------------------------------------------
// A scene (pattern switch) with the quantise bar / now does not wait for the pattern end: its pattern is read into the next-pattern
// buffer at once (main loop, asynchronous), and the audio side swaps it in at a step boundary where quantHit() says so, once the
// read is done. At every step boundary (pos = the step that plays next, len = steps of the playing pattern) it decides:
//  - SCENE_SWAP: the swap happens now; the tracks go on at pos mod their new length (the phase stays musical);
//  - SCENE_ARM_STOCK: no hit before the pattern end, and the coming step is the last one: the stock switch is armed, it loads the pattern
//    again on the last step and swaps at the pattern end exactly as before (this also keeps the Song page's slot logic in charge there).
//    This happens whether or not the early read finished, so the pattern end never depends on the SD timing and a scene is never dropped;
//  - SCENE_WAIT: nothing yet (also: the read is not finished - the next boundary after it is, decides).
enum SceneAct : uint8_t { SCENE_WAIT = 0, SCENE_ARM_STOCK = 1, SCENE_SWAP = 2 };
inline SceneAct sceneStepAction(Quant q, uint16_t pos, uint16_t len, bool ready, bool armedByStock)
{
    if (armedByStock || q == Q_PATTERN || q == Q_CLIPEND)
        return SCENE_WAIT;
    if (len == 0) len = 1;
    if (ready && quantHit(q, pos))
        return SCENE_SWAP;
    if ((uint16_t)(pos + 1) >= len)
        return SCENE_ARM_STOCK;
    return SCENE_WAIT;
}
// Position of the tracks after the swap: the global position inside the new length of the track.
inline uint16_t scenePhase(uint16_t pos, uint16_t newLen) { return newLen ? (uint16_t)(pos % newLen) : 0; }

// ---- pattern window (4 rows) ------------------------------------------------------------------
struct Window
{
    uint8_t first = 1; // pattern shown in row 1
    static uint8_t clampFirst(int v)
    {
        if (v < 1) v = 1;
        if (v > kPatternMax - (kWinRows - 1)) v = kPatternMax - (kWinRows - 1);
        return (uint8_t)v;
    }
    // returns true when the window moved
    bool scroll(int delta)
    {
        uint8_t n = clampFirst((int)first + delta);
        bool moved = n != first;
        first = n;
        return moved;
    }
    uint8_t pattern(uint8_t row) const { return (uint8_t)(first + row); }
};

// Jog wheel: one detent = one pattern, Shift = a whole window.
inline int jogToScroll(int16_t value, bool shift)
{
    int d = value > 0 ? 1 : (value < 0 ? -1 : 0);
    return shift ? d * kWinRows : d;
}

// Arrow keys: Up / Down move the pad window one pattern (Shift: a whole window), clamped like the jog wheel.
inline int arrowToScroll(bool down, bool shift) { return (down ? 1 : -1) * (shift ? (int)kWinRows : 1); }

// Left / Right: the group of tracks (8 at a time: 1-8, 9-16). Clamped, no wrap.
inline uint8_t stepGroup(uint8_t first, bool right, uint8_t groupSize)
{
    if (groupSize == 0 || groupSize > kTracks) return first;
    int f = (int)first - (int)(first % groupSize);
    f += right ? (int)groupSize : -(int)groupSize;
    if (f < 0) f = 0;
    if (f > kTracks - (int)groupSize) f = kTracks - (int)groupSize;
    return (uint8_t)f;
}

// The grid shows kVisRows patterns; the pad window (kWinRows rows from `winFirst`) must stay inside. `top` = first shown pattern.
inline uint8_t viewTop(uint8_t top, uint8_t winFirst)
{
    int t = top;
    int bottom = (int)winFirst + kWinRows - 1;
    if ((int)winFirst < t) t = winFirst;
    if (bottom > t + kVisRows - 1) t = bottom - (kVisRows - 1);
    if (t > kPatternMax - kVisRows + 1) t = kPatternMax - kVisRows + 1;
    if (t < 1) t = 1;
    return (uint8_t)t;
}

// ---- clip pad states and LEDs -----------------------------------------------------------------
// CS_FOLLOW = a follow action (Play 1x) is armed for this clip: shown steady, not blinking (nothing the performer did).
enum ClipState : uint8_t { CS_EMPTY = 0, CS_NOTES = 1, CS_QUEUED = 2, CS_PLAYING = 3, CS_FOLLOW = 4 };

inline ClipState clipState(bool hasNotes, bool playing, bool queued, bool follow = false)
{
    if (queued) return follow ? CS_FOLLOW : CS_QUEUED;
    if (playing) return CS_PLAYING;
    return hasNotes ? CS_NOTES : CS_EMPTY;
}
// 0..15. bright = playing, blinking = queued, medium = follow armed, dim = has notes, off = empty.
inline uint8_t clipLevel(ClipState s, bool blinkOn)
{
    switch (s)
    {
        case CS_PLAYING: return 15;
        case CS_QUEUED: return blinkOn ? 15 : 0;
        case CS_FOLLOW: return 8;
        case CS_NOTES: return 3;
        default: return 0;
    }
}
// An empty pad of a track that plays a clip is dimly lit: pressing it stops the track (plan 005 m9, the rule is visible).
inline uint8_t emptyPadLevel(bool trackSounds) { return trackSounds ? 1 : 0; }
inline bool blinkPhase(uint32_t ms) { return ((ms / 150u) & 1u) == 0; } // ~3.3 Hz

// ---- pad map (0-based col 0..11, row 0..3; pad index = row * 12 + col) ---------------------------
// Plan 005 R2: columns 1-8 = clips of the 8 tracks of the window, column 9 = empty gap, column 10 = scene launch, columns 11-12
// empty. STOP / QUANT / MODE are screen buttons now (hold 3 + pad = stop, tap 2 = quantise, hold 1 = play mode).
enum PadKind : uint8_t { PK_NONE = 0, PK_CLIP, PK_SCENE };

struct PadAction
{
    PadKind kind;
    uint8_t col, row;
};

inline PadAction mapPad(uint8_t col, uint8_t row)
{
    PadAction a = { PK_NONE, col, row };
    if (col < 8) a.kind = PK_CLIP;
    else if (col == 9) a.kind = PK_SCENE;
    return a;
}

// What a pad press does. Hold screen button 1 (mode): pads only focus a clip (no accidental launch while the mode is set).
// Hold button 3 (stop): clip pad = stop that track, scene pad = stop all. An empty clip pad stops its track.
enum PadOp : uint8_t { OP_NONE = 0, OP_FOCUS, OP_STOP_TRACK, OP_LAUNCH, OP_STOP_ALL, OP_SCENE };
inline PadOp padOp(PadKind k, bool modeHeld, bool stopHeld, bool hasNotes)
{
    if (k == PK_CLIP)
    {
        if (modeHeld) return OP_FOCUS;
        if (stopHeld || !hasNotes) return OP_STOP_TRACK;
        return OP_LAUNCH;
    }
    if (k == PK_SCENE)
    {
        if (modeHeld) return OP_FOCUS;
        return stopHeld ? OP_STOP_ALL : OP_SCENE;
    }
    return OP_NONE;
}

// Screen buttons of the Session view (0-based): 0..3 choose the group, 7 switches the layer (kButtonSwitch).
// A layer shows groupSize tracks: 8 -> buttons 0, 1 = tracks 1-8, 9-16; 4 -> buttons 0..3 = 1-4, 5-8, 9-12, 13-16.
// Returns the first track of the group, or 0xFF when the button does not choose a group.
inline uint8_t groupFromButton(uint8_t button, uint8_t groupSize)
{
    if (groupSize == 0 || button >= kTracks / groupSize) return 0xFF;
    return (uint8_t)(button * groupSize);
}
// Keep a group valid after the group size changed (CLIPS 8 <-> CUT 4).
inline uint8_t alignGroup(uint8_t first, uint8_t groupSize) { return (uint8_t)(first - first % groupSize); }
// Host layout (plan 005 decision): 0-3 belong to the layer (groups or gestures), 5 (button 6) to the active layer too (CUT: SYNC ALL),
// 6 (button 7) is an empty gap, 7 (button 8) switches CLIPS <-> CUT and is labelled with the other layer.
enum { kButtonLayer = 5, kButtonGap = 6, kButtonSwitch = 7 };
// CLIPS layer screen buttons (0-based): 0 = MODE (hold + wheel), 1 = QUANT (tap cycles), 2 = STOP (hold + pad); 3 and 4 are empty.
enum { kBtnMode = 0, kBtnQuant = 1, kBtnStop = 2 };

// ---- play mode of a clip (plan 005 R2: per clip = pattern x track, stored in seqclips.bin) --------------------------
// Wheel while screen button 1 is held: one detent = one mode step, clamped (Loop <-> Play 1x), no wrap.
inline uint8_t stepClipMode(uint8_t mode, int delta)
{
    int m = (int)mode + (delta > 0 ? 1 : (delta < 0 ? -1 : 0));
    if (m < 0) m = 0;
    if (m > (int)seqclips::CLIP_MODE_COUNT - 1) m = (int)seqclips::CLIP_MODE_COUNT - 1;
    return (uint8_t)m;
}
inline const char* clipModeName(uint8_t mode) { return mode == seqclips::CLIP_PLAY1X ? "PLAY 1X" : "LOOP"; }

// ---- follow action (Play 1x): what the clip does when it has played once ------------------------------------------
// The next row down is the pattern after this one. A track without content there (or no row left) stops.
struct FollowTarget
{
    bool launch;
    uint8_t pattern;
};
inline FollowTarget followTarget(uint8_t pattern, bool nextHasContent)
{
    FollowTarget f = { false, 0 };
    if (pattern >= 1 && pattern < kPatternMax && nextHasContent)
    {
        f.launch = true;
        f.pattern = (uint8_t)(pattern + 1);
    }
    return f;
}

// Remembers for which instance of a playing clip the follow action was queued, so it is queued once: a tap on the playing clip
// cancels it and it must not come back. An instance = (pattern, how often the track applied a command, pattern-switch epoch).
class FollowArms
{
  public:
    FollowArms() { reset(); }
    void reset()
    {
        for (int i = 0; i < kTracks; i++)
            key_[i] = 0;
    }
    bool needs(uint8_t t, uint8_t pattern, uint8_t applied, uint8_t epoch) const { return t < kTracks && key_[t] != make(pattern, applied, epoch); }
    void mark(uint8_t t, uint8_t pattern, uint8_t applied, uint8_t epoch)
    {
        if (t < kTracks) key_[t] = make(pattern, applied, epoch);
    }

  private:
    static uint32_t make(uint8_t pattern, uint8_t applied, uint8_t epoch) { return 0x1000000u | ((uint32_t)epoch << 16) | ((uint32_t)applied << 8) | pattern; }
    uint32_t key_[kTracks];
};

// ---- grid screen (clipGrid.*): geometry and the data the controls draw ----------------------------------------------
// 800 x 480 screen, content y 29-422 (stock title bar above, screen-button labels below). 12 pattern rows x 16 tracks, a gap
// after track 8, the scene cell after track 16, an info column on the right. All numbers in pixels.
namespace clipgrid {
enum
{
    kCellW = 27,
    kCellH = 23,
    kPitch = 31,
    kRowH = 27,
    kX0 = 44,
    kBlockGap = 16,
    kY0 = 60,
    kSceneW = 32,
    kInfoX = 616,
    kRowNumX = 4
};
inline int16_t colX(uint8_t track) { return (int16_t)(kX0 + track * kPitch + (track >= 8 ? kBlockGap : 0)); }
inline int16_t sceneX() { return (int16_t)(colX(15) + kCellW + kBlockGap); }
inline int16_t rowY(uint8_t visRow) { return (int16_t)(kY0 + visRow * kRowH); }
// progress of a clip inside the cell: position of the track (step, micro step 1..48) in a clip of `len` steps
inline uint8_t progressPx(uint8_t pos, uint8_t uStep, uint16_t len)
{
    if (len == 0) return 0;
    uint32_t span = (uint32_t)len * 48u;
    uint32_t at = (uint32_t)pos * 48u + (uStep ? uStep - 1u : 0u);
    uint32_t px = at * kCellW / span;
    return (uint8_t)(px > (uint32_t)kCellW ? (uint32_t)kCellW : px);
}

enum Cell : uint8_t { CELL_EMPTY = 0, CELL_NOTES, CELL_PLAYING, CELL_QUEUED, CELL_FOLLOW };
enum { F_ONEX = 1, F_END = 2, F_STOPMARK = 4 }; // 1x clip / 1x clip whose next row is empty (the track stops) / empty cell of a sounding track
enum MarkKind : uint8_t { MK_NONE = 0, MK_BLINK = 1, MK_FOLLOW = 2 };
enum Msg : uint8_t { MSG_NONE = 0, MSG_BUSY = 1, MSG_UNDO_CLEARED = 2, MSG_STOP_TO_SAVE = 3 };
inline const char* msgText(uint8_t m)
{
    return m == MSG_BUSY ? "Busy - try again" : m == MSG_UNDO_CLEARED ? "Undo history cleared (launched clips ended)" : m == MSG_STOP_TO_SAVE ? "Unsaved edits: stop and save first" : "";
}

// The static part of the screen (redrawn when it changes).
struct View
{
    uint8_t top;          // pattern of the first visible row
    uint8_t winFirst;     // pad window: first pattern (row) ...
    uint8_t winTrack;     // ... and first track
    uint8_t focusPattern; // focused clip (0 = none)
    uint8_t focusTrack;
    uint8_t focusMode;    // play mode of the focused clip
    uint8_t quant;        // Quant
    uint8_t focusScene;   // the focused row's scene is flagged "play next" (plan 006; the slot of the old LIVE line)
    uint8_t modeHeld;     // screen button 1 held
    uint8_t stopHeld;     // screen button 3 held
    uint8_t msg;          // MSG_* status line
    uint8_t cell[kVisRows][kTracks];
    uint8_t flags[kVisRows][kTracks];
    uint8_t scene[kVisRows];      // Cell of the scene cell
    uint8_t sceneFlags[kVisRows]; // F_ONEX: the scene is flagged "play next" (plan 006; before: every clip of the row plays once)
};

// The animated part (small, redrawn at most once per step and for the blink).
struct Marks
{
    uint8_t playRow[kTracks]; // visible row of the playing clip, 0xFF = none
    uint8_t prog[kTracks];    // progress in px
    uint8_t qRow[kTracks];    // visible row of the queued clip, 0xFF = none
    uint8_t qKind[kTracks];   // MarkKind
    uint8_t sceneRow;         // queued scene (visible row) or 0xFF
    uint8_t songRow;          // pattern the transport plays (visible row) or 0xFF
    uint8_t blink;            // blink phase (0 when nothing blinks)
    uint8_t countdown;        // steps to the next launch point (clamped to 254), 0xFF = nothing queued
};

// ---- display list budget (software counter) ----------------------------------------------------------------------------
// The display driver offers no usable size while a control is being drawn (reading the list offset in the middle of a
// co-processor burst breaks it), so the cost is summed from the View before anything is drawn. Bytes of the display list:
// a rect = 2 vertices = 8, an outline = 5 vertices + begin / end = 28, a number = 4 colour + 20, text = 4 colour + 12 + 4 per
// character (generous). The whole frame must stay under about 8 KB, shared with the title bar, labels and the marks control.
enum { kGridBudget = 5500, kRectCost = 8, kOutlineCost = 28, kBatchCost = 16, kNumberCost = 24 };
inline uint16_t textCost(uint8_t chars) { return (uint16_t)(16 + 4 * chars); }

struct GridPlan
{
    bool outlines; // steady outlines of armed follow actions
    bool notches;  // Play 1x notches
    bool stops;    // dim squares of empty pads that stop a sounding track
    bool help;     // the help lines (only while button 1 or 3 is held)
    uint16_t cost; // estimated bytes of the planned grid
};

inline GridPlan planGrid(const View& v)
{
    GridPlan p = { false, false, false, false, 0 };
    uint32_t cost = 0;
    cost += (uint32_t)(kTracks + kVisRows) * kNumberCost;                        // track and row numbers
    cost += (uint32_t)(kVisRows * kTracks + kVisRows) * kRectCost + 3 * kBatchCost; // cells and scene cells in three batches
    cost += 2 * kOutlineCost + 8 + kOutlineCost + 8;                              // pad window frames, focus frame
    cost += textCost(14) + textCost(18) + textCost(18);                           // launch quantise, focus lines
    cost += textCost(22);                                                         // the scene line of the info column (plan 006)
    if (v.msg) cost += textCost(44);
    uint32_t follow = 0, one = 0, stop = 0;
    for (uint8_t r = 0; r < kVisRows; r++)
    {
        for (uint8_t t = 0; t < kTracks; t++)
        {
            if (v.cell[r][t] == CELL_FOLLOW) follow++;
            if (v.flags[r][t] & F_ONEX) one++;
            if (v.flags[r][t] & F_STOPMARK) stop++;
        }
        if (v.sceneFlags[r] & F_ONEX) one++;
    }
    uint32_t c = cost + follow * kOutlineCost + (follow ? 8 : 0);
    if (c <= kGridBudget) { p.outlines = true; cost = c; }
    c = cost + one * kRectCost + 2 * kBatchCost;
    if (c <= kGridBudget) { p.notches = true; cost = c; }
    c = cost + stop * kRectCost + kBatchCost;
    if (c <= kGridBudget) { p.stops = true; cost = c; }
    c = cost + 7 * textCost(22);
    if ((v.modeHeld || v.stopHeld) && c <= kGridBudget) { p.help = true; cost = c; }
    p.cost = (uint16_t)(cost > 0xFFFF ? 0xFFFF : cost);
    return p;
}
} // namespace clipgrid

// ---- merge for saving the pattern (launched clips never reach the pattern file) ------------------------------
// Tracks whose RAM data comes from a launched clip are written from the pattern FILE (cleared when there is no file).
enum SaveSource : uint8_t { SRC_RAM = 0, SRC_FILE = 1, SRC_CLEAR = 2 };
inline SaveSource saveSource(uint16_t launchedMask, uint8_t track, bool fileExists)
{
    if (!((launchedMask >> track) & 1u)) return SRC_RAM;
    return fileExists ? SRC_FILE : SRC_CLEAR;
}

// v0.3.1 M2: with per-track performance patterns loaded (fader FX kept across modules, or the stock Performance page) the RAM pattern holds
// tracks of OTHER patterns: bit t = the track is not the loaded pattern's own (perfPatterns[t] != actual). Those tracks must come from the
// pattern FILE when the loaded pattern is saved, never from RAM. 0 when performance mode is off.
inline uint16_t perfSwapMask(const uint16_t* perfPatterns, uint16_t actual, bool performanceMode, uint8_t tracks)
{
    uint16_t m = 0;
    if (!performanceMode) return 0;
    for (uint8_t t = 0; t < tracks && t < 16; t++)
        if (perfPatterns[t] != actual) m = (uint16_t)(m | (1u << t));
    return m;
}

// What a read of the pattern's workspace file (readWorkspacePatternTo: -1 error, 0 no file, 1 ok) means for the merge:
// only "no file" may clear the launched tracks; an unreadable file aborts the save and is retried (never overwritten).
enum MergeMode : uint8_t { MERGE_ABORT = 0, MERGE_FROM_FILE = 1, MERGE_NO_FILE = 2 };
inline MergeMode mergeMode(int readResult)
{
    return readResult < 0 ? MERGE_ABORT : (readResult == 0 ? MERGE_NO_FILE : MERGE_FROM_FILE);
}
// The pattern undo history is dropped when the last launched clip goes away (undo could restore another pattern's clip).
inline bool clearUndoOnMask(uint16_t before, uint16_t after) { return before != 0 && after == 0; }

// Why a launch was refused (seqSessionLastRefusal): busy = try again, stop-to-save = Plus / Mini have no room for the own data of the
// tracks, so unsaved edits must be saved (transport stopped) before a clip replaces a track.
enum Refusal : uint8_t { REFUSE_NONE = 0, REFUSE_BUSY = 1, REFUSE_STOP_TO_SAVE = 2 };

// ---- the launcher: queued clips / stops per track --------------------------------------------------------
class Launcher
{
  public:
    enum Kind : uint8_t { K_NONE = 0, K_CLIP = 1, K_STOP = 2 };
    enum Action : uint8_t { A_NONE = 0, A_CLIP = 1, A_STOP = 2 };

    Launcher() { reset(); }
    void reset()
    {
        for (int i = 0; i < kTracks; i++)
        {
            cur_[i] = 0;
            pat_[i] = 0;
            kind_[i] = K_NONE;
            q_[i] = kQuantGlobal;
            since_[i] = 0;
            lastTick_[i] = 0;
            applied_[i] = 0;
        }
        ticks_ = 0;
        switchTick_ = 0xFFFF;
        fresh_ = true;
        stopped_ = 0;
        loading_ = 0;
        frozen_ = false;
        pos_ = 0;
        len_ = 1;
        head_ = 0;
        tail_ = 0;
        quant_ = Q_BAR;
        epoch_ = 0;
        cancelScene();
        gpos_ = 0;
        glen_ = 1;
    }

    // ---- scene (plan 007 B2): main loop writes, audio context reads ----
    void beginScene(uint8_t pattern, Quant q)
    {
        sceneReady_ = false;
        sceneQ_ = q;
        scenePattern_ = pattern;
        scenePending_ = true;
    }
    void setSceneReady(bool r) { sceneReady_ = r; }
    void cancelScene()
    {
        scenePending_ = false;
        sceneReady_ = false;
        scenePattern_ = 0;
        sceneQ_ = Q_PATTERN;
    }
    bool scenePending() const { return scenePending_; }
    bool sceneReady() const { return sceneReady_; }
    uint8_t scenePattern() const { return scenePattern_; }
    Quant sceneQuant() const { return (Quant)sceneQ_; }
    // The global position (the step that plays next) and the length of the playing pattern, as of the last advance(): what a scene
    // counts from (the clip counter pos_ / len_ is frozen after a launch and is not the pattern).
    uint16_t globalPosition() const { return gpos_; }
    uint16_t globalLength() const { return glen_; }

    // ---- main loop ----
    void setQuant(Quant q) { quant_ = q; }
    Quant quant() const { return quant_; }
    // The clip data of the track is being (re)written in the buffer: the audio side must not apply it.
    void beginLoad(uint8_t t) { loading_ = (uint16_t)(loading_ | (1u << t)); }
    void endLoad(uint8_t t) { loading_ = (uint16_t)(loading_ & ~(1u << t)); }
    // q = kQuantGlobal (the global setting) or a Quant of its own (Q_CLIPEND for a follow action)
    bool postClip(uint8_t t, uint8_t pattern, uint8_t q = kQuantGlobal) { return t < kTracks && pattern >= 1 && push(LCMD_CLIP, t, pattern, q); }
    bool postStop(uint8_t t, uint8_t q = kQuantGlobal) { return t < kTracks && push(LCMD_STOP, t, 0, q); }
    bool postStopAll() { return push(LCMD_STOPALL, 0, 0, kQuantGlobal); }
    bool postCancel(uint8_t t) { return t < kTracks && push(LCMD_CANCEL, t, 0, kQuantGlobal); } // drop a queued clip / stop of the track

    // The quantise a queued command waits for (its own, or the global one).
    Quant queuedQuant(uint8_t t) const { return q_[t] == kQuantGlobal ? (Quant)quant_ : (Quant)q_[t]; }
    // How often a command was applied on the track, and how often the whole pattern switched: a follow action is queued once
    // per (pattern, applied, epoch) instance of the playing clip (FollowArms).
    uint8_t applied(uint8_t t) const { return applied_[t]; }
    uint8_t epoch() const { return epoch_; }
    // Steps counted by advance(): a main-loop rate limit that follows the music. Commands posted and not yet drained.
    uint16_t ticks() const { return ticks_; }
    bool ringPending() const { return head_ != tail_; }
    uint8_t ringFree() const { return (uint8_t)((tail_ + kRing - head_ - 1) % kRing); }
    // A clip that joined mid-loop (Q_NOW): the step counter of the clip end starts at the step it entered (audio context).
    void setPhase(uint8_t t, uint16_t step) { if (t < kTracks) since_[t] = step; }

    // 0 = the pattern's own track (never launched since the last pattern switch)
    uint8_t current(uint8_t t) const { return cur_[t]; }
    bool stopped(uint8_t t) const { return (stopped_ >> t) & 1u; }
    uint16_t stoppedMask() const { return stopped_; }
    Kind queuedKind(uint8_t t) const { return (Kind)kind_[t]; }
    uint8_t queuedPattern(uint8_t t) const { return kind_[t] == K_CLIP ? pat_[t] : 0; }
    // Is the clip of pattern p playing on track t? (own = pattern the project currently has loaded)
    bool isPlaying(uint8_t t, uint8_t p, uint8_t own) const { return !stopped(t) && (cur_[t] ? cur_[t] : own) == p; }
    bool isQueued(uint8_t t, uint8_t p, uint8_t own) const
    {
        if (kind_[t] == K_CLIP) return pat_[t] == p;
        if (kind_[t] == K_STOP) return isPlaying(t, p, own);
        return false;
    }
    // Tracks whose pattern data in RAM comes from a launched clip of another pattern (own = loaded pattern).
    uint16_t launchedMask(uint8_t own) const
    {
        uint16_t m = 0;
        for (int i = 0; i < kTracks; i++)
            if (cur_[i] != 0 && cur_[i] != own) m = (uint16_t)(m | (1u << i));
        return m;
    }
    // Own step counter for the quantise points: the global position wraps at the length of track 1's clip once a clip
    // was launched, so after the first launch the counter runs on its own with the length frozen (the last bar may be short).
    // Call once per step from the hook of track 0 (before the other tracks).
    uint16_t advance(uint16_t globalPos, uint16_t patternLen)
    {
        gpos_ = globalPos;
        glen_ = patternLen ? patternLen : 1;
        if (!frozen_)
        {
            pos_ = globalPos;
            len_ = patternLen ? patternLen : 1;
        }
        else
        {
            pos_ = (uint16_t)(pos_ + 1);
            if (pos_ >= len_) pos_ = 0;
        }
        ticks_ = (uint16_t)(ticks_ + 1);
        if (fresh_)
        {
            // the transport started (maybe away from step 0): every track is on the step the global position says, so the clip
            // end counter starts one step before it (onStep counts the step just entered)
            fresh_ = false;
            uint16_t s = globalPos ? (uint16_t)(globalPos - 1) : 0;
            for (int i = 0; i < kTracks; i++)
                since_[i] = s;
        }
        return pos_;
    }
    uint16_t position() const { return pos_; }
    uint16_t length() const { return len_; }
    bool anyQueued() const
    {
        for (int i = 0; i < kTracks; i++)
            if (kind_[i] != K_NONE) return true;
        return false;
    }

    // ---- audio context ----
    void drain()
    {
        while (tail_ != head_)
        {
            __asm__ __volatile__("" ::: "memory");
            const Cmd c = ring_[tail_];
            tail_ = (uint8_t)((tail_ + 1) % kRing);
            if (c.kind == LCMD_CLIP)
            {
                kind_[c.track] = K_CLIP;
                pat_[c.track] = c.pattern;
                q_[c.track] = c.q;
            }
            else if (c.kind == LCMD_CANCEL)
            {
                kind_[c.track] = K_NONE;
                pat_[c.track] = 0;
                q_[c.track] = kQuantGlobal;
            }
            else if (c.kind == LCMD_STOP)
            {
                kind_[c.track] = K_STOP;
                pat_[c.track] = 0;
                q_[c.track] = c.q;
            }
            else
            {
                for (int i = 0; i < kTracks; i++)
                {
                    kind_[i] = K_STOP;
                    pat_[i] = 0;
                    q_[i] = kQuantGlobal;
                }
            }
        }
    }
    // One call per track at its step boundary. A_CLIP: copy the loaded clip into the pattern (pattern = source),
    // A_STOP: silence the track. clipLen = steps of the clip that plays on the track (0 = unknown: Q_CLIPEND never hits). The
    // step counter since_ counts the steps since the clip entered (reset by apply, wraps with the clip) and is what Q_CLIPEND
    // waits for: it counts steps, not positions, so CUT jumps and rate changes do not confuse it.
    Action onStep(uint8_t t, uint16_t pos, uint8_t& pattern, uint16_t clipLen = 0)
    {
        bool clipEnd = false;
        if (clipLen)
        {
            uint16_t s;
            if (switchTick_ == ticks_ && lastTick_[t] != ticks_)
                s = 0; // the pattern switched in this very step before this track was visited: it is on its first step
            else
                s = (uint16_t)((since_[t] + 1u) % clipLen);
            since_[t] = s;
            clipEnd = s == 0;
        }
        lastTick_[t] = ticks_;
        if (kind_[t] == K_NONE) return A_NONE;
        if (kind_[t] == K_CLIP && ((loading_ >> t) & 1u)) return A_NONE;
        Quant q = queuedQuant(t);
        if (q == Q_CLIPEND ? !clipEnd : !quantHit(q, pos)) return A_NONE;
        return apply(t, pattern);
    }
    // Transport not running: a pending launch applies at once (no quantise).
    Action flush(uint8_t t, uint8_t& pattern)
    {
        if (kind_[t] == K_NONE) return A_NONE;
        if (kind_[t] == K_CLIP && ((loading_ >> t) & 1u)) return A_NONE;
        return apply(t, pattern);
    }
    // The whole pattern switched (scene, song, Pattern page): every track follows the new pattern again.
    void onPatternSwitched()
    {
        for (int i = 0; i < kTracks; i++)
        {
            cur_[i] = 0;
            pat_[i] = 0;
            kind_[i] = K_NONE;
            q_[i] = kQuantGlobal;
            since_[i] = 0; // every track starts its pattern again
        }
        switchTick_ = ticks_;
        stopped_ = 0;
        frozen_ = false;
        epoch_ = (uint8_t)(epoch_ + 1);
    }
    // Transport stop: stopped tracks play again, queued stops are dropped.
    void onTransportStop()
    {
        stopped_ = 0;
        frozen_ = false;
        for (int i = 0; i < kTracks; i++)
        {
            if (kind_[i] == K_STOP) kind_[i] = K_NONE;
        }
        fresh_ = true; // the next Play: the clip end counters start from the position the transport starts at
        cancelScene();
    }
    // Panic (plan 005 M6, main loop, audio callback stopped, transport stopped): queued launches and stops are dropped, the
    // ring is emptied and stopped tracks play again. The launched clips (cur_) stay until the caller restored the tracks.
    void panic()
    {
        head_ = tail_;
        for (int i = 0; i < kTracks; i++)
        {
            kind_[i] = K_NONE;
            pat_[i] = 0;
            q_[i] = kQuantGlobal;
        }
        fresh_ = true;
        stopped_ = 0;
        frozen_ = false;
        cancelScene();
    }

  private:
    enum { kRing = 32 };
    enum CmdKind : uint8_t { LCMD_CLIP = 1, LCMD_STOP = 2, LCMD_STOPALL = 3, LCMD_CANCEL = 4 };
    struct Cmd
    {
        uint8_t kind, track, pattern, q;
    };
    bool push(uint8_t kind, uint8_t t, uint8_t p, uint8_t q)
    {
        uint8_t next = (uint8_t)((head_ + 1) % kRing);
        if (next == tail_) return false;
        ring_[head_].kind = kind;
        ring_[head_].track = t;
        ring_[head_].pattern = p;
        ring_[head_].q = q;
        __asm__ __volatile__("" ::: "memory"); // the entry must be written before the audio side sees the new head
        head_ = next;
        return true;
    }
    Action apply(uint8_t t, uint8_t& pattern)
    {
        Action a;
        if (kind_[t] == K_CLIP)
        {
            cur_[t] = pat_[t];
            pattern = pat_[t];
            stopped_ = (uint16_t)(stopped_ & ~(1u << t));
            frozen_ = true; // from now on the pattern length may change under us
            a = A_CLIP;
        }
        else
        {
            stopped_ = (uint16_t)(stopped_ | (1u << t));
            a = A_STOP;
        }
        kind_[t] = K_NONE;
        pat_[t] = 0;
        q_[t] = kQuantGlobal;
        since_[t] = 0; // the clip (or the silence) starts here
        applied_[t] = (uint8_t)(applied_[t] + 1);
        return a;
    }

    volatile uint8_t cur_[kTracks];
    volatile uint8_t pat_[kTracks];
    volatile uint8_t kind_[kTracks];
    volatile uint8_t q_[kTracks];
    volatile uint16_t since_[kTracks];
    volatile uint16_t lastTick_[kTracks];
    volatile uint16_t ticks_, switchTick_;
    volatile bool fresh_;
    volatile uint8_t applied_[kTracks];
    volatile uint8_t epoch_;
    volatile uint16_t stopped_;
    volatile uint16_t loading_;
    volatile Quant quant_;
    volatile bool frozen_;
    volatile uint16_t pos_, len_;
    volatile uint16_t gpos_, glen_;
    volatile bool scenePending_, sceneReady_;
    volatile uint8_t scenePattern_, sceneQ_;
    Cmd ring_[kRing];
    volatile uint8_t head_, tail_;
};


// A follow action may only be queued for a track when nothing of the performer is on its way: a tap that is posted but not yet
// drained leaves queuedKind NONE, and queueing over it would overwrite the buffer slot the tap loaded.
inline bool followMayQueue(const Launcher& l, uint8_t t)
{
    return t < kTracks && !l.ringPending() && !l.stopped(t) && l.queuedKind(t) == Launcher::K_NONE;
}

} // namespace seqsession
