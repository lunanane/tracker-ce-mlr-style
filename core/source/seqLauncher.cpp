// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
#include "seqLauncher.h"
#include "seqTracks.h"
#include "mtSequencer.h"
#include "mtStructs.h"
#include "fileManager.h"
#include "interruptsControl.h"
#include "patternEditor/patternEditor.h"
#include <stddef.h>
#include <string.h>

extern Sequencer sequencer;
extern strMtProject mtProject;
extern Sequencer::strPattern fileManagerPatternBuffer;

seqsession::Launcher seqLauncher;

namespace
{
seqsession::NoteCache s_cache;
bool s_active = false;   // Session view is on screen: the cache is kept up to date
bool s_ownsPerf = false; // Session switched performance mode on (only then it switches it off again)
bool s_wasPlay = false;

// Main-loop code that touches the audio-side state of the launcher stops the audio callback for a moment.
// (IPSR != 0 means we already run in an interrupt, i.e. the audio callback itself.)
bool inInterrupt()
{
#if defined(__arm__)
    uint32_t ipsr;
    __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
    return ipsr != 0;
#else
    return false;
#endif
}

bool ramTrackHasContent(uint8_t t)
{
    Sequencer::strPattern* p = sequencer.getActualPattern();
    uint8_t len = p->track[0].length;
    if (len > Sequencer::MAXSTEP)
        len = Sequencer::MAXSTEP;
    for (uint8_t s = 0; s <= len; s++)
    {
        const Sequencer::strPattern::strTrack::strStep& st = p->track[t].step[s];
        if (st.note >= Sequencer::STEP_NOTE_MIN || st.fx[0].type != 0 || st.fx[1].type != 0)
            return true;
    }
    return false;
}

// Transport stopped: queued launches apply at once (nothing ticks the audio side).
void flushNow()
{
    DISABLE_AUDIO_INTERRUPTS(); // the launcher and the player state are audio-side data
    seqLauncher.drain();
    for (uint8_t t = 0; t < seqsession::kTracks; t++)
    {
        uint8_t p = 0;
        seqsession::Launcher::Action a = seqLauncher.flush(t, p);
        if (a == seqsession::Launcher::A_CLIP)
            sequencer.seqSessionApplyClip(t, true);
        else if (a == seqsession::Launcher::A_STOP)
            sequencer.seqSessionApplyStop(t);
    }
    ENABLE_AUDIO_INTERRUPTS();
}

// The loaded pattern is written to its file before a launched clip replaces one of its tracks (the merge on save reads the
// file, so unsaved edits of the other tracks would be lost). A pattern that has content but no file yet is written too.
// false: the save did not happen (file manager busy / file unreadable), the launch is refused.
bool saveOwnBeforeLaunch()
{
    uint8_t own = seqSessionOwnPattern();
    if (!newFileManager.isPatternStructChanged(own) && newFileManager.isPatternEmpty(own)) // isPatternEmpty: no file
    {
        for (uint8_t t = 0; t < seqsession::kTracks; t++)
        {
            if (ramTrackHasContent(t))
            {
                newFileManager.setPatternStructChanged(own);
                break;
            }
        }
    }
    if (!newFileManager.isPatternStructChanged(own))
        return true;
    newFileManager.saveWorkspacePatternNow(own);
    return !newFileManager.isPatternStructChanged(own);
}

// Somebody else switched performance mode off while launched clips are mixed into the pattern: put the original tracks
// back from the pattern file (like the stock turn-off, which reloads the pattern).
void restoreLaunched()
{
    if (!newFileManager.isIdleForScratch())
        return; // try again on a later pass
    uint8_t own = seqSessionOwnPattern();
    uint16_t mask = seqLauncher.launchedMask(own);
    int r = newFileManager.readWorkspacePatternTo(own, &fileManagerPatternBuffer);
    if (seqsession::mergeMode(r) == seqsession::MERGE_ABORT)
        return; // read error: never clear the tracks, try again on a later pass
    Sequencer::strPattern* cur = sequencer.getActualPattern();
    DISABLE_AUDIO_INTERRUPTS();
    for (uint8_t t = 0; t < seqsession::kTracks; t++)
    {
        if (!((mask >> t) & 1u))
            continue;
        if (r == 1)
            cur->track[t] = fileManagerPatternBuffer.track[t];
        else
            sequencer.clearSingleTrack(&cur->track[t]);
    }
    seqLauncher.onPatternSwitched();
    ENABLE_AUDIO_INTERRUPTS();
    s_ownsPerf = false;
}

void scanOneRow()
{
    int row = s_cache.nextToScan();
    if (row < 0 || s_cache.first == 0)
        return;
    uint8_t pattern = (uint8_t)(s_cache.first + row);
    uint8_t own = seqSessionOwnPattern();

    bool allRam = false;
    if (pattern == own)
    {
        allRam = true;
        for (uint8_t t = 0; t < seqsession::kTracks; t++)
            if (seqLauncher.current(t) != 0 && seqLauncher.current(t) != pattern)
                allRam = false;
    }

    uint16_t mask = 0;
    if (!allRam && newFileManager.getPatternTracksWithNotes(pattern, mask) < 0)
        return; // a file operation runs: try again on a later pass
    if (pattern == own)
    {
        for (uint8_t t = 0; t < seqsession::kTracks; t++)
        {
            if (seqLauncher.current(t) == 0 || seqLauncher.current(t) == pattern)
            {
                if (ramTrackHasContent(t))
                    mask = (uint16_t)(mask | (1u << t));
                else
                    mask = (uint16_t)(mask & ~(1u << t));
            }
        }
    }
    s_cache.set((uint8_t)row, mask);
}
} // namespace

