// Sequencer tracks mod - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Host tests of the master recording (plan 030 A): ring, WAV header, file names, write scheduler, session (stall simulation with a fake card),
// exclusivity, errors. The audio runs in simulated time: one block of 128 frames every 2902 us (44 100 Hz), the main loop every 1 ms.
#include "seqMasterRecLogic.h"
#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>
#include <set>
#include <stdlib.h>

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

using namespace seqrec;

// ---- simulation: time, audio source, fake card ----
struct Sim : Io
{
    Ring* ring = nullptr;
    Session* sess = nullptr;
    uint64_t us = 0;
    uint64_t nextBlockUs = 0;
    uint32_t sampleIdx = 0;
    std::vector<int16_t> pushed; // everything the "audio ISR" put into the ring (interleaved)
    uint32_t blocksSent = 0;
    // card
    bool card = true;
    bool hasFree = true;
    uint64_t freeB = 100ull << 20;
    bool dirExists = false, dirCreateOk = true, createOk = true;
    std::set<std::string> files;
    std::vector<uint8_t> data; // the open file
    uint32_t pos = 0;
    bool open = false;
    int headerRewrites = 0;
    uint32_t writeUs = 3000;                // time of a normal write
    uint32_t stallEveryN = 0, stallUs = 0; // every Nth write takes stallUs
    int writeCount = 0;
    int failAtWrite = -1; // the Nth write is short
    int stallAtWrite = -1; // the Nth write takes stallUs (once)
    bool busy = false;
    uint32_t busyUntilMs = 0;
    uint32_t maxUsedSeen = 0;
    std::string created;

    void audioTo(uint64_t t)
    {
        while (nextBlockUs <= t)
        {
            int16_t l[kBlockFrames], r[kBlockFrames];
            for (uint32_t i = 0; i < kBlockFrames; i++)
            {
                l[i] = (int16_t)(sampleIdx * 7u);
                r[i] = (int16_t)(sampleIdx * 13u + 5u);
                sampleIdx++;
            }
            if (sess->tapOn)
            {
                if (ring->pushBlock(l, r, kBlockFrames))
                    for (uint32_t i = 0; i < kBlockFrames; i++)
                    {
                        pushed.push_back(l[i]);
                        pushed.push_back(r[i]);
                    }
                blocksSent++;
                if (ring->used() > maxUsedSeen)
                    maxUsedSeen = ring->used();
            }
            nextBlockUs += 2902;
        }
    }
    void advance(uint32_t dus)
    {
        us += dus;
        audioTo(us);
    }
    // Io
    bool cardPresent() override { return card; }
    bool freeBytes(uint64_t& out) override
    {
        out = freeB;
        return hasFree;
    }
    bool exists(const char* p) override { return std::string(p) == "/Recorded" ? dirExists : files.count(p) != 0; }
    bool makeDir(const char*) override
    {
        if (dirCreateOk)
            dirExists = true;
        return dirCreateOk;
    }
    bool create(const char* p) override
    {
        if (!createOk)
            return false;
        files.insert(p);
        created = p;
        data.clear();
        pos = 0;
        open = true;
        return true;
    }
    int32_t write(const void* d, uint32_t n) override
    {
        writeCount++;
        uint32_t dur = writeUs;
        if ((stallEveryN && (writeCount % stallEveryN) == 0) || writeCount == stallAtWrite)
            dur = stallUs;
        advance(dur);
        if (failAtWrite == writeCount)
            n /= 2;
        if (data.size() < pos + n)
            data.resize(pos + n);
        memcpy(&data[pos], d, n);
        pos += n;
        return (int32_t)n;
    }
    bool rewriteHeader(const uint8_t* hdr, uint32_t endPos) override
    {
        advance(2000);
        memcpy(&data[0], hdr, kHeaderBytes);
        pos = endPos;
        headerRewrites++;
        return true;
    }
    void close() override { open = false; }
    uint32_t nowMs() override { return (uint32_t)(us / 1000u); }
    bool asyncBusy() override { return busy && nowMs() < busyUntilMs; }
};

struct Rig
{
    static uint32_t* mem()
    {
        static std::vector<uint32_t> m(kRingBytes / 4);
        return m.data();
    }
    Ring ring;
    Session s;
    Sim sim;
    Rig()
    {
        ring.init(mem(), kRingBytes);
        sim.ring = &ring;
        sim.sess = &s;
        s.init(&ring, &sim);
    }
    // run the main loop for ms milliseconds (a pass every 1 ms)
    void run(uint32_t ms)
    {
        for (uint32_t i = 0; i < ms; i++)
        {
            sim.advance(1000);
            s.update();
        }
    }
};

static uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static bool fileMatchesPushed(const Rig& r)
{
    const uint32_t bytes = (uint32_t)r.sim.data.size() - kHeaderBytes;
    if (bytes > r.sim.pushed.size() * 2)
        return false;
    return memcmp(&r.sim.data[kHeaderBytes], r.sim.pushed.data(), bytes) == 0;
}

static void test_header()
{
    uint8_t h[kHeaderBytes];
    wavHeader(h, 1000);
    CHECK(memcmp(h, "RIFF", 4) == 0 && le32(h + 4) == 1036 && memcmp(h + 8, "WAVEfmt ", 8) == 0 && le32(h + 16) == 16);
    CHECK(h[20] == 1 && h[21] == 0 && h[22] == 2 && h[23] == 0); // PCM, 2 channels
    CHECK(le32(h + 24) == 44100 && le32(h + 28) == 176400);      // rate, byte rate
    CHECK(h[32] == 4 && h[33] == 0 && h[34] == 16 && h[35] == 0); // block align, bits
    CHECK(memcmp(h + 36, "data", 4) == 0 && le32(h + 40) == 1000);
    wavHeader(h, 0xFFFFFFF0u - 44u);
    CHECK(le32(h + 40) == 0xFFFFFFF0u - 44u && le32(h + 4) == 0xFFFFFFF0u - 8u);
    CHECK(sizeof(h) == 44);
}

static void test_ring()
{
    std::vector<uint32_t> m(kRingBytes / 4);
    Ring r;
    r.init(m.data(), kRingBytes);
    CHECK(r.used() == 0);
    int16_t l[kBlockFrames], rr[kBlockFrames];
    for (int i = 0; i < (int)kBlockFrames; i++)
    {
        l[i] = (int16_t)(i - 64);
        rr[i] = (int16_t)(-i * 100);
    }
    CHECK(r.pushBlock(l, rr, kBlockFrames));
    CHECK(r.used() == kBlockFrames * 4u);
    uint32_t n;
    const uint8_t* p = r.peek(n);
    CHECK(n == 512);
    const int16_t* q = (const int16_t*)p;
    CHECK(q[0] == l[0] && q[1] == rr[0] && q[2] == l[1] && q[3] == rr[1] && q[254] == l[127] && q[255] == rr[127]); // interleaved L R
    r.consume(512);
    CHECK(r.used() == 0);
    // silence for missing blocks
    CHECK(r.pushBlock(nullptr, nullptr, kBlockFrames));
    p = r.peek(n);
    bool z = true;
    for (uint32_t i = 0; i < 512; i++)
        z = z && p[i] == 0;
    CHECK(z);
    r.consume(512);
    // fill to the brim: capacity = size - one block; then a drop is counted, nothing is overwritten
    r.init(m.data(), kRingBytes);
    int pushed = 0;
    while (r.pushBlock(l, rr, kBlockFrames))
        pushed++;
    CHECK(pushed == (int)(kRingBytes / 512) - 1);
    CHECK(r.dropped == 1 && r.used() == kRingBytes - 512);
    CHECK(r.highPercent() == 99);
    // steady state across many wraps: blocks stay in order, the contiguous run never crosses the end
    r.init(m.data(), kRingBytes);
    uint32_t expect = 0;
    for (int i = 0; i < 2000; i++)
    {
        int16_t a[kBlockFrames], b[kBlockFrames];
        for (int k = 0; k < (int)kBlockFrames; k++)
        {
            a[k] = (int16_t)(i * 3 + k);
            b[k] = (int16_t)(-i * 3 - k);
        }
        CHECK(r.pushBlock(a, b, kBlockFrames));
        if (r.used() >= 3 * 512u)
        {
            const uint8_t* pp = r.peek(n);
            CHECK(n >= 512 && n <= r.used());
            const int16_t* s = (const int16_t*)pp;
            const int blk = (int)expect;
            CHECK(s[0] == (int16_t)(blk * 3) && s[1] == (int16_t)(-blk * 3) && s[2] == (int16_t)(blk * 3 + 1));
            r.consume(512);
            expect++;
        }
    }
    CHECK(r.wr < r.size && r.rd < r.size);
}

