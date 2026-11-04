// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Firmware glue of the Session view clip launcher (plans/002-session-view.md, phase S0/C1).
// A clip = one track of one pattern. The file is read on the main loop as soon as a clip is queued
// (loadPerformanceTrackToBuffer); the copy into the playing pattern happens in the audio path at the quantise point.
#pragma once
#include "seqSessionLogic.h"

extern seqsession::Launcher seqLauncher;

// ---- audio context ----
inline bool seqSessionStopped(int row) { return seqLauncher.stopped((uint8_t)row); } // play_microStep: stopped track is silent

// ---- main loop (Session view) ----
// All launch calls return false when the pattern buffer is busy (pattern load / queued pattern switch / last
// song step) or the ring is full; nothing is queued then.
bool seqSessionLaunchClip(uint8_t track, uint8_t pattern); // read the clip now, switch at the quantise point
bool seqSessionStopTrack(uint8_t track);                   // silent from the quantise point until a clip is launched
bool seqSessionStopAll();
bool seqSessionLaunchScene(uint8_t pattern);               // stock pattern switch (pattern end), or at once when stopped
uint8_t seqSessionOwnPattern();                            // pattern currently loaded (mtProject.values.actualPattern)
uint8_t seqSessionQueuedScene();                           // queued stock pattern switch, 0 = none

// "Has notes" cache of the 4-pattern window. The rows are read one per main-loop pass while the Session view is
// active; never in the audio context.
void seqSessionSetWindow(uint8_t first);
const seqsession::NoteCache& seqSessionCache();
void seqSessionActive(bool on);

void seqSessionUpdate(); // once per main loop pass (Tracker::update)

// ---- saving: launched clips never reach the user's pattern file ----
bool seqSessionHasLaunched();                // some track of the loaded pattern holds a launched clip (RAM != file)
void seqSessionMergeForSave(void* destPattern); // fills a strPattern with RAM + the launched tracks read from the pattern file
bool seqSessionOwnsPerformance();            // Session switched performance mode on (the pattern may then be saved normally)
