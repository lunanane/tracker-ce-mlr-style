// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Generic frame of the v0.2 sidecar files (seqclips.bin ...): "MAGIC4", version, 0, payload length (u16), payload, CRC16.
// The CRC is CRC-16/CCITT-FALSE over everything before it (the same as seqtracks.bin). No Polyend includes, host-testable.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace seqsidecar {

enum { kHeader = 8, kCrc = 2, kOverhead = kHeader + kCrc };

inline uint16_t crc16Update(uint16_t c, const uint8_t* b, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        c = (uint16_t)(c ^ ((uint16_t)b[i] << 8));
        for (int k = 0; k < 8; k++)
            c = (uint16_t)((c & 0x8000) ? ((c << 1) ^ 0x1021) : (c << 1));
    }
    return c;
}
inline uint16_t crc16(const uint8_t* b, size_t n) { return crc16Update(0xFFFF, b, n); }

// Writes the frame around `payload` into buf. Returns the frame size, 0 when cap is too small or the payload > 65535.
inline size_t frameWrite(uint8_t* buf, size_t cap, const char magic[4], uint8_t version, const uint8_t* payload, size_t plen)
{
    if (plen > 0xFFFF || cap < plen + kOverhead)
        return 0;
    for (int i = 0; i < 4; i++)
        buf[i] = (uint8_t)magic[i];
    buf[4] = version;
    buf[5] = 0;
    buf[6] = (uint8_t)(plen & 0xFF);
    buf[7] = (uint8_t)(plen >> 8);
    for (size_t i = 0; i < plen; i++) // payload may already be in place (payload == buf + kHeader)
        if (buf + kHeader + i != payload + i)
            buf[kHeader + i] = payload[i];
    uint16_t c = crc16(buf, kHeader + plen);
    buf[kHeader + plen] = (uint8_t)(c & 0xFF);
    buf[kHeader + plen + 1] = (uint8_t)(c >> 8);
    return kHeader + plen + kCrc;
}

enum FrameStatus : uint8_t { FRAME_OK = 0, FRAME_BAD = 1, FRAME_NEWER = 2 };

// Checks magic, version (<= maxVersion; a newer one is FRAME_NEWER: leave the file alone), length and CRC. On FRAME_OK
// *payload / *plen / *version describe the payload inside buf. Trailing bytes after the frame are ignored.
inline FrameStatus frameRead(const uint8_t* buf, size_t len, const char magic[4], uint8_t maxVersion, const uint8_t** payload,
                             size_t* plen, uint8_t* version)
{
    if (!buf || len < (size_t)kOverhead)
        return FRAME_BAD;
    for (int i = 0; i < 4; i++)
        if (buf[i] != (uint8_t)magic[i])
            return FRAME_BAD;
    size_t n = (size_t)buf[6] | ((size_t)buf[7] << 8);
    if (kHeader + n + kCrc > len)
        return FRAME_BAD;
    uint16_t c = crc16(buf, kHeader + n);
    if (buf[kHeader + n] != (uint8_t)(c & 0xFF) || buf[kHeader + n + 1] != (uint8_t)(c >> 8))
        return FRAME_BAD;
    if (buf[4] == 0)
        return FRAME_BAD;
    if (buf[4] > maxVersion)
        return FRAME_NEWER;
    *payload = buf + kHeader;
    *plen = n;
    *version = buf[4];
    return FRAME_OK;
}

}  // namespace seqsidecar
