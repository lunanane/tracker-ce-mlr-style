// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// HYPER_RASP pad-script adapter: the thin C++ counterpart of HYPER_RASP's PadScript API (host/src/hyper_host/pad_script.py).
// The scripts of E:\claude\_HYPER_RASP\scripts (same owner) are ported 1:1 onto it: event_grid(x, y, z), event_midi(248) clock
// pulses, start_playback / stop_playback, framework_tick, set_param, midi_note_on/off, grid_led, the shared scale / root bus
// (set_scale, current_scale, set_root_note, current_root_note) and euclidean(k, n).
// No Polyend includes, no heap, no exceptions/RTTI, integer maths only.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "seqgen.h"

namespace seqgen {

enum { kHrCols = 8, kHrRows = 4 };  // the HYPER_RASP pad is 8 x 4 = the pad columns 0..7 of the 12 x 4 Tracker pads; cols 8..11 are "extras"

// Script types, in the order of the type list (after Off, Chord/Arp, Mono). Only ported scripts are listed.
enum HrType : uint8_t { HR_ARP_LINES = 0, HR_CHORD_STABS, HR_ARP_SEQ, HR_CHORD_ARP, HR_BASS, HR_EUCLID, HR_GRIDSEQ, HR_STEPFOLLOW, HR_COUNT };

// What the HYPER_RASP host shares between tracks: the scale (pitch classes, tonic first; none = no scale in force) and the root note
// (set_root_note). Defaults of pad_script.py: DEFAULT_SCALE = D# natural minor, no root.
struct HrBus {
  uint8_t scale[12];
  uint8_t nScale;  // 0 = none
  int16_t root;    // -1 = nothing published
  HrBus() { reset(); }
  void reset() {
    static const uint8_t kDefault[7] = {3, 5, 6, 8, 10, 11, 1};
    for (int i = 0; i < 7; ++i) scale[i] = kDefault[i];
    nScale = 7;
    root = -1;
  }
  void setScale(const uint8_t* pc, int n) {
    nScale = (uint8_t)(n > 12 ? 12 : n);
    for (int i = 0; i < nScale; ++i) scale[i] = pc[i];
  }
};

// Shift-legend cell. zone numbers equal seqplay::Zone (checked in core/source/seqPlayLogic.h).
enum HrZone : uint8_t { HZ_NONE = 0, HZ_NOTE = 1, HZ_STEP = 2, HZ_PANEL = 3, HZ_MODE = 4, HZ_ADJ = 5, HZ_ACTION = 6, HZ_HOLD = 7 };
struct HrCell {
  uint8_t zone;
  bool on;      // the pad that is active now
  char txt[8];  // up to 6 characters, ASCII
  void clear() { zone = HZ_NONE; on = false; for (int i = 0; i < 8; ++i) txt[i] = 0; }
  void set(uint8_t z, const char* t, bool isOn = false) {
    zone = z; on = isOn;
    for (int i = 0; i < 8; ++i) txt[i] = 0;
    for (int i = 0; i < 6 && t[i]; ++i) txt[i] = t[i];
  }
};

// Base class of the ported scripts. One instance = one script on one track.
class HrScript : public SeqBase {
 public:
  void setBus(HrBus* b) { bus_ = b; }
  uint8_t hrType() const { return type_; }

  // ---- the Tracker side (12 x 4 pads, 0-based, row 0 = top) ----
  void pad(int col, int row, bool down) {
    if (col < 0 || row < 0 || row >= kHrRows || col >= kCols) return;
    if (col < kHrCols) grid(col + 1, row + 1, down ? 1 : 0);
    else ext(col - kHrCols, row, down);
  }
  // levels 0..15 per pad (index = row*12 + col) and a LedRole per pad
  void leds(uint8_t out[kPads], uint8_t* roles = 0) const;

