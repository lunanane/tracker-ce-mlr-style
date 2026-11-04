// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// v0.2.4 FX additions (plan 007): delay bandpass width, reverb highpass and reverb type. The values live here (not in the stock
// project struct) and are saved in the sidecar "seqfx.bin" next to the project, with the same deferred-write scheme as
// seqclips.bin: a change only marks the file dirty while the transport plays, the write is asked for after the stop.
// Every setter also pushes the value into the audio engine.
#pragma once
#include "seqFxLogic.h"

// ---- values (main loop) ----
uint8_t seqFxBandwidth();                 // delay bandpass width, 1/12 octave (seqfx::kBwMin .. kBwMax)
void seqFxSetBandwidth(uint8_t twelfths);
uint8_t seqFxHighpass();                  // reverb highpass index, 0 = off (seqfx::kHpMax)
void seqFxSetHighpass(uint8_t index);
uint8_t seqFxReverbType();                // seqfx::RT_STOCK / RT_PLINKY
void seqFxSetReverbType(uint8_t type);
void seqFxApplyToEngine();                // all three values + the stock delay centre (after a project load)

// ---- sidecar "seqfx.bin" ----
void seqFxLoadWorkspace();                                      // Workspace/seqfx.bin -> values (missing / invalid: defaults)
void seqFxSaveWorkspace();                                      // values -> Workspace/seqfx.bin when asked (never while playing)
void seqFxSaveProject(const char* path, const char* name);      // values -> <path>/<name>/seqfx.bin (+ workspace)
void seqFxImportProject(const char* path, const char* name);    // <path>/<name>/seqfx.bin -> Workspace (when present)
void seqFxUpdate();                                             // once per main loop pass: flags the autosave after the transport stopped
