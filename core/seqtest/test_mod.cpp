// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Host tests of the v0.2 step-0 foundations: performance-mode owner mask, panic latch, deferred-write gate, tap refusal, the
// generic sidecar frame and the per-clip play-mode table (plan 005).
#include "seqClipsLogic.h"
#include "seqModLogic.h"
#include "seqSidecarLogic.h"
#include "seqSongLogic.h"
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
    sn = seqsidecar::frameWrite(nb, sizeof(nb), kMagic, kVersionV1, payload, kPayload); // a v0.2 file (plan 006: version 2 has the scene flags too)
    CHECK(sn == (size_t)kPayload + seqsidecar::kOverhead && q.load(nb, sn));
    CHECK(q.mode(1, 0) == CLIP_LOOP && q.mode(1, 1) == CLIP_LOOP && q.mode(1, 4) == CLIP_PLAY1X);
    // cleaned entries are not stored back as reserved values
    n = q.save(buf, sizeof(buf));
    ClipModes q2;
    CHECK(q2.load(buf, n) && q2.mode(1, 0) == CLIP_LOOP && q2.mode(1, 4) == CLIP_PLAY1X && q2.hash() == q.hash());
}

// ---- plan 006: scene flag "play next" in seqclips.bin (version 2) ----
static void test_scene_flags()
{
    using namespace seqclips;
    ClipModes m;
    CHECK(kVersion == 2 && kVersionV1 == 1 && kPayloadV2 == kPayload + 32 && kFileMax == kPayloadV2 + seqsidecar::kOverhead);
    // default: every scene loops
    for (int p = 0; p <= 255; p++)
        CHECK(!m.playNext((uint8_t)p));
    CHECK(!m.anyNonDefault());
    uint16_t h0 = m.hash();
    // set / clear / unchanged / out of range
    CHECK(m.setPlayNext(1, true) && m.playNext(1) && !m.playNext(2) && m.anyNonDefault() && m.hash() != h0);
    CHECK(!m.setPlayNext(1, true));
    CHECK(m.setPlayNext(255, true) && m.playNext(255) && !m.playNext(254));
    CHECK(m.setPlayNext(9, true) && m.playNext(9) && !m.playNext(8) && !m.playNext(10)); // byte boundary
    CHECK(!m.setPlayNext(0, true) && !m.playNext(0));
    CHECK(m.setPlayNext(9, false) && !m.playNext(9));
    CHECK(m.setPlayNext(1, false) && m.setPlayNext(255, false) && !m.anyNonDefault() && m.hash() == h0);
    // the scene flag does not touch the clip modes and the other way round
    CHECK(m.setPlayNext(7, true) && m.mode(7, 0) == CLIP_LOOP);
    CHECK(m.setMode(7, 0, CLIP_PLAY1X) && m.playNext(7));

    // round trip (version 2)
    m.clear();
    m.setPlayNext(3, true);
    m.setPlayNext(255, true);
    m.setMode(4, 2, CLIP_PLAY1X);
    uint8_t buf[kFileMax + 8];
    size_t n = m.save(buf, sizeof(buf));
    CHECK(n == (size_t)kFileMax && buf[4] == 2);
    CHECK(m.save(buf, kFileMax - 1) == 0);
    ClipModes r;
    bool newer = true;
    CHECK(r.load(buf, n, &newer) && !newer && r.playNext(3) && r.playNext(255) && !r.playNext(4) && r.mode(4, 2) == CLIP_PLAY1X && r.hash() == m.hash());
    // a version 2 frame with a v1 length (or the other way round) is refused
    uint8_t payload[kPayloadV2];
    memset(payload, 0, sizeof(payload));
    uint8_t fb[kFileMax];
    ClipModes q;
    q.setPlayNext(5, true);
    uint16_t hq = q.hash();
    size_t sn = seqsidecar::frameWrite(fb, sizeof(fb), kMagic, kVersion, payload, kPayload);
    CHECK(!q.load(fb, sn) && q.hash() == hq);
    sn = seqsidecar::frameWrite(fb, sizeof(fb), kMagic, kVersionV1, payload, kPayloadV2);
    CHECK(!q.load(fb, sn) && q.hash() == hq);
    // a v0.2 file (version 1, clip modes only) loads with every scene flag = Loop and keeps its clip modes
    memset(payload, 0, sizeof(payload));
    payload[0] = 0x01; // clip (1,0) = Play 1x
    sn = seqsidecar::frameWrite(fb, sizeof(fb), kMagic, kVersionV1, payload, kPayload);
    ClipModes v1;
    v1.setPlayNext(9, true); // loading replaces everything, also the flags
    CHECK(v1.load(fb, sn) && v1.mode(1, 0) == CLIP_PLAY1X);
    for (int p = 0; p <= 255; p++)
        CHECK(!v1.playNext((uint8_t)p));
    // ... and saves as version 2 from then on
    n = v1.save(buf, sizeof(buf));
    CHECK(n == (size_t)kFileMax && buf[4] == 2 && r.load(buf, n) && r.mode(1, 0) == CLIP_PLAY1X && !r.playNext(3));
    // reserved mode values in a version 2 frame are cleaned to Loop too, the scene flags beside them survive
    memset(payload, 0, sizeof(payload));
    payload[0] = 0x0E;                 // clip (1,0) = 2, clip (1,1) = 3 (reserved)
    payload[1] = 0x01;                 // clip (1,4) = Play 1x
    payload[kPayload + 0] = 0x04;      // scene flag of pattern 3
    sn = seqsidecar::frameWrite(fb, sizeof(fb), kMagic, kVersion, payload, kPayloadV2);
    CHECK(sn == (size_t)kFileMax && q.load(fb, sn));
    CHECK(q.mode(1, 0) == CLIP_LOOP && q.mode(1, 1) == CLIP_LOOP && q.mode(1, 4) == CLIP_PLAY1X && q.playNext(3) && !q.playNext(1));
    n = q.save(buf, sizeof(buf));
    ClipModes q3;
    CHECK(q3.load(buf, n) && q3.hash() == q.hash() && q3.playNext(3));
    // a version 2 file with the unused bit 255 set: the bit is dropped, the other flags stay
    memset(payload, 0, sizeof(payload));
    payload[kPayload + 31] = 0x80 | 0x01; // bit 255 (no pattern) + pattern 249
    sn = seqsidecar::frameWrite(fb, sizeof(fb), kMagic, kVersion, payload, kPayloadV2);
    CHECK(q.load(fb, sn) && q.playNext(249) && !q.playNext(250));
    // a newer version is reported and nothing is loaded
    uint16_t hq2 = q.hash();
    sn = seqsidecar::frameWrite(fb, sizeof(fb), kMagic, kVersion + 1, payload, kPayloadV2);
    CHECK(!q.load(fb, sn, &newer) && newer && q.hash() == hq2);
    // bad CRC: nothing changes
    n = m.save(buf, sizeof(buf));
    buf[kPayload + 20] ^= 1;
    CHECK(!q.load(buf, n) && q.hash() == hq2);
}

