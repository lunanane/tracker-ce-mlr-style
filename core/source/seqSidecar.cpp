// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
#include "seqSidecar.h"
#include "seqClips.h"
#include "seqFx.h"
#include "seqPlinky.h"
#include "seqHooks.h"
#include "seqClipsLogic.h"
#include "seqTracks.h"
#include "seqTracksCore.h"
#include "fileManager.h"
#include "fileManagerDefs.h"
#include "fileTransfer.h"
#include "config.h"
#include <cr_section_macros.h>
#include <stdio.h>

namespace seqsidecar {

// One shared buffer for all sidecar file steps. Sized for the largest frame.
constexpr size_t kIoSize = (size_t)seqtracks::Engine::kSidecarMax > (size_t)seqclips::kFileMax ? (size_t)seqtracks::Engine::kSidecarMax
                                                                                              : (size_t)seqclips::kFileMax;
__NOINIT(EXTERNAL_RAM) static uint8_t s_io[kIoSize];

uint8_t* ioBuffer() { return s_io; }
size_t ioCapacity() { return kIoSize; }

bool exists(const char* path)
{
    FileSystem* fs = newFileManager.getFileSystem();
    return fs && fs->exists(path);
}

size_t fileSize(const char* path)
{
    FileSystem* fs = newFileManager.getFileSystem();
    if (!fs || !fs->exists(path))
        return 0;
    return fs->getFileSize(path);
}

// Writes <path>.tmp completely, then replaces <path> (remove + rename: a half-written file never takes the place of the old one;
// the FAT API has no atomic replace, so a power loss between remove and rename leaves only the .tmp file, which nothing reads).
bool writeFile(const char* path, const uint8_t* buf, size_t n)
{
    FileSystem* fs = newFileManager.getFileSystem();
    char tmp[PATCH_SIZE + 8];
    int len = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    bool ok;
    if (fs && len > 0 && (size_t)len < sizeof(tmp))
    {
        ok = fileTransfer.saveMemoryToFile(const_cast<uint8_t*>(buf), tmp, (uint32_t)n) == fileTransferEnd;
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
        ok = fileTransfer.saveMemoryToFile(const_cast<uint8_t*>(buf), path, (uint32_t)n) == fileTransferEnd;
    }
    return ok;
}

size_t readFile(const char* path, uint8_t* buf, size_t cap)
{
    size_t sz = fileSize(path);
    if (sz > cap)
        sz = cap;
    // the real file length is returned: bytes left in the shared buffer by an earlier file must not complete a short file
    if (sz && fileTransfer.loadFileToMemory(path, buf, (uint32_t)sz, fileWholeOnce) == fileTransferEnd)
        return sz;
    return 0;
}

void removeIfExists(const char* path)
{
    FileSystem* fs = newFileManager.getFileSystem();
    if (fs && fs->exists(path))
        fs->remove(path);
}

bool importToWorkspace(const char* projectFile, const char* workspaceFile)
{
    size_t n = readFile(projectFile, s_io, kIoSize);
    return n && fileTransfer.saveMemoryToFile(s_io, workspaceFile, (uint32_t)n) == fileTransferEnd;
}

void projectFilePath(char* out, size_t cap, const char* path, const char* name, const char* file)
{
    snprintf(out, cap, "%s/%s/%s", path, name, file);
}

}  // namespace seqsidecar

// ---------------------------------------------------------------- dispatcher
void seqSidecarsLoadWorkspace()
{
    seqTracksLoadWorkspace();
    seqClipsLoadWorkspace();
#ifdef FEATURE_PLINKY
    seqPlinkyLoadWorkspace(); // plan 009 S: synth voice values; a project without the file = voice off
#endif
    seqFxLoadWorkspace();     // plan 007: reverb type / highpass / delay bandpass width; a project without the file = stock sound
    seqFaderProjectLoaded(); // perform FX are not saved: nothing stays dialled in across a project load
}

void seqSidecarsSaveWorkspace()
{
    seqTracksSaveWorkspace();
    seqClipsSaveWorkspace();
    seqFxSaveWorkspace();
#ifdef FEATURE_PLINKY
    seqPlinkySaveWorkspace();
#endif
}

void seqSidecarsSaveProject(const char* path, const char* name)
{
    seqTracksSaveProject(path, name);
    seqClipsSaveProject(path, name);
    seqFxSaveProject(path, name);
#ifdef FEATURE_PLINKY
    seqPlinkySaveProject(path, name);
#endif
}

void seqSidecarsImportProject(const char* path, const char* name)
{
    seqTracksImportProject(path, name);
    seqClipsImportProject(path, name);
    seqFxImportProject(path, name);
#ifdef FEATURE_PLINKY
    seqPlinkyImportProject(path, name);
#endif
}
