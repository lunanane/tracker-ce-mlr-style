// Ring reverb: a port of the reverb of the Plinky firmware (plinky_public, sw/Core/Src/plinky.c, function Reverb2) to the
// Tracker's audio engine: 44.1 kHz instead of 32 kHz (all delay lengths, LFO rates, filter coefficients and shimmer timing are
// rescaled so the time behaviour is the same), a 32768 sample ring (the original 16384 at the lower rate), stereo pre-delay,
// an adjustable all-pass gain and a plain C++ form of the Cortex-M intrinsics.
//
// Algorithm (unchanged): four all-pass diffusers on the input (the right channel is injected between the second and the third),
// then one long loop in a single ring buffer that is walked backwards: modulated all-pass, all-pass, delay, a shimmer tap that
// reads older material of the ring (two random positions, cross-faded), a one-pole low-pass + DC blocker, and a second leg of
// modulated all-pass, all-pass, delay + low-pass. The left output is the first leg, the right output the second one.
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
// (The Plinky name, logo and front panel design are not part of this grant; this port is not a Plinky product.)
// ----------------------------------------------------------------------------------------------------------------------
// Port, mappings and the Tracker integration: Sequencer tracks mod, PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md).
//
// No Polyend includes, host-testable (core/seqtest/test_fx.cpp). The caller owns the two buffers.
#pragma once
#include "ring_tiles.h"
#include "seqFxDsp.h"
#include <stddef.h>
#include <stdint.h>

namespace ringverb {

constexpr uint32_t kRingSize = 32768;            // samples (64 KB)
constexpr uint32_t kRingMask = kRingSize - 1;
// pre-delay line, samples per channel. The Tracker Plus / Mini have only a few KB of external RAM left: 1024 samples (23 ms) there
// (and the ring lives in the two buffers of the stock reverb, see SplitRing), 4096 samples (93 ms, 2 x 8 KB) on the original.
#if defined(PLATFORM_IMX)
constexpr uint32_t kPreSize = 1024;
#else
constexpr uint32_t kPreSize = 4096;
#endif
constexpr uint32_t kPreMask = kPreSize - 1;

// delay lengths at 44.1 kHz: the original lengths (the original halves the numbers it lists: RVDIV) x 44100 / 32000
constexpr int kLenAp1 = 98, kLenAp2 = 260, kLenAp3 = 73, kLenAp4 = 190;
constexpr int kLenWob1 = 463, kLenAp5 = 1240, kLenDel1 = 3068;
constexpr int kLenWob2 = 626, kLenAp6 = 1830, kLenDel2 = 2179;
constexpr int kLoopLength = kLenAp1 + kLenAp2 + kLenAp3 + kLenAp4 + kLenWob1 + kLenAp5 + kLenDel1 + kLenWob2 + kLenAp6 + kLenDel2;
static_assert(kLoopLength == 10027, "the loop of the ring");

constexpr float kRateScale = 32000.0f / 44100.0f;
constexpr int kShimmerFadeLen = 32768;
constexpr int kShimmerMax = 127, kShimmerDefault = 32, kShimmerStepPerBlock = 2;

inline int32_t ssat16(int32_t v)
{
#if defined(__arm__)
    int32_t out;
    asm("ssat %0, #16, %1" : "=r"(out) : "r"(v));
    return out;
#else
    return v < -32768 ? -32768 : (v > 32767 ? 32767 : v);
#endif
}

// Write into the ring / the return (plan 010 A2): soft limit instead of the hard 16 bit clip. Identity up to 5/8 of full scale (the
// sound below that is bit-identical to v0.2.x), a quarter of the slope above, ssat16 only guards the largest sums. A near-unity loop
// (size 0.95 and loud input) compresses instead of crackling.
inline int32_t satw(int32_t v)
{
    return (v > 20480 || v < -20480) ? ssat16(seqfx::softLimit(v)) : v;
}

// The ring memory. PlainRing: one block of kRingSize samples (original Tracker, host). SplitRing: two blocks of kRingSize / 2 (the
// Tracker Plus / Mini borrow the two buffers of the stock reverb, which are idle while this reverb runs).
struct PlainRing
{
    int16_t* p;
    int16_t& operator[](uint32_t i) const { return p[i]; }
};
struct SplitRing
{
    int16_t* a;
    int16_t* b;
    int16_t& operator[](uint32_t i) const { return (i & (kRingSize / 2)) ? b[i & (kRingSize / 2 - 1)] : a[i]; }
};

#if defined(__arm__)
#define RINGVERB_INLINE __attribute__((always_inline)) inline
#define RINGVERB_NOINLINE __attribute__((noinline))
#else
#define RINGVERB_INLINE inline
#define RINGVERB_NOINLINE
#endif

// plan 027: the cell pointers of the tiled loop: wp = where the stage writes, rp = where it reads, both already moved to this sample
inline int16_t* ringBase(const SplitRing&) { return nullptr; }
inline int16_t* ringBase(const PlainRing& r) { return r.p; }
inline int32_t tileAp(int16_t* wp, const int16_t* rp, int32_t acc, int32_t g)
{
    int32_t d = *rp;
    acc -= (int32_t)(((int64_t)d * g) >> 15);
    *wp = (int16_t)satw(acc);
    acc = (int32_t)(((int64_t)acc * g) >> 15) + d;
    return acc;
}
// wobbled read: wobpos has 12 fractional bits and is subtracted from the read place
// rp = the cell kWobLo below the read place of this sample (the run that holds the wobbled reads)
constexpr int kWobLo = 57, kWobHi = 120;
inline int32_t tileApWob(int16_t* wp, const int16_t* rp, int wobpos, int32_t acc, int32_t g)
{
    const int back = (wobpos >> 12) - kWobLo;
    const int frac = wobpos & 0xFFF;
    int32_t a0 = rp[-back];
    int32_t a1 = rp[-back - 1];
    int32_t d = (a0 * (0x1000 - frac) + a1 * frac) >> 12;
    acc -= (int32_t)(((int64_t)d * g) >> 15);
    *wp = (int16_t)satw(acc);
    acc = (int32_t)(((int64_t)acc * g) >> 15) + d;
    return acc;
}

struct RingBlock;

template<typename Ring>
class BasicRingReverb
{
  public:
    // ring: kRingSize samples, pre: 2 * kPreSize samples (left, then right). Not cleared here (see resetState).
    void init(Ring ring, int16_t* pre)
    {
        ring_ = ring;
        preL_ = pre;
        preR_ = pre + kPreSize;
        setParams(234, 0.8f, 16384, 0);
        resetState();
    }

