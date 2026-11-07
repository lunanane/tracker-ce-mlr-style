// Sequencer tracks mod (plan 021) - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
#include "seqBeatRepeat.h"
#include "seqHooks.h"
#include "seqSidecar.h"
#include "seqModLogic.h"
#include "seqTracksLogic.h"
#include "mtSequencer.h"
#include "mtStructs.h"
#include "fileManager.h"
#include "fileManagerDefs.h"
#include "config.h"
#include "mtAudioEngine.h"

extern Sequencer sequencer;

namespace
{
seqbr::Ui s_ui;
seqlogic::DirtyGate s_dirty; // a value changed: the workspace autosave is asked for once the transport stopped
bool s_wsWritePending = false;
bool s_hadFile = false;
uint16_t s_savedHash = 0;
uint32_t s_loadedId = 0;
bool s_loadedValid = false;
bool s_newerFile = false; // written by a newer mod version: left alone (no downgrade, nothing written)
bool s_wired = false;

const char kWorkspaceFile[] = "Workspace/seqbrpt.bin";
const char kFileName[] = "seqbrpt.bin";

#ifdef FEATURE_BEAT_REPEAT
// Block clock for the stage (audio context). tick() has run to the END of this block interval before the audio blocks are updated, so the
// position at the start of the block is one block back.
bool clockSource(beatrepeat::Clock& k)
{
    float bpm = sequencer.getCurrentTempo();
    if (!(bpm >= 10.0f))
        bpm = 120.0f;
    k.bpm = bpm;
    int32_t t = 0;
    float f = 0.0f;
    if (!sequencer.getBeatClock(&t, &f))
    {
        k.running = false;
        return true;
    }
    float pos = (float)t + f;
    pos -= bpm * (float)beatrepeat::kTicksPerBeat / 60.0f * (float)beatrepeat::kBlock / beatrepeat::kFs;
    if (pos < 0.0f)
        pos = 0.0f;
    k.running = true;
    k.tick = (int32_t)pos;
    k.frac = pos - (float)k.tick;
    return true;
}

AudioEffectBeatRepeat* stage()
{
    return engine.getBeatRepeat();
}

void wire()
{
    if (s_wired)
        return;
    s_wired = true;
    stage()->setClockSource(&clockSource);
}
uint32_t s_waveFor = 0xFFFFFFFFu;
int8_t s_lo[seqbr::kBrWave], s_hi[seqbr::kBrWave];
bool s_waveOk = false;
#endif

bool writeValues(const char* path)
{
    if (s_newerFile)
        return false;
    uint8_t* buf = seqsidecar::ioBuffer();
    uint16_t h = s_ui.prm.hash();
    size_t n = s_ui.prm.save(buf, seqsidecar::ioCapacity());
    if (!n || !seqsidecar::writeFile(path, buf, n))
        return false;
    s_savedHash = h;
    return true;
}
} // namespace

seqbr::Ui& seqBrUi()
{
    return s_ui;
}

void seqBrPush()
{
#ifdef FEATURE_BEAT_REPEAT
    wire();
    stage()->core().setSettings(s_ui.prm.settings());
#endif
}

void seqBrAct(const seqbr::Result& r)
{
#ifdef FEATURE_BEAT_REPEAT
    wire();
    beatrepeat::Core& c = stage()->core();
    switch (r.act)
    {
        case seqbr::BA_DOWN: c.post(beatrepeat::Core::C_DOWN, r.arg); break;
        case seqbr::BA_UP: c.post(beatrepeat::Core::C_UP); break;
        case seqbr::BA_LATCH: c.post(r.arg ? beatrepeat::Core::C_LATCH : beatrepeat::Core::C_UNLATCH); break;
        case seqbr::BA_OFF: c.post(beatrepeat::Core::C_DISENGAGE); break;
        case seqbr::BA_SETTINGS:
            c.setSettings(s_ui.prm.settings());
            s_dirty.mark();
            break;
        default: break;
    }
#else
    if (r.act == seqbr::BA_SETTINGS)
        s_dirty.mark();
#endif
}

void seqBrPreroll(bool on)
{
#ifdef FEATURE_BEAT_REPEAT
    wire();
    stage()->core().post(beatrepeat::Core::C_PRE, on ? 1 : 0);
#else
    (void)on;
#endif
}

void seqBrPanic()
{
    s_ui.panic();
#ifdef FEATURE_BEAT_REPEAT
    wire();
    stage()->core().post(beatrepeat::Core::C_PANIC);
#endif
}

bool seqBrStatus(beatrepeat::Status* st)
{
#ifdef FEATURE_BEAT_REPEAT
    *st = stage()->core().status();
    return true;
#else
    (void)st;
    return false;
#endif
}

float seqBrCpuPercent()
{
#ifdef FEATURE_BEAT_REPEAT
    return stage()->cpuPercent();
#else
    return 0.0f;
#endif
}

