// Copyright 2014 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Adapted for Polyverb by Wojciech Jakóbczyk (jakobczyk.woj@gmail.com)
//
// -----------------------------------------------------------------------------
//
// Reverb.

#ifndef POLYVERB_REVERB_H_
#define POLYVERB_REVERB_H_

#include "fx_engine.h"
#include "seqFxCaps.h"
#include "util.h"

class Reverb
{
  public:
    Reverb() {}
    ~Reverb() {}

    void Init(uint16_t* buffer, uint16_t* buffer2)
    {
        engine_.Init(buffer, buffer2);
        engine_.SetLFOFrequency(LFO_1, 0.5f / 32000.0f);
        engine_.SetLFOFrequency(LFO_2, 0.3f / 32000.0f);
        lp_ = 0.7f;
        diffusion_ = 0.625f;
        predelay_length_ = 0;
    }

    static constexpr size_t GetBufferSize() { return E::GetSize() * sizeof(uint16_t); }

    inline __attribute__((always_inline)) void Process(int16_t* left, int16_t* right, size_t size)
    {
        // This is the Griesinger topology described in the Dattorro paper
        // (4 AP diffusers on the input, then a loop of 2x 2AP+1Delay).
        // Modulation is applied in the loop of the first diffuser AP for additional
        // smearing; and to the two long delays for a slow shimmer/chorus effect.
        typedef E::Reserve<
          150,
          E::Reserve<
            214,
            E::Reserve<
              319,
              E::Reserve<
                527,
                E::Reserve<
                  2182,
                  E::Reserve<2690, E::Reserve<4501, E::Reserve<predelay_buffer_size_>>>>>>>>
          Memory;
        typedef E2::Reserve<2525, E2::Reserve<2197, E2::Reserve<6312>>> Memory2;
        E::DelayLine<Memory, 0> ap1;
        E::DelayLine<Memory, 1> ap2;
        E::DelayLine<Memory, 2> ap3;
        E::DelayLine<Memory, 3> ap4;
        E::DelayLine<Memory, 4> dap1a;
        E::DelayLine<Memory, 5> dap1b;
        E::DelayLine<Memory, 6> del1;
        E::DelayLine<Memory, 7> del0;
        E2::DelayLine<Memory2, 0> dap2a;
        E2::DelayLine<Memory2, 1> dap2b;
        E2::DelayLine<Memory2, 2> del2;
        E::Context c;

        const float kap = diffusion_;
        const float klp = lp_;
        const float krt = reverb_time_;
        const float amount = amount_;
        const float gain = input_gain_;

        float lp_0 = lp_decay_0_;
        float lp_1 = lp_decay_1_;
        float lp_2 = lp_decay_2_;

        float delta;
        float level_sum = 0.0f; // plan 009 P#7: |wet| of both channels, the tail level (independent of the amount / dry input)

        for (size_t i = 0; i < size; i++)
        {
            float wet;
            float apout = 0.0f;
            engine_.Start(&c);

            // Smear AP1 inside the loop.
            // c.Interpolate(ap1, 10.0f, LFO_1, 80.0f, 1.0f);
            // c.Write(ap1, 100, 0.0f);

            c.Load(gain * ((float)*left + (float)*right) / 32768); // TODO better convertion

            c.Write(del0, 0.0f);
            c.Read(del0, predelay_length_, 1.0f);
            c.Lp(lp_0, klp);

            // Diffuse through 4 allpasses.
            c.Read(ap1 TAIL, kap);
            c.WriteAllPass(ap1, -kap);
            c.Read(ap2 TAIL, kap);
            c.WriteAllPass(ap2, -kap);
            c.Read(ap3 TAIL, kap);
            c.WriteAllPass(ap3, -kap);
            c.Read(ap4 TAIL, kap);
            c.WriteAllPass(ap4, -kap);
            c.Write(apout);

            // Main reverb loop.
            c.Load(apout);
            c.Interpolate2(del2, 6211.0f, LFO_2, 100.0f, krt);
            // c.Read2(del2 TAIL, krt);
            c.Lp(lp_1, klp);
            c.Read(dap1a TAIL, -kap);
            c.Soft(); // plan 010 A2: the tank saturates softly (bit-identical below 5/8 of full scale), no hard clip at size 0.95
            c.WriteAllPass(dap1a, kap);
            c.Read(dap1b TAIL, kap);
            c.Soft();
            c.WriteAllPass(dap1b, -kap);
            c.Soft();
            c.Write(del1, 2.0f);
            c.Write(wet, 0.0f);

            // plan 010 A2: the wet return (up to 2.0 of full scale) saturates softly; a mix of the input and a value inside the 16 bit range
            // can no longer leave it (it used to wrap in the float -> int16 conversion). Bit-identical while |wet| <= 5/8.
            delta = (seqfx::softSat16(wet * 32768) - *left) * amount;
            *left += delta;
            level_sum += __builtin_fabsf(wet); // plan 009 P#8: was libc abs(int) per sample (-fno-builtin)

            c.Load(apout);
            // c.Interpolate(del1, 4450.0f, LFO_1, 50.0f, krt);
            c.Read(del1 TAIL, krt);
            c.Lp(lp_2, klp);
            c.Read2(dap2a TAIL, kap);
            c.Soft();
            c.WriteAllPass2(dap2a, -kap);
            c.Read2(dap2b TAIL, -kap);
            c.Soft();
            c.WriteAllPass2(dap2b, kap);
            c.Soft();
            c.Write2(del2, 2.0f);
            c.Write(wet, 0.0f);

            delta = (seqfx::softSat16(wet * 32768) - *right) * amount;
            *right += delta;
            level_sum += __builtin_fabsf(wet);

            ++left;
            ++right;
        }

        lp_decay_0_ = lp_0;
        lp_decay_1_ = lp_1;
        lp_decay_2_ = lp_2;
        last_update_delta_ = level_sum * (32768.0f / size); // in 16 bit LSB, both channels added
        if (last_update_delta_ > sound_delta_threshold_)
            quiet_blocks_ = 0;
    }

