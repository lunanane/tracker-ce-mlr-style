// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
#include "seqTracks.h"
#include "mtSequencer.h"
#include "mtStructs.h"
#include "fileManager.h"
#include "fileManagerDefs.h"
#include "fileTransfer.h"
#include "elapsedMillis.h"
#include "cr_section_macros.h"
#include "seqCut.h"
#include <math.h>
#include <new>
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

// ---------------------------------------------------------------- CUT (MLR) layer glue
// 16 playheads (5.8 KB) live in external RAM: SRAM_DTC of the Plus is nearly full. Constructed when the workspace is loaded.
__NOINIT(EXTERNAL_RAM) static uint8_t s_cutMem[sizeof(seqcut::Cut)] __attribute__((aligned(8)));

namespace
{
void cutNoteMap(void*, uint8_t track, uint8_t steps, uint8_t* hasNote) { sequencer.seqCutNoteMap(track, steps, hasNote); }
int cutTrackPos(void*, uint8_t track) { return sequencer.seqCutPos(track); }
int cutTrackSteps(void*, uint8_t track) { return sequencer.seqCutSteps(track); }
void cutStopNote(void*, uint8_t track) { sequencer.seqCutStopNote(track); }
bool cutSampleTrack(void*, uint8_t track) { return isSampleTrack(track); }
} // namespace

void seqCutInit()
{
    if (seqEngine.cut())
        return;
    seqcut::Cut* c = new (s_cutMem) seqcut::Cut();
    seqcut::Hooks h = { 0, cutNoteMap, cutTrackPos, cutTrackSteps, cutStopNote, cutSampleTrack };
    c->setHooks(h);
    seqEngine.setCut(c); // published last (release)
}

// Steps the stock sequencer plays on this track (mirrors switchRowStep: last step index + 1).
int Sequencer::seqCutSteps(uint8_t row)
{
    int len = currentPattern->track[0].length;
    if (player.performanceMode && isPerformanceTrack(row))
        len = player.performance.trackLength[row];
    if (player.performance.patternLength > -1)
        len = player.performance.patternLength;
    int n = len + 1;
    return n < 1 ? 1 : (n > (int)MAXSTEP + 1 ? (int)MAXSTEP + 1 : n);
}

// 1 = the step has a note, 2 = a note-off, 0 = nothing that sounds or ends a sound.
void Sequencer::seqCutNoteMap(uint8_t row, uint8_t steps, uint8_t* hasNote)
{
    strPattern::strTrack& t = currentPattern->track[row];
    for (uint8_t i = 0; i < steps && i <= MAXSTEP; i++)
    {
        int8_t n = t.step[i].note;
        hasNote[i] = n >= 0 ? 1 : ((n == STEP_NOTE_OFF_FADE || n == STEP_NOTE_OFF_CUT || n == STEP_NOTE_OFF_DEFAULT) ? 2 : 0);
    }
}

// Silence the track and drop what would still fire later (micromove, roll, arp). A note the user holds while live
// recording (recOpen) is left alone: stopOpenNoteOnTrack would clear the recording state.
void Sequencer::seqCutStopNote(uint8_t row)
{
    strPlayer::strPlayerTrack& p = player.track[row];
    p.micromove.reset();
    p.roll.reset();
    p.arp.reset();
    if (!p.recOpen)
        stopOpenNoteOnTrack(row);
}

// The position the stock sequencer would have: row-0 duties (pattern preload, sequential switch, metronome) follow it.
int16_t Sequencer::seqCutStockPos(uint8_t row)
{
    seqcut::Cut* c = seqEngine.cut();
    if (c && row < seqlogic::kTracks && ((c->activeMask() >> row) & 1u))
        return (int16_t)c->stock(row);
    return player.track[row].actual_pos;
}

// inc_uStep, before switchRowStep: the stock sequencer advances its own position, not the cut one.
void Sequencer::seqCutPreSwitch(uint8_t row)
{
    seqcut::Cut* c = seqEngine.cut();
    if (!c || row >= seqlogic::kTracks || !((c->activeMask() >> row) & 1u))
        return;
    player.track[row].actual_pos = (int16_t)c->stock(row);
    seqCutPending |= (uint16_t)(1u << row);
}

