// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
#include "seqClips.h"
#include "seqSidecar.h"
#include "seqModLogic.h"
#include "seqTracksLogic.h"
#include "mtSequencer.h"
#include "fileManager.h"
#include "fileManagerDefs.h"
#include "config.h"

extern Sequencer sequencer;

namespace
{
// 1 KB, in the OCRAM bss section (the Plus has only ~5 KB of external RAM left)
OCRAM_BSS_SECTION seqclips::ClipModes s_modes;

seqlogic::DirtyGate s_dirty;     // a mode changed: the workspace autosave is asked for once the transport stopped
bool s_wsWritePending = false;   // the autosave was asked for; SaveWorkspace writes the file
bool s_hadFile = false;          // a seqclips.bin was loaded or written for this project
uint16_t s_savedHash = 0;        // hash of the table last written / loaded
uint32_t s_loadedId = 0;         // project folder whose sidecar was loaded (valid or not)
bool s_loadedValid = false;

const char kWorkspaceFile[] = "Workspace/seqclips.bin";
const char kFileName[] = "seqclips.bin";

bool writeTable(const char* path)
{
    uint8_t* buf = seqsidecar::ioBuffer();
    uint16_t h = s_modes.hash();
    size_t n = s_modes.save(buf, seqsidecar::ioCapacity());
    if (!n || !seqsidecar::writeFile(path, buf, n))
        return false;
    s_savedHash = h;
    return true;
}
} // namespace

uint8_t seqClipMode(uint8_t pattern, uint8_t track)
{
    return s_modes.mode(pattern, track);
}

void seqClipSetMode(uint8_t pattern, uint8_t track, uint8_t mode)
{
    if (s_modes.setMode(pattern, track, mode))
        s_dirty.mark();
}

void seqClipSetRowMode(uint8_t pattern, uint8_t mode)
{
    if (s_modes.setRow(pattern, mode))
        s_dirty.mark();
}

void seqClipsUpdate()
{
    // never ask for a write while the transport plays: the dirty flag waits for the stop
    if (!s_wsWritePending && s_dirty.take(sequencer.isPlay()))
    {
        if (s_modes.hash() != s_savedHash)
        {
            s_hadFile = true;
            newFileManager.setProjectStructChanged();
            s_wsWritePending = true;
        }
    }
}

void seqClipsLoadWorkspace()
{
    s_modes.clear();
    s_hadFile = false;
    s_loadedValid = false;
    if (seqsidecar::exists(kWorkspaceFile))
    {
        size_t n = seqsidecar::readFile(kWorkspaceFile, seqsidecar::ioBuffer(), seqsidecar::ioCapacity());
        if (n && s_modes.load(seqsidecar::ioBuffer(), n))
            s_hadFile = true;
        else
            s_modes.clear(); // short, corrupt or newer file: defaults, the file itself is left alone
        s_loadedValid = true;
    }
    s_loadedId = seqlogic::projectId(newFileManager.getCurrentProjectPath(), newFileManager.getCurrentProjectName());
    s_savedHash = s_modes.hash();
    s_wsWritePending = false;
    s_dirty.dirty = false;
}

void seqClipsSaveWorkspace()
{
    if (!s_wsWritePending)
        return;
    s_wsWritePending = false;
    if (sequencer.isPlay())
    {
        s_dirty.mark(); // still playing: write after the stop
        return;
    }
    if (!writeTable(kWorkspaceFile))
        s_dirty.mark(); // retried at the next pass without play
}

void seqClipsSaveProject(const char* path, const char* name)
{
    char file[PATCH_SIZE];
    seqsidecar::projectFilePath(file, sizeof(file), path, name, kFileName);
    uint32_t id = seqlogic::projectId(path, name);
    switch (seqlogic::sidecarAction(s_hadFile || s_modes.anyNonDefault(), s_loadedValid && s_loadedId == id))
    {
        case seqlogic::SIDE_WRITE:
            if (writeTable(file))
            {
                s_loadedValid = true;
                s_loadedId = id;
                s_hadFile = true;
            }
            writeTable(kWorkspaceFile);
            s_wsWritePending = false;
            break;
        case seqlogic::SIDE_REMOVE:
            seqsidecar::removeIfExists(file);
            seqsidecar::removeIfExists(kWorkspaceFile);
            break;
        case seqlogic::SIDE_KEEP: break;
    }
}

void seqClipsImportProject(const char* path, const char* name)
{
    char file[PATCH_SIZE];
    seqsidecar::projectFilePath(file, sizeof(file), path, name, kFileName);
    if (seqsidecar::exists(file))
        seqsidecar::importToWorkspace(file, kWorkspaceFile);
}
