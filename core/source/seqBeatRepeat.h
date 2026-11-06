// Sequencer tracks mod (plan 021) - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Beat repeat, glue between the CUT / MLR page and the master insert (libs/Audio/effect_beatRepeat.*): the controller state of the pads, the
// commands to the audio side, the screen model, and the settings sidecar "seqbrpt.bin" next to the project (same deferred-write scheme as
// seqwarp.bin: a change only marks the file dirty while the transport plays, the write is asked for after the stop). Not saved: latch, the
// page, a repeat in progress.
#pragma once
#include "seqBeatRepeatLogic.h"

seqbr::Ui& seqBrUi();                          // controller state of the layer (page, held pads, latch, the settings)
void seqBrAct(const seqbr::Result& r);         // an action from the pads: told to the audio side / values pushed (main loop)
void seqBrPanic();                             // transport Stop: any repeat ends, latch off, the pads in the hand are forgotten
bool seqBrStatus(beatrepeat::Status* st);      // state of the stage; false = no stage (host tests, other platforms)
float seqBrCpuPercent();                       // CPU of the stage in the audio interrupt, % of the block period (0 while idle)
void seqBrFillModel(seqbr::BrModel* m);        // the screen model of the beat repeat layer
void seqBrFillHead(seqbr::BrHead* h);          // the moving part (playhead)
void seqBrPush();                              // the settings to the audio side (after a load)

// ---- sidecar "seqbrpt.bin" ----
void seqBrLoadWorkspace();                                    // Workspace/seqbrpt.bin -> settings (missing / invalid: defaults)
void seqBrSaveWorkspace();                                    // settings -> Workspace/seqbrpt.bin when asked (never while playing)
void seqBrSaveProject(const char* path, const char* name);    // settings -> <path>/<name>/seqbrpt.bin (+ workspace)
void seqBrImportProject(const char* path, const char* name);  // <path>/<name>/seqbrpt.bin -> Workspace (when present)
void seqBrUpdate();                                           // once per main loop pass: flags the autosave after the transport stopped
