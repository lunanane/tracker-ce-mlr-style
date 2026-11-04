// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Per-clip play modes of the clip launcher and their sidecar "seqclips.bin" (plan 005 R2). The table lives here; the launcher UI
// and the follow action (R2) read and write it through these functions on the main loop.
#pragma once
#include "seqClipsLogic.h"

// ---- play modes (main loop) ----
uint8_t seqClipMode(uint8_t pattern, uint8_t track);                // seqclips::CLIP_LOOP / CLIP_PLAY1X
void seqClipSetMode(uint8_t pattern, uint8_t track, uint8_t mode);  // marks the sidecar dirty when it changed
void seqClipSetRowMode(uint8_t pattern, uint8_t mode);              // every track of the pattern (Shift + button 1 on a scene)

// ---- sidecar "seqclips.bin" (main loop, file manager steps; same scheme as seqtracks.bin) ----
void seqClipsLoadWorkspace();                                       // Workspace/seqclips.bin -> table (missing / invalid: all Loop)
void seqClipsSaveWorkspace();                                       // table -> Workspace/seqclips.bin when it changed (never while playing)
void seqClipsSaveProject(const char* path, const char* name);       // table -> <path>/<name>/seqclips.bin (+ workspace)
void seqClipsImportProject(const char* path, const char* name);     // <path>/<name>/seqclips.bin -> Workspace (when present)
void seqClipsUpdate();                                              // once per main loop pass: flags the autosave after the transport stopped
