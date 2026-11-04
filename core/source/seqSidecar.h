// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Shared file helpers of the project sidecars (seqtracks.bin, seqclips.bin) and the one dispatcher the four FileManager hook
// sites call. A sidecar never blocks a project load: a missing, short, newer or corrupt file means defaults.
// Write scheme (plan 005 M2): sidecars only mark dirty while the transport plays; the file is written once it stopped (or on an
// explicit Save). Safe write: <file>.tmp completely, then remove + rename.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace seqsidecar {

// The one I/O buffer shared by every sidecar step (main thread, one at a time). External RAM: the MK66 stack is only 4 KB.
uint8_t* ioBuffer();
size_t ioCapacity();

bool exists(const char* path);
size_t fileSize(const char* path); // 0 = missing
bool writeFile(const char* path, const uint8_t* buf, size_t n); // <path>.tmp, then replace <path>; false on failure
size_t readFile(const char* path, uint8_t* buf, size_t cap);    // bytes read (<= cap, the real file length); 0 = missing / failed
void removeIfExists(const char* path);
bool importToWorkspace(const char* projectFile, const char* workspaceFile); // copy a project's file into the Workspace mirror

// <path>/<name>/<file>
void projectFilePath(char* out, size_t cap, const char* path, const char* name, const char* file);

}  // namespace seqsidecar

// ---- the four FileManager hook sites (main loop, file manager steps) ----
void seqSidecarsLoadWorkspace();                                   // WorkspaceProjectLoad.cpp
void seqSidecarsSaveWorkspace();                                   // WorkspaceProjectSave.cpp
void seqSidecarsSaveProject(const char* path, const char* name);   // ProjectSave.cpp
void seqSidecarsImportProject(const char* path, const char* name); // ProjectOpen.cpp