    // size of the two buffers in bytes, for the chunked clear of the audio engine
    static constexpr size_t ringBytes() { return kRingSize * sizeof(int16_t); }
    static constexpr size_t preBytes() { return 2 * kPreSize * sizeof(int16_t); }
    static constexpr size_t kBytes = ringBytes() + preBytes();

    // fade: 0..250 (loop gain / 256 per pass), color: one-pole coefficient 0..1, apGain: all-pass gain in Q15 (16384 = 0.5, the
    // original), predelay: 0 .. 4095 samples
    void setParams(int fade, float color, int32_t apGain, int predelay)
    {
        fade_ = fade < 0 ? 0 : (fade > 250 ? 250 : fade);
        color_ = color;
        apGain_ = apGain;
        predelayTarget_ = predelay < 0 ? 0 : (predelay > (int)(kPreSize - 1) ? (int)(kPreSize - 1) : predelay);
    }
    // plan 010 A3: shimmer amount 0 .. 127 (the original's control 0 .. 255 halved; 32 = the fixed value of v0.2.x). The loop follows
    // the target in small steps per block, so a moving control never clicks.
    void setShimmer(int amount) { shimTarget_ = amount < 0 ? 0 : (amount > kShimmerMax ? kShimmerMax : amount); }
    int shimmer() const { return shimCur_; }
    // input gain (Q16), dry share of the input that stays in the return (Q8), wet output gain (Q8)
    void setLevels(int32_t inGain, int32_t dry, int32_t wet)
    {
        inGain_ = inGain;
        dry_ = dry;
        wet_ = wet;
    }

