// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Ported 1:1 from HYPER_RASP (same owner) scripts/euclid_trigger.lua: four drum rows of Euclidean rhythms.
// 8 x 4 pad (x = 1..8): row y = one drum voice of the current kit grouping; x 1..7 = presets E(1,8)..E(7,8) (tap the lit one again: the row
// is switched off), x 8 = RECORD (hold: the preset steps are stored, up to 8 chains of 8 steps, per row). Steps are sixteenths (6 pulses).
// Pad columns 8..11 (extras, the script's encoder params): row 0 Grp- Grp+ Vel- Vel+, row 1 Chance- Chance+ Swing- Swing+,
// row 3 reset Grp 1 / Vel 100 / Chance 100 / Swing 0.
// math.random is replaced by a fixed linear-congruential generator (hrRandom100, on both sides of the differential test).
#pragma once
#include "hr.h"

namespace seqgen {

class HrEuclid : public HrScript {
 public:
  enum Param { P_GROUPING = 0, P_VEL, P_SWING, P_CHANCE, P_NOTE1, P_NOTE2, P_NOTE3, P_NOTE4, P_PRESET1 /* + row: live preset of the row, read-only */ };
  enum { kSerialVersion = 1, kSerialSize = 45 };
  HrEuclid();

  void grid(int x, int y, int z) override;
  void ext(int c, int r, bool down) override;
  void pulse() override;
  void start() override { startPlayback(); }
  void stop() override { stopPlayback(); }
  void silence() override { stopPlayback(); stopAll(); }
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
  enum { kRows = 4, kChain = 8, kMaxChains = 8 };
  void applyGrouping(int index);  // 1..8
  bool onset(int row, int idx) const;
  int activeLength(int row) const { return live_[row] ? kChain : chains_[row] * kChain; }
  int stepIndex(int row) const { return (int)(stepTick_ % (uint32_t)activeLength(row)) + 1; }
  void stopRow(int row);
  void stopAll();
  bool rollsUnder(int percent);
  void playRow(int row);
  void advanceRecording(int row);
  int bgLevel(int row) const;
  void startPlayback();
  void stopPlayback();

  uint8_t grouping_, vel_, swing_, chance_;
  uint8_t notes_[kRows];
  uint8_t playing_, pulseCount_;
  uint32_t stepTick_;
  uint8_t live_[kRows];  // live preset 1..7, 0 = none
  uint8_t off_[kRows];
  uint8_t stored_[kRows][kMaxChains * kChain / 8];
  uint8_t chains_[kRows];
  uint8_t recording_[kRows], recChain_[kRows], recStep_[kRows];
  int16_t sounding_[kRows];
  uint32_t rng_;
};

}  // namespace seqgen