static void test_names()
{
    char b[96];
    fileName(b, sizeof(b), "MyProject", 7);
    CHECK(strcmp(b, "/Recorded/MyProject_007.wav") == 0);
    fileName(b, sizeof(b), "a/b:c*d", 123);
    CHECK(strcmp(b, "/Recorded/a_b_c_d_123.wav") == 0);
    fileName(b, sizeof(b), "", 1);
    CHECK(strcmp(b, "/Recorded/Master_001.wav") == 0);
    fileName(b, sizeof(b), nullptr, 999);
    CHECK(strcmp(b, "/Recorded/Master_999.wav") == 0);
    fileName(b, sizeof(b), "0123456789012345678901234567890123456789012345678901234567890123456789", 2);
    CHECK(strlen(b) < sizeof(b) && strstr(b, "_002.wav") != nullptr);
    char tiny[12];
    fileName(tiny, sizeof(tiny), "Project", 5);
    CHECK(strlen(tiny) < sizeof(tiny));
}

static void test_scheduler()
{
    const uint32_t S = kRingBytes;
    CHECK(!shouldWrite(kChunkBytes - 4, S, false, 1000, 0)); // less than a chunk
    CHECK(shouldWrite(kChunkBytes, S, false, 1000, 0));      // enough, idle card, gap over
    CHECK(!shouldWrite(kChunkBytes, S, true, 1000, 0));      // stream read in flight: wait
    CHECK(!shouldWrite(kChunkBytes, S, false, 1005, 1000));  // last write 5 ms ago
    CHECK(shouldWrite(kChunkBytes, S, false, 1006, 1000));   // 6 ms ago
    CHECK(!shouldWrite(S / 4 * 3, S, true, 1000, 999));      // exactly 75 %: not forced
    CHECK(shouldWrite(S / 4 * 3 + 4, S, true, 1000, 999));   // more than 75 %: forced, whatever the streams do
    CHECK(shouldWrite(S - 512, S, true, 1000, 1000));
    CHECK(shouldWrite(kChunkBytes, S, false, 5, 0xFFFFFFF0u)); // millisecond counter wrap
}

static void test_start_errors()
{
    {
        Rig r;
        CHECK(r.s.start("P", true, false) == E_BUSY_EXPORT && !r.s.active() && !r.s.tapOn);
        CHECK(r.s.start("P", false, true) == E_BUSY_SAMPLEREC && !r.s.active());
        CHECK(r.s.start("P", true, true) == E_BUSY_EXPORT);
        r.sim.card = false;
        CHECK(r.s.start("P", false, false) == E_NO_CARD);
        r.sim.card = true;
        r.sim.freeB = 4u << 20;
        CHECK(r.s.start("P", false, false) == E_NO_SPACE);
        r.sim.hasFree = false; // free space unknown: allowed
        CHECK(r.s.start("P", false, false) == E_NONE && r.s.active());
        r.s.stop();
    }
    {
        Rig r;
        r.sim.dirCreateOk = false;
        CHECK(r.s.start("P", false, false) == E_CREATE && !r.s.active());
        r.sim.dirCreateOk = true;
        r.sim.createOk = false;
        CHECK(r.s.start("P", false, false) == E_CREATE && !r.s.active() && !r.s.tapOn);
    }
    for (int e = 1; e < E_COUNT; e++)
        CHECK(errText((Err)e)[0] != 0);
    CHECK(errText((Err)200)[0] == 0);
}

static void test_record_plain()
{
    Rig r;
    // existing files are skipped
    r.sim.dirExists = true;
    r.sim.files.insert("/Recorded/Song_001.wav");
    r.sim.files.insert("/Recorded/Song_002.wav");
    CHECK(r.s.start("Song", false, false) == E_NONE);
    CHECK(r.sim.created == "/Recorded/Song_003.wav");
    r.run(65000); // 65 s
    CHECK(r.s.active() && r.ring.dropped == 0);
    CHECK(r.sim.headerRewrites == 2); // at 30 s and at 60 s
    CHECK(r.s.stats.longestWriteMs >= 3 && r.s.stats.longestWriteMs <= 5);
    CHECK(r.s.seconds() + 1 >= r.sim.us / 1000000u && r.s.seconds() <= r.sim.us / 1000000u + 1); // the clock counts the writes too
    const Err e = r.s.stop();
    CHECK(e == E_NONE && !r.s.active() && !r.s.tapOn && !r.sim.open);
    // file: header with the final data size, data identical to what the audio produced (nothing lost, nothing reordered)
    CHECK(r.sim.data.size() == kHeaderBytes + r.s.dataBytes);
    CHECK(le32(&r.sim.data[40]) == r.s.dataBytes && le32(&r.sim.data[4]) == 36 + r.s.dataBytes);
    CHECK(r.s.dataBytes == r.sim.pushed.size() * 2);
    CHECK(fileMatchesPushed(r));
    CHECK(r.ring.used() == 0);
    CHECK(r.ring.highPercent() < 30); // a calm card: the ring stays nearly empty
    printf("  plain 65 s: %u writes, longest %u ms, ring high-water %u %%, data %u bytes\n", (unsigned)r.s.stats.writes,
        (unsigned)r.s.stats.longestWriteMs, (unsigned)r.ring.highPercent(), (unsigned)r.s.dataBytes);
}

