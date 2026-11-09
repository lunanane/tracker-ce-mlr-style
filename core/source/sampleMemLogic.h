// Sample pool accounting decisions (plan 023). Pure logic, no firmware dependencies,
// unit-tested on the host (core/seqtest/test_sampmem.cpp).
//
// The resident pool (externalRamSampleMemory) holds whole short samples and only the attack
// pre-cache of streamed samples. A sample is "streamed" when its PCM stays on the SD card.
#ifndef SAMPLE_MEM_LOGIC_H_
#define SAMPLE_MEM_LOGIC_H_

#include <stdint.h>

namespace sampmem
{

constexpr uint32_t kStreamThresholdBytes = 256 * 1024; // same value as mtStructs.h
constexpr uint32_t kPreCacheFrames = 32768;            // same value as mtStructs.h
constexpr uint32_t kImportSafetyBytes = 10000;         // slack the importer keeps free

enum class Placement : uint8_t
{
    Resident,        // whole sample in the pool
    Streamed,        // pre-cache in the pool, rest on SD
    StreamedOneShot, // streamed, and the play mode must change to One-Shot (it needs resident data)
    Refused,         // does not fit even as a streamed sample
};

inline uint32_t sampleBytes(uint32_t frames, uint32_t channels)
{
    return frames * channels * 2u;
}

inline uint32_t preCacheBytes(uint32_t frames, uint32_t channels)
{
    return (frames < kPreCacheFrames ? frames : kPreCacheFrames) * channels * 2u;
}

// Pool bytes a sample occupies once import has finished (long samples end up streamed).
inline uint32_t importResidentBytes(uint32_t frames, uint32_t channels)
{
    uint32_t total = sampleBytes(frames, channels);
    return total >= kStreamThresholdBytes ? preCacheBytes(frames, channels) : total;
}

// What the firmware did before plan 023: the whole converted sample had to fit the free pool at
// once (it was loaded fully resident first and demoted to a stream afterwards).
inline bool legacyImportFits(uint32_t frames, uint32_t channels, uint32_t freeBytes)
{
    return sampleBytes(frames, channels) < freeBytes;
}

// Import of a WAV into a slot. freeBytes = pool bytes free after the slot's old sample is gone.
inline Placement decideImport(uint32_t frames,
                              uint32_t channels,
                              bool modeNeedsResident,
                              uint32_t freeBytes)
{
    uint32_t total = sampleBytes(frames, channels);
    uint32_t keep = freeBytes > kImportSafetyBytes ? freeBytes - kImportSafetyBytes : 0;
    if (total < keep)
    {
        return Placement::Resident; // old path (may be demoted to a stream afterwards)
    }
    if (total < kStreamThresholdBytes || preCacheBytes(frames, channels) > keep)
    {
        return Placement::Refused;
    }
    return modeNeedsResident ? Placement::StreamedOneShot : Placement::Streamed;
}

// Project load of an instrument whose PCM is on SD. modeStreamable = isPlayModeStreamable().
inline Placement decideLoad(uint32_t frames,
                            uint32_t channels,
                            bool modeStreamable,
                            bool modeNeedsResident,
                            uint32_t freeBytes)
{
    uint32_t total = sampleBytes(frames, channels);
    if (total >= kStreamThresholdBytes && modeStreamable)
    {
        return preCacheBytes(frames, channels) <= freeBytes ? Placement::Streamed
                                                            : Placement::Refused;
    }
    if (total <= freeBytes)
    {
        return Placement::Resident;
    }
    if (total < kStreamThresholdBytes || preCacheBytes(frames, channels) > freeBytes)
    {
        return Placement::Refused;
    }
    return modeNeedsResident ? Placement::StreamedOneShot : Placement::Streamed;
}

// Pool bytes still free; never wraps (used may exceed max after a pool shrink or a bad project file).
inline uint32_t poolFree(uint32_t maxBytes, uint32_t usedBytes)
{
    return maxBytes > usedBytes ? maxBytes - usedBytes : 0u;
}

// A sample of `bytes` placed `offsetBytes` into a pool of `poolBytes` stays inside the pool (no wrap in the sum).
inline bool fitsPool(uint32_t offsetBytes, uint32_t bytes, uint32_t poolBytes)
{
    return offsetBytes <= poolBytes && bytes <= poolBytes - offsetBytes;
}

// Whether a streamed sample can be made resident (play mode / editor change).
inline bool canPromote(uint32_t frames, uint32_t channels, uint32_t ramFrames, uint32_t freeBytes)
{
    uint32_t extra = frames > ramFrames ? (frames - ramFrames) * channels * 2u : 0;
    return extra <= freeBytes;
}

} // namespace sampmem

#endif
