// Synth voice engine (plan 009 S): up to 4 polyphonic voices, block based (128 samples at 44.1 kHz), single precision float, no
// divide or transcendental in the sample loops, control rate parameters with linear ramps inside the block.
//
// Voice: two oscillator pairs (left / right). Each pair is two band limited saws, either detuned against each other (Shape 0)
// or locked into one pulse wave (Shape 1..100: second saw inverted, phase offset = pulse width). Shape 101.. (plan 015 W) is the
// Plinky wavetable mode: ONE band limited table oscillator per side instead of the saw pair, the sound is the blend of two
// neighbouring tables of 17 (plinky_wavetable.h). Noise, then a two pole "low pass
// gate" that follows the envelope (the louder, the brighter) with resonance, a shared LFO, an ADSR and glide.
//
// What comes from the Plinky firmware (plinky_public, sw/Core/Src/plinky.c, RunVoice): the oscillator model with four detuned
// oscillators (pitch offsets (i-2)*detune, odd ones + interval), the integer phase polyBLEP saw (corrections from the fraction of
// the sample at the wrap, computed with one integer divide per cycle), the pulse made of two saws, the low pass gate recursion
//   y1 += (in - (y2 - y1) * resonance - y1) * k;  y1 *= 0.999;  y2 += (y1 - y2) * k;  y2 *= 0.999
// with k following the envelope, the envelope followed by a block rate one pole, and the per-block linear ramps of volume,
// noise and phase increment. Everything else (sampler, arpeggiator, sequencer, effects, touch strips) is NOT used.
// The wavetable mode (plan 015 W) follows RunVoice of the same file (the pwm > 0 branch): the 17 tables of sw/Core/Src/wavetable.h with
// 9 octave levels each (the level from the phase increment: the table of the octave that has no harmonic above Nyquist), the linear
// interpolation inside a table (16 bit fraction) and the linear blend of two tables. Voice allocation, event queue, pitch maths, LFO,
// scaling and CPU optimisations are written for the Tracker.
//
// ----------------------------------------------------------------------------------------------------------------------
// Original work: Copyright (c) the authors of the Plinky firmware, https://github.com/plinkysynth/plinky_public
// ("The rest of the software of plinky is licensed under the MIT License", LICENSE.md of that repository). MIT License:
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
// Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
// WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
// (The Plinky name, logo and front panel design are not part of this grant; this is not a Plinky product.)
// ----------------------------------------------------------------------------------------------------------------------
// Port and the Tracker integration: Sequencer tracks mod, PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md).
#pragma once
#include "plinky_params.h"
#include "plinky_wavetable.h"
#include <string.h>

namespace plinkyvoice {

constexpr int kMaxVoices = 4;
constexpr int kBlock = 128;
constexpr float kFs = 44100.0f;
constexpr float kBlockSec = (float)kBlock / kFs;

// Pitch trace (plan 013 V): per voice a ring of (pitch, envelope) written at control rate (every kTraceEvery blocks = ~35 ms), read by
// the main loop (interface/seqPlay/pitchTrace.cpp). The reader copies samples behind the head only, so a torn read can at worst show
// one wrong point; nothing here is ever waited for.
constexpr int kTraceLen = 128;     // power of two; the view uses the newest 112
constexpr int kTraceEvery = 12;    // blocks per sample
struct TraceSample
{
    int16_t pitch; // (base note + glide position + LFO / bend pitch offset) * 32, semitones
    uint8_t env;   // envelope level 0..255
    uint8_t on;    // the voice sounds (not idle)
};

// 2^x, relative error < 2e-5 (7 term series on the fraction, exponent by bit pattern). No libm.
__attribute__((noinline)) inline float exp2fast(float x)
{
    if (x < -120.0f)
        x = -120.0f;
    if (x > 120.0f)
        x = 120.0f;
    int32_t i = (int32_t)x;
    float f = x - (float)i;
    if (f < 0.0f)
    {
        f += 1.0f;
        i -= 1;
    }
    float p = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * (0.0096181f + f * (0.0013333f + f * 0.000154f)))));
    union
    {
        uint32_t u;
        float f;
    } c;
    c.u = (uint32_t)(i + 127) << 23;
    return p * c.f;
}

// Phase increment (2^32 = one cycle per sample) of a pitch in semitones (69 = 440 Hz), limited to 0.7 Hz .. 16 kHz.
inline int32_t semiToDphase(float semis)
{
    float d = exp2fast(semis * (1.0f / 12.0f) + 19.602868678f);
    if (d < 65536.0f)
        d = 65536.0f;
    if (d > 1.6106127e9f)
        d = 1.6106127e9f;
    return (int32_t)d;
}

inline float softclip(float x)
{
    if (x > 1.5f)
        return 1.0f;
    if (x < -1.5f)
        return -1.0f;
    return x - 0.148148148f * x * x * x;
}

