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
#include "plinky_governor.h"
#include "plinky_params.h"
#include "plinky_wavetable.h"
#include <string.h>

namespace plinkyvoice {

constexpr int kMaxVoices = 4;  // per instance (the Voices limit)
constexpr int kChLen = 1024; // chorus line length per side (power of two: 23 ms at 44.1 kHz)
constexpr int kPool = 8;       // voices of all instances together (kInstances is in plinky_params.h)
constexpr int kBlock = 128;
constexpr float kFs = 44100.0f;
constexpr float kBlockSec = (float)kBlock / kFs;

// Pitch trace (plan 013 V): per voice a ring of (pitch, envelope) written at control rate (every kTraceEvery blocks = 5.8 ms since
// v0.3.7, plan 015 V; was 12 blocks), read by the main loop (interface/seqPlay/pitchTrace.cpp), which keeps its own 3.9 s window. The
// reader copies samples behind the head only, so a torn read can at worst show one wrong point; nothing here is ever waited for.
constexpr int kTraceLen = 128;     // power of two; 743 ms of samples, the reader runs every ~40 ms
constexpr int kTraceEvery = 2;     // blocks per sample
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

// LFO waveform at phase 0 .. 1, -1 .. +1 (before the depth). 0: the parabolic sine of v0.3.x (starts at 0, goes down first), 1 triangle (starts at
// the bottom), 2 saw up, 3 saw down, 4 square (high first), 5 sample & hold (held = the value drawn at the last wrap), 6 smooth random
// (a smoothstep from the previous to the current random value over the cycle)
inline float lfoShape(int wave, float phase, float prev, float held)
{
    const float x = phase * 2.0f - 1.0f;
    const float ax = x < 0 ? -x : x;
    switch (wave)
    {
        case 1: return 1.0f - 2.0f * ax;
        case 2: return x;
        case 3: return -x;
        case 4: return phase < 0.5f ? 1.0f : -1.0f;
        case 5: return held;
        case 6: return prev + (held - prev) * (phase * phase * (3.0f - 2.0f * phase));
        default: return 4.0f * x * (1.0f - ax);
    }
}

// tan(x) for x < 1.1 (Pade 3/4, error under 1 percent), the SVF and ladder coefficient g = tan(pi fc / fs)
inline float tanPade(float x)
{
    const float x2 = x * x;
    return x * (105.0f - 10.0f * x2) / (105.0f - 45.0f * x2 + x2 * x2);
}

// Plan 019 B filters (own implementation of the textbook topology-preserving transform, Zavalishin / Simper):
// state variable filter, one sample. s[0], s[1] = the two integrator states; coefficients a1 = 1 / (1 + g (g + k)), a2 = g a1, a3 = g a2,
// k = damping (2 .. 0.04). mode 1 = low pass, 2 = band pass (peak gain 1), 3 = high pass.
inline float svfStep(float x, float* s, float a1, float a2, float a3, float k, int mode)
{
    const float v3 = x - s[1];
    const float v1 = a1 * s[0] + a2 * v3;
    const float v2 = s[1] + a2 * s[0] + a3 * v3;
    s[0] = 2.0f * v1 - s[0];
    s[1] = 2.0f * v2 - s[1];
    return mode == 1 ? v2 : (mode == 2 ? k * v1 : x - k * v1 - v2);
}
// 4 pole ladder low pass, zero delay feedback solved in closed form (linear), soft clip on the loop input. G = g / (1 + g),
// G2..G3 its powers, invD = 1 / (1 + kf G^4), kf = feedback 0 .. 3.9.
inline float ladderStep(float x, float* s, float G, float G2, float G3, float invD, float kf)
{
    const float S = (1.0f - G) * (G3 * s[0] + G2 * s[1] + G * s[2] + s[3]);
    float u = (x - kf * S) * invD;
    u = softclip(u * 0.5f) * 2.0f;
    float v = (u - s[0]) * G, y = v + s[0];
    s[0] = y + v;
    v = (y - s[1]) * G;
    y = v + s[1];
    s[1] = y + v;
    v = (y - s[2]) * G;
    y = v + s[2];
    s[2] = y + v;
    v = (y - s[3]) * G;
    y = v + s[3];
    s[3] = y + v;
    return y;
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
    uint8_t inst = 0;     // the instance (synth) that owns the voice
    uint8_t kill = 0;     // the governor took the voice: it fades out within the next block and is freed
    uint8_t wtOn = 0;     // the last block was rendered by the wavetable oscillator (the saws' delayed samples are stale then)
    // plan 019 B: sub oscillators (two squares, each a saw pair), filter envelope, SVF / ladder states
    float fenv = 0;       // filter envelope level
    float fg = 0;         // SVF / ladder coefficient g at the end of the last block (0: none yet)
    float sg1 = 0, sg2 = 0; // sub levels at the end of the last block
    float fs[8];          // filter states of the SVF / ladder: left 0..3, right 4..7
    uint32_t sph[2], spv[4];
    uint8_t fatk = 0, ftype = 0, subOn = 0;
    Voice()
    {
        memset(y, 0, sizeof(y));
        memset(fs, 0, sizeof(fs));
        memset(sph, 0, sizeof(sph));
        memset(spv, 0, sizeof(spv));
    }
    void clear()
    {
        memset(y, 0, sizeof(y));
        env = g = k = 0;
        memset(fs, 0, sizeof(fs));
        fenv = fg = sg1 = sg2 = 0;
        fatk = ftype = subOn = 0;
        stage = ST_IDLE;
        gate = 0;
        relPend = 0;
        kill = 0;
    }
};

// One of the kInstances synths: its own parameters and the control rate state (smoothing, LFO, bend) the voices of the instance share.
struct Inst
{
    uint8_t p[P_COUNT];  // the saved values (written by the UI only)
    uint8_t ef[P_COUNT]; // the effective values the DSP reads (MIDI CCs move these)
    float lfoPhase, bend, mod, lvlPrev;
    float sCut, sRes, sNoise, sDet, sShape, sLevel;
    float sWt, wtPrev; // wavetable position (0 .. 16) smoothed, and the position at the end of the last block (< 0: not in the mode)
    bool smoothed;
    // plan 019 B: LFO random shapes, chorus
    float lfoHeld, lfoPrev;
    float vibNow;      // v0.4.1: LFO / bend pitch offset of the last rendered block (semitones), read by the pitch view (liveVoice)
    uint32_t lfoRng;
    float chPhase;
    int16_t* chMem;    // 2 * kChLen samples (left line, right line), raw storage owned by the caller; null = no chorus
    uint16_t chPos;
    bool chStale;      // the lines hold old audio (the instance was idle): cleared before the next use
    void init(int idx)
    {
        for (int i = 0; i < P_COUNT; i++)
            p[i] = ef[i] = paramInfo(i).def;
        p[P_CHANNEL] = ef[P_CHANNEL] = defaultChannel(idx);
        lfoPhase = bend = mod = lvlPrev = 0;
        sCut = sRes = sNoise = sDet = sShape = sLevel = 0;
        sWt = 0;
        wtPrev = -1.0f;
        smoothed = false;
        lfoHeld = lfoPrev = 0;
        vibNow = 0;
        lfoRng = 0x2545F491u + (uint32_t)idx * 7919u;
        chPhase = 0;
        chMem = nullptr;
        chPos = 0;
        chStale = true;
    }
};

// Output blocks of one render: dry left / right and the reverb / delay sends (already scaled by each instance's send level; the
// mixers take them at unity). A null send pointer = not wanted.
struct Out
{
    int16_t *dryL, *dryR, *revL, *revR, *dlyL, *dlyR;
};

class Engine
{
  public:
    Engine() { init(); }

