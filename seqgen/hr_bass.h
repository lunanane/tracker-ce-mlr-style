// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Ported 1:1 from HYPER_RASP (same owner) scripts/bassline_seq.lua: the mono / bass sequencer.
// 8 x 4 pad (x = 1..8): rows 1..3 = a 24-key scale keyboard (24 consecutive degrees of the shared scale from the tonic at or below the shared
// root, row 3 lowest; held keys are walked in PRESS order); row 4 = Euclidean presets E(1,8)..E(7,8) (x 1..7) and RECORD (x 8, hold: the
// preset steps are stored, up to 8 chains of 8 steps). The Euclidean row decides WHEN, the held keys decide WHICH note.
// Pad columns 8..11 (extras, the script's encoder params): row 0 Oct- Oct+ Dir- Dir+, row 1 Groove- Groove+, row 3 reset Oct / Dir / Groove.
#pragma once
#include "hr.h"

namespace seqgen {

class HrBass : public HrScript {
 public:
  enum Param { P_OCTAVE = 0, P_DIR, P_GROOVE, P_PRESET /* live preset 0..7, read-only */, P_CHAINS /* stored chains, read-only */ };
  enum { kSerialVersion = 1, kSerialSize = 13 };
  HrBass();

  void grid(int x, int y, int z) override;
  void ext(int c, int r, bool down) override;
  void pulse() override;
  void start() override { startPlayback(); }
  void stop() override { stopPlayback(); }
  void silence() override { stopPlayback(); stopSounding(); }
  bool camKey(cam::Scale& s) const override { return busKeyScale(s); }
  bool live() const override { return heldN_ != 0 || sounding_ >= 0; }  // the keys decide WHICH note: nothing held = nothing to play
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
  enum { kKeys = 24, kChain = 8, kMaxChains = 8 };
  int noteForKey(int index) const;  // -1 = outside MIDI
  bool isRootKey(int index) const;
  int buildPool(int16_t* pool) const;
  bool onset(int idx) const;  // 1-based step of the active pattern
  int activeLength() const { return livePreset_ ? kChain : storedChains_ * kChain; }
  int stepIndex() const { return (int)(stepTick_ % (uint32_t)activeLength()) + 1; }
  void stopSounding();
  void playStep();
  void advanceRecording();
  void startPlayback();
  void stopPlayback();
  int bgLevel() const;
  bool isHeld(int index) const;

  int8_t octave_;
  uint8_t dir_, groove_;
  uint8_t held_[kKeys];
  uint8_t heldN_;
  uint8_t playing_, pulseCount_, livePreset_;  // livePreset_ 0 = play the stored pattern
  uint32_t stepTick_;
  uint8_t stored_[kMaxChains * kChain / 8];     // bit i = step i (0-based) is an onset
  uint8_t storedChains_;
  uint8_t recording_, recChain_, recStep_;
  int16_t sounding_;
  uint16_t arpStep_;
  uint8_t phase_;
};

}  // namespace seqgen