// One band limited saw sample (the Plinky integer polyBLEP). ph = phase after the increment, dph = the increment, prev = the
// delayed, already corrected sample (updated). Returns the sample of one step ago, -1 .. +1.
// At a wrap ft = the fraction of the sample since the wrap (0 .. 65535, one integer divide): the delayed sample loses
// (ft*ft)/2, the new one gains ((65535-ft)^2)/2. Kept inline and by value: no address-taken state, so the compiler keeps the
// phases in registers (an out-of-line helper cost about 15 cycles per sample in loads and stores).
inline float blepSaw(uint32_t ph, uint32_t dph, uint32_t& prev)
{
    uint32_t nw = ph;
    if (__builtin_expect(ph < dph, 0))
    {
        uint32_t ft = ph / (dph >> 16);
        if (ft > 65535u)
            ft = 65535u;
        prev -= (ft * ft) >> 1;
        ft = 65535u - ft;
        nw += (ft * ft) >> 1;
    }
    uint32_t o = prev;
    prev = nw;
    return (float)(int32_t)(o + 0x80000000u) * (1.0f / 2147483648.0f);
}

// Wavetable oscillator (plan 015 W). Octave level of a phase increment (2^32 = one cycle per sample): level 0 = 512 samples per cycle
// for increments below 2^23 (everything under 86 Hz), every octave up halves the table: level 8 = 2 samples. Table of level n starts at
// kWtLevelOff[n] in a table's 1031 values; n = 16 - 7 - clz(max(dph, 2^22)).
static constexpr uint16_t kWtLevelOff[9] = { 0, 513, 770, 899, 964, 997, 1014, 1023, 1028 };

inline int wtLevel(int32_t dph)
{
    uint32_t d = dph < (int32_t)(1 << 22) ? (uint32_t)(1 << 22) : (uint32_t)dph;
    int lv = 9 - __builtin_clz(d);
    return lv > 8 ? 8 : lv;
}
inline const int16_t* wtTable(int table, int level) { return wtData() + table * kWtSize + kWtLevelOff[level]; }

// One sample of the blend of tb (a table at its level) and tb + kWtSize (the next table): phase ph (32 bit), shift = level + 7, w = weight
// of the second table 0 .. 1. Result -1 .. +1 for the nominal +-16384 of the data. Both table reads are int16 with a 16 bit fraction;
// the sum is formed in 32 bit modular arithmetic (the true value fits), the blend in float.
inline float wtSample(const int16_t* tb, uint32_t ph, int shift, float w)
{
    const uint32_t i = ph >> shift;
    const uint32_t i2 = i >> 16, fr = i & 0xFFFFu;
    const int32_t a0 = tb[i2], a1 = tb[i2 + 1], b0 = tb[i2 + kWtSize], b1 = tb[i2 + 1 + kWtSize];
    const float ya = (float)(int32_t)(((uint32_t)a0 << 16) + (uint32_t)(a1 - a0) * fr);
    const float yb = (float)(int32_t)(((uint32_t)b0 << 16) + (uint32_t)(b1 - b0) * fr);
    return (ya + (yb - ya) * w) * (1.0f / 1073741824.0f); // 16384 * 65536 = 2^30 -> 1.0
}

struct Saw
{
    uint32_t ph = 0, prev = 0;
    int32_t dph = 65536, tdph = 65536;
};

enum Stage : uint8_t { ST_IDLE = 0, ST_ATTACK, ST_DECAY, ST_RELEASE };

struct Voice
{
    Saw s[4];       // 0 = left A, 1 = right A, 2 = left B, 3 = right B (Plinky: oscillator i, pairs (0,2) and (1,3))
    float y[4];     // low pass gate states: y1L, y2L, y1R, y2R
    float env = 0, pitch = 0, vel = 0;
    float g = 0, k = 0;   // amplitude / filter coefficient at the end of the last block
    uint32_t off = 0;     // pulse width offset (phase) at the end of the last block
    uint32_t serial = 0;
    uint8_t note = 0, stage = ST_IDLE, gate = 0, fresh = 0, relPend = 0;
    uint8_t wtOn = 0;     // the last block was rendered by the wavetable oscillator (the saws' delayed samples are stale then)
    Voice() { memset(y, 0, sizeof(y)); }
    void clear()
    {
        memset(y, 0, sizeof(y));
        env = g = k = 0;
        stage = ST_IDLE;
        gate = 0;
        relPend = 0;
    }
};

class Engine
{
  public:
    Engine() { init(); }

