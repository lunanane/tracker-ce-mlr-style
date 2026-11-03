// Chord/Arp sequencer: owner's grid-composer c31 page 3 (lines 237-708) without the arranger/tracker part.
#pragma once
#include "seqgen.h"

namespace seqgen {

class ChordArp;

// Shared between sequencers: the scale and the velocity groove come from the Chord/Arp instance (if any).
// Without one: C major and flat velocity.  gtpose = c31 pages[4].gtpose (global transpose, scale degrees).
struct SeqContext {
  const ChordArp* chord;
  int8_t gtpose;
  SeqContext() : chord(0), gtpose(0) {}
  void scale(int& root, bool& minor) const;  // folded tonic (54..65) and minor flag
  int scaleDeg(int d) const;                 // c31 scale_deg
  int vgv(int i, int base) const;            // c31 vgv
};

class ChordArp : public SeqBase {
 public:
  enum Panel { PANEL_TYPES = 0, PANEL_SETTINGS, PANEL_GROOVE, PANEL_PARAM };
  // Screen / encoder items. Setters act immediately (pads queue to the next quarter/bar like c31).
  enum Param {
    P_PMODE = 0,   // 1 key, 2 locked, 3 grid (c31 pmode)
    P_LOCK_ROOT,   // midi note of the lock tonic (lock_r)
    P_LOCK_MINOR,  // 0/1
    P_SWING,       // 0..2 pulses
    P_GATE,        // 0 normal, 1 short, 2 legato
    P_SEL_TYPE,    // 1 chord, 2 arp, 3 arp+oct, 4 off
    P_SEL_SPEED,   // 1 1/8, 2 1/8T, 3 1/16, 4 1/16T
    P_SEL_DIR,     // 1 up, 2 down, 3 up-down, 4 pattern
    P_SEL_SIZE,    // 5,4,3,2
    P_SEL_GROOVE,  // 1..16 (glib)
    P_SEL_MOD,     // chord type 1..15
    P_KOCT,        // -1..1
    P_INV,         // -6..6
    P_PON,         // parametric groove on/off
    P_PARCH,       // archetype 1..4
    P_PW0, P_PW1, P_PW2,  // tier weights downbeat / 8th / 16th, 0..3
    P_PSEED,       // groove seed
    P_TPOSE,       // arp_tpose (scale degrees)
    P_MUTE,        // amute
    P_PANEL,       // visible panel (Panel)
    P_Q_TYPE, P_Q_SPEED, P_Q_DIR, P_Q_SIZE, P_Q_GROOVE,  // queued values (0 = none), read-only
    P_Q_MOD,       // queued chord type (0 = none), read-only
    P_Q_ROOT,      // 1 when a root/chord waits for the next quarter, read-only
    P_COUNT
  };
  enum { kSerialVersion = 1, kSerialSize = 28 };

  ChordArp();
  void setContext(SeqContext* ctx) { ctx_ = ctx; if (ctx) ctx->chord = this; }
  void setInternalGate(bool on) { internalGate_ = on; }

  void reset();                         // power-on state
  void pad(int col, int row, bool down);  // 0-based, 12x4
  void lockButton(bool down);           // LCK: down = locked mode + root press sets the lock; up = release
  void start();                         // transport start/continue (c31 250/251)
  void stop();                          // transport stop (c31 252): all live notes off
  void pulse();                         // one 24-PPQN clock pulse
  void tick();                          // one framework tick (30 ms): blink, pulses, gate timer
  void advanceMs(uint32_t dtMs);        // convenience: accumulates dt and calls tick() per 30 ms
  void setBarPos(uint8_t pulsesIntoBar) { meas_ = pulsesIntoBar % kBarPulses; }  // host sync, 0..95

  void leds(uint8_t out[kPads], uint8_t* roles = 0) const;
  int getParam(int id) const;
  void setParam(int id, int v);

  size_t serialize(uint8_t* buf, size_t cap) const;       // returns bytes written (0 = buffer too small)
  bool deserialize(const uint8_t* buf, size_t len);

  // Read-only helpers (also used by Mono through SeqContext and by tests).
  void scaleSet(int& root, bool& minor) const;
  int scaleDeg(int d) const;
  int vgv(int i, int base) const;
  int pool(int rootNote, int mod, bool minor, int16_t out[8]) const;  // c_pool
  bool gbit(int g, int i) const;
  int gstep(int sp) const;              // 1-based step due on this pulse, or 0
  int pulsesIntoBar() const { return meas_; }
  int groovePulseLevel() const { return pulse_; }  // slow settings breath 8..15
  int notePulseLevel() const { return pn_; }      // fast eased note pulse 5..15
  bool blinkState() const { return blink_; }
  static const char* chordTypeName(int mod);

 private:
  struct RootRef { int8_t x, y; bool valid; };
  struct QRoot { bool valid; int16_t rv; uint8_t t, s, m; bool mn; int8_t cx, cy; };

  void gridRootEvent(int x, int y, bool down);
  void panelEvent(int pc, int pr, bool down);
  void controlEvent(int idx, bool down);
  void pgen();
  int lkd(int d) const;
  int dlk(int note) const;
  int rv(const RootRef& r) const;
  int arp(const int16_t* p, int n, int d, int st, bool alt) const;
  void killLive();
  void fl(const int16_t* p, int n);
  int liveAt(int rv, bool mn, int16_t* out) const;
  void tickLive(int rootNote);
  void applyQuarter();
  void barTick();
  void noteOnLive(int note, int vel, unsigned gateMs);
  unsigned gateTicks() const;
  void lockTransition(int mode, bool hold);

  SeqContext* ctx_;
  bool internalGate_;
  uint8_t selMod_, selSize_, selT_, selS_, selD_, selG_, sw_, glen_, pmode_, parch_, panel_;
  bool pon_;
  uint8_t PW_[3];
  uint16_t pmask_;
  uint8_t pvt_[16];
  int32_t pseed_;
  uint8_t qT_, qS_, qD_, qSize_, qG_, qMod_;
  QRoot ql_;
  RootRef last_;
  int16_t lockR_;
  bool lockMn_, centerHeld_, amute_;
  int8_t koct_, inv_, arpTpose_;
  uint32_t lAptr_;
  uint8_t lGptr_;
  int16_t llArp_;
  uint8_t lgate_;
  bool lN_[128];
  int16_t cpool3_[24];
  uint8_t ncpool3_;
  uint8_t meas_, fc_;
  bool blink_;
  uint8_t pulse_, pn_;
  bool held_[4];  // INV-, INV+, OCT-, OCT+
  uint32_t msAcc_;
};

}  // namespace seqgen
