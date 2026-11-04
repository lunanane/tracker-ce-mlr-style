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
volatile uint8_t s_queued = 0; // 1-based slot queued from the Song page, 0 = none

seqsong::Advance liveDecision(bool breakFx, bool exporting)
{
    uint8_t pos = mtProject.song.playlistPos;
    bool last = pos + 1 >= SONG_MAX || mtProject.song.playlist[pos + 1] == PLAYLIST_EMPTY_SLOT;
    return seqsong::advance(seqClipPlayNext((uint8_t)mtProject.values.actualPattern), last, breakFx, exporting);
}

uint8_t patternAt(uint8_t slot1)
{
    return slot1 ? mtProject.song.playlist[slot1 - 1] : 0;
}
} // namespace

bool seqSongStayAtLastStep(bool breakFx, bool exporting)
{
    return s_latch.decide(liveDecision(breakFx, exporting)) == seqsong::ADV_STAY;
}

bool seqSongStayAtEnd(bool breakFx, bool exporting)
{
    return s_latch.resolve(liveDecision(breakFx, exporting), breakFx) == seqsong::ADV_STAY;
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
        s_queued = 0;
    return s;
}

uint8_t seqSongQueuedSlot()
{
    uint8_t q = s_queued;
    if (!q)
        return 0;
    uint8_t shown = seqsong::queuedShown(q, patternAt(q), sequencer.sequencialSwitch_GetNext());
    if (!shown)
        s_queued = 0; // the stock switch is gone (a scene pad, the Pattern page, Stop): the queue is too
    return shown;
}

bool seqSongQueueSlot(uint8_t slot)
{
    if (slot >= SONG_MAX)
        return false;
    uint8_t pattern = mtProject.song.playlist[slot];
    switch (seqsong::queueOp(sequencer.isPlay(), slot, mtProject.song.playlistPos, pattern, seqSongQueuedSlot()))
    {
        case seqsong::QOP_QUEUE:
            if (!seqSessionLaunchScene(pattern))
                return false;
            s_queued = (uint8_t)(slot + 1);
            return true;
        case seqsong::QOP_CANCEL:
            sequencer.sequencialSwitch_Reset();
            sequencer.setOnGlobalStep(nullCallback);
            s_queued = 0;
            return true;
        default: return false;
    }
}

bool seqSongSlotStays()
{
    return sequencer.isSongMode() && liveDecision(false, false) == seqsong::ADV_STAY;
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
    s_latch.clear();
}