    void init()
    {
        for (int i = 0; i < P_COUNT; i++)
            p[i] = ef[i] = paramInfo(i).def;
        for (int i = 0; i < 4; i++)
            relPend[i] = 0;
        allOffPend = 0;
        for (int i = 0; i < kMaxVoices; i++)
            v[i] = Voice();
        head = 0;
        tail = 0;
        serial = 0;
        rng = 0x1234567u;
        lfoPhase = 0;
        bend = 0;
        mod = 0;
        lvlPrev = 0;
        smoothed = false;
        sWt = 0;
        wtPrev = -1.0f;
        for (int i = 0; i < E_N; i++)
            q[i].valid = 0;
        memset(trace, 0, sizeof(trace));
        traceHead = 0;
        traceDiv = 0;
        tracePending = false;
        traceOpen = false;
        memset(trLatch, 0, sizeof(trLatch));
    }

    // ---- any context (the sequencer runs in an interrupt, pads and the UI on the main loop) ----
    bool noteOn(uint8_t note, uint8_t vel) { return push(EV_ON, note, vel); }
    // A release that does not fit into the queue is remembered (one bit per note) and applied by the next block: a voice never sticks.
    bool noteOff(uint8_t note)
    {
        if (push(EV_OFF, note, 0))
            return true;
        __atomic_fetch_or(&relPend[(note >> 5) & 3], (uint32_t)1 << (note & 31), __ATOMIC_RELEASE);
        return true;
    }
    bool allNotesOff()
    {
        if (!push(EV_ALLOFF, 0, 0))
            __atomic_store_n(&allOffPend, (uint8_t)1, __ATOMIC_RELEASE);
        return true;
    }
    // 0 .. 16383, 8192 = centre
    bool pitchBend(int16_t value14) { return push(EV_BEND, (uint8_t)(value14 & 0x7F), (uint8_t)((value14 >> 7) & 0x7F)); }
    bool controlChange(uint8_t cc, uint8_t val) { return push(EV_CC, cc, val); }

    // The saved values p[] are written by the UI only (setParam). MIDI CCs change the effective values ef[] the DSP reads, like CC1 (mod
    // wheel): the saved patch is never overwritten by automation; moving the parameter in the UI takes the effective value back.
    void setParam(int i, int value)
    {
        if (i >= 0 && i < P_COUNT)
            p[i] = ef[i] = clampParam(i, value);
    }
    uint8_t param(int i) const { return p[i]; }
    uint8_t effective(int i) const { return ef[i]; }
    uint8_t p[P_COUNT];
    uint8_t ef[P_COUNT];

    // ---- audio context ----
    bool isActive() const
    {
        if (head != tail)
            return true;
        for (int i = 0; i < kMaxVoices; i++)
            if (v[i].stage != ST_IDLE)
                return true;
        return false;
    }
    int activeVoices() const
    {
        int n = 0;
        for (int i = 0; i < kMaxVoices; i++)
            n += v[i].stage != ST_IDLE;
        return n;
    }
    const Voice& voice(int i) const { return v[i]; }
    // pitch trace ring (any context, read only): sample k of voice i is trace[i][k & (kTraceLen - 1)], the newest is head - 1
    const TraceSample* traceRing(int i) const { return trace[i]; }
    uint16_t traceHeadIndex() const { return __atomic_load_n(&traceHead, __ATOMIC_ACQUIRE); }

    // Renders one block of kBlock samples (16 bit, left / right). Returns false (outputs untouched) when nothing sounds.
    bool render(int16_t* outL, int16_t* outR)
    {
        drain();
        bool any = false;
        for (int i = 0; i < kMaxVoices; i++)
            any |= v[i].stage != ST_IDLE;
        if (!any)
        {
            lvlPrev = 0;
            // v0.3.6: a note that began and ended between two trace samples (a stab) still gets its sample, and the line ends with a
            // closing sample (all off) where the engine went idle
            if (tracePending)
            {
                tracePending = false;
                traceDiv = 0;
                writeTrace();
            }
            if (traceOpen)
                writeTrace();
            return false;
        }
        Derived d;
        derive(d);
        memset(busL, 0, sizeof(busL));
        memset(busR, 0, sizeof(busR));
        for (int i = 0; i < kMaxVoices; i++)
        {
            Voice& vo = v[i];
            if (vo.stage == ST_IDLE)
                continue;
            if (i >= (int)ef[P_VOICES] && vo.stage != ST_RELEASE)
            {
                vo.stage = ST_RELEASE;
                vo.gate = 0;
            }
            renderVoice(vo, d);
            latchTrace(i, vo, d.vib);
            if (vo.relPend && !vo.fresh && vo.stage != ST_IDLE)
            {
                vo.relPend = 0;
                vo.gate = 0;
                vo.stage = ST_RELEASE;
            }
        }
        if (++traceDiv >= kTraceEvery)
        {
            traceDiv = 0;
            tracePending = false;
            writeTrace();
        }
        float lv = lvlPrev;
        const float dl = (d.level - lv) * (1.0f / kBlock);
        for (int i = 0; i < kBlock; i++)
        {
            lv += dl;
            outL[i] = (int16_t)(softclip(busL[i] * lv) * 32767.0f);
            outR[i] = (int16_t)(softclip(busR[i] * lv) * 32767.0f);
        }
        lvlPrev = d.level;
        return true;
    }

