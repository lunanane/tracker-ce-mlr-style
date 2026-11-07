// Sequencer tracks mod - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Live master recording to the SD card (plan 030 A): the pure part, host-tested in core/seqtest (test_masterrec.cpp).
//
// Data path: the audio ISR copies every block of the two limiter outputs (the master, after the limiter; no metronome / preview) into a
// ring of interleaved 16-bit stereo frames (Ring::pushBlock, one 32-bit store per frame). The main loop (Session::update) writes the ring
// to a WAV file in chunks of 16 KB, one chunk per pass at most. The audio ISR never waits for the card: a write that stalls for
// 250 ms only fills 44 KB of the 192 KB ring (1.1 s of audio); if it is full anyway the block is dropped and counted.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace seqrec {

enum : uint32_t
{
    kBlockFrames = 128,
    kRingBytes = 192u * 1024u,             // 12 chunks, 384 blocks, 1.114 s of audio (taken from the sample pool: plan 030)
    kChunkBytes = 16u * 1024u,             // one write
    kMinGapMs = 6,                         // normal writes are at least this far apart (SD reads of the streams in between)
    kForceNum = 3,                         // forced write when used * 4 > size * 3 (more than 75 %)
    kHeaderEveryMs = 30000,                // WAV header + f_sync
    kSampleRate = 44100,                   // owner decision: the header says 44100 (the codec runs at 44 117.6 Hz: 0.04 % slow)
    kBytesPerSec = kSampleRate * 4,
    kMaxDataBytes = 0xFFFFFFFFu - (8u << 20) - 44u, // FAT32 file limit with a margin (about 6.7 hours)
    kMinFreeBytes = 8u << 20,              // refuse to start with less free space (about 47 s)
    kHeaderBytes = 44
};

enum Err : uint8_t
{
    E_NONE,
    E_BUSY_EXPORT,   // an export is rendering
    E_BUSY_SAMPLEREC, // the Sample Recorder is recording
    E_NO_CARD,
    E_NO_SPACE,      // less than kMinFreeBytes free
    E_CREATE,        // folder or file could not be created
    E_WRITE,         // a write failed or was short (card full / removed)
    E_FILE_LIMIT,    // 4 GB file limit reached (a clean stop)
    E_COUNT
};
inline const char* errText(Err e)
{
    static const char* const t[E_COUNT] = { "", "Export is running", "Sample recorder is recording", "No SD card", "SD card nearly full",
        "Cannot create the file", "Write error: recording stopped", "File limit reached: recording stopped" };
    return (unsigned)e < (unsigned)E_COUNT ? t[e] : "";
}

// ---- the ring: one producer (audio ISR), one consumer (main loop) ----
struct Ring
{
    uint32_t* buf = nullptr;      // size / 4 words, one word = one stereo frame (L in the low half)
    uint32_t size = 0;            // bytes, a multiple of 4
    volatile uint32_t wr = 0, rd = 0; // byte positions in [0, size); wr is written by the ISR, rd by the main loop
    volatile uint32_t dropped = 0;    // blocks lost because the ring was full
    volatile uint32_t high = 0;       // most bytes ever used (this recording)
    volatile uint32_t pushedBytes = 0; // total bytes taken from the audio (wraps at 4 GB: far beyond kMaxDataBytes)

    void init(uint32_t* b, uint32_t bytes)
    {
        buf = b;
        size = bytes;
        wr = rd = 0;
        dropped = high = pushedBytes = 0;
    }
    void clear()
    {
        rd = wr; // the consumer's pointer follows the producer's: nothing is left (call with the producer stopped)
        dropped = high = pushedBytes = 0;
    }
    uint32_t used() const
    {
        const uint32_t w = wr, r = rd;
        return w >= r ? w - r : w + size - r;
    }
    // ISR: l / r may be null (silence). Returns false when the block did not fit (and was dropped).
    bool pushBlock(const int16_t* l, const int16_t* r, uint32_t frames)
    {
        const uint32_t bytes = frames * 4u;
        const uint32_t u = used();
        if (u + bytes >= size)
        {
            dropped = dropped + 1;
            return false;
        }
        uint32_t wi = wr >> 2;
        const uint32_t words = size >> 2;
        for (uint32_t i = 0; i < frames; i++)
        {
            const uint32_t lo = l ? (uint16_t)l[i] : 0u;
            const uint32_t hi = r ? (uint16_t)r[i] : 0u;
            buf[wi] = lo | (hi << 16);
            if (++wi >= words)
                wi = 0;
        }
        wr = wi << 2;
        pushedBytes = pushedBytes + bytes;
        if (u + bytes > high)
            high = u + bytes;
        return true;
    }
    // main loop: the longest contiguous run at the read position (at most `used`)
    const uint8_t* peek(uint32_t& contiguous) const
    {
        const uint32_t u = used();
        const uint32_t toEnd = size - rd;
        contiguous = u < toEnd ? u : toEnd;
        return (const uint8_t*)buf + rd;
    }
    void consume(uint32_t n)
    {
        uint32_t r = rd + n;
        if (r >= size)
            r -= size;
        rd = r;
    }
    uint32_t highPercent() const { return size ? (uint32_t)(((uint64_t)high * 100u) / size) : 0u; }
};

