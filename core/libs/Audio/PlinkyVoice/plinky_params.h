// Parameters of the synth voice (plan 009 S): the list, ranges, defaults, names and the sidecar file plinky.bin.
// No Polyend includes, host-testable (core/seqtest/test_plinky.cpp). The DSP is in plinky_voice.h.
//
// Voice design after the synth voice of the Plinky firmware (plinky_public, MIT, see plinky_voice.h): detuned or pulse-width
// saw pairs, a noise source, a two pole "low pass gate" whose cutoff follows the envelope, one ADSR, a shared LFO.
// Sequencer tracks mod, PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md); the Plinky parts are MIT (see plinky_voice.h).
#pragma once
#include "seqSidecarLogic.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace plinkyvoice {

// plan 019: three PolySynth instances (PolySynth 1 / 2 / 3), each with its own parameters. Instance k plays MIDI instrument / channel
// defaultChannel(k) = 16, 15, 14 (instance 0 keeps the channel 16 of every v0.3.x project).
constexpr int kInstances = 3;
inline uint8_t defaultChannel(int inst) { return (uint8_t)(16 - (inst < 0 ? 0 : (inst >= kInstances ? kInstances - 1 : inst))); }

// Shape (plan 015 W): 0 saws, 1..100 pulse, then the Plinky wavetables: kWtTables tables, kWtSteps parameter steps from one table to
// the next (the sound is the linear blend of the two neighbouring tables). Values 0..100 are the v0.3.6 sounds, unchanged.
constexpr int kWtTables = 17;
constexpr int kWtSteps = 8;
constexpr int kShapePulseMax = 100;
constexpr int kShapeWt0 = kShapePulseMax + 1;                          // first table, pure
constexpr int kShapeMax = kShapeWt0 + (kWtTables - 1) * kWtSteps;      // last table, pure (229)

enum Param : uint8_t
{
    P_CHANNEL,   // 1..16 = the MIDI instrument / channel that is the "PolySynth" instrument (default 16; v0.3.2: not visible any more; 0 = off, only from old files)
    P_VOICES,    // 1..4
    P_LEVEL,
    P_REVERB,    // send
    P_DELAY,     // send
    P_GLIDE,
    P_WIDTH,     // 0 = mono (renders one oscillator pair: about half the CPU)
    P_SHAPE,     // 0 = two detuned saws, 1..100 = pulse (two saws, phase offset): wide .. narrow, 101.. = wavetables (plan 015 W, kShapeWt0)
    P_DETUNE,
    P_INTERVAL,  // stored 0..24 = -12 .. +12 semitones between the two oscillator pairs
    P_NOISE,
    P_CUTOFF,
    P_RESO,
    P_DRIVE,
    P_ATTACK,
    P_DECAY,
    P_SUSTAIN,
    P_RELEASE,
    P_LFO_RATE,
    P_LFO_DEPTH,
    P_LFO_DEST,  // 0 pitch, 1 shape, 2 cutoff
    P_COUNT
};
// plan 019: the parameters of the file formats before the engine extensions (plinky.bin version 1 frame and the PLVM trailer hold exactly these)
constexpr int kLegacyCount = 21;
static_assert(P_COUNT >= kLegacyCount, "parameters are only ever appended");

struct ParamInfo
{
    uint8_t min, max, def;
    const char* name;
};

inline const ParamInfo& paramInfo(int i)
{
    static const ParamInfo t[P_COUNT] = {
        { 0, 16, 16, "Channel" },   { 1, 4, 4, "Voices" },    { 0, 100, 70, "Level" },  { 0, 100, 0, "Reverb" },
        { 0, 100, 0, "Delay" },    { 0, 100, 0, "Glide" },   { 0, 100, 60, "Width" },  { 0, kShapeMax, 0, "Shape" },
        { 0, 100, 30, "Detune" },  { 0, 24, 12, "Interval" }, { 0, 100, 0, "Noise" },  { 0, 100, 60, "Cutoff" },
        { 0, 100, 20, "Reso" },    { 0, 100, 30, "Drive" },  { 0, 100, 10, "Attack" }, { 0, 100, 50, "Decay" },
        { 0, 100, 60, "Sustain" }, { 0, 100, 40, "Release" }, { 0, 100, 40, "LFO rate" }, { 0, 100, 0, "LFO depth" },
        { 0, 2, 0, "LFO to" },
    };
    return t[i < 0 ? 0 : (i >= P_COUNT ? P_COUNT - 1 : i)];
}

