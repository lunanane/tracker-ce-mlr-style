// Sequencer tracks mod - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Live master recording to the SD card (plan 030 A): the audio tap (an AudioStream sink on both limiter outputs), the ring in EXTERNAL_RAM
// raw storage (no constructor), the file access through the platform FileSystem, and the main loop hook.
#include "seqMasterRec.h"
#include "Audio.h"
#include "seqCpu.h"
#include "fileManager.h"
#include "Platform.h"
#include "core/interfacePopups.h"
#include "sampleRecorder/sampleRecorder.h"
#include "mtAudioExport.h"
#include <cr_section_macros.h>

uint32_t seqNowMs(); // seqTracks.h: free-running milliseconds
extern mtAudioExport exporter;
extern cSampleRecorder sampleRecorder;

namespace seqmasterrec {

__NOINIT(EXTERNAL_RAM) __attribute__((aligned(32))) static uint32_t s_ringBuf[seqrec::kRingBytes / 4];

static seqrec::Ring s_ring;
static seqrec::Session s_sess;

namespace {

struct DevIo final : seqrec::Io
{
    FileSystem* fs = nullptr;
    FileSystem::FileHandle h = nullptr;
    bool cardPresent() override { return fs && fs->isSdCardInserted(); }
    bool freeBytes(uint64_t& out) override { return fs->getFreeBytes(out); }
    bool exists(const char* p) override { return fs->exists(p); }
    bool makeDir(const char* p) override { return fs->mkdir(0, p); }
    bool create(const char* p) override
    {
        h = nullptr;
        auto r = fs->openFile(p, FileSystem::kWriteCreateNew);
        h = std::get<1>(r);
        return std::get<0>(r) && h;
    }
    int32_t write(const void* d, uint32_t n) override { return fs->writeFile(d, n, h); }
    bool rewriteHeader(const uint8_t* hdr, uint32_t endPos) override
    {
        bool ok = fs->seekFile(0, h) && fs->writeFile(hdr, seqrec::kHeaderBytes, h) == (int32_t)seqrec::kHeaderBytes;
        ok = fs->syncFile(h) && ok;
        return fs->seekFile(endPos, h) && ok;
    }
    void close() override
    {
        if (h)
            fs->closeFile(h);
        h = nullptr;
    }
    uint32_t nowMs() override { return seqNowMs(); }
    bool asyncBusy() override { return fs->isAsyncReadBusy(); }
};
DevIo s_io;

// the sink: both limiter outputs, after the limiter (no metronome, no preview), one block per audio interrupt
class MasterTap final : public AudioStream
{
  public:
    MasterTap()
      : AudioStream(2, q)
    {
        perfStage = seqcpu::S_IO;
    }
    AUDIO_CORE_RAM void update(void) override
    {
        audio_block_t* l = receiveReadOnly(0);
        audio_block_t* r = receiveReadOnly(1);
        if (s_sess.tapOn)
            s_ring.pushBlock(l ? l->data : nullptr, r ? r->data : nullptr, AUDIO_BLOCK_SAMPLES);
        if (l)
            release(l);
        if (r)
            release(r);
    }

  private:
    audio_block_t* q[2];
};
MasterTap s_tap;

} // namespace

AudioStream* tapStream() { return &s_tap; }

void init(FileSystem* fs)
{
    s_io.fs = fs;
    s_ring.init(s_ringBuf, seqrec::kRingBytes);
    s_sess.init(&s_ring, &s_io);
}

void update()
{
    if (!s_sess.active())
        return;
    s_sess.update();
    if (!s_sess.active() && s_sess.lastError != seqrec::E_NONE) // stopped by an error (card full / removed, file limit): say so, wherever the user is
        mtPopups.show(0, seqrec::errText(s_sess.lastError));
}
bool active() { return s_sess.active(); }

seqrec::Err start()
{
    return s_sess.start(newFileManager.getCurrentProjectName(), exporter.getStatus() != mtAudioExport::statusIdle,
        sampleRecorder.recordInProgressFlag != 0);
}
seqrec::Err stop() { return s_sess.stop(); }
const seqrec::Session& session() { return s_sess; }
const seqrec::Ring& ring() { return s_ring; }

} // namespace seqmasterrec
