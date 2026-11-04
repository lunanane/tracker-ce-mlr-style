// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Host tests of the v0.2 step-0 foundations: performance-mode owner mask, panic latch, deferred-write gate, tap refusal, the
// generic sidecar frame and the per-clip play-mode table (plan 005).
#include "seqClipsLogic.h"
#include "seqModLogic.h"
#include "seqSidecarLogic.h"
#include "seqTracksLogic.h"
#include <stdio.h>
#include <string.h>

using namespace seqlogic;

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

// ---- owner mask: a tiny model of the firmware flag, exactly as Sequencer::enter/releasePerformanceMode use it ----
struct PerfModel
{
    PerfOwners owners;
    bool flag = false;
    void fxEnter()
    {
        owners.claim(PERF_OWNER_FX);
        flag = true;
    }
    void launcherEnter()
    {
        owners.claim(PERF_OWNER_LAUNCHER);
        flag = true;
    }
    bool release(uint8_t o) // Sequencer::releasePerformanceMode
    {
        if (!owners.release(o))
            return false;
        flag = false;
        return true;
    }
};

static void test_owner_mask()
{
    PerfOwners o;
    CHECK(!o.any());
    CHECK(o.claim(PERF_OWNER_FX));        // first claim switches the flag on
    CHECK(!o.claim(PERF_OWNER_LAUNCHER)); // second one does not
    CHECK(o.has(PERF_OWNER_FX) && o.has(PERF_OWNER_LAUNCHER));
    CHECK(o.othersThan(PERF_OWNER_FX) && o.othersThan(PERF_OWNER_LAUNCHER));
    CHECK(!o.release(PERF_OWNER_FX)); // the launcher still owns it
    CHECK(o.any() && !o.has(PERF_OWNER_FX));
    CHECK(!o.othersThan(PERF_OWNER_LAUNCHER));
    CHECK(o.release(PERF_OWNER_LAUNCHER)); // last owner: off
    CHECK(!o.any());
    CHECK(o.release(PERF_OWNER_FX)); // releasing a non-owner of an empty mask is harmless
    o.claim(PERF_OWNER_FX);
    o.claim(PERF_OWNER_FX); // idempotent
    CHECK(o.release(PERF_OWNER_FX));
}

static void test_launch_then_fader_page_keeps_clips()
{
    // launch a clip, enter and leave the fader page: the clip (flag) is still there
    PerfModel m;
    m.launcherEnter();
    m.fxEnter(); // fader page start
    CHECK(m.flag);
    CHECK(!m.release(PERF_OWNER_FX)); // fader page left: turnOff must not exit performance mode
    CHECK(m.flag && m.owners.has(PERF_OWNER_LAUNCHER));
    CHECK(m.release(PERF_OWNER_LAUNCHER)); // clips end at the pattern switch: now it goes off
    CHECK(!m.flag);
}

static void test_fader_then_launch_then_leave_performance()
{
    // the reverse: the Performance page owns the flag, clips are launched, the page is left
    PerfModel m;
    m.fxEnter();
    m.launcherEnter(); // seqSessionEnterPerformance with the flag already on claims too
    CHECK(!m.release(PERF_OWNER_FX));
    CHECK(m.flag); // the clips are not ended by leaving the page
    CHECK(m.release(PERF_OWNER_LAUNCHER));
    CHECK(!m.flag);
}

static void test_fx_only_behaves_like_stock()
{
    PerfModel m;
    m.fxEnter();
    CHECK(m.release(PERF_OWNER_FX) && !m.flag); // stock: leaving the page switches it off (caller reloads the pattern)
}

static void test_panic_latch()
{
    PanicLatch p;
    CHECK(!p.take());
    p.request();
    p.request(); // several Stops before the main loop looks: one panic
    CHECK(p.take());
    CHECK(!p.take());
}

static void test_dirty_gate()
{
    DirtyGate g;
    CHECK(!g.take(false));
    g.mark();
    CHECK(!g.take(true)); // playing: nothing is written, the flag stays
    CHECK(!g.take(true));
    CHECK(g.take(false)); // transport stopped: once
    CHECK(!g.take(false));
    g.mark();
    g.mark();
    CHECK(g.take(false) && !g.take(false));
}

