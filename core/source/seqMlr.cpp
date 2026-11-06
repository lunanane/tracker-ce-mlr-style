// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
#include "seqMlr.h"
#include "seqCpu.h" // plan 022 P1
#include "seqCueCache.h"
#include "VoiceStreamManager.h"
#include "mtSequencer.h"
#include "mtStructs.h"
#include "seqCut.h"
#include "seqTracks.h"
#include "seqSession/cutLayer.h"
#include <cr_section_macros.h>
#include <string.h>

extern strMtProject mtProject;

namespace
{
using namespace seqcue;

// Raw storage in EXTERNAL_RAM: no constructor, no initialiser (platforms/mk66/check_sdram_ctors.sh). 768 KB for the cue audio.
__NOINIT(EXTERNAL_RAM) __attribute__((aligned(32))) int16_t s_pool[kEntries][kChannels][kFrames];
__NOINIT(EXTERNAL_RAM) seqcut::WaveCol s_wave[seqcut::kSlots][seqcut::kWaveCols];

Cache s_cache; // internal RAM, plain bookkeeping
uint8_t s_curBank[kSlots], s_prefetch[kSlots], s_from[kSlots];
uint16_t s_mask[kSlots];

struct WaveJob
{
    uint32_t sig;
    uint8_t col; // next column to build
    bool done;
};
WaveJob s_job[kSlots];

struct Ctx
{
    uint8_t track;
    int steps;
    uint8_t map[Sequencer::MAXSTEP + 1];
    bool streamedOnly;
};
Ctx s_ctx[kSlots];

bool frameFn(void* u, int pos, uint8_t& instr, uint32_t& frame, uint32_t& hash, uint8_t& stereo)
{
    Ctx* c = (Ctx*)u;
    if (!sequencer.seqCutFrameForPos(c->track, pos, c->steps, c->map, instr, frame, hash, stereo)) return false;
    const auto& mem = mtProject.sampleMemory[instr];
    return !c->streamedOnly || (mem.isStreamed && frame >= mem.preCacheFrames);
}

uint32_t mixSig(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

// One column of the waveform: the peak of a short probe at the column's first frame (approximation: whole columns are not scanned).
void buildColumn(uint8_t slot, uint8_t col, int base, int ss, bool allowSd)
{
    Ctx& c = s_ctx[slot];
    const int pos = base + seqcut::waveColStep(col, ss);
    uint8_t instr, stereo;
    uint32_t frame, hash;
    seqcut::WaveCol w = { 0, 0 };
    c.streamedOnly = false;
    if (pos < c.steps && frameFn(&c, pos, instr, frame, hash, stereo))
    {
        const auto& mem = mtProject.sampleMemory[instr];
        const uint32_t total = mtProject.instrument[instr].sample.length;
        uint32_t n = 256;
        if (frame >= total) n = 0;
        else if (frame + n > total) n = total - frame;
        if (n && mem.address[0] && (!mem.isStreamed || frame + n <= mem.preCacheFrames))
            seqcut::waveFold(w, (const int16_t*)mem.address[0] + frame, (int)n, 1);
        else if (n && mem.isStreamed && allowSd)
        {
            static int16_t buf[256];
            seqcpu::readKind(seqcpu::R_WAVE);
            if (voiceStreamManager.readInstrumentFramesBlocking(instr, 0, frame, n, buf))
                seqcut::waveFold(w, buf, (int)n, 1);
        }
    }
    s_wave[slot][col] = w;
}

void waveWork(bool allowSd, int cols)
{
    for (uint8_t k = 0; k < kSlots && cols > 0; k++)
    {
        WaveJob& j = s_job[k];
        if (j.done) continue;
        const seqcut::View v = seqEngine.cut() ? seqEngine.cut()->view(s_ctx[k].track) : seqcut::View();
        const int spp = (v.flags2 & seqcut::V2_GRID8) ? 2 : 4;
        const int ss = seqcut::sectionSteps(spp);
        while (j.col < seqcut::kWaveCols && cols > 0)
        {
            // streamed samples need a blocking SD read: only at the idle moment of the stream scheduler
            const int pos = v.sec * ss + seqcut::waveColStep(j.col, ss);
            uint8_t i2, s2;
            uint32_t f2, h2;
            Ctx& c = s_ctx[k];
            c.streamedOnly = false;
            const bool has = pos < c.steps && frameFn(&c, pos, i2, f2, h2, s2);
            const bool sd = has && mtProject.sampleMemory[i2].isStreamed && f2 >= mtProject.sampleMemory[i2].preCacheFrames;
            if (sd && !allowSd) break;
            buildColumn(k, j.col, v.sec * ss, ss, allowSd);
            j.col++;
            cols--;
        }
        if (j.col >= seqcut::kWaveCols) j.done = true;
    }
}
} // namespace

namespace seqmlr
{
void update()
{
    static uint32_t last = 0;
    const uint32_t now = seqNowMs();
    seqcut::Cut* cut = seqEngine.cut();
    if (!cut || !cutSlotsUsed() || (uint32_t)(now - last) < 100) return;
    last = now;
    for (uint8_t k = 0; k < kSlots; k++)
    {
        Ctx& c = s_ctx[k];
        c.track = cutSlotTrack(k);
        c.steps = sequencer.seqCutSteps(c.track);
        memset(c.map, 0, sizeof(c.map));
        sequencer.seqCutNoteMap(c.track, (uint8_t)(c.steps > Sequencer::MAXSTEP + 1 ? Sequencer::MAXSTEP + 1 : c.steps), c.map);
        c.streamedOnly = true;
        const seqcut::View v = cut->view(c.track);
        const int spp = (v.flags2 & seqcut::V2_GRID8) ? 2 : 4;
        const int bank = v.sec & 1;
        Want w[kCues];
        computeWants(frameFn, &c, v.sec, spp, c.steps, 0, w);
        s_cache.want(k, bank, w);
        // the next section: its cues are fetched once the playhead is in the second half of this one
        s_prefetch[k] = (v.col != 0xFF && v.col >= kCues / 2 && v.sec + 1 < v.nsec) ? 1 : 0;
        if (s_prefetch[k])
        {
            computeWants(frameFn, &c, v.sec + 1, spp, c.steps, 0, w);
            s_cache.want(k, bank ^ 1, w);
        }
        s_curBank[k] = (uint8_t)bank;
        s_from[k] = v.col == 0xFF ? 0 : v.col;
        s_mask[k] = s_cache.readyMask(k, bank);
        // waveform: rebuilt when the track, section, grid or the sample mapping (loop points, tempo, instrument) changed
        uint32_t sig = mixSig(mixSig(mixSig(c.track, v.sec), spp), (uint32_t)c.steps);
        c.streamedOnly = false;
        for (int q = 0; q < 3; q++)
        {
            uint8_t i2, s2;
            uint32_t f2 = 0, h2 = 0;
            const int pos = v.sec * spp * 16 + (q * spp * 16) / 3;
            if (pos < c.steps && frameFn(&c, pos, i2, f2, h2, s2)) sig = mixSig(mixSig(sig, f2), h2);
        }
        if (sig != s_job[k].sig)
        {
            s_job[k].sig = sig;
            s_job[k].col = 0;
            s_job[k].done = false;
        }
    }
    waveWork(false, 24); // resident samples are read from RAM right here; streamed columns wait for idle()
}

// plan 022 P2: one cue is read in pieces of kPiece frames (one call of idle() each, which the stream manager spaces out and holds back
// while voices jump), so a blocking read never keeps the main loop - and the next stream refill - waiting for a whole 6 KB cue.
namespace
{
constexpr uint32_t kPiece = 1024;
int s_fIdx = -1, s_fCh = 0;
uint32_t s_fDone = 0, s_fFrame = 0, s_fHash = 0;
} // namespace

void idle()
{
    int idx, ch;
    if (!seqEngine.cut() || !cutSlotsUsed()) return;
    if (s_cache.next(s_curBank, s_prefetch, s_from, idx, ch))
    {
        const Cache::Entry& e = s_cache.entry(idx);
        if (idx != s_fIdx || ch != s_fCh || e.frame != s_fFrame || e.hash != s_fHash)
        {
            s_fIdx = idx; // a new entry (or one that went stale meanwhile): start it from the top
            s_fCh = ch;
            s_fFrame = e.frame;
            s_fHash = e.hash;
            s_fDone = 0;
        }
        const uint32_t n = (kFrames - s_fDone) < kPiece ? (kFrames - s_fDone) : kPiece;
        seqcpu::readKind(seqcpu::R_CUE);
        const bool ok = voiceStreamManager.readInstrumentFramesBlocking(e.instr, ch, e.frame + s_fDone, n, s_pool[idx][ch] + s_fDone);
        s_fDone += n;
        if (!ok || s_fDone >= kFrames)
        {
            s_cache.loaded(idx, ch); // done (an unreadable file too: the zero filled cue is still better than asking again and again)
            s_fIdx = -1;
        }
        return;
    }
    waveWork(true, 1);
}

bool findCue(int instr, uint32_t startFrame, int channel, const int16_t*& addr, uint32_t& frames)
{
    int idx;
    uint32_t delta;
    if (!s_cache.find(instr, startFrame, idx, delta)) return false;
    addr = &s_pool[idx][channel > 0 && s_cache.entry(idx).need > 1 ? 1 : 0][delta];
    frames = kFrames - delta;
    return true;
}

uint16_t cacheMask(uint8_t slot) { return slot < kSlots ? s_mask[slot] : 0; }

const seqcut::WaveCol* wave(uint8_t slot) { return (slot < kSlots && s_job[slot].done) ? s_wave[slot] : nullptr; }
} // namespace seqmlr