// ---------------------------------------------------------------- saving (file manager, main loop)
bool seqSessionHasLaunched()
{
    return seqLauncher.launchedMask(seqSessionOwnPattern()) != 0;
}

// dest := the pattern to write: header and the tracks of the loaded pattern from RAM, the tracks that came from launched
// clips from the pattern FILE (cleared when there is none), so a launched clip never ends up in the user's pattern file.
bool seqSessionMergeForSave(void* destPtr)
{
    Sequencer::strPattern* dest = (Sequencer::strPattern*)destPtr;
    Sequencer::strPattern* cur = sequencer.getActualPattern();
    uint8_t own = seqSessionOwnPattern();
    uint16_t mask = seqLauncher.launchedMask(own);

    int rd = newFileManager.readWorkspacePatternTo(own, dest); // dest = file (or cleared)
    seqsession::MergeMode mode = seqsession::mergeMode(rd);
    if (mode == seqsession::MERGE_ABORT)
        return false; // the file exists but could not be read: never overwrite it, the save is aborted
    bool fromFile = mode == seqsession::MERGE_FROM_FILE;
    if (fromFile)
    {
        memcpy(dest, cur, offsetof(Sequencer::strPattern, track));
        for (uint8_t t = 0; t < seqsession::kTracks; t++)
            if (seqsession::saveSource(mask, t, true) == seqsession::SRC_RAM)
                dest->track[t] = cur->track[t];
    }
    else
    {
        memcpy(dest, cur, sizeof(Sequencer::strPattern));
        for (uint8_t t = 0; t < seqsession::kTracks; t++)
        {
            if (seqsession::saveSource(mask, t, false) == seqsession::SRC_CLEAR)
            {
                dest->track[t].length = cur->track[0].length;
                sequencer.clearSingleTrack(&dest->track[t]);
            }
        }
    }
    return true;
}

bool seqSessionSaveFailed()
{
    return seqSessionHasLaunched() && newFileManager.isPatternStructChanged(seqSessionOwnPattern());
}

bool seqSessionOwnsPerformance()
{
    return s_ownsPerf;
}

