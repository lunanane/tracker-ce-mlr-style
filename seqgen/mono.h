// Mono sequencer: owner's grid-composer c34 page 4, one instance = one tk[k].
#pragma once
#include "chordarp.h"
#include "seqgen.h"

namespace seqgen {

class Mono : public SeqBase {
 public:
  enum Param {
    P_SPEED = 0,  // sp_pos 1..12 (index into the SPD table)
    P_TIE,        // 0/1
    P_WIN,        // degree window offset (scale degrees)
    P_NP,         // page count 1..8
    P_EP,         // edit page 1..np
    P_PG,         // playing page (read-only)
    P_LSTART,     // loop start 1..16
    P_LEND,       // loop end 1..16
    P_POS,        // playing step 1..16 (read-only)
    P_TPOSE,      // per-sequencer transpose (scale degrees)
    P_MUTE,       // 0/1
    P_HALF,       // visible half of the page: 0 = steps 1-8, 1 = steps 9-16
    P_DIV,        // pulses per step (read-only, SPD[speed])
    P_PLAYING,    // sounding note or -1 (read-only)
    P_VG,         // velocity pattern 1..72 (c34 tk.vg; a, or a + 8*b blend)
    P_COUNT
  };
  enum { kSerialVersion = 2, kSerialSize = 12 + 128 };
  enum { kMaxSteps = 128, kNoStep = -128, kWinLimit = 40 };

  Mono();
  void setContext(const SeqContext* ctx) { ctx_ = ctx; }

  void reset();
  void pad(int col, int row, bool down);  // 0-based, 12x4
  void start();                           // transport start: pc=0, pos=lstart, pg=1, fire (c31 250/251)
  void stop();                            // kill the sounding note (c31 252)
  void pulse();                           // one 24-PPQN pulse (c31 clock_pulse for this track)
  void tick();                            // framework tick (30 ms): blink counter only

  void leds(uint8_t out[kPads], uint8_t* roles = 0) const;
  int getParam(int id) const;
  void setParam(int id, int v);

  // Absolute step index 1..128 ((page-1)*16 + x). Degree or kNoStep.
  int stepAt(int absIndex) const { return (absIndex >= 1 && absIndex <= kMaxSteps) ? (int)steps_[absIndex - 1] : (int)kNoStep; }
  void setStep(int absIndex, int degree);  // degree kNoStep clears
  static int speedPulses(int spPos);       // SPD table: {96,48,32,24,16,12,8,6,4,3,2,1}

  size_t serialize(uint8_t* buf, size_t cap) const;
  bool deserialize(const uint8_t* buf, size_t len);

 private:
  void fire();
  void killNote();
  void toggleAt(int x, int row);
  void loopPad(int x, bool down);
  int degAtRow(int row) const { return win_ + (3 - row); }

  const SeqContext* ctx_;
  int8_t steps_[kMaxSteps];
  uint8_t lstart_, lend_, pos_, pc_, div_, spPos_, np_, ep_, pg_;
  int8_t win_, tpose_;
  bool mute_, ti_, half_;
  uint8_t vg_;
  int16_t playing_;
  uint8_t fc_;
  bool bl_;
  // gesture state
  bool loopHeld_;
  int8_t lpS_, lpE_;      // 0 = none
  int8_t downX_[4][8];    // step pad press memo: x (edit) or x+32 (loop gesture); 0 = not down
};

}  // namespace seqgen