static void test_tap_refused()
{
    TapDetector t;
    t.down(true, 1000);
    CHECK(t.up(1100) && !t.refusedLast());
    // another button during the press: quick = refused
    t.down(true, 1000);
    t.other();
    CHECK(!t.up(1200) && t.refusedLast());
    // another button already held on the press
    t.down(false, 1000);
    CHECK(!t.up(1100) && t.refusedLast());
    // a long hold is a hold, not a refused tap
    t.down(true, 1000);
    CHECK(!t.up(1500) && !t.refusedLast());
    // release without press
    CHECK(!t.up(1600) && !t.refusedLast());
    // pads and the jog wheel do not call other() (the router only counts buttons): a tap survives them
    t.down(true, 2000);
    CHECK(t.up(2100));
    // wrap-around of the 32-bit clock
    t.down(true, 0xFFFFFF00u);
    t.other();
    CHECK(!t.up(0x40u) && t.refusedLast());
}

static void test_sidecar_frame()
{
    const char magic[4] = { 'T', 'E', 'S', 'T' };
    uint8_t payload[10], buf[64];
    for (int i = 0; i < 10; i++)
        payload[i] = (uint8_t)(i * 7 + 1);
    size_t n = seqsidecar::frameWrite(buf, sizeof(buf), magic, 3, payload, 10);
    CHECK(n == 10 + seqsidecar::kOverhead);
    CHECK(seqsidecar::frameWrite(buf, 11, magic, 3, payload, 10) == 0); // buffer too small
    const uint8_t* p = nullptr;
    size_t pl = 0;
    uint8_t v = 0;
    CHECK(seqsidecar::frameRead(buf, n, magic, 3, &p, &pl, &v) == seqsidecar::FRAME_OK);
    CHECK(pl == 10 && v == 3 && memcmp(p, payload, 10) == 0);
    // trailing bytes after the frame are ignored (the shared buffer holds older data)
    buf[n] = 0x55;
    CHECK(seqsidecar::frameRead(buf, n + 5, magic, 3, &p, &pl, &v) == seqsidecar::FRAME_OK);
    // newer version: left alone
    CHECK(seqsidecar::frameRead(buf, n, magic, 2, &p, &pl, &v) == seqsidecar::FRAME_NEWER);
    // wrong magic, short file, wrong length
    const char other[4] = { 'N', 'O', 'P', 'E' };
    CHECK(seqsidecar::frameRead(buf, n, other, 3, &p, &pl, &v) == seqsidecar::FRAME_BAD);
    CHECK(seqsidecar::frameRead(buf, n - 1, magic, 3, &p, &pl, &v) == seqsidecar::FRAME_BAD);
    CHECK(seqsidecar::frameRead(buf, 5, magic, 3, &p, &pl, &v) == seqsidecar::FRAME_BAD);
    CHECK(seqsidecar::frameRead(nullptr, 50, magic, 3, &p, &pl, &v) == seqsidecar::FRAME_BAD);
    // any single flipped bit is caught (magic, version, length or CRC changes)
    for (size_t i = 0; i < n; i++)
    {
        for (int bit = 0; bit < 8; bit++)
        {
            uint8_t c[64];
            memcpy(c, buf, n);
            c[i] = (uint8_t)(c[i] ^ (1u << bit));
            CHECK(seqsidecar::frameRead(c, n, magic, 3, &p, &pl, &v) != seqsidecar::FRAME_OK);
        }
    }
    // payload written in place
    memcpy(buf + seqsidecar::kHeader, payload, 10);
    CHECK(seqsidecar::frameWrite(buf, sizeof(buf), magic, 3, buf + seqsidecar::kHeader, 10) == n);
    CHECK(seqsidecar::frameRead(buf, n, magic, 3, &p, &pl, &v) == seqsidecar::FRAME_OK && memcmp(p, payload, 10) == 0);
    // the CRC is the one of seqtracks.bin (CRC-16/CCITT-FALSE, check value of "123456789" = 0x29B1)
    CHECK(seqsidecar::crc16((const uint8_t*)"123456789", 9) == 0x29B1);
}

