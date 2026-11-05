// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Ported 1:1 from HYPER_RASP (same owner) scripts/grid_seq.lua: the 4-row drum step sequencer with ratchets.
// 8 x 4 pad (x = 1..8): row y = one drum voice of the kit grouping, x = step. Tap a cell to toggle the step; hold a cell (or several cells of a
// row: the spread picks the ratchet speed) to retrigger that row's voice from the leftmost held step (a hold that ratcheted does not edit).
// Steps run at the div (default 6 pulses = sixteenths) through the loop range.
// Pad columns 8..11 (extras, the script's encoder params): row 0 Grp- Grp+ Vel- Vel+, row 1 Div- Div+ Tran- Tran+, row 2 Lo- Lo+ Hi- Hi+
// (the loop range), row 3 reset Grp 1 / Vel 100 / Div 1/16 / Tran 0.
// The script's "chance" parameter is stored but never used by grid_seq.lua, so it is not ported.
#pragma once
#include "hr.h"

namespace seqgen {

class HrGridSeq : public HrScript {
 public:
  enum Param { P_GROUPING = 0, P_VEL, P_TRANSP, P_DIV, P_RANGE /* lo * 10 + hi, as the script's range */, P_NOTE1, P_NOTE2, P_NOTE3, P_NOTE4, P_COLUMN /* play column 1..8, read-only */,
               P_START, P_END /* one end of the range */ };
  enum { kSerialVersion = 1, kSerialSize = 15 };
  HrGridSeq();

  void grid(int x, int y, int z) override;
  void ext(int c, int r, bool down) override;
  void pulse() override;
  void start() override { startPlayback(); }
  void stop() override { stopPlayback(); }
  void silence() override { stopPlayback(); stopSounding(); }
  bool live() const override;
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
  enum { kRows = 4, kSteps = 8 };
  void applyGrouping(int index);
  void stopSounding();
  void stopRow(int row);
  void playRowNote(int row, int col);  // col 1..8
  void playColumn(int col);
  int ratchetDivision(int spread) const;
  void recalcRatchet(int row);
  void startPlayback();
  void stopPlayback();

  uint8_t grouping_, vel_, div_;  // div_ = pulses per step
  int8_t transp_;
  uint8_t voice_[kRows];          // the grouping's notes (DRUM_VOICE_NOTES)
  int16_t note_[kRows];           // TRACK_NOTES (voice + transpose, or a note param)
  uint8_t steps_[kRows];          // bit c-1 = step c
  uint8_t playing_, col_, pulseCount_, start_, end_;
  int16_t sounding_[kRows];
  uint8_t held_[kRows];           // bit c-1
  uint8_t anchor_[kRows];         // 0 = none
  uint8_t division_[kRows];
  int8_t spread_[kRows];          // -1 = none
  uint8_t rowPulse_[kRows], grace_[kRows], ratcheted_[kRows], multi_[kRows];
};

}  // namespace seqgen