// ---- the WAV header: 16-bit stereo 44 100 Hz ----
inline void put32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
inline void put16(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
__attribute__((noinline)) inline void wavHeader(uint8_t* h, uint32_t dataBytes)
{
    memcpy(h, "RIFF", 4);
    put32(h + 4, 36u + dataBytes);
    memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16);
    put16(h + 20, 1);  // PCM
    put16(h + 22, 2);  // channels
    put32(h + 24, kSampleRate);
    put32(h + 28, kBytesPerSec);
    put16(h + 32, 4);  // block align
    put16(h + 34, 16); // bits
    memcpy(h + 36, "data", 4);
    put32(h + 40, dataBytes);
}

// ---- file names: /Recorded/<project>_<nnn>.wav ----
inline void cleanName(char* out, int cap, const char* project)
{
    int n = 0;
    for (; project && *project && n < cap - 1 && n < 40; project++)
    {
        const char c = *project;
        const bool bad = c < 32 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|';
        out[n++] = bad ? '_' : c;
    }
    if (n == 0)
    {
        const char* d = "Master";
        while (*d && n < cap - 1)
            out[n++] = *d++;
    }
    out[n] = 0;
}
__attribute__((noinline)) inline void fileName(char* out, int cap, const char* project, uint32_t number)
{
    char nm[48];
    cleanName(nm, sizeof(nm), project);
    int n = 0;
    const char* dir = "/Recorded/";
    for (const char* s = dir; *s && n < cap - 1; s++)
        out[n++] = *s;
    for (const char* s = nm; *s && n < cap - 1; s++)
        out[n++] = *s;
    char tail[12] = { '_', (char)('0' + (number / 100) % 10), (char)('0' + (number / 10) % 10), (char)('0' + number % 10), '.', 'w', 'a', 'v', 0 };
    for (const char* s = tail; *s && n < cap - 1; s++)
        out[n++] = *s;
    out[n] = 0;
}

// ---- when to write ----
inline bool shouldWrite(uint32_t used, uint32_t ringSize, bool asyncBusy, uint32_t nowMs, uint32_t lastWriteEndMs)
{
    if (used < kChunkBytes)
        return false;
    if ((uint64_t)used * 4u > (uint64_t)ringSize * kForceNum)
        return true; // more than 75 %: now, whatever the streams do
    if (asyncBusy)
        return false; // a stream refill is in flight on the card: not now
    return (uint32_t)(nowMs - lastWriteEndMs) >= (uint32_t)kMinGapMs;
}

