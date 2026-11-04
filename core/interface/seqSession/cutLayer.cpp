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

void post(uint8_t track, uint8_t sub, bool down, uint8_t col)
{
    seqEngine.postCut(track, packArg(sub, down, col));
}
} // namespace

void cutSetGroup(uint8_t firstTrack)
{
    init();
    s_first = groupFirst(firstTrack); // held pads keep the track they were pressed on
}

void cutPad(uint8_t col, uint8_t row, bool down)
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

void cutReleaseAll()
{
    init();
    for (uint8_t i = 0; i < kCutPads; i++)
        if (s_heldTrack[i] >= 0)
            cutPad((uint8_t)(i % kCols), (uint8_t)(i / kCols), false);
    cutRec(false);
}
