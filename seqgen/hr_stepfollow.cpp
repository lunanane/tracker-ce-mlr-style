// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Ported 1:1 from HYPER_RASP (same owner) scripts/step_follower.lua. See hr_stepfollow.h.
#include "hr_stepfollow.h"

#include <stdio.h>

namespace seqgen {

HrStepFollow::HrStepFollow()
    : HrScript(HR_STEPFOLLOW), div_(6), vel_(100), start_(1), end_(kSteps), col_(1), pulseCount_(0), playing_(0), octave_(0), base_(0), nLit_(0), nSounding_(0) {
  for (int i = 0; i < kMaxDegrees; ++i) { lit_[i].degree = 0; lit_[i].mask = 0; sounding_[i] = 0; }
}

// context(): the published scale (none = chromatic) and the root (default 60)
void HrStepFollow::context(const uint8_t** scale, int* n, int* root) const {
  const HrBus& b = bus();
  *scale = b.nScale ? b.scale : 0;
  *n = b.nScale;
  *root = b.root >= 0 ? b.root : 60;
}

int HrStepFollow::noteForDegree(int degree) const {
  const uint8_t* sc;
  int n, root;
  context(&sc, &n, &root);
  if (!sc) return root + degree + 12 * octave_;
  int interval = floormod(sc[floormod(degree, n)] - sc[0], 12);
  int base = root - floormod(root - sc[0], 12);
  return base + 12 * floordiv(degree, n) + interval + 12 * octave_;
}

bool HrStepFollow::isTonic(int degree) const {
  const HrBus& b = bus();
  return b.nScale == 0 ? floormod(degree, 12) == 0 : floormod(degree, b.nScale) == 0;
}

bool HrStepFollow::windowFits(int base) const { return noteForDegree(base) >= kMinNote && noteForDegree(base + kNoteRows - 1) <= kMaxNote; }

bool HrStepFollow::isLit(int degree, int col) const {
  for (int i = 0; i < nLit_; ++i) if (lit_[i].degree == degree) return (lit_[i].mask >> (col - 1)) & 1;
  return false;
}

void HrStepFollow::toggle(int degree, int col) {
  uint8_t bit = (uint8_t)(1 << (col - 1));
  for (int i = 0; i < nLit_; ++i) {
    if (lit_[i].degree != degree) continue;
    lit_[i].mask ^= bit;
    if (!lit_[i].mask) {  // set_degree: an empty row is removed
      for (int k = i; k + 1 < nLit_; ++k) lit_[k] = lit_[k + 1];
      --nLit_;
    }
    return;
  }
  if (nLit_ >= kMaxDegrees) return;  // out of storage (the script has no limit)
  int at = nLit_;
  while (at > 0 && lit_[at - 1].degree > degree) { lit_[at] = lit_[at - 1]; --at; }
  lit_[at].degree = (int16_t)degree;
  lit_[at].mask = bit;
  ++nLit_;
}

void HrStepFollow::stopSounding() {
  for (int i = 0; i < nSounding_; ++i) noteOff(sounding_[i]);
  nSounding_ = 0;
}

void HrStepFollow::playColumnNotes(int col) {
  stopSounding();
  for (int i = 0; i < nLit_; ++i) {
    if (!((lit_[i].mask >> (col - 1)) & 1)) continue;
    int note = noteForDegree(lit_[i].degree);
    if (note >= 0 && note <= 127) {
      noteOn(note, vel_);
      sounding_[nSounding_++] = (uint8_t)note;
    }
  }
}

void HrStepFollow::startPlayback() {
  if (playing_) return;
  playing_ = 1;
  col_ = start_;
  pulseCount_ = 0;
  playColumnNotes(col_);
}

void HrStepFollow::stopPlayback() {
  if (!playing_) return;
  playing_ = 0;
  stopSounding();
}

void HrStepFollow::pulse() {
  if (!playing_) return;
  if (++pulseCount_ < div_) return;
  pulseCount_ = 0;
  ++col_;
  if (col_ > end_ || col_ < start_) col_ = start_;
  playColumnNotes(col_);
}

void HrStepFollow::page(int delta) {
  int target = base_;
  for (int i = 1; i <= 2; ++i) {
    int candidate = base_ + delta * i;
    if (!windowFits(candidate)) break;
    target = candidate;
  }
  base_ = (int16_t)target;
}

void HrStepFollow::grid(int x, int y, int z) {
  if (z != 1) return;
  if (x < 1 || x > kSteps || y < 1 || y > kNoteRows) return;
  if (x == 8 && (y == 3 || y == 4)) {
    page(y == 3 ? 1 : -1);
    return;
  }
  toggle(base_ + (kNoteRows - y), x);
}

// ---------------------------------------------------------------- params and the extra pads
int HrStepFollow::getParam(int id) const {
  switch (id) {
    case P_DIV: return div_;
    case P_VEL: return vel_;
    case P_OCTAVE: return octave_;
    case P_RANGE: return start_ * 10 + end_;
    case P_BASE: return base_;
    case P_COLUMN: return col_;
    case P_START: return start_;
    case P_END: return end_;
    default: return 0;
  }
}

void HrStepFollow::setParam(int id, int v) {
  switch (id) {
    case P_DIV: div_ = (uint8_t)(v < 1 ? 1 : v > 24 ? 24 : v); break;
    case P_VEL: vel_ = (uint8_t)(v < 1 ? 1 : v > 127 ? 127 : v); break;
    case P_OCTAVE: octave_ = (int8_t)(v < -2 ? -2 : v > 2 ? 2 : v); break;
    case P_RANGE: case P_START: case P_END: {
      int lo = id == P_START ? v : start_, hi = id == P_END ? v : end_;
      if (id == P_RANGE) { lo = v / 10; hi = v - lo * 10; }
      if (lo >= 1 && hi >= lo && hi <= kSteps) {
        start_ = (uint8_t)lo; end_ = (uint8_t)hi;
        if (col_ < start_ || col_ > end_) col_ = start_;
      }
      break;
    }
    default: break;
  }
}

void HrStepFollow::ext(int c, int r, bool down) {
  if (!down) return;
  switch (r * 4 + c) {
    case 0: setParam(P_OCTAVE, octave_ - 1); break;
    case 1: setParam(P_OCTAVE, octave_ + 1); break;
    case 2: setParam(P_VEL, vel_ - 8); break;
    case 3: setParam(P_VEL, vel_ + 8); break;
    case 4: setParam(P_DIV, hrRateStep(div_, -1)); break;
    case 5: setParam(P_DIV, hrRateStep(div_, 1)); break;
    case 6: setParam(P_START, start_ - 1); break;
    case 7: setParam(P_START, start_ + 1); break;
    case 8: setParam(P_END, end_ - 1); break;
    case 9: setParam(P_END, end_ + 1); break;
    case 12: setParam(P_OCTAVE, 0); break;
    case 13: setParam(P_VEL, 100); break;
    case 14: setParam(P_DIV, 6); break;
    case 15: setParam(P_RANGE, 10 + kSteps); break;
    default: break;
  }
}

// ---------------------------------------------------------------- serialization (settings, the window and the lit steps)
size_t HrStepFollow::serialize(uint8_t* b, size_t cap) const {
  if (cap < (size_t)kSerialSize) return 0;
  for (int i = 0; i < kSerialSize; ++i) b[i] = 0;
  b[0] = kSerialVersion; b[1] = div_; b[2] = vel_; b[3] = (uint8_t)(octave_ + 2); b[4] = start_; b[5] = end_;
  b[6] = (uint8_t)((uint16_t)base_ & 255); b[7] = (uint8_t)((uint16_t)base_ >> 8); b[8] = nLit_;
  for (int i = 0; i < nLit_; ++i) {
    b[9 + i * 3] = (uint8_t)((uint16_t)lit_[i].degree & 255);
    b[10 + i * 3] = (uint8_t)((uint16_t)lit_[i].degree >> 8);
    b[11 + i * 3] = lit_[i].mask;
  }
  return kSerialSize;
}

bool HrStepFollow::deserialize(const uint8_t* b, size_t len) {
  if (len < (size_t)kSerialSize || b[0] != kSerialVersion) return false;
  if (b[1] < 1 || b[1] > 24 || b[2] < 1 || b[2] > 127 || b[3] > 4 || b[4] < 1 || b[5] < b[4] || b[5] > kSteps || b[8] > kMaxDegrees) return false;
  Lit tmp[kMaxDegrees];
  for (int i = 0; i < b[8]; ++i) {
    tmp[i].degree = (int16_t)(b[9 + i * 3] | b[10 + i * 3] << 8);
    tmp[i].mask = b[11 + i * 3];
    if (!tmp[i].mask || (i > 0 && tmp[i].degree <= tmp[i - 1].degree)) return false;
  }
  stopSounding();
  playing_ = 0; pulseCount_ = 0;
  div_ = b[1]; vel_ = b[2]; octave_ = (int8_t)(b[3] - 2); start_ = b[4]; end_ = b[5]; col_ = start_;
  base_ = (int16_t)(b[6] | b[7] << 8);
  nLit_ = b[8];
  for (int i = 0; i < nLit_; ++i) lit_[i] = tmp[i];
  return true;
}

// ---------------------------------------------------------------- LEDs
void HrStepFollow::paint(uint8_t lv[32], uint8_t role[32]) const {
  for (int y = 1; y <= kNoteRows; ++y) {
    int degree = base_ + (kNoteRows - y);
    bool tonic = isTonic(degree);
    for (int col = 1; col <= kSteps; ++col) {
      int i = (y - 1) * 8 + col - 1;
      if (col == 8 && (y == 3 || y == 4)) {
        lv[i] = windowFits(base_ + (y == 3 ? 1 : -1)) ? 12 : 3;
        role[i] = ROLE_CONTROL;
        continue;
      }
      int level;
      if (isLit(degree, col)) level = 15;
      else if (playing_ && col == col_) level = 4;
      else if (col >= start_ && col <= end_) level = 1;
      else level = 0;
      lv[i] = (uint8_t)level;
      role[i] = tonic ? ROLE_ROOT_MAJOR : ROLE_DEGREE;
    }
  }
}

void HrStepFollow::paintExt(uint8_t lv[16]) const {
  for (int i = 0; i < 16; ++i) lv[i] = 0;
  for (int i = 0; i < 10; ++i) lv[i] = 3;
  for (int i = 12; i < 16; ++i) lv[i] = 3;
}

// ---------------------------------------------------------------- Shift legend
void HrStepFollow::legend(int col, int row, HrCell& c) const {
  c.clear();
  if (col < kSteps) {
    if (col == 7 && (row == 2 || row == 3)) {
      c.set(HZ_ACTION, row == 2 ? "Pg up" : "Pg dn");
      return;
    }
    int degree = base_ + (kNoteRows - 1 - row);
    int note = noteForDegree(degree);
    if (note >= 0 && note <= 127) c.set(HZ_STEP, hrNoteName(note), isLit(degree, col + 1));
  } else {
    static const char* const k[16] = {"Oct-", "Oct+", "Vel-", "Vel+", "Div-", "Div+", "Lo-", "Lo+", "Hi-", "Hi+", "", "", "Oct 0", "Vel100", "1/16", "Full"};
    int i = row * 4 + col - 8;
    if (k[i][0]) c.set(i >= 12 ? HZ_ACTION : HZ_ADJ, k[i]);
  }
}

void HrStepFollow::title(char* out, size_t n) const {
  int k = hrAppend(out, n, 0, "STEP FOLLOWER  %s  %s  Vel %d  Steps %d-%d", playing_ ? "Playing" : "Stopped", hrRateName(div_), (int)vel_, (int)start_, (int)end_);
  if (octave_) k = hrAppend(out, n, k, "  Oct %+d", (int)octave_);
  if (bus().nScale == 0) hrAppend(out, n, k, "  chromatic");
}

const char* HrStepFollow::help() const {
  return "Tap cells: steps in the shared scale. Right pads (Pg up / Pg dn) move the window.";
}

}  // namespace seqgen
