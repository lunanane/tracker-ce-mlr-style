// Sequencer tracks mod - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Plan 022 P3: the register kernel of the forward sample playback (play_memory_kernel.h) against a transcription of the stock loops
// (play_memory_single_shot.cpp updateSingleShotNormal / play_memory_loop_forward.cpp updateLoopForwardNormal, resident sample, no glide):
// identical output samples and identical final state over many random blocks, plus a host cycle estimate.
#include "play_memory_kernel.h"
#include <chrono>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                \
    do                                                          \
    {                                                           \
        g_checks++;                                             \
        if (!(c))                                               \
        {                                                       \
            g_fail++;                                           \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
        }                                                       \
    } while (0)

// The stock voice state and loop, members as in AudioPlayMemory.
struct Ref
{
    int32_t iPitchCounter, interpolationDif;
    int16_t currentSampelValue;
    volatile uint8_t playing;
    volatile uint32_t length;
    uint32_t endPoint, loopPoint1;
    uint32_t interpolationCondition;
    int32_t frac;
    const int16_t* base;

    int16_t readSampleAtRel(const int16_t* in, int32_t rel) const { return *(in + rel); }

    void block(int16_t* out, int32_t castPitchControl, int32_t currentFractionPitchControl, bool loop, int32_t loopEndPoint)
    {
        const int16_t* in = base;
        int32_t currentFractionPitchCounter = frac;
        for (int i = 0; i < 128; i++)
        {
            if ((int32_t)length > iPitchCounter)
            {
                currentSampelValue = readSampleAtRel(in, iPitchCounter);
                if (interpolationCondition)
                    interpolationDif = 0;
                else
                    interpolationDif = (readSampleAtRel(in, iPitchCounter + 1) - currentSampelValue);
                *out++ = currentSampelValue + (int32_t)(((currentFractionPitchCounter >> 2) * interpolationDif) >> 14);
                iPitchCounter += castPitchControl;
                currentFractionPitchCounter += currentFractionPitchControl;
                if (currentFractionPitchCounter >= 65535)
                {
                    currentFractionPitchCounter -= 65535;
                    iPitchCounter++;
                }
                if (loop)
                {
                    if (iPitchCounter >= loopEndPoint)
                        iPitchCounter = loopPoint1 + (iPitchCounter - loopEndPoint);
                }
                else if (iPitchCounter >= (int32_t)endPoint)
                {
                    iPitchCounter = length;
                }
            }
            else
            {
                *out++ = 0;
                playing = 0;
            }
        }
        frac = currentFractionPitchCounter;
    }
};

static uint32_t s_rng = 12345;
static uint32_t rnd() { return s_rng = s_rng * 1664525u + 1013904223u; }

static void run(bool loop)
{
    static int16_t data[70000];
    for (int i = 0; i < 70000; i++)
        data[i] = (int16_t)(rnd() >> 12);
    long blocks = 0, ended = 0;
    for (int trial = 0; trial < 400; trial++)
    {
        const uint32_t len = 600 + rnd() % 60000;             // frames from the start point on
        const uint32_t endPoint = len - rnd() % 200;           // end point inside the sample
        const uint32_t loop1 = rnd() % (len / 2);
        const uint32_t loop2 = loop1 + 64 + rnd() % (len / 2); // loop end point
        const float pitch = 0.2f + (float)(rnd() % 4000) / 1000.0f; // 0.2 .. 4.2
        const bool interp = (rnd() & 3) != 0;
        Ref r = {};
        r.base = data;
        r.length = len;
        r.endPoint = endPoint;
        r.loopPoint1 = loop1;
        r.iPitchCounter = (int32_t)(rnd() % (len / 4));
        r.frac = (int32_t)(rnd() % 65535);
        r.playing = 1;
        pmkernel::Fwd k = { r.iPitchCounter, r.frac, 1, 0, 0 };
        const int32_t cast = (int32_t)pitch;
        const float pitchFraction = pitch - (int32_t)pitch;
        const int32_t fracCtl = (int32_t)(pitchFraction * 65535);
        const int32_t loopEnd = loop2 < len ? (int32_t)loop2 : (int32_t)len; // std::min(loopPoint2, min(length, endPoint)) in the stock code
        const int32_t loopEndPoint = loopEnd < (int32_t)endPoint ? loopEnd : (int32_t)endPoint;
        for (int b = 0; b < 60 && (r.playing || b < 2); b++)
        {
            // interpolationCondition as the stock code sets it per block (interpolation off near the end)
            r.interpolationCondition = interp ? (((r.iPitchCounter + 128 * pitch) < r.length) ? 0 : 1) : 1;
            int16_t outRef[128], outK[128];
            memset(outRef, 0x5A, sizeof(outRef));
            memset(outK, 0x5A, sizeof(outK));
            k.ipc = r.iPitchCounter;
            k.frac = r.frac;
            k.playing = r.playing;
            k.sample = r.currentSampelValue;
            k.dif = r.interpolationDif;
            r.block(outRef, cast, fracCtl, loop, loopEndPoint);
            if (loop)
                pmkernel::renderForward<true>(k, data, outK, 128, (int32_t)len, 0, loopEndPoint, loop1, cast, fracCtl, r.interpolationCondition != 0);
            else
                pmkernel::renderForward<false>(k, data, outK, 128, (int32_t)len, (int32_t)endPoint, 0, 0, cast, fracCtl, r.interpolationCondition != 0);
            blocks++;
            if (memcmp(outRef, outK, sizeof(outRef)) != 0 || k.ipc != r.iPitchCounter || k.frac != r.frac || k.playing != r.playing ||
                k.sample != r.currentSampelValue || k.dif != r.interpolationDif)
            {
                CHECK(false);
                printf("  trial %d block %d loop %d pitch %.3f\n", trial, b, loop, pitch);
                return;
            }
            if (!r.playing)
                ended++;
        }
    }
    CHECK(blocks > 5000);
    CHECK(loop || ended > 50); // single shot trials reach the end of the sample
    printf("kernel %s: %ld blocks bit-identical (%ld ended inside a block)\n", loop ? "loop forward" : "single shot", blocks, ended);
}

// host timing: the member-store loop vs the register loop (indicative only; the device is ARM with SDRAM reads)
static void timing()
{
    static int16_t data[70000];
    for (int i = 0; i < 70000; i++)
        data[i] = (int16_t)(rnd() >> 12);
    Ref r = {};
    r.base = data;
    r.length = 69000;
    r.endPoint = 69000;
    r.loopPoint1 = 0;
    r.interpolationCondition = 0;
    int16_t out[128];
    auto t0 = std::chrono::steady_clock::now();
    long sink = 0;
    for (int n = 0; n < 200000; n++)
    {
        r.iPitchCounter = n % 60000;
        r.block(out, 1, 20000, true, 69000);
        sink += out[5];
    }
    auto t1 = std::chrono::steady_clock::now();
    pmkernel::Fwd k = { 0, 0, 1, 0, 0 };
    for (int n = 0; n < 200000; n++)
    {
        k.ipc = n % 60000;
        pmkernel::renderForward<true>(k, data, out, 128, 69000, 0, 69000, 0, 1, 20000, false);
        sink += out[5];
    }
    auto t2 = std::chrono::steady_clock::now();
    printf("host (-O1): stock loop %.0f ns/block, register kernel %.0f ns/block (sink %ld)\n",
           std::chrono::duration<double, std::nano>(t1 - t0).count() / 200000, std::chrono::duration<double, std::nano>(t2 - t1).count() / 200000, sink);
}

int main()
{
    run(false);
    run(true);
    timing();
    printf("kernel: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
