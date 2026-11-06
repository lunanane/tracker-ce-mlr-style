// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md). Plinky voice parts: MIT (libs/Audio/PlinkyVoice)
#include "seqPlinky.h"
#ifdef FEATURE_PLINKY
#include <new>
#include "seqSidecar.h"
#include "seqModLogic.h"
#include "seqPlinkySeqLogic.h"
#include "seqPlinkyMorphLogic.h"
#include "seqNotesViewLogic.h"
#include "seqInstOrderLogic.h"
#include "seqPlinkyUiLogic.h"
#include "seqTracks.h"
#include "seqTracksLogic.h"
#include "mtAudioEngine.h"
#include "mtSequencer.h"
#include "mtStructs.h"
#include "fileManager.h"
#include "fileManagerDefs.h"
#include "config.h"
#include "mtConfig.h"
#include "mtMidi.h"

extern Sequencer sequencer;
extern strMtProject mtProject;

namespace
{
using namespace plinkyvoice;

inline Engine& eng() { return engine.getPlinky()->engine(); }

seqlogic::DirtyGate s_dirty;   // a value changed: the workspace autosave is asked for once the transport stopped
bool s_wsWritePending = false; // the autosave was asked for; SaveWorkspace writes the file
bool s_hadFile = false;        // a plinky.bin was loaded or written for this project
uint16_t s_savedHash = 0;      // hash of the values last written / loaded
uint32_t s_loadedId = 0;       // project folder whose sidecar was loaded (valid or not)
bool s_loadedValid = false;
bool s_newerFile = false;      // written by a newer mod version: left alone (no downgrade, nothing written)

// plan 012: snapshots per pattern (5.4 KB) and the file buffer in EXTERNAL_RAM, not in the internal SRAM.
// v0.3.5 (boot hang of v0.3.4): the Controller has constructors (Store() fills the base values, Morph / Controller have member
// initialisers). A __NOINIT(EXTERNAL_RAM) object of that type gets a C++ static constructor that runs from __libc_init_array, long
// before hardware init powers up and configures the SDRAM -> bus fault at the first write. So: raw storage only (no static
// constructor), constructed by placement new in seqPlinkyInit() from TrackerImpl::init() (SDRAM ready). See developer_guide.md.
// plan 019: one snapshot store + morph per instance (3 x 5.4 KB) and a file buffer for the three instances
__NOINIT(EXTERNAL_RAM) alignas(plinkymorph::Controller) uint8_t s_ctlMem[kInstances][sizeof(plinkymorph::Controller)];
inline plinkymorph::Controller& ctl(int k) { return *reinterpret_cast<plinkymorph::Controller*>(s_ctlMem[k]); }
__NOINIT(EXTERNAL_RAM) uint8_t s_fbuf[plinkymorph::kFileMaxBank];
int s_inst = 0; // the instance the UI shows and edits (main loop only)

// hash of everything that is saved: the values, the morph time and the snapshots
uint16_t stateHash()
{
    uint16_t h = 0;
    for (int k = 0; k < kInstances; k++)
    {
        uint8_t cur[P_COUNT];
        for (int i = 0; i < P_COUNT; i++)
            cur[i] = eng().param(i, k);
        h = seqsidecar::crc16Update(h, cur, P_COUNT);
        h = seqsidecar::crc16Update(h, &ctl(k).halfSec, 1);
        h = seqsidecar::crc16Update(h, ctl(k).store.bits, sizeof(ctl(k).store.bits));
        for (int i = 1; i < plinkymorph::kPatterns; i++)
            if (ctl(k).store.has(i))
                h = seqsidecar::crc16Update(h, ctl(k).store.v[i], P_COUNT);
    }
    return h;
}

inline int shownPattern() { return (int)mtProject.values.actualPattern; }

// applies the values the morph / a snapshot changed to the engine (not a hand edit: the project is not marked changed)
void applyMask(int k, const uint8_t* vals, uint32_t mask)
{
    if (!mask)
        return;
    for (int i = 1; i < P_COUNT; i++)
        if ((mask >> i) & 1u)
            eng().setParam(i, vals[i], k);
}

const char kWorkspaceFile[] = "Workspace/plinky.bin";
const char kFileName[] = "plinky.bin";

Params current(int k)
{
    Params p;
    for (int i = 0; i < P_COUNT; i++)
        p.v[i] = eng().param(i, k);
    return p;
}

bool bankNeedsFile()
{
    for (int k = 0; k < kInstances; k++)
        if (plinkymorph::needsTrailer(ctl(k).store, ctl(k).halfSec) || !(current(k) == plinkymorph::instDefaults(k)))
            return true;
    return false;
}

bool writeValues(const char* path)
{
    if (s_newerFile)
        return false;
    Params cur[kInstances];
    const plinkymorph::Store* st[kInstances];
    uint8_t hs[kInstances];
    for (int k = 0; k < kInstances; k++)
    {
        cur[k] = current(k);
        st[k] = &ctl(k).store;
        hs[k] = ctl(k).halfSec;
    }
    size_t n = plinkymorph::saveBank(s_fbuf, sizeof(s_fbuf), cur, st, hs);
    if (!n || !seqsidecar::writeFile(path, s_fbuf, n))
        return false;
    s_savedHash = stateHash();
    return true;
}

// the instance that plays this MIDI channel, -1 = none (any context: the channel parameters are plain bytes)
int instOfChannel(uint8_t channel)
{
    for (int k = 0; k < kInstances; k++)
        if (eng().param(P_CHANNEL, k) == channel)
            return k;
    return -1;
}

// set one value of instance k (hand edit): snapshot, dirty flag
void setInst(int k, int param, int value)
{
    if (param == kPlinkyMorphParam)
    {
        const int v = value < 0 ? 0 : (value > plinkymorph::kHalfSecMax ? plinkymorph::kHalfSecMax : value);
        if (v != ctl(k).halfSec)
        {
            ctl(k).halfSec = (uint8_t)v;
            s_dirty.mark();
        }
        return;
    }
    const uint8_t before = eng().param(param, k);
    if (param == P_CHANNEL && clampParam(P_CHANNEL, value) != before && clampParam(P_CHANNEL, value) != 0)
        sendCC(123, 0, clampParam(P_CHANNEL, value), false); // the notes still sounding on the MIDI outputs of the new voice channel end first
    eng().setParam(param, value, k);
    if (eng().param(param, k) != before)
    {
        if (param == P_CHANNEL)
        {
            eng().allNotesOff(k); // the voice's own notes end
            if (before != 0)
                sendCC(123, 0, before, false); // v0.3.1 minor 7: the old channel is a plain MIDI output again: all notes off there too
        }
        if (param != P_CHANNEL)
        {   // plan 012: a hand edit leaves the morph and writes the snapshot of the playing (or shown) pattern
            const Params now = current(k);
            ctl(k).edit(shownPattern(), param, eng().param(param, k), now.v);
        }
        s_dirty.mark();
    }
}
} // namespace

