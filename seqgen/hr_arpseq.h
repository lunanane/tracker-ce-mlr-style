// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Ported 1:1 from HYPER_RASP (same owner) scripts/arp_seq.lua: the one-page arp / chord sequencer.
// 8 x 4 pad (x = 1..8): roots 1-12 (x 1..6, y 1..2); MAJOR/MINOR toggle (1,3); five cycle buttons (2..6, 3: mode, speed, direction, groove,
// octave); three chord keys (7, 1..3) whose overlapping hold picks one of 15 chord types; PLAY (8,3); eight sequencer steps (x 1..8, y 4):
// tap = store the current setting, hold two = loop range. The script has no encoder parameters, so pad columns 8..11 are unused.
#pragma once
#include "hr.h"

namespace seqgen {

class HrArpSeq : public HrScript {
 public:
  enum Param { P_ROOT = 0, P_QUALITY, P_CHORD_TYPE, P_MODE, P_SPEED, P_DIR, P_GROOVE, P_OCT, P_PLAYING, P_START, P_END, P_CURRENT };
  enum { kSerialVersion = 1, kSerialSize = 78 };
  HrArpSeq();

  void grid(int x, int y, int z) override;
  void pulse() override;
  void tick() override;
  void stop() override { stopSounding(); }
  bool live() const override { return latched_ != 0 || playing_; }
  bool hasLatch() const override { return true; }
  bool latch() const override { return !hold_; }
  void setLatch(bool on) override { hold_ = on ? 0 : 1; if (!on && manual_) unlatchRoot(); }
  int getParam(int id) const override;
  void setParam(int id, int v) override;
  size_t serialize(uint8_t* buf, size_t cap) const override;
  bool deserialize(const uint8_t* buf, size_t len) override;
  void paint(uint8_t lv[32], uint8_t role[32]) const override;
  void legend(int col, int row, HrCell& c) const override;
  void title(char* out, size_t n) const override;
  const char* help() const override;

 private:
  struct Step {  // seq_steps[col]: root = 0 is a stored rest
    uint8_t used, root, quality, ctype, seqN, seq[3], cyc[5];
  };
  enum { MODE = 0, SPEED, DIRECTION, GROOVE, OCTAVE };
  const int8_t* chordIntervals(int* n) const;
  int velocityNow() const;
  void stopSounding();
  void soundLatched();
  void pressRoot(int pc);
  void unlatchRoot() { stopSounding(); latched_ = 0; manual_ = 0; poolN_ = 0; }
  void toggleQuality();
  void pressCycle(int c);
  void selectChord(int key);
  void captureStep(int col);
  void applyStep(const Step& s);
  void setPlay(bool running);
  void advanceArp();
  void advanceSequencer();
  int arpNoteAt(int direction, int step) const;
  int chordPosition(int key) const;

  uint8_t latched_, manual_, quality_, ctype_, seqN_, seq_[3], chordDown_, cyc_[5];
  uint8_t poolN_, soundN_;
  int16_t pool_[5];
  uint8_t sounding_[5];
  uint16_t arpStep_, arpPulse_, seqPulse_;
  uint8_t phase_, fade_;
  Step steps_[8];
  uint8_t start_, end_, current_, playing_;
  uint8_t heldSteps_, gestureN_, gFirst_, gLo_, gHi_;
  uint8_t hold_ = 0;  // Latch off: a root sounds while its pad is held
};

}  // namespace seqgen