// ---- display strings ----
inline void fmtTime(char* out, int cap, uint32_t seconds)
{
    const uint32_t h = seconds / 3600u, m = (seconds / 60u) % 60u, s = seconds % 60u;
    if (h)
        snprintf(out, (size_t)cap, "%u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)s);
    else
        snprintf(out, (size_t)cap, "%02u:%02u", (unsigned)m, (unsigned)s);
}
inline void fmtSize(char* out, int cap, uint32_t bytes)
{
    if (bytes < 1000u * 1024u)
        snprintf(out, (size_t)cap, "%u KB", (unsigned)(bytes / 1024u));
    else
    {
        const uint32_t tenthMb = (uint32_t)(((uint64_t)bytes * 10u) / (1024u * 1024u));
        snprintf(out, (size_t)cap, "%u.%u MB", (unsigned)(tenthMb / 10u), (unsigned)(tenthMb % 10u));
    }
}

// ---- file access: the device implements it with the FileSystem, the host test with a fake card ----
struct Io
{
    virtual bool cardPresent() = 0;
    virtual bool freeBytes(uint64_t& out) = 0; // false: unknown (then the start is allowed)
    virtual bool exists(const char* path) = 0;
    virtual bool makeDir(const char* path) = 0;
    virtual bool create(const char* path) = 0;
    virtual int32_t write(const void* data, uint32_t n) = 0; // bytes written; < n or < 0: failure
    virtual bool rewriteHeader(const uint8_t* hdr, uint32_t endPos) = 0; // header at 0, then back to endPos, f_sync
    virtual void close() = 0;
    virtual uint32_t nowMs() = 0;
    virtual bool asyncBusy() = 0;
    virtual ~Io() {}
};

struct Stats
{
    uint32_t longestWriteMs = 0; // longest single write (chunk or header) of this recording
    uint32_t writes = 0;
    uint32_t forced = 0;         // writes done because the ring was more than 75 % full
};

// ---- the recording ----
struct Session
{
    Ring* ring = nullptr;
    Io* io = nullptr;
    volatile bool tapOn = false; // the audio ISR pushes only while this is set
    bool recording = false;
    Err lastError = E_NONE;
    uint32_t dataBytes = 0;      // bytes written to the file (without the header)
    uint32_t lastWriteEnd = 0, lastHeaderMs = 0;
    Stats stats;
    char path[96] = {};

    void init(Ring* r, Io* i)
    {
        ring = r;
        io = i;
        tapOn = false;
        recording = false;
    }
    bool active() const { return recording; }

    // returns E_NONE when the recording runs
    Err start(const char* project, bool exportBusy, bool sampleRecBusy)
    {
        lastError = E_NONE;
        if (recording)
            return E_NONE;
        if (exportBusy)
            return lastError = E_BUSY_EXPORT;
        if (sampleRecBusy)
            return lastError = E_BUSY_SAMPLEREC;
        if (!io->cardPresent())
            return lastError = E_NO_CARD;
        uint64_t freeB = 0;
        if (io->freeBytes(freeB) && freeB < kMinFreeBytes)
            return lastError = E_NO_SPACE;
        if (!io->exists("/Recorded") && !io->makeDir("/Recorded"))
            return lastError = E_CREATE;
        uint32_t n = 1;
        for (; n <= 999; n++)
        {
            fileName(path, sizeof(path), project, n);
            if (!io->exists(path))
                break;
        }
        if (n > 999 || !io->create(path))
            return lastError = E_CREATE;
        uint8_t h[kHeaderBytes];
        wavHeader(h, 0);
        if (io->write(h, kHeaderBytes) != (int32_t)kHeaderBytes)
        {
            io->close();
            return lastError = E_CREATE;
        }
        stats = Stats();
        dataBytes = 0;
        ring->clear();
        lastWriteEnd = lastHeaderMs = io->nowMs();
        recording = true;
        tapOn = true;
        return E_NONE;
    }

    // seconds of audio taken so far (what the screen shows)
    uint32_t seconds() const { return ring ? ring->pushedBytes / kBytesPerSec : 0u; }
    uint32_t fileBytes() const { return dataBytes + kHeaderBytes; }

    // main loop, once per pass (cheap when not recording)
    void update()
    {
        if (!recording)
            return;
        const uint32_t now = io->nowMs();
        const uint32_t used = ring->used();
        if (shouldWrite(used, ring->size, io->asyncBusy(), now, lastWriteEnd))
        {
            if ((uint64_t)used * 4u > (uint64_t)ring->size * kForceNum)
                stats.forced++;
            if (!writeChunk(kChunkBytes))
                return;
        }
        if ((uint32_t)(io->nowMs() - lastHeaderMs) >= (uint32_t)kHeaderEveryMs)
        {
            if (!header())
                fail(E_WRITE);
        }
    }

    // stop: flush the rest of the ring, final header, close. Returns the reason of an error stop (E_NONE for a normal stop).
    Err stop()
    {
        if (!recording)
            return lastError;
        tapOn = false; // the ISR stops pushing (the block in flight finishes before the next main loop pass: it is part of used())
        while (recording && ring->used() >= 4u)
        {
            if (!writeChunk(kChunkBytes))
                break;
        }
        if (recording)
            finish();
        return lastError;
    }

  private:
    bool writeChunk(uint32_t maxBytes)
    {
        uint32_t n = 0;
        const uint8_t* p = ring->peek(n);
        if (n > maxBytes)
            n = maxBytes;
        n &= ~3u;
        if (n == 0)
            return true;
        if (dataBytes + n > kMaxDataBytes)
            n = (kMaxDataBytes - dataBytes) & ~3u;
        const uint32_t t0 = io->nowMs();
        const int32_t w = n ? io->write(p, n) : 0;
        const uint32_t t1 = io->nowMs();
        lastWriteEnd = t1;
        stats.writes++;
        if (t1 - t0 > stats.longestWriteMs)
            stats.longestWriteMs = t1 - t0;
        if (w < 0 || (uint32_t)w != n)
        {
            if (w > 0)
                dataBytes += (uint32_t)w & ~3u;
            fail(E_WRITE);
            return false;
        }
        ring->consume(n);
        dataBytes += n;
        if (dataBytes >= kMaxDataBytes)
        {
            fail(E_FILE_LIMIT);
            return false;
        }
        return true;
    }
    bool header()
    {
        uint8_t h[kHeaderBytes];
        wavHeader(h, dataBytes);
        const uint32_t t0 = io->nowMs();
        const bool ok = io->rewriteHeader(h, kHeaderBytes + dataBytes);
        const uint32_t t1 = io->nowMs();
        lastHeaderMs = lastWriteEnd = t1;
        if (t1 - t0 > stats.longestWriteMs)
            stats.longestWriteMs = t1 - t0;
        return ok;
    }
    void finish()
    {
        header();
        io->close();
        recording = false;
    }
    void fail(Err e)
    {
        tapOn = false;
        lastError = e;
        finish();
    }
};

} // namespace seqrec
