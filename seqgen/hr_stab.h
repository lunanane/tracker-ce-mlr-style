// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Ported 1:1 from HYPER_RASP (same owner) scripts/arp_lines.lua and scripts/chord_stabs.lua: two files with one layout, so one class.
//   STABS ("Chord Stabs"): a hit plays the whole chord, ten stab rhythms.   ARP ("Arp Lines"): a hit plays one note of the chord,
//   ten arpeggio figures, and a LEAD / FOLLOW role (FOLLOW: the twelve keys are the degrees of the scale another track published).
// 8 x 4 pad (x = 1..8): roots 1-12 (x 1..6, y 1..2); MAJOR / MINOR (x 1, y 3 / 4); ten patterns (x 2..6, y 3..4); eight chords (x 7..8, y 1..4).
// Pad columns 8..11 (extras, the HYPER_RASP encoders): row 0 Oct- Oct+ Tran- Tran+, row 1 Slower Faster Vel- Vel+,
// row 2 (ARP only) Lead Follow, row 3 reset Oct / Rate / Vel / Tran.
#pragma once
#include "hr.h"

namespace seqgen {

class HrStab : public HrScript {
 public:
  enum Kind : uint8_t { KIND_STABS = 0, KIND_ARP = 1 };
  enum Param { P_OCTAVE = 0, P_RATE, P_VEL, P_TRANSP, P_ROLE, P_PATTERN, P_CHORD, P_QUALITY, P_ROOT /* latched root 0..12, read-only */ };
  enum { kSerialVersion = 1, kSerialSize = 10 };
  explicit HrStab(Kind k = KIND_STABS);

  void grid(int x, int y, int z) override;
  void ext(int c, int r, bool down) override;
  void pulse() override;
  void start() override { pos_ = 0; }
  void stop() override { stopVoices(); }
  bool live() const override { return latched_ != 0; }
  bool camKey(cam::Scale& s) const override;
  void camChanged() override { publishScale(); }
  bool hasLatch() const override { return true; }
  bool latch() const override { return !hold_; }
  void setLatch(bool on) override { hold_ = on ? 0 : 1; if (!on) unlatchRoot(); }
  int getParam(int id) const override;
  void setParam(int id, int v) override;
  size_t serialize(uint8_t* buf, size_t cap) const override;
  bool deserialize(const uint8_t* buf, size_t len) override;
  void paint(uint8_t lv[32], uint8_t role[32]) const override;
  void paintExt(uint8_t lv[16]) const override;
  void legend(int col, int row, HrCell& c) const override;
  void title(char* out, size_t n) const override;
  const char* help() const override;

 private:
  struct Voice { uint8_t note; int8_t left; };
  enum { ROLE_LEADER = 1, ROLE_FOLLOWER = 2 };
  bool arp() const { return type_ == HR_ARP_LINES; }
  bool follower() const { return arp() && role_ == ROLE_FOLLOWER; }
  int patternSteps() const;
  int patternPulses() const { return patternSteps() * rate_; }
  int rootNote(int pc) const { return 60 + pc - 1 + 12 * octave_ + transp_; }
  int chordNotes(int pc, int* notes) const;  // intervals() of the chord / quality applied to a root note
  void stopVoices();
  void ageVoices();
  void trigger(int degree, int pct, int gate);
  void revoice();
  void publishScale();
  void publishRoot(int pc);
  void pressRoot(int pc);
  void unlatchRoot() { if (latched_) { stopVoices(); latched_ = 0; blink_ = 0; } }
  void pressQuality(int q);
  void pressChord(int i);
  void fireStep(int index);  // 1-based
  int stepPct(int index, int* degree, int* gate) const;  // 0 = rest
  // follower
  void scaleContext(uint8_t* scale, int* n, int* root) const;
  int chordPool(int pc, int* notes) const;
  int rootPct() const;
  int rootLevel(int pc) const;

  uint8_t latched_, quality_, chord_, pattern_, role_, blink_, rate_, vel_;
  int8_t octave_, transp_;
  uint16_t pos_;
  uint16_t published_;  // signature of the scale published last (0 = none), like the script's `published_scale`
  Voice voices_[6];
  uint8_t nv_;
  uint8_t hold_ = 0;  // Latch off: a root sounds while its pad is held
};

}  // namespace seqgen
