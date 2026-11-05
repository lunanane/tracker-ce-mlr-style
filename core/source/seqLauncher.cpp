// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
#include "seqLauncher.h"
#include "seqClips.h"
#include "seqTracks.h"
#include "seqSong.h"
#include "mtSequencer.h"
#include "mtStructs.h"
#include "fileManager.h"
#include "interruptsControl.h"
#include "patternEditor/patternEditor.h"
#ifdef FEATURE_PLINKY
#include "seqPlinky.h"
#endif
#include <cr_section_macros.h>
#include <stddef.h>
#include <string.h>

extern Sequencer sequencer;
extern strMtProject mtProject;
extern Sequencer::strPattern fileManagerPatternBuffer;
extern Sequencer::strPattern fileManagerPerformancePatternBuffer;

seqsession::Launcher seqLauncher;

namespace
{
bool s_active = false;   // Session view is on screen
bool s_ownsPerf = false; // Session switched performance mode on (only then it switches it off again)
bool s_wasPlay = false;
seqsession::FollowArms s_arms;      // Play 1x follow actions queued for the clip instance that plays now
bool s_panicRestore = false;        // panic: the launched tracks still have to be put back
bool s_undoCleared = false;         // the pattern undo history was dropped by the last launched clip going away
uint8_t s_refusal = seqsession::REFUSE_NONE; // why the last launch was refused

// Plan 006: no LIVE freeze any more. The song loops its slot by default (seqSong.h), so a launch or a scene never has to stop the song
// from running away; the clips simply end when the pattern switches.

// Play 1x follow actions: at most one attempt per step, hard read errors count towards giving up
uint16_t s_lastTry = 0xFFFF;
uint8_t s_hardFails[seqsession::kTracks];

// Restoring launched tracks from the pattern file: at most every 200 ms, given up after 25 tries
uint32_t s_restoreNextMs = 0;
uint8_t s_restoreFails = 0;
bool s_restoreGaveUp = false;

// ---- own data of the tracks the clips replace (plan 005 M1: no SD write while playing) ----
// Taken at the launch (a memcpy of about 1 KB per track). The merge for saving and the restore read it instead of the pattern
// file, so a launch needs no write. The Tracker Plus / Mini have no room for it (EXTERNAL_RAM is full): there a launch while
// the transport plays is refused when the loaded pattern has unsaved edits (stop, save, launch), and the file is the source.
#ifndef PLATFORM_IMX
__NOINIT(EXTERNAL_RAM) Sequencer::strPattern::strTrack s_snap[seqsession::kTracks];
constexpr bool kHaveSnapshots = true;
#else
constexpr bool kHaveSnapshots = false;
#endif
uint16_t s_snapValid = 0; // bit per track: s_snap[t] holds the own data of a track a launched clip replaces

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

// Content of the tracks of the loaded pattern in RAM, scanned at most every 30 ms (the LEDs and the screen ask many times).
uint16_t ramContentMask(uint8_t own)
{
    static uint32_t s_at = 0;
    static uint8_t s_own = 0;
    static uint16_t s_mask = 0;
    static bool s_have = false;
    uint32_t now = seqNowMs();
    if (!s_have || own != s_own || (uint32_t)(now - s_at) >= 30)
    {
        uint16_t m = 0;
        for (uint8_t t = 0; t < seqsession::kTracks; t++)
            if (ramTrackHasContent(t))
                m = (uint16_t)(m | (1u << t));
        s_mask = m;
        s_own = own;
        s_at = now;
        s_have = true;
    }
    return s_mask;
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

// Without snapshots (Plus / Mini): the loaded pattern is written to its file before a launched clip replaces one of its tracks
// (the merge on save reads the file, so unsaved edits of the other tracks would be lost). A pattern that has content but no
// file yet is written too. Only with the transport stopped. false: the save did not happen, the launch is refused.
bool ownPatternDirty()
{
    uint8_t own = seqSessionOwnPattern();
    if (newFileManager.isPatternStructChanged(own))
        return true;
    if (newFileManager.isPatternEmpty(own)) // no file yet
        return ramContentMask(own) != 0;
    return false;
}

bool saveOwnBeforeLaunch()
{
    uint8_t own = seqSessionOwnPattern();
    if (!ownPatternDirty())
        return true;
    newFileManager.setPatternStructChanged(own);
    newFileManager.saveWorkspacePatternNow(own);
    return !newFileManager.isPatternStructChanged(own);
}

// Copy the own data of track t away before a clip replaces it (RAM only). false: no room on this platform.
bool snapshotTrack(uint8_t t)
{
#ifndef PLATFORM_IMX
    s_snap[t] = sequencer.getActualPattern()->track[t];
    return true;
#else
    (void)t;
    return false;
#endif
}

// Put the original tracks back (a clip replaced them in RAM): from the snapshot, else from the pattern file (cleared when there
// is none). Panic (leavePerf): the launcher also leaves performance mode itself (the perform FX keep it when they own it).
// true: done; false: the file manager is busy / the file is unreadable, try again on a later pass.
bool restoreLaunched(bool leavePerf = false)
{
    uint8_t own = seqSessionOwnPattern();
    uint16_t mask = seqLauncher.launchedMask(own);
    uint16_t fromFile = (uint16_t)(mask & ~s_snapValid);
    int r = 1;
    if (fromFile)
    {
        if (!newFileManager.isIdleForScratch())
            return false; // try again on a later pass
        r = newFileManager.readWorkspacePatternTo(own, &fileManagerPatternBuffer);
        if (seqsession::mergeMode(r) == seqsession::MERGE_ABORT)
            return false; // read error: never clear the tracks, try again on a later pass
    }
    Sequencer::strPattern* cur = sequencer.getActualPattern();
    DISABLE_AUDIO_INTERRUPTS();
    for (uint8_t t = 0; t < seqsession::kTracks; t++)
    {
        if (!((mask >> t) & 1u))
            continue;
#ifndef PLATFORM_IMX
        if ((s_snapValid >> t) & 1u)
        {
            cur->track[t] = s_snap[t];
            continue;
        }
#endif
        if (r == 1)
            cur->track[t] = fileManagerPatternBuffer.track[t];
        else
            sequencer.clearSingleTrack(&cur->track[t]);
    }
    s_snapValid = 0;
    seqLauncher.onPatternSwitched();
    ENABLE_AUDIO_INTERRUPTS();
    if (leavePerf)
        sequencer.seqSessionPatternSwitched(); // releases the launcher's claim; performance mode goes off unless the perform FX own it
    s_ownsPerf = false;
    Sequencer::seqPerfOwners.release(seqlogic::PERF_OWNER_LAUNCHER); // the clips are gone; the flag itself was handled by the one who switched it off
    return true;
}

// Throttled restore for the main loop: every 200 ms, and given up after 25 tries (a file that cannot be read must not keep the
// card busy for ever). true: done.
bool restoreThrottled(bool leavePerf)
{
    uint32_t now = seqNowMs();
    if ((int32_t)(now - s_restoreNextMs) < 0)
        return false;
    if (restoreLaunched(leavePerf))
    {
        s_restoreFails = 0;
        return true;
    }
    s_restoreNextMs = now + 200;
    if (++s_restoreFails >= 25)
        s_restoreGaveUp = true;
    return false;
}

enum LaunchResult : uint8_t { LR_OK = 0, LR_BUSY, LR_ERROR };

// Launch the clips of `pattern` on every track of `tracks` (one file read for all of them). Checks and the load come first; only
// when they are done the song is frozen, performance mode claimed and the commands posted (plan 005: no write or freeze before a
// refused launch).
LaunchResult launchClips(uint16_t tracks, uint8_t pattern, uint8_t quant)
{
    s_refusal = seqsession::REFUSE_BUSY;
    uint8_t own = seqSessionOwnPattern();
    uint8_t n = 0;
    for (uint8_t t = 0; t < seqsession::kTracks; t++)
        if ((tracks >> t) & 1u)
            n++;
    if (n == 0 || seqLauncher.ringFree() < n)
        return LR_BUSY;
    if (!newFileManager.isIdleForScratch())
        return LR_BUSY;

    // the own data of the tracks that still play the loaded pattern
    uint16_t needSnap = 0;
    for (uint8_t t = 0; t < seqsession::kTracks; t++)
        if (((tracks >> t) & 1u) && seqLauncher.current(t) == 0)
            needSnap = (uint16_t)(needSnap | (1u << t));
    uint16_t snapped = 0;
    if (needSnap)
    {
        if (kHaveSnapshots)
        {
            for (uint8_t t = 0; t < seqsession::kTracks; t++)
                if (((needSnap >> t) & 1u) && snapshotTrack(t))
                    snapped = (uint16_t)(snapped | (1u << t));
        }
        else if (ownPatternDirty())
        {
            if (sequencer.isPlay())
            {
                s_refusal = seqsession::REFUSE_STOP_TO_SAVE; // no write while playing
                return LR_BUSY;
            }
            if (!saveOwnBeforeLaunch())
                return LR_BUSY;
        }
    }

    // load: the own pattern's clip from the snapshot when there is one, the rest in one read of the pattern file
    uint16_t fromFile = tracks;
    uint16_t have = (uint16_t)(s_snapValid | snapped);
    for (uint8_t t = 0; t < seqsession::kTracks; t++)
    {
        if (!((tracks >> t) & 1u) || pattern != own || !((have >> t) & 1u))
            continue;
#ifndef PLATFORM_IMX
        fileManagerPerformancePatternBuffer.track[t] = s_snap[t];
#endif
        fromFile = (uint16_t)(fromFile & ~(1u << t));
    }
    for (uint8_t t = 0; t < seqsession::kTracks; t++)
        if ((tracks >> t) & 1u)
            seqLauncher.beginLoad(t);
    bool ok = fromFile == 0 || newFileManager.loadClipTracksToBuffer(pattern, fromFile);
    if (!ok)
    {
        for (uint8_t t = 0; t < seqsession::kTracks; t++)
            if ((tracks >> t) & 1u)
                seqLauncher.endLoad(t);
        return LR_ERROR;
    }

    sequencer.seqSessionEnterPerformance();
    for (uint8_t t = 0; t < seqsession::kTracks; t++)
    {
        if (!((tracks >> t) & 1u))
            continue;
        ok = seqLauncher.postClip(t, pattern, quant) && ok; // room was checked above
        seqLauncher.endLoad(t);
    }
    s_snapValid = (uint16_t)(s_snapValid | snapped);
    if (!sequencer.isPlay())
        flushNow();
#ifdef FEATURE_PLINKY
    if (ok)
        for (uint8_t t = 0; t < seqsession::kTracks; t++)
            if ((tracks >> t) & 1u)
                seqPlinkyClipLaunched(t, pattern); // plan 012: a PolySynth track morphs to the sound of the clip's source pattern
#endif
    s_refusal = seqsession::REFUSE_NONE;
    return ok ? LR_OK : LR_BUSY;
}

// Plan 005 R2 follow action (Play 1x): a track whose playing clip is set to Play 1x gets the next row down queued with the
// quantise "clip end" as soon as that clip instance plays: the load has the whole clip length of time, the audio side applies it
// at the step where the clip would restart. The next row empty (or none left) = the track stops. The tracks that follow into the
// same row share one read of the pattern file.
//  - never while a command is still in the ring (a tap of the performer is not drained yet and would be overwritten),
//    while a scene is queued, or around the last step of the pattern;
//  - at most one attempt per step; a hard read error three times in a row stops the track instead of retrying for ever.
void queueFollowActions()
{
    if (!sequencer.isPlay() || (sequencer.isSongMode() && !seqSongSlotStays()) || s_panicRestore)
        return; // the song advances by itself (a slot flagged "play next"; a looping slot is like a pattern loop)
    if (seqLauncher.ringPending() || seqSessionQueuedScene() != 0)
        return;
    if (sequencer.isLastPatternStep() || newFileManager.isPatternLoading())
        return;
    uint16_t tick = seqLauncher.ticks();
    if (tick == s_lastTry)
        return;
    s_lastTry = tick;

    uint8_t own = seqSessionOwnPattern();
    uint16_t stopMask = 0, launchMask = 0;
    uint8_t launchPattern = 0;
    uint8_t cps[seqsession::kTracks];
    for (uint8_t t = 0; t < seqsession::kTracks; t++)
    {
        cps[t] = 0;
        if (!seqsession::followMayQueue(seqLauncher, t))
            continue;
        uint8_t cp = seqLauncher.current(t) ? seqLauncher.current(t) : own;
        if (seqClipMode(cp, t) != seqclips::CLIP_PLAY1X)
            continue;
        if (!s_arms.needs(t, cp, seqLauncher.applied(t), seqLauncher.epoch()))
            continue;
        cps[t] = cp;
        bool nextHas = cp < seqsession::kPatternMax && ((seqSessionPatternMask((uint8_t)(cp + 1)) >> t) & 1u);
        seqsession::FollowTarget f = seqsession::followTarget(cp, nextHas);
        if (!f.launch)
            stopMask = (uint16_t)(stopMask | (1u << t));
        else if (launchPattern == 0 || launchPattern == f.pattern)
        {
            launchPattern = f.pattern;
            launchMask = (uint16_t)(launchMask | (1u << t));
        } // another target row: next step
    }
    for (uint8_t t = 0; t < seqsession::kTracks; t++)
    {
        if (!((stopMask >> t) & 1u) || seqLauncher.ringFree() < 1)
            continue;
        if (seqLauncher.postStop(t, seqsession::Q_CLIPEND))
            s_arms.mark(t, cps[t], seqLauncher.applied(t), seqLauncher.epoch());
    }
    if (launchMask)
    {
        LaunchResult r = launchClips(launchMask, launchPattern, seqsession::Q_CLIPEND);
        for (uint8_t t = 0; t < seqsession::kTracks; t++)
        {
            if (!((launchMask >> t) & 1u))
                continue;
            if (r == LR_OK)
            {
                s_hardFails[t] = 0;
                s_arms.mark(t, cps[t], seqLauncher.applied(t), seqLauncher.epoch());
            }
            else if (r == LR_ERROR && ++s_hardFails[t] >= 3)
            {
                // the next clip cannot be read: stop the track at the clip end rather than retrying for ever
                s_hardFails[t] = 0;
                if (seqLauncher.postStop(t, seqsession::Q_CLIPEND))
                    s_arms.mark(t, cps[t], seqLauncher.applied(t), seqLauncher.epoch());
            }
        }
    }
}
} // namespace

// ---------------------------------------------------------------- saving (file manager, main loop)
bool seqSessionHasLaunched()
{
    return seqLauncher.launchedMask(seqSessionOwnPattern()) != 0;
}

// v0.3.1 M2: tracks the performance mode swapped in from other patterns (per-track performance patterns)
static uint16_t perfSwapped()
{
    return seqsession::perfSwapMask(mtProject.perfTracksPatterns, mtProject.values.actualPattern, sequencer.isPerformanceMode(),
                                    kPerformanceTracks);
}

bool seqSessionSaveNeedsMerge()
{
    return seqSessionHasLaunched() || perfSwapped() != 0;
}

// dest := the pattern to write: header and the tracks of the loaded pattern from RAM, the tracks that came from launched
// clips from their snapshot (the own data taken at the launch) or else from the pattern FILE (cleared when there is none), so a
// launched clip never ends up in the user's pattern file.
bool seqSessionMergeForSave(void* destPtr)
{
    Sequencer::strPattern* dest = (Sequencer::strPattern*)destPtr;
    Sequencer::strPattern* cur = sequencer.getActualPattern();
    uint8_t own = seqSessionOwnPattern();
    uint16_t launched = seqLauncher.launchedMask(own);
    uint16_t swapped = perfSwapped();
    uint16_t mask = (uint16_t)(launched | swapped); // tracks whose RAM data is not the pattern's own

    if ((launched & ~s_snapValid) == 0 && swapped == 0)
    {
        memcpy(dest, cur, sizeof(Sequencer::strPattern)); // every launched track has its own data in RAM: no file needed
    }
    else
    {
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
    }
#ifndef PLATFORM_IMX
    for (uint8_t t = 0; t < seqsession::kTracks; t++)
        if (((launched >> t) & 1u) && ((s_snapValid >> t) & 1u))
            dest->track[t] = s_snap[t];
#endif
    return true;
}

bool seqSessionSaveFailed()
{
    return seqSessionSaveNeedsMerge() && newFileManager.isPatternStructChanged(seqSessionOwnPattern());
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
    // steps the clip on this track runs (what the stock step switch loops over): Q_CLIPEND waits for it
    uint8_t clipLast = currentPattern->track[0].length;
    if (player.performanceMode && isPerformanceTrack(row))
        clipLast = player.performance.trackLength[row];
    if (player.performance.patternLength > -1)
        clipLast = (uint8_t)player.performance.patternLength;
    seqsession::Quant waitedFor = seqLauncher.queuedQuant(row); // the command's own quantise (cleared when it applies)
    switch (seqLauncher.onStep(row, seqLauncher.position(), pattern, (uint16_t)(clipLast + 1)))
    {
        case seqsession::Launcher::A_CLIP: seqSessionApplyClip(row, waitedFor != seqsession::Q_NOW); break; // a follow action restarts at the clip start
        case seqsession::Launcher::A_STOP: seqSessionApplyStop(row); break;
        default: break;
    }
}

// The clip that was read into the performance buffer replaces this track of the playing pattern.
// restart (bar / pattern end): the clip starts at its first step, which is on the bar. Immediate: it joins in phase with
// the pattern position (the position is the new step the track is entering), and its clip-end counter starts there too.
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
        seqLauncher.setPhase(track, ph); // Play 1x: the clip end coincides with the clip's own wrap
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
    s_snapValid = 0; // the clips are gone, the saved own data is of no use any more
    if (s_ownsPerf)
    {
        s_ownsPerf = false;
        // performance mode only carried the per-track clip lengths; it stays on while the perform FX still own it
        releasePerformanceMode(seqlogic::PERF_OWNER_LAUNCHER);
        if (player.performanceMode)
        {
            // the perform FX keep it: the tracks follow the new pattern, so their lengths must be its length (as the song path)
            for (uint8_t a = 0; a < kPerformanceTracks; a++)
                player.performance.trackLength[a] = getActualPattern()->track[0].length;
        }
    }
    if (!irq)
        ENABLE_AUDIO_INTERRUPTS();
}

