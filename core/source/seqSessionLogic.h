// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Session view / clip launcher: pure logic (no Polyend includes), host-tested in core/seqtest.
// Threads: the Launcher is written by the main loop through a ring buffer (post*), consumed in the audio
// context (drain / onStep). The getters are safe to call from the main loop (single bytes / masks).
#pragma once
#include <stdint.h>

namespace seqsession {

enum { kTracks = 16, kWinRows = 4, kPatternMax = 255, kCols = 12, kRows = 4, kPads = 48 };

// ---- launch quantise -------------------------------------------------------------------------
enum Quant : uint8_t { Q_BAR = 0, Q_PATTERN = 1, Q_NOW = 2, Q_COUNT = 3 };

inline Quant nextQuant(Quant q) { return (Quant)((q + 1) % Q_COUNT); }
inline const char* quantName(Quant q) { return q == Q_BAR ? "next bar" : q == Q_PATTERN ? "pattern end" : "immediate"; }

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

// ---- clip pad states and LEDs -----------------------------------------------------------------
enum ClipState : uint8_t { CS_EMPTY = 0, CS_NOTES = 1, CS_QUEUED = 2, CS_PLAYING = 3 };

inline ClipState clipState(bool hasNotes, bool playing, bool queued)
{
    if (queued) return CS_QUEUED;
    if (playing) return CS_PLAYING;
    return hasNotes ? CS_NOTES : CS_EMPTY;
}
// 0..15. bright = playing, blinking = queued, dim = has notes, off = empty.
inline uint8_t clipLevel(ClipState s, bool blinkOn)
{
    switch (s)
    {
        case CS_PLAYING: return 15;
        case CS_QUEUED: return blinkOn ? 15 : 0;
        case CS_NOTES: return 3;
        default: return 0;
    }
}
inline bool blinkPhase(uint32_t ms) { return ((ms / 150u) & 1u) == 0; } // ~3.3 Hz

// ---- pad map (0-based col 0..11, row 0..3; pad index = row * 12 + col) ---------------------------
enum PadKind : uint8_t { PK_NONE = 0, PK_CLIP, PK_SCENE, PK_STOP_T, PK_STOP_ALL, PK_LAYER_CLIPS, PK_LAYER_CUT, PK_QUANT };

struct PadAction
{
    PadKind kind;
    uint8_t col, row;
};

inline PadAction mapPad(uint8_t col, uint8_t row)
{
    PadAction a = { PK_NONE, col, row };
    if (col < 8) a.kind = PK_CLIP;
    else if (col == 8) a.kind = PK_SCENE;
    else if (col == 9 && row == 0) a.kind = PK_STOP_T;
    else if (col == 9 && row == 3) a.kind = PK_STOP_ALL;
    else if (col == 10 && row == 0) a.kind = PK_LAYER_CLIPS;
    else if (col == 10 && row == 1) a.kind = PK_LAYER_CUT;
    else if (col == 11 && row == 0) a.kind = PK_QUANT;
    return a;
}
// The two layer-switch pads belong to the Session module in every layer.
inline bool isLayerSwitchPad(uint8_t col, uint8_t row) { return col == 10 && row < 2; }

// Group of 8 tracks from the screen buttons (0..7): button 1 = tracks 1-8, button 2 = tracks 9-16.
// Shift + any button = tracks 9-16. Returns the first track, or 0xFF when the button does nothing.
inline uint8_t groupFromButton(uint8_t button, bool shift)
{
    if (shift) return 8;
    if (button == 0) return 0;
    if (button == 1) return 8;
    return 0xFF;
}

// ---- "has notes" cache of the window: one 16-bit track mask per pattern row -----------------------
struct NoteCache
{
    uint8_t first = 0; // window start the masks belong to (0 = none)
    uint16_t mask[kWinRows] = { 0, 0, 0, 0 };
    uint8_t known = 0; // bit per row
    void invalidate(uint8_t newFirst)
    {
        first = newFirst;
        known = 0;
        for (int i = 0; i < kWinRows; i++)
            mask[i] = 0;
    }
    void invalidateRow(uint8_t row) { known &= (uint8_t) ~(1u << row); }
    void set(uint8_t row, uint16_t m)
    {
        if (row >= kWinRows) return;
        mask[row] = m;
        known |= (uint8_t)(1u << row);
    }
    bool isKnown(uint8_t row) const { return (known >> row) & 1u; }
    // next row still to be read, or -1
    int nextToScan() const
    {
        for (int i = 0; i < kWinRows; i++)
            if (!isKnown((uint8_t)i)) return i;
        return -1;
    }
    bool hasNotes(uint8_t row, uint8_t track) const { return row < kWinRows && isKnown(row) && ((mask[row] >> track) & 1u); }
};

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
        }
        stopped_ = 0;
        loading_ = 0;
        head_ = tail_ = 0;
        quant_ = Q_BAR;
    }

    // ---- main loop ----
    void setQuant(Quant q) { quant_ = q; }
    Quant quant() const { return quant_; }
    // The clip data of the track is being (re)written in the buffer: the audio side must not apply it.
    void beginLoad(uint8_t t) { loading_ = (uint16_t)(loading_ | (1u << t)); }
    void endLoad(uint8_t t) { loading_ = (uint16_t)(loading_ & ~(1u << t)); }
    bool postClip(uint8_t t, uint8_t pattern) { return t < kTracks && pattern >= 1 && push(CMD_CLIP, t, pattern); }
    bool postStop(uint8_t t) { return t < kTracks && push(CMD_STOP, t, 0); }
    bool postStopAll() { return push(CMD_STOPALL, 0, 0); }

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
            const Cmd c = ring_[tail_];
            tail_ = (uint8_t)((tail_ + 1) % kRing);
            if (c.kind == CMD_CLIP)
            {
                kind_[c.track] = K_CLIP;
                pat_[c.track] = c.pattern;
            }
            else if (c.kind == CMD_STOP)
            {
                kind_[c.track] = K_STOP;
                pat_[c.track] = 0;
            }
            else
            {
                for (int i = 0; i < kTracks; i++)
                {
                    kind_[i] = K_STOP;
                    pat_[i] = 0;
                }
            }
        }
    }
    // One call per track at its step boundary. A_CLIP: copy the loaded clip into the pattern (pattern = source),
    // A_STOP: silence the track.
    Action onStep(uint8_t t, uint16_t globalPos, uint8_t& pattern)
    {
        if (kind_[t] == K_NONE) return A_NONE;
        if (kind_[t] == K_CLIP && ((loading_ >> t) & 1u)) return A_NONE;
        if (!quantHit(quant_, globalPos)) return A_NONE;
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
        }
        stopped_ = 0;
    }
    // Transport stop: stopped tracks play again, queued stops are dropped.
    void onTransportStop()
    {
        stopped_ = 0;
        for (int i = 0; i < kTracks; i++)
            if (kind_[i] == K_STOP) kind_[i] = K_NONE;
    }

  private:
    enum { kRing = 32 };
    enum CmdKind : uint8_t { CMD_CLIP = 1, CMD_STOP = 2, CMD_STOPALL = 3 };
    struct Cmd
    {
        uint8_t kind, track, pattern;
    };
    bool push(uint8_t kind, uint8_t t, uint8_t p)
    {
        uint8_t next = (uint8_t)((head_ + 1) % kRing);
        if (next == tail_) return false;
        ring_[head_].kind = kind;
        ring_[head_].track = t;
        ring_[head_].pattern = p;
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
            a = A_CLIP;
        }
        else
        {
            stopped_ = (uint16_t)(stopped_ | (1u << t));
            a = A_STOP;
        }
        kind_[t] = K_NONE;
        pat_[t] = 0;
        return a;
    }

    volatile uint8_t cur_[kTracks];
    volatile uint8_t pat_[kTracks];
    volatile uint8_t kind_[kTracks];
    volatile uint16_t stopped_;
    volatile uint16_t loading_;
    volatile Quant quant_;
    Cmd ring_[kRing];
    volatile uint8_t head_, tail_;
};

} // namespace seqsession