    void init()
    {
        for (int k = 0; k < kInstances; k++)
            in[k].init(k);
        for (int k = 0; k < kInstances; k++)
            for (int i = 0; i < 4; i++)
                relPend[k][i] = 0;
        allOffPend = 0;
        for (int i = 0; i < kPool; i++)
            v[i] = Voice();
        head = 0;
        tail = 0;
        serial = 0;
        rng = 0x1234567u;
        for (int i = 0; i < E_N; i++)
            q[i].valid = 0;
        memset(trace, 0, sizeof(trace));
        traceHead = 0;
        traceDiv = 0;
        tracePending = false;
        traceOpen = false;
        memset(trLatch, 0, sizeof(trLatch));
        gov.reset();
        load = 0;
    }

    // ---- any context (the sequencer runs in an interrupt, pads and the UI on the main loop) ----
    // inst = 0 .. kInstances - 1 (the synth the message is for); the default 0 keeps single synth callers unchanged.
    bool noteOn(uint8_t note, uint8_t vel, int inst = 0) { return push(EV_ON, note, vel, inst); }
    // A release that does not fit into the queue is remembered (one bit per note and instance) and applied by the next block: a voice never sticks.
    bool noteOff(uint8_t note, int inst = 0)
    {
        if (push(EV_OFF, note, 0, inst))
            return true;
        __atomic_fetch_or(&relPend[inst][(note >> 5) & 3], (uint32_t)1 << (note & 31), __ATOMIC_RELEASE);
        return true;
    }
    // all notes of the instance (inst < 0: every instance)
    bool allNotesOff(int inst = 0)
    {
        if (inst < 0)
        {
            bool ok = true;
            for (int k = 0; k < kInstances; k++)
                ok &= allNotesOff(k);
            return ok;
        }
        if (!push(EV_ALLOFF, 0, 0, inst))
            __atomic_fetch_or(&allOffPend, (uint8_t)(1u << inst), __ATOMIC_RELEASE);
        return true;
    }
    // 0 .. 16383, 8192 = centre
    bool pitchBend(int16_t value14, int inst = 0) { return push(EV_BEND, (uint8_t)(value14 & 0x7F), (uint8_t)((value14 >> 7) & 0x7F), inst); }
    bool controlChange(uint8_t cc, uint8_t val, int inst = 0) { return push(EV_CC, cc, val, inst); }

