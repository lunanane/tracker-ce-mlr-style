// Chord/Arp engine, port of c31 page 3 (lines 237-708) without the arranger. See README.md for the mapping.
#include "chordarp.h"

namespace seqgen {

// ---- c31 tables (page 3 header, lines 237-262) -------------------------------------------------
static const uint8_t kCam[4][6] = {{64, 69, 62, 67, 60, 65}, {61, 66, 71, 64, 69, 62},
                                   {71, 66, 61, 68, 63, 70}, {68, 63, 70, 65, 60, 67}};
static const uint8_t kScMaj[7] = {0, 2, 4, 5, 7, 9, 11};
static const uint8_t kScMin[7] = {0, 2, 3, 5, 7, 8, 10};
static const int8_t kCdeg[8][5] = {{0}, {0, 1, 4}, {0, 2, 4}, {0, 3, 4}, {0, 4}, {0, 2, 4, 5}, {0, 2, 4, 6}, {0, 2, 4, 6, 8}};
static const uint8_t kCdegN[8] = {1, 3, 3, 3, 2, 4, 4, 5};
static const int8_t kCabs[7][4] = {{0, 4, 7, 10}, {0, 4, 7, 11}, {0, 3, 6}, {0, 4, 8}, {0, 4, 6, 10}, {0, 4, 8, 10}, {0, 1, 4, 7}};
static const uint8_t kCabsN[7] = {4, 4, 3, 3, 4, 4, 4};
static const uint16_t kGlib[16] = {65535, 21845, 18761, 4369, 26214, 27437, 43690, 17476,
                                   4681, 9289, 21157, 257, 4112, 43947, 28013, 34952};
static const uint8_t kSm[4] = {5, 4, 3, 2};
static const uint8_t kPlv[4] = {0, 20, 45, 100};
static const uint8_t kPar[4][3] = {{2, 1, 1}, {3, 3, 1}, {3, 2, 2}, {2, 2, 1}};
static const uint8_t kMdiv[4] = {12, 8, 6, 4};
static const uint8_t kNsb[4] = {8, 12, 16, 12};
static const uint8_t kArpSq[8] = {1, 3, 2, 4, 3, 5, 4, 6};
static const uint8_t kPnLut[6] = {5, 6, 9, 11, 14, 15};  // eased note pulse, index = distance from the cycle edge

template <typename T> static inline T clampv(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---- SeqContext ------------------------------------------------------------------------------
void SeqContext::scale(int& root, bool& minor) const {
  if (chord) { chord->scaleSet(root, minor); return; }
  root = 60; minor = false;
}
int SeqContext::scaleDeg(int d) const {
  if (chord) return chord->scaleDeg(d);
  return 60 + 12 * floordiv(d, 7) + kScMaj[floormod(d, 7)];
}
int SeqContext::vgv(int i, int base) const { return chord ? chord->vgv(i, base) : base; }

// ---- construction / state --------------------------------------------------------------------
ChordArp::ChordArp() : ctx_(0), internalGate_(true) { ch_ = 3; reset(); }

void ChordArp::reset() {
  selMod_ = 1; selSize_ = 5; selT_ = 1; selS_ = 1; selD_ = 1; selG_ = 1; sw_ = 0; glen_ = 1; pmode_ = 1;
  parch_ = 1; panel_ = PANEL_TYPES; pon_ = false;
  PW_[0] = 2; PW_[1] = 1; PW_[2] = 0;
  pseed_ = 1;
  qT_ = qS_ = qD_ = qSize_ = qG_ = qMod_ = 0;
  ql_.valid = false; last_.valid = false; last_.x = last_.y = 0;
  lockR_ = 60; lockMn_ = false; centerHeld_ = false; amute_ = false;
  koct_ = 0; inv_ = 0; arpTpose_ = 0;
  lAptr_ = 1; lGptr_ = 1; llArp_ = -1; lgate_ = 0;
  for (int i = 0; i < 128; ++i) lN_[i] = false;
  ncpool3_ = 0; meas_ = 0; fc_ = 0; blink_ = false; pulse_ = 8; pn_ = 8;
  for (int i = 0; i < 4; ++i) held_[i] = false;
  msAcc_ = 0;
  pgen();
}

const char* ChordArp::chordTypeName(int mod) {
  static const char* const n[16] = {"?", "root", "sus2", "sus4m", "5th", "add6", "7", "9",
                                    "dom7", "maj7", "dim", "aug", "7b5", "7#5", "addb9", "?"};
  return (mod >= 0 && mod < 16) ? n[mod] : "?";
}

// ---- parametric groove (c31 pgen) ------------------------------------------------------------
void ChordArp::pgen() {
  uint16_t m = 0;
  for (int i = 0; i < 16; ++i) {
    int w = kPlv[PW_[(i % 4 == 0) ? 0 : ((i % 2 == 0) ? 1 : 2)]];
    if (((pseed_ * 97 + i * 41) % 251) * 100 < w * 251) m |= (uint16_t)(1u << i);
    pvt_[i] = (uint8_t)(76 + (32 * w) / 100);
  }
  pmask_ = m;
}

bool ChordArp::gbit(int g, int i) const {
  if (pon_) return ((pmask_ >> ((i - 1) % 16)) & 1) == 1;
  int idx = (g >= 1 && g <= 16) ? g : 1;
  return ((kGlib[idx - 1] >> (i - 1)) & 1) == 1;
}

// Groove step from bar position (c31 gstep). meas_ is 1..96 inside pulse().
int ChordArp::gstep(int sp) const {
  if (sp < 1 || sp > 4 || meas_ == 0) return 0;
  int d = kMdiv[sp - 1];
  int p = meas_ - 1;
  int t = p / d;
  if (p != t * d + ((t % 2 == 1) ? sw_ : 0)) return 0;
  return (t % kNsb[sp - 1]) + 1;
}

// ---- scale ------------------------------------------------------------------------------------
int ChordArp::rv(const RootRef& r) const {
  if (!r.valid || r.x < 1 || r.x > 4 || r.y < 1 || r.y > 6) return -1;
  return kCam[r.x - 1][r.y - 1];
}

void ChordArp::scaleSet(int& root, bool& minor) const {
  int r = -1; bool mn = false;
  if (pmode_ >= 2) { r = lockR_; mn = lockMn_; }
  else if (last_.valid) { r = rv(last_); mn = (last_.x % 2 == 0); }
  if (r < 0) { r = 60; mn = false; }
  root = 54 + floormod(r - 54, 12);
  minor = mn;
}

int ChordArp::scaleDeg(int d) const {
  int r; bool mn; scaleSet(r, mn);
  return r + 12 * floordiv(d, 7) + (mn ? kScMin : kScMaj)[floormod(d, 7)];
}

int ChordArp::vgv(int i, int base) const {
  if (pon_) return pvt_[floormod(i - 1, 16)];
  return base;
}

int ChordArp::dlk(int note) const {
  const uint8_t* iv = lockMn_ ? kScMin : kScMaj;
  int rel = note - lockR_, oct = floordiv(rel, 12), off = floormod(rel, 12);
  int b = 1, bd = 99;
  for (int i = 1; i <= 7; ++i) { int dd = iabs(iv[i - 1] - off); if (dd < bd) { bd = dd; b = i; } }
  return oct * 7 + (b - 1);
}
int ChordArp::lkd(int d) const {
  const uint8_t* iv = lockMn_ ? kScMin : kScMaj;
  return lockR_ + 12 * floordiv(d, 7) + iv[floormod(d, 7)];
}

// ---- chord pool (c31 c_pool) -----------------------------------------------------------------
int ChordArp::pool(int r, int m, bool mn, int16_t out[8]) const {
  int n = 0;
  if (amute_) return 0;
  m = clampv(m, 1, 15);
  int ko = 12 * koct_;
  bool lk = pmode_ >= 2;
  if (m > 8) {
    int rr = r;
    if (lk) rr = lkd(dlk(r));
    for (int i = 0; i < kCabsN[m - 9]; ++i) out[n++] = (int16_t)(rr + kCabs[m - 9][i] + ko);
  } else if (lk) {
    int d0 = dlk(r);
    for (int i = 0; i < kCdegN[m - 1]; ++i) out[n++] = (int16_t)(lkd(d0 + kCdeg[m - 1][i]) + ko);
  } else {
    const uint8_t* iv = mn ? kScMin : kScMaj;
    for (int i = 0; i < kCdegN[m - 1]; ++i) {
      int d = kCdeg[m - 1][i];
      out[n++] = (int16_t)(r + 12 * floordiv(d, 7) + iv[floormod(d, 7)] + ko);
    }
  }
  if (n > selSize_) n = selSize_;
  int T = arpTpose_ + (ctx_ ? ctx_->gtpose : 0);
  if (T != 0) {  // snap to scale degrees, shift by T, keep voices distinct
    int degs[8];
    int sr; bool smn; scaleSet(sr, smn);
    const uint8_t* siv = smn ? kScMin : kScMaj;
    for (int k = 0; k < n; ++k) {
      int rel = out[k] - sr, oct = floordiv(rel, 12), pc = floormod(rel, 12);
      int best = 99, bd = 0;
      for (int di = 0; di < 7; ++di) { int dd = iabs(siv[di] - pc); if (dd < best) { best = dd; bd = di; } }
      int dw = iabs(12 - pc); if (dw < best) { best = dw; bd = 7; }
      degs[k] = oct * 7 + bd;
    }
    for (int i = 1; i < n; ++i) { int v = degs[i], j = i - 1; while (j >= 0 && degs[j] > v) { degs[j + 1] = degs[j]; --j; } degs[j + 1] = v; }
    for (int i = 1; i < n; ++i) if (degs[i] <= degs[i - 1]) degs[i] = degs[i - 1] + 1;
    int m2 = 0;
    for (int i = 0; i < n; ++i) { int nn = scaleDeg(degs[i] + T); if (nn >= 0 && nn <= 127) out[m2++] = (int16_t)nn; }
    n = m2;
  }
  int iv2 = inv_;
  if (iv2 != 0 && n > 0) {
    for (int c = 0; c < iabs(iv2); ++c) {
      if (iv2 > 0) { int mi = 0; for (int j = 1; j < n; ++j) if (out[j] < out[mi]) mi = j; out[mi] = (int16_t)(out[mi] + 12); }
      else { int ma = 0; for (int j = 1; j < n; ++j) if (out[j] > out[ma]) ma = j; out[ma] = (int16_t)(out[ma] - 12); }
    }
  }
  return n;
}

int ChordArp::arp(const int16_t* p, int n, int d, int st, bool alt) const {
  if (n == 0) return -1;
  int l = n, pi = -1;
  if (d == 1) pi = p[(st - 1) % l];
  else if (d == 2) pi = p[l - ((st - 1) % l) - 1];
  else if (d == 3) { int cy = l * 2, ph = (st - 1) % cy; pi = ph < l ? p[ph] : p[cy - ph - 1]; }
  else if (d == 4) pi = p[(kArpSq[(st - 1) % 8] - 1) % l];
  if (alt && st % 2 == 0 && pi >= 0) pi += 12;
  return pi;
}

// ---- live layer -------------------------------------------------------------------------------
unsigned ChordArp::gateTicks() const { return glen_ == 1 ? 4 : (glen_ == 2 ? 2 : 0); }

void ChordArp::killLive() {  // c31 k_l
  for (int n = 0; n < 128; ++n) if (lN_[n]) { emit(EV_NOTE_OFF, n, 0); lN_[n] = false; }
  if (llArp_ >= 0) { emit(EV_NOTE_OFF, llArp_, 0); llArp_ = -1; }
}

void ChordArp::noteOnLive(int note, int vel, unsigned gateMs) {
  emit(EV_NOTE_ON, note, vel, gateMs);
}

void ChordArp::fl(const int16_t* p, int n) {  // sound a pool at the held-chord velocity
  for (int i = 0; i < n; ++i) {
    noteOnLive(p[i], 105, 0);
    if (p[i] >= 0 && p[i] <= 127) lN_[p[i]] = true;
  }
}

int ChordArp::liveAt(int r, bool mn, int16_t* out) const {
  if (pmode_ == 3) { for (int i = 0; i < ncpool3_; ++i) out[i] = cpool3_[i]; return ncpool3_; }
  return pool(r, selMod_, mn, out);
}

void ChordArp::tickLive(int rootNote) {
  bool mn = last_.valid && (last_.x % 2 == 0);
  int16_t p[24];
  if (selT_ == 1 && selS_ > 1) {
    int g = gstep(selS_);
    if (g) {
      lGptr_ = (uint8_t)g; killLive();
      if (gbit(selG_, g)) {
        int v = vgv(g, 105);
        int n = liveAt(rootNote, mn, p);
        unsigned gt = gateTicks();
        for (int i = 0; i < n; ++i) {
          noteOnLive(p[i], v, gt * kTickMs);
          if (p[i] >= 0 && p[i] <= 127) lN_[p[i]] = true;
        }
        lgate_ = internalGate_ ? (uint8_t)gt : 0;
      }
    }
  } else if (selT_ == 2 || selT_ == 3) {
    int g = gstep(selS_);
    if (g) {
      lGptr_ = (uint8_t)g;
      if (gbit(selG_, g)) {
        if (llArp_ >= 0) { emit(EV_NOTE_OFF, llArp_, 0); llArp_ = -1; }
        int n = liveAt(rootNote, mn, p);
        int pn = arp(p, n, selD_, (int)lAptr_, selT_ == 3);
        if (pn >= 0) {
          int v = vgv(g, 105);
          unsigned gt = gateTicks();
          noteOnLive(pn, v, gt * kTickMs);
          llArp_ = (pn <= 127) ? (int16_t)pn : (int16_t)-1;
          lgate_ = internalGate_ ? (uint8_t)gt : 0;
        }
        lAptr_++;
      }
    }
  }
}

void ChordArp::applyQuarter() {  // c31 clock_pulse_tick, g_meas%24==0 block
  int16_t p[24];
  if (ql_.valid) {
    QRoot q = ql_; ql_.valid = false; qMod_ = 0; killLive(); selMod_ = q.m;
    if (q.t == 1 && q.s == 1) {
      lgate_ = 0;
      int n;
      if (pmode_ == 3) { n = ncpool3_; for (int i = 0; i < n; ++i) p[i] = cpool3_[i]; }
      else n = pool(q.rv, q.m, q.mn, p);
      fl(p, n);
    } else if (q.t == 2 || q.t == 3) lAptr_ = 1;
  } else if (qMod_) {
    int qm = qMod_; qMod_ = 0; selMod_ = (uint8_t)qm; killLive();
    int r = rv(last_);
    if (selT_ == 1 && selS_ == 1) {
      if (r >= 0) { int n = pool(r, qm, last_.valid && (last_.x % 2 == 0), p); fl(p, n); }
    } else if (selT_ == 2 || selT_ == 3) lAptr_ = 1;
  }
}

void ChordArp::barTick() {
  bool qc = false;
  if (qT_) { selT_ = qT_; qT_ = 0; qc = true; }
  if (qS_) { selS_ = qS_; qS_ = 0; qc = true; }
  if (qD_) { selD_ = qD_; qD_ = 0; qc = true; }
  if (qSize_) { selSize_ = qSize_; qSize_ = 0; qc = true; }
  if (qG_) { selG_ = qG_; qG_ = 0; qc = true; }
  if (qc) lAptr_ = 1;
}

void ChordArp::pulse() {
  meas_++;
  if (meas_ % 24 == 0) applyQuarter();
  int r = rv(last_);
  if (r >= 0) tickLive(r);
  if (meas_ >= kBarPulses) { meas_ = 0; barTick(); }
}

void ChordArp::start() { meas_ = 0; }  // arranger resets (bar_c, g_ptr, a_ptr, c_stage) have no counterpart here

void ChordArp::stop() { killLive(); }  // c31 252 leaves a pending gate timer armed (it only calls k_l)

void ChordArp::tick() {
  fc_ = (uint8_t)((fc_ + 1) % 48);
  blink_ = (fc_ % 16) < 8;
  int k = fc_ < 24 ? fc_ : 48 - fc_;
  pulse_ = (uint8_t)(8 + (14 * k + 24) / 48);  // 8 + floor(k*7/24 + 0.5)
  int j = fc_ % 10;
  pn_ = kPnLut[j < 5 ? j : 10 - j];
  if (lgate_) { if (lgate_ <= 1) { killLive(); lgate_ = 0; } else lgate_--; }
}

void ChordArp::advanceMs(uint32_t dt) {
  msAcc_ += dt;
  while (msAcc_ >= kTickMs) { msAcc_ -= kTickMs; tick(); }
}

// ---- mode transitions -------------------------------------------------------------------------
void ChordArp::lockTransition(int mode, bool hold) {  // c31 x14 play-mode radio, press
  killLive();
  ncpool3_ = 0; last_.valid = false; ql_.valid = false;
  pmode_ = (uint8_t)mode;
  centerHeld_ = (mode == 2) && hold;
}

void ChordArp::lockButton(bool down) {
  if (down) lockTransition(2, true);
  else centerHeld_ = false;
}

// ---- pads -------------------------------------------------------------------------------------
void ChordArp::pad(int col, int row, bool down) {
  if (col < 0 || col >= kCols || row < 0 || row >= kRows) return;
  if (col <= 5) gridRootEvent(row + 1, col + 1, down);
  else if (col <= 9) panelEvent(col - 6, row, down);
  else controlEvent(row * 2 + (col - 10), down);
}

void ChordArp::gridRootEvent(int x, int y, bool down) {  // c31 event(): chord roots / scale grid
  int16_t p[24];
  if (pmode_ == 3) {
    int n = lkd((x - 1) * 6 + (6 - y));
    if (down) {
      bool f = false;
      for (int i = 0; i < ncpool3_; ++i) if (cpool3_[i] == n) { f = true; break; }
      if (!f && ncpool3_ < 24) cpool3_[ncpool3_++] = (int16_t)n;
      if (!last_.valid) {
        last_.valid = true; last_.x = (int8_t)x; last_.y = (int8_t)y;
        if (selT_ == 1 && selS_ == 1) fl(cpool3_, ncpool3_);
        else {
          ql_.valid = true; ql_.rv = (int16_t)n; ql_.t = selT_; ql_.s = selS_; ql_.m = selMod_; ql_.mn = false;
          ql_.cx = (int8_t)x; ql_.cy = (int8_t)y;
        }
      } else if (selT_ == 1 && selS_ == 1 && !f) {
        noteOnLive(n, 105, 0);
        if (n >= 0 && n <= 127) lN_[n] = true;
      }
    } else {
      for (int i = 0; i < ncpool3_; ++i) if (cpool3_[i] == n) {
        for (int j = i; j + 1 < ncpool3_; ++j) cpool3_[j] = cpool3_[j + 1];
        ncpool3_--; break;
      }
      if (selT_ == 1 && selS_ == 1) { emit(EV_NOTE_OFF, n, 0); if (n >= 0 && n <= 127) lN_[n] = false; }
      if (ncpool3_ == 0) { last_.valid = false; ql_.valid = false; killLive(); }
    }
    return;
  }
  if (down) {
    RootRef cand; cand.valid = true; cand.x = (int8_t)x; cand.y = (int8_t)y;
    int r = rv(cand);
    if (centerHeld_) { lockR_ = (int16_t)r; lockMn_ = (x % 2 == 0); return; }
    if (r >= 0) {
      if (pmode_ == 1) { lockR_ = (int16_t)r; lockMn_ = (x % 2 == 0); }
      last_ = cand;
      int pm = qMod_ ? qMod_ : selMod_; qMod_ = 0;
      if (selT_ == 1 && selS_ == 1) {
        int n = pool(r, selMod_, x % 2 == 0, p); fl(p, n);
      } else {
        ql_.valid = true; ql_.rv = (int16_t)r; ql_.t = selT_; ql_.s = selS_; ql_.m = (uint8_t)pm; ql_.mn = (x % 2 == 0);
        ql_.cx = (int8_t)x; ql_.cy = (int8_t)y;
      }
    }
  } else if (last_.valid && last_.x == x && last_.y == y) {
    last_.valid = false; ql_.valid = false; killLive();
  }
}

void ChordArp::panelEvent(int pc, int pr, bool down) {
  if (!down) return;
  int16_t p[24];
  switch (panel_) {
    case PANEL_TYPES: {  // direct pads 1..15 (c31 chord mod pads incl. the two-button extended types)
      int mv = pr * 4 + pc + 1;
      if (mv > 15) return;
      if (selT_ == 1 && selS_ == 1) {
        selMod_ = (uint8_t)mv; qMod_ = 0; killLive();
        if (last_.valid) { int n = pool(rv(last_), mv, last_.x % 2 == 0, p); fl(p, n); }
      } else if (ql_.valid) { ql_.m = (uint8_t)mv; qMod_ = 0; }
      else qMod_ = (uint8_t)mv;
      break;
    }
    case PANEL_SETTINGS: {
      int v = pc + 1;
      if (pr == 0) qT_ = (uint8_t)v;
      else if (pr == 1) qS_ = (uint8_t)v;
      else if (pr == 2) qD_ = (uint8_t)v;
      else qSize_ = kSm[v - 1];
      break;
    }
    case PANEL_GROOVE: qG_ = (uint8_t)(pr * 4 + pc + 1); break;
    case PANEL_PARAM:
      if (pr <= 2) PW_[pr] = (uint8_t)pc;
      else {
        int a = pc + 1;
        if (a == parch_) pseed_ = (pseed_ * 7 + 29) % 251;  // re-tap the active archetype: reroll
        else { parch_ = (uint8_t)a; for (int j = 0; j < 3; ++j) PW_[j] = kPar[a - 1][j]; }
      }
      pgen();
      break;
  }
}

void ChordArp::controlEvent(int idx, bool down) {
  if (idx >= 4) held_[idx - 4] = down;
  if (!down) return;
  switch (idx) {
    case 0: panel_ = PANEL_TYPES; break;
    case 1: panel_ = PANEL_SETTINGS; break;
    case 2: panel_ = PANEL_GROOVE; break;
    case 3:
      if (panel_ == PANEL_PARAM) pon_ = !pon_;  // re-tap PARAM = parametric groove on/off
      else panel_ = PANEL_PARAM;
      break;
    case 4: case 5:  // INV- / INV+ ; both = reset
      if (held_[0] && held_[1]) inv_ = 0;
      else if (idx == 5) inv_ = (int8_t)(inv_ + 1 > 6 ? 6 : inv_ + 1);
      else inv_ = (int8_t)(inv_ - 1 < -6 ? -6 : inv_ - 1);
      break;
    case 6: case 7:  // OCT- / OCT+ ; both = 0
      if (held_[2] && held_[3]) koct_ = 0;
      else if (idx == 7) koct_ = (int8_t)(koct_ + 1 > 1 ? 1 : koct_ + 1);
      else koct_ = (int8_t)(koct_ - 1 < -1 ? -1 : koct_ - 1);
      break;
  }
}

// ---- params -----------------------------------------------------------------------------------
int ChordArp::getParam(int id) const {
  switch (id) {
    case P_PMODE: return pmode_;
    case P_LOCK_ROOT: return lockR_;
    case P_LOCK_MINOR: return lockMn_;
    case P_SWING: return sw_;
    case P_GATE: return glen_ - 1;
    case P_SEL_TYPE: return selT_;
    case P_SEL_SPEED: return selS_;
    case P_SEL_DIR: return selD_;
    case P_SEL_SIZE: return selSize_;
    case P_SEL_GROOVE: return selG_;
    case P_SEL_MOD: return selMod_;
    case P_KOCT: return koct_;
    case P_INV: return inv_;
    case P_PON: return pon_;
    case P_PARCH: return parch_;
    case P_PW0: case P_PW1: case P_PW2: return PW_[id - P_PW0];
    case P_PSEED: return pseed_;
    case P_TPOSE: return arpTpose_;
    case P_MUTE: return amute_;
    case P_PANEL: return panel_;
    case P_Q_TYPE: return qT_;
    case P_Q_SPEED: return qS_;
    case P_Q_DIR: return qD_;
    case P_Q_SIZE: return qSize_;
    case P_Q_GROOVE: return qG_;
    case P_Q_MOD: return ql_.valid ? ql_.m : qMod_;
    case P_Q_ROOT: return ql_.valid;
  }
  return 0;
}

void ChordArp::setParam(int id, int v) {
  switch (id) {
    case P_PMODE: lockTransition(clampv(v, 1, 3), false); break;
    case P_LOCK_ROOT: lockR_ = (int16_t)clampv(v, 0, 127); break;
    case P_LOCK_MINOR: lockMn_ = v != 0; break;
    case P_SWING: sw_ = (uint8_t)clampv(v, 0, 2); break;
    case P_GATE: glen_ = (uint8_t)(clampv(v, 0, 2) + 1); break;
    case P_SEL_TYPE: selT_ = (uint8_t)clampv(v, 1, 4); lAptr_ = 1; break;
    case P_SEL_SPEED: selS_ = (uint8_t)clampv(v, 1, 4); lAptr_ = 1; break;
    case P_SEL_DIR: selD_ = (uint8_t)clampv(v, 1, 4); lAptr_ = 1; break;
    case P_SEL_SIZE: selSize_ = (uint8_t)clampv(v, 2, 5); lAptr_ = 1; break;
    case P_SEL_GROOVE: selG_ = (uint8_t)clampv(v, 1, 16); lAptr_ = 1; break;
    case P_SEL_MOD: selMod_ = (uint8_t)clampv(v, 1, 15); break;
    case P_KOCT: koct_ = (int8_t)clampv(v, -1, 1); break;
    case P_INV: inv_ = (int8_t)clampv(v, -6, 6); break;
    case P_PON: pon_ = v != 0; break;
    case P_PARCH:
      parch_ = (uint8_t)clampv(v, 1, 4);
      for (int j = 0; j < 3; ++j) PW_[j] = kPar[parch_ - 1][j];
      pgen();
      break;
    case P_PW0: case P_PW1: case P_PW2: PW_[id - P_PW0] = (uint8_t)clampv(v, 0, 3); pgen(); break;
    case P_PSEED: pseed_ = clampv(v, 0, 250); pgen(); break;
    case P_TPOSE: arpTpose_ = (int8_t)clampv(v, -24, 24); break;
    case P_MUTE: amute_ = v != 0; if (amute_) killLive(); break;  // c31 keeps a pending gate timer
    case P_PANEL: panel_ = (uint8_t)clampv(v, 0, 3); break;
  }
}

// ---- serialization ----------------------------------------------------------------------------
size_t ChordArp::serialize(uint8_t* b, size_t cap) const {
  if (cap < (size_t)kSerialSize) return 0;
  for (int i = 0; i < kSerialSize; ++i) b[i] = 0;
  b[0] = kSerialVersion; b[1] = pmode_;
  b[2] = (uint8_t)(lockR_ & 0xff); b[3] = (uint8_t)((lockR_ >> 8) & 0xff);
  b[4] = lockMn_; b[5] = sw_; b[6] = glen_; b[7] = selT_; b[8] = selS_; b[9] = selD_; b[10] = selSize_;
  b[11] = selG_; b[12] = selMod_; b[13] = (uint8_t)koct_; b[14] = (uint8_t)inv_; b[15] = pon_; b[16] = parch_;
  b[17] = PW_[0]; b[18] = PW_[1]; b[19] = PW_[2];
  b[20] = (uint8_t)(pseed_ & 0xff); b[21] = (uint8_t)((pseed_ >> 8) & 0xff);
  b[22] = (uint8_t)arpTpose_; b[23] = amute_; b[24] = panel_;
  return kSerialSize;
}

bool ChordArp::deserialize(const uint8_t* b, size_t len) {
  if (len < (size_t)kSerialSize || b[0] != kSerialVersion) return false;
  int pm = b[1], sw = b[5], gl = b[6], t = b[7], s = b[8], d = b[9], sz = b[10], g = b[11], mod = b[12];
  int pa = b[16], pseed = b[20] | (b[21] << 8), pn = b[24];
  if (pm < 1 || pm > 3 || sw > 2 || gl < 1 || gl > 3 || t < 1 || t > 4 || s < 1 || s > 4 || d < 1 || d > 4 ||
      sz < 2 || sz > 5 || g < 1 || g > 16 || mod < 1 || mod > 15 || pa < 1 || pa > 4 || pseed > 250 || pn > 3 ||
      b[17] > 3 || b[18] > 3 || b[19] > 3 || (int8_t)b[13] < -1 || (int8_t)b[13] > 1 || (int8_t)b[14] < -6 ||
      (int8_t)b[14] > 6)
    return false;
  killLive();
  lgate_ = 0; ncpool3_ = 0; last_.valid = false; ql_.valid = false;
  qT_ = qS_ = qD_ = qSize_ = qG_ = qMod_ = 0; centerHeld_ = false; lAptr_ = 1;
  pmode_ = (uint8_t)pm; lockR_ = (int16_t)(b[2] | (b[3] << 8)); lockMn_ = b[4] != 0; sw_ = (uint8_t)sw;
  glen_ = (uint8_t)gl; selT_ = (uint8_t)t; selS_ = (uint8_t)s; selD_ = (uint8_t)d; selSize_ = (uint8_t)sz;
  selG_ = (uint8_t)g; selMod_ = (uint8_t)mod; koct_ = (int8_t)b[13]; inv_ = (int8_t)b[14]; pon_ = b[15] != 0;
  parch_ = (uint8_t)pa; PW_[0] = b[17]; PW_[1] = b[18]; PW_[2] = b[19]; pseed_ = pseed;
  arpTpose_ = (int8_t)b[22]; amute_ = b[23] != 0; panel_ = (uint8_t)pn;
  pgen();
  return true;
}

// ---- LEDs (c31 draw(), lines 480-560, on the 12x4 layout) ------------------------------------
void ChordArp::leds(uint8_t out[kPads], uint8_t* roles) const {
  uint8_t dummy[kPads];
  uint8_t* ro = roles ? roles : dummy;
  for (int i = 0; i < kPads; ++i) { out[i] = 0; ro[i] = ROLE_NONE; }
  int pul = pulse_;
  // roots / scale grid, cols 0..5
  int lcx = 0, lcy = 0;
  if (pmode_ == 2) {  // g_co(lock_r): first cam cell with that note
    for (int x = 1; x <= 4 && !lcx; ++x)
      for (int y = 1; y <= 6; ++y)
        if (kCam[x - 1][y - 1] == lockR_) { lcx = x; lcy = y; break; }
  }
  for (int row = 0; row < 4; ++row) for (int col = 0; col < 6; ++col) {
    int x = row + 1, y = col + 1, led;
    uint8_t role;
    if (pmode_ == 3) {
      int deg = (x - 1) * 6 + (6 - y);
      led = (deg % 7 == 0) ? (blink_ ? 15 : 6) : 5;
      int gn = lkd(deg);
      for (int i = 0; i < ncpool3_; ++i) if (cpool3_[i] == gn) { led = 15; break; }
      role = ROLE_DEGREE;
    } else {
      led = (x == 1 || x == 3) ? 5 : 3;
      if (lcx == x && lcy == y) led = blink_ ? 15 : 4;
      if (last_.valid && last_.x == x && last_.y == y) led = 15;
      role = (x % 2 == 0) ? ROLE_ROOT_MINOR : ROLE_ROOT_MAJOR;
    }
    out[row * 12 + col] = (uint8_t)led; ro[row * 12 + col] = role;
  }
  if (ql_.valid) out[(ql_.cx - 1) * 12 + (ql_.cy - 1)] = (uint8_t)pul;
  // panel, cols 6..9
  for (int row = 0; row < 4; ++row) for (int pc = 0; pc < 4; ++pc) {
    int led = 0;
    int idx = row * 4 + pc + 1;
    if (panel_ == PANEL_TYPES) {
      int eff = ql_.valid ? ql_.m : qMod_;
      if (idx <= 15) led = (idx == selMod_) ? 15 : ((eff && idx == eff) ? pul : 3);
    } else if (panel_ == PANEL_SETTINGS) {
      int v = pc + 1;
      if (row == 0) led = ((qT_ ? qT_ : selT_) == v) ? pul : 0;
      else if (row == 1) led = ((qS_ ? qS_ : selS_) == v) ? pul : 0;
      else if (row == 2) led = ((qD_ ? qD_ : selD_) == v) ? pul : 0;
      else led = (kSm[pc] == (qSize_ ? qSize_ : selSize_)) ? pul : 0;
    } else if (panel_ == PANEL_GROOVE) {
      int ag = qG_ ? qG_ : selG_;
      led = (idx == ag) ? (gbit(ag, lGptr_) ? 15 : 7) : 6;
    } else {
      if (row < 3) led = (pc == PW_[row]) ? (pon_ ? (((pmask_ >> ((lGptr_ - 1) % 16)) & 1) ? 15 : 10) : 5) : 2;
      else led = ((pc + 1) == parch_) ? (pon_ ? 12 : 5) : 2;
    }
    out[row * 12 + 6 + pc] = (uint8_t)led; ro[row * 12 + 6 + pc] = ROLE_PANEL;
  }
  // controls, cols 10..11
  for (int i = 0; i < 8; ++i) {
    int led;
    switch (i) {
      case 0: case 1: case 2: led = (panel_ == i) ? 15 : 4; break;
      case 3: led = (panel_ == PANEL_PARAM) ? 15 : (pon_ ? 8 : 4); break;
      case 4: led = inv_ < 0 ? (6 - inv_ * 3 > 15 ? 15 : 6 - inv_ * 3) : 2; break;
      case 5: led = inv_ > 0 ? (6 + inv_ * 3 > 15 ? 15 : 6 + inv_ * 3) : 2; break;
      case 6: led = koct_ == -1 ? 15 : 3; break;
      default: led = koct_ == 1 ? 15 : 3; break;
    }
    int row = i / 2, col = 10 + (i % 2);
    out[row * 12 + col] = (uint8_t)led; ro[row * 12 + col] = ROLE_CONTROL;
  }
}

}  // namespace seqgen
