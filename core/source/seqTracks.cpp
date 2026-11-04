// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
#include "seqTracks.h"
#include "mtSequencer.h"
#include "mtStructs.h"
#include "fileManager.h"
#include "fileManagerDefs.h"
#include "fileTransfer.h"
#include "elapsedMillis.h"
#include <cr_section_macros.h>
#include <stdio.h>

extern Sequencer sequencer;
extern strMtProject mtProject;

seqlogic::TapDetector seqPatternTap;

namespace
{
// Instrument used for a taken-over track while it plays (chosen when the takeover begins).
uint8_t s_instrument[seqlogic::kTracks];
// Sample tracks are monophonic: the note that currently sounds (-1 = none).
int16_t s_sampleNote[seqlogic::kTracks];

class SeqHost : public seqtracks::Host
{
  public:
    SeqHost()
    {
        for (int i = 0; i < seqlogic::kTracks; i++)
        {
            s_instrument[i] = 0;
            s_sampleNote[i] = -1;
        }
        seqEngine.setHost(this);
    }
    void takeoverBegin(uint8_t track) override
    {
        s_instrument[track] = sequencer.seqPickInstrument(track);
        s_sampleNote[track] = -1;
        sequencer.seqTakeoverBegin(track);
    }
    void takeoverEnd(uint8_t track) override { s_sampleNote[track] = -1; }
    void noteOn(uint8_t track, uint8_t note, int8_t volume) override
    {
        if (isSampleTrack(track))
        {
            if (s_sampleNote[track] >= 0)
                sequencer.seqNoteOff(track, (uint8_t)s_sampleNote[track], s_instrument[track]);
            s_sampleNote[track] = note;
        }
        sequencer.seqNoteOn(track, note, s_instrument[track], volume);
    }
    void noteOff(uint8_t track, uint8_t note) override
    {
        if (isSampleTrack(track))
        {
            if (s_sampleNote[track] != note)
                return; // an older chord note on a monophonic voice: already replaced
            s_sampleNote[track] = -1;
        }
        sequencer.seqNoteOff(track, note, s_instrument[track]);
    }
    bool muted(uint8_t track) override { return sequencer.seqTrackMuted(track); }
    void trackPos(uint8_t track, seqrec::TrackPos& out) override
    {
        sequencer.seqTrackPos(track, out.pos, out.uStep, out.last);
    }
    bool sampleTrack(uint8_t track) override { return isSampleTrack(track); }
    uint8_t instrument(uint8_t track) override { return s_instrument[track]; }
    seqrec::Params recParams() override
    {
        seqrec::Params p;
        p.quantize = !sequencer.seqRecMicrotiming();
        p.velocity = sequencer.seqRecVelocity();
        return p;
    }
};
} // namespace

seqtracks::Engine seqEngine;
static SeqHost s_host;

// ---------------------------------------------------------------- Sequencer members used by the glue
uint8_t Sequencer::seqPickInstrument(uint8_t track)
{
    return changeInstrumentInSpecificOrder(mtProject.values.lastUsedInstrument, 0, !isSampleTrack(track));
}

// The pattern note of this track goes quiet and the pattern stops driving the track until the takeover ends.
void Sequencer::seqTakeoverBegin(uint8_t track)
{
    stopOpenNoteOnTrack(track);
    strPlayer::strPlayerTrack& p = player.track[track];
    p.micromove.reset();
    p.roll.reset();
    p.arp.reset();
}

void Sequencer::seqNoteOn(uint8_t track, uint8_t note, uint8_t instrument, int8_t volume)
{
    strPlayer::strPlayerTrack::strSendStep step;
    step.note = (int8_t)note;
    step.instrument = instrument;
    step.volume = volume;
    sendNoteOn(track, &step);
}