    // The saved values p[] are written by the UI only (setParam). MIDI CCs change the effective values ef[] the DSP reads, like CC1 (mod
    // wheel): the saved patch is never overwritten by automation; moving the parameter in the UI takes the effective value back.
    void setParam(int i, int value, int inst = 0)
    {
        if (i >= 0 && i < P_COUNT && inst >= 0 && inst < kInstances)
            in[inst].p[i] = in[inst].ef[i] = clampParam(i, value);
    }
    uint8_t param(int i, int inst = 0) const { return in[inst].p[i]; }
    uint8_t effective(int i, int inst = 0) const { return in[inst].ef[i]; }
    // plan 019 B: the chorus delay lines of an instance (2 * kChLen int16, left then right) live in memory the caller owns (EXTERNAL_RAM on the
    // device); without it the instance has no chorus. Main loop / init only.
    void setChorusMemory(int inst, int16_t* mem)
    {
        if (inst >= 0 && inst < kInstances)
        {
            in[inst].chMem = mem;
            in[inst].chStale = true;
        }
    }
    // the CPU governor needs the load of the previous audio block (permille of the block period); 0 = not measured
    void setBlockLoad(uint32_t permille) { load = permille; }
    // plan 022 P2: what one sounding voice costs (permille of the block, measured by the synth); 0 = unknown (the governor assumes a default)
    void setVoiceCost(uint32_t permille) { voiceCost = permille; }
    int voiceCap() const { return gov.cap; }
    void resetGovernor() { gov.reset(); }
    Governor& governor() { return gov; }
    // some instance has a send level: the stream allocates the send blocks
    bool wantSend(bool delay) const
    {
        for (int k = 0; k < kInstances; k++)
            if (in[k].ef[delay ? P_DELAY : P_REVERB] > 0)
                return true;
        return false;
    }

    // ---- audio context ----
    bool isActive() const
    {
        if (head != tail)
            return true;
        for (int i = 0; i < kPool; i++)
            if (v[i].stage != ST_IDLE)
                return true;
        return false;
    }
    int activeVoices() const
    {
        int n = 0;
        for (int i = 0; i < kPool; i++)
            n += v[i].stage != ST_IDLE;
        return n;
    }
    int activeVoices(int inst) const
    {
        int n = 0;
        for (int i = 0; i < kPool; i++)
            n += v[i].stage != ST_IDLE && v[i].inst == inst;
        return n;
    }
    const Voice& voice(int i) const { return v[i]; }
    // v0.4.1 (pitch view): the live state of pool voice i, plain reads from any context (a torn read shows at worst one wrong point):
    // sounding, its instance, pitch in semitones (base note + glide position + the instance's LFO / bend offset), envelope 0..1, and the
    // serial of its last note start (changes with every note on that took the voice: a note that began and ended between two reads)
    void liveVoice(int i, bool& on, uint8_t& inst, float& pitch, float& env, uint32_t& serial) const
    {
        const Voice& vo = v[i];
        on = vo.stage != ST_IDLE;
        inst = vo.inst < kInstances ? vo.inst : 0;
        pitch = vo.pitch + in[inst].vibNow;
        env = vo.env;
        serial = vo.serial;
    }
    // pitch trace ring (any context, read only): sample k of voice i is trace[i][k & (kTraceLen - 1)], the newest is head - 1.
    // TraceSample.on: bit 0 = sounds, bits 1 and 2 = the instance of the voice at that time.
    const TraceSample* traceRing(int i) const { return trace[i]; }
    uint16_t traceHeadIndex() const { return __atomic_load_n(&traceHead, __ATOMIC_ACQUIRE); }