  private:
    enum { EV_ON, EV_OFF, EV_ALLOFF, EV_BEND, EV_CC };
    enum { E_N = 32 };
    struct Ev
    {
        uint8_t type, a, b;
        volatile uint8_t valid;
    };

    struct Derived
    {
        float kA, kD, kR, sus, glideK;
        float cutGain, res, noise;
        float detune, interval, vib, level;
        float sL, sR;
        uint32_t offTarget;
        bool stereo, pulse;
        // wavetable mode (Shape > 100): tables wtA0 / wtA1 (and the next ones) with the weights of the second table at the start
        // (wtW0) and at the end (wtW1) of the block. wtXF: the position crossed a table in this block (rare): the block renders the old
        // pair with wtW0 and the new one with wtW1 and crossfades.
        bool wt, wtXF;
        int wtA0, wtA1;
        float wtW0, wtW1;
    };

    Voice v[kMaxVoices];
    Ev q[E_N];
    volatile uint32_t head, tail;
    volatile uint32_t relPend[4]; // releases that found the queue full: one bit per note (32 bit atomics only on the Cortex-M4)
    volatile uint8_t allOffPend;
    uint32_t serial, rng;
    float lfoPhase, bend, mod, lvlPrev;
    float sCut, sRes, sNoise, sDet, sShape, sLevel;
    float sWt, wtPrev; // wavetable position (0 .. 16) smoothed, and the position at the end of the last block (< 0: not in the mode)
    bool smoothed;
    float busL[kBlock], busR[kBlock];
    TraceSample trace[kMaxVoices][kTraceLen];
    uint16_t traceHead;
    uint8_t traceDiv;

    // v0.3.6 (plan 014 A3): the trace is written every kTraceEvery rendered blocks (~35 ms); a voice that sounded in ANY block since
    // the last sample is "on" (latched in latchTrace), with the pitch / envelope of its last rendered block. Before, only the state at
    // the sample instant counted: a staccato chord stab (shorter than 35 ms of attack + release) or a note that ended before the next
    // sample was never visible, and the engine going idle wrote no closing sample.
    struct TraceLatch
    {
        float pitch;
        float env;
        uint8_t on;
    };
    TraceLatch trLatch[kMaxVoices];
    bool tracePending;
    bool traceOpen; // the last written sample had a sounding voice

    void latchTrace(int i, const Voice& vo, float vib)
    {
        if (vo.stage == ST_IDLE)
            return; // cleared in this block (its last block ramped down to zero): the line has ended
        TraceLatch& t = trLatch[i];
        t.pitch = vo.pitch + vib;
        t.env = vo.env;
        t.on = 1;
        tracePending = true;
    }

    void writeTrace()
    {
        const uint16_t h = traceHead;
        const int k = h & (kTraceLen - 1);
        bool open = false;
        for (int i = 0; i < kMaxVoices; i++)
        {
            TraceLatch& t = trLatch[i];
            open |= t.on != 0;
            float p = t.pitch * 32.0f;
            p = p > 32000.0f ? 32000.0f : (p < -32000.0f ? -32000.0f : p);
            float e = t.env * 255.0f;
            e = e > 255.0f ? 255.0f : (e < 0.0f ? 0.0f : e);
            TraceSample& s = trace[i][k];
            s.pitch = (int16_t)(p < 0 ? p - 0.5f : p + 0.5f);
            s.env = (uint8_t)e;
            s.on = t.on;
            t.on = 0;
        }
        traceOpen = open;
        __atomic_store_n(&traceHead, (uint16_t)(h + 1), __ATOMIC_RELEASE);
    }

    bool push(uint8_t type, uint8_t a, uint8_t b)
    {
        if (head - tail >= (uint32_t)(E_N - 2))
            return false;
        uint32_t i = __atomic_fetch_add(&head, 1u, __ATOMIC_ACQ_REL);
        Ev& e = q[i & (E_N - 1)];
        e.type = type;
        e.a = a;
        e.b = b;
        __atomic_store_n(&e.valid, (uint8_t)1, __ATOMIC_RELEASE);
        return true;
    }

    void drain()
    {
        while (tail != head)
        {
            Ev& e = q[tail & (E_N - 1)];
            if (!__atomic_load_n(&e.valid, __ATOMIC_ACQUIRE))
                break; // a producer is still writing it: next block
            const uint8_t type = e.type, a = e.a, b = e.b;
            e.valid = 0;
            tail = tail + 1;
            handle(type, a, b);
        }
        if (__atomic_exchange_n(&allOffPend, (uint8_t)0, __ATOMIC_ACQ_REL))
            handle(EV_ALLOFF, 0, 0);
        for (int w = 0; w < 4; w++)
        {
            uint32_t m = __atomic_exchange_n(&relPend[w], (uint32_t)0, __ATOMIC_ACQ_REL);
            for (int n = 0; m; n++, m >>= 1)
                if (m & 1u)
                    release((uint8_t)(w * 32 + n));
        }
    }

