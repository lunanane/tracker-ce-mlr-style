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
int seqPlinkyInstrument();                // any context: instrument index of the PolySynth slot, -1 while the voice is off
bool seqPlinkyIsVoiceInstrument(int instrument);
bool seqPlinkyTrackUsesVoice(uint8_t track); // main loop: the instrument the track plays now is the PolySynth
bool seqPlinkyNewerFile(); // plinky.bin was written by a newer mod version: edits are not saved

// ---- values (main loop) ----
int seqPlinkyGet(int param);
void seqPlinkySet(int param, int value); // clamped; marks the project sidecar dirty; applies the sends
void seqPlinkyDefault(int param);
float seqPlinkyCpu();                    // % of the 128 sample block period: average ...
float seqPlinkyCpuPeak();                // ... and slowly decaying peak
int seqPlinkyVoicesActive();

// ---- sidecar "plinky.bin" ----
void seqPlinkyLoadWorkspace();                                   // Workspace/plinky.bin -> values (missing / invalid: defaults)
void seqPlinkySaveWorkspace();                                   // values -> Workspace/plinky.bin when asked (never while playing)
void seqPlinkySaveProject(const char* path, const char* name);   // values -> <path>/<name>/plinky.bin (+ workspace)
void seqPlinkyImportProject(const char* path, const char* name); // <path>/<name>/plinky.bin -> Workspace (when present)
void seqPlinkyUpdate();                                          // once per main loop pass: flags the autosave after the transport stopped
#endif