    // Renders one block of kBlock samples. Returns false (outputs untouched) when nothing sounds. The output blocks must be
    // writable for kBlock samples; they are overwritten.
    bool render(const Out& o)
    {
        drain();
        bool any = false;
        for (int i = 0; i < kPool; i++)
            any |= v[i].stage != ST_IDLE;
        if (!any)
        {
            for (int k = 0; k < kInstances; k++)
            {
                in[k].lvlPrev = 0;
                in[k].chStale = true;
            }
            gov.update(load, 0);
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
        // plan 019 A: the governor (measured load) and the pool cap: voices over the cap fade out within this block
        enforceCap(gov.update(load, activeVoices(), voiceCost));
        memset(o.dryL, 0, kBlock * sizeof(int16_t));
        memset(o.dryR, 0, kBlock * sizeof(int16_t));
        if (o.revL)
        {
            memset(o.revL, 0, kBlock * sizeof(int16_t));
            memset(o.revR, 0, kBlock * sizeof(int16_t));
        }
        if (o.dlyL)
        {
            memset(o.dlyL, 0, kBlock * sizeof(int16_t));
            memset(o.dlyR, 0, kBlock * sizeof(int16_t));
        }
        for (int k = 0; k < kInstances; k++)
        {
            Inst& I = in[k];
            // the instance's voices beyond its Voices limit release (the first ones in pool order stay)
            int rank = 0, cnt = 0;
            for (int i = 0; i < kPool; i++)
                if (v[i].inst == k && v[i].stage != ST_IDLE)
                {
                    cnt++;
                    if (rank++ >= (int)I.ef[P_VOICES] && v[i].stage != ST_RELEASE)
                    {
                        v[i].stage = ST_RELEASE;
                        v[i].gate = 0;
                    }
                }
            if (!cnt)
            {
                I.lvlPrev = 0;
                I.chStale = true;
                continue;
            }
            Derived d;
            derive(I, d);
            I.vibNow = d.vib;
            memset(busL, 0, sizeof(busL));
            memset(busR, 0, sizeof(busR));
            for (int i = 0; i < kPool; i++)
            {
                Voice& vo = v[i];
                if (vo.stage == ST_IDLE || vo.inst != k)
                    continue;
                renderVoice(vo, d);
                latchTrace(i, vo, d.vib);
                if (vo.relPend && !vo.fresh && vo.stage != ST_IDLE)
                {
                    vo.relPend = 0;
                    vo.gate = 0;
                    vo.stage = ST_RELEASE;
                }
            }
            float lv = I.lvlPrev;
            const float dl = (d.level - lv) * (1.0f / kBlock);
            const float sRev = 0.5f * (float)I.ef[P_REVERB] * 0.01f, sDly = 0.5f * (float)I.ef[P_DELAY] * 0.01f;
            const bool doRev = o.revL && sRev > 0.0f, doDly = o.dlyL && sDly > 0.0f;
            const bool chorus = I.chMem && I.ef[P_CH_MIX] > 0;
            if (chorus && I.chStale)
            {
                memset(I.chMem, 0, 2 * kChLen * sizeof(int16_t));
                I.chPos = 0;
                I.chStale = false;
            }
            const float chMix = (float)I.ef[P_CH_MIX] * 0.005f; // 0 .. 0.5 wet share
            const float chDph = 0.2f * exp2fast((float)I.ef[P_CH_RATE] * 0.05f) * (1.0f / kFs);
            const float chDepth = (float)I.ef[P_CH_DEPTH] * 2.2f; // +- 220 samples (5 ms) at full depth
            for (int i = 0; i < kBlock; i++)
            {
                lv += dl;
                float yl = softclip(busL[i] * lv) * 32767.0f, yr = softclip(busR[i] * lv) * 32767.0f;
                if (chorus)
                {   // modulated delay per side (triangle LFOs half a cycle apart), linear interpolation, 10 ms centre
                    I.chPhase += chDph;
                    if (I.chPhase >= 1.0f)
                        I.chPhase -= 1.0f;
                    float pl = I.chPhase, pr = I.chPhase + 0.5f;
                    if (pr >= 1.0f)
                        pr -= 1.0f;
                    const float tl = (pl < 0.5f ? pl : 1.0f - pl) * 4.0f - 1.0f, tr = (pr < 0.5f ? pr : 1.0f - pr) * 4.0f - 1.0f;
                    I.chMem[I.chPos & (kChLen - 1)] = (int16_t)yl;
                    I.chMem[kChLen + (I.chPos & (kChLen - 1))] = (int16_t)yr;
                    const float dL = 441.0f + chDepth * tl, dR = 441.0f + chDepth * tr;
                    const int iL = (int)dL, iR = (int)dR;
                    const float fL = dL - (float)iL, fR = dR - (float)iR;
                    const int16_t* lineL = I.chMem;
                    const int16_t* lineR = I.chMem + kChLen;
                    const float wl = (float)lineL[(I.chPos - iL) & (kChLen - 1)] +
                                     ((float)lineL[(I.chPos - iL - 1) & (kChLen - 1)] - (float)lineL[(I.chPos - iL) & (kChLen - 1)]) * fL;
                    const float wr = (float)lineR[(I.chPos - iR) & (kChLen - 1)] +
                                     ((float)lineR[(I.chPos - iR - 1) & (kChLen - 1)] - (float)lineR[(I.chPos - iR) & (kChLen - 1)]) * fR;
                    I.chPos++;
                    yl += (wl - yl) * chMix;
                    yr += (wr - yr) * chMix;
                }
                o.dryL[i] = (int16_t)sat16(o.dryL[i] + (int)yl);
                o.dryR[i] = (int16_t)sat16(o.dryR[i] + (int)yr);
                if (doRev)
                {
                    o.revL[i] = (int16_t)sat16(o.revL[i] + (int)(yl * sRev));
                    o.revR[i] = (int16_t)sat16(o.revR[i] + (int)(yr * sRev));
                }
                if (doDly)
                {
                    o.dlyL[i] = (int16_t)sat16(o.dlyL[i] + (int)(yl * sDly));
                    o.dlyR[i] = (int16_t)sat16(o.dlyR[i] + (int)(yr * sDly));
                }
            }
            I.lvlPrev = d.level;
        }
        if (++traceDiv >= kTraceEvery)
        {
            traceDiv = 0;
            tracePending = false;
            writeTrace();
        }
        return true;
    }
    // single synth callers (tests): dry only
    bool render(int16_t* outL, int16_t* outR)
    {
        Out o = { outL, outR, nullptr, nullptr, nullptr, nullptr };
        return render(o);
    }

  private:
    static int sat16(int x) { return x > 32767 ? 32767 : (x < -32768 ? -32768 : x); }
    enum { EV_ON, EV_OFF, EV_ALLOFF, EV_BEND, EV_CC };
    enum { E_N = 32 };
    struct Ev
    {
        uint8_t type, a, b; // type = event | instance << 4
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
        // plan 019 B
        uint8_t ft;                  // filter type (0 low pass gate)
        bool sub, fenvOn;
        float subG1, subG2;          // sub oscillator levels
        float famt, kFA, kFD, kFR, fsus; // filter envelope: octaves, one pole coefficients, sustain level
        float svfOct, svfK, ladK;    // log2(fc / 1300 Hz) before the envelope, SVF damping, ladder feedback
    };

    Voice v[kPool];
    Inst in[kInstances];
    Governor gov;
    uint32_t voiceCost = 0;
    uint32_t load;
    Ev q[E_N];
    volatile uint32_t head, tail;
    volatile uint32_t relPend[kInstances][4]; // releases that found the queue full: one bit per note (32 bit atomics only on the Cortex-M4)
    volatile uint8_t allOffPend;              // bit per instance
    uint32_t serial, rng;
    float busL[kBlock], busR[kBlock];
    TraceSample trace[kPool][kTraceLen];
    uint16_t traceHead;
    uint8_t traceDiv;

    // v0.3.6 (plan 014 A3): the trace is written every kTraceEvery rendered blocks (~5.8 ms since v0.3.7); a voice that sounded in ANY block since
    // the last sample is "on" (latched in latchTrace), with the pitch / envelope of its last rendered block. Before, only the state at
    // the sample instant counted: a staccato chord stab (shorter than 35 ms of attack + release) or a note that ended before the next
    // sample was never visible, and the engine going idle wrote no closing sample.
    struct TraceLatch
    {
        float pitch;
        float env;
        uint8_t on;
        uint8_t inst;
    };
    TraceLatch trLatch[kPool];
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
        t.inst = vo.inst;
        tracePending = true;
    }

