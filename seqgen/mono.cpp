// Mono sequencer, port of c31 page 4 (lines 711-841). See README.md for the mapping.
#include "mono.h"

namespace seqgen {

static const uint8_t kSpd[12] = {96, 48, 32, 24, 16, 12, 8, 6, 4, 3, 2, 1};

template <typename T> static inline T clampv(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

int Mono::speedPulses(int spPos) { return kSpd[clampv(spPos, 1, 12) - 1]; }

Mono::Mono() : ctx_(0) { ch_ = 4; reset(); }

void Mono::reset() {
  for (int i = 0; i < kMaxSteps; ++i) steps_[i] = (int8_t)kNoStep;
  lstart_ = 1; lend_ = 16; pos_ = 1; pc_ = 0; spPos_ = 8; div_ = kSpd[7];
  np_ = 1; ep_ = 1; pg_ = 1; win_ = 0; tpose_ = 0;
  mute_ = false; ti_ = false; half_ = false; playing_ = -1; fc_ = 0; bl_ = false;
  loopHeld_ = false; lpS_ = lpE_ = 0;
  for (int r = 0; r < 4; ++r) for (int c = 0; c < 8; ++c) downX_[r][c] = 0;
}

void Mono::setStep(int absIndex, int degree) {
  if (absIndex < 1 || absIndex > kMaxSteps) return;
  steps_[absIndex - 1] = (int8_t)(degree == kNoStep ? kNoStep : clampv(degree, -100, 100));
}

void Mono::killNote() {
  if (playing_ >= 0) { emit(EV_NOTE_OFF, playing_, 0); playing_ = -1; }
}

// c31 fire(): resolve the step, tie keeps an identical sounding note, otherwise kill + attack.
void Mono::fire() {
  static const SeqContext dflt;
  const SeqContext& c = ctx_ ? *ctx_ : dflt;
  int n = -1;
  if (!mute_) {
    int d = steps_[(pg_ - 1) * 16 + pos_ - 1];
    if (d != kNoStep) {
      int m = c.scaleDeg(d + tpose_ + c.gtpose);
      if (m >= 0 && m <= 127) n = m;
    }
  }
  if (n >= 0 && ti_ && playing_ == n) return;
  killNote();
  if (n >= 0) { emit(EV_NOTE_ON, n, c.vgv(pos_, 100)); playing_ = (int16_t)n; }
}

// One traversal of the loop window = one page; pages 1..np chain, then wrap. Landing on page 1 kills first.
void Mono::pulse() {
  pc_++;
  if (pc_ >= div_) {
    pc_ = 0; pos_++;
    if (pos_ > lend_ || pos_ < lstart_) {
      pos_ = lstart_;
      pg_ = (pg_ >= np_) ? 1 : (uint8_t)(pg_ + 1);
      if (pg_ == 1) killNote();
    }
    fire();
  }
}

void Mono::start() { pc_ = 0; pos_ = lstart_; pg_ = 1; fire(); }
void Mono::stop() { killNote(); }
void Mono::tick() { fc_ = (uint8_t)((fc_ + 1) % 16); bl_ = fc_ < 8; }

void Mono::toggleAt(int x, int row) {
  int deg = degAtRow(row);
  int i = (ep_ - 1) * 16 + x;
  if (steps_[i - 1] == deg) steps_[i - 1] = (int8_t)kNoStep;
  else steps_[i - 1] = (int8_t)deg;
}

// c31 event() y==8: hold start pad + end pad.
void Mono::loopPad(int x, bool down) {
  if (down) {
    if (lpS_ == 0) lpS_ = (int8_t)x;
    else {
      lpE_ = (int8_t)x;
      int a = lpS_, b = lpE_;
      if (a > b) { int t = a; a = b; b = t; }
      lstart_ = (uint8_t)a; lend_ = (uint8_t)b;
      if (pos_ < a || pos_ > b) pos_ = (uint8_t)a;
    }
  } else {
    if (lpE_ != 0) { lpS_ = 0; lpE_ = 0; }
    else if (lpS_ == x) lpS_ = 0;
  }
}

void Mono::pad(int col, int row, bool down) {
  if (col < 0 || col >= kCols || row < 0 || row >= kRows) return;
  if (col < 8) {
    int8_t& memo = downX_[row][col];
    if (down) {
      int x = col + 1 + (half_ ? 8 : 0);
      if (loopHeld_) { memo = (int8_t)(x + 32); loopPad(x, true); }
      else { memo = (int8_t)x; toggleAt(x, row); }
    } else {
      if (memo > 32) loopPad(memo - 32, false);
      memo = 0;
    }
    return;
  }
  if (col == 9 && row == 2) {  // LOOP is a hold modifier
    loopHeld_ = down;
    if (!down) { lpS_ = 0; lpE_ = 0; }
    return;
  }
  if (!down) return;
  switch (col * 4 + row) {
    case 8 * 4 + 0: half_ = !half_; break;                                    // HALF
    case 8 * 4 + 1: ti_ = !ti_; break;                                        // TIE
    case 8 * 4 + 2: win_ = (int8_t)clampv(win_ + 1, -(int)kWinLimit, (int)kWinLimit); break;  // WIN+
    case 8 * 4 + 3: win_ = (int8_t)clampv(win_ - 1, -(int)kWinLimit, (int)kWinLimit); break;  // WIN-
    case 9 * 4 + 0: if (ep_ > 1) ep_--; break;                                // PAGE-
    case 10 * 4 + 0: if (ep_ < 8) { ep_++; if (ep_ > np_) np_ = ep_; } break;  // PAGE+ (extends the chain)
    case 9 * 4 + 1: if (spPos_ > 1) { spPos_--; div_ = kSpd[spPos_ - 1]; } break;   // SPD-
    case 10 * 4 + 1: if (spPos_ < 12) { spPos_++; div_ = kSpd[spPos_ - 1]; } break; // SPD+
    case 11 * 4 + 0:  // CLEAR
      for (int i = 0; i < kMaxSteps; ++i) steps_[i] = (int8_t)kNoStep;
      lstart_ = 1; lend_ = 16; pos_ = 1; np_ = 1; ep_ = 1; pg_ = 1;
      break;
    default: break;
  }
}

int Mono::getParam(int id) const {
  switch (id) {
    case P_SPEED: return spPos_;
    case P_TIE: return ti_;
    case P_WIN: return win_;
    case P_NP: return np_;
    case P_EP: return ep_;
    case P_PG: return pg_;
    case P_LSTART: return lstart_;
    case P_LEND: return lend_;
    case P_POS: return pos_;
    case P_TPOSE: return tpose_;
    case P_MUTE: return mute_;
    case P_HALF: return half_;
    case P_DIV: return div_;
    case P_PLAYING: return playing_;
  }
  return 0;
}

void Mono::setParam(int id, int v) {
  switch (id) {
    case P_SPEED: spPos_ = (uint8_t)clampv(v, 1, 12); div_ = kSpd[spPos_ - 1]; break;
    case P_TIE: ti_ = v != 0; break;
    case P_WIN: win_ = (int8_t)clampv(v, -(int)kWinLimit, (int)kWinLimit); break;
    case P_NP:
      np_ = (uint8_t)clampv(v, 1, 8);
      if (ep_ > np_) ep_ = np_;
      if (pg_ > np_) pg_ = np_;
      break;
    case P_EP: ep_ = (uint8_t)clampv(v, 1, (int)np_); break;
    case P_LSTART: lstart_ = (uint8_t)clampv(v, 1, 16); if (lend_ < lstart_) lend_ = lstart_; if (pos_ < lstart_ || pos_ > lend_) pos_ = lstart_; break;
    case P_LEND: lend_ = (uint8_t)clampv(v, 1, 16); if (lstart_ > lend_) lstart_ = lend_; if (pos_ < lstart_ || pos_ > lend_) pos_ = lstart_; break;
    case P_TPOSE: tpose_ = (int8_t)clampv(v, -24, 24); break;
    case P_MUTE: mute_ = v != 0; break;
    case P_HALF: half_ = v != 0; break;
  }
}

size_t Mono::serialize(uint8_t* b, size_t cap) const {
  if (cap < (size_t)kSerialSize) return 0;
  for (int i = 0; i < kSerialSize; ++i) b[i] = 0;
  b[0] = kSerialVersion; b[1] = spPos_; b[2] = ti_; b[3] = (uint8_t)win_; b[4] = np_; b[5] = ep_;
  b[6] = lstart_; b[7] = lend_; b[8] = (uint8_t)tpose_; b[9] = mute_; b[10] = half_;
  for (int i = 0; i < kMaxSteps; ++i) b[12 + i] = (uint8_t)steps_[i];
  return kSerialSize;
}

bool Mono::deserialize(const uint8_t* b, size_t len) {
  if (len < (size_t)kSerialSize || b[0] != kSerialVersion) return false;
  int sp = b[1], np = b[4], ep = b[5], ls = b[6], le = b[7];
  int8_t win = (int8_t)b[3];
  if (sp < 1 || sp > 12 || np < 1 || np > 8 || ep < 1 || ep > np || ls < 1 || ls > le || le > 16 ||
      win < -kWinLimit || win > kWinLimit)
    return false;
  killNote();
  spPos_ = (uint8_t)sp; div_ = kSpd[sp - 1]; ti_ = b[2] != 0; win_ = win; np_ = (uint8_t)np; ep_ = (uint8_t)ep;
  lstart_ = (uint8_t)ls; lend_ = (uint8_t)le; tpose_ = (int8_t)b[8]; mute_ = b[9] != 0; half_ = b[10] != 0;
  for (int i = 0; i < kMaxSteps; ++i) steps_[i] = (int8_t)b[12 + i];
  pos_ = lstart_; pc_ = 0; pg_ = 1; loopHeld_ = false; lpS_ = lpE_ = 0;
  for (int r = 0; r < 4; ++r) for (int c = 0; c < 8; ++c) downX_[r][c] = 0;
  return true;
}

void Mono::leds(uint8_t out[kPads], uint8_t* roles) const {
  static const SeqContext dflt;
  const SeqContext& c = ctx_ ? *ctx_ : dflt;
  uint8_t dummy[kPads];
  uint8_t* ro = roles ? roles : dummy;
  for (int i = 0; i < kPads; ++i) { out[i] = 0; ro[i] = ROLE_NONE; }
  int base = (ep_ - 1) * 16;
  bool ph = (pg_ == ep_);
  for (int col = 0; col < 8; ++col) {
    int x = col + 1 + (half_ ? 8 : 0);
    bool inloop = (x >= lstart_ && x <= lend_);
    int vb = 5 + floordiv(c.vgv(x, 100) - 76, 4);  // note brightness = the velocity it will fire at
    int sv = steps_[base + x - 1];
    bool pl = ph && x == pos_;
    for (int row = 0; row < 4; ++row) {
      int deg = degAtRow(row), led = 0;
      uint8_t role = ROLE_STEP;
      if (floormod(deg, 7) == 0) led = 2;
      if (inloop && led == 0) { led = 1; role = ROLE_LOOP; }
      if (sv == deg) led = vb;
      if (pl) { int m = (sv == deg) ? 15 : 5; if (m > led) led = m; role = ROLE_PLAYHEAD; }
      out[row * 12 + col] = (uint8_t)clampv(led, 0, 15); ro[row * 12 + col] = role;
    }
  }
  int ctl[12] = {half_ ? 15 : 5, ti_ ? (bl_ ? 15 : 2) : 5, bl_ ? 9 : 4, bl_ ? 9 : 4,
                 ep_ > 1 ? 9 : 3, 5, loopHeld_ ? 15 : 5, 0, ep_ < 8 ? 9 : 3, 5, 0, 0};
  // columns 8..11, rows 0..3: [HALF TIE WIN+ WIN-] [PAGE- SPD- LOOP -] [PAGE+ SPD+ - -] [CLEAR - - -]
  for (int i = 0; i < 4; ++i) { out[i * 12 + 8] = (uint8_t)ctl[i]; ro[i * 12 + 8] = ROLE_CONTROL; }
  out[0 * 12 + 9] = (uint8_t)ctl[4]; out[1 * 12 + 9] = (uint8_t)ctl[5]; out[2 * 12 + 9] = (uint8_t)ctl[6];
  out[0 * 12 + 10] = (uint8_t)ctl[8]; out[1 * 12 + 10] = (uint8_t)ctl[9];
  out[0 * 12 + 11] = (uint8_t)(bl_ ? 12 : 5);
  ro[0 * 12 + 9] = ro[1 * 12 + 9] = ro[2 * 12 + 9] = ROLE_CONTROL;
  ro[0 * 12 + 10] = ro[1 * 12 + 10] = ro[0 * 12 + 11] = ROLE_CONTROL;
}

}  // namespace seqgen