    // filter / LFO / shimmer state back to the start (the buffers are cleared by the caller)
    void resetState()
    {
        pos_ = 0;
        fb1_ = 0;
        lpf_ = lpf2_ = dc_ = 0.0f;
        lfoR1_ = 1.0f;
        lfoI1_ = 0.0f;
        lfoR2_ = 1.0f;
        lfoI2_ = 0.0f;
        shimFade_ = 0;
        shimCur_ = shimTarget_;
        shimStep_ = 6;
        shimPos1_ = (int)(8192 / kRateScale);
        shimPos2_ = (int)(9000 / kRateScale);
        rng_ = 0x2545F491u;
        prePos_ = 0;
        predelay_ = predelayTarget_;
        quiet_ = 0;
    }

    // In place on interleaved-free int16 blocks: input send -> return (dry share + wet).
    void process(int16_t* left, int16_t* right, int n);

    // plan 027: SRAM for the tiles of the SDRAM ring (about 3000 words are needed for a block of 128; no scratch = the plain loop)
    void setScratch(int16_t* mem, uint32_t words)
    {
        scratch_ = mem;
        scratchWords_ = words;
    }
    void setTiling(bool on) { tileOn_ = on; }

    int predelay() const { return predelay_; }
    int fade() const { return fade_; }

  private:
    enum { kTileBlock = 128 };
    void slowRun(int16_t* left, int16_t* right, int n, const RingBlock& rb, int32_t& inPeak, int32_t& outPeak);
    int tileChunk(int16_t* left, int16_t* right, int rem, const RingBlock& rb, int32_t& inPeak, int32_t& outPeak);
    uint32_t nextRandom()
    {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return rng_;
    }

    Ring ring_ = {};
    int16_t* preL_ = nullptr;
    int16_t* preR_ = nullptr;

    int fade_ = 234;
    float color_ = 0.8f;
    int32_t apGain_ = 16384;
    int predelayTarget_ = 0;
    int predelay_ = 0;
    int32_t inGain_ = 32768; // x 1/65536: 0.5 per channel
    int32_t dry_ = 51;       // /256: the stock reverb also returns a fifth of its input
    int32_t wet_ = 80; // /256: about the loudness of the stock reverb at the same send (measured with noise, size 0.2 .. 0.95)

