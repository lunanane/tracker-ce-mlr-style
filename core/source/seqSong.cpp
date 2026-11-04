// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
#include "seqSong.h"
#include "seqClips.h"
#include "seqLauncher.h"
#include "mtSequencer.h"
#include "mtStructs.h"

extern Sequencer sequencer;
extern strMtProject mtProject;

namespace
{
seqsong::Latch s_latch;
volatile uint8_t s_queued = 0;   // 1-based slot queued from the Song page, 0 = none
volatile bool s_same = false;    // the queued slot holds the loaded pattern: no pattern switch, the song position moves at the pattern end
volatile bool s_dropped = false; // the mod dropped the song mode (a scene outside the song); a switch back into the song restores it

seqsong::Advance liveDecision(bool exporting)
{
    if (s_same && !exporting)
        return seqsong::ADV_STAY; // the queued slot is the same pattern: nothing to load, the end moves the position
    uint8_t pos = mtProject.song.playlistPos;
    bool last = pos + 1 >= SONG_MAX || mtProject.song.playlist[pos + 1] == PLAYLIST_EMPTY_SLOT;
    return seqsong::advance(seqClipPlayNext((uint8_t)mtProject.values.actualPattern), last, exporting);
}

uint8_t patternAt(uint8_t slot1)
{
    return slot1 ? mtProject.song.playlist[slot1 - 1] : 0;
}

void dropArmedSwitch()
{
    sequencer.sequencialSwitch_Reset();
    if (sequencer.globalStepIsSetReady()) // never take the slot of another callback (the Song page loop toggle shares it)
        sequencer.setOnGlobalStep(nullCallback);
}
} // namespace

bool seqSongStayAtLastStep(bool exporting)
{
    return s_latch.decide(liveDecision(exporting)) == seqsong::ADV_STAY;
}

bool seqSongStayAtEnd(bool exporting)
{
    bool stay = s_latch.resolve(exporting) == seqsong::ADV_STAY;
    uint8_t q = s_queued;
    if (stay && s_same && q)
    {
        if (patternAt(q) == (uint8_t)mtProject.values.actualPattern)
            mtProject.song.playlistPos = (uint8_t)(q - 1);
        s_queued = 0;
        s_same = false;
    }
    return stay;
}

void seqSongPatternEndTaken()
{
    s_latch.clear();
}

int16_t seqSongSlotAfterSwitch(uint8_t actualPattern)
{
    uint8_t q = s_queued;
    int16_t s = seqsong::slotAfterSwitch(q, patternAt(q), actualPattern);
    if (s >= 0)
    {
        s_queued = 0;
        s_same = false;
    }
    return s;
}

bool seqSongModeRestore()
{
    if (!s_dropped)
        return false;
    s_dropped = false;
    return true;
}

void seqSongModeDropped()
{
    s_dropped = true;
}

void seqSongUserMode()
{
    s_dropped = false;
}

uint8_t seqSongQueuedSlot()
{
    uint8_t q = s_queued;
    if (!q)
        return 0;
    uint8_t shown;
    if (s_same)
        shown = patternAt(q) == (uint8_t)mtProject.values.actualPattern ? q : 0;
    else
        shown = seqsong::queuedShown(q, patternAt(q), sequencer.sequencialSwitch_GetNext());
    if (!shown)
    {
        s_queued = 0; // the stock switch is gone (a scene pad, the Pattern page, Stop): the queue is too
        s_same = false;
    }
    return shown;
}

bool seqSongQueueSlot(uint8_t slot)
{
    if (slot >= SONG_MAX)
        return false;
    uint8_t pattern = mtProject.song.playlist[slot];
    uint8_t actual = (uint8_t)mtProject.values.actualPattern;
    seqsong::QueueOp op = seqsong::queueOp(sequencer.isPlay(), slot, mtProject.song.playlistPos, pattern, seqSongQueuedSlot());
    if (op == seqsong::QOP_NONE)
        return false;
    if (sequencer.isLastPatternStep() || sequencer.sequencialSwitch_IsLoaded())
        return false; // the end of the pattern is here: a change now would race with the switch (try again)
    if (op == seqsong::QOP_CANCEL)
    {
        dropArmedSwitch();
        s_queued = 0;
        s_same = false;
        return true;
    }
    if (!seqsong::queueNeedsSwitch(pattern, actual))
    {
        if (!sequencer.isSongMode())
        {
            mtProject.song.playlistPos = slot; // a pattern loop: the cursor slot is all there is to move
            return true;
        }
        dropArmedSwitch(); // an earlier queue of another pattern is replaced
        s_queued = (uint8_t)(slot + 1);
        s_same = true;
        return true;
    }
    if (!seqSessionLaunchScene(pattern))
        return false;
    s_same = false;
    s_queued = (uint8_t)(slot + 1);
    return true;
}

bool seqSongSlotStays()
{
    return sequencer.isSongMode() && liveDecision(false) == seqsong::ADV_STAY;
}

bool seqSongPlayNext(uint8_t slot)
{
    return slot < SONG_MAX && seqClipPlayNext(mtProject.song.playlist[slot]);
}

void seqSongSetPlayNext(uint8_t slot, bool on)
{
    if (slot < SONG_MAX && mtProject.song.playlist[slot] != 0)
        seqClipSetPlayNext(mtProject.song.playlist[slot], on);
}

uint8_t seqSongSlotMarks(uint16_t slot)
{
    if (slot >= SONG_MAX)
        return 0;
    return (uint8_t)((seqSongPlayNext((uint8_t)slot) ? 1 : 0) | (seqSongQueuedSlot() == slot + 1 ? 2 : 0));
}

void seqSongPanic()
{
    s_queued = 0;
    s_same = false;
    s_dropped = false;
    s_latch.clear();
}