// inc_uStep, after switchRowStep: remember where the stock sequencer is now.
void Sequencer::seqCutPostSwitch(uint8_t row)
{
    seqcut::Cut* c = seqEngine.cut();
    if (c && ((seqCutPending >> row) & 1u))
        c->setStock(row, player.track[row].actual_pos);
}

// A selection loop keeps the cut rows inside the selection.
uint8_t Sequencer::seqCutLimit(uint8_t pos, int steps)
{
    if (pos >= steps)
        pos = (uint8_t)(steps - 1);
    if (player.selectionModeLooped)
    {
        int first = player.loopSelection.firstStep;
        int last = player.loopSelection.lastStep < steps - 1 ? player.loopSelection.lastStep : steps - 1;
        if (pos < first || pos > last)
            pos = (uint8_t)(first <= last ? first : last);
    }
    return pos;
}

// After the step switch of ALL tracks (so a song / pattern switch at the end of the pattern is already done and
// globalPos is the step that plays next): the playheads of the cut tracks decide the next positions.
void Sequencer::seqCutApply()
{
    seqcut::Cut* c = seqEngine.cut();
    uint16_t mask = seqCutPending;
    seqCutPending = 0;
    if (!c || !mask || !player.isPlay)
        return;
    for (uint8_t row = 0; row < seqlogic::kTracks; row++)
    {
        if (!((mask >> row) & 1u) || !((c->activeMask() >> row) & 1u))
            continue;
        int steps = seqCutSteps(row);
        seqcut::StepPlan p = c->boundary(row, steps, player.globalPos);
        if (p.override)
            player.track[row].actual_pos = seqCutLimit(p.pos, steps);
    }
}

// play_microStep, plus the second half-step of a track running at double rate (starts at uStep 25 as a step of its own).
void Sequencer::seqCutMicroStep(int row)
{
    seqcut::Cut* c = seqEngine.cut();
    strPlayer::strPlayerTrack& p = player.track[row];
    seqcut::StepPlan plan;
    if (c && row < seqlogic::kTracks && p.uStep == 25)
    {
        int steps = seqCutSteps((uint8_t)row);
        if (c->mid((uint8_t)row, steps, plan))
        {
            uint8_t saved = p.uStep;
            p.actual_pos = seqCutLimit(plan.pos, steps);
            p.uStep = 1;
            seqCutMid = true; // the row-0 duties of a step start (pattern preload, metronome) already ran at uStep 1
            play_microStep(row);
            seqCutMid = false;
            p.uStep = saved;
            return;
        }
    }
    play_microStep(row);
}

