// Playhead: MLR-style per-track playhead controller for the Session view (plan 002, phase S1).
// One instance = one track. No platform includes, no heap, no STL, no float, fixed arrays.
// Rules and rationale: README.md, section "Playhead".
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace seqgen {

class Playhead {
 public:
  enum Rate : uint8_t { RATE_HALF = 0, RATE_NORMAL = 1, RATE_DOUBLE = 2 };
  enum { kMaxLength = 256, kMaxColumns = 12, kMaxRec = 256 };
  enum { kSerialVersion = 1, kSerialSize = 8 + kMaxRec + kMaxRec / 8 };

  // What the track should play for one sequencer step.
  struct StepOut {
    uint8_t pos;    // position 0..length-1 to play on this step (first half-step when has2)
    uint8_t pos2;   // rate double only (has2): position for the second half-step (pos2 == pos otherwise)
    bool jumped;    // pos is NOT the engine's natural successor of the previously played position (+1, wrapping):
                    // retrigger / re-seek the track ("as if at pos", see noteOffsetFor). Also set after resync().
    bool jumped2;   // same for pos2 relative to pos
    bool hold;      // rate half, second step of a position: still pos, do not trigger anything (jumped == false)
    bool has2;      // rate double: play pos2 at the middle of the step
  };

  Playhead() { reset(); }
  void reset();  // defaults: length 16, 8 columns, quantise 1, normal rate, forward, nothing recorded

  // ---- settings (take effect on the next step()) ----
  void setLength(int steps);       // 1..256
  void setColumns(int n);          // 8 or 12 (anything else = 8); clears held pads / loop / queued jumps
  void setQuantise(int steps);     // 1 = next step, 4 = next beat (steps; 1..64)
  void setRate(Rate r);
  void setReverse(bool rev);
  int length() const { return length_; }
  int columns() const { return columns_; }
  int quantise() const { return quant_; }
  Rate rate() const { return (Rate)rate_; }
  bool reverse() const { return rev_; }
  int columnPosition(int col) const { return (col * length_) / columns_; }  // col 0..columns; position of a column's first step

  // ---- gestures (any time between step() calls) ----
  void press(int col);    // 0-based column of this track's row
  void release(int col);
  void recDown();         // start a take: records what is played on each following step()
  void recUp();           // close the take: recorded steps become a loop (a take with 0 steps clears the loop)
  void resync(uint32_t globalStep = 0);  // align to the global step counter; clears loop, queue, recording

  // ---- clock: call once per sequencer step boundary ----
  StepOut step();

  // ---- state ----
  int position() const { return pos_; }            // last played position
  bool recording() const { return recording_; }
  bool recorded() const { return recActive_; }     // a recorded playhead loop is replaying
  int recordedLength() const { return recActive_ ? recLen_ : 0; }
  bool looping() const { return loopActive_; }     // a two-pad loop range is active
  int heldCount() const;
  bool pending() const { return pendLoop_ || pendJump_ >= 0; }  // a jump/loop is queued for the next boundary
  void clearRecording() { recActive_ = false; recording_ = false; recCount_ = 0; }

  // "Replay as if at X": the last step with a note at or before `position` (wrapping to the end of the track when
  // none), and how many steps ago that was. hasNote = `length` bytes, non-zero = the step has a note.
  // Returns false when the track has no note at all.
  static bool noteOffsetFor(int position, const uint8_t* hasNote, int length, int& noteStep, int& stepsSinceNote);
  bool noteOffsetFor(int position, const uint8_t* hasNote, int& noteStep, int& stepsSinceNote) const {
    return noteOffsetFor(position, hasNote, length_, noteStep, stepsSinceNote);
  }

  // Settings + recorded loop only; runtime state (position, held pads, queue) restarts clean.
  size_t serialize(uint8_t* buf, size_t cap) const;  // returns kSerialSize, or 0 if cap is too small
  bool deserialize(const uint8_t* buf, size_t len);

 private:
  void rangeOf(int a, int b, int& lo, int& hi) const;
  void heldRange(int& a, int& b) const;
  int succ(int p, int lo, int hi) const;
  void closeRecording();

  uint16_t length_, recLen_, recIdx_, recCount_;
  uint8_t columns_, quant_, rate_;
  bool rev_;
  uint16_t held_;                    // bit c = column c is down
  int16_t pos_;                      // last played position
  int16_t pendJump_;                 // queued absolute jump position, -1 = none
  bool pendLoop_, pendStart_;        // queued loop range (columns pendA_..pendB_); pendStart_ = jump to its start
  uint8_t pendA_, pendB_;
  bool loopActive_;
  uint8_t loA_, loB_;                // active loop, columns inclusive
  bool ovr_;                         // a pad override is in force over the recorded loop
  uint8_t holdLeft_;                 // rate half: steps left to hold the current position
  bool forceJump_;
  bool recording_, recActive_;
  uint32_t gstep_;                   // index of the next step() (quantise boundaries)
  uint8_t recBuf_[kMaxRec];
  uint8_t recHold_[kMaxRec / 8];
};

}  // namespace seqgen