// Names of the 17 tables (by their spectrum; Plinky's own names for most of them are placeholders). 1 based in the display ("WT3 Buzz").
inline const char* wtName(int table)
{
    static const char* const k[kWtTables] = { "Saw",  "Sine", "Buzz", "Soft", "Reed", "Glass", "Pipe", "Hiss", "Hole",
                                              "Bell", "Pad",  "Vox",  "Warm", "Mild", "Comb",  "Duo",  "Hash" };
    return k[table < 0 ? 0 : (table >= kWtTables ? kWtTables - 1 : table)];
}

// Display text of Shape: "Saws", "Pulse 37", on a table "WT3 Buzz", between two tables "WT3+37%" (that far towards the next one).
inline void shapeText(int v, char* out, size_t n)
{
    if (v <= 0)
        snprintf(out, n, "Saws");
    else if (v <= kShapePulseMax)
        snprintf(out, n, "Pulse %d", v);
    else
    {
        const int pos = (v > kShapeMax ? kShapeMax : v) - kShapeWt0;
        const int t = pos / kWtSteps, s = pos % kWtSteps;
        if (s == 0)
            snprintf(out, n, "WT%d %s", t + 1, wtName(t));
        else
            snprintf(out, n, "WT%d+%d%%", t + 1, (s * 100 + kWtSteps / 2) / kWtSteps);
    }
}

inline uint8_t clampParam(int i, int v)
{
    const ParamInfo& p = paramInfo(i);
    return (uint8_t)(v < p.min ? p.min : (v > p.max ? p.max : v));
}

// ---------------------------------------------------------------- sidecar plinky.bin
constexpr char kMagic[4] = { 'P', 'L', 'V', 'X' };
enum { kVersion = 1, kPayload = 24, kFileMax = kPayload + seqsidecar::kOverhead };

struct Params
{
    uint8_t v[P_COUNT];
    Params() { setDefaults(); }
    void setDefaults()
    {
        for (int i = 0; i < P_COUNT; i++)
            v[i] = paramInfo(i).def;
    }
    bool isDefault() const
    {
        for (int i = 0; i < P_COUNT; i++)
            if (v[i] != paramInfo(i).def)
                return false;
        return true;
    }
    bool operator==(const Params& o) const
    {
        for (int i = 0; i < P_COUNT; i++)
            if (v[i] != o.v[i])
                return false;
        return true;
    }
    uint16_t hash() const { return seqsidecar::crc16(v, P_COUNT); }

    size_t save(uint8_t* buf, size_t cap) const
    {
        if (cap < (size_t)kFileMax)
            return 0;
        uint8_t* p = buf + seqsidecar::kHeader;
        for (int i = 0; i < kPayload; i++)
            p[i] = i < kLegacyCount ? v[i] : 0; // the rest is reserved (the newer parameters are in the PLVI frames)
        return seqsidecar::frameWrite(buf, cap, kMagic, kVersion, p, kPayload);
    }
    // false: missing / bad frame / short payload (nothing changed). *newer: a later mod version wrote it (leave the file alone).
    // A value outside its range falls back to its default; a longer payload of a later version is read as far as it is known.
    bool load(const uint8_t* buf, size_t len, bool* newer = nullptr)
    {
        const uint8_t* p = nullptr;
        size_t n = 0;
        uint8_t ver = 0;
        seqsidecar::FrameStatus st = seqsidecar::frameRead(buf, len, kMagic, kVersion, &p, &n, &ver);
        if (newer)
            *newer = st == seqsidecar::FRAME_NEWER;
        if (st != seqsidecar::FRAME_OK || n < (size_t)kLegacyCount)
            return false;
        for (int i = 0; i < P_COUNT; i++)
            v[i] = paramInfo(i).def;
        for (int i = 0; i < kLegacyCount; i++)
        {
            const ParamInfo& pi = paramInfo(i);
            v[i] = (p[i] >= pi.min && p[i] <= pi.max) ? p[i] : pi.def;
        }
        return true;
    }
};

}  // namespace plinkyvoice
