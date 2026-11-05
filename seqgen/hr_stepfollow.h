// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Ported 1:1 from HYPER_RASP (same owner) scripts/step_follower.lua: an 8-step melodic step sequencer in SCALE DEGREES that follows the
// scale and the root another track published (no scale = chromatic, no root = 60).
// 8 x 4 pad (x = 1..8): a window of 4 degrees (row 4 = the window base, row 1 = base + 3) x 8 steps; tap a cell to light / unlight the step.
// The two nav pads x = 8, y = 3 / 4 page the window up / down (two degrees, as far as the notes stay within 12..120). Tonic rows use the
// root colour. A step with several lit degrees plays them together; notes outside 0..127 are dropped.
// Pad columns 8..11 (extras, the script's params): row 0 Oct- Oct+ Vel- Vel+, row 1 Div- Div+ Lo- Lo+, row 2 Hi- Hi+, row 3 reset Oct 0 /
// Vel 100 / Div 1/16 / the whole loop. At most 24 distinct degrees are kept (the script keeps any number).
#pragma once
#include "hr.h"

namespace seqgen {

class HrStepFollow : public HrScript {
 public:
  enum Param { P_DIV = 0, P_VEL, P_OCTAVE, P_RANGE /* lo * 10 + hi, as the script's range */, P_BASE /* window base degree, read-only */, P_COLUMN /* play column, read-only */,
               P_START, P_END /* one end of the range */ };
  enum { kMaxDegrees = 24, kSerialVersion = 1, kSerialSize = 9 + kMaxDegrees * 3 };
  HrStepFollow();

  void grid(int x, int y, int z) override;
  void ext(int c, int r, bool down) override;
  void pulse() override;
  void start() override { startPlayback(); }
  void stop() override { stopPlayback(); }
  void silence() override { stopPlayback(); stopSounding(); }
  bool live() const override { return playing_ || nSounding_ != 0; }
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
  enum { kSteps = 8, kNoteRows = 4, kMinNote = 12, kMaxNote = 120 };
  struct Lit { int16_t degree; uint8_t mask; };  // mask bit c-1 = step c; entries kept sorted by degree, mask never 0
  void context(const uint8_t** scale, int* n, int* root) const;
  int noteForDegree(int degree) const;
  bool isTonic(int degree) const;
  bool windowFits(int base) const;
  bool isLit(int degree, int col) const;
  void toggle(int degree, int col);
  void stopSounding();
  void playColumnNotes(int col);
  void page(int delta);
  void startPlayback();
  void stopPlayback();

  uint8_t div_, vel_, start_, end_, col_, pulseCount_, playing_;
  int8_t octave_;
  int16_t base_;
  uint8_t nLit_, nSounding_;
  Lit lit_[kMaxDegrees];
  uint8_t sounding_[kMaxDegrees];
};

}  // namespace seqgen
