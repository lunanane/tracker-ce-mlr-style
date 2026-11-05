// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md). Plinky voice parts: MIT (libs/Audio/PlinkyVoice)
#include "seqPlinky.h"
#ifdef FEATURE_PLINKY
#include <new>
#include "seqSidecar.h"
#include "seqModLogic.h"
#include "seqPlinkySeqLogic.h"
#include "seqPlinkyMorphLogic.h"
#include "seqNotesViewLogic.h"
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
__NOINIT(EXTERNAL_RAM) alignas(plinkymorph::Controller) uint8_t s_ctlMem[sizeof(plinkymorph::Controller)];
plinkymorph::Controller& s_ctl = *reinterpret_cast<plinkymorph::Controller*>(s_ctlMem);
__NOINIT(EXTERNAL_RAM) uint8_t s_fbuf[plinkymorph::kFileMaxAll];

// hash of everything that is saved: the values, the morph time and the snapshots
uint16_t stateHash(const Params& p)
{
    uint16_t h = seqsidecar::crc16(p.v, P_COUNT);
    h = seqsidecar::crc16Update(h, &s_ctl.halfSec, 1);
    h = seqsidecar::crc16Update(h, s_ctl.store.bits, sizeof(s_ctl.store.bits));
    for (int i = 1; i < plinkymorph::kPatterns; i++)
        if (s_ctl.store.has(i))
            h = seqsidecar::crc16Update(h, s_ctl.store.v[i], P_COUNT);
    return h;
}

inline int shownPattern() { return (int)mtProject.values.actualPattern; }

// applies the values the morph / a snapshot changed to the engine (not a hand edit: the project is not marked changed)
void applyMask(const uint8_t* vals, uint32_t mask)
{
    if (!mask)
        return;
    for (int i = 1; i < P_COUNT; i++)
        if ((mask >> i) & 1u)
            eng().setParam(i, vals[i]);
    if (mask & ((1u << P_REVERB) | (1u << P_DELAY)))
        engine.applyPlinkySends();
}

const char kWorkspaceFile[] = "Workspace/plinky.bin";
const char kFileName[] = "plinky.bin";

Params current()
{
    Params p;
    for (int i = 0; i < P_COUNT; i++)
        p.v[i] = eng().param(i);
    return p;
}

void apply(const Params& p)
{
    for (int i = 0; i < P_COUNT; i++)
        eng().setParam(i, p.v[i]);
    engine.applyPlinkySends();
}

bool writeValues(const char* path)
{
    if (s_newerFile)
        return false;
    const Params p = current();
    size_t n = plinkymorph::saveFile(s_fbuf, sizeof(s_fbuf), p, s_ctl.store, s_ctl.halfSec);
    if (!n || !seqsidecar::writeFile(path, s_fbuf, n))
        return false;
    s_savedHash = stateHash(p);
    return true;
}
} // namespace

bool seqPlinkyNote(uint8_t channel, uint8_t note, uint8_t velo, bool on)
{
    if (eng().param(P_CHANNEL) != channel)
        return false;
    // the same "middle C" shift the MIDI outputs get (sendMidiNoteOn), so the voice plays what an external synth would
    int n = note + 12 * (2 - (int)mtConfig.midi.middleC);
    n = n < 0 ? 0 : (n > 127 ? 127 : n);
    if (on && velo > 0)
    {
        // v0.3.1 minor 4: a note-on the full event queue refused is DROPPED, not sent on: its note-off is absorbed here too, so an external
        // synth would have been left hanging
        eng().noteOn((uint8_t)n, velo);
        return true;
    }
    eng().noteOff((uint8_t)n); // velocity 0 is a note-off; a release never gets lost (remembered when the queue is full)
    return true;
}

bool seqPlinkyControlChange(uint8_t channel, uint8_t cc, uint8_t value)
{
    if (eng().param(P_CHANNEL) != channel)
        return false;
    eng().controlChange(cc, value);
    return true;
}