static void test_clip_modes()
{
    using namespace seqclips;
    ClipModes m;
    CHECK(!m.anyNonDefault());
    CHECK(m.mode(1, 0) == CLIP_LOOP && m.mode(255, 15) == CLIP_LOOP);
    CHECK(m.mode(0, 0) == CLIP_LOOP && m.mode(1, 16) == CLIP_LOOP); // out of range reads as Loop
    CHECK(!m.setMode(0, 0, CLIP_PLAY1X) && !m.setMode(1, 16, CLIP_PLAY1X) && !m.setMode(1, 0, 2)); // ignored
    uint16_t h0 = m.hash();
    CHECK(m.setMode(1, 0, CLIP_PLAY1X));
    CHECK(!m.setMode(1, 0, CLIP_PLAY1X)); // unchanged
    CHECK(m.mode(1, 0) == CLIP_PLAY1X && m.anyNonDefault() && m.hash() != h0);
    // neighbours are not touched, ends of the table work
    CHECK(m.mode(1, 1) == CLIP_LOOP && m.mode(2, 0) == CLIP_LOOP);
    CHECK(m.setMode(255, 15, CLIP_PLAY1X) && m.mode(255, 15) == CLIP_PLAY1X && m.mode(255, 14) == CLIP_LOOP);
    // scene row
    CHECK(m.setRow(7, CLIP_PLAY1X));
    for (uint8_t t = 0; t < seqclips::kTracks; t++)
        CHECK(m.mode(7, t) == CLIP_PLAY1X);
    CHECK(!m.setRow(7, CLIP_PLAY1X));
    CHECK(m.setRow(7, CLIP_LOOP) && m.mode(7, 3) == CLIP_LOOP);

    // save / load round trip
    uint8_t buf[kFileMax + 16];
    size_t n = m.save(buf, sizeof(buf));
    CHECK(n == (size_t)kFileMax);
    CHECK(m.save(buf, kFileMax - 1) == 0);
    ClipModes r;
    bool newer = true;
    CHECK(r.load(buf, n, &newer) && !newer);
    CHECK(r.hash() == m.hash() && r.mode(1, 0) == CLIP_PLAY1X && r.mode(255, 15) == CLIP_PLAY1X && r.mode(2, 0) == CLIP_LOOP);
    // bad CRC / short file: nothing changes
    ClipModes q;
    q.setMode(3, 3, CLIP_PLAY1X);
    uint16_t hq = q.hash();
    buf[100] ^= 1;
    CHECK(!q.load(buf, n) && q.hash() == hq);
    buf[100] ^= 1;
    CHECK(!q.load(buf, n - 1) && q.hash() == hq);
    CHECK(!q.load(buf, 0) && q.hash() == hq);
    // newer version is reported and nothing is loaded
    uint8_t nb[kFileMax];
    uint8_t payload[kPayload];
    memset(payload, 0, sizeof(payload));
    seqsidecar::frameWrite(nb, sizeof(nb), kMagic, kVersion + 1, payload, kPayload);
    CHECK(!q.load(nb, sizeof(nb), &newer) && newer && q.hash() == hq);
    // wrong payload length
    size_t sn = seqsidecar::frameWrite(nb, sizeof(nb), kMagic, kVersion, payload, kPayload - 4);
    CHECK(!q.load(nb, sn) && q.hash() == hq);
    // reserved mode values (2, 3) in a file read back as Loop, the other entries survive (per-entry fallback)
    memset(payload, 0, sizeof(payload));
    payload[0] = 0x0E; // clip (1,0) = 2 (bits 1..0), clip (1,1) = 3 (bits 3..2)
    payload[1] = 0x01; // clip (1,4) = 1 (Play 1x)
    sn = seqsidecar::frameWrite(nb, sizeof(nb), kMagic, kVersion, payload, kPayload);
    CHECK(sn == (size_t)kFileMax && q.load(nb, sn));
    CHECK(q.mode(1, 0) == CLIP_LOOP && q.mode(1, 1) == CLIP_LOOP && q.mode(1, 4) == CLIP_PLAY1X);
    // cleaned entries are not stored back as reserved values
    n = q.save(buf, sizeof(buf));
    ClipModes q2;
    CHECK(q2.load(buf, n) && q2.mode(1, 0) == CLIP_LOOP && q2.mode(1, 4) == CLIP_PLAY1X && q2.hash() == q.hash());
}

int main()
{
    test_owner_mask();
    test_launch_then_fader_page_keeps_clips();
    test_fader_then_launch_then_leave_performance();
    test_fx_only_behaves_like_stock();
    test_panic_latch();
    test_dirty_gate();
    test_tap_refused();
    test_sidecar_frame();
    test_clip_modes();
    printf("seqmod: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