    __attribute__((optimize("Os"))) void handle(uint8_t type, uint8_t a, uint8_t b)
    {
        switch (type)
        {
            case EV_ON:
                if (b == 0)
                    release(a);
                else
                    start(a, b);
                break;
            case EV_OFF: release(a); break;
            case EV_ALLOFF:
                for (int i = 0; i < kMaxVoices; i++)
                    if (v[i].stage != ST_IDLE)
                    {
                        v[i].stage = ST_RELEASE;
                        v[i].gate = 0;
                    }
                break;
            case EV_BEND: bend = ((float)(((int)b << 7) | a) - 8192.0f) * (2.0f / 8192.0f); break;
            case EV_CC: cc(a, b); break;
            default: break;
        }
    }

    __attribute__((optimize("Os"))) void cc(uint8_t n, uint8_t val)
    {
        const int pct = (val * 100 + 63) / 127;
        switch (n)
        {
            case 1: mod = (float)val * (1.0f / 127.0f); break;
            case 5: ef[P_GLIDE] = clampParam(P_GLIDE, pct); break;
            case 7: ef[P_LEVEL] = clampParam(P_LEVEL, pct); break;
            case 70: ef[P_SHAPE] = clampParam(P_SHAPE, pct); break;
            case 71: ef[P_RESO] = clampParam(P_RESO, pct); break;
            case 72: ef[P_RELEASE] = clampParam(P_RELEASE, pct); break;
            case 73: ef[P_ATTACK] = clampParam(P_ATTACK, pct); break;
            case 74: ef[P_CUTOFF] = clampParam(P_CUTOFF, pct); break;
            case 75: ef[P_DECAY] = clampParam(P_DECAY, pct); break;
            case 76: ef[P_DETUNE] = clampParam(P_DETUNE, pct); break;
            case 77: ef[P_NOISE] = clampParam(P_NOISE, pct); break;
            case 91: ef[P_REVERB] = clampParam(P_REVERB, pct); break;
            case 93: ef[P_DELAY] = clampParam(P_DELAY, pct); break;
            case 120:
            case 123: handle(EV_ALLOFF, 0, 0); break;
            default: break;
        }
    }

    void release(uint8_t note)
    {
        for (int i = 0; i < kMaxVoices; i++)
            if (v[i].gate && v[i].note == note)
            {
                if (v[i].fresh)
                {
                    v[i].relPend = 1; // note-on and note-off in one block: it sounds for one block, then releases (else it would be silent)
                    continue;
                }
                v[i].gate = 0;
                v[i].stage = ST_RELEASE;
            }
    }

    // voice for a new note: the same note still held, else an idle one, else the quietest releasing one, else the oldest
    int pick(uint8_t note) const
    {
        const int n = ef[P_VOICES];
        for (int i = 0; i < n; i++)
            if (v[i].stage != ST_IDLE && v[i].gate && v[i].note == note)
                return i;
        for (int i = 0; i < n; i++)
            if (v[i].stage == ST_IDLE)
                return i;
        int best = 0;
        uint32_t bestKey = 0xFFFFFFFFu;
        for (int i = 0; i < n; i++)
        {
            uint32_t key = v[i].stage == ST_RELEASE ? (uint32_t)(v[i].env * 60000.0f) : 0x10000u + (v[i].serial & 0x7FFFFFFFu);
            if (key < bestKey)
            {
                bestKey = key;
                best = i;
            }
        }
        return best;
    }

    __attribute__((optimize("Os"))) void start(uint8_t note, uint8_t vel)
    {
        Voice& vo = v[pick(note)];
        const bool wasIdle = vo.stage == ST_IDLE;
        vo.note = note;
        vo.vel = 0.2f + 0.8f * (float)vel * (1.0f / 127.0f);
        vo.serial = ++serial;
        if (wasIdle)
        {
            vo.clear();
            vo.pitch = (float)note;
            vo.fresh = 1;
            rng = rng * 1664525u + 1013904223u;
            vo.s[0].ph = vo.s[1].ph = 0;
            vo.s[2].ph = rng;
            vo.s[3].ph = rng * 2654435761u;
            for (int i = 0; i < 4; i++)
                vo.s[i].prev = vo.s[i].ph;
        }
        vo.gate = 1;
        vo.stage = ST_ATTACK; // a steal or retrigger keeps level, phases and filter: the amplitude is continuous
    }

    static float lerpParam(float s, float t, float a) { return s + (t - s) * a; }