    uint32_t pos_ = 0;
    uint32_t prePos_ = 0;
    int fb1_ = 0;
    float lpf_ = 0, lpf2_ = 0, dc_ = 0;
    float lfoR1_ = 1, lfoI1_ = 0, lfoR2_ = 1, lfoI2_ = 0;
    int shimFade_ = 0, shimStep_ = 6;
    int shimTarget_ = kShimmerDefault, shimCur_ = kShimmerDefault;
    int shimPos1_ = 0, shimPos2_ = 0;
    uint32_t rng_ = 1;
    int16_t* scratch_ = nullptr;
    uint32_t scratchWords_ = 0;
    bool tileOn_ = true;
    uint32_t quiet_ = 0; // consecutive samples with (almost) no input and no wet output; >= kRingSize: the whole ring is silent
};

#if defined(PLATFORM_IMX)
typedef BasicRingReverb<SplitRing> RingReverb;
#else
typedef BasicRingReverb<PlainRing> RingReverb;
#endif

// ---------------------------------------------------------------------------------------------------------------------
// The loop itself. A stage at ring index i with length len reads / writes around i + len; the index walks along the chain and
// the whole chain moves back by one sample per output sample (pos_--), so every stage is a delay of its length.
// ---------------------------------------------------------------------------------------------------------------------
// acc, ring index: all-pass with gain g (Q15). With g = 16384 this is exactly the original (acc -= d >> 1; acc = (acc >> 1) + d).
template<typename Ring>
RINGVERB_INLINE int32_t apStage(const Ring& buf, uint32_t& i, int len, int32_t acc, int32_t g)
{
    uint32_t j = (i + (uint32_t)len) & kRingMask;
    int32_t d = buf[j];
    acc -= (int32_t)(((int64_t)d * g) >> 15);
    buf[i] = (int16_t)satw(acc);
    acc = (int32_t)(((int64_t)acc * g) >> 15) + d;
    i = j;
    return acc;
}
// the same with the read position modulated: wobpos has 12 fractional bits and is subtracted from j
template<typename Ring>
RINGVERB_INLINE int32_t apWobStage(const Ring& buf, uint32_t& i, int len, int wobpos, int32_t acc, int32_t g)
{
    uint32_t j = (i + (uint32_t)len) & kRingMask;
    uint32_t base = j - (uint32_t)(wobpos >> 12);
    int frac = wobpos & 0xFFF;
    int32_t a0 = buf[base & kRingMask];
    int32_t a1 = buf[(base - 1) & kRingMask];
    int32_t d = (a0 * (0x1000 - frac) + a1 * frac) >> 12;
    acc -= (int32_t)(((int64_t)d * g) >> 15);
    buf[i] = (int16_t)satw(acc);
    acc = (int32_t)(((int64_t)acc * g) >> 15) + d;
    i = j;
    return acc;
}
template<typename Ring>
RINGVERB_INLINE int32_t delayStage(const Ring& buf, uint32_t& i, int len, int32_t acc)
{
    uint32_t j = (i + (uint32_t)len) & kRingMask;
    buf[i] = (int16_t)satw(acc);
    acc = buf[j];
    i = j;
    return acc;
}

// Constants of one block that the untiled loop and the tiled loop share (plan 027: the loop is split in chunks)
struct RingBlock
{
    int32_t g;
    int fade;
    float color, lfoA1, lfoA2, wobScale, dcA;
    int32_t shimAmt, inGain, dry, wet;
};

template<typename Ring>
RINGVERB_INLINE void BasicRingReverb<Ring>::process(int16_t* left, int16_t* right, int n)
{
    RingBlock rb;
    rb.g = apGain_;
    rb.fade = fade_;
    rb.color = color_;
    // LFOs: original rates at 32 kHz 2.9 Hz and 1.27 Hz, rescaled; the DC blocker 0.005 per sample at 32 kHz
    rb.lfoA1 = 2.0f * (9.4f / 32777.0f) * kRateScale;
    rb.lfoA2 = 2.0f * (1.3f * 3.15971f / 32777.0f) * kRateScale;
    rb.wobScale = 262144.0f / kRateScale; // (wob + 1) * 64 samples of the original, in 12 bit fractions
    rb.dcA = 0.005f * kRateScale;
    rb.shimAmt = shimCur_; // 0 .. 127 (plan 010 A3: was fixed 32)
    if (shimCur_ < shimTarget_)
        shimCur_ = shimCur_ + kShimmerStepPerBlock > shimTarget_ ? shimTarget_ : shimCur_ + kShimmerStepPerBlock;
    else if (shimCur_ > shimTarget_)
        shimCur_ = shimCur_ - kShimmerStepPerBlock < shimTarget_ ? shimTarget_ : shimCur_ - kShimmerStepPerBlock;
    rb.inGain = inGain_;
    rb.dry = dry_;
    rb.wet = wet_;
    const int32_t dry = dry_;

    // silence bypass (as the stock reverb has): no input for a whole ring length and no tail left -> nothing to compute, the
    // return is just the dry share. Costs one pass over the block while the input is quiet.
    if (quiet_ >= kRingSize)
    {
        int32_t peak = 0;
        for (int s = 0; s < n; s++)
        {
            int32_t a = left[s] < 0 ? -left[s] : left[s], b = right[s] < 0 ? -right[s] : right[s];
            peak |= a | b;
        }
        if (peak <= 1)
        {
            for (int s = 0; s < n; s++)
            {
                left[s] = (int16_t)((left[s] * dry) >> 8);
                right[s] = (int16_t)((right[s] * dry) >> 8);
            }
            return;
        }
        quiet_ = 0;
    }
    int32_t inPeak = 0, outPeak = 0;

    // plan 027: the block runs in SRAM tiles of the SDRAM ring where it can; a chunk it cannot take (a shimmer jump inside, an odd
    // pointer, no scratch) runs the plain loop
    int off = 0;
    while (off < n)
    {
        const int rem = n - off;
        const int done = tileOn_ ? tileChunk(left + off, right + off, rem, rb, inPeak, outPeak) : -1;
        if (done > 0)
        {
            off += done;
            continue;
        }
        const int cnt = done == 0 ? 1 : rem; // 0: the sample where the shimmer taps jump, then the tiles go on
        slowRun(left + off, right + off, cnt, rb, inPeak, outPeak);
        off += cnt;
    }
    if (inPeak <= 1 && outPeak <= 1)
        quiet_ = quiet_ + (uint32_t)n > kRingSize ? kRingSize : quiet_ + (uint32_t)n;
    else
        quiet_ = 0;
}

// The plain loop (the ring in SDRAM, every access single): unchanged arithmetic of v0.4.4, now for `n` samples of a block.
template<typename Ring>
RINGVERB_NOINLINE void BasicRingReverb<Ring>::slowRun(int16_t* left, int16_t* right, int n, const RingBlock& rb, int32_t& inPeak,
                                                    int32_t& outPeak)
{
    const Ring buf = ring_;
    const int32_t g = rb.g;
    const int fade = rb.fade;
    const float color = rb.color;
    const float lfoA1 = rb.lfoA1, lfoA2 = rb.lfoA2;
    const float kWobble = 0.3f;
    const float wobScale = rb.wobScale;
    const float dcA = rb.dcA;
    const int32_t shimAmt = rb.shimAmt;
    const int32_t inGain = rb.inGain;
    const int32_t dry = rb.dry, wet = rb.wet;

    for (int s = 0; s < n; s++)
    {
        int32_t inL = left[s], inR = right[s];
        inPeak |= (inL < 0 ? -inL : inL) | (inR < 0 ? -inR : inR);
        // pre-delay (stereo), slewed by one sample per sample so a moving control cannot click
        preL_[prePos_] = (int16_t)inL;
        preR_[prePos_] = (int16_t)inR;
        if (predelay_ < predelayTarget_)
            predelay_++;
        else if (predelay_ > predelayTarget_)
            predelay_--;
        uint32_t rp = (prePos_ - (uint32_t)predelay_) & kPreMask;
        int32_t dl = preL_[rp], dr = preR_[rp];
        prePos_ = (prePos_ + 1) & kPreMask;

        // modulation
        lfoR1_ -= lfoA1 * lfoI1_;
        lfoI1_ += lfoA1 * lfoR1_;
        lfoR2_ -= lfoA2 * lfoI2_;
        lfoI2_ += lfoA2 * lfoR2_;
        int apWob = (int)((lfoR1_ * kWobble + 1.0f) * wobScale);
        int delWob = (int)((lfoR2_ * kWobble + 1.0f) * wobScale);

        uint32_t i = pos_;
        int32_t outl = 0, outr = 0;

        // input diffusion, right channel injected between the second and the third all-pass
        int32_t acc = (int32_t)(((int64_t)dl * inGain) >> 16);
        acc = apStage(buf, i, kLenAp1, acc, g);
        acc = apStage(buf, i, kLenAp2, acc, g);
        acc += (int32_t)(((int64_t)dr * inGain) >> 16);
        acc = apStage(buf, i, kLenAp3, acc, g);
        acc = apStage(buf, i, kLenAp4, acc, g);
        const int32_t reinject = acc;
        acc += fb1_;
        acc = apWobStage(buf, i, kLenWob1, apWob, acc, g);
        acc = apStage(buf, i, kLenAp5, acc, g);
        acc = delayStage(buf, i, kLenDel1, acc);

        // shimmer: two taps into older material, cross-faded, then a new random position each time the fade is complete
        shimFade_ += shimStep_;
        if (shimFade_ >= kShimmerFadeLen)
        {
            shimFade_ -= kShimmerFadeLen;
            shimPos1_ = shimPos2_;
            shimPos2_ = (int)(((nextRandom() & 4095) + 8192) / kRateScale);
            shimStep_ = (int)((((nextRandom() & 7) + 8)) * kRateScale + 0.5f);
        }
        {
            uint32_t p1 = i + (uint32_t)shimPos1_, p2 = i + (uint32_t)shimPos2_;
            int32_t s1l = buf[p1 & kRingMask], s1r = buf[p2 & kRingMask];
            int32_t s2l = buf[(p1 + 1) & kRingMask], s2r = buf[(p2 + 1) & kRingMask];
            int32_t shl = (s1l + s2l) >> 1, shr = (s1r + s2r) >> 1;
            int32_t shimo = (shl * ((kShimmerFadeLen - 1) - shimFade_) + shr * shimFade_) >> 15;
            shimo = ((shimo * shimAmt) >> 8) >> 1;
            acc += shimo;
            outl = shimo;
            outr = shimo;
            shimPos1_--;
            shimPos2_--;
        }

        // first leg: low-pass + DC blocker -> left output
        lpf_ += ((float)((acc * fade) >> 8) - lpf_) * color;
        dc_ += (lpf_ - dc_) * dcA;
        acc = (int32_t)(lpf_ - dc_);
        outl += acc;

        // second leg -> right output
        acc += reinject;
        acc = apWobStage(buf, i, kLenWob2, delWob, acc, g);
        acc = apStage(buf, i, kLenAp6, acc, g);
        acc = delayStage(buf, i, kLenDel2, acc);
        lpf2_ += ((float)((acc * fade) >> 8) - lpf2_) * color;
        acc = (int32_t)lpf2_;
        outr += acc;

        pos_ = (pos_ - 1) & kRingMask;
        fb1_ = (acc * fade) >> 8;

        const int32_t wl = (outl * wet) >> 8, wr = (outr * wet) >> 8;
        outPeak |= (wl < 0 ? -wl : wl) | (wr < 0 ? -wr : wr);
        left[s] = (int16_t)satw(((inL * dry) >> 8) + wl);
        right[s] = (int16_t)satw(((inR * dry) >> 8) + wr);
    }
}

// The same loop on SRAM tiles (plan 027). Returns the number of samples done (>= 1), 0 when the next sample is the one where the shimmer
// taps jump (the caller runs that sample in the plain loop), -1 when the tiles cannot be used for this block. Nothing is changed in the
// reverb before it is certain that the chunk runs.
template<typename Ring>
RINGVERB_NOINLINE int BasicRingReverb<Ring>::tileChunk(int16_t* left, int16_t* right, int rem, const RingBlock& rb, int32_t& inPeak,
                                                     int32_t& outPeak)
{
    int16_t* const ringMem = ringBase(ring_);
    if (ringMem == nullptr || scratch_ == nullptr || ((uintptr_t)scratch_ & 3u) || ((uintptr_t)ringMem & 3u) || ((uintptr_t)preL_ & 3u) || ((uintptr_t)preR_ & 3u))
        return -1;
    // the wobble positions stay inside the windows below as long as the oscillators keep their amplitude
    if (!(lfoR1_ <= 1.1f && lfoR1_ >= -1.1f && lfoR2_ <= 1.1f && lfoR2_ >= -1.1f))
        return -1;
    if (shimStep_ < 1 || shimFade_ < 0 || shimFade_ >= kShimmerFadeLen || shimPos1_ < 0 || shimPos2_ < 0 || shimPos1_ > 20000 ||
        shimPos2_ > 20000)
        return -1;
    // samples before the taps jump to a new random position
    const int safe = (kShimmerFadeLen - 1 - shimFade_) / shimStep_;
    if (safe <= 0)
        return 0;
    const int L = rem < safe ? (rem < kTileBlock ? rem : kTileBlock) : (safe < kTileBlock ? safe : kTileBlock);

    // offsets (from the ring index of the first sample) of the places the chain touches: stage m writes at kB[m] and reads at kB[m + 1]
    static const int kB[11] = { 0,
                                kLenAp1,
                                kLenAp1 + kLenAp2,
                                kLenAp1 + kLenAp2 + kLenAp3,
                                kLenAp1 + kLenAp2 + kLenAp3 + kLenAp4,
                                kLenAp1 + kLenAp2 + kLenAp3 + kLenAp4 + kLenWob1,
                                kLenAp1 + kLenAp2 + kLenAp3 + kLenAp4 + kLenWob1 + kLenAp5,
                                kLenAp1 + kLenAp2 + kLenAp3 + kLenAp4 + kLenWob1 + kLenAp5 + kLenDel1,
                                kLenAp1 + kLenAp2 + kLenAp3 + kLenAp4 + kLenWob1 + kLenAp5 + kLenDel1 + kLenWob2,
                                kLenAp1 + kLenAp2 + kLenAp3 + kLenAp4 + kLenWob1 + kLenAp5 + kLenDel1 + kLenWob2 + kLenAp6,
                                kLoopLength };
    // the wobble read lies kWobLo .. kWobHi (57 .. 120) samples below the stage's read place for |lfo| <= 1.1 (wobScale 361 262: (1 +- 0.33) * 88.2)
    const int tap0 = kB[7]; // the shimmer taps are relative to the chain index after the first delay

    TileSet T;
    T.init(ringMem, pos_, kRingMask);
    bool ok = true;
    for (int m = 0; m < 11; m++)
        ok = ok && T.add(kB[m] - (L - 1), kB[m], true);
    ok = ok && T.add(kB[5] - kWobHi - 1 - (L - 1), kB[5] - kWobLo, false);
    ok = ok && T.add(kB[8] - kWobHi - 1 - (L - 1), kB[8] - kWobLo, false);
    const int t1 = tap0 + shimPos1_, t2 = tap0 + shimPos2_; // offset of the first tap cell at sample 0; the cell moves down 2 per sample
    ok = ok && T.add(t1 - 2 * (L - 1), t1 + 1, false);
    ok = ok && T.add(t2 - 2 * (L - 1), t2 + 1, false);
    if (!ok)
        return -1;

    // pre-delay line: the write run and the read run (the read index moves between 0 and 2 per sample), per channel
    const int pd0 = predelay_;
    const bool preTile = pd0 + 2 * L + 4 < (int)kPreSize; // else the read run would wrap onto the write run: plain accesses
    const bool preZero = preTile && pd0 == 0 && predelayTarget_ == 0;
    TileSet PL, PR;
    if (preTile)
    {
        PL.init(preL_, prePos_, kPreMask);
        PR.init(preR_, prePos_, kPreMask);
        bool pok = PL.add(0, L - 1, true) && PR.add(0, L - 1, true);
        if (!preZero)
            pok = pok && PL.add(-pd0 - 1, -pd0 + 2 * L - 1, false) && PR.add(-pd0 - 1, -pd0 + 2 * L - 1, false);
        if (!pok)
            return -1;
    }
    int16_t* sc = scratch_;
    int room = (int)scratchWords_;
    if (!T.layout(sc, room) || (preTile && (!PL.layout(sc, room) || !PR.layout(sc, room))))
        return -1;

    T.load();
    int16_t* P[11];
    for (int m = 0; m < 11; m++)
        P[m] = T.ptr(kB[m]);
    int16_t* wob1 = T.ptr(kB[5] - kWobLo); // the wobbled reads of the two stages
    int16_t* wob2 = T.ptr(kB[8] - kWobLo);
    int16_t* tp1 = T.ptr(t1);
    int16_t* tp2 = T.ptr(t2);
    int16_t *pwl = nullptr, *pwr = nullptr, *prl = nullptr, *prr = nullptr;
    if (preTile)
    {
        PL.load();
        PR.load();
        pwl = PL.ptr(0);
        pwr = PR.ptr(0);
        if (!preZero)
        {
            prl = PL.ptr(-pd0 - 1) + (pd0 + 1); // the read run at offset 0; the read cell of sample s is prl[s - predelay]
            prr = PR.ptr(-pd0 - 1) + (pd0 + 1);
        }
    }

    const int32_t g = rb.g;
    const int fade = rb.fade;
    const float color = rb.color;
    const float lfoA1 = rb.lfoA1, lfoA2 = rb.lfoA2;
    const float kWobble = 0.3f;
    const float wobScale = rb.wobScale;
    const float dcA = rb.dcA;
    const int32_t shimAmt = rb.shimAmt;
    const int32_t inGain = rb.inGain;
    const int32_t dry = rb.dry, wet = rb.wet;

    for (int s = 0; s < L; s++)
    {
        int32_t inL = left[s], inR = right[s];
        inPeak |= (inL < 0 ? -inL : inL) | (inR < 0 ? -inR : inR);
        int32_t dl, dr;
        if (preZero)
        {
            pwl[s] = (int16_t)inL; // no pre-delay: the cell read back is the one just written
            pwr[s] = (int16_t)inR;
            dl = inL;
            dr = inR;
        }
        else if (preTile)
        {
            pwl[s] = (int16_t)inL;
            pwr[s] = (int16_t)inR;
            if (predelay_ < predelayTarget_)
                predelay_++;
            else if (predelay_ > predelayTarget_)
                predelay_--;
            dl = prl[s - predelay_];
            dr = prr[s - predelay_];
        }
        else
        {
            preL_[prePos_] = (int16_t)inL;
            preR_[prePos_] = (int16_t)inR;
            if (predelay_ < predelayTarget_)
                predelay_++;
            else if (predelay_ > predelayTarget_)
                predelay_--;
            uint32_t rp = (prePos_ - (uint32_t)predelay_) & kPreMask;
            dl = preL_[rp];
            dr = preR_[rp];
        }
        prePos_ = (prePos_ + 1) & kPreMask;

        // modulation
        lfoR1_ -= lfoA1 * lfoI1_;
        lfoI1_ += lfoA1 * lfoR1_;
        lfoR2_ -= lfoA2 * lfoI2_;
        lfoI2_ += lfoA2 * lfoR2_;
        const int apWob = (int)((lfoR1_ * kWobble + 1.0f) * wobScale);
        const int delWob = (int)((lfoR2_ * kWobble + 1.0f) * wobScale);

        int32_t outl = 0, outr = 0;

        // input diffusion, right channel injected between the second and the third all-pass
        int32_t acc = (int32_t)(((int64_t)dl * inGain) >> 16);
        acc = tileAp(P[0] - s, P[1] - s, acc, g);
        acc = tileAp(P[1] - s, P[2] - s, acc, g);
        acc += (int32_t)(((int64_t)dr * inGain) >> 16);
        acc = tileAp(P[2] - s, P[3] - s, acc, g);
        acc = tileAp(P[3] - s, P[4] - s, acc, g);
        const int32_t reinject = acc;
        acc += fb1_;
        acc = tileApWob(P[4] - s, wob1 - s, apWob, acc, g);
        acc = tileAp(P[5] - s, P[6] - s, acc, g);
        P[6][-s] = (int16_t)satw(acc);
        acc = P[7][-s];

        // shimmer (the taps do not jump inside this chunk)
        shimFade_ += shimStep_;
        {
            int32_t s1l = tp1[-2 * s], s1r = tp2[-2 * s];
            int32_t s2l = tp1[-2 * s + 1], s2r = tp2[-2 * s + 1];
            int32_t shl = (s1l + s2l) >> 1, shr = (s1r + s2r) >> 1;
            int32_t shimo = (shl * ((kShimmerFadeLen - 1) - shimFade_) + shr * shimFade_) >> 15;
            shimo = ((shimo * shimAmt) >> 8) >> 1;
            acc += shimo;
            outl = shimo;
            outr = shimo;
            shimPos1_--;
            shimPos2_--;
        }

        // first leg: low-pass + DC blocker -> left output
        lpf_ += ((float)((acc * fade) >> 8) - lpf_) * color;
        dc_ += (lpf_ - dc_) * dcA;
        acc = (int32_t)(lpf_ - dc_);
        outl += acc;

        // second leg -> right output
        acc += reinject;
        acc = tileApWob(P[7] - s, wob2 - s, delWob, acc, g);
        acc = tileAp(P[8] - s, P[9] - s, acc, g);
        P[9][-s] = (int16_t)satw(acc);
        acc = P[10][-s];
        lpf2_ += ((float)((acc * fade) >> 8) - lpf2_) * color;
        acc = (int32_t)lpf2_;
        outr += acc;

        pos_ = (pos_ - 1) & kRingMask;
        fb1_ = (acc * fade) >> 8;

        const int32_t wl = (outl * wet) >> 8, wr = (outr * wet) >> 8;
        outPeak |= (wl < 0 ? -wl : wl) | (wr < 0 ? -wr : wr);
        left[s] = (int16_t)satw(((inL * dry) >> 8) + wl);
        right[s] = (int16_t)satw(((inR * dry) >> 8) + wr);
    }
    T.store();
    if (preTile)
    {
        PL.store();
        PR.store();
    }
    return L;
}

}  // namespace ringverb