bool seqPlinkyPitchBend(uint8_t channel, int16_t value)
{
    if (eng().param(P_CHANNEL) != channel)
        return false;
    int v = value + 8192;
    eng().pitchBend((int16_t)(v < 0 ? 0 : (v > 16383 ? 16383 : v)));
    return true;
}

void seqPanicPlinky()
{
    eng().allNotesOff();
}

int seqPlinkyInstrument()
{
    const int ch = eng().param(P_CHANNEL);
    return (ch >= 1 && ch <= kMidiInstrumentsCount) ? MIDI_INSTRUMENTS_START_IDX + ch - 1 : -1;
}

bool seqPlinkyIsVoiceInstrument(int instrument)
{
    return instrument >= 0 && instrument == seqPlinkyInstrument();
}

bool seqPlinkyTrackUsesVoice(uint8_t track)
{
    return seqPlinkyIsVoiceInstrument(sequencer.seqPickInstrument(track));
}

int seqPlinkyGet(int param)
{
    if (param == kPlinkyMorphParam)
        return s_ctl.halfSec;
    return eng().param(param);
}

void seqPlinkySet(int param, int value)
{
    if (param == kPlinkyMorphParam)
    {
        const int v = value < 0 ? 0 : (value > plinkymorph::kHalfSecMax ? plinkymorph::kHalfSecMax : value);
        if (v != s_ctl.halfSec)
        {
            s_ctl.halfSec = (uint8_t)v;
            s_dirty.mark();
        }
        return;
    }
    const uint8_t before = eng().param(param);
    if (param == P_CHANNEL && clampParam(P_CHANNEL, value) != before && clampParam(P_CHANNEL, value) != 0)
        sendCC(123, 0, clampParam(P_CHANNEL, value), false); // the notes still sounding on the MIDI outputs of the new voice channel end first
    eng().setParam(param, value);
    if (eng().param(param) != before)
    {
        if (param == P_CHANNEL)
        {
            eng().allNotesOff(); // the voice's own notes end
            if (before != 0)
                sendCC(123, 0, before, false); // v0.3.1 minor 7: the old channel is a plain MIDI output again: all notes off there too
        }
        if (param == P_REVERB || param == P_DELAY)
            engine.applyPlinkySends();
        if (param != P_CHANNEL)
        {   // plan 012: a hand edit leaves the morph and writes the snapshot of the playing (or shown) pattern
            const Params now = current();
            s_ctl.edit(shownPattern(), param, eng().param(param), now.v);
        }
        s_dirty.mark();
    }
}

void seqPlinkyDefault(int param)
{
    seqPlinkySet(param, param == kPlinkyMorphParam ? (int)plinkymorph::kHalfSecDefault : (int)paramInfo(param).def);
}

bool seqPlinkyMorphStatus(uint16_t* remainMs, uint8_t* pattern)
{
    if (!s_ctl.morph.running)
        return false;
    uint32_t r = s_ctl.morph.remainMs(seqNowMs());
    if (remainMs)
        *remainMs = (uint16_t)(r > 65535 ? 65535 : r);
    if (pattern)
        *pattern = s_ctl.morph.pattern;
    return true;
}

bool seqPlinkyVoice(int i, uint8_t* note, uint8_t* level, uint8_t* releasing)
{
    if (i < 0 || i >= kMaxVoices)
        return false;
    const Voice& v = eng().voice(i); // plain reads of the audio side's state: enough for a display
    if (v.stage == ST_IDLE)
        return false;
    float e = v.env * (float)seqnotes::kVoiceBar;
    *note = v.note;
    *level = (uint8_t)(e < 0.0f ? 0 : (e > (float)seqnotes::kVoiceBar ? seqnotes::kVoiceBar : (int)e));
    *releasing = v.stage == ST_RELEASE ? 1 : 0;
    return true;
}

uint32_t seqPlinkyMarks()
{
    return s_ctl.store.marks(shownPattern());
}

