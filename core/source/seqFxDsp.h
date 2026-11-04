// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// DSP primitives of the v0.2.4 FX filters (plan 007 F1 / F2): a zero-delay-feedback state variable filter (Zavalishin / Cytomic
// TPT form) and the small block-rate controller that smooths its centre / width so a moving control never clicks.
// Used by the ping-pong delay (bandpass in the feedback loop) and the reverb input (highpass). No Polyend includes,
// host-testable (core/seqtest/test_fx.cpp). Per sample and channel: 9 float multiply-adds + two int/float conversions.
#pragma once
#include <math.h>
#include <stdint.h>

namespace seqfx {

constexpr float kFs = 44100.0f;
constexpr float kPi = 3.14159265358979f;

struct SvfCoef
{
    float a1 = 0, a2 = 0, a3 = 0, k = 1;
};

struct SvfState
{
    float ic1 = 0, ic2 = 0;
    void clear() { ic1 = ic2 = 0; }
};

// tan(x) for 0 < x < 1.45: a [7/6] Pade approximation (relative error < 1e-6, tested) and 1 / tan(pi/2 - x) above. Cheaper per block
// than tanf() (tanf stays in the image anyway, other code uses it).
inline float tanApprox(float x)
{
    bool inv = x > 0.9f;
    if (inv)
        x = 1.57079632679f - x;
    float x2 = x * x;
    float num = x * (135135.0f + x2 * (-17325.0f + x2 * (378.0f - x2)));
    float den = 135135.0f + x2 * (-62370.0f + x2 * (3150.0f - 28.0f * x2));
    float t = num / den;
    return inv ? 1.0f / t : t;
}

// Centre / corner frequency fc (Hz), quality q. fc is limited to 20 Hz .. 0.45 fs (the tan() warp stays finite).
inline SvfCoef svfCoef(float fc, float q)
{
    if (fc < 20.0f)
        fc = 20.0f;
    if (fc > 0.45f * kFs)
        fc = 0.45f * kFs;
    if (q < 0.1f)
        q = 0.1f;
    float g = tanApprox(kPi * fc / kFs);
    SvfCoef c;
    c.k = 1.0f / q;
    c.a1 = 1.0f / (1.0f + g * (g + c.k));
    c.a2 = g * c.a1;
    c.a3 = g * c.a2;
    return c;
}

// One sample. v1 = bandpass (peak gain 1/k at the centre), v2 = lowpass; highpass = x - k*v1 - v2.
inline void svfStep(SvfState& s, const SvfCoef& c, float x, float& v1, float& v2)
{
    float v3 = x - s.ic2;
    v1 = c.a1 * s.ic1 + c.a2 * v3;
    v2 = s.ic2 + c.a2 * s.ic1 + c.a3 * v3;
    s.ic1 = 2.0f * v1 - s.ic1;
    s.ic2 = 2.0f * v2 - s.ic2;
}

// Bandpass with unity gain at the centre (the loop gain of the delay feedback can never exceed the feedback setting).
inline float bandpass(SvfState& s, const SvfCoef& c, float x)
{
    float v1, v2;
    svfStep(s, c, x, v1, v2);
    return c.k * v1;
}

inline float highpass(SvfState& s, const SvfCoef& c, float x)
{
    float v1, v2;
    svfStep(s, c, x, v1, v2);
    return x - c.k * v1 - v2;
}

// Linear ramp of the mix over a block of n samples: the value for sample s (0 .. n-1) is prev -> cur, reaching cur at the last sample.
inline float mixAt(float prev, float cur, int s, int n) { return prev + (cur - prev) * (float)(s + 1) / (float)n; }

// Soft limit of the delay feedback write (value after the >> 8, +-32767 range): linear up to 5/8 of full scale, a quarter of the
// slope above. The 16 bit line never hard-clips for any sum a feedback loop can build (the largest sum of 65534 gives 31743, the
// final saturation only guards that), so a loud sustained tone at a high feedback compresses instead of crackling.
inline int32_t softLimit(int32_t v)
{
    const int32_t t = 20480;
    if (v > t)
        return t + ((v - t) >> 2);
    if (v < -t)
        return -t - ((-v - t) >> 2);
    return v;
}

// Denormals / numeric dust in the state of a silent filter: flushed once per block.
inline void svfFlush(SvfState& s)
{
    if (fabsf(s.ic1) < 1e-15f)
        s.ic1 = 0;
    if (fabsf(s.ic2) < 1e-15f)
        s.ic2 = 0;
}

// Block-rate smoothing of a filter control. The targets are plain floats written from the main loop (a torn read is impossible
// for an aligned 32 bit float on the Cortex-M4); the audio side calls blockUpdate() once per audio block and then runs the
// samples with coef() and the mix ramp. centre is smoothed in octaves (log2 Hz), so a sweep over the whole range is even to the ear.
// mix is the bypass crossfade: 0 = the filter is out of the signal path (state cleared, nothing computed), 1 = fully in. It is
// stepped once per block here; the audio side ramps it linearly per sample from prevMix() to mix() (mixAt()), so no step reaches
// the signal.
class FilterControl
{
  public:
    float targetOct = 10.0f; // log2(Hz)
    float targetQ = 0.707f;
    float targetMix = 0.0f;

    // returns true when the coefficients changed (a new set is available through coef())
    bool blockUpdate()
    {
        bool moving = false;
        prevMix_ = mix_;
        float dOct = targetOct - oct_;
        float dQ = targetQ - q_;
        float dMix = targetMix - mix_;
        if (!init_)
        {
            oct_ = targetOct;
            q_ = targetQ;
            mix_ = targetMix;
            prevMix_ = mix_;
            init_ = true;
            moving = true;
        }
        else
        {
            if (fabsf(dOct) > 1e-3f || fabsf(dQ) > 1e-3f)
            {
                oct_ += kAlpha * dOct;
                q_ += kAlpha * dQ;
                moving = true;
            }
            else if (dOct != 0.0f || dQ != 0.0f)
            {
                oct_ = targetOct;
                q_ = targetQ;
                moving = true;
            }
            if (fabsf(dMix) > 1e-3f)
                mix_ += kAlphaMix * dMix;
            else
                mix_ = targetMix;
        }
        if (moving)
            coef_ = svfCoef(exp2f(oct_), q_);
        return moving;
    }
    const SvfCoef& coef() const { return coef_; }
    float mix() const { return mix_; }
    float prevMix() const { return prevMix_; }
    float centreHz() const { return exp2f(oct_); }
    // after a hard reset (the audio side cleared its filter states)
    void restart() { init_ = false; }

    static constexpr float kAlpha = 0.3f;     // per 128 sample block: ~2.8 blocks (8 ms) time constant for the centre / width
    static constexpr float kAlphaMix = 0.25f; // mix target follows in ~3.5 blocks (10 ms); settles (< 0.001) in about 24 blocks (70 ms)

  private:
    float oct_ = 10.0f, q_ = 0.707f, mix_ = 0.0f, prevMix_ = 0.0f;
    bool init_ = false;
    SvfCoef coef_;
};

}  // namespace seqfx