    __attribute__((optimize("Os"))) void derive(Derived& d)
    {
        const float tCut = (float)ef[P_CUTOFF], tRes = (float)ef[P_RESO], tNoise = (float)ef[P_NOISE], tDet = (float)ef[P_DETUNE],
                    tLevel = (float)ef[P_LEVEL];
        if (!smoothed)
        {
            sCut = tCut;
            sRes = tRes;
            sNoise = tNoise;
            sDet = tDet;
            sLevel = tLevel;
            sShape = (float)ef[P_SHAPE];
            smoothed = true;
        }
        const float a = 0.35f; // block rate one pole: ~12 ms, then ramped linearly across each block
        sCut = lerpParam(sCut, tCut, a);
        sRes = lerpParam(sRes, tRes, a);
        sNoise = lerpParam(sNoise, tNoise, a);
        sDet = lerpParam(sDet, tDet, a);
        sLevel = lerpParam(sLevel, tLevel, a);
        sShape = lerpParam(sShape, (float)ef[P_SHAPE], a);

        // shared LFO (parabolic sine), rate 0.1 .. 19 Hz
        lfoPhase += 0.1f * exp2fast((float)ef[P_LFO_RATE] * 0.076f) * kBlockSec;
        if (lfoPhase >= 1.0f)
            lfoPhase -= 1.0f;
        const float x = lfoPhase * 2.0f - 1.0f;
        float lfo = 4.0f * x * (1.0f - (x < 0 ? -x : x));
        float depth = (float)ef[P_LFO_DEPTH] * 0.01f;
        if (mod > depth)
            depth = mod;
        lfo *= depth;
        const uint8_t dest = ef[P_LFO_DEST];

        d.vib = (dest == 0 ? lfo * 2.0f : 0.0f) + bend;
        const float cutMod = dest == 2 ? exp2fast(lfo * 3.0f) : 1.0f;
        d.cutGain = exp2fast((sCut - 60.0f) * 0.08f) * cutMod;
        const float r = sRes * 0.01f;
        d.res = 2.0f * r * r;
        const float nz = sNoise * 0.01f;
        d.noise = nz * nz;
        d.detune = sDet * 0.0012f;
        d.interval = (float)((int)ef[P_INTERVAL] - 12);
        const float lv = sLevel * 0.01f;
        d.level = lv * lv * 1.5f * (1.0f + (float)ef[P_DRIVE] * 0.03f);

        // envelope: per block one pole coefficients from the times (attack 1 ms .. 2 s, decay / release 5 ms .. 10 s)
        d.kA = 1.0f - exp2fast(-kBlockSec * 1.4427f * 2.4f / (0.001f * exp2fast((float)ef[P_ATTACK] * 0.11f)));
        d.kD = 1.0f - exp2fast(-kBlockSec * 1.4427f * 4.6f / (0.005f * exp2fast((float)ef[P_DECAY] * 0.11f)));
        d.kR = 1.0f - exp2fast(-kBlockSec * 1.4427f * 4.6f / (0.005f * exp2fast((float)ef[P_RELEASE] * 0.11f)));
        d.sus = (float)ef[P_SUSTAIN] * 0.01f;
        d.sus *= d.sus;
        d.glideK = ef[P_GLIDE] == 0 ? 1.0f : 1.0f - exp2fast(-kBlockSec * 1.4427f / (0.005f * exp2fast((float)ef[P_GLIDE] * 0.1f)));

        d.stereo = ef[P_WIDTH] != 0;
        const float w = (float)ef[P_WIDTH] * 0.01f;
        d.sR = 0.5f * (1.0f - w); // share of the opposite side
        d.sL = 1.0f - d.sR;
        d.wt = ef[P_SHAPE] > kShapePulseMax;
        d.pulse = ef[P_SHAPE] != 0 && !d.wt;
        float sh = sShape;
        if (dest == 1 && d.pulse)
            sh += lfo * 50.0f;
        if (sh < 1.0f)
            sh = 1.0f;
        if (sh > 100.0f)
            sh = 100.0f;
        // pulse width 50% .. 2%
        d.offTarget = (uint32_t)((0.5f - 0.48f * (sh - 1.0f) * (1.0f / 99.0f)) * 4294967040.0f);

        // wavetable position in tables (0 .. 16): the parameter smoothed like the others (a jump into the mode starts at the target), the
        // LFO (target Shape) swings it by +-4 tables at full depth
        d.wtXF = false;
        d.wtA0 = d.wtA1 = 0;
        d.wtW0 = d.wtW1 = 0.0f;
        if (d.wt)
        {
            const float tgt = (float)((int)ef[P_SHAPE] - kShapeWt0) * (1.0f / kWtSteps);
            sWt = wtPrev < 0.0f ? tgt : lerpParam(sWt, tgt, a);
            float pos = sWt + (dest == 1 ? lfo * 4.0f : 0.0f);
            const float top = (float)(kWtTables - 1);
            pos = pos < 0.0f ? 0.0f : (pos > top ? top : pos);
            const float pos0 = wtPrev < 0.0f ? pos : wtPrev;
            wtPrev = pos;
            // a pair is (table A, A + 1) and a weight 0 .. 1: the last table is A = 15 with weight 1
            int a1 = (int)pos;
            if (a1 > kWtTables - 2)
                a1 = kWtTables - 2;
            int a0 = (int)pos0;
            if (a0 > kWtTables - 2)
                a0 = kWtTables - 2;
            d.wtA1 = a1;
            d.wtW1 = pos - (float)a1;
            d.wtA0 = a0;
            d.wtW0 = pos0 - (float)a0;
            if (a0 == a1)
                d.wtXF = false; // the weight ramps from wtW0 to wtW1 inside the pair
            else
                d.wtXF = true;
        }
        else
            wtPrev = -1.0f;
    }

