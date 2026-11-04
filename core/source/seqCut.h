// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// CUT (MLR) layer engine: one seqgen::Playhead per track drives the stock sequencer's playhead of a "cut-active" track
// (plan 002). No Polyend includes, host-tested in core/seqtest.
// Threads: apply/boundary/mid/transportStart/take*/holding are audio context; view()/activeMask() are read by the main loop.
#pragma once
#include "playhead.h"
#include "seqCutLogic.h"

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
    StepPlan boundary(uint8_t track, int steps, int globalPos);
    bool mid(uint8_t track, int steps, StepPlan& out);            // µstep 25 of a rate-2 step: start the second half-step
    bool consumeRetrigger(uint8_t track, StepPlan& out);          // play_microStep, uStep == 1: pending retrigger
    bool holding(uint8_t track) const { return track < kCutTracks && (((holdMask_ >> track) & 1u) != 0); } // second half of a rate-1/2 position
    bool stopped(uint8_t track) const { return track < kCutTracks && (((stopMask_ >> track) & 1u) != 0); } // STOP: nothing plays
    void transportStart(int fromPos = 0);                         // Play pressed: playheads continue from step fromPos + 1
    // The stock sequencer's own position of a cut track (what actual_pos would be without the override), kept by the glue.
    int stock(uint8_t track) const { return track < kCutTracks ? stock_[track] : 0; }
    void setStock(uint8_t track, int pos) { if (track < kCutTracks) stock_[track] = (int16_t)pos; }
    uint16_t activeMask() const { return activeMask_; }

    // ---- main loop ----
    View view(uint8_t track) const;
    seqgen::Playhead& playhead(uint8_t track) { return ph_[track]; } // tests only

  private:
    struct Rt
    {
        bool stopped, resync, hold, has2, jumped2, retrig;
        uint8_t pos2, noteStep, elapsed, heldCols;
        uint16_t held;
        View view;
    };
    void activate(uint8_t t);
    void deactivate(uint8_t t);
    void planJump(uint8_t t, int steps, uint8_t pos, StepPlan& p);
    void publish(uint8_t t);
    void setHold(uint8_t t, bool h);
    void setStopped(uint8_t t, bool s);

    seqgen::Playhead ph_[kCutTracks];
    Rt rt_[kCutTracks];
    uint8_t scratch_[seqgen::Playhead::kMaxLength];
    Hooks hooks_;
    volatile uint16_t activeMask_;
    volatile uint16_t holdMask_;
    volatile uint16_t stopMask_;
    int16_t stock_[kCutTracks];
    View views_[kCutTracks];
};

} // namespace seqcut
