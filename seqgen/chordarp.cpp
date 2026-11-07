// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Chord/Arp engine, port of c34 page 3 without the arranger. See README.md for the mapping.
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
static const uint8_t kMdiv[10] = {12, 8, 6, 4, 24, 16, 3, 2, 48, 32};
static const uint8_t kNsb[10] = {8, 12, 16, 12, 4, 6, 16, 12, 2, 3};
static const uint16_t kLM[5] = {0, 17476, 34952, 4369, 43690};  // gate lanes 1..4 (c34 LM)
static const uint8_t kP1[6] = {1, 1, 1, 2, 2, 3}, kP2[6] = {2, 3, 4, 3, 4, 4};  // pair index 5..10 -> pads
// c34 VP: velocity patterns 1..7 as 16 levels a..p per step (pattern 8 reuses 2 with a per-bar shape)
static const char kVP[] = "pppppppppppppppppglgnglgnglgnglgmgjgpgjgmgjgpgjglhphlhphlhphlhphpdjdpdjdpdjdpdjdffghhijjklmmnooppoonmmlkjjihhgff";
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

int vgvCalc(int g, int i, int base, int gb) {
  int p = floormod(i - 1, 16), a = floormod(g - 1, 8) + 1, b = floordiv(g - 1, 8);
  if (b == 0) b = a;
  int m = (gb * 5 + p * 7 + p * p) % 9, N = 0;  // pattern 8 is scaled by h = (20 - m) / 20
  for (int n = 0; n < 2; ++n) {
    int k = n == 0 ? a : b;
    int v = kVP[((k == 8 ? 2 : k) - 1) * 16 + p] - 'a' + 1;
    N += v * (k == 8 ? 20 - m : 20);
  }
  return (2 * base * (5760 + 11 * N) + 12800) / 25600;  // floor(base*(0.45+0.55*L/32)+0.5), L = N/20
}

// ---- construction / state --------------------------------------------------------------------
ChordArp::ChordArp() : ctx_(0), internalGate_(true) { ch_ = 3; reset(); }

void ChordArp::reset() {
  selMod_ = 1; selSize_ = 5; selT_ = 1; selS_ = 1; selD_ = 1; selG_ = 1; sw_ = 0; glen_ = 1; pmode_ = 1;
  parch_ = 1; page_ = PAGE_PLAY; editPage_ = PAGE_GROOVE; pon_ = false;
  PW_[0] = 2; PW_[1] = 1; PW_[2] = 0;
  pseed_ = 1;
  selVg_ = 1; lane_ = 0; orng_ = 1; ownGb_ = 0; xh_[0] = xh_[1] = 0; gbPending_ = false; gx_ = 0; rng_ = 2463534242u;
  qT_ = qS_ = qD_ = qSize_ = qG_ = qMod_ = 0;
  ql_.valid = false; last_.valid = false; last_.x = last_.y = 0;
  lockR_ = 60; lockMn_ = false; centerHeld_ = false; amute_ = false;
  koct_ = 0; inv_ = 0; arpTpose_ = 0;
  lAptr_ = 1; lGptr_ = 1; llArp_ = -1; lgate_ = 0;
  for (int i = 0; i < 128; ++i) lN_[i] = false;
  lLo_ = 128; lHi_ = 0;
  ncpool3_ = 0; meas_ = 0; fc_ = 0; blink_ = false; pulse_ = 8; pn_ = 8;
  for (int i = 0; i < 4; ++i) held_[i] = false;
  vh_ = lh_ = 0; modHeld_ = false; padLock_ = false;
  for (int i = 0; i < kPads; ++i) downPage_[i] = 0xFF;
  msAcc_ = 0;
  pgen();
}

const char* ChordArp::chordTypeName(int mod) {
  static const char* const n[16] = {"?", "Root", "Sus2", "Triad", "Sus4", "5th", "6th", "7th", "9th",
                                    "Dom7", "Maj7", "Dim", "Aug", "7b5", "7#5", "b9"};  // index = type pad value (c34 cdeg / cabs)
  return (mod >= 0 && mod < 16) ? n[mod] : "?";
}

