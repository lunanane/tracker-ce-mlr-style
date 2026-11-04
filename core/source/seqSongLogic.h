// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Live-set Song page (plan 006): the song loops the current slot unless its scene (pattern) is flagged "play next"; a slot can be
// queued from the Song page. Pure decisions, no Polyend includes, host-testable (core/seqtest).
#pragma once
#include <stdint.h>

namespace seqsong {

// ---- advance decision at the pattern end of the song (sequencer interrupt: a few compares, no loops, no SD) ----
enum Advance : uint8_t { ADV_STAY = 0, ADV_NEXT = 1 };

//  playNext   the playing slot's pattern is flagged "play next"
//  lastSlot   there is no slot behind the playing one (the song would wrap to slot 1: the song end)
//  exporting  the song is being rendered to audio: the stock song (and its end) is wanted
// Stay = the slot plays again (same as a pattern loop). A flagged last slot also stays: the song never ends in a live set.
// A pattern break effect (a shortened pass) follows the same rule: it does not advance a looping slot (owner: loop by default).
inline Advance advance(bool playNext, bool lastSlot, bool exporting)
{
    if (exporting)
        return ADV_NEXT;
    if (playNext && !lastSlot)
        return ADV_NEXT;
    return ADV_STAY;
}

// The next pattern is loaded (or not) at the last step of the pattern; the pattern end must act on the same answer, or it would
// switch to a buffer nobody loaded. The latch keeps the answer of the last step.
struct Latch
{
    uint8_t v = 0; // 0 = not decided, 1 = stay, 2 = next
    void clear() { v = 0; }
    Advance decide(Advance a)
    {
        v = (uint8_t)(a == ADV_NEXT ? 2 : 1);
        return a;
    }
    // at the pattern end: the latched answer wins. No answer (no last-step hook ran, so nothing was loaded): stay - switching would
    // take an unloaded buffer - except for the audio export, which keeps the stock behaviour. Clears the latch.
    Advance resolve(bool exporting)
    {
        Advance r = v == 2 ? ADV_NEXT : (v == 1 ? ADV_STAY : (exporting ? ADV_NEXT : ADV_STAY));
        v = 0;
        return r;
    }
};

// ---- Song page queue (main loop) ----
// A slot whose pattern is the loaded one needs no pattern switch (nothing to load): the song position just moves at the pattern end.
inline bool queueNeedsSwitch(uint8_t patternAtSlot, uint8_t actualPattern) { return patternAtSlot != actualPattern; }
enum QueueOp : uint8_t { QOP_NONE = 0, QOP_QUEUE = 1, QOP_CANCEL = 2 };
// Press on the selected slot (0-based) while the song page shows the transport:
//  - stopped, or the slot holds no pattern: nothing
//  - the press is on the playing slot: cancels a pending queue, else nothing (it plays already; no reload of its pattern)
//  - otherwise queue the slot (another press on another slot moves the queue)
inline QueueOp queueOp(bool playing, uint8_t slot, uint8_t playingSlot, uint8_t patternAtSlot, uint8_t queuedSlot1)
{
    if (!playing || patternAtSlot == 0)
        return QOP_NONE;
    if (slot == playingSlot)
        return queuedSlot1 != 0 ? QOP_CANCEL : QOP_NONE;
    return QOP_QUEUE;
}

// The queued slot (1-based, 0 = none) as the Song page shows it: only while the stock pattern switch really is armed for that slot's
// pattern (a later scene launch, the Pattern page or Stop drop the queue silently).
inline uint8_t queuedShown(uint8_t queuedSlot1, uint8_t patternAtQueued, uint16_t pendingPattern)
{
    if (queuedSlot1 == 0 || patternAtQueued == 0 || pendingPattern != patternAtQueued)
        return 0;
    return queuedSlot1;
}

// After the pattern switched (audio): where the song stands. queuedSlot1 wins when its slot holds the pattern that is playing now,
// else the stock search (first slot with the pattern, from the position on) stays: -1 = leave the position alone.
inline int16_t slotAfterSwitch(uint8_t queuedSlot1, uint8_t patternAtQueued, uint8_t actualPattern)
{
    if (queuedSlot1 != 0 && patternAtQueued == actualPattern)
        return (int16_t)(queuedSlot1 - 1);
    return -1;
}

}  // namespace seqsong
