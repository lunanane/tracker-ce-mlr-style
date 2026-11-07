// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// HYPER_RASP pad-script adapter: shared helpers and the per-track script slot. See hr.h.
#include "hr.h"

#include <new>
#include <stdarg.h>
#include <stdio.h>

#include "hr_scripts.h"

namespace seqgen {

// ---------------------------------------------------------------- euclidean(k, n): pad_script._bjorklund
namespace {
struct Bj {
  uint8_t counts[40], rem[40];
  uint8_t* out;
  int n;
  // Expands level lvl into out[]. Non-recursive (an explicit stack of at most 42 small frames, ~90 bytes): this runs per step in the
  // audio interrupt, where the recursive form used ~0.9 KB of the 4 KB stack. Same order of output as the recursive original:
  //   build(l) = { build(l-1) x counts[l] ; if rem[l] != 0: build(l-2) },  build(0) = 0,  build(-1) = 1.
  void build(int lvl) {
    int8_t fl[42];
    uint8_t fi[42];
    int sp = 0;
    fl[0] = (int8_t)lvl; fi[0] = 0; sp = 1;
    while (sp > 0) {
      int top = sp - 1, l = fl[top];
      if (l == 0) { out[n++] = 0; --sp; }
      else if (l == -1) { out[n++] = 1; --sp; }
      else if (fi[top] < counts[l]) { ++fi[top]; fl[sp] = (int8_t)(l - 1); fi[sp] = 0; ++sp; }
      else if (fi[top] == counts[l]) { ++fi[top]; if (rem[l] != 0) { fl[sp] = (int8_t)(l - 2); fi[sp] = 0; ++sp; } }
      else --sp;
    }
  }
};
}  // namespace

void hrEuclid(int k, int n, uint8_t* out) {
  if (n <= 0) return;
  if (n > 128) n = 128;
  if (k <= 0) { for (int i = 0; i < n; ++i) out[i] = 0; return; }
  if (k >= n) { for (int i = 0; i < n; ++i) out[i] = 1; return; }
  Bj b;
  uint8_t raw[128];
  b.out = raw; b.n = 0;
  for (int i = 0; i < 40; ++i) b.counts[i] = b.rem[i] = 0;
  b.rem[1] = (uint8_t)k;
  int divisor = n - k, level = 1;
  while (b.rem[level] > 1) {
    b.counts[level] = (uint8_t)(divisor / b.rem[level]);
    b.rem[level + 1] = (uint8_t)(divisor % b.rem[level]);
    divisor = b.rem[level];
    ++level;
  }
  b.counts[level] = (uint8_t)divisor;
  b.build(level);
  int first = 0;
  for (int i = 0; i < b.n; ++i) if (raw[i]) { first = i; break; }
  for (int i = 0; i < n; ++i) out[i] = raw[(i + first) % b.n];
}

// ---------------------------------------------------------------- velocities
int hrScaleVel(int base, int pct) {
  int v = (base * pct + 50) / 100;  // math.floor(base * level + 0.5), level = pct / 100
  return v < 1 ? 1 : v > 127 ? 127 : v;
}

int hrGrooveVel(int g, int i, int base) {
  if (g <= 1) return base;
  static const uint8_t kSin[4][16] = {  // floor(76 + 32 * (sin(p / 16 * f * 6.2832) + 1) * 0.5 + 0.5), f = 1..4
      {92, 98, 103, 107, 108, 107, 103, 98, 92, 86, 81, 77, 76, 77, 81, 86},
      {92, 103, 108, 103, 92, 81, 76, 81, 92, 103, 108, 103, 92, 81, 76, 81},
      {92, 107, 103, 86, 76, 86, 103, 107, 92, 77, 81, 98, 108, 98, 81, 77},
      {92, 108, 92, 76, 92, 108, 92, 76, 92, 108, 92, 76, 92, 108, 92, 76}};
  int n = g - 1;
  int p = floormod(i - 1, 16);
  if (n <= 4) {
    int k = n == 1 ? 4 : n == 2 ? 2 : n == 3 ? 8 : 3;
    return (p % k == 0) ? 108 : 81;  // floor(76 + 32 * 1 + 0.5), floor(76 + 32 * 0.15 + 0.5)
  }
  if (n <= 8) {
    int m = n - 4;
    int k = m <= 2 ? 4 : m == 3 ? 2 : 8;
    int o = m == 1 ? 2 : m == 4 ? 4 : 1;
    return (floormod(p - o, k) == 0) ? 108 : 82;  // 0.2 -> floor(76 + 6.4 + 0.5)
  }
  int f = n - 8;
  if (f < 1) f = 1;
  if (f > 4) f = 4;
  return kSin[f - 1][p];
}

// ---------------------------------------------------------------- rate options
int hrRateStep(int pulses, int dir) {
  static const uint8_t kOpt[8] = {24, 12, 8, 6, 4, 3, 2, 1};  // slowest .. fastest
  int idx = 3;
  for (int i = 0; i < 8; ++i) if (kOpt[i] == pulses) idx = i;
  idx += dir > 0 ? 1 : -1;
  return kOpt[idx < 0 ? 0 : idx > 7 ? 7 : idx];
}

const char* hrRateName(int pulses) {
  switch (pulses) {
    case 24: return "1/4";
    case 12: return "1/8";
    case 8: return "1/12";
    case 6: return "1/16";
    case 4: return "1/24";
    case 3: return "1/32";
    case 2: return "1/48";
    case 1: return "1/96";
    default: return "?";
  }
}

int hrAppend(char* out, size_t n, int at, const char* fmt, ...) {
  if (at < 0 || (size_t)at >= n) return at;
  va_list ap;
  va_start(ap, fmt);
  int k = vsnprintf(out + at, n - (size_t)at, fmt, ap);
  va_end(ap);
  return k < 0 ? at : at + k;
}

bool HrScript::busKeyScale(cam::Scale& s) const {
  const HrBus& b = bus();
  if (!b.nScale || b.keyPc < 0) return false;
  cam::resolve(s, b.keyPc, b.keyMinor != 0, camA_ ? camA_ : b.keyA, camB_ ? camB_ : b.keyB);
  return true;
}

int HrScript::busScale(uint8_t pcs[12]) const {
  const HrBus& b = bus();
  cam::Scale s;
  if (busKeyScale(s) && (b.keyMinor ? camA_ : camB_)) {  // an own type of that side replaces the published flavour
    for (int i = 0; i < s.n; ++i) pcs[i] = (uint8_t)((s.tonic + s.iv[i]) % 12);
    return s.n;
  }
  for (int i = 0; i < b.nScale; ++i) pcs[i] = b.scale[i];
  return b.nScale;
}

const char* hrNoteName(int pc0) {
  static const char* const k[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  return k[floormod(pc0, 12)];
}

// ---------------------------------------------------------------- HrScript
void HrScript::paintExt(uint8_t lv[16]) const {
  for (int i = 0; i < 16; ++i) lv[i] = 0;
}

void HrScript::leds(uint8_t out[kPads], uint8_t* roles) const {
  uint8_t lv[32], rl[32], ex[16];
  for (int i = 0; i < 32; ++i) lv[i] = rl[i] = 0;
  for (int i = 0; i < 16; ++i) ex[i] = 0;
  paint(lv, rl);
  paintExt(ex);
  for (int y = 0; y < kHrRows; ++y) {
    for (int x = 0; x < kCols; ++x) {
      int v = x < kHrCols ? lv[y * kHrCols + x] : ex[y * 4 + (x - kHrCols)];
      out[y * kCols + x] = (uint8_t)(v > 15 ? 15 : v);
      if (roles) roles[y * kCols + x] = x < kHrCols ? rl[y * kHrCols + x] : (ex[y * 4 + (x - kHrCols)] ? (uint8_t)ROLE_CONTROL : (uint8_t)ROLE_NONE);
    }
  }
}

// ---------------------------------------------------------------- slot
static_assert(sizeof(HrStab) <= HrSlot::kBytes && sizeof(HrArpSeq) <= HrSlot::kBytes && sizeof(HrChordArp) <= HrSlot::kBytes && sizeof(HrStepFollow) <= HrSlot::kBytes && sizeof(HrGridSeq) <= HrSlot::kBytes && sizeof(HrEuclid) <= HrSlot::kBytes && sizeof(HrBass) <= HrSlot::kBytes, "HrSlot buffer too small");
static_assert((int)HrStab::kSerialSize <= (int)HrScript::kMaxSerial && (int)HrArpSeq::kSerialSize <= (int)HrScript::kMaxSerial && (int)HrChordArp::kSerialSize <= (int)HrScript::kMaxSerial && (int)HrStepFollow::kSerialSize <= (int)HrScript::kMaxSerial && (int)HrGridSeq::kSerialSize <= (int)HrScript::kMaxSerial && (int)HrEuclid::kSerialSize <= (int)HrScript::kMaxSerial && (int)HrBass::kSerialSize <= (int)HrScript::kMaxSerial, "HrScript::kMaxSerial too small");

HrScript* HrSlot::create(uint8_t hrType) {
  switch (hrType) {
    case HR_ARP_LINES: s_ = new (buf_) HrStab(HrStab::KIND_ARP); break;
    case HR_CHORD_STABS: s_ = new (buf_) HrStab(HrStab::KIND_STABS); break;
    case HR_ARP_SEQ: s_ = new (buf_) HrArpSeq(); break;
    case HR_CHORD_ARP: s_ = new (buf_) HrChordArp(); break;
    case HR_BASS: s_ = new (buf_) HrBass(); break;
    case HR_EUCLID: s_ = new (buf_) HrEuclid(); break;
    case HR_GRIDSEQ: s_ = new (buf_) HrGridSeq(); break;
    case HR_STEPFOLLOW: s_ = new (buf_) HrStepFollow(); break;
    default: s_ = 0; break;
  }
  return s_;
}

}  // namespace seqgen
