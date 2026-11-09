// Sequencer tracks mod - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Crash / hang record (plan 032): the struct kept in SRAM .noinit across a software reset, its checksum and validation, the breadcrumb
// ring, and the text block appended to /crashlog.txt. Pure logic (no hardware), tested on the host (core/seqtest/test_crash.cpp).
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace seqcrash {

enum Kind : uint32_t { kNone = 0, kFault = 1, kHang = 2 };
constexpr uint32_t kMagic = 0xC4A5710Fu;
constexpr uint32_t kMarks = 8; // ring size (power of two)

struct Mark { uint16_t s, a, b; }; // stage, two arguments (stage table: plans/032-crash-log.md)

struct Rec
{
    uint32_t magic;  // kMagic once sealed (written last)
    uint32_t kind;   // Kind | exception number (IPSR) << 8
    uint32_t pc, lr, psr, sp;
    uint32_t cfsr, hfsr, bfar, mmfar;
    uint32_t head;   // marks written so far
    Mark m[kMarks];
    uint32_t crc;    // over everything between magic and crc
};
static_assert(sizeof(Rec) == 96, "the crash record must stay small (SRAM is tight)");

inline uint32_t crcOf(const Rec& r)
{
    const uint8_t* p = (const uint8_t*)&r;
    uint32_t h = 2166136261u; // FNV-1a
    for (uint32_t i = 4; i < sizeof(Rec) - 4; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}
inline bool valid(const Rec& r)
{
    const uint32_t k = r.kind & 0xFF;
    return r.magic == kMagic && (k == kFault || k == kHang) && r.crc == crcOf(r);
}
inline void put(Rec& r, uint8_t s, uint16_t a, uint16_t b)
{
    Mark& m = r.m[r.head & (kMarks - 1)];
    m.s = s; m.a = a; m.b = b;
    r.head++;
}
inline void seal(Rec& r, uint32_t kind)
{
    r.kind = kind;
    r.crc = crcOf(r);
    r.magic = kMagic; // last: a record cut short is never valid
}
inline void clear(Rec& r) { memset(&r, 0, sizeof(Rec)); }

// "--- crash ---\r\nfw <ver>\r\nHANG vec=03 pc=... lr=... psr=... sp=...\r\ncfsr=... hfsr=... bfar=... mmfar=...\r\nmarks: 0A:0000:0000 ...\r\n\r\n"
// marks run oldest to newest, "stage:a:b" in hex. Returns the length (0 when it does not fit).
inline uint32_t format(const Rec& r, const char* fw, char* out, uint32_t cap)
{
    int n = snprintf(out, cap, "--- crash ---\r\nfw %s\r\n%s vec=%02X pc=%08X lr=%08X psr=%08X sp=%08X\r\n"
                     "cfsr=%08X hfsr=%08X bfar=%08X mmfar=%08X\r\nmarks:",
                     fw, (r.kind & 0xFF) == kHang ? "HANG" : "FAULT", (unsigned)((r.kind >> 8) & 0x1FF), (unsigned)r.pc, (unsigned)r.lr,
                     (unsigned)r.psr, (unsigned)r.sp, (unsigned)r.cfsr, (unsigned)r.hfsr, (unsigned)r.bfar, (unsigned)r.mmfar);
    if (n < 0 || (uint32_t)n >= cap) return 0;
    const uint32_t cnt = r.head < kMarks ? r.head : kMarks;
    for (uint32_t i = 0; i < cnt; i++)
    {
        const Mark& m = r.m[(r.head - cnt + i) & (kMarks - 1)];
        int k = snprintf(out + n, cap - n, " %02X:%04X:%04X", (unsigned)m.s, (unsigned)m.a, (unsigned)m.b);
        if (k < 0 || (uint32_t)(n + k) >= cap) return 0;
        n += k;
    }
    int k = snprintf(out + n, cap - n, "\r\n\r\n");
    if (k < 0 || (uint32_t)(n + k) >= cap) return 0;
    return (uint32_t)(n + k);
}

// the short text for the screen when the file could not be written (registers only)
inline void screenFallback(const Rec& r, char* out, uint32_t cap)
{
    snprintf(out, cap, "Crash, log not saved. %s pc=%08X lr=%08X cfsr=%08X. Power off and on to retry.",
             (r.kind & 0xFF) == kHang ? "HANG" : "FAULT", (unsigned)r.pc, (unsigned)r.lr, (unsigned)r.cfsr);
}

} // namespace seqcrash