    void renderVoice(Voice& vo, const Derived& d)
    {
        // both are per block constants: a predictable branch per sample instead of four copies of the loop (flash is tight)
        const bool ST = d.stereo, PU = d.pulse, WT = d.wt;
        if (vo.stage == ST_RELEASE && vo.env == 0.0f)
        {
            vo.clear(); // the last block ramped down to zero
            return;
        }
        switch (vo.stage)
        {
            case ST_ATTACK:
                vo.env += (1.15f - vo.env) * d.kA;
                if (vo.env >= 1.0f)
                {
                    vo.env = 1.0f;
                    vo.stage = ST_DECAY;
                }
                break;
            case ST_DECAY:
                vo.env += (d.sus - vo.env) * d.kD;
                if (vo.env < 0.0005f && d.sus < 0.0005f)
                {
                    vo.env = 0.0f; // sustain 0: the note has rung out
                    vo.stage = ST_RELEASE;
                    vo.gate = 0;
                }
                break;
            default:
                vo.env -= vo.env * d.kR;
                if (vo.env < 0.0005f)
                    vo.env = 0.0f;
                break;
        }
        vo.pitch += ((float)vo.note - vo.pitch) * d.glideK;
        if (d.glideK >= 1.0f)
            vo.pitch = (float)vo.note;

        // targets
        const float base = vo.pitch + d.vib;
        if (PU || WT)
        {
            vo.s[0].tdph = semiToDphase(base - d.detune);
            vo.s[1].tdph = semiToDphase(base + d.interval);
        }
        else
        {
            vo.s[0].tdph = semiToDphase(base - 2.0f * d.detune);
            vo.s[2].tdph = semiToDphase(base);
            if (ST)
            {
                vo.s[1].tdph = semiToDphase(base - d.detune + d.interval);
                vo.s[3].tdph = semiToDphase(base + d.detune + d.interval);
            }
        }
        if (vo.fresh)
        {
            for (int i = 0; i < 4; i++)
                vo.s[i].dph = vo.s[i].tdph;
            vo.off = d.offTarget;
            vo.fresh = 0;
        }
        const float a = vo.vel * vo.env;
        const float gT = 0.35f * a;
        float kT = a * a * d.cutGain;
        if (kT > 1.0f)
            kT = 1.0f;
        float g = vo.g, k = vo.k;
        const float dg = (gT - g) * (1.0f / kBlock), dk = (kT - k) * (1.0f / kBlock);
        const float res = d.res, nz = d.noise;

        uint32_t ph0 = vo.s[0].ph, ph1 = vo.s[1].ph, ph2 = vo.s[2].ph, ph3 = vo.s[3].ph;
        uint32_t pv0 = vo.s[0].prev, pv1 = vo.s[1].prev, pv2 = vo.s[2].prev, pv3 = vo.s[3].prev;
        int32_t dp0 = vo.s[0].dph, dp1 = vo.s[1].dph, dp2 = vo.s[2].dph, dp3 = vo.s[3].dph;
        const int32_t dd0 = (vo.s[0].tdph - dp0) >> 7, dd1 = (vo.s[1].tdph - dp1) >> 7, dd2 = (vo.s[2].tdph - dp2) >> 7,
                      dd3 = (vo.s[3].tdph - dp3) >> 7;
        uint32_t off = vo.off;
        const int32_t doff = (int32_t)(d.offTarget - off) >> 7;
        if (!WT && vo.wtOn)
        {
            // back from the wavetable mode: the delayed saw samples are stale (a click); restart them at the phases
            pv0 = ph0;
            pv1 = ph1;
            pv2 = PU ? ph0 + off : ph2;
            pv3 = PU ? ph1 + off : ph3;
        }
        vo.wtOn = WT;
        // wavetable mode: tables of the octave level of the louder end of the block's pitch glide (left: oscillator 0, right: oscillator 1)
        const int16_t *tL0 = nullptr, *tL1 = nullptr, *tR0 = nullptr, *tR1 = nullptr;
        int shL = 7, shR = 7;
        float wL = 0.0f, dwW = 0.0f, xfU = 0.0f, dxfU = 0.0f, w1c = 0.0f;
        if (WT)
        {
            const int lvL = wtLevel(dp0 > vo.s[0].tdph ? dp0 : vo.s[0].tdph), lvR = wtLevel(dp1 > vo.s[1].tdph ? dp1 : vo.s[1].tdph);
            shL = lvL + 7;
            shR = lvR + 7;
            tL0 = wtTable(d.wtA0, lvL);
            tR0 = wtTable(d.wtA0, lvR);
            wL = d.wtW0;
            if (d.wtXF)
            {
                tL1 = wtTable(d.wtA1, lvL);
                tR1 = wtTable(d.wtA1, lvR);
                w1c = d.wtW1;
                dxfU = 1.0f / kBlock;
            }
            else
                dwW = (d.wtW1 - d.wtW0) * (1.0f / kBlock);
        }
        float y1L = vo.y[0], y2L = vo.y[1], y1R = vo.y[2], y2R = vo.y[3];
        uint32_t r = rng;
        float* oL = busL;
        float* oR = busR;

        for (int i = 0; i < kBlock; i++)
        {
            g += dg;
            k += dk;
            r = r * 1664525u + 1013904223u;
            const float n = (float)(int32_t)r * (1.0f / 2147483648.0f) * nz;
            float xL, xR = 0.0f;
            if (WT)
            {
                dp0 += dd0;
                ph0 += (uint32_t)dp0;
                xL = wtSample(tL0, ph0, shL, wL);
                if (ST)
                {
                    dp1 += dd1;
                    ph1 += (uint32_t)dp1;
                    xR = wtSample(tR0, ph1, shR, wL);
                }
                if (tL1)
                {
                    xfU += dxfU;
                    xL += (wtSample(tL1, ph0, shL, w1c) - xL) * xfU;
                    if (ST)
                        xR += (wtSample(tR1, ph1, shR, w1c) - xR) * xfU;
                }
                else
                    wL += dwW;
            }
            else if (PU)
            {
                off += (uint32_t)doff;
                dp0 += dd0;
                ph0 += (uint32_t)dp0;
                xL = blepSaw(ph0, (uint32_t)dp0, pv0) - blepSaw(ph0 + off, (uint32_t)dp0, pv2);
                if (ST)
                {
                    dp1 += dd1;
                    ph1 += (uint32_t)dp1;
                    xR = blepSaw(ph1, (uint32_t)dp1, pv1) - blepSaw(ph1 + off, (uint32_t)dp1, pv3);
                }
            }
            else
            {
                dp0 += dd0;
                ph0 += (uint32_t)dp0;
                dp2 += dd2;
                ph2 += (uint32_t)dp2;
                xL = blepSaw(ph0, (uint32_t)dp0, pv0) + blepSaw(ph2, (uint32_t)dp2, pv2);
                if (ST)
                {
                    dp1 += dd1;
                    ph1 += (uint32_t)dp1;
                    dp3 += dd3;
                    ph3 += (uint32_t)dp3;
                    xR = blepSaw(ph1, (uint32_t)dp1, pv1) + blepSaw(ph3, (uint32_t)dp3, pv3);
                }
            }
            xL = (xL + n) * g;
            y1L += (xL - (y2L - y1L) * res - y1L) * k;
            y1L *= 0.999f;
            y2L += (y1L - y2L) * k;
            y2L *= 0.999f;
            if (ST)
            {
                xR = (xR + n) * g;
                y1R += (xR - (y2R - y1R) * res - y1R) * k;
                y1R *= 0.999f;
                y2R += (y1R - y2R) * k;
                y2R *= 0.999f;
                oL[i] += y2L * d.sL + y2R * d.sR;
                oR[i] += y2R * d.sL + y2L * d.sR;
            }
            else
            {
                oL[i] += y2L;
                oR[i] += y2L;
            }
        }
        rng = r;
        vo.s[0].ph = ph0;
        vo.s[1].ph = ph1;
        vo.s[2].ph = ph2;
        vo.s[3].ph = ph3;
        vo.s[0].prev = pv0;
        vo.s[1].prev = pv1;
        vo.s[2].prev = pv2;
        vo.s[3].prev = pv3;
        for (int i = 0; i < 4; i++)
            vo.s[i].dph = vo.s[i].tdph;
        vo.off = d.offTarget;
        vo.g = gT;
        vo.k = kT;
        const float lim = 1.0e4f; // a corrupt state (never seen) is cleared instead of ringing forever
        if (!(y1L > -lim && y1L < lim && y2L > -lim && y2L < lim && y1R > -lim && y1R < lim && y2R > -lim && y2R < lim))
            y1L = y2L = y1R = y2R = 0.0f;
        vo.y[0] = y1L;
        vo.y[1] = y2L;
        vo.y[2] = y1R;
        vo.y[3] = y2R;
    }
};

}  // namespace plinkyvoice