// The track jumped to a position without a note: play the last note before it, that many steps late.
void Sequencer::seqCutRetrigger(uint8_t row, strPlayer::strPlayerTrack::strSendStep& step)
{
    seqCutStart[row] = 0;
    seqcut::Cut* c = seqEngine.cut();
    seqcut::StepPlan plan;
    if (!c || row >= seqlogic::kTracks || !c->consumeRetrigger(row, plan) || plan.noteStep > MAXSTEP)
        return;
    strPattern::strTrack::strStep& src = currentPattern->track[row].step[plan.noteStep];
    const bool sample = isSampleTrack(row) && isSampleInstrument(src.instrument);
    // MIDI / synth: a note is struck again only while it sustains; a gated note has ended in the gap by now
    if (!sample && (src.fx[0].type == fx.FX_TYPE_GATE_LENGTH || src.fx[1].type == fx.FX_TYPE_GATE_LENGTH))
        return;
    memcpy((uint8_t*)&step, (uint8_t*)&src, sizeof(strPattern::strTrack::strStep));
    step.micromove = -1; // the retrigger is not delayed
    if (!sample || step.note < 0)
        return;
    const auto& instr = mtProject.instrument[step.instrument];
    if (instr.playMode == playModeSlice || instr.playMode == playModeBeatSlice || instr.playMode == playModeGranular ||
        instr.playMode == playModeWavetable)
        return; // Position FX means something else in these modes: replay without offset

    // The start point goes in through the Position FX path of the engine (full 16 bit) in a free FX slot; a busy slot is
    // only given up when its FX is not one that changes how the sample plays.
    int posSlot = -1, freeSlot = -1;
    for (int i = 0; i < 2; i++)
    {
        if (step.fx[i].type == fx.FX_TYPE_POSITION)
            posSlot = i;
        else if (step.fx[i].type == fx.FX_TYPE_NONE && freeSlot < 0)
            freeSlot = i;
    }
    if (posSlot < 0 && freeSlot < 0)
    {
        uint8_t t = step.fx[1].type;
        if (t == fx.FX_TYPE_SAMPLE_SLICE || t == fx.FX_TYPE_REVERSE_PLAYBACK || t == fx.FX_TYPE_GRANULAR_LENGTH)
            return;
    }
    uint16_t base = instr.startPoint;
    if (posSlot >= 0)
        base = (uint16_t)mapClamped<int>(step.fx[posSlot].value, getFxMin(fx.FX_TYPE_POSITION), getFxMax(fx.FX_TYPE_POSITION), 0, MAX_16BIT);
    float semis = (float)(step.note - 60) + (float)instr.tune + (float)instr.fineTune / 100.0f;
    uint32_t speed1000 = (uint32_t)(powf(2.0f, semis / 12.0f) * 1000.0f + 0.5f);
    uint16_t start;
    if (!seqcut::startPoint16(plan.elapsed, getStepPeriodMs(), instr.sample.length, 44100, speed1000, base,
                              instr.playMode != playModeSingleShot, instr.loopPoint1, instr.loopPoint2, instr.endPoint, start))
        return; // behind the end of a one-shot sample: the retrigger would be silent anyway, play it from the start
    seqCutStart[row] = (uint32_t)start + 1;
}

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

bool writeSidecar(const char* path)
{
    uint8_t buf[seqtracks::Engine::kSidecarMax];
    size_t n = seqEngine.saveState(buf, sizeof(buf));
    if (!n)
        return false;
    return fileTransfer.saveMemoryToFile(buf, path, (uint32_t)n) == fileTransferEnd;
}

// Reads a sidecar into the engine. Missing or invalid file: every track Off (stock behaviour).
bool readSidecar(const char* path)
{
    uint8_t buf[seqtracks::Engine::kSidecarMax];
    uint8_t st = fileTransfer.loadFileToMemory(path, buf, sizeof(buf), fileWholeOnce);
    if (st == fileTransferEnd && seqEngine.loadState(buf, sizeof(buf)))
        return true;
    seqEngine.resetState();
    return false;
}
} // namespace

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
        if (h != s_savedHash)
        {
            s_savedHash = h;
            s_hadSidecar = true;
            newFileManager.setProjectStructChanged();
            s_wsWritePending = true;
        }
    }
}

// ---------------------------------------------------------------- sidecar files
void seqTracksLoadWorkspace()
{
    seqCutInit();
    s_hadSidecar = readSidecar(kWorkspaceSidecar);
    s_savedHash = seqEngine.stateHash();
    s_hashInit = true;
    s_wsWritePending = false;
}

void seqTracksSaveWorkspace()
{
    if (!s_wsWritePending)
        return;
    s_wsWritePending = false;
    writeSidecar(kWorkspaceSidecar);
}

void seqTracksSaveProject(const char* path, const char* name)
{
    if (!s_hadSidecar && !seqEngine.seqActive())
        return; // never used in this project: leave the folder as stock firmware writes it
    char file[PATCH_SIZE];
    projectSidecarPath(file, sizeof(file), path, name);
    writeSidecar(file);
    writeSidecar(kWorkspaceSidecar);
    s_wsWritePending = false;
}

void seqTracksImportProject(const char* path, const char* name)
{
    uint8_t buf[seqtracks::Engine::kSidecarMax];
    char file[PATCH_SIZE];
    projectSidecarPath(file, sizeof(file), path, name);
    if (fileTransfer.loadFileToMemory(file, buf, sizeof(buf), fileWholeOnce) == fileTransferEnd)
        fileTransfer.saveMemoryToFile(buf, kWorkspaceSidecar, sizeof(buf));
}
