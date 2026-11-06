// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Per-clip play modes of the clip launcher (plan 005 R2): one mode per pattern x track, stored in the sidecar seqclips.bin.
// A clip is "track t of pattern p" (launcher rows are patterns, owner answer 2026-11-04), so the mode survives however the clip
// is launched. Plan 006 adds one more bit per pattern: the scene's "play next" flag of the live-set Song page (see below). No Polyend
// includes, host-testable (core/seqtest).
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
// Scene flags (plan 006): one bit per pattern, set = the slot of that pattern advances to the next slot at the pattern end ("play next");
// clear (the default) = the slot loops. Bit (pattern - 1) of a 32 byte table behind the clip modes.
enum { kScenePayload = (kPatterns + 7) / 8 };
static_assert(kScenePayload == 32, "255 patterns x 1 bit");
// Scene colours (plan 017): 4 bits per pattern (nibble (pattern - 1) & 1 of byte (pattern - 1) / 2; low nibble = the even pattern index),
// a seqpal scene colour index, 0 = none. A table behind the scene flags.
enum { kColorPayload = (kPatterns + 1) / 2, kColorMax = 15 };
static_assert(kColorPayload == 128, "255 patterns x 4 bits");
// Version 1 (v0.2): clip modes only. Version 2 (v0.2.3): clip modes + scene flags; a version 1 file loads with every flag = Loop.
// Version 3 (v0.3.8): + scene colours; older files load with every colour = none.
enum { kVersionV1 = 1, kVersionV2 = 2, kVersion = 3, kPayloadV2 = kPayload + kScenePayload, kPayloadV3 = kPayloadV2 + kColorPayload,
       kFileMax = kPayloadV3 + seqsidecar::kOverhead };

class ClipModes
{
  public:
    ClipModes() { clear(); }
    void clear()
    {
        for (size_t i = 0; i < sizeof(bits_); i++)
            bits_[i] = 0;
        for (size_t i = 0; i < sizeof(scene_); i++)
            scene_[i] = 0;
        for (size_t i = 0; i < sizeof(color_); i++)
            color_[i] = 0;
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
    // ---- scene flag "play next" (plan 006). Read from the sequencer interrupt too: one byte read, no loops.
    bool playNext(uint8_t pattern) const
    {
        if (pattern < 1 || pattern > kPatterns)
            return false;
        return ((scene_[(pattern - 1) >> 3] >> ((pattern - 1) & 7)) & 1u) != 0;
    }
    // true: the stored flag changed
    bool setPlayNext(uint8_t pattern, bool on)
    {
        if (pattern < 1 || pattern > kPatterns || playNext(pattern) == on)
            return false;
        uint8_t bit = (uint8_t)(1u << ((pattern - 1) & 7));
        if (on)
            scene_[(pattern - 1) >> 3] = (uint8_t)(scene_[(pattern - 1) >> 3] | bit);
        else
            scene_[(pattern - 1) >> 3] = (uint8_t)(scene_[(pattern - 1) >> 3] & ~bit);
        return true;
    }
    // ---- scene colour (plan 017): palette index 0..kColorMax, 0 = none
    uint8_t color(uint8_t pattern) const
    {
        if (pattern < 1 || pattern > kPatterns)
            return 0;
        return (uint8_t)((color_[(pattern - 1) >> 1] >> (((pattern - 1) & 1) * 4)) & 15u);
    }
    // true: the stored colour changed
    bool setColor(uint8_t pattern, uint8_t c)
    {
        if (pattern < 1 || pattern > kPatterns || c > kColorMax || color(pattern) == c)
            return false;
        uint8_t sh = (uint8_t)(((pattern - 1) & 1) * 4);
        size_t b = (pattern - 1) >> 1;
        color_[b] = (uint8_t)((color_[b] & ~(15u << sh)) | ((unsigned)c << sh));
        return true;
    }
    bool anyNonDefault() const
    {
        for (size_t i = 0; i < sizeof(bits_); i++)
            if (bits_[i])
                return true;
        for (size_t i = 0; i < sizeof(scene_); i++)
            if (scene_[i])
                return true;
        for (size_t i = 0; i < sizeof(color_); i++)
            if (color_[i])
                return true;
        return false;
    }
    uint16_t hash() const
    {
        return seqsidecar::crc16Update(seqsidecar::crc16Update(seqsidecar::crc16(bits_, sizeof(bits_)), scene_, sizeof(scene_)), color_, sizeof(color_));
    }

    // frame -> buf (kFileMax bytes at most); 0 = buffer too small
    size_t save(uint8_t* buf, size_t cap) const
    {
        if (cap < (size_t)kFileMax)
            return 0;
        uint8_t* p = buf + seqsidecar::kHeader; // payload built in place, then framed
        for (size_t i = 0; i < sizeof(bits_); i++)
            p[i] = bits_[i];
        for (size_t i = 0; i < sizeof(scene_); i++)
            p[sizeof(bits_) + i] = scene_[i];
        for (size_t i = 0; i < sizeof(color_); i++)
            p[sizeof(bits_) + sizeof(scene_) + i] = color_[i];
        return seqsidecar::frameWrite(buf, cap, kMagic, kVersion, p, kPayloadV3);
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
        if (st != seqsidecar::FRAME_OK)
            return false;
        if (!((v == kVersionV1 && n == (size_t)kPayload) || (v == kVersionV2 && n == (size_t)kPayloadV2) || (v == kVersion && n == (size_t)kPayloadV3)))
            return false;
        for (size_t i = 0; i < sizeof(bits_); i++)
            bits_[i] = p[i];
        for (size_t i = 0; i < sizeof(scene_); i++)
            scene_[i] = v >= kVersionV2 ? p[sizeof(bits_) + i] : (uint8_t)0; // version 1: every scene loops
        scene_[kScenePayload - 1] = (uint8_t)(scene_[kScenePayload - 1] & 0x7F); // bit 255 belongs to no pattern
        for (size_t i = 0; i < sizeof(color_); i++)
            color_[i] = v == kVersion ? p[sizeof(bits_) + sizeof(scene_) + i] : (uint8_t)0; // older files: no colours
        color_[kColorPayload - 1] = (uint8_t)(color_[kColorPayload - 1] & 0x0F); // the high nibble of the last byte belongs to no pattern
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
    uint8_t scene_[kScenePayload];
    uint8_t color_[kColorPayload];
};

}  // namespace seqclips
