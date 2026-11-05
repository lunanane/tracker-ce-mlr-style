// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Ported 1:1 from HYPER_RASP (same owner) scripts/chord_arp.lua ("HR Chord/Arp": a different script from the grid-composer Chord/Arp,
// ChordArp). Four views of the 8 x 4 pad, switched by the function column (x = 8, the HYPER_RASP host's ViewSwitcher, ported here too):
//   view 1  roots (x 1..6, y 1..4) + chord-type column (x 7: four keys, hold two for the six combo types)
//   view 2  arp direction / speed / mode / key lock / size rows, octave (x 5) and inversion (x 6) columns
//   view 3  gate patterns (x 1..4) and the velocity-groove radio (x 5..7)
//   view 4  the 8-step chord tracker (x 1..4 = step of the page, y = duration), page keys (x 5, y 1..2), clear key (x 6, y 1)
// Function column: a tap on one key = view y; keys 3+4 together = tracker page toggle; keys 1+2 together = tracker play / stop.
// (the third HYPER_RASP combo, keys 1+3 = mute, is the host's track mute: not here - the Tracker has its own.)
// Pad columns 8..11 (extras, the HYPER_RASP encoders/faders that no view puts on a pad): row 0 Vel- Vel+ Tran- Tran+, row 1 Key- Key+ Scale
// Latch, row 2 Start Play Page -, row 3 View 1..4 (direct).
#pragma once
#include "hr.h"

namespace seqgen {

class HrChordArp : public HrScript {
 public:
  // set_param names: type size octave inv mode speed dir groove vel gate transp qstart keylock key scale latch
  enum Param { P_TYPE = 0, P_SIZE, P_OCTAVE, P_INV, P_MODE, P_SPEED, P_DIR, P_GROOVE, P_VEL, P_GATE, P_TRANSP, P_QSTART, P_KEYLOCK, P_KEY, P_SCALE, P_LATCH,
               P_VIEW, P_PAGE, P_PLAYING, P_ROOT_X, P_ROOT_Y };
  enum { kSerialVersion = 1, kSerialSize = 44 };
  HrChordArp();

  void grid(int x, int y, int z) override;
  void ext(int c, int r, bool down) override;
  void pulse() override;
  void start() override { startPlayback(); }   // start_playback: the tracker (arranger) plays
  void stop() override { stopPlayback(); }     // stop_playback
  void transportStopped() override { hostRunning_ = 0; }  // the Tracker transport stopped: the next 1+2 combo starts the arranger again
  bool followsTransport() const override { return false; }  // no FOLLOWS_TRANSPORT in chord_arp.lua: the tracker has its own play combo
  void silence() override;
  bool live() const override { return hasRoot_ || arpActive_ || nActive_ > 0 || playing_ || nPlayback_ > 0; }
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
  struct Step { uint8_t used, type, dur; uint8_t note; };  // steps[i]: root = MIDI note (0..127), chord type 1..10, duration 1..4
  int lockRoot() const { return 60 + lockKey_; }
  int noteToDegree(int note) const;
  int degreeToNote(int d) const;
  int buildChord(int root, int type, int* out) const;           // build_chord: + transpose, key lock
  int applyModifiers(int* notes, int n) const;                   // size, octave, inversion
  int arpNoteAt(int direction, int step) const;                  // -1 = none
  int velocity() const { return hrGrooveVel(vg_, phase_, baseVel_); }
  bool gateOpen() const;
  void stopActiveChord();
  void stopArp();
  void stopActiveRoot();
  void soundRoot(int note);
  void retriggerLatched();
  void selectType(int type, int k1, int k2);
  void applyPendingType();
  void applyPendingRoot();
  void handleModKey(int y, bool pressed);
  void view1(int x, int y, int z);
  void view2(int x, int y, int z);
  void view3(int x, int y, int z);
  void view4(int x, int y, int z);
  void stopPlaybackNotes();
  void playCurrentStep();
  void startPlayback();
  void stopPlayback();
  void publishKeyLock();
  void setView(int n);
  void setPage(int n);
  void setPlayback(bool running);
  void render();
  void doParam(int id, int n, bool fromPad);
  void advanceArp();
  uint8_t keyLevel(int y) const;

  // settings
  uint8_t type_, size_, mode_, speed_, dir_, vg_, gate_, qstart_, keylock_, lockKey_, lockMinor_, latched_, baseVel_;
  int8_t octave_, inv_, transp_;
  // runtime
  uint8_t view_, page_, clearHeld_, playing_, hostRunning_;
  uint8_t phase_, arpActive_, arpStep_, arpPulse_, nPool_, nActive_, nPlayback_;
  int arpNote_;
  int16_t pool_[6];
  int16_t active_[6], playback_[6];
  uint8_t hasRoot_, rootX_, rootY_;        // active_root
  uint8_t hasPendingRoot_, pendX_, pendY_;
  uint8_t hasPendingType_, pendType_, pendK1_, pendK2_;
  uint8_t selK1_, selK2_;                  // selected_mod_key: one key or a pair (K2 = 0 when single)
  uint8_t modHeld_, modFired_;             // bit y-1 per held mod key
  uint8_t hasLastRoot_, lastRoot_;
  uint8_t playIndex_;
  uint16_t playPulse_;
  uint32_t pulseIndex_;
  uint16_t published_;                     // signature of the scale published last (0 = free / none yet "free")
  Step steps_[8];
  uint8_t swHeld_, swFired_;               // the function column (ViewSwitcher): held rows bitmask
  uint8_t led_[32], role_[32];             // what render() staged: the HYPER_RASP host shows the last render
};

}  // namespace seqgen