void seqBrFillModel(seqbr::BrModel* m)
{
    memset(m, 0, sizeof(*m));
    m->page = s_ui.page;
    m->mode = s_ui.prm.mode;
    m->latch = s_ui.latch ? 1 : 0;
    m->heldMask = s_ui.heldMask();
    memcpy(m->step, s_ui.prm.step, sizeof(m->step));
    beatrepeat::Status st;
    memset(&st, 0, sizeof(st));
    if (seqBrStatus(&st))
    {
        m->state = st.state;
        m->eventGrid = st.grid;
        m->repeatIdx = (uint8_t)(st.repeatIdx > 255 ? 255 : st.repeatIdx);
        m->levelDb = st.levelDb == 255 ? 0 : st.levelDb;
        m->pitchSemi = st.pitchSemi;
        const float cpu = seqBrCpuPercent() * 10.0f;
        m->cpu10 = (uint8_t)(cpu > 255.0f ? 255.0f : cpu);
#ifdef FEATURE_BEAT_REPEAT
        const float bpm = sequencer.getCurrentTempo();
        m->bpm10 = (uint16_t)(bpm > 0.0f ? bpm * 10.0f : 0.0f);
        if (st.state != beatrepeat::S_IDLE && s_ui.nHeld)
        {
            // the grid the pad asked for is the held one; the stage shortens it when the tempo is too slow for the ring
            const uint8_t held = s_ui.held[s_ui.nHeld - 1];
            m->setGrid = held;
            m->clamped = beatrepeat::gridTicks(held) != beatrepeat::gridTicks(st.grid) ? 1 : 0;
        }
        if (st.captures != s_waveFor)
        {
            s_waveFor = st.captures;
            const uint32_t n = stage()->core().captured();
            s_waveOk = n >= 16;
            if (s_waveOk)
                seqbr::foldWave(stage()->core().ring(), n, s_lo, s_hi, stage()->core().sliceBase(), stage()->core().capacity());
        }
        m->waveOk = s_waveOk ? 1 : 0;
        if (s_waveOk)
        {
            memcpy(m->lo, s_lo, sizeof(m->lo));
            memcpy(m->hi, s_hi, sizeof(m->hi));
        }
#endif
    }
}

void seqBrFillHead(seqbr::BrHead* h)
{
    beatrepeat::Status st;
    memset(&st, 0, sizeof(st));
    seqBrStatus(&st);
    *h = seqbr::makeBrHead(st.state, st.slice1000, (uint8_t)(st.repeatIdx > 255 ? 255 : st.repeatIdx), seqbr::kBrViewW);
}

void seqBrUpdate()
{
#ifdef FEATURE_BEAT_REPEAT
    if (!s_wired)
    {
        wire();
        seqBrPush();
    }
#endif
    // never ask for a write while the transport plays: the dirty flag waits for the stop
    if (!s_newerFile && !s_wsWritePending && s_dirty.take(sequencer.isPlay()))
    {
        if (s_ui.prm.hash() != s_savedHash)
        {
            s_hadFile = true;
            newFileManager.setProjectStructChanged();
            s_wsWritePending = true;
        }
    }
}

void seqBrLoadWorkspace()
{
    s_ui.prm = seqbr::BrParams();
    s_hadFile = false;
    s_loadedValid = false;
    s_newerFile = false;
    if (seqsidecar::exists(kWorkspaceFile))
    {
        size_t n = seqsidecar::readFile(kWorkspaceFile, seqsidecar::ioBuffer(), seqsidecar::ioCapacity());
        bool newer = false;
        if (n && s_ui.prm.load(seqsidecar::ioBuffer(), n, &newer))
            s_hadFile = true;
        else
        {
            s_ui.prm = seqbr::BrParams(); // short, corrupt or newer file: defaults, the file itself is left alone
            s_newerFile = newer;
        }
        s_loadedValid = true;
    }
    s_loadedId = seqlogic::projectId(newFileManager.getCurrentProjectPath(), newFileManager.getCurrentProjectName());
    s_savedHash = s_ui.prm.hash();
    s_wsWritePending = false;
    s_dirty.dirty = false;
    seqBrPush();
    seqCutLoadSettings(s_ui.prm.mlrQuant, s_ui.prm.mlrSlotA, s_ui.prm.mlrSlotB); // plan 026: the MLR page settings of the project
}

void seqBrSaveWorkspace()
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

void seqBrSaveProject(const char* path, const char* name)
{
    if (s_newerFile)
        return; // a file of a newer mod version stays as it is
    char file[PATCH_SIZE];
    seqsidecar::projectFilePath(file, sizeof(file), path, name, kFileName);
    uint32_t id = seqlogic::projectId(path, name);
    switch (seqlogic::sidecarAction(s_hadFile || !s_ui.prm.isDefault(), s_loadedValid && s_loadedId == id))
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

void seqBrImportProject(const char* path, const char* name)
{
    char file[PATCH_SIZE];
    seqsidecar::projectFilePath(file, sizeof(file), path, name, kFileName);
    if (seqsidecar::exists(file))
        seqsidecar::importToWorkspace(file, kWorkspaceFile);
}