void seqPlinkyClipLaunched(uint8_t track, uint8_t pattern)
{
    if (!seqPlinkyTrackUsesVoice(track))
        return;
    Params cur = current();
    applyMask(cur.v, s_ctl.onClip(pattern, true, sequencer.isPlay(), cur.v, seqNowMs()));
}

float seqPlinkyCpu() { return engine.getPlinkyCpuPercent(); }
float seqPlinkyCpuPeak() { return engine.getPlinkyCpuPeakPercent(); }
int seqPlinkyVoicesActive() { return eng().activeVoices(); }
const void* seqPlinkyTraceRing(int voice) { return eng().traceRing(voice); }
unsigned seqPlinkyTraceLen() { return plinkyvoice::kTraceLen; }
uint16_t seqPlinkyTraceHead() { return eng().traceHeadIndex(); }

bool seqPlinkyNewerFile() { return s_newerFile; }

void seqPlinkyUpdate()
{
    {   // CC 91 / 93 change the effective sends in the audio context: the mixer gains follow here (main loop)
        static int lastRev = -1, lastDly = -1;
        const int rev = eng().effective(P_REVERB), dly = eng().effective(P_DELAY);
        if (rev != lastRev || dly != lastDly)
        {
            lastRev = rev;
            lastDly = dly;
            engine.applyPlinkySends();
        }
    }
    {   // plan 012: the playing (or shown) pattern changed on any path (stock switch, song, scene, queue, follow action): morph to its snapshot
        Params cur = current();
        const uint32_t now = seqNowMs();
        applyMask(cur.v, s_ctl.onPattern(shownPattern(), sequencer.isPlay(), cur.v, now));
        applyMask(cur.v, s_ctl.tick(cur.v, now));
    }
    // never ask for a write while the transport plays: the dirty flag waits for the stop
    if (!s_newerFile && !s_wsWritePending && s_dirty.take(sequencer.isPlay()))
    {
        if (stateHash(current()) != s_savedHash)
        {
            s_hadFile = true;
            newFileManager.setProjectStructChanged();
            s_wsWritePending = true;
        }
    }
}

void seqPlinkyInit()
{
    new (s_ctlMem) plinkymorph::Controller();
}

void seqPlinkyLoadWorkspace()
{
    Params p;
    s_hadFile = false;
    s_loadedValid = false;
    s_newerFile = false;
    s_ctl.store.clear();
    s_ctl.halfSec = plinkymorph::kHalfSecDefault;
    s_ctl.resync(); // the next main loop pass applies the snapshot of the shown pattern at once (no morph)
    if (seqsidecar::exists(kWorkspaceFile))
    {
        size_t n = seqsidecar::readFile(kWorkspaceFile, s_fbuf, sizeof(s_fbuf));
        bool newer = false;
        if (n && plinkymorph::loadFile(s_fbuf, n, p, s_ctl.store, s_ctl.halfSec, &newer))
            s_hadFile = true;
        else
        {
            p = Params(); // short, corrupt or newer file: defaults, the file itself is left alone
            s_ctl.store.clear();
            s_ctl.halfSec = plinkymorph::kHalfSecDefault;
        }
        s_newerFile = newer;
        s_loadedValid = true;
    }
    s_loadedId = seqlogic::projectId(newFileManager.getCurrentProjectPath(), newFileManager.getCurrentProjectName());
    eng().allNotesOff();
    if (p.v[P_CHANNEL] == 0)
        p.v[P_CHANNEL] = paramInfo(P_CHANNEL).def; // v0.3.2: the PolySynth instrument always exists (a v0.3 file with the voice off: MIDI instrument 16)
    apply(p); // a file of v0.3 / v0.3.1 keeps its claimed channel: that MIDI instrument is the PolySynth instrument
    s_savedHash = stateHash(p);
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
    switch (seqlogic::sidecarAction(s_hadFile || !current().isDefault() || plinkymorph::needsTrailer(s_ctl.store, s_ctl.halfSec),
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
