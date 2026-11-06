// Sequencer tracks mod (plan 016 B) - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
#include "seqWarp.h"
#include "seqSidecar.h"
#include "seqModLogic.h"
#include "seqTracksLogic.h"
#include "mtSequencer.h"
#include "mtStructs.h"
#include "fileManager.h"
#include "fileManagerDefs.h"
#include "config.h"
#include "play_memory.h"

extern Sequencer sequencer;

namespace
{
seqwarp::Params s_params;
seqlogic::DirtyGate s_dirty;     // a value changed: the workspace autosave is asked for once the transport stopped
bool s_wsWritePending = false;   // the autosave was asked for; SaveWorkspace writes the file
bool s_hadFile = false;          // a seqwarp.bin was loaded or written for this project
uint16_t s_savedHash = 0;        // hash of the values last written / loaded
uint32_t s_loadedId = 0;         // project folder whose sidecar was loaded (valid or not)
bool s_loadedValid = false;
bool s_newerFile = false;        // the loaded sidecar was written by a newer mod version: it is left alone (no downgrade, nothing written)

const char kWorkspaceFile[] = "Workspace/seqwarp.bin";
const char kFileName[] = "seqwarp.bin";

bool writeValues(const char* path)
{
    if (s_newerFile)
        return false;
    uint8_t* buf = seqsidecar::ioBuffer();
    uint16_t h = s_params.hash();
    size_t n = s_params.save(buf, seqsidecar::ioCapacity());
    if (!n || !seqsidecar::writeFile(path, buf, n))
        return false;
    s_savedHash = h;
    return true;
}
} // namespace

bool seqWarpOn(uint8_t instr) { return instr < seqwarp::kCount && s_params.it[instr].on != 0; }
uint8_t seqWarpBars(uint8_t instr) { return instr < seqwarp::kCount ? s_params.it[instr].bars : (uint8_t)0; }

void seqWarpSetOn(uint8_t instr, bool on)
{
    if (instr >= seqwarp::kCount || (s_params.it[instr].on != 0) == on)
        return;
    s_params.it[instr].on = on ? 1 : 0;
    s_dirty.mark();
}

void seqWarpSetBars(uint8_t instr, uint8_t bars)
{
    if (instr >= seqwarp::kCount)
        return;
    bars = seqwarp::clampBars(bars);
    if (bars == s_params.it[instr].bars)
        return;
    s_params.it[instr].bars = bars;
    s_dirty.mark();
}

float seqWarpCpuPercent(uint8_t* voices)
{
    return AudioPlayMemory::warpCpuPercent(voices);
}

void seqWarpUpdate()
{
    // never ask for a write while the transport plays: the dirty flag waits for the stop
    if (!s_newerFile && !s_wsWritePending && s_dirty.take(sequencer.isPlay()))
    {
        if (s_params.hash() != s_savedHash)
        {
            s_hadFile = true;
            newFileManager.setProjectStructChanged();
            s_wsWritePending = true;
        }
    }
}

void seqWarpLoadWorkspace()
{
    s_params = seqwarp::Params();
    s_hadFile = false;
    s_loadedValid = false;
    s_newerFile = false;
    if (seqsidecar::exists(kWorkspaceFile))
    {
        size_t n = seqsidecar::readFile(kWorkspaceFile, seqsidecar::ioBuffer(), seqsidecar::ioCapacity());
        bool newer = false;
        if (n && s_params.load(seqsidecar::ioBuffer(), n, &newer))
            s_hadFile = true;
        else
        {
            s_params = seqwarp::Params(); // short, corrupt or newer file: warp off, the file itself is left alone
            s_newerFile = newer;
        }
        s_loadedValid = true;
    }
    s_loadedId = seqlogic::projectId(newFileManager.getCurrentProjectPath(), newFileManager.getCurrentProjectName());
    s_savedHash = s_params.hash();
    s_wsWritePending = false;
    s_dirty.dirty = false;
}

void seqWarpSaveWorkspace()
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

void seqWarpSaveProject(const char* path, const char* name)
{
    if (s_newerFile)
        return; // a file of a newer mod version stays as it is
    char file[PATCH_SIZE];
    seqsidecar::projectFilePath(file, sizeof(file), path, name, kFileName);
    uint32_t id = seqlogic::projectId(path, name);
    switch (seqlogic::sidecarAction(s_hadFile || !s_params.isDefault(), s_loadedValid && s_loadedId == id))
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

void seqWarpImportProject(const char* path, const char* name)
{
    char file[PATCH_SIZE];
    seqsidecar::projectFilePath(file, sizeof(file), path, name, kFileName);
    if (seqsidecar::exists(file))
        seqsidecar::importToWorkspace(file, kWorkspaceFile);
}