void Sequencer::seqNoteOff(uint8_t track, uint8_t note, uint8_t instrument)
{
    strPlayer::strPlayerTrack::strSendStep step;
    step.note = (int8_t)note;
    step.instrument = instrument;
    sendNoteOff(track, &step, STEP_NOTE_OFF_DEFAULT);
}

void Sequencer::seqTrackPos(uint8_t track, uint8_t& pos, uint8_t& uStep, uint8_t& last)
{
    const strPlayer::strPlayerTrack& p = player.track[track];
    int16_t a = p.actual_pos;
    pos = (uint8_t)(a < 0 ? 0 : (a > (int16_t)MAXSTEP ? (int16_t)MAXSTEP : a));
    uStep = (uint8_t)(p.uStep > 48 ? 48 : p.uStep);
    uint8_t len = currentPattern->track[track].length;
    last = len > (uint8_t)MAXSTEP ? (uint8_t)MAXSTEP : len;
}

// Main loop: one planned operation of a recording replaces a whole step.
void Sequencer::seqWriteStep(uint8_t track, uint8_t stepIdx, uint8_t kind, uint8_t note, uint8_t instrument, uint8_t chord, int8_t volume, int8_t micromove)
{
    if (track >= kAllTracks || stepIdx > MAXSTEP)
        return;
    strPattern::strTrack::strStep* step = &currentPattern->track[track].step[stepIdx];
    if (kind == seqrec::OP_OFF_SOFT)
    {
        // end of a recording: a note-off only where the pattern holds no note (stock rule)
        if (step->isNoteEmpty())
            step->note = STEP_NOTE_OFF_DEFAULT;
        return;
    }
    clearStep(step);
    if (kind == seqrec::OP_NOTE)
    {
        step->note = (int8_t)note;
        step->instrument = instrument;
        step->volume = volume;
        step->micromove = micromove;
        if (chord != seqrec::kNoChord)
        {
            step->fx[0].type = (uint8_t)fx.FX_TYPE_MIDI_CHORD;
            step->fx[0].value = chord;
        }
    }
    else if (kind == seqrec::OP_OFF)
    {
        step->note = STEP_NOTE_OFF_DEFAULT;
        step->micromove = micromove;
    }
}