bool seqPlinkyNote(uint8_t channel, uint8_t note, uint8_t velo, bool on)
{
    const int k = instOfChannel(channel);
    if (k < 0)
        return false;
    // the same "middle C" shift the MIDI outputs get (sendMidiNoteOn), so the voice plays what an external synth would
    int n = note + 12 * (2 - (int)mtConfig.midi.middleC);
    n = n < 0 ? 0 : (n > 127 ? 127 : n);
    if (on && velo > 0)
    {
        // v0.3.1 minor 4: a note-on the full event queue refused is DROPPED, not sent on: its note-off is absorbed here too, so an external
        // synth would have been left hanging
        eng().noteOn((uint8_t)n, velo, k);
        return true;
    }
    eng().noteOff((uint8_t)n, k); // velocity 0 is a note-off; a release never gets lost (remembered when the queue is full)
    return true;
}

bool seqPlinkyControlChange(uint8_t channel, uint8_t cc, uint8_t value)
{
    const int k = instOfChannel(channel);
    if (k < 0)
        return false;
    eng().controlChange(cc, value, k);
    return true;
}

bool seqPlinkyPitchBend(uint8_t channel, int16_t value)
{
    const int k = instOfChannel(channel);
    if (k < 0)
        return false;
    int v = value + 8192;
    eng().pitchBend((int16_t)(v < 0 ? 0 : (v > 16383 ? 16383 : v)), k);
    return true;
}

