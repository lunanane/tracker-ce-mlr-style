// Sequencer tracks mod - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Host tests of the crash / hang record (plan 032): validation, the breadcrumb ring, the text block and the screen fallback.
#include "seqCrashLogic.h"
#include <stdio.h>
#include <string.h>
#include <string>

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

using namespace seqcrash;

static Rec sample(uint32_t kind, int marks)
{
    Rec r;
    memset(&r, 0xA5, sizeof r); // power-up garbage
    clear(r);
    for (int i = 0; i < marks; i++) put(r, (uint8_t)(10 + i), (uint16_t)(i * 3), (uint16_t)(0xF000 + i));
    r.pc = 0x0802A1C4; r.lr = 0x0801F3B1; r.psr = 0x61000000; r.sp = 0x2002FF40;
    r.cfsr = 0x00008200; r.hfsr = 0x40000000; r.bfar = 0x70123456; r.mmfar = 0xDEADBEEF;
    seal(r, kind | (3u << 8));
    return r;
}

static void test_validate()
{
    Rec z; clear(z);
    CHECK(!valid(z));                       // cleared: no record
    Rec g; memset(&g, 0xA5, sizeof g);
    CHECK(!valid(g));                       // garbage
    Rec r = sample(kFault, 3);
    CHECK(valid(r));
    Rec h = sample(kHang, 3);
    CHECK(valid(h));
    Rec n = sample(kNone, 3);
    CHECK(!valid(n));                       // kind none
    Rec b = r; b.pc ^= 1;
    CHECK(!valid(b));                       // any flipped field
    b = r; b.m[2].a ^= 0x100;
    CHECK(!valid(b));                       // a flipped breadcrumb
    b = r; b.crc ^= 1;
    CHECK(!valid(b));
    b = r; b.magic = 0;                     // unsealed (cut short): not valid
    CHECK(!valid(b));
    Rec c = r; clear(c);
    CHECK(!valid(c));
    CHECK(sizeof(Rec) == 96);
}

static void test_ring()
{
    Rec r; clear(r);
    for (int i = 0; i < 5; i++) put(r, (uint8_t)i, 0, 0);
    CHECK(r.head == 5);
    for (int i = 5; i < 20; i++) put(r, (uint8_t)i, (uint16_t)i, (uint16_t)(i * 2));
    CHECK(r.head == 20);
    // last 8 marks are 12..19
    bool all = true;
    for (int i = 12; i < 20; i++) all = all && r.m[i & 7].s == i && r.m[i & 7].a == i && r.m[i & 7].b == i * 2;
    CHECK(all);
    seal(r, kHang);
    char out[512];
    uint32_t n = format(r, "1.0.0 CE-SEQ0.4.9 abc1234", out, sizeof out);
    CHECK(n > 0 && n == strlen(out));
    std::string s(out);
    // oldest first: 0C ... 13 (hex)
    size_t p = s.find("marks:");
    CHECK(p != std::string::npos);
    CHECK(s.find(" 0C:000C:0018 0D:000D:001A") != std::string::npos);
    CHECK(s.find(" 13:0013:0026\r\n") != std::string::npos);
    CHECK(s.find("0B:") == std::string::npos);   // already overwritten
}

static void test_format()
{
    Rec r = sample(kFault, 3);
    char out[512];
    uint32_t n = format(r, "1.0.0 CE-SEQ0.4.9 abc1234", out, sizeof out);
    CHECK(n > 0 && n < 400);
    std::string s(out);
    CHECK(s.rfind("--- crash ---\r\n", 0) == 0);
    CHECK(s.find("fw 1.0.0 CE-SEQ0.4.9 abc1234\r\n") != std::string::npos);
    CHECK(s.find("FAULT vec=03 pc=0802A1C4 lr=0801F3B1 psr=61000000 sp=2002FF40\r\n") != std::string::npos);
    CHECK(s.find("cfsr=00008200 hfsr=40000000 bfar=70123456 mmfar=DEADBEEF\r\n") != std::string::npos);
    CHECK(s.find("marks: 0A:0000:F000 0B:0003:F001 0C:0006:F002\r\n") != std::string::npos);
    CHECK(s.size() >= 4 && s.substr(s.size() - 4) == "\r\n\r\n");

    Rec h = sample(kHang, 0);               // no marks yet
    format(h, "x", out, sizeof out);
    s = out;
    CHECK(s.find("HANG vec=03") != std::string::npos);
    CHECK(s.find("marks:\r\n") != std::string::npos);

    // does not fit: 0, never overruns
    char small[40];
    memset(small, 'z', sizeof small);
    CHECK(format(r, "fw", small, sizeof small) == 0);
    char tight[512];
    uint32_t full = format(r, "fw", tight, sizeof tight);
    CHECK(full > 0);
    CHECK(format(r, "fw", tight, full) == 0);       // one byte short (snprintf needs the NUL)
    CHECK(format(r, "fw", tight, full + 1) == full);
}

static void test_screen()
{
    Rec r = sample(kHang, 2);
    char msg[128];
    screenFallback(r, msg, sizeof msg);
    std::string s(msg);
    CHECK(s.find("HANG pc=0802A1C4 lr=0801F3B1 cfsr=00008200") != std::string::npos);
    CHECK(s.find("Power off and on") != std::string::npos);
    CHECK(strlen(msg) < sizeof msg - 1);
}

int main()
{
    test_validate();
    test_ring();
    test_format();
    test_screen();
    printf("%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
