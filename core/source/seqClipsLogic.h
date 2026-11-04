// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Per-clip play modes of the clip launcher (plan 005 R2): one mode per pattern x track, stored in the sidecar seqclips.bin.
// A clip is "track t of pattern p" (launcher rows are patterns, owner answer 2026-11-04), so the mode survives however the clip
// is launched. No Polyend includes, host-testable (core/seqtest).
#pragma once
#include "seqSidecarLogic.h"
#include <stddef.h>
#include <stdint.h>

namespace seqclips {

enum { kPatterns = 255, kTracks = 16, kBitsPerClip = 2, kPayload = kPatterns * kTracks * kBitsPerClip / 8 };
static_assert(kPayload == 1020, "255 patterns x 16 tracks x 2 bits");

// Play modes. The stored value has 2 bits: 2 and 3 are reserved for later modes and read back as LOOP today (per-entry fallback).
enum ClipMode : uint8_t { CLIP_LOOP = 0, CLIP_PLAY1X = 1, CLIP_MODE_COUNT = 2 };

constexpr const char kMagic[4] = { 'S', 'Q', 'C', 'L' };
enum { kVersion = 1, kFileMax = kPayload + seqsidecar::kOverhead };

class ClipModes
{
  public:
    ClipModes() { clear(); }
    void clear()
    {
        for (size_t i = 0; i < sizeof(bits_); i++)
            bits_[i] = 0;
    }
    // pattern 1..255, track 0..15; anything else reads as LOOP and is ignored on write
    uint8_t mode(uint8_t pattern, uint8_t track) const
    {
        if (!valid(pattern, track))
            return CLIP_LOOP;
        uint8_t v = (uint8_t)((bits_[byteOf(pattern, track)] >> shiftOf(pattern, track)) & 3u);
        return v < CLIP_MODE_COUNT ? v : (uint8_t)CLIP_LOOP;
    }
    // true: the stored value changed
    bool setMode(uint8_t pattern, uint8_t track, uint8_t m)
    {
        if (!valid(pattern, track) || m >= CLIP_MODE_COUNT)
            return false;
        if (mode(pattern, track) == m)
            return false;
        size_t b = byteOf(pattern, track);
        uint8_t sh = shiftOf(pattern, track);
        bits_[b] = (uint8_t)((bits_[b] & ~(3u << sh)) | ((unsigned)m << sh));
        return true;
    }
    // Shift + button 1 on a scene: the mode of every clip in the row (pattern). true: something changed.
    bool setRow(uint8_t pattern, uint8_t m)
    {
        bool changed = false;
        for (uint8_t t = 0; t < kTracks; t++)
            changed |= setMode(pattern, t, m);
        return changed;
    }
    bool anyNonDefault() const
    {
        for (size_t i = 0; i < sizeof(bits_); i++)
            if (bits_[i])
                return true;
        return false;
    }
    uint16_t hash() const { return seqsidecar::crc16(bits_, sizeof(bits_)); }

    // frame -> buf (kFileMax bytes at most); 0 = buffer too small
    size_t save(uint8_t* buf, size_t cap) const
    {
        return seqsidecar::frameWrite(buf, cap, kMagic, kVersion, bits_, sizeof(bits_));
    }
    // Missing / bad frame / wrong length: nothing changed and false is returned (the caller resets). A newer version is
    // reported through *newer so the file can be left alone. Reserved mode values are cleaned to LOOP per entry.
    bool load(const uint8_t* buf, size_t len, bool* newer = nullptr)
    {
        const uint8_t* p = nullptr;
        size_t n = 0;
        uint8_t v = 0;
        seqsidecar::FrameStatus st = seqsidecar::frameRead(buf, len, kMagic, kVersion, &p, &n, &v);
        if (newer)
            *newer = st == seqsidecar::FRAME_NEWER;
        if (st != seqsidecar::FRAME_OK || n != (size_t)kPayload)
            return false;
        for (size_t i = 0; i < sizeof(bits_); i++)
            bits_[i] = p[i];
        for (size_t i = 0; i < (size_t)kPatterns * kTracks; i++)
        {
            uint8_t sh = (uint8_t)((i % 4) * 2);
            if (((bits_[i / 4] >> sh) & 3u) >= CLIP_MODE_COUNT)
                bits_[i / 4] = (uint8_t)(bits_[i / 4] & ~(3u << sh));
        }
        return true;
    }

  private:
    static bool valid(uint8_t pattern, uint8_t track) { return pattern >= 1 && pattern <= kPatterns && track < kTracks; }
    static size_t index(uint8_t pattern, uint8_t track) { return (size_t)(pattern - 1) * kTracks + track; }
    static size_t byteOf(uint8_t pattern, uint8_t track) { return index(pattern, track) / 4; }
    static uint8_t shiftOf(uint8_t pattern, uint8_t track) { return (uint8_t)((index(pattern, track) % 4) * 2); }
    uint8_t bits_[kPayload];
};

}  // namespace seqclips