static void test_stall()
{
    // a 250 ms write stall (every 7th write) with the ring at its usual level: no block may be lost
    {
        Rig r;
        r.sim.dirExists = true;
        r.sim.stallEveryN = 7;
        r.sim.stallUs = 250000;
        CHECK(r.s.start("S", false, false) == E_NONE);
        r.run(120000);
        r.s.stop();
        CHECK(r.ring.dropped == 0);
        CHECK(r.s.stats.longestWriteMs >= 250 && r.s.stats.longestWriteMs <= 256);
        CHECK(fileMatchesPushed(r) && r.s.dataBytes == r.sim.pushed.size() * 2);
        printf("  250 ms stall every 7th write, 120 s: dropped %u, ring high-water %u %%, longest %u ms\n", (unsigned)r.ring.dropped,
            (unsigned)r.ring.highPercent(), (unsigned)r.s.stats.longestWriteMs);
    }
    // the worst case: the streams keep the card busy the whole time, so nothing is written until the ring is forced (75 %), and the
    // first forced write stalls for 250 ms; the audio goes on and fills the ring in the meantime
    {
        Rig r;
        r.sim.dirExists = true;
        r.sim.busy = true;
        r.sim.busyUntilMs = 0xFFFFFFFFu;
        r.sim.stallAtWrite = 2; // write 1 is the start header
        r.sim.stallUs = 250000;
        CHECK(r.s.start("W", false, false) == E_NONE);
        r.run(20000);
        r.s.stop();
        CHECK(r.ring.dropped == 0);
        CHECK(r.s.stats.forced > 0 && r.s.stats.longestWriteMs >= 250);
        CHECK(r.sim.maxUsedSeen > kRingBytes / 4 * 3 && r.sim.maxUsedSeen < kRingBytes - 512);
        CHECK(fileMatchesPushed(r));
        printf("  card busy all the time, first forced write 250 ms: dropped %u, ring high-water %u %%, forced writes %u\n", (unsigned)r.ring.dropped,
            (unsigned)r.ring.highPercent(), (unsigned)r.s.stats.forced);
    }
    // the card busy for 3 s, then free: writes are deferred, forced at 75 %, nothing lost
    {
        Rig r;
        r.sim.dirExists = true;
        r.sim.busy = true;
        r.sim.busyUntilMs = 3000;
        CHECK(r.s.start("B", false, false) == E_NONE);
        r.run(10000);
        r.s.stop();
        CHECK(r.ring.dropped == 0 && r.s.stats.forced > 0);
        CHECK(fileMatchesPushed(r));
    }
    // a stall longer than the ring (1.3 s) really loses blocks - and counts them
    {
        Rig r;
        r.sim.dirExists = true;
        r.sim.stallEveryN = 10;
        r.sim.stallUs = 1300000;
        CHECK(r.s.start("L", false, false) == E_NONE);
        r.run(30000);
        r.s.stop();
        CHECK(r.ring.dropped > 0);
        CHECK(r.s.dataBytes == r.sim.pushed.size() * 2 && fileMatchesPushed(r)); // what was kept is intact
    }
}

