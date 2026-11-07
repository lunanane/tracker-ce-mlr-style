// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// CUT (MLR) layer engine: one seqgen::Playhead per track drives the stock sequencer's playhead of a "cut-active" track
// (plan 002). No Polyend includes, host-tested in core/seqtest.
// Threads: apply/boundary/mid/transportStart/take*/holding are audio context; view()/activeMask() are read by the main loop.
#pragma once
#include "playhead.h"
#include "seqCutLogic.h"
#include "seqMlrLogic.h"

namespace seqcut {

// What the firmware has to provide (set by the glue; both optional in tests).
struct Hooks
{
    void* user;
    // hasNote[0..steps-1]: 1 = the step holds a note, 2 = a note-off, 0 = nothing (other steps).
    void (*noteMap)(void* user, uint8_t track, uint8_t steps, uint8_t* hasNote);
    int (*trackPos)(void* user, uint8_t track);   // current pattern position (actual_pos) of the track
    int (*trackSteps)(void* user, uint8_t track); // number of steps of the pattern (length + 1)
    void (*stopNote)(void* user, uint8_t track);  // silence the sounding note of the track (and drop its pending micromove / roll / arp)
    bool (*sampleTrack)(void* user, uint8_t track); // optional: false = MIDI / synth track (default: sample track)
};

// Result of a step boundary (or the middle of a step at rate 2).
struct StepPlan
{
    bool override;    // actual_pos must be set to `pos` (false: leave the stock position)
    uint8_t pos;
    bool hold;        // do not trigger anything for this step (rate 1/2 second half, or the track is stopped)
    bool retrigger;   // play the note of step `noteStep` instead of the step at `pos`, `elapsed` steps late
    uint8_t noteStep;
    uint8_t elapsed;
};

class Cut
{
  public:
    Cut();
    void setHooks(const Hooks& h) { hooks_ = h; }

    // ---- audio context ----
    void apply(uint8_t track, uint8_t arg);                       // arg = packArg(...)
    // After the stock step switch of ALL tracks; globalPos = the global step that plays next (the global clock has already advanced).
    // frozen = the Session view stopped the track: nothing advances, nothing triggers (Session STOP wins over CUT).
    StepPlan boundary(uint8_t track, int steps, int globalPos, bool frozen = false);
    bool mid(uint8_t track, int steps, StepPlan& out, bool frozen = false);
    // Quantise "off": a pad gesture is played at the next audio tick, between two step boundaries (uStep != 1). true = jump now.
    // Quantise 1/32 (plan 026): called only in the middle of a step (µstep 25), applies a pad gesture that waits for the next half-step.
    bool now(uint8_t track, int steps, StepPlan& out, bool frozen = false);
    bool consumeRetrigger(uint8_t track, StepPlan& out);          // play_microStep, uStep == 1: pending retrigger
    bool holding(uint8_t track) const { return track < kCutTracks && (((holdMask_ >> track) & 1u) != 0); } // second half of a rate-1/2 position
    bool stopped(uint8_t track) const { return track < kCutTracks && (((stopMask_ >> track) & 1u) != 0); } // STOP: nothing plays
    // A Session clip was launched on the track: the playhead restarts at `pos` (the step the track plays next, in a pattern of
    // `steps`), a pending retrigger / second half-step is dropped and a CUT stop ends (the clip is meant to be heard).
    void launched(uint8_t track, int steps, int pos);
    void transportStart(int fromPos = 0);                         // Play pressed: playheads continue from step fromPos + 1
    // The stock sequencer's own position of a cut track (what actual_pos would be without the override), kept by the glue.
    int stock(uint8_t track) const { return track < kCutTracks ? stock_[track] : 0; }
    void setStock(uint8_t track, int pos) { if (track < kCutTracks) stock_[track] = (int16_t)pos; }
    uint16_t activeMask() const { return activeMask_; }
    // MLR sections (plan 016 A): off = the legacy layout (8 pads share the whole pattern, kept for the engine tests), on = 16 pads on the
    // musical grid, the pads address one section. Set once before use (seqCutInit); the grid (1/4, 1/8) and a section hold are per track.
    void setSectionMode(bool on) { secMode_ = on; }
    bool sectionMode() const { return secMode_; }
    uint8_t grid(uint8_t track) const { return track < kCutTracks ? gridMode_[track] : 0; }
    int holdSection(uint8_t track) const { return track < kCutTracks ? holdSec_[track] : -1; }
    // Plan 026 behaviour (set by seqCutInit): deliberate two-pad loops - a second pad pressed while another is held is a jump, the loop starts
    // with the engage command (packEngage) the layer sends after both pads were held ~0.35 s - and the ratchet (a held pad retriggers its
    // position at the quantise rate). Off (default) = the earlier instant loop without ratchet (engine tests of plans 002-016).
    void setDeliberate(bool on) { gate_ = on; }
    bool deliberate() const { return gate_; }
    uint8_t quant() const { return quant_; } // jump quantise mode (seqcut::Quant), audio context writes it, any context may read it
    // Mask for the audio hooks: driven tracks plus tracks whose hand-back retrigger is still to be played (see consumeRetrigger).
    uint16_t hookMask() const { return (uint16_t)(activeMask_ | handMask_); }
    // Tracks that really are off the grid: driven and not already snapped by SYNC ALL (UI, R1 strip).
    uint16_t offGridMask() const { return (uint16_t)(activeMask_ & ~snapMask_); }

    // ---- main loop ----
    View view(uint8_t track) const;
    seqgen::Playhead& playhead(uint8_t track) { return ph_[track]; } // tests only

  private:
    struct Rt
    {
        bool stopped, resync, hold, has2, jumped2, retrig, snap; // snap: SYNC ALL is pending / done (cleared by any later gesture)
        uint8_t pos2, noteStep, elapsed, heldCols;
        uint16_t held;
        View view;
    };
    void activate(uint8_t t);
    void applyQuant(seqgen::Playhead& p);
    void syncAll();
    void setSnap(uint8_t t, bool s);
    void deactivate(uint8_t t);
    void planJump(uint8_t t, int steps, uint8_t pos, StepPlan& p);
    void publish(uint8_t t);
    void setHold(uint8_t t, bool h);
    void setStopped(uint8_t t, bool s);
    void sectionView(View& v, int pos, int steps, int spp, int holdSec, uint8_t t) const;

    seqgen::Playhead ph_[kCutTracks];
    Rt rt_[kCutTracks];
    uint8_t scratch_[seqgen::Playhead::kMaxLength];
    Hooks hooks_;
    volatile uint16_t activeMask_;
    volatile uint16_t holdMask_;
    volatile uint16_t stopMask_;
    volatile uint16_t snapMask_; // SYNC ALL done / pending: the view shows the stock state
    volatile uint16_t handMask_; // retrigger of a track that was just handed back to the stock sequencer
    struct Hand { uint8_t noteStep, elapsed; } hand_[kCutTracks];
    int16_t stock_[kCutTracks];
    volatile uint8_t quant_; // the engine starts on every step; the CUT layer (cutLayer.cpp) sets the user's default (beat) when it is first used
    View views_[kCutTracks];
    bool secMode_;
    bool gate_;
    uint8_t gridMode_[kCutTracks];
    int8_t holdSec_[kCutTracks]; // held section of the track, -1 = follow
};

} // namespace seqcut