// ---------------------------------------------------------------- Sequencer members (audio context)
void Sequencer::seqSessionStep(uint8_t row)
{
    if (row >= seqsession::kTracks)
        return;
    if (row == 0)
        seqLauncher.advance((uint16_t)player.globalPos, (uint16_t)(currentPattern->track[0].length + 1));
    seqLauncher.drain();
    uint8_t pattern = 0;
    switch (seqLauncher.onStep(row, seqLauncher.position(), pattern))
    {
        case seqsession::Launcher::A_CLIP: seqSessionApplyClip(row, seqLauncher.quant() != seqsession::Q_NOW); break;
        case seqsession::Launcher::A_STOP: seqSessionApplyStop(row); break;
        default: break;
    }
}

// The clip that was read into the performance buffer replaces this track of the playing pattern.
// restart (bar / pattern end): the clip starts at its first step, which is on the bar. Immediate: it joins in phase with
// the pattern position (the position is the new step the track is entering).
void Sequencer::seqSessionApplyClip(uint8_t track, bool restart)
{
    seqTakeoverBegin(track);          // the old clip's sounding note ends (sample and MIDI tracks)
    switchPerformanceTrackNow(track); // copies the track, takes its length, realigns when the playhead is beyond it
    player.track[track].performanceSourcePattern = -1;
    cancelFxes(track);
    if (player.track[track].performancePlayMode == PLAYMODE_FORWARD)
    {
        uint16_t ph = 0;
        if (!restart)
            ph = (uint16_t)(seqLauncher.position() % (uint16_t)(player.performance.trackLength[track] + 1));
        player.track[track].actual_pos = (int16_t)(MINSTEP + ph);
        player.track[track].actual_pos_absolute = ph;
    }
    // CUT layer: a cut track's playhead follows the clip (restart at the position the clip enters, stock position too)
    if (seqcut::Cut* c = seqEngine.cut())
        c->launched(track, seqCutSteps(track), player.track[track].actual_pos);
}

void Sequencer::seqSessionApplyStop(uint8_t track)
{
    seqTakeoverBegin(track); // closes the sounding note, clears roll / arp / micromove
}

// The whole pattern changed (scene, song, Pattern page): clips are over, every track follows the pattern again.
// Called from the audio callback (pattern end) and from the main loop (Pattern page switch): the main loop part runs with
// the audio callback stopped.
void Sequencer::seqSessionPatternSwitched()
{
    bool irq = inInterrupt();
    if (!irq)
        DISABLE_AUDIO_INTERRUPTS();
    seqLauncher.onPatternSwitched();
    if (s_ownsPerf)
    {
        s_ownsPerf = false;
        exitPerformanceMode(); // performance mode only carried the per-track clip lengths
    }
    if (!irq)
        ENABLE_AUDIO_INTERRUPTS();
}

// The per-track lengths are filled before the flag is set, so the audio side never sees the flag with stale lengths.
void Sequencer::seqSessionEnterPerformance()
{
    if (player.performanceMode)
        return; // somebody else (Performance screen) has it on: it stays theirs
    for (uint8_t a = 0; a < kAllTracks; a++)
        player.performance.trackLength[a] = getActualPattern()->track[0].length;
    __asm__ __volatile__("" ::: "memory");
    player.performanceMode = 1;
    s_ownsPerf = true;
}

// ---------------------------------------------------------------- main loop
bool seqSessionLaunchClip(uint8_t track, uint8_t pattern)
{
    if (track >= seqsession::kTracks || pattern < 1)
        return false;
    if (seqLauncher.current(track) == 0 && pattern == seqSessionOwnPattern() && !seqLauncher.stopped(track))
    {
        // the track already plays its own pattern: the newest tap wins, so a queued clip / stop is dropped
        bool ok = seqLauncher.postCancel(track);
        if (ok && !sequencer.isPlay())
            flushNow();
        return ok;
    }
    if (!newFileManager.isIdleForScratch())
        return false;
    if (seqLauncher.current(track) == 0 && !saveOwnBeforeLaunch())
        return false; // "Busy": the pattern's unsaved edits are not on the card yet

    sequencer.seqSessionEnterPerformance();
    seqLauncher.beginLoad(track);
    bool ok = newFileManager.loadClipTrackToBuffer(pattern, track);
    if (ok)
        ok = seqLauncher.postClip(track, pattern);
    seqLauncher.endLoad(track);
    if (ok && !sequencer.isPlay())
        flushNow();
    return ok;
}

