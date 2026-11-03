// seqgen - pure C++ sequencer engines (Chord/Arp and Mono) ported from the owner's grid-composer c31.
// No platform includes, no heap, no exceptions/RTTI, float-free (integer maths only).
// License: to be chosen by the owner (see README.md).
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace seqgen {

enum { kCols = 12, kRows = 4, kPads = 48 };  // pad (col,row) is 0-based, row 0 = top row; index = row*12+col
enum { kPPQN = 24, kBarPulses = 96 };        // pulse() is called at 24 PPQN; one bar = 96 pulses (4/4)
enum { kTickMs = 30 };                       // tick() is the c31 framework tick (update(), ~30 ms)

enum EventKind : uint8_t { EV_NOTE_OFF = 0, EV_NOTE_ON = 1 };

// What a sequencer emits. chan = MIDI channel (1-based, as c31) or the host's voice/track index.
// gateMs (note-on only) is informational: the gate the engine itself will apply through tick();
// 0 = sounds until the next note / chord release / legato. A host that schedules its own note-offs
// can turn the internal gate off (setInternalGate(false)) and use gateMs instead.
struct Event {
  uint8_t kind;
  uint8_t chan;
  uint8_t note;
  uint8_t vel;
  uint16_t gateMs;
};
typedef void (*EventSink)(void* user, const Event& e);

// Role of a pad for the host's colour mapping (brightness comes separately as level 0..15).
enum LedRole : uint8_t {
  ROLE_NONE = 0,
  ROLE_ROOT_MAJOR,  // chord root, major row
  ROLE_ROOT_MINOR,  // chord root, minor row
  ROLE_DEGREE,      // grid play mode: scale-degree pad
  ROLE_PANEL,       // 4x4 panel pad (types / settings / groove / param)
  ROLE_CONTROL,     // button-like control pad
  ROLE_STEP,        // mono step pad (note or empty cell)
  ROLE_PLAYHEAD,    // mono: cell on the playing step
  ROLE_LOOP         // mono: cell inside the loop region
};

inline int floordiv(int a, int b) { int q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) --q; return q; }
inline int floormod(int a, int b) { return a - floordiv(a, b) * b; }  // Lua % semantics (b > 0)
inline int iabs(int a) { return a < 0 ? -a : a; }

// Shared emit helper.
class SeqBase {
 public:
  void setSink(EventSink fn, void* user) { sink_ = fn; user_ = user; }
  void setChannel(uint8_t ch) { ch_ = ch; }
  uint8_t channel() const { return ch_; }

 protected:
  SeqBase() : sink_(0), user_(0), ch_(1) {}
  void emit(uint8_t kind, int note, int vel, unsigned gateMs = 0) const {
    if (!sink_ || note < 0 || note > 127) return;
    Event e;
    e.kind = kind; e.chan = ch_; e.note = (uint8_t)note; e.vel = (uint8_t)vel; e.gateMs = (uint16_t)gateMs;
    sink_(user_, e);
  }
  EventSink sink_;
  void* user_;
  uint8_t ch_;
};

}  // namespace seqgen