// ---------------------------------------------------------------- main loop: record
namespace
{
struct RecMain
{
    bool undoDone = false;
    bool dirty = false;
    uint8_t pattern = 0;
    uint8_t endsSeen = 0;
    bool stopSent = false;
} s_rec;

uint16_t s_savedHash = 0;     // hash of the state last written / loaded
bool s_hadSidecar = false;    // a sidecar was loaded or written for this project
bool s_hashInit = false;
bool s_wsWritePending = false;
elapsedMillis s_hashTimer;

const char kWorkspaceSidecar[] = "Workspace/seqtracks.bin";
const char kSidecarName[] = "seqtracks.bin";

void projectSidecarPath(char* out, size_t cap, const char* path, const char* name)
{
    snprintf(out, cap, "%s/%s/%s", path, name, kSidecarName);
}

// One shared buffer for all sidecar file steps (main thread, one at a time). Kept in external RAM: the MK66 stack is only
// 4 KB and on the Plus SRAM_DTC is nearly full.
__NOINIT(EXTERNAL_RAM) uint8_t s_io[seqtracks::Engine::kSidecarMax];

// Writes <path>.tmp completely, then replaces <path> (remove + rename: a half-written file never takes the place of the old one;
// the FAT API has no atomic replace, so a power loss between remove and rename leaves only the .tmp file, which nothing reads).
// On success s_savedHash = hash of what was written; on failure it stays, so the next change check retries.
bool writeSidecar(const char* path)
{
    uint8_t* buf = s_io;
    uint16_t h = seqEngine.stateHash();
    size_t n = seqEngine.saveState(buf, seqtracks::Engine::kSidecarMax);
    if (!n)
        return false;
    FileSystem* fs = newFileManager.getFileSystem();
    char tmp[PATCH_SIZE + 8];
    int len = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    bool ok;
    if (fs && len > 0 && (size_t)len < sizeof(tmp))
    {
        ok = fileTransfer.saveMemoryToFile(buf, tmp, (uint32_t)n) == fileTransferEnd;
        if (ok)
        {
            if (fs->exists(path))
                fs->remove(path);
            ok = fs->rename(tmp, path);
        }
        if (!ok && fs->exists(tmp))
            fs->remove(tmp);
    }
    else
    {
        ok = fileTransfer.saveMemoryToFile(buf, path, (uint32_t)n) == fileTransferEnd;
    }
    if (ok)
        s_savedHash = h;
    return ok;
}

enum ReadResult { READ_MISSING, READ_VALID, READ_INVALID };

// Reads a sidecar into the engine. Missing or invalid file: every track Off (stock behaviour).
ReadResult readSidecar(const char* path)
{
    FileSystem* fs = newFileManager.getFileSystem();
    if (!fs || !fs->exists(path))
    {
        seqEngine.resetState();
        return READ_MISSING;
    }
    // the real file length goes to loadState: bytes left in the shared buffer by an earlier file must not complete a short file
    size_t sz = fs->getFileSize(path);
    if (sz > seqtracks::Engine::kSidecarMax)
        sz = seqtracks::Engine::kSidecarMax;
    uint8_t* buf = s_io;
    if (sz && fileTransfer.loadFileToMemory(path, buf, (uint32_t)sz, fileWholeOnce) == fileTransferEnd &&
        seqEngine.loadState(buf, sz))
        return READ_VALID;
    seqEngine.resetState();
    return READ_INVALID;
}

// The project folder whose sidecar was loaded (valid or not): Save back to it keeps an unknown file it cannot read.
uint32_t s_loadedId = 0;
bool s_loadedValid = false;

void removeIfExists(const char* path)
{
    FileSystem* fs = newFileManager.getFileSystem();
    if (fs && fs->exists(path))
        fs->remove(path);
}
} // namespace

namespace
{
seqlogic::PendingReleases s_pendingRel;
}

bool seqPadPress(uint8_t track, uint8_t pad)
{
    if (s_pendingRel.has(track, pad))
    {
        if (!seqEngine.postPad(track, pad, false))
            return false; // the old release goes first
        s_pendingRel.remove(track, pad);
    }
    return seqEngine.postPad(track, pad, true);
}

void seqPadRelease(uint8_t track, uint8_t pad)
{
    if (!seqEngine.postPad(track, pad, false))
        s_pendingRel.add(track, pad);
}

bool seqRecStart(bool oneLoop, uint8_t track)
{
    if (seqEngine.recState() != seqrec::Planner::IDLE || seqEngine.type(track) == seqlogic::TYPE_OFF)
        return false;
    s_rec.undoDone = false;
    s_rec.dirty = false;
    s_rec.stopSent = false;
    s_rec.pattern = mtProject.values.actualPattern;
    s_rec.endsSeen = seqEngine.recEnds();
    if (!sequencer.isPlay())
        sequencer.playPattern(); // the transport runs first, then the recording is queued behind it
    return seqEngine.postRec(track, oneLoop ? seqtracks::Engine::REC_LOOP : seqtracks::Engine::REC_PUNCH);
}

void seqRecStop()
{
    seqEngine.postRec(seqEngine.recTrack(), seqtracks::Engine::REC_STOP);
}

