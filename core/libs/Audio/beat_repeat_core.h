// Sequencer tracks mod (plan 021) - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Beat repeat of the master bus, Ableton style, after the owner's Model:Samples design (port/mfx2/SPEC.md 2.3 / 7): a stereo stage
// without any Polyend include, header only, host-tested (core/seqtest/test_beatrepeat.cpp).
//
//   * NO continuous recording. A repeat is TRIGGERED (automatically every INTERVAL with probability CHANCE, or by a momentary pad
//     at the pad's grid): from the trigger the slice (one GRID long) is written into the ring while the audio passes; at the
//     next grid boundary the repeats start and loop the slice (one repeat per grid, each starting at a grid boundary of the clock).
//     Idle = no work at all (the wrapper passes the blocks through untouched).
//   * The clock is the sequencer's: integer TICKS (192 per beat = 48 per sequencer step = one uStep), advanced inside the block
//     by the tempo, so a trigger / slice start lands on the exact sample of the clock (tick increment).
//   * Per repeat k (k = 0 for the first repeat): level = (-6 dB * DECAY/100)^k, pitch = -3 semitones * PITCHDECAY/100 * k (floor
//     -24 st, a resampled read - the repeat still lasts one grid, so it plays only the start of the slice: the rhythm stays on the grid).
//   * Modes: MIX (repeats on top of the running audio, the default), INSERT (the audio is muted while repeats sound), GATE (the audio is
//     muted while the effect is enabled - chance > 0 or a pad held or a repeat running - including while the slice is captured).
//   * Click free: every repeat has a 32 frame fade in / out, the audio gain moves by 1/64 per frame, a repeat cut by the next slice
//     start or by the end of the event fades out over 32 frames. If the tempo drops, the slice is shorter than its grid: its last
//     frame (already faded to 0) is held until the next slice start.
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace beatrepeat {

constexpr float kFs = 44117.64706f;      // AUDIO_SAMPLE_RATE_EXACT
constexpr uint32_t kRingFrames = 32768;  // stereo frames of the slice ring (128 KiB of EXTERNAL_RAM)
constexpr int kEdge = 32;                // fade frames at both ends of a repeat and of a cut
constexpr int kDryRamp = 64;             // frames the audio gain needs for a full move
constexpr float kBpmMin = 30.0f, kBpmMax = 300.0f;
constexpr int kTicksPerBeat = 192, kTicksPerStep = 48;
constexpr int kBlock = 128;

enum Mode : uint8_t { MODE_MIX = 0, MODE_INSERT = 1, MODE_GATE = 2, MODE_COUNT = 3 };
enum State : uint8_t { S_IDLE = 0, S_PENDING = 1, S_CAPTURE = 2, S_REPEAT = 3 };

// ---- grid / interval / gate tables ----------------------------------------------------------------------------------------------
// Grid ids as the pads show them: 1/4 1/8 1/16 1/32, then the triplets 1/4T 1/8T 1/16T 1/32T.
enum { kGrids = 8, kIntervals = 8, kGates = 8 };
inline uint16_t gridTicks(uint8_t id)
{
    static const uint16_t t[kGrids] = { 192, 96, 48, 24, 128, 64, 32, 16 };
    return t[id & 7];
}
inline const char* gridName(uint8_t id)
{
    static const char* const n[kGrids] = { "1/4", "1/8", "1/16", "1/32", "1/4T", "1/8T", "1/16T", "1/32T" };
    return n[id & 7];
}
// the grids from the longest to the shortest (the order "positions away" of VARIATION and the clamp for slow tempos use)
inline uint8_t gridOrder(uint8_t pos)
{
    static const uint8_t o[kGrids] = { 0, 4, 1, 5, 2, 6, 3, 7 };
    return o[pos & 7];
}
inline uint8_t gridPos(uint8_t id)
{
    for (uint8_t p = 0; p < kGrids; p++)
        if (gridOrder(p) == (id & 7))
            return p;
    return 0;
}
// INTERVAL: 1/32 ... 4 bars in powers of two
inline uint16_t intervalTicks(uint8_t i)
{
    static const uint16_t t[kIntervals] = { 24, 48, 96, 192, 384, 768, 1536, 3072 };
    return t[i & 7];
}
inline const char* intervalName(uint8_t i)
{
    static const char* const n[kIntervals] = { "1/32", "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars" };
    return n[i & 7];
}
// GATE: how long an automatic repeat event lasts, 8 positions in sixteenths (1/16 ... 4/4)
inline uint16_t gateTicks(uint8_t g)
{
    static const uint8_t n16[kGates] = { 1, 2, 3, 4, 6, 8, 12, 16 };
    return (uint16_t)(n16[g & 7] * kTicksPerStep);
}
inline const char* gateName(uint8_t g)
{
    static const char* const n[kGates] = { "1/16", "2/16", "3/16", "4/16", "6/16", "8/16", "12/16", "16/16" };
    return n[g & 7];
}

inline float clampBpm(float bpm) { return bpm < kBpmMin ? kBpmMin : (bpm > kBpmMax ? kBpmMax : bpm); }
inline float framesPerTick(float bpm) { return kFs * 60.0f / (clampBpm(bpm) * (float)kTicksPerBeat); }

// The grid actually used at this tempo: a slice has to fit the ring, so a grid that is too long steps down to the next shorter one
// (1/4 reaches down to 81 BPM at 32768 frames, 1/4T to 54, 1/8 to 40, 1/8T to 27).
inline uint8_t clampGrid(uint8_t id, float bpm, uint32_t capacity = kRingFrames)
{
    const float fpt = framesPerTick(bpm);
    uint8_t p = gridPos(id);
    while (p < kGrids - 1 && (float)gridTicks(gridOrder(p)) * fpt > (float)capacity - 8.0f)
        p++;
    return gridOrder(p);
}

// ---- parameters (all plain values; the UI maps its 8 step faders to them) --------------------------------------------------------
struct Settings
{
    uint8_t grid = 2;       // grid of the AUTOMATIC repeats (grid id)
    uint8_t interval = 5;   // INTERVAL index
    uint8_t chance = 0;     // CHANCE 0..100 %; 0 = no automatic repeats
    uint8_t gate = 7;       // GATE index (how long an automatic event lasts)
    uint8_t variation = 0;  // VARIATION 0..10
    uint8_t pitchDecay = 0; // PITCH DECAY 0..100
    uint8_t decay = 0;      // DECAY 0..100
    uint8_t mode = MODE_MIX;
};

struct Clock
{
    bool running = false; // the transport plays: tick / frac are the sequencer's position
    int32_t tick = 0;     // whole ticks (192 per beat) of the position at the start of the block
    float frac = 0.0f;    // 0 .. <1 of the next tick
    float bpm = 120.0f;
};

// What the UI shows
struct Status
{
    uint8_t state;      // State
    uint8_t padHeld;
    uint8_t latch;
    uint8_t grid;       // grid id of the event in progress (clamped to the tempo)
    uint16_t gridTick;
    uint32_t capLen;    // frames of the captured slice
    uint16_t repeatIdx; // repeats started so far in this event
    uint16_t slice1000; // 0..1000 position inside the grid cell of the event (live playhead)
    uint32_t captures;  // counts finished captures (the waveform of the slice changes with it)
    uint8_t levelDb;    // 0.. repeat level, dB below full (0 = full), 255 = none
    uint8_t pitchSemi;  // 0..24 semitones down of the running repeat
};

struct Voice
{
    bool on = false;
    float pos = 0, ratio = 1, gain = 1, fadeMul = 1;
    bool forced = false;
    uint32_t j = 0, len = 0;
    float lastL = 0, lastR = 0;
};

class Core
{
  public:
    void init(int16_t* ring, uint32_t frames)
    {
        ring_ = ring;
        cap_ = frames;
        reset();
    }
    void reset()
    {
        state_ = S_IDLE;
        padEvent_ = padHeld_ = false;
        latch_ = false;
        v_[0] = Voice();
        v_[1] = Voice();
        dry_ = 1.0f;
        needRebase_ = true;
        fired_ = false;
        qHead_ = qTail_ = 0;
        wr_ = capLen_ = 0;
        rep_ = 0;
        evTicks_ = 0;
        tk_ = 0;
        fr_ = 0;
    }
    void seed(uint32_t s) { rng_ = s ? s : 1u; }
    void setSettings(const Settings& s) { s_ = s; }
    const Settings& settings() const { return s_; }

    // ---- main loop -> audio: commands in a small single producer / single consumer ring (applied at the start of the next block) ----
    enum Cmd : uint8_t { C_DOWN = 1, C_UP = 2, C_LATCH = 3, C_UNLATCH = 4, C_DISENGAGE = 5, C_PANIC = 6 };
    bool post(uint8_t cmd, uint8_t arg = 0)
    {
        const uint8_t h = qHead_, n = (uint8_t)((h + 1) & (kQ - 1));
        if (n == qTail_)
            return false;
        q_[h] = (uint8_t)((cmd << 4) | (arg & 15));
        qHead_ = n;
        return true;
    }
    bool pending() const { return qHead_ != qTail_; }

    // nothing to do for the audio: the wrapper passes the blocks through untouched (exact bypass)
    bool bypassable() const
    {
        return state_ == S_IDLE && !v_[0].on && !v_[1].on && dry_ == 1.0f && s_.chance == 0 && qHead_ == qTail_ && !padHeld_;
    }
    void noteBypass() { needRebase_ = true; }

    // in-place processing is fine (frame i is read before it is written)
    void process(const Clock& clk, const int16_t* inL, const int16_t* inR, int16_t* outL, int16_t* outR, int n)
    {
        const float bpm = clampBpm(clk.bpm);
        bpm_ = bpm;
        drain();
        const float tpf = bpm * (float)kTicksPerBeat / (60.0f * kFs); // ticks per frame
        if (needRebase_)
        {
            needRebase_ = false;
            fired_ = false;
            if (clk.running)
            {
                tk_ = clk.tick;
                fr_ = clk.frac;
            }
        }
        else if (clk.running)
        {
            const int32_t dt = clk.tick - tk_;
            const float d = (float)dt + (clk.frac - fr_);
            if (d > 3.0f || d < -3.0f)
            {
                tk_ = clk.tick; // the position jumped (pattern wrap, jump, new Play): no tick is fired for it
                fr_ = clk.frac;
                fired_ = false;
            }
            else if (d > 0.0f)
            {
                for (int32_t k = 0; k < dt; k++) // a small catch up: the skipped ticks are not lost
                {
                    tk_++;
                    onTick(bpm);
                }
                fr_ = clk.frac;
            }
        }
        for (int i = 0; i < n; i++)
        {
            // the position of frame i is (tk_, fr_) at its start; a tick fires at the first frame whose start position has reached it
            if (fired_)
            {
                fired_ = false;
                onTick(bpm);
            }
            const float l = (float)inL[i], r = (float)inR[i];
            if (state_ == S_CAPTURE && wr_ < cap_)
            {
                ring_[2 * wr_] = inL[i];
                ring_[2 * wr_ + 1] = inR[i];
                wr_++;
            }
            float rl = 0.0f, rr = 0.0f;
            for (int v = 0; v < 2; v++)
                if (v_[v].on)
                    render(v_[v], rl, rr);
            // the audio gain
            float target = 1.0f;
            if (s_.mode == MODE_INSERT)
                target = state_ == S_REPEAT ? 0.0f : 1.0f;
            else if (s_.mode == MODE_GATE)
                target = (s_.chance > 0 || padHeld_ || state_ != S_IDLE) ? 0.0f : 1.0f;
            if (dry_ < target)
            {
                dry_ += 1.0f / (float)kDryRamp;
                if (dry_ > target)
                    dry_ = target;
            }
            else if (dry_ > target)
            {
                dry_ -= 1.0f / (float)kDryRamp;
                if (dry_ < target)
                    dry_ = target;
            }
            outL[i] = sat(l * dry_ + rl);
            outR[i] = sat(r * dry_ + rr);
            fr_ += tpf;
            if (fr_ >= 1.0f)
            {
                fr_ -= 1.0f;
                tk_++;
                fired_ = true;
            }
        }
    }

    // ---- readout ----
    uint8_t state() const { return state_; }
    int32_t tick() const { return tk_; }
    uint32_t captured() const { return capLen_; }
    uint32_t captures() const { return captures_; }
    uint16_t repeatIndex() const { return rep_; }
    float dryGain() const { return dry_; }
    const int16_t* ring() const { return ring_; }
    uint32_t capacity() const { return cap_; }
    uint8_t eventGrid() const { return gridId_; }
    Status status() const
    {
        Status st;
        memset(&st, 0, sizeof(st));
        st.state = state_;
        st.padHeld = padHeld_;
        st.latch = latch_;
        st.grid = gridId_;
        st.gridTick = gridT_;
        st.capLen = capLen_;
        st.repeatIdx = rep_;
        st.captures = captures_;
        st.levelDb = 255;
        if (state_ == S_REPEAT || state_ == S_CAPTURE)
        {
            const uint32_t per = gridT_ ? gridT_ : 1u;
            st.slice1000 = (uint16_t)((evTicks_ % per) * 1000u / per);
        }
        if (state_ == S_REPEAT)
        {
            const float g = v_[0].on ? v_[0].gain : 1.0f;
            const float db = g > 0.001f ? -20.0f * log10f(g) : 60.0f;
            st.levelDb = (uint8_t)(db > 60.0f ? 60.0f : db);
            st.pitchSemi = pitchSemi(rep_ ? (uint16_t)(rep_ - 1) : (uint16_t)0);
        }
        return st;
    }
    uint8_t pitchSemi(uint16_t k) const
    {
        float st = 3.0f * (float)s_.pitchDecay / 100.0f * (float)k;
        return (uint8_t)(st > 24.0f ? 24.0f : st + 0.5f);
    }

  private:
    enum { kQ = 16 };

    static int16_t sat(float v)
    {
        v += v >= 0.0f ? 0.5f : -0.5f;
        return v > 32767.0f ? (int16_t)32767 : (v < -32768.0f ? (int16_t)-32768 : (int16_t)v);
    }
    uint32_t rnd()
    {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return rng_;
    }
    void drain()
    {
        while (qTail_ != qHead_)
        {
            const uint8_t b = q_[qTail_];
            qTail_ = (uint8_t)((qTail_ + 1) & (kQ - 1));
            const uint8_t cmd = (uint8_t)(b >> 4), arg = (uint8_t)(b & 15);
            switch (cmd)
            {
                case C_DOWN: padDown(arg); break;
                case C_UP: padUp(); break;
                case C_LATCH: latch_ = true; break;
                case C_UNLATCH:
                    latch_ = false;
                    if (!padHeld_ && padEvent_)
                        endEvent();
                    break;
                case C_DISENGAGE: endEvent(); break;
                case C_PANIC:
                    endEvent();
                    padHeld_ = false;
                    latch_ = false;
                    break;
                default: break;
            }
        }
    }
    void padDown(uint8_t id)
    {
        id &= 7;
        padHeld_ = true;
        const uint8_t g = clampGrid(id, bpm_ > 0 ? bpm_ : 120.0f, cap_);
        if (latch_ && padEvent_ && state_ != S_IDLE && gridId_ == g)
        {
            endEvent(); // latched and the same pad again: off
            return;
        }
        if (state_ != S_IDLE)
            endEvent(); // jam: the running repeat stops, the new grid starts at its next boundary
        gridId_ = g;
        gridT_ = gridTicks(g);
        padEvent_ = true;
        state_ = S_PENDING;
    }
    void padUp()
    {
        padHeld_ = false;
        if (padEvent_ && !latch_)
            endEvent();
    }
    void endEvent()
    {
        if (v_[0].on)
            cutVoice(v_[0]);
        if (v_[1].on && !v_[1].forced)
            cutVoice(v_[1]);
        state_ = S_IDLE;
        padEvent_ = false;
    }
    static void cutVoice(Voice& v)
    {
        if (v.j >= v.len)
            v.on = false; // held (already 0): nothing to fade
        else
            v.forced = true;
    }
    void beginCapture()
    {
        state_ = S_CAPTURE;
        wr_ = 0;
        evTicks_ = 0;
        rep_ = 0;
        capLen_ = 0;
    }
    void startVoice(uint16_t k)
    {
        // the running repeat leaves with a fade (or is dropped when it only holds its last frame)
        if (v_[0].on)
        {
            if (v_[0].j >= v_[0].len)
                v_[0].on = false;
            else
                v_[0].forced = true;
            v_[1] = v_[0];
        }
        Voice nv;
        nv.on = true;
        nv.pos = 0.0f;
        nv.j = 0;
        nv.len = capLen_ < 1 ? 1u : capLen_;
        float semi = 3.0f * (float)s_.pitchDecay / 100.0f * (float)k;
        if (semi > 24.0f)
            semi = 24.0f;
        nv.ratio = semi > 0.0f ? exp2f(-semi / 12.0f) : 1.0f;
        nv.gain = (s_.decay > 0 && k > 0) ? powf(0.5011872f, (float)s_.decay / 100.0f * (float)k) : 1.0f;
        v_[0] = nv;
    }
    uint8_t pickAutoGrid()
    {
        uint8_t p = gridPos(s_.grid);
        if (s_.variation && (rnd() % 10u) < s_.variation)
        {
            const int maxd = 1 + s_.variation / 4;
            int d = 1 + (int)(rnd() % (uint32_t)maxd);
            if (rnd() & 1u)
                d = -d;
            int q = (int)p + d;
            p = (uint8_t)(q < 0 ? 0 : (q > kGrids - 1 ? kGrids - 1 : q));
        }
        return gridOrder(p);
    }
    void onTick(float bpm)
    {
        if (state_ == S_IDLE)
        {
            if (s_.chance > 0 && tk_ >= 0 && (tk_ % (int32_t)intervalTicks(s_.interval)) == 0)
            {
                if ((rnd() % 100u) < s_.chance)
                {
                    const uint8_t g = clampGrid(pickAutoGrid(), bpm, cap_);
                    gridId_ = g;
                    gridT_ = gridTicks(g);
                    const uint32_t gt = gateTicks(s_.gate), two = 2u * gridT_;
                    evTotal_ = gt > two ? gt : two;
                    padEvent_ = false;
                    beginCapture();
                }
            }
        }
        else if (state_ == S_PENDING)
        {
            if (tk_ >= 0 && (tk_ % (int32_t)gridT_) == 0)
                beginCapture();
        }
        else // CAPTURE / REPEAT
        {
            evTicks_++;
            if (!padEvent_ && evTicks_ >= evTotal_)
            {
                endEvent();
                return;
            }
            if ((evTicks_ % gridT_) == 0)
            {
                if (state_ == S_CAPTURE)
                {
                    capLen_ = wr_;
                    captures_++;
                    state_ = S_REPEAT;
                    rep_ = 0;
                }
                startVoice(rep_);
                rep_++;
            }
        }
    }
    // one frame of a repeat voice
    void render(Voice& v, float& outL, float& outR)
    {
        float l, r;
        if (v.j < v.len)
        {
            uint32_t i0 = (uint32_t)v.pos;
            float f = v.pos - (float)i0;
            if (i0 + 1 >= v.len)
            {
                i0 = v.len - 1;
                f = 0.0f;
            }
            const int16_t* p = ring_ + 2 * i0;
            if (f > 0.0f)
            {
                l = (float)p[0] + ((float)p[2] - (float)p[0]) * f;
                r = (float)p[1] + ((float)p[3] - (float)p[1]) * f;
            }
            else
            {
                l = (float)p[0];
                r = (float)p[1];
            }
            // 0 at both ends of the repeat: no click at the slice boundary, and the held frame after it is silent
            float env = 1.0f;
            const float a = (float)v.j / (float)kEdge, b = (float)(v.len - 1u - v.j) / (float)kEdge;
            if (a < env)
                env = a;
            if (b < env)
                env = b;
            env *= v.gain;
            l *= env;
            r *= env;
            v.lastL = l;
            v.lastR = r;
            v.pos += v.ratio;
            v.j++;
        }
        else
        {
            l = v.lastL; // the tempo dropped: the slice is over before the next one starts - its last frame is held
            r = v.lastR;
        }
        if (v.forced)
        {
            v.fadeMul -= 1.0f / (float)kEdge;
            if (v.fadeMul <= 0.0f)
            {
                v.on = false;
                return;
            }
            l *= v.fadeMul;
            r *= v.fadeMul;
        }
        outL += l;
        outR += r;
    }

    int16_t* ring_ = nullptr;
    uint32_t cap_ = 0;
    Settings s_;
    volatile uint8_t q_[kQ] = {};
    volatile uint8_t qHead_ = 0, qTail_ = 0;
    uint8_t state_ = S_IDLE;
    bool padEvent_ = false, padHeld_ = false, latch_ = false, needRebase_ = true, fired_ = false;
    uint8_t gridId_ = 0;
    uint16_t gridT_ = 48;
    uint32_t evTicks_ = 0, evTotal_ = 0;
    uint32_t wr_ = 0, capLen_ = 0, captures_ = 0;
    uint16_t rep_ = 0;
    int32_t tk_ = 0;
    float fr_ = 0.0f, dry_ = 1.0f, bpm_ = 120.0f;
    Voice v_[2];
    uint32_t rng_ = 0x2545F491u;
};

} // namespace beatrepeat