    // plan 009 P#7: Process(), but a block that has nothing to do is skipped: the input has been silent (all zero) for kIdleBlocks
    // blocks (so everything fed in has gone round the delay lines: they are 15 591 samples long at most) and the tail at the output is
    // below the activity threshold (default 5 LSB summed over both channels, i.e. about -82 dBFS per channel). The block is left as it
    // is (zero input = zero output); the delay lines and filter states freeze. Returns true when the block was processed.
    inline bool ProcessOrSkip(int16_t* left, int16_t* right, size_t size)
    {
        bool silent = true;
        const uint32_t* wl = reinterpret_cast<const uint32_t*>(left);
        const uint32_t* wr = reinterpret_cast<const uint32_t*>(right);
        for (size_t i = 0; i < size / 2; i++)
            silent &= (wl[i] | wr[i]) == 0;
        if (!silent || last_update_delta_ > sound_delta_threshold_)
        {
            quiet_blocks_ = 0;
        }
        else if (quiet_blocks_ >= kIdleBlocks)
        {
            return false;
        }
        else
        {
            quiet_blocks_++;
        }
        Process(left, right, size);
        return true;
    }

    inline void SetAmount(float amount) { amount_ = amount; }

    inline void SetInputGain(float input_gain) { input_gain_ = input_gain; }

    inline void SetTime(float reverb_time) { reverb_time_ = reverb_time; }

    inline void SetDiffusion(float diffusion) { diffusion_ = diffusion; }

    inline void SetLP(float lp) { lp_ = lp; }

    inline void SetPredelayLength(float length)
    {
        predelay_length_ = length * predelay_buffer_size_;
    }

    // plan 007: filter / activity state only (the audio engine clears the delay memory itself, in small pieces)
    inline void ResetState()
    {
        lp_decay_0_ = 0;
        lp_decay_1_ = 0;
        lp_decay_2_ = 0;
        last_update_delta_ = sound_delta_threshold_;
        quiet_blocks_ = 0;
    }

    inline void Reset()
    {
        engine_.Reset();
        lp_decay_0_ = 0;
        lp_decay_1_ = 0;
        lp_decay_2_ = 0;
        last_update_delta_ = sound_delta_threshold_;
        quiet_blocks_ = 0;
    }

    inline bool IsActive() { return last_update_delta_ > sound_delta_threshold_; }

    inline void SetActiveThreshold(float threshold) { sound_delta_threshold_ = threshold; }

  public:
    // plan 009 P#1: the delay lines use 15 591 (buffer 1) and 11 037 (buffer 2) of the old 2 x 16 384 words. Buffer 1 stays a power of
    // two (the LFO step test looks at the low bits of its write pointer, so the output stays bit-identical); buffer 2 is cut to a
    // multiple of 32 words that still holds its three lines, so both live in internal SRAM (54 848 bytes instead of 64 KB of SDRAM).
    static constexpr size_t kBuffer1Words = 16384;
    static constexpr size_t kBuffer2Words = 11040;

  private:
    typedef FxEngine<kBuffer1Words, FORMAT_16_BIT, kBuffer2Words> E;
    typedef E E2;
    E engine_;

    static constexpr size_t predelay_buffer_size_ = 5000;

    float amount_;

    float input_gain_;
    float reverb_time_;
    float diffusion_;
    float lp_;

    float lp_decay_0_;
    float lp_decay_1_;
    float lp_decay_2_;

    int predelay_length_;

    float last_update_delta_;
    float sound_delta_threshold_ = 5.0f;
    static constexpr uint32_t kIdleBlocks = 256; // 32 768 samples of silent input and a quiet output before the skip starts
    uint32_t quiet_blocks_ = 0;

    DISALLOW_COPY_AND_ASSIGN(Reverb);
};

#endif // POLYVERB_REVERB_H_
