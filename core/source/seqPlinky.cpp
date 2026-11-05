// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md). Plinky voice parts: MIT (libs/Audio/PlinkyVoice)
#include "seqPlinky.h"
#ifdef FEATURE_PLINKY
#include "seqSidecar.h"
#include "seqModLogic.h"
#include "seqTracksLogic.h"
#include "mtAudioEngine.h"
#include "mtSequencer.h"
#include "mtStructs.h"
#include "fileManager.h"
#include "fileManagerDefs.h"
#include "config.h"

extern Sequencer sequencer;

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
    uint8_t* buf = seqsidecar::ioBuffer();
    const Params p = current();
    size_t n = p.save(buf, seqsidecar::ioCapacity());
    if (!n || !seqsidecar::writeFile(path, buf, n))
        return false;
    s_savedHash = p.hash();
    return true;
}
} // namespace

bool seqPlinkyNote(uint8_t channel, uint8_t note, uint8_t velo, bool on)
{
    if (eng().param(P_CHANNEL) != channel)
        return false;
    if (on)
        eng().noteOn(note, velo ? velo : 1);
    else
        eng().noteOff(note);
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

int seqPlinkyGet(int param)
{
    return eng().param(param);
}

void seqPlinkySet(int param, int value)
{
    const uint8_t before = eng().param(param);
    eng().setParam(param, value);
    if (eng().param(param) != before)
    {
        if (param == P_CHANNEL)
            eng().allNotesOff(); // the old channel goes back to the MIDI outputs: nothing may hang
        if (param == P_REVERB || param == P_DELAY)
            engine.applyPlinkySends();
        s_dirty.mark();
    }
}

void seqPlinkyDefault(int param)
{
    seqPlinkySet(param, paramInfo(param).def);
}

float seqPlinkyCpu() { return engine.getPlinkyCpuPercent(); }
float seqPlinkyCpuPeak() { return engine.getPlinkyCpuPeakPercent(); }
int seqPlinkyVoicesActive() { return eng().activeVoices(); }

void seqPlinkyUpdate()
{
    // never ask for a write while the transport plays: the dirty flag waits for the stop
    if (!s_newerFile && !s_wsWritePending && s_dirty.take(sequencer.isPlay()))
    {
        if (current().hash() != s_savedHash)
        {
            s_hadFile = true;
            newFileManager.setProjectStructChanged();
            s_wsWritePending = true;
        }
    }
}

void seqPlinkyLoadWorkspace()
{
    Params p;
    s_hadFile = false;
    s_loadedValid = false;
    s_newerFile = false;
    if (seqsidecar::exists(kWorkspaceFile))
    {
        size_t n = seqsidecar::readFile(kWorkspaceFile, seqsidecar::ioBuffer(), seqsidecar::ioCapacity());
        bool newer = false;
        if (n && p.load(seqsidecar::ioBuffer(), n, &newer))
            s_hadFile = true;
        else
        {
            p = Params(); // short, corrupt or newer file: defaults, the file itself is left alone
            s_newerFile = newer;
        }
        s_loadedValid = true;
    }
    s_loadedId = seqlogic::projectId(newFileManager.getCurrentProjectPath(), newFileManager.getCurrentProjectName());
    eng().allNotesOff();
    apply(p); // a project without the file: the synth voice is off (Channel 0), the MIDI instruments work as before
    s_savedHash = p.hash();
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
    switch (seqlogic::sidecarAction(s_hadFile || !current().isDefault(), s_loadedValid && s_loadedId == id))
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
