// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
#include "seqLauncher.h"
#include "mtSequencer.h"
#include "mtStructs.h"
#include "fileManager.h"
#include "patternEditor/patternEditor.h"

extern Sequencer sequencer;
extern strMtProject mtProject;

seqsession::Launcher seqLauncher;

namespace
{
seqsession::NoteCache s_cache;
bool s_active = false;          // Session view is on screen: the cache is kept up to date
volatile bool s_used = false;   // a clip was applied since the last pattern switch (we switched performance mode on)
bool s_wasPlay = false;

// The pattern buffer (Sequencer::bufferPattern) is also the target of the next-pattern load: never touch it while a
// pattern switch is queued or loading.
bool bufferBusy()
{
    return newFileManager.isPatternLoading() || sequencer.sequencialSwitch_GetNext() > 0 ||
           (sequencer.isSongMode() && sequencer.isLastPatternStep());
}

bool ramTrackHasNotes(uint8_t t)
{
    Sequencer::strPattern* p = sequencer.getActualPattern();
    uint8_t len = p->track[0].length;
    if (len > Sequencer::MAXSTEP)
        len = Sequencer::MAXSTEP;
    for (uint8_t s = 0; s <= len; s++)
        if (p->track[t].step[s].note >= Sequencer::STEP_NOTE_MIN)
            return true;
    return false;
}

// Transport stopped: queued launches apply at once (nothing ticks the audio side).
void flushNow()
{
    seqLauncher.drain();
    for (uint8_t t = 0; t < seqsession::kTracks; t++)
    {
        uint8_t p = 0;
        seqsession::Launcher::Action a = seqLauncher.flush(t, p);
        if (a == seqsession::Launcher::A_CLIP)
            sequencer.seqSessionApplyClip(t, false);
        else if (a == seqsession::Launcher::A_STOP)
            sequencer.seqSessionApplyStop(t);
    }
}

void scanOneRow()
{
    int row = s_cache.nextToScan();
    if (row < 0 || s_cache.first == 0)
        return;
    uint8_t pattern = (uint8_t)(s_cache.first + row);
    uint8_t own = (uint8_t)mtProject.values.actualPattern;

    bool allRam = false;
    if (pattern == own)
    {
        allRam = true;
        for (uint8_t t = 0; t < seqsession::kTracks; t++)
            if (seqLauncher.current(t) != 0 && seqLauncher.current(t) != pattern)
                allRam = false;
    }
    if (!allRam && bufferBusy())
        return; // try again on a later pass

    uint16_t mask = 0;
    if (!allRam)
        newFileManager.getPatternTracksWithNotes(pattern, mask);
    if (pattern == own)
    {
        for (uint8_t t = 0; t < seqsession::kTracks; t++)
        {
            if (seqLauncher.current(t) == 0 || seqLauncher.current(t) == pattern)
            {
                if (ramTrackHasNotes(t))
                    mask = (uint16_t)(mask | (1u << t));
                else
                    mask = (uint16_t)(mask & ~(1u << t));
            }
        }
    }
    s_cache.set((uint8_t)row, mask);
}
} // namespace

// ---------------------------------------------------------------- Sequencer members (audio context)
void Sequencer::seqSessionStep(uint8_t row)
{
    if (row >= seqsession::kTracks)
        return;
    seqLauncher.drain();
    uint8_t pattern = 0;
    switch (seqLauncher.onStep(row, (uint16_t)player.globalPos, pattern))
    {
        case seqsession::Launcher::A_CLIP: seqSessionApplyClip(row, seqLauncher.quant() != seqsession::Q_NOW); break;
        case seqsession::Launcher::A_STOP: seqSessionApplyStop(row); break;
        default: break;
    }
}

// The clip that was read into the performance buffer replaces this track of the playing pattern.
void Sequencer::seqSessionApplyClip(uint8_t track, bool restart)
{
    switchPerformanceTrackNow(track); // copies the track, takes its length, realigns when the playhead is beyond it
    player.track[track].performanceSourcePattern = -1;
    cancelFxes(track);
    if (restart && player.track[track].performancePlayMode == PLAYMODE_FORWARD)
    {
        player.track[track].actual_pos = MINSTEP; // a clip starts at its first step
        player.track[track].actual_pos_absolute = 0;
    }
    s_used = true;
}

void Sequencer::seqSessionApplyStop(uint8_t track)
{
    seqTakeoverBegin(track); // closes the sounding note, clears roll / arp / micromove
}

// The whole pattern changed (scene, song, Pattern page): clips are over, every track follows the pattern again.
void Sequencer::seqSessionPatternSwitched()
{
    seqLauncher.onPatternSwitched();
    if (s_used)
    {
        s_used = false;
        exitPerformanceMode(); // performance mode only carried the per-track clip lengths
    }
}

void Sequencer::seqSessionEnterPerformance()
{
    if (!player.performanceMode)
        enterPerformanceMode();
}

// ---------------------------------------------------------------- main loop
bool seqSessionLaunchClip(uint8_t track, uint8_t pattern)
{
    if (track >= seqsession::kTracks || pattern < 1)
        return false;
    if (seqLauncher.current(track) == 0 && pattern == seqSessionOwnPattern() && !seqLauncher.stopped(track))
        return true; // the track already plays its own pattern
    if (bufferBusy())
        return false;

    sequencer.seqSessionEnterPerformance();
    seqLauncher.beginLoad(track);
    newFileManager.loadPerformanceTrackToBuffer(pattern, track);
    bool ok = seqLauncher.postClip(track, pattern);
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
        while (delta != 0)
        {
            int step = delta > 100 ? 100 : (delta < -100 ? -100 : delta); // the stock call takes an int8_t
            sequencer.sequencialSwitch_changeNextPattern((int8_t)step);
            delta -= step;
        }
        sequencer.sequencialSwitch_SetReady();
        return true;
    }
    // transport stopped: switch at once, like the Pattern page does
    sequencer.sequencialSwitch_Reset();
    sequencer.setOnGlobalStep(nullCallback);
    newFileManager.saveWorkspacePatternNow(mtProject.values.actualPattern);
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
        seqLauncher.drain();
        for (uint8_t t = 0; t < seqsession::kTracks; t++)
        {
            if (seqLauncher.queuedKind(t) == seqsession::Launcher::K_CLIP)
            {
                uint8_t p = 0;
                if (seqLauncher.flush(t, p) == seqsession::Launcher::A_CLIP)
                    sequencer.seqSessionApplyClip(t, false);
            }
        }
        seqLauncher.onTransportStop();
    }
    s_wasPlay = play;

    if (s_active)
        scanOneRow();
}
