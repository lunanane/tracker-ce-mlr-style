#include "Profiler.h"
#include "config.h"
#include "limits.h"
#include "log.h"
#include "malloc.h"
#include <polyassert.h>

#include "Platform.h"
#include "Tracker.h"
#include "math.h"
#include <algorithm>
#include <vector>

// extern uint32_t _estack;
extern "C"
{
    extern void _vStackTop(void); // symbols from linker script
    extern void _vStackBase(void);
}
#ifndef PLATFORM_IMX
extern "C"
{
    extern char* __brkval;       // end of the heap (teensy_core _sbrk)
    extern unsigned long _estack; // top of the stack (linker script)
}
// plan 009 P#1: the gap between the heap and the stack is painted once; the lowest address that has lost the paint is the deepest the
// stack has ever been (a probe for the headroom after the reverb buffers moved into SRAM).
static uint32_t* s_stackGapLow = nullptr;
static uint32_t* s_stackGapHigh = nullptr;
static constexpr uint32_t kGapPaint = 0xAAAAAAAAu;
#endif
Profiler* Profiler::instance = nullptr;

void Profiler::init()
{
    // allocate and then free all available memory for malloc to claim all space
    // from sbrk, so that mallinfo data includes all of it

    std::vector<void*> allocations;
    allocations.reserve(128);
    size_t allocSize = 4096;
    do
    {
        void* buf = malloc(allocSize);
        if (buf != nullptr)
        {
            allocations.push_back(buf);
        }
        else
        {
            allocSize /= 2;
        }
    } while (allocSize > 16);

    std::for_each(std::rbegin(allocations), std::rend(allocations), [](void* buf) { free(buf); });
    allocations.clear();
    allocations.shrink_to_fit();

    struct mallinfo mi = mallinfo();
    (void)mi;
    LOGF(
      "memory on init: arena: %d, allocated: %d, free: %d\r\n", mi.arena, mi.uordblks, mi.fordblks);

#ifndef PLATFORM_IMX
    char* sp;
    __asm volatile("mov %0, sp" : "=r"(sp));
    s_stackGapLow = (uint32_t*)(((uintptr_t)__brkval + 3) & ~3u);
    s_stackGapHigh = (uint32_t*)(((uintptr_t)sp - 128) & ~3u); // keep clear of the live frames
    for (uint32_t* p = s_stackGapLow; p < s_stackGapHigh; p++)
        *p = kGapPaint;
#endif

    resetMeasuringAudio();
}

size_t Profiler::freeRamBytes()
{
    return mallinfo().fordblks;
}

// plan 010: the lowest free heap seen so far (device test: read it after a project open / save, a sample export, big file browser lists).
// Called from the main loop; mallinfo() walks the free list, so it is sampled every 16th pass only.
static size_t s_minFreeHeap = (size_t)-1;
void Profiler::sampleHeap()
{
    static uint8_t divider = 0;
    if ((++divider & 15) != 0)
        return;
    const size_t f = mallinfo().fordblks;
    if (f < s_minFreeHeap)
        s_minFreeHeap = f;
}
size_t Profiler::minFreeHeapBytes()
{
    return s_minFreeHeap == (size_t)-1 ? freeRamBytes() : s_minFreeHeap;
}

size_t Profiler::maxStackBytes()
{
#ifdef PLATFORM_IMX
    const uint8_t* base = (uint8_t*)&_vStackBase;
    const uint8_t* top = (uint8_t*)&_vStackTop;

    const uint8_t* addr = top - 1;

    int watermarksCounter = 0;
    static constexpr int kConsecutiveWatermarksToSuccess = 3;
    while (addr >= base)
    {

        if (*addr-- == kStackWatermark)
        {
            watermarksCounter++;
        }
        else
        {
            watermarksCounter = 0;
        }

        if (watermarksCounter == kConsecutiveWatermarksToSuccess)
        {
            break;
        }
    }

    return (size_t)(top - addr) - watermarksCounter;
#else
    // deepest stack use so far: from the top of the stack down to the lowest word that lost the paint
    if (!s_stackGapLow)
        return 0;
    const uint32_t* p = s_stackGapLow;
    while (p < s_stackGapHigh && *p == kGapPaint)
        p++;
    return (size_t)((uintptr_t)&_estack - (uintptr_t)p);
#endif
}

// bytes between the end of the heap and the deepest stack point so far (the smallest the headroom has been); 0 when unknown
size_t Profiler::minFreeStackBytes()
{
#ifdef PLATFORM_IMX
    return 0;
#else
    if (!s_stackGapLow)
        return 0;
    const uint32_t* p = s_stackGapLow;
    while (p < s_stackGapHigh && *p == kGapPaint)
        p++;
    return (size_t)((uintptr_t)p - (uintptr_t)s_stackGapLow);
#endif
}

void Profiler::resetMeasuringAudio()
{
    lastSampleUs = 0;
    sumOfSamples = 0;
    minSampleUs = INT_MAX;
    maxSampleUs = 0;
    samples = 0;
    tempMaxSampleUs = 0;
}

void Profiler::startMeasuringAudio()
{
    if (Tracker::getInstance()->getPlatform() == nullptr)
    {
        return;
    }

    audioUpdateTimer = Tracker::getInstance()->getPlatform()->getTimer()->clockCycles();
}

void Profiler::stopMeasuringAudio()
{
    if (Tracker::getInstance()->getPlatform() == nullptr)
    {
        return;
    }

    lastSampleUs =
      std::max<int>(
        Tracker::getInstance()->getPlatform()->getTimer()->clockCycles() - audioUpdateTimer, 0) /
      (float)Tracker::getInstance()->getPlatform()->getTimer()->clockCyclesPerUs();

    minSampleUs = std::min(minSampleUs, lastSampleUs);
    maxSampleUs = std::max(maxSampleUs, lastSampleUs);
    tempMaxSampleUs = std::max(tempMaxSampleUs, lastSampleUs);

    sumOfSamples += lastSampleUs;
    samples++;
}

void Profiler::getAudioProcessingUsageUs(AudioProcessorUsage& usage, bool resetTempMaximum)
{
    usage.average = sumOfSamples / samples;
    usage.last = lastSampleUs;
    usage.maximum = maxSampleUs;
    usage.minimum = minSampleUs;
    usage.tempMaximum = tempMaxSampleUs;

    if (resetTempMaximum)
    {
        tempMaxSampleUs = 0;
    }
}

void Profiler::getAudioProcessingUsagePercent(AudioProcessorUsage& usage, bool resetTempMaximum)
{
    getAudioProcessingUsageUs(usage, resetTempMaximum);

    usage.average = calculatePercentProcessorUsage(usage.average);
    usage.last = calculatePercentProcessorUsage(usage.last);
    usage.maximum = calculatePercentProcessorUsage(usage.maximum);
    usage.minimum = calculatePercentProcessorUsage(usage.minimum);
    usage.tempMaximum = calculatePercentProcessorUsage(usage.tempMaximum);
}

int Profiler ::calculatePercentProcessorUsage(int processorUsageUs)
{
    return (100 * processorUsageUs) / kMaxAudioUpdateTime;
}
