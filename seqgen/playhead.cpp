// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Playhead, see playhead.h and README.md ("Playhead").
#include "playhead.h"

namespace seqgen {

template <typename T> static inline T clampv(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

void Playhead::reset() {
  length_ = 16; recLen_ = 0; recIdx_ = 0; recCount_ = 0;
  columns_ = 8; quant_ = 1; rate_ = RATE_NORMAL; rev_ = false;
  held_ = 0; pos_ = 15; pendJump_ = -1; pendLoop_ = false; pendStart_ = false; pendA_ = pendB_ = 0;
  loopActive_ = false; loA_ = loB_ = 0; ovr_ = false; holdLeft_ = 0; forceJump_ = false;
  recording_ = false; recActive_ = false; gstep_ = 0;
  for (int i = 0; i < kMaxRec; ++i) recBuf_[i] = 0;
  for (int i = 0; i < kMaxRec / 8; ++i) recHold_[i] = 0;
}

void Playhead::setLength(int steps) {
  length_ = (uint16_t)clampv(steps, 1, (int)kMaxLength);
  if (pos_ >= length_ || gstep_ == 0) pos_ = (int16_t)(length_ - 1);  // before the first step the next step is position 0
  if (pendJump_ >= length_) pendJump_ = (int16_t)(length_ - 1);
}
void Playhead::setColumns(int n) {
  columns_ = (uint8_t)(n == 12 ? 12 : 8);
  held_ = 0; loopActive_ = false; pendLoop_ = false; pendStart_ = false; pendJump_ = -1; ovr_ = false;
}
void Playhead::setQuantise(int steps) { quant_ = (uint8_t)clampv(steps, 1, 64); }
void Playhead::setRate(Rate r) { rate_ = (uint8_t)(r > RATE_DOUBLE ? RATE_NORMAL : r); holdLeft_ = 0; }
void Playhead::setReverse(bool rev) { rev_ = rev; }

int Playhead::heldCount() const { int n = 0; for (int c = 0; c < kMaxColumns; ++c) n += (held_ >> c) & 1; return n; }

void Playhead::heldRange(int& a, int& b) const {
  a = -1; b = -1;
  for (int c = 0; c < columns_; ++c) if ((held_ >> c) & 1) { if (a < 0) a = c; b = c; }
}

// Inclusive position range of columns a..b (the last column runs to the end of its share).
void Playhead::rangeOf(int a, int b, int& lo, int& hi) const {
  lo = columnPosition(a); hi = columnPosition(b + 1) - 1;
  if (hi < lo) hi = lo;  // fewer steps than columns
}

int Playhead::succ(int p, int lo, int hi) const {
  if (!rev_) { p += 1; return (p > hi || p < lo) ? lo : p; }
  p -= 1; return (p < lo || p > hi) ? hi : p;
}

void Playhead::press(int col) {
  if (col < 0 || col >= columns_ || ((held_ >> col) & 1)) return;
  held_ |= (uint16_t)(1u << col);
  if (heldCount() == 1) {
    pendJump_ = (int16_t)columnPosition(col); pendLoop_ = false; pendStart_ = false;
  } else {
    int a, b; heldRange(a, b);
    pendLoop_ = true; pendA_ = (uint8_t)a; pendB_ = (uint8_t)b;
    if (!loopActive_) pendStart_ = true;  // loop starts at its first (or, reversed, last) step
  }
}

void Playhead::release(int col) {
  if (col < 0 || col >= columns_ || !((held_ >> col) & 1)) return;
  held_ &= (uint16_t)~(1u << col);
  if (heldCount() >= 2) {
    int a, b; heldRange(a, b);
    pendLoop_ = true; pendA_ = (uint8_t)a; pendB_ = (uint8_t)b;
  } else {
    loopActive_ = false; pendLoop_ = false; pendStart_ = false;  // free running from the current position
  }
}

void Playhead::closeRecording() {
  recording_ = false;
  if (recCount_ == 0) { recActive_ = false; return; }
  recLen_ = recCount_; recIdx_ = 0; recActive_ = true;
}

void Playhead::recDown() {
  if (recording_) return;
  recording_ = true; recActive_ = false; recCount_ = 0;
  for (int i = 0; i < kMaxRec / 8; ++i) recHold_[i] = 0;
}
void Playhead::recUp() { if (recording_) closeRecording(); }

void Playhead::resync(uint32_t globalStep) {
  const int target = (int)(globalStep % length_);
  gstep_ = globalStep;
  pos_ = (int16_t)(rev_ ? (target + 1) % length_ : (target + length_ - 1) % length_);  // next step() lands on `target`
  held_ = 0; loopActive_ = false; pendLoop_ = false; pendStart_ = false; pendJump_ = -1;
  ovr_ = false; holdLeft_ = 0; forceJump_ = true;
  recording_ = false; recActive_ = false; recCount_ = 0;
}

void Playhead::restart(uint32_t nextStep) {
  const int target = (int)(nextStep % length_);
  gstep_ = nextStep;
  pos_ = (int16_t)(rev_ ? (target + 1) % length_ : (target + length_ - 1) % length_);
  holdLeft_ = 0; forceJump_ = false; ovr_ = false;
  if (recActive_) recIdx_ = (uint16_t)(nextStep % recLen_);
}

Playhead::StepOut Playhead::step() {
  const int len = length_;
  const uint32_t g = gstep_++;

  // 1. apply queued gestures on a quantise boundary
  bool applied = false;
  int target = 0;
  if (g % quant_ == 0) {
    if (pendLoop_) {
      loopActive_ = true; loA_ = pendA_; loB_ = pendB_;
      int lo, hi; rangeOf(loA_, loB_, lo, hi);
      if (pendStart_ || pos_ < lo || pos_ > hi) { applied = true; target = rev_ ? hi : lo; }
    }
    if (!applied && pendJump_ >= 0) { applied = true; target = pendJump_; }
    pendLoop_ = false; pendStart_ = false; pendJump_ = -1;
  }
  if (applied) ovr_ = true;
  else if (held_ == 0) ovr_ = false;
  if (target >= len) target = len - 1;

  // 2. choose the position
  int lo = 0, hi = len - 1;
  if (loopActive_) rangeOf(loA_, loB_, lo, hi);
  const bool replay = !applied && recActive_ && !ovr_;
  bool hold = false, has2 = false;
  int pos, pos2;
  if (applied) {
    pos = target;
    holdLeft_ = (uint8_t)(rate_ == RATE_HALF ? 1 : 0);
  } else if (replay) {
    pos = recBuf_[recIdx_] % len;
    hold = ((recHold_[recIdx_ >> 3] >> (recIdx_ & 7)) & 1) && pos == pos_;
    holdLeft_ = 0;
  } else if (holdLeft_ > 0) {
    holdLeft_--; hold = true; pos = pos_;
  } else {
    pos = succ(pos_, lo, hi);
    holdLeft_ = (uint8_t)(rate_ == RATE_HALF ? 1 : 0);
  }
  pos2 = pos;
  if (!hold && !replay && rate_ == RATE_DOUBLE) { has2 = true; pos2 = succ(pos, lo, hi); }

  // 3. flags
  StepOut o;
  o.jumped = false;
  if (!hold) { o.jumped = forceJump_ || pos != (pos_ + 1) % len; forceJump_ = false; }
  o.jumped2 = has2 && pos2 != (pos + 1) % len;
  o.pos = (uint8_t)pos; o.pos2 = (uint8_t)pos2; o.hold = hold; o.has2 = has2;
  pos_ = (int16_t)pos2;

  // 4. the recorded loop keeps its own clock (also while overridden), the take records what was played
  if (recActive_) recIdx_ = (uint16_t)((recIdx_ + 1) % recLen_);
  if (recording_) {
    recBuf_[recCount_] = (uint8_t)pos;
    if (hold) recHold_[recCount_ >> 3] |= (uint8_t)(1u << (recCount_ & 7));
    if (++recCount_ >= kMaxRec) closeRecording();  // a full take closes itself; the loop restarts right here
  }
  return o;
}

bool Playhead::noteOffsetFor(int position, const uint8_t* hasNote, int length, int& noteStep, int& stepsSinceNote) {
  if (!hasNote || length < 1) return false;
  position = clampv(position, 0, length - 1);
  for (int i = 0; i < length; ++i) {
    int p = position - i; if (p < 0) p += length;
    if (hasNote[p]) { noteStep = p; stepsSinceNote = i; return true; }
  }
  return false;
}

size_t Playhead::serialize(uint8_t* buf, size_t cap) const {
  if (!buf || cap < (size_t)kSerialSize) return 0;
  const int rl = recActive_ ? recLen_ : 0;
  buf[0] = kSerialVersion;
  buf[1] = (uint8_t)(length_ & 0xFF); buf[2] = (uint8_t)(length_ >> 8);
  buf[3] = columns_; buf[4] = quant_; buf[5] = (uint8_t)(rate_ | (rev_ ? 0x80 : 0));
  buf[6] = (uint8_t)(rl & 0xFF); buf[7] = (uint8_t)(rl >> 8);
  for (int i = 0; i < kMaxRec; ++i) buf[8 + i] = recBuf_[i];
  for (int i = 0; i < kMaxRec / 8; ++i) buf[8 + kMaxRec + i] = recHold_[i];
  return kSerialSize;
}

bool Playhead::deserialize(const uint8_t* buf, size_t len) {
  if (!buf || len < (size_t)kSerialSize || buf[0] != kSerialVersion) return false;
  const int l = buf[1] | (buf[2] << 8), rl = buf[6] | (buf[7] << 8);
  const int rate = buf[5] & 0x7F;
  if (l < 1 || l > kMaxLength || (buf[3] != 8 && buf[3] != 12) || buf[4] < 1 || buf[4] > 64 || rate > RATE_DOUBLE ||
      rl > kMaxRec) return false;
  reset();
  length_ = (uint16_t)l; pos_ = (int16_t)(l - 1); columns_ = buf[3]; quant_ = buf[4]; rate_ = (uint8_t)rate;
  rev_ = (buf[5] & 0x80) != 0;
  for (int i = 0; i < kMaxRec; ++i) recBuf_[i] = buf[8 + i];
  for (int i = 0; i < kMaxRec / 8; ++i) recHold_[i] = buf[8 + kMaxRec + i];
  recActive_ = rl > 0; recLen_ = (uint16_t)rl;
  return true;
}

}  // namespace seqgen