  // ---- HYPER_RASP callbacks (names as in the Lua scripts) ----
  virtual void grid(int x, int y, int z) = 0;  // event_grid, 1-based
  virtual void ext(int c, int r, bool down) { (void)c; (void)r; (void)down; }  // extras: pad columns 8..11 -> c 0..3
  virtual void pulse() = 0;                    // event_midi(248, ...): 24 PPQN clock pulse
  virtual void start() {}                      // start_playback
  virtual void stop() {}                       // stop_playback
  virtual void transportStopped() {}           // the Tracker transport stopped (called after stop(), for every script; host-side state only)
  virtual bool followsTransport() const { return true; }  // FOLLOWS_TRANSPORT: the Tracker transport calls start() / stop()
  virtual void silence() { stop(); }           // the track changes type / is cleared: end everything this script sounds
  virtual void tick() {}                       // framework_tick (30 ms)
  virtual bool live() const = 0;               // the script is performing (latched / running / sounding): keeps the track taken over
  virtual int getParam(int id) const { (void)id; return 0; }
  virtual void setParam(int id, int v) { (void)id; (void)v; }
  virtual size_t serialize(uint8_t* buf, size_t cap) const = 0;  // settings only, version byte first, returns 0 if cap too small
  virtual bool deserialize(const uint8_t* buf, size_t len) = 0;

  // ---- display ----
  virtual void paint(uint8_t lv[32], uint8_t role[32]) const = 0;  // the 8 x 4 pad, index = (y-1)*8 + (x-1)
  virtual void paintExt(uint8_t lv[16]) const;                      // extras, index = row*4 + c
  virtual void legend(int col, int row, HrCell& c) const = 0;       // Shift legend (12 x 4)
  virtual void title(char* out, size_t n) const = 0;                // caption line 1 (state)
  virtual const char* help() const = 0;                             // caption line 2 (how to play, <= 88 characters)

  enum { kMaxSerial = 96 };  // upper bound of any script serialized settings

 protected:
  explicit HrScript(uint8_t type) : type_(type), bus_(0) {}
  void noteOn(int note, int vel) const { emit(EV_NOTE_ON, note, vel, 0); }  // midi_note_on
  void noteOff(int note) const { emit(EV_NOTE_OFF, note, 0, 0); }          // midi_note_off
  const HrBus& bus() const { static const HrBus none; return bus_ ? *bus_ : none; }
  uint8_t type_;
  HrBus* bus_;
};

// ---- shared helpers (hr.cpp) ----
// euclidean(k, n): n flags (1 where a pulse falls), pad_script._bjorklund (rotated to the first onset); n <= 128
void hrEuclid(int k, int n, uint8_t* out);
// math.floor(base * pct / 100 + 0.5) clamped to 1..127 (the scripts' level * velocity); pct 1..100
int hrScaleVel(int base, int pct);
// velocity_for_groove(g, i, base) of arp_seq / chord_arp (g 1 = flat; g 2..13 = 12 patterns), math.floor(76 + 32 * shape + 0.5)
int hrGrooveVel(int g, int i, int base);
// HYPER_RASP rate option label for a pulse count per step ("1/4", "1/16" ...)
const char* hrRateName(int pulses);
// the "next" rate option (24 12 8 6 4 3 2 1 pulses per step): dir > 0 faster, < 0 slower
int hrRateStep(int pulses, int dir);
const char* hrNoteName(int pc0);  // "C" "C#" ... for 0..11
// snprintf at out + at (a caption is built from pieces); returns the new length
int hrAppend(char* out, size_t n, int at, const char* fmt, ...);

// ---- storage for one script per track (no heap): placement-new into a buffer, picked by type ----
class HrSlot {
 public:
  enum { kBytes = 400 };  // >= sizeof of every script (static_assert in hr.cpp)
  HrSlot() : s_(0) {}
  HrSlot(const HrSlot&) = delete;
  HrSlot& operator=(const HrSlot&) = delete;
  HrScript* get() const { return s_; }
  HrScript* create(uint8_t hrType);  // destroys nothing (scripts are trivially destructible); 0 for an unknown type
  void clear() { s_ = 0; }
 private:
  alignas(8) uint8_t buf_[kBytes];
  HrScript* s_;
};

}  // namespace seqgen
