// Sequencer tracks mod (plan 016 B) - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Warp of the sample instruments: Off / On and the length in bars (0 = Auto). The values live here (not in the stock instrument
// struct, which is fixed at 372 bytes) and are saved in the sidecar "seqwarp.bin" next to the project, with the same deferred
// write scheme as seqfx.bin: a change only marks the file dirty while the transport plays, the write is asked for after the stop.
// The audio engine reads the values at every note start (play_memory_warp.cpp).
#pragma once
#include "seqWarpLogic.h"

// ---- values (main loop writes, audio interrupt reads single bytes) ----
bool seqWarpOn(uint8_t instr);
uint8_t seqWarpBars(uint8_t instr); // the setting, 0 = Auto
void seqWarpSetOn(uint8_t instr, bool on);
void seqWarpSetBars(uint8_t instr, uint8_t bars);

// ---- sidecar "seqwarp.bin" ----
void seqWarpLoadWorkspace();                                    // Workspace/seqwarp.bin -> values (missing / invalid: all off)
void seqWarpSaveWorkspace();                                    // values -> Workspace/seqwarp.bin when asked (never while playing)
void seqWarpSaveProject(const char* path, const char* name);    // values -> <path>/<name>/seqwarp.bin (+ workspace)
void seqWarpImportProject(const char* path, const char* name);  // <path>/<name>/seqwarp.bin -> Workspace (when present)
void seqWarpUpdate();                                           // once per main loop pass: flags the autosave after the transport stopped

// ---- display / readout (main loop) ----
float seqWarpCpuPercent(uint8_t* voices);  // CPU of the warped voices in the audio interrupt, % of the block period (sum); voices = how many warp now