bool seqSessionStopTrack(uint8_t track)
{
    if (!seqLauncher.postStop(track))
        return false;
    if (!sequencer.isPlay())
        flushNow();
    return true;
}

bool seqSessionStopAll()
{
    if (!seqLauncher.postStopAll())
        return false;
    if (!sequencer.isPlay())
        flushNow();
    return true;
}

uint8_t seqSessionOwnPattern()
{
    return (uint8_t)mtProject.values.actualPattern;
}

uint8_t seqSessionQueuedScene()
{
    uint16_t n = sequencer.sequencialSwitch_GetNext();
    return n > 255 ? 255 : (uint8_t)n;
}

bool seqSessionLaunchScene(uint8_t pattern)
{
    if (pattern < 1)
        return false;
    if (sequencer.isPlay())
    {
        // stock pattern switch: queued, happens at the end of the pattern (like Pattern + left / right)
        if (newFileManager.isPatternLoading())
            return false;
        uint16_t base = sequencer.sequencialSwitch_GetNext();
        if (base == 0)
            base = (uint16_t)mtProject.values.actualPattern;
        int delta = (int)pattern - (int)base;
        do // at least once, also for delta 0: it is what sets the next pattern (the own pattern = all tracks back)
        {
            int step = delta > 100 ? 100 : (delta < -100 ? -100 : delta); // the stock call takes an int8_t
            sequencer.sequencialSwitch_changeNextPattern((int8_t)step);
            delta -= step;
        } while (delta != 0);
        sequencer.sequencialSwitch_SetReady();
        return true;
    }
    // transport stopped: switch at once, like the Pattern page does
    sequencer.sequencialSwitch_Reset();
    sequencer.setOnGlobalStep(nullCallback);
    newFileManager.saveWorkspacePatternNow(mtProject.values.actualPattern);
    if (seqSessionSaveFailed())
        return false; // launched clips and the file manager busy: switching would lose the edits
    mtProject.values.actualPattern = pattern;
    patternEditor.changeActualPattern(0); // loads and swaps the pattern (switchRamPatternsNow -> seqSessionPatternSwitched)
    return true;
}

void seqSessionSetWindow(uint8_t first)
{
    s_cache.invalidate(first);
}

const seqsession::NoteCache& seqSessionCache()
{
    return s_cache;
}

void seqSessionActive(bool on)
{
    s_active = on;
    if (on)
        s_cache.invalidate(s_cache.first ? s_cache.first : 1); // rescan on entering
}

void seqSessionUpdate()
{
    bool play = sequencer.isPlay();
    if (s_wasPlay && !play)
    {
        // transport stopped: launches still queued take effect now, stopped tracks play again
        DISABLE_AUDIO_INTERRUPTS();
        seqLauncher.drain();
        for (uint8_t t = 0; t < seqsession::kTracks; t++)
        {
            if (seqLauncher.queuedKind(t) == seqsession::Launcher::K_CLIP)
            {
                uint8_t p = 0;
                if (seqLauncher.flush(t, p) == seqsession::Launcher::A_CLIP)
                    sequencer.seqSessionApplyClip(t, true);
            }
        }
        seqLauncher.onTransportStop();
        ENABLE_AUDIO_INTERRUPTS();
    }
    s_wasPlay = play;

    // the last launched clip is gone (scene, Pattern page, restore): undo steps taken meanwhile hold clip content
    static uint16_t s_lastMask = 0;
    uint16_t mask = seqLauncher.launchedMask(seqSessionOwnPattern());
    if (seqsession::clearUndoOnMask(s_lastMask, mask))
        newFileManager.clearPatternUndoHistory();
    s_lastMask = mask;

    // performance mode went off (Performance screen / fader page) while launched clips are mixed into the pattern
    if (!sequencer.isPerformanceMode() && seqSessionHasLaunched())
        restoreLaunched();

    if (s_active)
        scanOneRow();
}
