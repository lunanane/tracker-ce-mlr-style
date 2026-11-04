// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
#include "cutLayer.h"
#include "seqTracks.h"
#include "seqCutLogic.h"
#include <string.h>

using namespace seqcut;

namespace
{
uint8_t s_first = 0;
bool s_rec = false;
uint16_t s_recTracks = 0; // tracks whose take was started while Rec was held
int8_t s_heldTrack[kCutPads];
bool s_init = false;
uint8_t s_blink = 0;

void init()
{
    if (s_init)
        return;
    s_init = true;
    memset(s_heldTrack, -1, sizeof(s_heldTrack));
    seqCutInit();
}

// Releases (pad up, Rec up) that do not fit into the command queue are kept and retried: a lost release would leave a
// loop or a take open for ever. Presses and the one-shot controls may be dropped.
struct Pending
{
    uint8_t track, arg;
};
Pending s_pending[2 * kCutTracks + 8];
uint8_t s_nPending = 0;

void flushPending()
{
    uint8_t keep = 0;
    for (uint8_t i = 0; i < s_nPending; i++)
        if (!seqEngine.postCut(s_pending[i].track, s_pending[i].arg))
            s_pending[keep++] = s_pending[i];
    s_nPending = keep;
}

// SYNC ALL is one ring entry that must not be lost: kept until the queue accepts it, behind the releases that wait.
bool s_syncPending = false;

void trySync()
{
    flushPending();
    if (s_syncPending && s_nPending == 0 && seqEngine.postCut(0, packSyncAll()))
        s_syncPending = false;
}

void post(uint8_t track, uint8_t sub, bool down, uint8_t col)
{
    trySync();
    uint8_t arg = packArg(sub, down, col);
    if (s_syncPending && down)
        return; // a press must not overtake a SYNC ALL that is still waiting (it would be wiped by it)
    if (s_nPending == 0 && !s_syncPending && seqEngine.postCut(track, arg))
        return;
    if (!down && (sub == SUB_PAD || sub == SUB_REC) && s_nPending < sizeof(s_pending) / sizeof(s_pending[0]))
    {
        s_pending[s_nPending].track = track; // keep the order: later commands wait behind it
        s_pending[s_nPending].arg = arg;
        s_nPending++;
    }
}
} // namespace

void cutSetGroup(uint8_t firstTrack)
{
    init();
    s_first = groupFirst(firstTrack); // held pads keep the track they were pressed on
}

void cutPad(uint8_t col, uint8_t row, bool down, bool shift)
{
    if (col >= kCols || row >= kRows)
        return;
    init();
    const uint8_t idx = (uint8_t)(row * kCols + col);
    uint8_t track;
    if (down)
    {
        track = trackOfRow(s_first, row);
        s_heldTrack[idx] = (int8_t)track;
    }
    else
    {
        if (s_heldTrack[idx] < 0)
            return;
        track = (uint8_t)s_heldTrack[idx];
        s_heldTrack[idx] = -1;
    }
    if (col < kPosCols)
    {
        if (down && s_rec && !((s_recTracks >> track) & 1u))
        {
            s_recTracks = (uint16_t)(s_recTracks | (1u << track));
            post(track, SUB_REC, true, 0); // the take starts with the first pad of the row
        }
        post(track, SUB_PAD, down, col);
        return;
    }
    if (!down)
        return;
    switch (col)
    {
        case COL_STOP: post(track, SUB_STOP, true, 0); break;
        case COL_REVERSE: post(track, SUB_REVERSE, true, 0); break;
        case COL_RATE: post(track, SUB_RATE, true, 0); break;
        case COL_RESYNC:
            if (shift)
            {
                cutSyncAll(); // Shift + RESYNC: all tracks
                break;
            }
            s_recTracks = (uint16_t)(s_recTracks & ~(1u << track)); // a resync ends a take in progress
            post(track, SUB_RESYNC, true, 0);
            break;
        default: break;
    }
}

void cutRec(bool down)
{
    init();
    if (down == s_rec)
        return;
    s_rec = down;
    if (!down)
    {
        for (uint8_t t = 0; t < kCutTracks; t++)
            if ((s_recTracks >> t) & 1u)
                post(t, SUB_REC, false, 0);
        s_recTracks = 0;
    }
}

void cutLeds(uint8_t* out48)
{
    init();
    trySync(); // called on every redraw: also retries lost releases and a SYNC ALL
    View views[kRows];
    Cut* c = seqEngine.cut();
    for (uint8_t r = 0; r < kRows; r++)
    {
        if (c)
            views[r] = c->view(trackOfRow(s_first, r));
        else
        {
            views[r].col = 0; views[r].loopLo = 1; views[r].loopHi = 0; views[r].flags = (uint8_t)(1 << VF_RATE_SHIFT);
        }
    }
    s_blink++;
    fillLeds(views, ((s_blink >> 3) & 1u) != 0, out48);
}

void cutSyncAll()
{
    init();
    // local state first: the audio side drops held pads and takes with the command, so no per-pad releases are posted for them
    memset(s_heldTrack, -1, sizeof(s_heldTrack));
    s_recTracks = 0;
    s_syncPending = true;
    trySync();
}

void cutUpdate()
{
    if (!s_init)
        return;
    trySync();
}

bool cutRecHeld() { return s_rec; }
bool cutSyncPending() { return s_syncPending; }

void cutPanic()
{
    init();
    cutReleaseAll(); // holds and open takes end (the releases go first, in order)
    cutSyncAll();
}

void cutReleaseAll()
{
    init();
    for (uint8_t i = 0; i < kCutPads; i++)
        if (s_heldTrack[i] >= 0)
            cutPad((uint8_t)(i % kCols), (uint8_t)(i / kCols), false);
    cutRec(false);
}
