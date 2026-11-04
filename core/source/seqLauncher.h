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
// song step) or the ring is full; nothing is queued then. A launch while the song plays freezes the song first (LIVE).
bool seqSessionLaunchClip(uint8_t track, uint8_t pattern); // read the clip now, switch at the quantise point
bool seqSessionLaunchClipQ(uint8_t track, uint8_t pattern, uint8_t quant); // same, with the quantise of this command (Q_CLIPEND: follow action)
bool seqSessionStopTrack(uint8_t track);                   // silent from the quantise point until a clip is launched
bool seqSessionStopTrackQ(uint8_t track, uint8_t quant);
bool seqSessionStopAll();
bool seqSessionLaunchScene(uint8_t pattern);               // stock pattern switch (pattern end; the song mode goes on), or at once when stopped
uint8_t seqSessionOwnPattern();                            // pattern currently loaded (mtProject.values.actualPattern)
uint8_t seqSessionQueuedScene();                           // queued stock pattern switch, 0 = none
uint8_t seqSessionLastRefusal();                          // seqsession::Refusal of the last refused launch

// Tracks of a pattern that hold content (bit per track). From the project bitmasks the stock Song page uses (no SD access);
// the loaded pattern is read from RAM, so unsaved edits count.
uint16_t seqSessionPatternMask(uint8_t pattern);

// Plan 006: the LIVE freeze / BACK TO SONG of plan 005 C2 are gone. The song loops its slot by default (seqSong.h), so a launch needs
// no freeze: the clips end when the pattern switches (a scene pad, a queued Song page slot, or the slot's play-next flag).

// Panic (transport Stop, plan 005 M6): queued launches / stops are dropped, launched clips end and the tracks play their
// pattern again, LIVE ends. The track data is restored from the pattern files on a later pass when the file manager is busy.
void seqSessionPanic();

void seqSessionActive(bool on);
bool seqSessionTakeUndoCleared();   // true once after a launch step dropped the pattern undo history (the view shows a note)

void seqSessionUpdate(); // once per main loop pass (Tracker::update); also queues the Play 1x follow actions

// ---- saving: launched clips never reach the user's pattern file ----
bool seqSessionHasLaunched();                // some track of the loaded pattern holds a launched clip (RAM != file)
bool seqSessionMergeForSave(void* destPattern); // fills a strPattern with RAM + the launched tracks read from the pattern file
bool seqSessionSaveFailed();                 // launched clips exist and the loaded pattern's changed flag is still set after a save attempt
bool seqSessionOwnsPerformance();            // Session switched performance mode on (the pattern may then be saved normally)