// ---- parametric groove (c31 pgen) ------------------------------------------------------------
void ChordArp::pgen() {
  uint16_t m = 0;
  for (int i = 0; i < 16; ++i) {
    int w = kPlv[PW_[(i % 4 == 0) ? 0 : ((i % 2 == 0) ? 1 : 2)]];
    if (((pseed_ * 97 + i * 41) % 251) * 100 < w * 251) m |= (uint16_t)(1u << i);
  }
  pmask_ = m;
}

bool ChordArp::gbit(int g, int i) const {
  if (pon_) return ((pmask_ >> ((i - 1) % 16)) & 1) == 1;
  int idx = (g >= 1 && g <= 16) ? g : 1;
  return ((kGlib[idx - 1] >> (i - 1)) & 1) == 1;
}

// Groove step from bar position (c31 gstep, c34: swing capped below the step length, gate per step).
// meas_ is 1..96 inside pulse().
int ChordArp::gstep(int sp) const {
  if (sp < 1 || sp > 10 || meas_ == 0) return 0;
  int d = kMdiv[sp - 1];
  int p = meas_ - 1;
  int t = p / d;
  int swv = sw_ < d - 1 ? sw_ : d - 1;
  if (p != t * d + ((t % 2 == 1) ? swv : 0)) return 0;
  int n = (t % kNsb[sp - 1]) + 1;
  int lo = lane_ % 8, hi = lane_ / 8;
  unsigned mask = (lo <= 4 ? kLM[lo] : 0u) | (hi <= 4 ? kLM[hi] : 0u);
  gx_ = ((mask >> (n - 1)) & 1) ? 7 : (uint8_t)gateTicks();
  return n;
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

// ---- chord pool (c34 c_pool: transpose T is applied while the pool is built, in the chord's own key) --------
int ChordArp::pool(int r, int m, bool mn, int16_t out[8]) const {
  int raw[8], n = 0;
  if (amute_) return 0;
  m = clampv(m, 1, 15);
  int ko = 12 * koct_;
  bool lk = pmode_ >= 2;
  int T = arpTpose_ + (ctx_ ? ctx_->gtpose : 0);
  const uint8_t* iv = mn ? kScMin : kScMaj;
  if (m > 8) {
    int rr = r + 12 * floordiv(T, 7) + iv[floormod(T, 7)];
    if (lk) rr = lkd(dlk(r) + T);
    for (int i = 0; i < kCabsN[m - 9]; ++i) raw[n++] = rr + kCabs[m - 9][i] + ko;
  } else if (lk) {
    int d0 = dlk(r) + T;
    for (int i = 0; i < kCdegN[m - 1]; ++i) raw[n++] = lkd(d0 + kCdeg[m - 1][i]) + ko;
  } else {
    for (int i = 0; i < kCdegN[m - 1]; ++i) {
      int d = kCdeg[m - 1][i] + T;
      raw[n++] = r + 12 * floordiv(d, 7) + iv[floormod(d, 7)] + ko;
    }
  }
  int k = 0;
  for (int i = 0; i < n; ++i) if (i < selSize_ && raw[i] >= 0 && raw[i] <= 127) out[k++] = (int16_t)raw[i];
  n = k;
  int iv2 = inv_;
  if (iv2 != 0 && n > 0) {
    for (int c = 0; c < iabs(iv2); ++c) {
      if (iv2 > 0) { int mi = 0; for (int j = 1; j < n; ++j) if (out[j] < out[mi]) mi = j; out[mi] = (int16_t)(out[mi] + 12); }
      else { int ma = 0; for (int j = 1; j < n; ++j) if (out[j] > out[ma]) ma = j; out[ma] = (int16_t)(out[ma] - 12); }
    }
  }
  return n;
}

uint32_t ChordArp::rnd(int l) {  // xorshift32, stands in for math.random(l) so runs are reproducible
  uint32_t x = rng_;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  rng_ = x;
  return 1 + x % (uint32_t)l;
}

int ChordArp::arp(const int16_t* p0, int n, int d, int st, bool alt) {
  if (n == 0) return -1;
  int16_t q[24];
  const int16_t* p = p0;
  int l = n;
  if (orng_ > 1) {  // arp octave range
    l = 0;
    for (int o = 0; o < orng_; ++o) for (int i = 0; i < n; ++i) if (p0[i] + 12 * o < 128 && l < 24) q[l++] = (int16_t)(p0[i] + 12 * o);
    p = q;
    if (l == 0) return -1;
  }
  int pi;
  if (d == 1) pi = p[(st - 1) % l];
  else if (d == 2) pi = p[l - ((st - 1) % l) - 1];
  else if (d == 3) { int cy = l * 2, ph = (st - 1) % cy; pi = ph < l ? p[ph] : p[cy - ph - 1]; }
  else if (d == 4) pi = p[(kArpSq[(st - 1) % 8] - 1) % l];
  else {
    int j = (st - 1) % l, cy = 2 * l - 2 > 1 ? 2 * l - 2 : 1, ph = (st - 1) % cy, e = ph < l ? ph : cy - ph;
    if (d == 5) pi = p[e];
    else if (d == 6) pi = p[l - e - 1];
    else if (d == 7) pi = p[rnd(l) - 1];
    else if (d == 8) pi = p[(j % 2 == 0) ? j / 2 : l - j / 2 - 1];
    else if (d == 9) { int mm = (l + 1) / 2; pi = p[(j % 2 == 0) ? mm - j / 2 - 1 : mm + j / 2]; }
    else pi = p[(ph % 2 == 0) ? 0 : ph / 2 + 1];
  }
  if (alt && st % 2 == 0) pi += 12;
  return pi;
}

// ---- live layer -------------------------------------------------------------------------------
unsigned ChordArp::gateTicks() const { return glen_ == 1 ? 4 : (glen_ == 2 ? 2 : 0); }

void ChordArp::markLive(int n) {
  lN_[n] = true;
  if (n < lLo_) lLo_ = (uint8_t)n;
  if (n > lHi_) lHi_ = (uint8_t)n;
}

void ChordArp::killLive() {  // c31 k_l
  for (int n = lLo_; n <= lHi_; ++n) if (lN_[n]) { emit(EV_NOTE_OFF, n, 0); lN_[n] = false; }  // only the range ever set
  lLo_ = 128; lHi_ = 0;
  if (llArp_ >= 0) { emit(EV_NOTE_OFF, llArp_, 0); llArp_ = -1; }
}

void ChordArp::noteOnLive(int note, int vel, unsigned gateMs) {
  emit(EV_NOTE_ON, note, vel, gateMs);
}

void ChordArp::fl(const int16_t* p, int n) {  // sound a pool at the held-chord velocity
  for (int i = 0; i < n; ++i) {
    noteOnLive(p[i], 105, 0);
    if (p[i] >= 0 && p[i] <= 127) markLive(p[i]);
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
        int v = vgvSel(g, 105);
        int n = liveAt(rootNote, mn, p);
        unsigned gt = gx_;
        for (int i = 0; i < n; ++i) {
          noteOnLive(p[i], v, gt * kTickMs);
          if (p[i] >= 0 && p[i] <= 127) markLive(p[i]);
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
          int v = vgvSel(g, 105);
          unsigned gt = gx_;
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

void ChordArp::applyBar() {
  if (!gbPending_) return;
  gbPending_ = false;
  if (ctx_) ctx_->barTick(); else ownGb_ = (uint8_t)((ownGb_ + 1) % 9);
}

void ChordArp::pulse() {
  applyBar();
  meas_++;
  if (meas_ % 24 == 0) applyQuarter();
  int r = rv(last_);
  if (r >= 0) tickLive(r);
  if (meas_ >= kBarPulses) {
    meas_ = 0;
    gbPending_ = true;  // c34 counts the bar after the other pages ran their pulse; applied at the next pulse/start
    barTick();
  }
}

void ChordArp::start() { applyBar(); meas_ = 0; }  // arranger resets (bar_c, g_ptr, a_ptr, c_stage) have no counterpart here

void ChordArp::retrigger() {
  const int r = rv(last_);
  if (r < 0 || selT_ != 1 || selS_ != 1) return;
  int16_t p[24];
  killLive();
  lgate_ = 0;
  fl(p, liveAt(r, last_.valid && (last_.x % 2 == 0), p));
}

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
// Pad map (col, row), row 0 = top. Cols 0..5 = the 24 roots (always; c34 x1-4 / y1-6 turned: row = x-1, col = y-1).
// Cols 6..11 = the visible page (Page). PLAY page: cols 6-7 chord types 1..8 (hold Root + pad = types 9..15), row 0 cols 8-10
// Chord / Arp / Arp+Oct, row 1 speed, row 2 direction, row 3 Inv- Inv+ Oct- Oct+, (11,0) = Page (opens the last edit page).
// Edit pages: col 11 = tabs Play / Groove / Feel / Setup (rows 0..3). See README "Chord/Arp pads" for the whole table.
void ChordArp::setPage(int p) {
  page_ = (uint8_t)p;
  if (p) editPage_ = (uint8_t)p;
  xh_[0] = xh_[1] = 0; vh_ = lh_ = 0; modHeld_ = false;  // gestures held on the page we leave are over (their releases are ignored)
  for (int i = 0; i < 4; ++i) held_[i] = false;
  if (padLock_) { centerHeld_ = false; padLock_ = false; }
}

void ChordArp::pad(int col, int row, bool down) {
  if (col < 0 || col >= kCols || row < 0 || row >= kRows) return;
  if (col <= 5) { gridRootEvent(row + 1, col + 1, down); return; }
  int idx = row * kCols + col, pg;
  if (down) { downPage_[idx] = page_; pg = page_; }
  else { pg = downPage_[idx] == 0xFF ? page_ : downPage_[idx]; downPage_[idx] = 0xFF; }  // the release belongs to the page of the press
  if (col == 11 && (pg != PAGE_PLAY || row == 0)) {  // page pad / tabs
    if (down) setPage(pg == PAGE_PLAY ? editPage_ : row);
    return;
  }
  switch (pg) {
    case PAGE_PLAY: playPage(col, row, down); break;
    case PAGE_GROOVE: groovePage(col, row, down); break;
    case PAGE_FEEL: feelPage(col, row, down); break;
    default: setupPage(col, row, down); break;
  }
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
        if (n >= 0 && n <= 127) markLive(n);
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

// c34 chord mod pads (x1-4, y7-8): single types 1..8; pad 1 is also a modifier: hold it and tap pad 2..8 for the extended types 9..15.
void ChordArp::typePad(int m, bool down) {
  if (!down) { if (m == 1) modHeld_ = false; return; }
  int16_t p[24];
  int mv = (modHeld_ && m > 1) ? 8 + (m - 1) : m;
  if (m == 1) modHeld_ = true;
  if (selT_ == 1 && selS_ == 1) {
    selMod_ = (uint8_t)mv; qMod_ = 0; killLive();
    if (last_.valid) { int n = pool(rv(last_), mv, last_.x % 2 == 0, p); fl(p, n); }
  } else if (ql_.valid) { ql_.m = (uint8_t)mv; qMod_ = 0; }
  else qMod_ = (uint8_t)mv;
}

void ChordArp::playPage(int col, int row, bool down) {
  if (col <= 7) { typePad((col - 6) * 4 + row + 1, down); return; }
  int v = col - 7;  // 1..4 (c34 x5..8)
  switch (row) {
    case 0: if (down && v <= 3) qT_ = (uint8_t)v; break;  // chord / arp / arp+oct; "off" is the Setup page's Mute
    case 1: case 2: {
      uint8_t& cur = row == 1 ? qS_ : qD_;
      if (down) setPair(cur, row, v);
      else if (xh_[row - 1] == v) xh_[row - 1] = 0;
      break;
    }
    default: {
      int i = col - 8;  // 0 INV-, 1 INV+, 2 OCT-, 3 OCT+
      held_[i] = down;
      if (!down) break;
      if (i < 2) {
        if (held_[0] && held_[1]) inv_ = 0;
        else if (i == 1) inv_ = (int8_t)(inv_ + 1 > 6 ? 6 : inv_ + 1);
        else inv_ = (int8_t)(inv_ - 1 < -6 ? -6 : inv_ - 1);
      } else {
        if (held_[2] && held_[3]) koct_ = 0;
        else if (i == 3) koct_ = (int8_t)(koct_ + 1 > 1 ? 1 : koct_ + 1);
        else koct_ = (int8_t)(koct_ - 1 < -1 ? -1 : koct_ - 1);
      }
    }
  }
}

// GROOVE: cols 6-9 = the 4x4 groove block (c34 x5-8 y5-8; with parametric groove on: three tier rows + archetype row),
// col 10 = parametric on/off, gate length, swing -, swing +.
void ChordArp::groovePage(int col, int row, bool down) {
  if (!down) return;
  if (col <= 9) {
    int pc = col - 6;
    if (pon_) {
      if (row <= 2) PW_[row] = (uint8_t)pc;
      else {
        int a = pc + 1;
        if (a == parch_) pseed_ = (pseed_ * 7 + 29) % 251;  // re-tap the active archetype: reroll
        else { parch_ = (uint8_t)a; for (int j = 0; j < 3; ++j) PW_[j] = kPar[a - 1][j]; }
      }
      pgen();
    } else qG_ = (uint8_t)(row * 4 + pc + 1);
    return;
  }
  switch (row) {
    case 0: pon_ = !pon_; break;
    case 1: glen_ = (uint8_t)(glen_ % 3 + 1); break;
    case 2: if (sw_ > 0) sw_--; break;
    default: if (sw_ < 3) sw_++; break;
  }
}

// FEEL: cols 6-9 rows 0-1 = velocity patterns 1..8 (a second finger blends two), row 2 = gate lanes 1..4 (tap again = off, a second
// finger = both), row 3 = arp octave range 1..4 (c34: hold octave centre + tap); col 10 = chord size 5 / 4 / 3 / 2 (c34 x5-8 y4).
void ChordArp::feelPage(int col, int row, bool down) {
  if (col == 10) { if (down) qSize_ = kSm[row]; return; }
  int pc = col - 6;
  if (row <= 1) {
    int k = row * 4 + pc + 1;
    if (down) {
      int g = k;
      if (vh_ && vh_ != k) g = (vh_ < k ? vh_ : k) + 8 * (vh_ < k ? k : vh_);
      else vh_ = (uint8_t)k;
      selVg_ = (uint8_t)g;
    } else if (vh_ == k) vh_ = 0;
  } else if (row == 2) {
    int g = pc + 1;
    if (down) {
      int f = lh_;
      if (f && f != g) lane_ = (uint8_t)((f < g ? f : g) + 8 * (f < g ? g : f));
      else { lane_ = (lane_ == g) ? 0 : (uint8_t)g; lh_ = (uint8_t)g; }
    } else if (lh_ == g) lh_ = 0;
  } else if (down) orng_ = (uint8_t)(pc + 1);
}

// SETUP: row 0 = key mode Key / Locked / Grid (hold Locked and tap a root = set the lock key, c34 x14), row 1 = transpose - / + / 0
// (scale degrees), row 2 = mute / off (type 4, from the next bar).
void ChordArp::setupPage(int col, int row, bool down) {
  if (row == 0 && col <= 8) {
    int mode = col - 5;  // 1 key, 2 locked, 3 grid
    if (down) { lockTransition(mode, true); padLock_ = (mode == 2); }
    else if (mode == 2 && padLock_) { centerHeld_ = false; padLock_ = false; }
    return;
  }
  if (!down) return;
  if (row == 1 && col <= 8) {
    if (col == 6) arpTpose_ = (int8_t)(arpTpose_ > -24 ? arpTpose_ - 1 : -24);
    else if (col == 7) arpTpose_ = (int8_t)(arpTpose_ < 24 ? arpTpose_ + 1 : 24);
    else arpTpose_ = 0;
  } else if (row == 2 && col == 6) {
    amute_ = !amute_;
    if (amute_) killLive();
  } else if (row == 2 && col == 7) qT_ = 4;  // c34 type 4 (off): the live layer is silent from the next bar
}

// c34: a second finger on the speed / direction row selects the pair (index 5..10) of the two held pads.
void ChordArp::setPair(uint8_t& cur, int row, int v) {
  int f = xh_[row - 1];
  if (f && f != v) {
    if (f > v) { int t = f; f = v; v = t; }
    cur = (uint8_t)(4 + (f - 1) * 3 - ((f - 1) * (f - 2)) / 2 + v - f);
  } else {
    xh_[row - 1] = (uint8_t)v;
    cur = (uint8_t)v;
  }
}

// ---- params -----------------------------------------------------------------------------------
int ChordArp::getParam(int id) const {
  switch (id) {
    case P_PMODE: return pmode_;
    case P_LOCK_ROOT: return lockR_;
    case P_LOCK_MINOR: return lockMn_;
    case P_SWING: return sw_;
    case P_SEL_VG: return selVg_;
    case P_LANE: return lane_;
    case P_ORANGE: return orng_;
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
    case P_PAGE: return page_;
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
    case P_SWING: sw_ = (uint8_t)clampv(v, 0, 3); break;
    case P_SEL_VG: selVg_ = (uint8_t)clampv(v, 1, 72); break;
    case P_LANE: lane_ = (uint8_t)clampv(v, 0, 36); break;
    case P_ORANGE: orng_ = (uint8_t)clampv(v, 1, 4); break;
    case P_GATE: glen_ = (uint8_t)(clampv(v, 0, 2) + 1); break;
    case P_SEL_TYPE: selT_ = (uint8_t)clampv(v, 1, 4); lAptr_ = 1; break;
    case P_SEL_SPEED: selS_ = (uint8_t)clampv(v, 1, 10); lAptr_ = 1; break;
    case P_SEL_DIR: selD_ = (uint8_t)clampv(v, 1, 10); lAptr_ = 1; break;
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
    case P_PAGE: setPage(clampv(v, 0, 3)); break;
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
  b[22] = (uint8_t)arpTpose_; b[23] = amute_; b[24] = page_;
  b[25] = selVg_; b[26] = lane_; b[27] = orng_;
  return kSerialSize;
}

bool ChordArp::deserialize(const uint8_t* b, size_t len) {
  if (len < (size_t)kSerialSize || b[0] != kSerialVersion) return false;
  int pm = b[1], sw = b[5], gl = b[6], t = b[7], s = b[8], d = b[9], sz = b[10], g = b[11], mod = b[12];
  int pa = b[16], pseed = b[20] | (b[21] << 8), pn = b[24];
  if (pm < 1 || pm > 3 || sw > 3 || gl < 1 || gl > 3 || t < 1 || t > 4 || s < 1 || s > 10 || d < 1 || d > 10 ||
      b[25] < 1 || b[25] > 72 || b[26] > 36 || b[27] < 1 || b[27] > 4 ||
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
  arpTpose_ = (int8_t)b[22]; amute_ = b[23] != 0; page_ = (uint8_t)pn; editPage_ = pn ? (uint8_t)pn : (uint8_t)PAGE_GROOVE;
  selVg_ = b[25]; lane_ = b[26]; orng_ = b[27]; xh_[0] = xh_[1] = 0; vh_ = lh_ = 0; modHeld_ = false; padLock_ = false;
  for (int i = 0; i < 4; ++i) held_[i] = false;
  for (int i = 0; i < kPads; ++i) downPage_[i] = 0xFF;
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
  // right half: the visible page (cols 6..11)
  auto put = [&](int col, int row, int led, uint8_t role) { out[row * 12 + col] = (uint8_t)led; ro[row * 12 + col] = role; };
  // tab column (edit pages) / page pad (Play)
  if (page_ == PAGE_PLAY) put(11, 0, 6, ROLE_CONTROL);
  else for (int r = 0; r < 4; ++r) put(11, r, r == page_ ? 15 : 5, ROLE_CONTROL);
  if (page_ == PAGE_PLAY) {
    int eff = ql_.valid ? ql_.m : qMod_;
    int selp = selMod_ <= 8 ? selMod_ : selMod_ - 7;
    int effp = eff ? (eff <= 8 ? eff : eff - 7) : 0;
    for (int c = 0; c < 2; ++c) for (int r = 0; r < 4; ++r) {
      int mi = c * 4 + r + 1, led = 3;
      if (selMod_ > 8 && mi == 1 && selp != 1) led = pul;  // an extended type: pad 1 (the modifier) pulses
      if (effp && mi == effp && mi != selp) led = pul;
      if (mi == selp) led = 15;
      put(6 + c, r, led, ROLE_PANEL);
    }
    for (int c = 8; c <= 11; ++c) {
      int v = c - 7;
      if (c <= 10) put(c, 0, ((qT_ ? qT_ : selT_) == v) ? pul : 3, ROLE_PANEL);
      for (int r = 1; r <= 2; ++r) {
        int cur = r == 1 ? (qS_ ? qS_ : selS_) : (qD_ ? qD_ : selD_);
        put(c, r, (cur == v || (cur >= 5 && (kP1[cur - 5] == v || kP2[cur - 5] == v))) ? pul : 3, ROLE_PANEL);
      }
    }
    put(8, 3, inv_ < 0 ? (6 - inv_ * 3 > 15 ? 15 : 6 - inv_ * 3) : 3, ROLE_CONTROL);
    put(9, 3, inv_ > 0 ? (6 + inv_ * 3 > 15 ? 15 : 6 + inv_ * 3) : 3, ROLE_CONTROL);
    put(10, 3, koct_ == -1 ? 15 : 3, ROLE_CONTROL);
    put(11, 3, koct_ == 1 ? 15 : 3, ROLE_CONTROL);
  } else if (page_ == PAGE_GROOVE) {
    int ag = qG_ ? qG_ : selG_;
    for (int r = 0; r < 4; ++r) for (int pc = 0; pc < 4; ++pc) {
      int idx = r * 4 + pc + 1, led;
      if (pon_) {
        if (r < 3) led = (pc == PW_[r]) ? ((((pmask_ >> ((lGptr_ - 1) % 16)) & 1) ? 15 : 10)) : 3;
        else led = ((pc + 1) == parch_) ? 12 : 3;
      } else led = (idx == ag) ? (gbit(ag, lGptr_) ? 15 : 7) : 6;
      put(6 + pc, r, led, ROLE_PANEL);
    }
    put(10, 0, pon_ ? 15 : 3, ROLE_CONTROL);
    put(10, 1, glen_ == 1 ? 6 : (glen_ == 2 ? 3 : 15), ROLE_CONTROL);  // gate: normal / short / legato
    put(10, 2, sw_ > 0 ? 6 + sw_ * 3 : 3, ROLE_CONTROL);
    put(10, 3, sw_ < 3 ? 6 + sw_ * 3 : 15, ROLE_CONTROL);
  } else if (page_ == PAGE_FEEL) {
    int va = (selVg_ - 1) % 8 + 1, vq = (selVg_ - 1) / 8;
    for (int r = 0; r < 2; ++r) for (int pc = 0; pc < 4; ++pc) {
      int k = r * 4 + pc + 1;
      put(6 + pc, r, (k == va || (vq > 0 && k == vq)) ? 15 : 3, ROLE_PANEL);
    }
    for (int pc = 0; pc < 4; ++pc) {
      put(6 + pc, 2, (pc + 1 == lane_ % 8 || pc + 1 == lane_ / 8) ? 15 : 3, ROLE_PANEL);
      put(6 + pc, 3, orng_ == pc + 1 ? 15 : 3, ROLE_PANEL);
    }
    for (int r = 0; r < 4; ++r) put(10, r, kSm[r] == (qSize_ ? qSize_ : selSize_) ? pul : 3, ROLE_PANEL);
  } else {
    for (int m = 1; m <= 3; ++m) put(5 + m, 0, pmode_ == m ? 15 : (m == 2 && centerHeld_ ? 9 : 4), ROLE_CONTROL);
    put(6, 1, arpTpose_ < 0 ? 9 : 4, ROLE_CONTROL);
    put(7, 1, arpTpose_ > 0 ? 9 : 4, ROLE_CONTROL);
    put(8, 1, arpTpose_ == 0 ? 4 : 8, ROLE_CONTROL);
    put(6, 2, amute_ ? 15 : 4, ROLE_CONTROL);
  }
}

}  // namespace seqgen