void seqPanicPlinky()
{
    eng().allNotesOff(-1);
}

int seqPlinkyInstrumentOf(int inst)
{
    if (inst < 0 || inst >= kInstances)
        return -1;
    const int ch = eng().param(P_CHANNEL, inst);
    return (ch >= 1 && ch <= kMidiInstrumentsCount) ? MIDI_INSTRUMENTS_START_IDX + ch - 1 : -1;
}

int seqPlinkyInstanceOf(int instrument)
{
    if (instrument < MIDI_INSTRUMENTS_START_IDX)
        return -1;
    return instOfChannel((uint8_t)(instrument - MIDI_INSTRUMENTS_START_IDX + 1));
}

bool seqPlinkyIsVoiceInstrument(int instrument)
{
    return instrument >= 0 && seqPlinkyInstanceOf(instrument) >= 0;
}

bool seqPlinkyTrackUsesVoice(uint8_t track)
{
    return seqPlinkyIsVoiceInstrument(sequencer.seqPickInstrument(track));
}

int seqPlinkyTrackInstance(uint8_t track)
{
    return seqPlinkyInstanceOf(sequencer.seqPickInstrument(track));
}

void seqPlinkyFillLayout(instorder::Layout& L)
{
    L.nSamples = kSampleInstrumentsCount;
    L.nMidi = kMidiInstrumentsCount;
    for (int k = 0; k < instorder::kMaxSynth; k++)
        L.synth[k] = seqPlinkyInstrumentOf(k);
}

int seqPlinkyInst() { return s_inst; }

void seqPlinkySetInst(int inst)
{
    s_inst = inst < 0 ? 0 : (inst >= kInstances ? kInstances - 1 : inst);
}

int seqPlinkyCycleInst()
{
    s_inst = (s_inst + 1) % kInstances;
    return s_inst;
}

int seqPlinkyGet(int param)
{
    if (param == kPlinkyMorphParam)
        return ctl(s_inst).halfSec;
    return eng().param(param, s_inst);
}

void seqPlinkySet(int param, int value)
{
    setInst(s_inst, param, value);
}

// Shift + dial (plan 019 A, the Model:Samples multi-edit): the same delta is added to the parameter of ALL instances, each clamped on its own
void seqPlinkySetAll(int param, int delta)
{
    for (int k = 0; k < kInstances; k++)
    {
        const int cur = param == kPlinkyMorphParam ? ctl(k).halfSec : eng().param(param, k);
        setInst(k, param, seqplinky::allValue(param, cur, delta));
    }
}

void seqPlinkyDefault(int param)
{
    seqPlinkySet(param, param == kPlinkyMorphParam ? (int)plinkymorph::kHalfSecDefault : (int)paramInfo(param).def);
}

bool seqPlinkyMorphStatus(uint16_t* remainMs, uint8_t* pattern)
{
    if (!ctl(s_inst).morph.running)
        return false;
    uint32_t r = ctl(s_inst).morph.remainMs(seqNowMs());
    if (remainMs)
        *remainMs = (uint16_t)(r > 65535 ? 65535 : r);
    if (pattern)
        *pattern = ctl(s_inst).morph.pattern;
    return true;
}

