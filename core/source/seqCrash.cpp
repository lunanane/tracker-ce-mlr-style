// Sequencer tracks mod - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Crash and hang log (plan 032). The record is in SRAM .noinit (not EXTERNAL_RAM: it must survive the reset and needs no constructor).
// Fault vectors: strong definitions of the four weak aliases of the Teensy core. A naked stub picks MSP or PSP from EXC_RETURN bit 2 and
// hands the stacked frame to a C function that records it and resets. No SD work and nothing heavy happens in the handler.
#include "seqCrash.h"
#include "seqCrashLogic.h"
#include "Platform.h"

extern "C" void restartMCU(); // TrackerMain.cpp: saves the start state (so the reset boots, not sleeps) and resets

namespace seqcrash {

static Rec g_rec __attribute__((section(".noinit")));
static volatile uint32_t s_hb, s_seen, s_ms;
static volatile bool s_armed, s_in;

void begin()
{
    if (!valid(g_rec)) clear(g_rec); // power-up garbage or a half written record
}
void mark(uint8_t stage, uint16_t a, uint16_t b)
{
    if (g_rec.magic == kMagic) return; // the record of the last run is still waiting for the log
    put(g_rec, stage, a, b);
}
void beat() { s_hb = s_hb + 1; }
void arm(bool on)
{
    s_seen = s_hb;
    s_ms = 0;
    s_armed = on;
}
bool pending() { return valid(g_rec); }

void report(FileSystem* fs, const char* fw, char* scratch, uint32_t scratchCap, char* msg, uint32_t msgCap)
{
    bool ok = false;
    const uint32_t len = format(g_rec, fw, scratch, scratchCap);
    if (len)
    {
        auto f = fs->openFile("/crashlog.txt", FileSystem::kWriteAppend);
        if (std::get<0>(f))
        {
            ok = fs->writeFile(scratch, len, std::get<1>(f)) == (int32_t)len;
            fs->closeFile(std::get<1>(f));
        }
    }
    if (ok) snprintf(msg, msgCap, "Crash logged to crashlog.txt. Power off and on to retry.");
    else screenFallback(g_rec, msg, msgCap);
    clear(g_rec); // after this the marks run again and the next power cycle opens the project normally
}

static void record(uint32_t kind, const uint32_t* f)
{
    if (s_in) // the handler itself faulted: reset without further work
    {
        *(volatile uint32_t*)0xE000ED0C = 0x05FA0004u;
        for (;;) {}
    }
    s_in = true;
    __asm volatile("cpsid i");
    uint32_t ipsr;
    __asm volatile("mrs %0, ipsr" : "=r"(ipsr));
    g_rec.pc = g_rec.lr = g_rec.psr = 0xFFFFFFFFu;
    if ((uint32_t)f - 0x1FFF0000u < 0x50000u) // the frame is in SRAM (a wild stack pointer must not fault again)
    {
        g_rec.pc = f[6]; g_rec.lr = f[5]; g_rec.psr = f[7];
    }
    g_rec.sp = (uint32_t)f;
    g_rec.cfsr = *(volatile uint32_t*)0xE000ED28;
    g_rec.hfsr = *(volatile uint32_t*)0xE000ED2C;
    g_rec.mmfar = *(volatile uint32_t*)0xE000ED34;
    g_rec.bfar = *(volatile uint32_t*)0xE000ED38;
    seal(g_rec, kind | ((ipsr & 0x1FF) << 8));
    restartMCU();
    for (;;) {}
}

} // namespace seqcrash

extern "C" {
// frame = the stacked R0-R3, R12, LR, PC, xPSR of the faulting code
__attribute__((used)) void seqcrash_fault_c(const uint32_t* frame) { seqcrash::record(seqcrash::kFault, frame); }
// called from systick_isr (1 kHz) with the interrupted frame: while armed, a heartbeat that stands still for 10 s is a hang
void seqcrash_tick(const uint32_t* frame)
{
    using namespace seqcrash;
    if (!s_armed) return;
    if (s_hb != s_seen) { s_seen = s_hb; s_ms = 0; return; }
    if (++s_ms >= 10000) record(kHang, frame);
}
__attribute__((naked)) void seqcrash_fault_stub(void)
{
    __asm volatile("tst lr, #4\n\tite eq\n\tmrseq r0, msp\n\tmrsne r0, psp\n\tb seqcrash_fault_c\n\t");
}
void hard_fault_isr(void) __attribute__((alias("seqcrash_fault_stub")));
void memmanage_fault_isr(void) __attribute__((alias("seqcrash_fault_stub")));
void bus_fault_isr(void) __attribute__((alias("seqcrash_fault_stub")));
void usage_fault_isr(void) __attribute__((alias("seqcrash_fault_stub")));
}
