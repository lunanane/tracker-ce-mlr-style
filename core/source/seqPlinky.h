// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md). Plinky voice parts: MIT (libs/Audio/PlinkyVoice)
// Glue of the synth voice engine (plan 009 S, original Tracker only): MIDI routing, parameter values, the sidecar "plinky.bin"
// with the same deferred-write scheme as seqfx.bin. The engine is audioEngine::getPlinky() (libs/Audio/synth_plinky.h).
#pragma once
#include <stdint.h>

#ifdef FEATURE_PLINKY
// ---- MIDI routing (called by sendMidiNoteOn / Off, sendCC, sendPitchbend for non-synth ports; any context). true = the channel
// is the synth voice (Plinky parameter "Channel"): the message does not go to the MIDI outputs.
bool seqPlinkyNote(uint8_t channel, uint8_t note, uint8_t velo, bool on);
bool seqPlinkyControlChange(uint8_t channel, uint8_t cc, uint8_t value);
bool seqPlinkyPitchBend(uint8_t channel, int16_t value); // -8192 .. 8191
void seqPanicPlinky();                                   // transport Stop: all notes off

// ---- the PolySynth instrument (v0.3.2) ----
// A fixed MIDI instrument slot is the synth: MIDI instrument 16 for a new project, the instrument of the claimed channel for a project
// of v0.3 / v0.3.1 (a file with Channel 0 = off gets 16). Any track, pad, pattern note or sequencer that plays that instrument plays the voice.
// plan 019: three instances PolySynth 1 / 2 / 3 (MIDI instruments 16, 15, 14 by default; each instance has its own channel parameter)
int seqPlinkyInstrumentOf(int inst);         // any context: instrument index of the instance's slot, -1 = none
int seqPlinkyInstanceOf(int instrument);     // any context: the instance (0 .. 2) that plays this instrument index, -1 = not a PolySynth
bool seqPlinkyIsVoiceInstrument(int instrument);
bool seqPlinkyTrackUsesVoice(uint8_t track); // main loop: the instrument the track plays now is a PolySynth
int seqPlinkyTrackInstance(uint8_t track);   // main loop: ... which instance (-1 = none)
// the instance the view shows and edits (main loop)
namespace instorder { struct Layout; }
void seqPlinkyFillLayout(instorder::Layout& L); // plan 019 A: the instrument order (PolySynth 1 / 2 / 3 first), see seqInstOrderLogic.h
int seqPlinkyInst();
void seqPlinkySetInst(int inst);
int seqPlinkyCycleInst();                    // 0 -> 1 -> 2 -> 0, returns the new one
bool seqPlinkyNewerFile(); // plinky.bin was written by a newer mod version: edits are not saved

// ---- values (main loop) ----
int seqPlinkyGet(int param);
void seqPlinkySet(int param, int value); // clamped; marks the project sidecar dirty; applies the sends
void seqPlinkyDefault(int param);
void seqPlinkySetAll(int param, int delta); // Shift + dial: the same delta on ALL instances, clamped per instance (plan 019 A)
int seqPlinkyVoiceCap();                     // the governor: voices the pool may sound now
float seqPlinkyCpu();                    // % of the 128 sample block period: average ...
float seqPlinkyCpuPeak();                // ... and slowly decaying peak
int seqPlinkyVoicesActive();
// v0.3.6: fills the parameter view model (values, marks, morph, voices, CPU, newer-file flag) for the Play-mode view and the instrument
// page; the caller sets sec / idx / track (0 = the instrument page, no track)
namespace seqplinky { struct Model; }
void seqPlinkyFillModel(seqplinky::Model& m);
// v0.4.1 pitch view (any context, plain reads): the live state of pool voice 0 .. 7 (plinkyvoice::Engine::liveVoice)
void seqPlinkyLiveVoice(int voice, bool* on, uint8_t* inst, float* pitch, float* env, uint32_t* serial);

// ---- plan 012: snapshots per pattern and the timed morph ----
// Parameter number kPlinkyMorphParam (= the engine parameter count) is the project morph time in half seconds (0 = jump .. 60 = 30 s);
// seqPlinkyGet / Set / Default take it like any parameter. Edits are written into the snapshot of the playing (or shown) pattern.
enum { kPlinkyMorphParam = 33 }; // = plinkyvoice::P_COUNT (checked in seqPlinky.cpp)
bool seqPlinkyMorphStatus(uint16_t* remainMs, uint8_t* pattern); // true while a morph runs
bool seqPlinkyVoice(int i, uint8_t* note, uint8_t* level, uint8_t* releasing); // v0.3.3 display: voice i (0..3) sounds; level 0..60
uint64_t seqPlinkyMarks();                                       // bit per parameter that differs from the inherited value (shown pattern)
void seqPlinkyClipLaunched(uint8_t track, uint8_t pattern);      // main loop: a clip was queued on the track (morph to its source pattern)

// TrackerImpl::init (after the SDRAM is up, before any other seqPlinky call that touches the morph state): constructs the
// EXTERNAL_RAM state (it has no C++ static constructor, see seqPlinky.cpp)
void seqPlinkyInit();

// ---- sidecar "plinky.bin" ----
void seqPlinkyLoadWorkspace();                                   // Workspace/plinky.bin -> values (missing / invalid: defaults)
void seqPlinkySaveWorkspace();                                   // values -> Workspace/plinky.bin when asked (never while playing)
void seqPlinkySaveProject(const char* path, const char* name);   // values -> <path>/<name>/plinky.bin (+ workspace)
void seqPlinkyImportProject(const char* path, const char* name); // <path>/<name>/plinky.bin -> Workspace (when present)
void seqPlinkyUpdate();                                          // once per main loop pass: flags the autosave after the transport stopped
#endif