// the i-th sounding voice (pool order) of the shown instance
bool seqPlinkyVoice(int i, uint8_t* note, uint8_t* level, uint8_t* releasing)
{
    if (i < 0 || i >= kMaxVoices)
        return false;
    int n = -1, slot = -1;
    for (int s = 0; s < kPool && n < i; s++)
        if (eng().voice(s).stage != ST_IDLE && eng().voice(s).inst == s_inst)
        {
            n++;
            slot = s;
        }
    if (n != i)
        return false;
    const Voice& v = eng().voice(slot); // plain reads of the audio side's state: enough for a display
    float e = v.env * (float)seqnotes::kVoiceBar;
    *note = v.note;
    *level = (uint8_t)(e < 0.0f ? 0 : (e > (float)seqnotes::kVoiceBar ? seqnotes::kVoiceBar : (int)e));
    *releasing = v.stage == ST_RELEASE ? 1 : 0;
    return true;
}

uint32_t seqPlinkyMarks()
{
    return ctl(s_inst).store.marks(shownPattern());
}

void seqPlinkyClipLaunched(uint8_t track, uint8_t pattern)
{
    const int k = seqPlinkyTrackInstance(track);
    if (k < 0)
        return;
    Params cur = current(k);
    applyMask(k, cur.v, ctl(k).onClip(pattern, true, sequencer.isPlay(), cur.v, seqNowMs()));
}

float seqPlinkyCpu() { return engine.getPlinkyCpuPercent(); }
float seqPlinkyCpuPeak() { return engine.getPlinkyCpuPeakPercent(); }
int seqPlinkyVoicesActive() { return eng().activeVoices(); }
int seqPlinkyVoiceCap() { return eng().voiceCap(); }

void seqPlinkyFillModel(seqplinky::Model& m)
{
    m.inst = (uint8_t)s_inst;
    for (int i = 0; i <= plinkyvoice::P_COUNT; i++)
        m.v[i] = (uint8_t)seqPlinkyGet(i); // the last one is the morph time (plan 012)
    uint16_t ms = 0;
    uint8_t mp = 0;
    m.morphPat = seqPlinkyMorphStatus(&ms, &mp) ? mp : 0;
    m.morphMs = ms;
    m.mark = seqPlinkyMarks();
    for (int i = 0; i < seqnotes::kVoices; i++)
    {
        uint8_t n = 0, lv = 0, rel = 0;
        if (seqPlinkyVoice(i, &n, &lv, &rel))
        {
            m.voice[i].note = n;
            m.voice[i].level = lv;
            m.voice[i].rel = rel;
        }
        else
            m.voice[i] = seqnotes::VoiceView();
    }
    m.cap = (uint8_t)seqPlinkyVoiceCap();
    m.cpu = (uint8_t)(seqPlinkyCpu() + 0.5f);
    m.peak = (uint8_t)(seqPlinkyCpuPeak() + 0.5f);
    m.newer = seqPlinkyNewerFile() ? 1 : 0;
}
const void* seqPlinkyTraceRing(int voice) { return eng().traceRing(voice); } // pool voice 0 .. kPool - 1
unsigned seqPlinkyTraceLen() { return plinkyvoice::kTraceLen; }
uint16_t seqPlinkyTraceHead() { return eng().traceHeadIndex(); }

bool seqPlinkyNewerFile() { return s_newerFile; }

void seqPlinkyUpdate()
{
    {   // plan 012: the playing (or shown) pattern changed on any path (stock switch, song, scene, queue, follow action): morph to its snapshot
        const uint32_t now = seqNowMs();
        for (int k = 0; k < kInstances; k++)
        {
            Params cur = current(k);
            applyMask(k, cur.v, ctl(k).onPattern(shownPattern(), sequencer.isPlay(), cur.v, now));
            applyMask(k, cur.v, ctl(k).tick(cur.v, now));
        }
    }
    // never ask for a write while the transport plays: the dirty flag waits for the stop
    if (!s_newerFile && !s_wsWritePending && s_dirty.take(sequencer.isPlay()))
    {
        if (stateHash() != s_savedHash)
        {
            s_hadFile = true;
            newFileManager.setProjectStructChanged();
            s_wsWritePending = true;
        }
    }
}