// The per-track lengths are filled before the flag is set, so the audio side never sees the flag with stale lengths.
void Sequencer::seqSessionEnterPerformance()
{
    if (player.performanceMode)
    {
        // the perform FX have it on: the clips claim it too, so leaving the Performance page does not end them (C1). The
        // per-track lengths are those of the pattern already.
        seqPerfOwners.claim(seqlogic::PERF_OWNER_LAUNCHER);
        s_ownsPerf = true;
        return;
    }
    seqPerfOwners.claim(seqlogic::PERF_OWNER_LAUNCHER);
    for (uint8_t a = 0; a < kAllTracks; a++)
        player.performance.trackLength[a] = getActualPattern()->track[0].length;
    __asm__ __volatile__("" ::: "memory");
    player.performanceMode = 1;
    s_ownsPerf = true;
}

// ---------------------------------------------------------------- main loop
bool seqSessionLaunchClip(uint8_t track, uint8_t pattern)
{
    return seqSessionLaunchClipQ(track, pattern, seqsession::kQuantGlobal);
}

bool seqSessionLaunchClipQ(uint8_t track, uint8_t pattern, uint8_t quant)
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
    s_restoreGaveUp = false;
    s_restoreFails = 0;
    return launchClips((uint16_t)(1u << track), pattern, quant) == LR_OK;
}