void seqTracksUpdate()
{
    if (s_pendingRel.any())
        s_pendingRel.retry([](uint8_t t, uint8_t p) { return seqEngine.postPad(t, p, false); });

    // 1. operations planned in the audio context -> pattern (one undo step per recording)
    seqrec::Op op;
    bool wrote = false;
    while (seqEngine.popRecOp(op))
    {
        if (mtProject.values.actualPattern != s_rec.pattern)
        {
            if (!s_rec.stopSent)
            {
                s_rec.stopSent = true;
                seqRecStop(); // the pattern changed under the recording: end it, write nothing more
            }
            continue;
        }
        if (!s_rec.undoDone)
        {
            newFileManager.storePatternUndoRevision();
            s_rec.undoDone = true;
        }
        sequencer.seqWriteStep(op.track, op.step, op.kind, op.note, op.instrument, op.chord, op.volume, op.micromove);
        wrote = true;
    }
    if (wrote)
        newFileManager.setActualPatternStructChanged();
    uint8_t ends = seqEngine.recEnds();
    if (ends != s_rec.endsSeen)
    {
        s_rec.endsSeen = ends;
        s_rec.undoDone = false;
        s_rec.stopSent = false;
    }

    // 2. sequencer state changed -> let the workspace autosave write the sidecar (checked about once a second)
    if (s_hashTimer > 1000)
    {
        s_hashTimer = 0;
        uint16_t h = seqEngine.stateHash();
        if (!s_hashInit)
        {
            s_hashInit = true;
            s_savedHash = h; // the state at boot is what is on the card (all Off without a sidecar)
        }
        // s_savedHash only moves when a write succeeded (writeSidecar), so a failed write is retried at the next check
        if (h != s_savedHash && !s_wsWritePending)
        {
            s_hadSidecar = true;
            newFileManager.setProjectStructChanged();
            s_wsWritePending = true;
        }
    }
}

// ---------------------------------------------------------------- sidecar files
void seqTracksLoadWorkspace()
{
    ReadResult r = readSidecar(kWorkspaceSidecar);
    s_hadSidecar = r == READ_VALID;
    s_loadedValid = r != READ_MISSING; // the file came from the project being opened
    s_loadedId = seqlogic::projectId(newFileManager.getCurrentProjectPath(), newFileManager.getCurrentProjectName());
    s_savedHash = seqEngine.stateHash();
    s_hashInit = true;
    s_wsWritePending = false;
}

void seqTracksSaveWorkspace()
{
    if (!s_wsWritePending)
        return;
    s_wsWritePending = false;
    writeSidecar(kWorkspaceSidecar); // on failure s_savedHash stays: the next change check (<= 1 s) flags it again
}

void seqTracksSaveProject(const char* path, const char* name)
{
    char file[PATCH_SIZE];
    projectSidecarPath(file, sizeof(file), path, name);
    uint32_t id = seqlogic::projectId(path, name);
    switch (seqlogic::sidecarAction(s_hadSidecar || seqEngine.active(), s_loadedValid && s_loadedId == id))
    {
        case seqlogic::SIDE_WRITE:
            if (writeSidecar(file))
            {
                s_loadedValid = true;
                s_loadedId = id;
            }
            writeSidecar(kWorkspaceSidecar);
            s_wsWritePending = false;
            break;
        case seqlogic::SIDE_REMOVE:
            // never used in this project: no stale file of an overwritten project (nor in the Workspace mirror)
            removeIfExists(file);
            removeIfExists(kWorkspaceSidecar);
            break;
        case seqlogic::SIDE_KEEP: break; // leave the folder as stock firmware writes it
    }
}

void seqTracksImportProject(const char* path, const char* name)
{
    FileSystem* fs = newFileManager.getFileSystem();
    char file[PATCH_SIZE];
    projectSidecarPath(file, sizeof(file), path, name);
    if (!fs || !fs->exists(file))
        return;
    size_t sz = fs->getFileSize(file);
    if (sz > seqtracks::Engine::kSidecarMax)
        sz = seqtracks::Engine::kSidecarMax;
    uint8_t* buf = s_io;
    if (sz && fileTransfer.loadFileToMemory(file, buf, (uint32_t)sz, fileWholeOnce) == fileTransferEnd)
        fileTransfer.saveMemoryToFile(buf, kWorkspaceSidecar, (uint32_t)sz);
}
