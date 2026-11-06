// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
#include "cutLayer.h"
#include "seqTracks.h"
#include "seqBeatRepeat.h"
#include "seqCutLogic.h"
#include "seqMlrLogic.h"
#include <string.h>

using namespace seqcut;

namespace
{
uint8_t s_slot[kSlots] = { 0, 1 }; // the track each slot holds (plan 016 A: slot A = pad rows 1-2, slot B = rows 3-4)
int8_t s_pick = -1;                // slot whose two pad rows are the track picker, -1 = none
bool s_legend = false;             // the pad legend is shown on the screen
bool s_rec = false;
bool s_brOn = false;               // plan 021: the pads are the beat repeat controls (the MLR state underneath is untouched)
uint16_t s_recTracks = 0; // tracks whose take was started while Rec was held
int8_t s_heldTrack[kCutPads];
bool s_init = false;
// The jump quantise setting (plan 008 A): the mode is kept here, the audio side learns it with one command (kept until accepted).
uint8_t s_quant = Q_DEFAULT;
bool s_quantPending = false;
uint8_t s_blink = 0;

void init()
{
    if (s_init)
        return;
    s_init = true;
    memset(s_heldTrack, -1, sizeof(s_heldTrack));
    seqCutInit();
    s_quantPending = true; // the default (beat) goes to the audio side with the first use of the layer
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
    if (s_quantPending && seqEngine.postCut(0, packQuant(s_quant)))
        s_quantPending = false;
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

void cutInit() { init(); }
bool cutSlotsUsed() { return s_init; }
bool cutTrackRecArmed(uint8_t track) { return track < kCutTracks && ((s_recTracks >> track) & 1u) != 0; }

uint8_t cutSlotTrack(uint8_t slot) { return s_slot[slot & 1]; }

// A track belongs to one slot: picking the other slot's track swaps the two.
void cutAssignSlot(uint8_t slot, uint8_t track)
{
    init();
    slot &= 1;
    if (track >= kCutTracks)
        return;
    if (s_slot[slot ^ 1] == track)
        s_slot[slot ^ 1] = s_slot[slot];
    s_slot[slot] = track;
}

void cutPickToggle(uint8_t slot)
{
    init();
    s_pick = (s_pick == (int8_t)(slot & 1)) ? (int8_t)-1 : (int8_t)(slot & 1);
}
int8_t cutPickSlot() { return s_pick; }
void cutLegendToggle() { s_legend = !s_legend; }

// ---- plan 021: beat repeat on the pads (fader page layout, seqBeatRepeatLogic.h). The MLR layer keeps playing underneath; its holds,
// loops and takes are not touched by the switch.
bool cutBrActive() { return s_brOn; }

void cutBrToggle()
{
    init();
    if (s_brOn)
    {
        // back to MLR: the grid pads in the hand are let go (a latched repeat goes on)
        seqbr::Ui& ui = seqBrUi();
        if (ui.releaseAll())
        {
            seqbr::Result r = { seqbr::BA_UP, 0 };
            seqBrAct(r);
        }
        s_brOn = false;
        return;
    }
    // MLR pads in the hand are let go first (a held pad would otherwise never see its release), the track picker closes
    for (uint8_t i = 0; i < kCutPads; i++)
        if (s_heldTrack[i] >= 0)
            cutPad((uint8_t)(i % kCols), (uint8_t)(i / kCols), false);
    s_pick = -1;
    s_brOn = true;
}

namespace
{
void brPad(uint8_t col, uint8_t row, bool down)
{
    seqbr::Result r[2];
    const int n = seqBrUi().pad(col, row, down, r);
    for (int i = 0; i < n; i++)
        seqBrAct(r[i]);
}
} // namespace
bool cutLegendShown() { return s_legend; }

void cutPad(uint8_t col, uint8_t row, bool down, bool shift)
{
    if (col >= kCols || row >= kRows)
        return;
    init();
    if (s_brOn)
    {
        brPad(col, row, down);
        return;
    }
    const uint8_t idx = (uint8_t)(row * kCols + col);
    const PadInfo pi = padInfo(col, row);
    if (pi.role == PR_STEP && s_pick == (int8_t)pi.slot)
    {
        if (down)
        {
            cutAssignSlot(pi.slot, pi.idx); // the picker: this pad is the track; the rows return to MLR
            s_pick = -1;
        }
        return;
    }
    uint8_t track;
    if (down)
    {
        track = s_slot[pi.slot];
        s_heldTrack[idx] = (int8_t)track;
    }
    else
    {
        if (s_heldTrack[idx] < 0)
            return;
        track = (uint8_t)s_heldTrack[idx];
        s_heldTrack[idx] = -1;
    }
    if (pi.role == PR_STEP)
    {
        if (down && s_rec && !((s_recTracks >> track) & 1u))
        {
            s_recTracks = (uint16_t)(s_recTracks | (1u << track));
            post(track, SUB_REC, true, 0); // the take starts with the first pad of the slot
        }
        post(track, SUB_PAD, down, pi.idx);
        return;
    }
    if (!down)
        return;
    const View v = seqEngine.cut() ? seqEngine.cut()->view(track) : View();
    switch (pi.role)
    {
        case PR_STOP: post(track, SUB_STOP, true, 0); break;
        case PR_REV: post(track, SUB_REVERSE, true, 0); break;
        case PR_RATE: post(track, SUB_RATE, true, 0); break;
        case PR_RSYNC:
            if (shift)
            {
                cutSyncAll(); // Shift + RSYNC: all tracks
                break;
            }
            s_recTracks = (uint16_t)(s_recTracks & ~(1u << track)); // a resync ends a take in progress
            post(track, SUB_RESYNC, true, 0);
            break;
        case PR_GRID: seqEngine.postCut(track, packGrid((v.flags2 & V2_GRID8) ? GRID_4 : GRID_8)); break;
        case PR_HOLD: seqEngine.postCut(track, packHold(!(v.flags2 & V2_HOLD))); break;
        case PR_REC: // a take of this slot only: the Rec key stays the global toggle
            if ((s_recTracks >> track) & 1u)
            {
                s_recTracks = (uint16_t)(s_recTracks & ~(1u << track));
                post(track, SUB_REC, false, 0);
            }
            else
            {
                s_recTracks = (uint16_t)(s_recTracks | (1u << track));
                post(track, SUB_REC, true, 0);
            }
            break;
        case PR_MUTE: seqCutToggleMute(track); break;
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
    if (s_brOn)
    {
        seqbr::Ui& ui = seqBrUi();
        seqbr::LedState ls;
        memset(&ls, 0, sizeof(ls));
        ls.page = ui.page;
        ls.mode = ui.prm.mode;
        ls.latch = ui.latch ? 1 : 0;
        ls.heldMask = ui.heldMask();
        memcpy(ls.step, ui.prm.step, sizeof(ls.step));
        beatrepeat::Status st;
        memset(&st, 0, sizeof(st));
        if (seqBrStatus(&st))
        {
            ls.state = st.state;
            ls.eventGrid = st.grid;
        }
        s_blink++;
        seqbr::fillLeds(ls, ((s_blink >> 3) & 1u) != 0, out48);
        return;
    }
    SlotLeds sl[kSlots];
    Cut* c = seqEngine.cut();
    const uint16_t has = s_pick >= 0 ? seqCutTracksWithNotes() : 0;
    for (uint8_t k = 0; k < kSlots; k++)
    {
        memset(&sl[k], 0, sizeof(sl[k]));
        sl[k].track = s_slot[k];
        if (c)
            sl[k].v = c->view(s_slot[k]);
        sl[k].picking = s_pick == (int8_t)k;
        sl[k].has = has;
        sl[k].muted = seqCutTrackMuted(s_slot[k]);
        sl[k].rec = (s_recTracks >> s_slot[k]) & 1u;
    }
    s_blink++;
    fillMlrLeds(sl, ((s_blink >> 3) & 1u) != 0, out48);
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

// Rec is a toggle (plan 008 A): a press arms the take (the first pad on a row starts that row's take), the next press closes all
// takes, which the tracks then replay. cutRec() sets the state directly (leaving the layer, panic, tests).
void cutRecToggle()
{
    init();
    cutRec(!s_rec);
}

bool cutRecHeld() { return s_rec; }

uint8_t cutQuantMode() { return s_quant; }

void cutQuantSet(uint8_t mode)
{
    init();
    s_quant = (uint8_t)(mode % Q_COUNT);
    s_quantPending = true;
    trySync();
}

void cutQuantCycle() { cutQuantSet(quantNext(s_quant)); }
bool cutSyncPending() { return s_syncPending; }

void cutPanic()
{
    init();
    cutReleaseAll(); // holds and open takes end (the releases go first, in order)
    seqBrPanic();    // plan 021: Stop ends any beat repeat, the latch is off
    cutSyncAll();
}

void cutReleaseAll()
{
    init();
    if (seqBrUi().releaseAll()) // plan 021: grid pads in the hand are let go (a latched repeat goes on)
    {
        seqbr::Result r = { seqbr::BA_UP, 0 };
        seqBrAct(r);
    }
    for (uint8_t i = 0; i < kCutPads; i++)
        if (s_heldTrack[i] >= 0)
            cutPad((uint8_t)(i % kCols), (uint8_t)(i / kCols), false);
    cutRec(false);
}
