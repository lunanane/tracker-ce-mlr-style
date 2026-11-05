// host stub (plan 009 P tests): portable C versions of the Cortex-M4 DSP intrinsics the voice-chain files use (same results).
// Force-included before the real utility/dspinst.h, whose include guard it takes over.
#ifndef dspinst_h_
#define dspinst_h_
#include <stdint.h>
#include <assert.h>
static inline int32_t signed_saturate_rshift(int32_t val, int bits, int rshift)
{
    int32_t out = val >> rshift, max = 1 << (bits - 1);
    return out > max - 1 ? max - 1 : (out < -max ? -max : out);
}
static inline int16_t saturate16(int32_t val) { return (int16_t)(val > 32767 ? 32767 : (val < -32768 ? -32768 : val)); }
static inline int32_t signed_multiply_32x16b(int32_t a, uint32_t b) { return (int32_t)(((int64_t)a * (int16_t)(b & 0xFFFF)) >> 16); }
static inline int32_t signed_multiply_32x16t(int32_t a, uint32_t b) { return (int32_t)(((int64_t)a * (int16_t)(b >> 16)) >> 16); }
static inline int32_t multiply_32x32_rshift32(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 32); }
static inline int32_t multiply_32x32_rshift32_rounded(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b + 0x80000000LL) >> 32); }
static inline uint32_t pack_16b_16b(int32_t a, int32_t b) { return ((uint32_t)a << 16) | ((uint32_t)b & 0xFFFF); }
static inline uint32_t signed_add_16_and_16(uint32_t a, uint32_t b)
{
    int32_t lo = saturate16((int16_t)(a & 0xFFFF) + (int16_t)(b & 0xFFFF));
    int32_t hi = saturate16((int16_t)(a >> 16) + (int16_t)(b >> 16));
    return ((uint32_t)hi << 16) | ((uint32_t)lo & 0xFFFF);
}
static inline int32_t multiply_32x32_rshift(int32_t x, int32_t y, int z) { return (int32_t)(((int64_t)x * y) >> (31 - z)); }
#endif