void seqPlinkyInit()
{
    for (int k = 0; k < kInstances; k++)
        new (s_ctlMem[k]) plinkymorph::Controller();
}

void seqPlinkyLoadWorkspace()
{
    Params p[kInstances];
    plinkymorph::Store* st[kInstances];
    uint8_t hs[kInstances];
    s_hadFile = false;
    s_loadedValid = false;
    s_newerFile = false;
    for (int k = 0; k < kInstances; k++)
    {
        p[k] = plinkymorph::instDefaults(k);
        st[k] = &ctl(k).store;
        st[k]->clear();
        hs[k] = plinkymorph::kHalfSecDefault;
        ctl(k).resync(); // the next main loop pass applies the snapshot of the shown pattern at once (no morph)
    }
    if (seqsidecar::exists(kWorkspaceFile))
    {
        size_t n = seqsidecar::readFile(kWorkspaceFile, s_fbuf, sizeof(s_fbuf));
        bool newer = false;
        if (n && plinkymorph::loadBank(s_fbuf, n, p, st, hs, &newer))
            s_hadFile = true;
        else
            for (int k = 0; k < kInstances; k++) // short, corrupt or newer file: defaults, the file itself is left alone
            {
                p[k] = plinkymorph::instDefaults(k);
                st[k]->clear();
                hs[k] = plinkymorph::kHalfSecDefault;
            }
        s_newerFile = newer;
        s_loadedValid = true;
    }
    s_loadedId = seqlogic::projectId(newFileManager.getCurrentProjectPath(), newFileManager.getCurrentProjectName());
    eng().allNotesOff(-1);
    eng().resetGovernor();
    plinkymorph::fixChannels(p); // the PolySynth instruments always exist; a v0.3 file keeps its claimed channel for PolySynth 1
    for (int k = 0; k < kInstances; k++)
    {
        for (int i = 0; i < P_COUNT; i++)
            eng().setParam(i, p[k].v[i], k);
        ctl(k).halfSec = hs[k];
    }
    s_savedHash = stateHash();
    s_wsWritePending = false;
    s_dirty.dirty = false;
}

void seqPlinkySaveWorkspace()
{
    if (!s_wsWritePending)
        return;
    s_wsWritePending = false;
    if (sequencer.isPlay())
    {
        s_dirty.mark(); // still playing: write after the stop
        return;
    }
    if (!writeValues(kWorkspaceFile))
        s_dirty.mark(); // retried at the next pass without play
}

void seqPlinkySaveProject(const char* path, const char* name)
{
    if (s_newerFile)
        return; // a file of a newer mod version stays as it is
    char file[PATCH_SIZE];
    seqsidecar::projectFilePath(file, sizeof(file), path, name, kFileName);
    uint32_t id = seqlogic::projectId(path, name);
    switch (seqlogic::sidecarAction(s_hadFile || bankNeedsFile(),
                                    s_loadedValid && s_loadedId == id))
    {
        case seqlogic::SIDE_WRITE:
            if (writeValues(file))
            {
                s_loadedValid = true;
                s_loadedId = id;
                s_hadFile = true;
            }
            writeValues(kWorkspaceFile);
            s_wsWritePending = false;
            break;
        case seqlogic::SIDE_REMOVE:
            seqsidecar::removeIfExists(file);
            seqsidecar::removeIfExists(kWorkspaceFile);
            break;
        case seqlogic::SIDE_KEEP: break;
    }
}

void seqPlinkyImportProject(const char* path, const char* name)
{
    char file[PATCH_SIZE];
    seqsidecar::projectFilePath(file, sizeof(file), path, name, kFileName);
    if (seqsidecar::exists(file))
        seqsidecar::importToWorkspace(file, kWorkspaceFile);
}
#endif