// ---- plan 006: song advance decision (loop by default), latch, queue ----
static void test_song_advance()
{
    using namespace seqsong;
    // (playNext, lastSlot, exporting)
    // not flagged: the slot plays again, whatever the position (a pattern break effect is a shortened pass of the same loop, owner: loop
    // by default, so it uses this rule too)
    CHECK(advance(false, false, false) == ADV_STAY);
    CHECK(advance(false, true, false) == ADV_STAY);
    // flagged: the song goes on to the next slot (also when a break effect shortens the pass)
    CHECK(advance(true, false, false) == ADV_NEXT);
    // flagged on the last slot: the song does not end, the slot loops
    CHECK(advance(true, true, false) == ADV_STAY);
    // audio export: the stock song (and its end)
    CHECK(advance(false, false, true) == ADV_NEXT && advance(true, true, true) == ADV_NEXT && advance(false, true, true) == ADV_NEXT);

    // latch: the answer of the last step (where the next pattern is loaded) is the one the pattern end acts on
    Latch l;
    CHECK(l.resolve(false) == ADV_STAY && l.v == 0);                    // no answer = nothing was loaded: stay (never an unloaded buffer)
    CHECK(l.resolve(true) == ADV_NEXT && l.v == 0);                     // ... except the audio export: stock
    CHECK(l.decide(ADV_STAY) == ADV_STAY && l.v == 1);
    CHECK(l.resolve(false) == ADV_STAY && l.v == 0);                    // the flag flipped after the last step: nothing was loaded, stay
    CHECK(l.decide(ADV_NEXT) == ADV_NEXT && l.v == 2);
    CHECK(l.resolve(false) == ADV_NEXT && l.v == 0);                    // the next pattern was loaded (last step or break effect): take it
    CHECK(l.decide(ADV_STAY) == ADV_STAY && l.resolve(true) == ADV_STAY); // an answer beats the export default
    l.decide(ADV_NEXT);
    l.clear();
    CHECK(l.v == 0);

    // queue: press on a slot (playing, slot, playingSlot, patternAtSlot, queuedSlot1)
    CHECK(queueOp(false, 3, 0, 5, 0) == QOP_NONE);       // stopped
    CHECK(queueOp(true, 3, 0, 0, 0) == QOP_NONE);        // empty slot
    CHECK(queueOp(true, 3, 0, 5, 0) == QOP_QUEUE);
    CHECK(queueOp(true, 4, 0, 6, 4) == QOP_QUEUE);       // another slot moves the queue
    CHECK(queueOp(true, 0, 0, 5, 0) == QOP_NONE);        // the playing slot: nothing to queue
    CHECK(queueOp(true, 0, 0, 5, 4) == QOP_CANCEL);      // ... but it cancels a pending queue
    // a slot that holds the loaded pattern needs no pattern switch (the position moves at the pattern end)
    CHECK(!queueNeedsSwitch(5, 5) && queueNeedsSwitch(6, 5));
    // shown only while the stock switch is armed for that slot's pattern
    CHECK(queuedShown(4, 6, 6) == 4);
    CHECK(queuedShown(4, 6, 7) == 0 && queuedShown(4, 6, 0) == 0 && queuedShown(0, 6, 6) == 0 && queuedShown(4, 0, 0) == 0);
    // after the switch: the queued slot wins over the stock search when its pattern plays now
    CHECK(slotAfterSwitch(4, 6, 6) == 3);
    CHECK(slotAfterSwitch(4, 6, 7) == -1 && slotAfterSwitch(0, 6, 6) == -1);
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
    test_scene_flags();
    test_song_advance();
    printf("seqmod: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
