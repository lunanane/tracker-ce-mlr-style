// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Mono sequencer, port of c34 page 4. See README.md for the mapping.
#include "mono.h"

namespace seqgen {

static const uint8_t kSpd[12] = {96, 48, 32, 24, 16, 12, 8, 6, 4, 3, 2, 1};

template <typename T> static inline T clampv(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

int Mono::speedPulses(int spPos) { return kSpd[clampv(spPos, 1, 12) - 1]; }

Mono::Mono() : ctx_(0) { ch_ = 0; reset(); }  // no channel of its own: the host glue tags events with the track it belongs to

void Mono::reset() {
  for (int i = 0; i < kMaxSteps; ++i) steps_[i] = (int8_t)kNoStep;
  lstart_ = 1; lend_ = 16; pos_ = 1; pc_ = 0; spPos_ = 8; div_ = kSpd[7];
  np_ = 1; ep_ = 1; pg_ = 1; win_ = 0; tpose_ = 0;
  mute_ = false; ti_ = false; half_ = false; vg_ = 1; playing_ = -1; fc_ = 0; bl_ = false;
  loopHeld_ = false; lpS_ = lpE_ = 0; menu_ = false; fp_ = 0; ra_ = false;
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
      int m = c.scaleDeg(d + tpose_ + c.gtpose, camA_, camB_);
      if (m >= 0 && m <= 127) n = m;
    }
  }
  if (n >= 0 && ti_ && playing_ == n) return;
  killNote();
  if (n >= 0) { emit(EV_NOTE_ON, n, c.vgv(vg_, pos_, 100)); playing_ = (int16_t)n; }
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

// Pad map (col, row), row 0 = top.  NOTE view (default): cols 0..7 = 8 steps x 4 scale degrees (row 0 = highest), then
//   col 8: (0) Window up  (1) Window down  (2) Half = steps 1-8 / 9-16  (3) Loop (hold it, tap start step and end step)
//   col 9: (0) Page -  (1) Page +  (2) Tie        col 10: (0) Slower  (1) Faster  (2) Velocity -   col 11: (2) Velocity +  (3) Menu
// MENU view (c34 menu overlay, all 12 columns): row 0 = speed 1..12 (direct), row 1 = pages 1..8 (tap = edit page, hold 1 + tap N =
//   page count N), (1,8) Tie, (1,10) Window up, (1,11) Window down; row 2 = velocity pattern 1..12 (9..12 = pattern 1..4 blended
//   with 1), row 3 = (0) Transpose - (1) Transpose + (2) Mute (7) Clear all, (3,11) back to the notes.
void Mono::pad(int col, int row, bool down) {
  if (col < 0 || col >= kCols || row < 0 || row >= kRows) return;
  if (col < 8 && !down) {  // a release belongs to the press, whatever view is shown now
    int8_t& memo = downX_[row][col];
    if (memo >= 64) pagePad(memo - 64, false);
    else if (memo > 32) loopPad(memo - 32, false);
    memo = 0;
    return;
  }
  if (col == 11 && row == 3) { if (down) setMenu(!menu_); return; }
  if (menu_) menuPad(col, row, down);
  else notePad(col, row, down);
}

void Mono::setMenu(bool on) {
  menu_ = on;
  loopHeld_ = false; lpS_ = lpE_ = 0; fp_ = 0; ra_ = false;  // a gesture of the other view is over
}

void Mono::clearAll() {  // c34 menu "clear sequence" (14,6)
  for (int i = 0; i < kMaxSteps; ++i) steps_[i] = (int8_t)kNoStep;
  lstart_ = 1; lend_ = 16; pos_ = 1; np_ = 1; ep_ = 1; pg_ = 1;
}

// c34 menu row 6, x1..8: TAP = select the edit page (acts on release), HOLD pad 1 and press pad N = page count N.
void Mono::pagePad(int x, bool down) {
  if (down) {
    if (fp_ == 0) { fp_ = (uint8_t)x; ra_ = false; }
    else if (fp_ == 1 && x > 1) {
      np_ = (uint8_t)x; ra_ = true;
      if (ep_ > x) ep_ = (uint8_t)x;
      if (pg_ > x) pg_ = (uint8_t)x;
    }
  } else if (fp_ == x) {
    if (!ra_ && x <= np_) ep_ = (uint8_t)x;
    fp_ = 0; ra_ = false;
  }
}

void Mono::notePad(int col, int row, bool down) {
  if (col < 8) {
    int x = col + 1 + (half_ ? 8 : 0);
    int8_t& memo = downX_[row][col];
    if (loopHeld_) { memo = (int8_t)(x + 32); loopPad(x, true); }
    else { memo = (int8_t)x; toggleAt(x, row); }
    return;
  }
  if (col == 8 && row == 3) {  // Loop is a hold modifier
    loopHeld_ = down;
    if (!down) { lpS_ = 0; lpE_ = 0; }
    return;
  }
  if (!down) return;
  switch (col * 4 + row) {
    case 8 * 4 + 0: win_ = (int8_t)clampv(win_ + 1, -(int)kWinLimit, (int)kWinLimit); break;  // window up
    case 8 * 4 + 1: win_ = (int8_t)clampv(win_ - 1, -(int)kWinLimit, (int)kWinLimit); break;  // window down
    case 8 * 4 + 2: half_ = !half_; break;                                    // steps 1-8 / 9-16
    case 9 * 4 + 0: if (ep_ > 1) ep_--; break;                                // PAGE-
    case 9 * 4 + 1: if (ep_ < 8) { ep_++; if (ep_ > np_) np_ = ep_; } break;  // PAGE+ (extends the chain)
    case 9 * 4 + 2: ti_ = !ti_; break;                                        // TIE
    case 10 * 4 + 0: if (spPos_ > 1) { spPos_--; div_ = kSpd[spPos_ - 1]; } break;   // slower
    case 10 * 4 + 1: if (spPos_ < 12) { spPos_++; div_ = kSpd[spPos_ - 1]; } break;  // faster
    case 10 * 4 + 2: if (vg_ > 1) vg_--; break;   // VEL- / VEL+ walk 1..12 like the menu row (9..12 = blends with pattern 1)
    case 11 * 4 + 2: if (vg_ < 12) vg_++; break;
    default: break;
  }
}

void Mono::menuPad(int col, int row, bool down) {
  if (col < 8 && row == 1) {  // page pads: down and up (the release comes back through the memo)
    if (down) { downX_[row][col] = (int8_t)(col + 1 + 64); pagePad(col + 1, true); }
    return;
  }
  if (!down) return;
  switch (row) {
    case 0: spPos_ = (uint8_t)(col + 1); div_ = kSpd[col]; break;  // c34 menu row 1
    case 1:
      if (col == 8) ti_ = !ti_;
      else if (col == 10) win_ = (int8_t)clampv(win_ + 1, -(int)kWinLimit, (int)kWinLimit);
      else if (col == 11) win_ = (int8_t)clampv(win_ - 1, -(int)kWinLimit, (int)kWinLimit);
      break;
    case 2: vg_ = (uint8_t)(col + 1); break;  // c34 menu row 7: vg = x
    default:
      if (col == 0) tpose_ = (int8_t)clampv(tpose_ - 1, -24, 24);
      else if (col == 1) tpose_ = (int8_t)clampv(tpose_ + 1, -24, 24);
      else if (col == 2) mute_ = !mute_;
      else if (col == 7) clearAll();
      break;
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
    case P_VG: return vg_;
    case P_MENU: return menu_;
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
    case P_VG: vg_ = (uint8_t)clampv(v, 1, 72); break;
    case P_HALF: half_ = v != 0; break;
    case P_MENU: setMenu(v != 0); break;
  }
}

size_t Mono::serialize(uint8_t* b, size_t cap) const {
  if (cap < (size_t)kSerialSize) return 0;
  for (int i = 0; i < kSerialSize; ++i) b[i] = 0;
  b[0] = kSerialVersion; b[1] = spPos_; b[2] = ti_; b[3] = (uint8_t)win_; b[4] = np_; b[5] = ep_;
  b[6] = lstart_; b[7] = lend_; b[8] = (uint8_t)tpose_; b[9] = mute_; b[10] = half_; b[11] = vg_;
  for (int i = 0; i < kMaxSteps; ++i) b[12 + i] = (uint8_t)steps_[i];
  return kSerialSize;
}

bool Mono::deserialize(const uint8_t* b, size_t len) {
  if (len < (size_t)kSerialSize || b[0] != kSerialVersion) return false;
  int sp = b[1], np = b[4], ep = b[5], ls = b[6], le = b[7];
  int8_t win = (int8_t)b[3];
  if (sp < 1 || sp > 12 || np < 1 || np > 8 || ep < 1 || ep > np || ls < 1 || ls > le || le > 16 ||
      win < -kWinLimit || win > kWinLimit || b[11] < 1 || b[11] > 72)
    return false;
  killNote();
  spPos_ = (uint8_t)sp; div_ = kSpd[sp - 1]; ti_ = b[2] != 0; win_ = win; np_ = (uint8_t)np; ep_ = (uint8_t)ep;
  lstart_ = (uint8_t)ls; lend_ = (uint8_t)le; tpose_ = (int8_t)b[8]; mute_ = b[9] != 0; half_ = b[10] != 0; vg_ = b[11];
  for (int i = 0; i < kMaxSteps; ++i) steps_[i] = (int8_t)b[12 + i];
  pos_ = lstart_; pc_ = 0; pg_ = 1; loopHeld_ = false; lpS_ = lpE_ = 0; menu_ = false; fp_ = 0; ra_ = false;
  for (int r = 0; r < 4; ++r) for (int c = 0; c < 8; ++c) downX_[r][c] = 0;
  return true;
}

void Mono::leds(uint8_t out[kPads], uint8_t* roles) const {
  static const SeqContext dflt;
  const SeqContext& c = ctx_ ? *ctx_ : dflt;
  uint8_t dummy[kPads];
  uint8_t* ro = roles ? roles : dummy;
  for (int i = 0; i < kPads; ++i) { out[i] = 0; ro[i] = ROLE_NONE; }
  auto put = [&](int col, int row, int led, uint8_t role) { out[row * 12 + col] = (uint8_t)clampv(led, 0, 15); ro[row * 12 + col] = role; };
  if (menu_) {  // c34 menu_draw on the 12x4 pads
    for (int x = 1; x <= 12; ++x) put(x - 1, 0, x == spPos_ ? (bl_ ? 15 : 8) : (x == 8 ? 4 : 2), ROLE_CONTROL);  // speed
    for (int x = 1; x <= 8; ++x)  // pages: edit page blinks, playing page mid, existing dim, beyond the chain off-ish
      put(x - 1, 1, x <= np_ ? (x == ep_ ? (bl_ ? 15 : 9) : (x == pg_ ? 11 : 4)) : 1, ROLE_CONTROL);
    put(8, 1, ti_ ? (bl_ ? 15 : 2) : 5, ROLE_CONTROL);  // tie blinks hard when armed
    put(10, 1, bl_ ? 9 : 4, ROLE_CONTROL);
    put(11, 1, bl_ ? 9 : 4, ROLE_CONTROL);
    int va = floormod(vg_ - 1, 8) + 1, vq = floordiv(vg_ - 1, 8);
    for (int x = 1; x <= 12; ++x) put(x - 1, 2, (x == vg_ || (x == va && vq == 0)) ? (bl_ ? 15 : 8) : (x == 1 ? 3 : 2), ROLE_CONTROL);
    put(0, 3, tpose_ < 0 ? 9 : 4, ROLE_CONTROL);
    put(1, 3, tpose_ > 0 ? 9 : 4, ROLE_CONTROL);
    put(2, 3, mute_ ? (bl_ ? 15 : 6) : 4, ROLE_CONTROL);
    put(7, 3, bl_ ? 12 : 5, ROLE_CONTROL);
    put(11, 3, 15, ROLE_CONTROL);
    return;
  }
  int base = (ep_ - 1) * 16;
  bool ph = (pg_ == ep_);
  for (int col = 0; col < 8; ++col) {
    int x = col + 1 + (half_ ? 8 : 0);
    bool inloop = (x >= lstart_ && x <= lend_);
    int vb = 5 + floordiv(c.vgv(vg_, x, 100) - 45, 6);  // note brightness = the velocity it will fire at
    int sv = steps_[base + x - 1];
    bool pl = ph && x == pos_;
    for (int row = 0; row < 4; ++row) {
      int deg = degAtRow(row), led = 0;
      uint8_t role = ROLE_STEP;
      if (floormod(deg, 7) == 0) led = 2;
      if (inloop && led == 0) { led = 1; role = ROLE_LOOP; }
      if (loopHeld_ && (x == lstart_ || x == lend_) && led < 3) led = 3;  // the loop ends show while Loop is held
      if (sv == deg) led = vb;
      if (pl) { int m = (sv == deg) ? 15 : 5; if (m > led) led = m; role = ROLE_PLAYHEAD; }
      put(col, row, led, role);
    }
  }
  put(8, 0, bl_ ? 9 : 4, ROLE_CONTROL);                       // window up
  put(8, 1, bl_ ? 9 : 4, ROLE_CONTROL);                       // window down
  put(8, 2, half_ ? 15 : 5, ROLE_CONTROL);                    // half
  put(8, 3, loopHeld_ ? 15 : 5, ROLE_CONTROL);                // loop
  put(9, 0, ep_ > 1 ? 9 : 3, ROLE_CONTROL);                   // page -
  put(9, 1, ep_ < 8 ? 9 : 3, ROLE_CONTROL);                   // page +
  put(9, 2, ti_ ? (bl_ ? 15 : 2) : 5, ROLE_CONTROL);          // tie
  put(10, 0, spPos_ > 1 ? 5 : 2, ROLE_CONTROL);               // slower
  put(10, 1, spPos_ < 12 ? 5 : 2, ROLE_CONTROL);              // faster
  put(10, 2, 3 + floormod(vg_ - 1, 8) / 2, ROLE_CONTROL);     // VEL- / VEL+ brighten with the pattern number
  put(11, 2, 3 + floormod(vg_ - 1, 8) / 2 + 1, ROLE_CONTROL);
  put(11, 3, 6, ROLE_CONTROL);                                // menu
}

}  // namespace seqgen