    void writeTrace()
    {
        const uint16_t h = traceHead;
        const int k = h & (kTraceLen - 1);
        bool open = false;
        for (int i = 0; i < kPool; i++)
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
            s.on = t.on ? (uint8_t)(1 | (t.inst << 1)) : 0;
            t.on = 0;
        }
        traceOpen = open;
        __atomic_store_n(&traceHead, (uint16_t)(h + 1), __ATOMIC_RELEASE);
    }

    bool push(uint8_t type, uint8_t a, uint8_t b, int inst)
    {
        if (inst < 0 || inst >= kInstances)
            return true; // nothing to do for it
        if (head - tail >= (uint32_t)(E_N - 2))
            return false;
        uint32_t i = __atomic_fetch_add(&head, 1u, __ATOMIC_ACQ_REL);
        Ev& e = q[i & (E_N - 1)];
        e.type = (uint8_t)(type | (inst << 4));
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
            handle(type & 15, type >> 4, a, b);
        }
        const uint8_t ao = __atomic_exchange_n(&allOffPend, (uint8_t)0, __ATOMIC_ACQ_REL);
        for (int k = 0; k < kInstances; k++)
        {
            if ((ao >> k) & 1u)
                handle(EV_ALLOFF, k, 0, 0);
            for (int w = 0; w < 4; w++)
            {
                uint32_t m = __atomic_exchange_n(&relPend[k][w], (uint32_t)0, __ATOMIC_ACQ_REL);
                for (int n = 0; m; n++, m >>= 1)
                    if (m & 1u)
                        release(k, (uint8_t)(w * 32 + n));
            }
        }
    }

    __attribute__((optimize("Os"))) void handle(uint8_t type, int k, uint8_t a, uint8_t b)
    {
        switch (type)
        {
            case EV_ON:
                if (b == 0)
                    release(k, a);
                else
                    start(k, a, b);
                break;
            case EV_OFF: release(k, a); break;
            case EV_ALLOFF:
                for (int i = 0; i < kPool; i++)
                    if (v[i].inst == k && v[i].stage != ST_IDLE)
                    {
                        v[i].stage = ST_RELEASE;
                        v[i].gate = 0;
                    }
                break;
            case EV_BEND: in[k].bend = ((float)(((int)b << 7) | a) - 8192.0f) * (2.0f / 8192.0f); break;
            case EV_CC: cc(k, a, b); break;
            default: break;
        }
    }

    __attribute__((optimize("Os"))) void cc(int k, uint8_t n, uint8_t val)
    {
        Inst& I = in[k];
        const int pct = (val * 100 + 63) / 127;
        switch (n)
        {
            case 1: I.mod = (float)val * (1.0f / 127.0f); break;
            case 5: I.ef[P_GLIDE] = clampParam(P_GLIDE, pct); break;
            case 7: I.ef[P_LEVEL] = clampParam(P_LEVEL, pct); break;
            case 70: I.ef[P_SHAPE] = clampParam(P_SHAPE, pct); break;
            case 71: I.ef[P_RESO] = clampParam(P_RESO, pct); break;
            case 72: I.ef[P_RELEASE] = clampParam(P_RELEASE, pct); break;
            case 73: I.ef[P_ATTACK] = clampParam(P_ATTACK, pct); break;
            case 74: I.ef[P_CUTOFF] = clampParam(P_CUTOFF, pct); break;
            case 75: I.ef[P_DECAY] = clampParam(P_DECAY, pct); break;
            case 76: I.ef[P_DETUNE] = clampParam(P_DETUNE, pct); break;
            case 77: I.ef[P_NOISE] = clampParam(P_NOISE, pct); break;
            case 91: I.ef[P_REVERB] = clampParam(P_REVERB, pct); break;
            case 93: I.ef[P_DELAY] = clampParam(P_DELAY, pct); break;
            case 120:
            case 123: handle(EV_ALLOFF, k, 0, 0); break;
            default: break;
        }
    }

    void release(int k, uint8_t note)
    {
        for (int i = 0; i < kPool; i++)
            if (v[i].inst == k && v[i].gate && v[i].note == note)
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

    // steal key: a releasing voice by its level (quietest first), then the oldest note
    static uint32_t victimKey(const Voice& x)
    {
        return x.stage == ST_RELEASE ? (uint32_t)(x.env * 60000.0f) : 0x10000u + (x.serial & 0x7FFFFFFFu);
    }

    // The pool slot for a new note of instance k: the same note still held, else an idle slot while the instance is under its Voices
    // limit and the pool under the governor's cap, else a victim: of the instance itself when it is at its limit, otherwise of any
    // instance (a releasing voice first, then the oldest note).
    int pick(int k, uint8_t note) const
    {
        const int limit = in[k].ef[P_VOICES];
        for (int i = 0; i < kPool; i++)
            if (v[i].inst == k && v[i].stage != ST_IDLE && v[i].gate && v[i].note == note)
                return i;
        int own = 0, total = 0;
        for (int i = 0; i < kPool; i++)
            if (v[i].stage != ST_IDLE)
            {
                total++;
                own += v[i].inst == k;
            }
        if (own < limit && total < gov.cap)
            for (int i = 0; i < kPool; i++)
                if (v[i].stage == ST_IDLE)
                    return i;
        int best = -1;
        uint32_t bestKey = 0xFFFFFFFFu;
        for (int i = 0; i < kPool; i++)
        {
            if (v[i].stage == ST_IDLE || (own >= limit && v[i].inst != k))
                continue;
            const uint32_t key = victimKey(v[i]);
            if (key < bestKey)
            {
                bestKey = key;
                best = i;
            }
        }
        if (best < 0) // nothing to steal (cap 0 and an empty pool): the first idle slot
        {
            for (int i = 0; i < kPool; i++)
                if (v[i].stage == ST_IDLE)
                    return i;
            return 0;
        }
        return best;
    }

    // The governor lowered the cap: sounding voices above it fade out within the block (victim order as for stealing)
    void enforceCap(int cap)
    {
        int n = 0;
        for (int i = 0; i < kPool; i++)
            n += v[i].stage != ST_IDLE && !v[i].kill;
        while (n > cap)
        {
            int best = -1;
            uint32_t bestKey = 0xFFFFFFFFu;
            for (int i = 0; i < kPool; i++)
            {
                if (v[i].stage == ST_IDLE || v[i].kill)
                    continue;
                const uint32_t key = victimKey(v[i]);
                if (key < bestKey)
                {
                    bestKey = key;
                    best = i;
                }
            }
            if (best < 0)
                break;
            v[best].kill = 1;
            v[best].gate = 0;
            n--;
        }
    }

    __attribute__((optimize("Os"))) void start(int k, uint8_t note, uint8_t vel)
    {
        const int slot = pick(k, note);
        Voice& vo = v[slot];
        const bool wasIdle = vo.stage == ST_IDLE;
        vo.inst = (uint8_t)k;
        vo.kill = 0;
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
        vo.fatk = 1; // the filter envelope attacks again from where it is
        vo.stage = ST_ATTACK; // a steal or retrigger keeps level, phases and filter: the amplitude is continuous
    }

    static float lerpParam(float s, float t, float a) { return s + (t - s) * a; }

    __attribute__((optimize("Os"))) void derive(Inst& I, Derived& d)
    {
        const float tCut = (float)I.ef[P_CUTOFF], tRes = (float)I.ef[P_RESO], tNoise = (float)I.ef[P_NOISE], tDet = (float)I.ef[P_DETUNE],
                    tLevel = (float)I.ef[P_LEVEL];
        if (!I.smoothed)
        {
            I.sCut = tCut;
            I.sRes = tRes;
            I.sNoise = tNoise;
            I.sDet = tDet;
            I.sLevel = tLevel;
            I.sShape = (float)I.ef[P_SHAPE];
            I.smoothed = true;
        }
        const float a = 0.35f; // block rate one pole: ~12 ms, then ramped linearly across each block
        I.sCut = lerpParam(I.sCut, tCut, a);
        I.sRes = lerpParam(I.sRes, tRes, a);
        I.sNoise = lerpParam(I.sNoise, tNoise, a);
        I.sDet = lerpParam(I.sDet, tDet, a);
        I.sLevel = lerpParam(I.sLevel, tLevel, a);
        I.sShape = lerpParam(I.sShape, (float)I.ef[P_SHAPE], a);

        // shared LFO (parabolic sine), rate 0.1 .. 19 Hz
        I.lfoPhase += 0.1f * exp2fast((float)I.ef[P_LFO_RATE] * 0.076f) * kBlockSec;
        if (I.lfoPhase >= 1.0f)
        {
            I.lfoPhase -= 1.0f;
            I.lfoPrev = I.lfoHeld; // a new random value per cycle (sample & hold, smooth random)
            I.lfoRng = I.lfoRng * 1664525u + 1013904223u;
            I.lfoHeld = (float)(int32_t)I.lfoRng * (1.0f / 2147483648.0f);
        }
        float lfo = lfoShape(I.ef[P_LFO_WAVE], I.lfoPhase, I.lfoPrev, I.lfoHeld);
        float depth = (float)I.ef[P_LFO_DEPTH] * 0.01f;
        if (I.mod > depth)
            depth = I.mod;
        lfo *= depth;
        const uint8_t dest = I.ef[P_LFO_DEST];

        d.vib = (dest == 0 ? lfo * 2.0f : 0.0f) + I.bend;
        const float cutMod = dest == 2 ? exp2fast(lfo * 3.0f) : 1.0f;
        d.cutGain = exp2fast((I.sCut - 60.0f) * 0.08f) * cutMod;
        const float r = I.sRes * 0.01f;
        d.res = 2.0f * r * r;
        const float nz = I.sNoise * 0.01f;
        d.noise = nz * nz;
        d.detune = I.sDet * 0.0012f;
        d.interval = (float)((int)I.ef[P_INTERVAL] - 12);
        const float lv = I.sLevel * 0.01f;
        d.level = lv * lv * 1.5f * (1.0f + (float)I.ef[P_DRIVE] * 0.03f);
        if (dest == 3)
            d.level *= 1.0f + lfo * 0.5f; // tremolo

        // envelope: per block one pole coefficients from the times (attack 1 ms .. 2 s, decay / release 5 ms .. 10 s)
        d.kA = 1.0f - exp2fast(-kBlockSec * 1.4427f * 2.4f / (0.001f * exp2fast((float)I.ef[P_ATTACK] * 0.11f)));
        d.kD = 1.0f - exp2fast(-kBlockSec * 1.4427f * 4.6f / (0.005f * exp2fast((float)I.ef[P_DECAY] * 0.11f)));
        d.kR = 1.0f - exp2fast(-kBlockSec * 1.4427f * 4.6f / (0.005f * exp2fast((float)I.ef[P_RELEASE] * 0.11f)));
        d.sus = (float)I.ef[P_SUSTAIN] * 0.01f;
        d.sus *= d.sus;
        d.glideK = I.ef[P_GLIDE] == 0 ? 1.0f : 1.0f - exp2fast(-kBlockSec * 1.4427f / (0.005f * exp2fast((float)I.ef[P_GLIDE] * 0.1f)));

        d.stereo = I.ef[P_WIDTH] != 0;
        const float w = (float)I.ef[P_WIDTH] * 0.01f;
        d.sR = 0.5f * (1.0f - w); // share of the opposite side
        d.sL = 1.0f - d.sR;
        d.wt = I.ef[P_SHAPE] > kShapePulseMax;
        d.pulse = I.ef[P_SHAPE] != 0 && !d.wt;
        float sh = I.sShape;
        if (dest == 1 && d.pulse)
            sh += lfo * 50.0f;
        if (sh < 1.0f)
            sh = 1.0f;
        if (sh > 100.0f)
            sh = 100.0f;
        // pulse width 50% .. 2%
        d.offTarget = (uint32_t)((0.5f - 0.48f * (sh - 1.0f) * (1.0f / 99.0f)) * 4294967040.0f);

        // wavetable position in tables (0 .. 16): the parameter I.smoothed like the others (a jump into the mode starts at the target), the
        // LFO (target Shape) swings it by +-4 tables at full depth
        d.wtXF = false;
        d.wtA0 = d.wtA1 = 0;
        d.wtW0 = d.wtW1 = 0.0f;
        if (d.wt)
        {
            const float tgt = (float)((int)I.ef[P_SHAPE] - kShapeWt0) * (1.0f / kWtSteps);
            I.sWt = I.wtPrev < 0.0f ? tgt : lerpParam(I.sWt, tgt, a);
            float pos = I.sWt + (dest == 1 ? lfo * 4.0f : 0.0f);
            const float top = (float)(kWtTables - 1);
            pos = pos < 0.0f ? 0.0f : (pos > top ? top : pos);
            const float pos0 = I.wtPrev < 0.0f ? pos : I.wtPrev;
            I.wtPrev = pos;
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
            I.wtPrev = -1.0f;

        // ---- plan 019 B: sub oscillators, filter type and envelope
        d.ft = I.ef[P_FTYPE];
        d.sub = I.ef[P_SUB1] > 0 || I.ef[P_SUB2] > 0;
        d.subG1 = (float)I.ef[P_SUB1] * 0.005f; // a square of +-1 times half the level
        d.subG2 = (float)I.ef[P_SUB2] * 0.005f;
        d.famt = ((float)I.ef[P_FENV_AMT] - 50.0f) * 0.1f;
        d.fenvOn = I.ef[P_FENV_AMT] != 50;
        d.kFA = 1.0f - exp2fast(-kBlockSec * 1.4427f * 2.4f / (0.001f * exp2fast((float)I.ef[P_FENV_A] * 0.11f)));
        d.kFD = 1.0f - exp2fast(-kBlockSec * 1.4427f * 4.6f / (0.005f * exp2fast((float)I.ef[P_FENV_D] * 0.11f)));
        d.kFR = 1.0f - exp2fast(-kBlockSec * 1.4427f * 4.6f / (0.005f * exp2fast((float)I.ef[P_FENV_R] * 0.11f)));
        d.fsus = (float)I.ef[P_FENV_S] * 0.01f;
        d.fsus *= d.fsus;
        d.svfOct = (I.sCut - 60.0f) * 0.1f + (dest == 2 ? lfo * 3.0f : 0.0f);
        d.svfK = 2.0f - 1.96f * r * r;
        d.ladK = 3.9f * r * r;
    }

    void renderVoice(Voice& vo, const Derived& d)
    {
        // both are per block constants: a predictable branch per sample instead of four copies of the loop (flash is tight)
        const bool ST = d.stereo, PU = d.pulse, WT = d.wt;
        if ((vo.stage == ST_RELEASE && vo.env == 0.0f) || (vo.kill && vo.g == 0.0f))
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
        // plan 019 B: the filter envelope (block rate, follows the gate like the amplitude envelope)
        if (vo.gate)
        {
            if (vo.fatk)
            {
                vo.fenv += (1.15f - vo.fenv) * d.kFA;
                if (vo.fenv >= 1.0f)
                {
                    vo.fenv = 1.0f;
                    vo.fatk = 0;
                }
            }
            else
                vo.fenv += (d.fsus - vo.fenv) * d.kFD;
        }
        else
            vo.fenv -= vo.fenv * d.kFR;
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
        const float a = vo.kill ? 0.0f : vo.vel * vo.env; // killed (governor): the amplitude ramps to zero over the block
        const float gT = 0.35f * a;
        float kT = a * a * d.cutGain;
        if (d.fenvOn && d.ft == 0)
            kT *= exp2fast(d.famt * vo.fenv); // the filter envelope moves the low pass gate's cutoff (octaves)
        if (kT > 1.0f)
            kT = 1.0f;
        // SVF / ladder: coefficient g = tan(pi fc / fs), ramped over the block in steps of 32 samples
        const int FT = d.ft;
        if (vo.ftype != FT)
        {
            memset(vo.fs, 0, sizeof(vo.fs)); // another filter: clean states
            vo.ftype = (uint8_t)FT;
            vo.fg = 0.0f;
        }
        float fgS = 0.0f, fgE = 0.0f;
        if (FT)
        {
            float oct = d.svfOct + (d.fenvOn ? d.famt * vo.fenv : 0.0f);
            float fc = 1300.0f * exp2fast(oct);
            fc = fc < 20.0f ? 20.0f : (fc > 13000.0f ? 13000.0f : fc);
            fgE = tanPade(fc * (3.14159265f / kFs));
            fgS = vo.fg > 0.0f ? vo.fg : fgE;
            vo.fg = fgE;
        }
        // sub oscillators: squares one and two octaves below the pitch
        const bool SUB = d.sub;
        uint32_t sp0 = vo.sph[0], sp1 = vo.sph[1];
        uint32_t spv0 = vo.spv[0], spv1 = vo.spv[1], spv2 = vo.spv[2], spv3 = vo.spv[3];
        int32_t sd0 = 0, sd1 = 0;
        float sgl1 = vo.sg1, sgl2 = vo.sg2;
        float dsg1 = 0.0f, dsg2 = 0.0f;
        if (SUB)
        {
            sd0 = semiToDphase(base - 12.0f);
            sd1 = semiToDphase(base - 24.0f);
            if (!vo.subOn)
            {
                spv0 = sp0;
                spv1 = sp0 + 0x80000000u;
                spv2 = sp1;
                spv3 = sp1 + 0x80000000u;
                sgl1 = d.subG1;
                sgl2 = d.subG2;
            }
            dsg1 = (d.subG1 - sgl1) * (1.0f / kBlock);
            dsg2 = (d.subG2 - sgl2) * (1.0f / kBlock);
        }
        vo.subOn = SUB;
        float a1 = 0, a2 = 0, a3 = 0, fk = 0, G = 0, G2 = 0, G3 = 0, invD = 1.0f;
        const float svfK = d.svfK, ladK = d.ladK;
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
            xL += n;
            if (SUB)
            {
                sp0 += (uint32_t)sd0;
                sp1 += (uint32_t)sd1;
                sgl1 += dsg1;
                sgl2 += dsg2;
                const float sb = (blepSaw(sp0, (uint32_t)sd0, spv0) - blepSaw(sp0 + 0x80000000u, (uint32_t)sd0, spv1)) * sgl1 +
                                 (blepSaw(sp1, (uint32_t)sd1, spv2) - blepSaw(sp1 + 0x80000000u, (uint32_t)sd1, spv3)) * sgl2;
                xL += sb;
                xR += sb; // (only used with ST)
            }
            if (FT)
            {
                if ((i & 31) == 0)
                {
                    const float gg = fgS + (fgE - fgS) * ((float)i * (1.0f / kBlock));
                    if (FT < 4)
                    {
                        fk = svfK;
                        a1 = 1.0f / (1.0f + gg * (gg + fk));
                        a2 = gg * a1;
                        a3 = gg * a2;
                    }
                    else
                    {
                        G = gg / (1.0f + gg);
                        G2 = G * G;
                        G3 = G2 * G;
                        invD = 1.0f / (1.0f + ladK * G2 * G2);
                    }
                }
                float fL, fR = 0.0f;
                if (FT < 4)
                {
                    fL = svfStep(xL, vo.fs, a1, a2, a3, fk, FT);
                    if (ST)
                    {
                        xR += n;
                        fR = svfStep(xR, vo.fs + 4, a1, a2, a3, fk, FT);
                    }
                }
                else
                {
                    fL = ladderStep(xL, vo.fs, G, G2, G3, invD, ladK);
                    if (ST)
                    {
                        xR += n;
                        fR = ladderStep(xR, vo.fs + 4, G, G2, G3, invD, ladK);
                    }
                }
                fL *= g;
                if (ST)
                {
                    fR *= g;
                    oL[i] += fL * d.sL + fR * d.sR;
                    oR[i] += fR * d.sL + fL * d.sR;
                }
                else
                {
                    oL[i] += fL;
                    oR[i] += fL;
                }
                continue;
            }
            xL = xL * g;
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
        vo.sph[0] = sp0;
        vo.sph[1] = sp1;
        vo.spv[0] = spv0;
        vo.spv[1] = spv1;
        vo.spv[2] = spv2;
        vo.spv[3] = spv3;
        vo.sg1 = SUB ? d.subG1 : 0.0f;
        vo.sg2 = SUB ? d.subG2 : 0.0f;
        for (int q = 0; q < 8; q++) // a corrupt filter state is cleared like the low pass gate's
            if (!(vo.fs[q] > -1.0e4f && vo.fs[q] < 1.0e4f))
            {
                memset(vo.fs, 0, sizeof(vo.fs));
                break;
            }
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
        if (vo.kill)
            vo.clear();
    }
};

}  // namespace plinkyvoice