static void test_errors_and_limits()
{
    // a short write (card full): clean stop with the message, the header says how much is in the file
    {
        Rig r;
        r.sim.dirExists = true;
        r.sim.failAtWrite = 5;
        CHECK(r.s.start("F", false, false) == E_NONE);
        r.run(5000);
        CHECK(!r.s.active() && !r.s.tapOn && !r.sim.open);
        CHECK(r.s.lastError == E_WRITE);
        CHECK(le32(&r.sim.data[40]) == r.s.dataBytes);
        CHECK(r.s.stop() == E_WRITE); // a stop after the error reports it again, changes nothing
    }
    // the file limit: stops cleanly just below 4 GB
    {
        Rig r;
        r.sim.dirExists = true;
        CHECK(r.s.start("G", false, false) == E_NONE);
        r.s.dataBytes = kMaxDataBytes - 100000;
        r.run(5000);
        CHECK(!r.s.active() && r.s.lastError == E_FILE_LIMIT);
        CHECK(r.s.dataBytes <= kMaxDataBytes && r.s.dataBytes >= kMaxDataBytes - 4);
    }
    // a second start after a stop works and the counters restart
    {
        Rig r;
        r.sim.dirExists = true;
        CHECK(r.s.start("R", false, false) == E_NONE);
        r.run(2000);
        r.s.stop();
        CHECK(r.s.dataBytes > 0);
        r.sim.pushed.clear();
        CHECK(r.s.start("R", false, false) == E_NONE);
        CHECK(r.s.dataBytes == 0 && r.ring.dropped == 0 && r.s.stats.writes == 0 && r.ring.used() == 0);
        CHECK(r.sim.created == "/Recorded/R_002.wav");
        r.run(2000);
        r.s.stop();
        CHECK(fileMatchesPushed(r));
    }
    // start while running does nothing; stop while idle does nothing
    {
        Rig r;
        r.sim.dirExists = true;
        CHECK(r.s.stop() == E_NONE);
        CHECK(r.s.start("X", false, false) == E_NONE);
        CHECK(r.s.start("X", true, true) == E_NONE && r.s.active()); // already running: the exclusion only matters for a start
        r.s.stop();
    }
}

static void test_format()
{
    char b[24];
    fmtTime(b, sizeof(b), 0);
    CHECK(strcmp(b, "00:00") == 0);
    fmtTime(b, sizeof(b), 754);
    CHECK(strcmp(b, "12:34") == 0);
    fmtTime(b, sizeof(b), 3600 + 61);
    CHECK(strcmp(b, "1:01:01") == 0);
    fmtSize(b, sizeof(b), 44);
    CHECK(strcmp(b, "0 KB") == 0);
    fmtSize(b, sizeof(b), 500 * 1024);
    CHECK(strcmp(b, "500 KB") == 0);
    fmtSize(b, sizeof(b), 12u * 1024 * 1024 + 600 * 1024);
    CHECK(strcmp(b, "12.5 MB") == 0);
    fmtSize(b, sizeof(b), 4000000000u);
    CHECK(strcmp(b, "3814.6 MB") == 0);
    CHECK(kRingBytes % kChunkBytes == 0 && kRingBytes % 512 == 0 && kRingBytes == 196608);
}

// Preview of the Global Mixer 3/3 page with the Rec button (SEQ_REC_PREVIEW=<dir>): the texts come from the same functions the page uses
// (fmtTime / fmtSize, the labels "Rec" / "Stop rec"); rec_preview.py draws them on a schematic of the stock page.
static void dump_preview(const char* dir)
{
    for (int on = 0; on < 2; on++)
    {
        char path[300];
        snprintf(path, sizeof(path), "%s/rec_%s.ops", dir, on ? "on" : "idle");
        FILE* f = fopen(path, "w");
        if (!f)
            continue;
        char t[16], z[16];
        fmtTime(t, sizeof(t), 754);
        fmtSize(z, sizeof(z), 12u * 1024 * 1024 + 600 * 1024);
        fprintf(f, "title Global Mixer 3/3\n");
        if (on)
            fprintf(f, "right REC %s   %s\n", t, z);
        const char* lab[8] = { "Mute", "Mute", "Mute", "Mute", " ", on ? "Stop rec" : "Rec", "Faders", "Amp view" };
        for (int i = 0; i < 8; i++)
            fprintf(f, "label %d %s %s\n", i, (i == 5 && on) ? "red" : "grey", lab[i]);
        fclose(f);
    }
}

int main()
{
    if (getenv("SEQ_REC_PREVIEW"))
        dump_preview(getenv("SEQ_REC_PREVIEW"));
    test_header();
    test_ring();
    test_names();
    test_scheduler();
    test_start_errors();
    test_record_plain();
    test_stall();
    test_errors_and_limits();
    test_format();
    printf("master recording: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