bool seqSessionStopTrack(uint8_t track)
{
    return seqSessionStopTrackQ(track, seqsession::kQuantGlobal);
}

bool seqSessionStopTrackQ(uint8_t track, uint8_t quant)
{
    if (!seqLauncher.postStop(track, quant))
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

uint16_t seqSessionPatternMask(uint8_t pattern)
{
    if (pattern < 1)
        return 0;
    uint16_t m = (uint16_t)(mtProject.values.allPatternsBitmask[pattern - 1] | (mtProject.values.allPatternsBitmaskMidi[pattern - 1] << 8));
    if (pattern == seqSessionOwnPattern())
    {
        // the loaded pattern: RAM is newer than the bitmask (unsaved edits); tracks that play a launched clip keep the file's view
        uint16_t ram = ramContentMask(pattern);
        for (uint8_t t = 0; t < seqsession::kTracks; t++)
        {
            if (seqLauncher.current(t) == 0 || seqLauncher.current(t) == pattern)
            {
                if ((ram >> t) & 1u)
                    m = (uint16_t)(m | (1u << t));
                else
                    m = (uint16_t)(m & ~(1u << t));
            }
        }
    }
    return m;
}

uint8_t seqSessionLastRefusal()
{
    return s_refusal;
}

void seqSessionPanic()
{
    DISABLE_AUDIO_INTERRUPTS(); // the launcher and the player state are audio-side data
    seqLauncher.panic();
    ENABLE_AUDIO_INTERRUPTS();
    seqSongPanic();
    s_arms.reset();
    memset(s_hardFails, 0, sizeof(s_hardFails));
    s_restoreGaveUp = false;
    s_restoreFails = 0;
    s_restoreNextMs = 0;
    s_panicRestore = true; // the launched tracks are put back in seqSessionUpdate (immediately on the next pass)
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
    return seqSessionLaunchSceneQ(pattern, seqsession::Q_PATTERN);
}

void seqSessionSceneDone()
{
    if (seqLauncher.scenePending() && !sequencer.sequencialSwitch_IsArmed())
        sequencer.sequencialSwitch_Reset(); // overtaken by another switch (song advance): no stale "queued scene"
    seqLauncher.cancelScene();
}

bool seqSessionLaunchSceneQ(uint8_t pattern, uint8_t quant)
{
    if (pattern < 1)
        return false;
    seqsession::Quant q = quant == seqsession::kQuantGlobal ? seqLauncher.quant() : (seqsession::Quant)quant;
    if (sequencer.isRec())
        q = seqsession::Q_PATTERN; // recording writes the pattern: no early swap, the stock switch at the pattern end
    if (sequencer.isPlay() && q != seqsession::Q_PATTERN && q != seqsession::Q_CLIPEND)
    {
        // bar / now: read the pattern into the next-pattern buffer now (asynchronous, the main loop never blocks), the audio side swaps
        // it in at the quantise boundary once the read is done (seqSessionSceneBoundary)
        if (!newFileManager.isIdleForScratch() || sequencer.sequencialSwitch_IsLoaded() || sequencer.isLastPatternStep() ||
            (uint16_t)(seqLauncher.globalPosition() + 2) >= seqLauncher.globalLength())
            return false; // a read is running, or the stock switch is about to happen (the last two steps belong to it)
        if (seqLauncher.scenePending() && seqLauncher.scenePattern() == pattern && seqLauncher.sceneQuant() == q)
            return true;
        seqLauncher.setSceneReady(false); // an earlier scene must not swap while the buffer is read again
        if (!newFileManager.loadWorkspacePattern(pattern))
            return false;
        if (sequencer.globalStepIsSetReady())
            sequencer.setOnGlobalStep(nullCallback); // an earlier pattern-end scene was waiting to arm
        DISABLE_AUDIO_INTERRUPTS();
        seqLauncher.cancelScene();
        sequencer.sequencialSwitch_Reset();
        sequencer.sequencialSwitch_SetPattern(pattern);
        seqLauncher.beginScene(pattern, q);
        ENABLE_AUDIO_INTERRUPTS();
        return true;
    }
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

void seqSessionActive(bool on)
{
    s_active = on;
    if (on)
        s_undoCleared = false; // a note from long ago must not appear on entering
}

bool seqSessionTakeUndoCleared()
{
    bool v = s_undoCleared;
    s_undoCleared = false;
    return v;
}

// A quantised scene (plan 007 B2): tell the audio side when the next-pattern buffer holds the scene's pattern.
// Edits made while the scene waits would be lost at the swap (the stock switch saves nothing): the change flag of the playing pattern
// makes the read start again, and that read saves the pattern first - the same save the stock pattern switch does while playing (the
// "no SD writes while playing" rule of plan 005/006 is about the mod's own sidecar and perform data, not this stock save). Recording
// turns the scene into the stock pattern-end switch. The last two steps are left to the stock switch (no new read there).
static void sceneHousekeeping()
{
    if (!seqLauncher.scenePending())
        return;
    const uint8_t pattern = seqLauncher.scenePattern();
    if (!sequencer.isPlay() || sequencer.sequencialSwitch_GetNext() != pattern)
    {
        // Stop (panic), or the Pattern / Song page took the switch over
        if (!sequencer.isPlay())
            sequencer.sequencialSwitch_Reset();
        seqLauncher.cancelScene();
        return;
    }
    if (newFileManager.getStatus() >= fmError)
    {
        // the read failed: never count it as ready; the scene is given up
        sequencer.sequencialSwitch_Reset();
        seqLauncher.cancelScene();
        return;
    }
    if (sequencer.isRec())
    {
        seqLauncher.cancelScene(); // pattern-end scene: the stock switch takes it from here
        sequencer.sequencialSwitch_SetReady();
        return;
    }
    const bool idle = newFileManager.isIdleForScratch();
    const bool nearEnd = (uint16_t)(seqLauncher.globalPosition() + 2) >= seqLauncher.globalLength();
    const bool loaded = idle && newFileManager.getLoadedPatternIndex() == pattern;
    const bool edited = newFileManager.isPatternStructChanged(seqSessionOwnPattern());
    static uint32_t s_nextRead = 0;
    if (idle && !nearEnd && !sequencer.sequencialSwitch_IsArmed() && (!loaded || edited) && (int32_t)(seqNowMs() - s_nextRead) >= 0)
    {
        seqLauncher.setSceneReady(false);
        if (newFileManager.loadWorkspacePattern(pattern)) // saves the edited pattern first, then reads
            s_nextRead = seqNowMs() + 300;
        return;
    }
    seqLauncher.setSceneReady(loaded && !edited);
}

void seqSessionUpdate()
{
    sceneHousekeeping();
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
    {
        newFileManager.clearPatternUndoHistory();
        s_undoCleared = true; // the Session view says so (plan 005 m11)
    }
    s_lastMask = mask;

    // panic: put the launched tracks back (from the snapshot at once; from the file when the file manager is free)
    if (s_panicRestore)
    {
        if (!seqSessionHasLaunched())
        {
            sequencer.seqSessionPatternSwitched(); // nothing to restore: just end the launcher state and its performance-mode claim
            s_panicRestore = false;
        }
        else if (restoreThrottled(true) || s_restoreGaveUp)
            s_panicRestore = false;
        return;
    }

    // performance mode went off (Performance screen / fader page) while launched clips are mixed into the pattern
    if (!sequencer.isPerformanceMode() && seqSessionHasLaunched() && !s_restoreGaveUp)
        restoreThrottled(false);

    queueFollowActions();
}
