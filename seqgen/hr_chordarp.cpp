// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Ported 1:1 from HYPER_RASP (same owner) scripts/chord_arp.lua. See hr_chordarp.h.
#include "hr_chordarp.h"

#include <stdio.h>

namespace seqgen {

namespace {
const uint8_t kRoots[4][6] = {{64, 69, 62, 67, 60, 65}, {61, 66, 71, 64, 69, 62}, {71, 66, 63, 68, 63, 70}, {68, 63, 70, 65, 60, 67}};
// TYPE_ORDER 1..10: maj min dom7 sus4 | maj7 min7 dim aug 6 add9
const int8_t kIv[10][4] = {{0, 4, 7, -1}, {0, 3, 7, -1}, {0, 4, 7, 10}, {0, 5, 7, -1}, {0, 4, 7, 11}, {0, 3, 7, 10}, {0, 3, 6, -1}, {0, 4, 8, -1}, {0, 4, 7, 9}, {0, 4, 7, 14}};
const char* const kTypeName[10] = {"maj", "min", "dom7", "sus4", "maj7", "min7", "dim", "aug", "6", "add9"};
const uint16_t kGatePat[16] = {65535, 21845, 52428, 18761, 61166, 28013, 6745, 4369, 22875, 26214, 48059, 27437, 19609, 27997, 61713, 63761};  // GATE_PATTERNS
const uint8_t kSpeeds[4] = {12, 8, 6, 4};
const uint8_t kArpSq[8] = {1, 3, 2, 4, 3, 5, 4, 6};
const uint8_t kSizeOpt[4] = {5, 4, 3, 2};
}  // namespace

HrChordArp::HrChordArp()
    : HrScript(HR_CHORD_ARP), type_(1), size_(5), mode_(1), speed_(1), dir_(1), vg_(1), gate_(1), qstart_(1), keylock_(1), lockKey_(0), lockMinor_(0),
      latched_(1), baseVel_(100), octave_(0), inv_(0), transp_(0), view_(1), page_(1), clearHeld_(0), playing_(0), hostRunning_(0), phase_(1), arpActive_(0),
      arpStep_(1), arpPulse_(0), nPool_(0), nActive_(0), nPlayback_(0), arpNote_(-1), hasRoot_(0), rootX_(0), rootY_(0), hasPendingRoot_(0), pendX_(0),
      pendY_(0), hasPendingType_(0), pendType_(0), pendK1_(0), pendK2_(0), selK1_(1), selK2_(0), modHeld_(0), modFired_(0), hasLastRoot_(0), lastRoot_(0),
      playIndex_(1), playPulse_(0), pulseIndex_(0), published_(0), swHeld_(0), swFired_(0) {
  for (int i = 0; i < 6; ++i) { pool_[i] = 0; active_[i] = 0; playback_[i] = 0; }
  for (int i = 0; i < 8; ++i) { Step z = {0, 0, 0, 0}; steps_[i] = z; }
  for (int i = 0; i < 32; ++i) led_[i] = role_[i] = 0;
  render();
}

// ---------------------------------------------------------------- key lock
int HrChordArp::noteToDegree(int note) const {
  cam::Scale s;
  int rel = note - lockScale(s);
  int oct = floordiv(rel, 12), pc = floormod(rel, 12);
  int best = 99, bestDeg = 0;
  for (int di = 0; di < s.n; ++di) {
    int dd = iabs(s.iv[di] - pc);
    if (dd < best) { best = dd; bestDeg = di; }
  }
  if (iabs(12 - pc) < best) bestDeg = s.n;
  return oct * s.n + bestDeg;
}

int HrChordArp::degreeToNote(int d) const {
  cam::Scale s;
  const int base = lockScale(s);
  return cam::degreeNote(s, base, d);
}

int HrChordArp::buildChord(int root, int type, int* out) const {
  int notes[4], n = 0;
  for (int i = 0; i < 4 && kIv[type - 1][i] >= 0; ++i) notes[n++] = root + kIv[type - 1][i] + transp_;
  if (keylock_ != 2) {
    for (int i = 0; i < n; ++i) out[i] = notes[i];
    return n;
  }
  int deg[4];
  for (int i = 0; i < n; ++i) deg[i] = noteToDegree(notes[i]);
  for (int i = 1; i < n; ++i)  // table.sort (insertion sort, ints)
    for (int j = i; j > 0 && deg[j] < deg[j - 1]; --j) { int t = deg[j]; deg[j] = deg[j - 1]; deg[j - 1] = t; }
  for (int i = 1; i < n; ++i)
    if (deg[i] <= deg[i - 1]) deg[i] = deg[i - 1] + 1;
  int m = 0;
  for (int i = 0; i < n; ++i) {
    int nt = degreeToNote(deg[i]);
    if (nt >= 0 && nt <= 127) out[m++] = nt;
  }
  return m;
}

int HrChordArp::applyModifiers(int* notes, int n) const {
  if (n > size_) n = size_;
  if (octave_ != 0)
    for (int i = 0; i < n; ++i) notes[i] += 12 * octave_;
  if (inv_ != 0 && n > 0) {
    for (int k = 0; k < iabs(inv_); ++k) {
      if (inv_ > 0) {
        int lowest = 0;
        for (int j = 1; j < n; ++j) if (notes[j] < notes[lowest]) lowest = j;
        notes[lowest] += 12;
      } else {
        int highest = 0;
        for (int j = 1; j < n; ++j) if (notes[j] > notes[highest]) highest = j;
        notes[highest] -= 12;
      }
    }
  }
  return n;
}

int HrChordArp::arpNoteAt(int direction, int step) const {
  int n = nPool_;
  if (n == 0) return -1;
  int index;
  if (direction == 2) index = n - floormod(step - 1, n);
  else if (direction == 3) {
    int cycle = n * 2, phase = floormod(step - 1, cycle);
    index = phase < n ? phase + 1 : cycle - phase;
  } else if (direction == 4) index = floormod(kArpSq[floormod(step - 1, 8)] - 1, n) + 1;
  else index = floormod(step - 1, n) + 1;
  return pool_[index - 1];
}

bool HrChordArp::gateOpen() const {
  int bit = floormod(phase_ - 1, 16);
  return (kGatePat[gate_ - 1] >> bit) & 1;
}

// ---------------------------------------------------------------- sounding
void HrChordArp::stopActiveChord() {
  for (int i = 0; i < nActive_; ++i) noteOff(active_[i]);
  nActive_ = 0;
  hasRoot_ = 0;
}

void HrChordArp::stopArp() {
  if (arpNote_ >= 0) { noteOff(arpNote_); arpNote_ = -1; }
  arpActive_ = 0;
  nPool_ = 0;
  hasRoot_ = 0;
}

void HrChordArp::stopActiveRoot() {
  if (arpActive_) stopArp();
  else stopActiveChord();
}

void HrChordArp::soundRoot(int note) {
  if (bus_) bus_->root = (int16_t)note;
  int notes[6];
  int n = buildChord(note, type_, notes);
  n = applyModifiers(notes, n);
  if (mode_ == 2) {
    nPool_ = (uint8_t)n;
    for (int i = 0; i < n; ++i) pool_[i] = (int16_t)notes[i];
    arpStep_ = 1;
    arpPulse_ = 0;
    arpActive_ = 1;
    arpNote_ = arpNoteAt(dir_, arpStep_);
    if (arpNote_ >= 0) noteOn(arpNote_, velocity());
  } else {
    nActive_ = (uint8_t)n;
    int vel = velocity();
    for (int i = 0; i < n; ++i) {
      active_[i] = (int16_t)notes[i];  // outside 0..127 the note is not sent (SeqBase::emit), its off neither
      noteOn(notes[i], vel);
    }
  }
}

void HrChordArp::retriggerLatched() {
  if (!latched_ || !hasRoot_) return;
  int x = rootX_, y = rootY_;
  int root = kRoots[y - 1][x - 1];
  stopActiveRoot();
  hasRoot_ = 1; rootX_ = (uint8_t)x; rootY_ = (uint8_t)y;
  soundRoot(root);
}

void HrChordArp::selectType(int type, int k1, int k2) {
  if (hasPendingRoot_) {
    type_ = (uint8_t)type; selK1_ = (uint8_t)k1; selK2_ = (uint8_t)k2;
    hasPendingType_ = 0;
    return;
  }
  if (qstart_ != 1 && latched_ && hasRoot_) {
    hasPendingType_ = 1; pendType_ = (uint8_t)type; pendK1_ = (uint8_t)k1; pendK2_ = (uint8_t)k2;
    return;
  }
  type_ = (uint8_t)type; selK1_ = (uint8_t)k1; selK2_ = (uint8_t)k2;
  retriggerLatched();
}

void HrChordArp::applyPendingType() {
  type_ = pendType_; selK1_ = pendK1_; selK2_ = pendK2_;
  hasPendingType_ = 0;
  retriggerLatched();
}

void HrChordArp::applyPendingRoot() {
  int x = pendX_, y = pendY_;
  hasPendingRoot_ = 0;
  if (hasRoot_) stopActiveRoot();
  int root = kRoots[y - 1][x - 1];
  hasRoot_ = 1; rootX_ = (uint8_t)x; rootY_ = (uint8_t)y;
  hasLastRoot_ = 1; lastRoot_ = (uint8_t)root;
  soundRoot(root);
}

void HrChordArp::handleModKey(int y, bool pressed) {
  static const uint8_t kComboType[5][5] = {{0}, {0, 0, 5, 6, 7}, {0, 0, 0, 8, 9}, {0, 0, 0, 0, 10}, {0}};  // [lo][hi]
  int bit = 1 << (y - 1);
  if (pressed) {
    modHeld_ |= (uint8_t)bit;
    int count = 0, lo = 0, hi = 0;
    for (int k = 1; k <= 4; ++k)
      if (modHeld_ & (1 << (k - 1))) { ++count; if (!lo) lo = k; hi = k; }
    if (count == 2 && !modFired_) {
      modFired_ = 1;
      int t = kComboType[lo][hi];
      if (t) selectType(t, lo, hi);
    }
  } else {
    int count = 0;
    for (int k = 1; k <= 4; ++k) if (modHeld_ & (1 << (k - 1))) ++count;
    bool lone = count == 1 && !modFired_;
    modHeld_ &= (uint8_t)~bit;
    if (modHeld_ == 0) {
      if (lone) selectType(y, y, 0);  // SINGLE_TYPES[y]: types 1..4
      modFired_ = 0;
    }
  }
}

// ---------------------------------------------------------------- views
void HrChordArp::view1(int x, int y, int z) {
  if (x == 7 && y >= 1 && y <= 4) {
    handleModKey(y, z == 1);
    render();
    return;
  }
  if (x >= 1 && x <= 6 && y >= 1 && y <= 4) {
    if (z == 1) {
      bool wasSounding = hasRoot_ && rootX_ == x && rootY_ == y;
      bool queueIt = qstart_ != 1 && !(latched_ && wasSounding);
      if (hasRoot_ && !queueIt) stopActiveRoot();
      if (latched_ && wasSounding) {
        hasPendingRoot_ = 0;
        render();
        return;
      }
      if (queueIt) {
        if (hasPendingType_) {
          type_ = pendType_; selK1_ = pendK1_; selK2_ = pendK2_;
          hasPendingType_ = 0;
        }
        hasPendingRoot_ = 1; pendX_ = (uint8_t)x; pendY_ = (uint8_t)y;
        render();
        return;
      }
      int root = kRoots[y - 1][x - 1];
      hasRoot_ = 1; rootX_ = (uint8_t)x; rootY_ = (uint8_t)y;
      hasLastRoot_ = 1; lastRoot_ = (uint8_t)root;
      soundRoot(root);
    } else if (!latched_) {
      if (hasPendingRoot_ && pendX_ == x && pendY_ == y) hasPendingRoot_ = 0;
      if (hasRoot_ && rootX_ == x && rootY_ == y) stopActiveRoot();
    }
    render();
  }
}

void HrChordArp::view2(int x, int y, int z) {
  if (z != 1) return;
  if (y == 1 && x >= 1 && x <= 4) { dir_ = (uint8_t)x; render(); return; }
  if (y == 2 && x >= 1 && x <= 4) { speed_ = (uint8_t)x; render(); return; }
  if (y == 3 && (x == 1 || x == 2)) { mode_ = (uint8_t)x; render(); return; }
  if (y == 3 && (x == 3 || x == 4)) {
    keylock_ = x == 3 ? 1 : 2;
    publishKeyLock();
    retriggerLatched();
    render();
    return;
  }
  if (y == 4 && x >= 1 && x <= 4) { size_ = kSizeOpt[x - 1]; render(); return; }
  if (x == 5 && y >= 1 && y <= 3) { octave_ = (int8_t)(y == 1 ? 1 : y == 2 ? 0 : -1); render(); return; }
  if (x == 6 && y >= 1 && y <= 3) {
    if (y == 1) inv_ = (int8_t)(inv_ + 1 > 6 ? 6 : inv_ + 1);
    else if (y == 2) inv_ = 0;
    else inv_ = (int8_t)(inv_ - 1 < -6 ? -6 : inv_ - 1);
    render();
    return;
  }
}

void HrChordArp::view3(int x, int y, int z) {
  if (z != 1) return;
  if (y < 1 || y > 4) return;
  if (x <= 4) { gate_ = (uint8_t)((y - 1) * 4 + x); render(); return; }
  int idx = (y - 1) * 3 + (x - 5 + 1);
  if (idx >= 1 && idx <= 12) { vg_ = (uint8_t)idx; render(); }
}

void HrChordArp::view4(int x, int y, int z) {
  if (x == 6 && y == 1) { clearHeld_ = z == 1; render(); return; }
  if (z != 1) return;
  if (x >= 1 && x <= 4 && y >= 1 && y <= 4) {
    int idx = (page_ - 1) * 4 + x;
    if (clearHeld_) {
      Step zr = {0, 0, 0, 0};
      steps_[idx - 1] = zr;
    } else if (hasLastRoot_) {
      Step s = {1, type_, (uint8_t)y, lastRoot_};
      steps_[idx - 1] = s;
    }
    render();
  }
}

void HrChordArp::grid(int x, int y, int z) {
  if (x == 8) {  // the function column: ViewSwitcher (single tap = view, 3+4 = page, 1+2 = tracker play / stop, 1+3 = host mute: not here)
    if (y < 1 || y > 4) return;
    int bit = 1 << (y - 1);
    if (z == 1) {
      swHeld_ |= (uint8_t)bit;
      int count = 0, lo = 0, hi = 0;
      for (int k = 1; k <= 4; ++k) if (swHeld_ & (1 << (k - 1))) { ++count; if (!lo) lo = k; hi = k; }
      if (count == 2 && !swFired_) {
        swFired_ = 1;
        if (lo == 3 && hi == 4) setPage(page_ == 1 ? 2 : 1);
        else if (lo == 1 && hi == 2) setPlayback(!hostRunning_);
      }
    } else {
      int count = 0;
      for (int k = 1; k <= 4; ++k) if (swHeld_ & (1 << (k - 1))) ++count;
      bool lone = count == 1 && !swFired_;
      swHeld_ &= (uint8_t)~bit;
      if (swHeld_ == 0) {
        if (lone) setView(y);
        swFired_ = 0;
      }
    }
    return;
  }
  if (view_ == 2) view2(x, y, z);
  else if (view_ == 3) view3(x, y, z);
  else if (view_ == 4) view4(x, y, z);
  else view1(x, y, z);
}

void HrChordArp::setView(int n) {
  if (n >= 1 && n <= 4) {
    clearHeld_ = 0;
    view_ = (uint8_t)n;
    render();
  }
}

void HrChordArp::setPage(int n) {
  if (n == 1 || n == 2) { page_ = (uint8_t)n; render(); }
}

void HrChordArp::setPlayback(bool running) {
  if (running == (hostRunning_ != 0)) return;
  hostRunning_ = running ? 1 : 0;
  if (running) startPlayback();
  else stopPlayback();
}

// ---------------------------------------------------------------- tracker playback
void HrChordArp::stopPlaybackNotes() {
  for (int i = 0; i < nPlayback_; ++i) noteOff(playback_[i]);
  nPlayback_ = 0;
}

void HrChordArp::playCurrentStep() {
  const Step& s = steps_[playIndex_ - 1];
  if (s.used && gateOpen()) {
    int notes[6];
    int n = buildChord(s.note, s.type, notes);
    n = applyModifiers(notes, n);
    nPlayback_ = (uint8_t)n;
    int vel = velocity();
    for (int i = 0; i < n; ++i) {
      playback_[i] = (int16_t)notes[i];
      noteOn(notes[i], vel);
    }
  } else {
    nPlayback_ = 0;
  }
}

void HrChordArp::startPlayback() {
  if (playing_) return;
  playing_ = 1;
  playIndex_ = 1;
  playPulse_ = 0;
  playCurrentStep();
  render();
}

void HrChordArp::stopPlayback() {
  if (!playing_) return;
  playing_ = 0;
  stopPlaybackNotes();
  render();
}

void HrChordArp::silence() {
  // a type change / panic: everything this script sounds ends (the script itself has no such entry point; the HYPER_RASP host re-loads it)
  stopActiveRoot();
  stopActiveChord();
  stopPlaybackNotes();
  playing_ = 0;
  hostRunning_ = 0;
  hasPendingRoot_ = 0;
  hasPendingType_ = 0;
}

void HrChordArp::publishKeyLock() {
  uint16_t sig = keylock_ == 2 ? (uint16_t)(1 + lockKey_ * 2 + (lockMinor_ ? 1 : 0) + 25 * camA_ + 150 * camB_) : 0;
  if (keylock_ != 2) sig = 0;
  if (sig == published_) return;
  published_ = sig;
  if (!bus_) return;
  if (keylock_ != 2) { bus_->clearScale(); return; }
  bus_->setKey(lockKey_, lockMinor_ != 0, camA_, camB_);
}

bool HrChordArp::camKey(cam::Scale& s) const {
  if (keylock_ == 2) { cam::resolve(s, lockKey_, lockMinor_ != 0, camA_, camB_); return true; }
  if (!hasRoot_) return false;
  cam::resolve(s, kRoots[rootY_ - 1][rootX_ - 1] % 12, (rootY_ & 1) == 0, 0, 0);  // free key: the root pad's position (even rows = minor roots)
  return true;
}

// ---------------------------------------------------------------- the clock
void HrChordArp::advanceArp() {
  if (!arpActive_) return;
  if (++arpPulse_ < kSpeeds[speed_ - 1]) return;
  arpPulse_ = 0;
  ++arpStep_;
  if (arpNote_ >= 0) noteOff(arpNote_);
  arpNote_ = arpNoteAt(dir_, arpStep_);
  if (arpNote_ >= 0) noteOn(arpNote_, velocity());
}

void HrChordArp::pulse() {
  if ((pulseIndex_ % 24) == 0 && (hasPendingRoot_ || hasPendingType_)) {
    if (hasPendingRoot_) {
      hasPendingType_ = 0;
      applyPendingRoot();
    } else {
      applyPendingType();
    }
    if (view_ == 1) render();
  }
  ++pulseIndex_;
  phase_ = (uint8_t)(phase_ % 16 + 1);
  advanceArp();
  if (!playing_) return;
  const Step& s = steps_[playIndex_ - 1];
  int dwell = (s.used ? s.dur : 1) * 6;
  if (++playPulse_ >= dwell) {
    playPulse_ = 0;
    stopPlaybackNotes();
    playIndex_ = (uint8_t)(playIndex_ % 8 + 1);
    playCurrentStep();
    if (view_ == 4) render();
  }
}

// ---------------------------------------------------------------- set_param
void HrChordArp::setParam(int id, int v) { doParam(id, v, false); }

void HrChordArp::doParam(int id, int n, bool fromPad) {
  bool reshape = false;
  switch (id) {
    case P_TYPE:
      if (n >= 1 && n <= 10) selectType(n, selK1_, selK2_);
      break;
    case P_SIZE: size_ = (uint8_t)n; reshape = true; break;
    case P_OCTAVE: octave_ = (int8_t)n; reshape = true; break;
    case P_INV: inv_ = (int8_t)(n < -6 ? -6 : n > 6 ? 6 : n); reshape = true; break;
    case P_MODE: mode_ = (uint8_t)n; break;
    case P_SPEED: if (n >= 1 && n <= 4) speed_ = (uint8_t)n; break;
    case P_DIR: dir_ = (uint8_t)n; break;
    case P_GROOVE: if (n >= 1 && n <= 12) vg_ = (uint8_t)n; break;
    case P_VEL: baseVel_ = (uint8_t)n; break;
    case P_GATE: if (n >= 1 && n <= 16) gate_ = (uint8_t)n; break;
    case P_TRANSP: transp_ = (int8_t)n; reshape = true; break;
    case P_QSTART:
      qstart_ = (uint8_t)n;
      if (qstart_ == 1) {
        hasPendingRoot_ = 0;
        if (hasPendingType_) applyPendingType();
      }
      break;
    case P_KEYLOCK: keylock_ = n == 2 ? 2 : 1; reshape = true; publishKeyLock(); break;
    case P_KEY:
      if (n >= 0 && n <= 11) { lockKey_ = (uint8_t)n; reshape = true; publishKeyLock(); }
      break;
    case P_SCALE: lockMinor_ = n == 2; reshape = true; publishKeyLock(); break;
    case P_LATCH:
      latched_ = n == 2;
      if (!latched_) stopActiveRoot();
      break;
    default: return;
  }
  if (reshape) retriggerLatched();
  if (view_ != 1 || fromPad) render();
}

int HrChordArp::getParam(int id) const {
  switch (id) {
    case P_TYPE: return type_;
    case P_SIZE: return size_;
    case P_OCTAVE: return octave_;
    case P_INV: return inv_;
    case P_MODE: return mode_;
    case P_SPEED: return speed_;
    case P_DIR: return dir_;
    case P_GROOVE: return vg_;
    case P_VEL: return baseVel_;
    case P_GATE: return gate_;
    case P_TRANSP: return transp_;
    case P_QSTART: return qstart_;
    case P_KEYLOCK: return keylock_;
    case P_KEY: return lockKey_;
    case P_SCALE: return lockMinor_ ? 2 : 1;
    case P_LATCH: return latched_ ? 2 : 1;
    case P_VIEW: return view_;
    case P_PAGE: return page_;
    case P_PLAYING: return playing_;
    case P_ROOT_X: return hasRoot_ ? rootX_ : 0;
    case P_ROOT_Y: return hasRoot_ ? rootY_ : 0;
    default: return 0;
  }
}

// extras: the encoder / fader values that no view puts on a pad
void HrChordArp::ext(int c, int r, bool down) {
  if (!down) return;
  switch (r * 4 + c) {
    case 0: doParam(P_VEL, baseVel_ - 8 < 1 ? 1 : baseVel_ - 8, true); break;
    case 1: doParam(P_VEL, baseVel_ + 8 > 127 ? 127 : baseVel_ + 8, true); break;
    case 2: doParam(P_TRANSP, transp_ - 1 < -12 ? -12 : transp_ - 1, true); break;
    case 3: doParam(P_TRANSP, transp_ + 1 > 12 ? 12 : transp_ + 1, true); break;
    case 4: doParam(P_KEY, (lockKey_ + 11) % 12, true); break;
    case 5: doParam(P_KEY, (lockKey_ + 1) % 12, true); break;
    case 6: doParam(P_SCALE, lockMinor_ ? 1 : 2, true); break;
    case 7: doParam(P_LATCH, latched_ ? 1 : 2, true); break;
    case 8: doParam(P_QSTART, qstart_ == 1 ? 2 : 1, true); break;
    case 9: setPlayback(!hostRunning_); break;
    case 10: setPage(page_ == 1 ? 2 : 1); break;
    case 12: case 13: case 14: case 15: setView(c + 1); break;
    default: break;
  }
}

// ---------------------------------------------------------------- serialization (settings + the programmed tracker steps)
size_t HrChordArp::serialize(uint8_t* b, size_t cap) const {
  if (cap < (size_t)kSerialSize) return 0;
  for (int i = 0; i < kSerialSize; ++i) b[i] = 0;
  b[0] = kSerialVersion; b[1] = type_; b[2] = size_; b[3] = (uint8_t)(octave_ + 1); b[4] = (uint8_t)(inv_ + 6); b[5] = mode_; b[6] = speed_; b[7] = dir_;
  b[8] = vg_; b[9] = baseVel_; b[10] = gate_; b[11] = (uint8_t)(transp_ + 12); b[12] = qstart_; b[13] = keylock_; b[14] = lockKey_; b[15] = lockMinor_;
  b[16] = latched_;
  for (int i = 0; i < 8; ++i) {  // [used|dur<<1|type<<4] [note]
    b[17 + i * 2] = (uint8_t)(steps_[i].used | steps_[i].dur << 1 | steps_[i].type << 4);
    b[18 + i * 2] = steps_[i].note;
  }
  return kSerialSize;
}

bool HrChordArp::deserialize(const uint8_t* b, size_t len) {
  if (len < (size_t)kSerialSize || b[0] != kSerialVersion) return false;
  if (b[1] < 1 || b[1] > 10 || b[2] < 2 || b[2] > 5 || b[3] > 2 || b[4] > 12 || b[5] < 1 || b[5] > 2 || b[6] < 1 || b[6] > 4 || b[7] < 1 || b[7] > 4 ||
      b[8] < 1 || b[8] > 12 || b[9] < 1 || b[9] > 127 || b[10] < 1 || b[10] > 16 || b[11] > 24 || b[12] < 1 || b[12] > 2 || b[13] < 1 || b[13] > 2 || b[14] > 11 ||
      b[15] > 1 || b[16] > 1)
    return false;
  Step st[8];
  for (int i = 0; i < 8; ++i) {
    uint8_t p = b[17 + i * 2];
    st[i].used = p & 1; st[i].dur = (p >> 1) & 7; st[i].type = p >> 4; st[i].note = b[18 + i * 2];
    if (!st[i].used) { Step z = {0, 0, 0, 0}; st[i] = z; continue; }
    if (st[i].dur < 1 || st[i].dur > 4 || st[i].type < 1 || st[i].type > 10 || st[i].note > 127) return false;
  }
  silence();
  type_ = b[1]; size_ = b[2]; octave_ = (int8_t)(b[3] - 1); inv_ = (int8_t)(b[4] - 6); mode_ = b[5]; speed_ = b[6]; dir_ = b[7]; vg_ = b[8]; baseVel_ = b[9];
  gate_ = b[10]; transp_ = (int8_t)(b[11] - 12); qstart_ = b[12]; keylock_ = b[13]; lockKey_ = b[14]; lockMinor_ = b[15]; latched_ = b[16];
  selK1_ = 1; selK2_ = 0; view_ = 1; page_ = 1; clearHeld_ = 0; modHeld_ = 0; modFired_ = 0; swHeld_ = 0; swFired_ = 0; hasLastRoot_ = 0;
  published_ = 0;
  for (int i = 0; i < 8; ++i) steps_[i] = st[i];
  render();
  publishKeyLock();  // followers get the key after a load
  return true;
}

// ---------------------------------------------------------------- LEDs: render() stages them like the script, the host shows the last render
uint8_t HrChordArp::keyLevel(int y) const { return y == selK1_ || y == selK2_ ? 15 : 3; }

void HrChordArp::render() {
  uint8_t* lv = led_;
  uint8_t* rl = role_;
  for (int i = 0; i < 32; ++i) lv[i] = rl[i] = 0;
  auto at = [](int x, int y) { return (y - 1) * 8 + (x - 1); };
  if (view_ == 2) {
    for (int x = 1; x <= 4; ++x) {
      lv[at(x, 1)] = x == dir_ ? 15 : 3;
      lv[at(x, 2)] = x == speed_ ? 15 : 3;
      lv[at(x, 4)] = kSizeOpt[x - 1] == size_ ? 15 : 3;
      rl[at(x, 1)] = rl[at(x, 2)] = ROLE_PANEL;
      rl[at(x, 4)] = ROLE_PANEL;
    }
    lv[at(1, 3)] = mode_ == 1 ? 15 : 3;
    lv[at(2, 3)] = mode_ == 2 ? 15 : 3;
    lv[at(3, 3)] = keylock_ == 1 ? 15 : 3;
    lv[at(4, 3)] = keylock_ == 2 ? 15 : 3;
    for (int y = 1; y <= 3; ++y) {
      lv[at(5, y)] = (y == 1 && octave_ == 1) || (y == 2 && octave_ == 0) || (y == 3 && octave_ == -1) ? 15 : 2;
      int l = 2;
      if (y == 2) l = inv_ == 0 ? 6 : 2;
      else if (y == 1 && inv_ > 0) l = 6 + inv_ * 3 > 15 ? 15 : 6 + inv_ * 3;
      else if (y == 3 && inv_ < 0) l = 6 - inv_ * 3 > 15 ? 15 : 6 - inv_ * 3;
      lv[at(6, y)] = (uint8_t)l;
      rl[at(5, y)] = rl[at(6, y)] = ROLE_PANEL;
    }
  } else if (view_ == 3) {
    for (int y = 1; y <= 4; ++y) {
      for (int x = 1; x <= 4; ++x) {
        int idx = (y - 1) * 4 + x;
        lv[at(x, y)] = idx == gate_ ? 15 : (idx == 1 ? 3 : 2);
        rl[at(x, y)] = ROLE_PANEL;
      }
      for (int x = 5; x <= 7; ++x) {
        int idx = (y - 1) * 3 + (x - 5 + 1);
        int l;
        if (idx == vg_) {
          int v = velocity() - 76;
          l = 4 + floordiv(v * 22 + 32, 64);
          l = l < 4 ? 4 : l > 15 ? 15 : l;
        } else {
          l = idx == 1 ? 3 : 2;
        }
        lv[at(x, y)] = (uint8_t)l;
        rl[at(x, y)] = ROLE_PANEL;
      }
    }
  } else if (view_ == 4) {
    int headPage = playing_ ? (playIndex_ + 3) / 4 : 0;
    int headCol = playing_ ? ((playIndex_ - 1) % 4) + 1 : 0;
    for (int col = 1; col <= 4; ++col) {
      const Step& s = steps_[(page_ - 1) * 4 + col - 1];
      bool head = playing_ && headPage == page_ && headCol == col;
      for (int row = 1; row <= 4; ++row) {
        int l;
        if (!s.used) l = 1;
        else if (row < s.dur) l = 3;
        else if (row == s.dur) l = 10;
        else l = 2;
        if (head) l = 15;
        lv[at(col, row)] = (uint8_t)l;
        rl[at(col, row)] = head ? ROLE_PLAYHEAD : ROLE_STEP;
      }
    }
    lv[at(5, 1)] = page_ == 1 ? 15 : 3;
    lv[at(5, 2)] = page_ == 2 ? 15 : 3;
    lv[at(6, 1)] = clearHeld_ ? 15 : 3;
    rl[at(5, 1)] = rl[at(5, 2)] = rl[at(6, 1)] = ROLE_CONTROL;
  } else {
    for (int y = 1; y <= 4; ++y)
      for (int x = 1; x <= 6; ++x) {
        int l = 3;
        if (hasRoot_ && rootX_ == x && rootY_ == y) l = 15;
        else if (hasPendingRoot_ && pendX_ == x && pendY_ == y) l = 10;
        lv[at(x, y)] = (uint8_t)l;
        rl[at(x, y)] = (y & 1) ? ROLE_ROOT_MAJOR : ROLE_ROOT_MINOR;
      }
    for (int y = 1; y <= 4; ++y) {
      int l = keyLevel(y);
      if (hasPendingType_ && (y == pendK1_ || y == pendK2_)) l = 10;
      if (modHeld_ & (1 << (y - 1))) l = 10;
      lv[at(7, y)] = (uint8_t)l;
      rl[at(7, y)] = ROLE_PANEL;
    }
  }
}

void HrChordArp::paint(uint8_t lv[32], uint8_t role[32]) const {
  for (int i = 0; i < 32; ++i) { lv[i] = led_[i]; role[i] = role_[i]; }
  for (int y = 1; y <= 4; ++y) { lv[(y - 1) * 8 + 7] = y == view_ ? 15 : 3; role[(y - 1) * 8 + 7] = ROLE_CONTROL; }  // the function column (host-lit)
}

void HrChordArp::paintExt(uint8_t lv[16]) const {
  for (int i = 0; i < 16; ++i) lv[i] = 3;
  lv[6] = lockMinor_ ? 15 : 3;
  lv[7] = latched_ ? 15 : 3;
  lv[8] = qstart_ == 2 ? 15 : 3;
  lv[9] = hostRunning_ ? 15 : 3;
  lv[10] = page_ == 2 ? 15 : 3;
  lv[11] = 0;
  for (int c = 0; c < 4; ++c) lv[12 + c] = c + 1 == view_ ? 15 : 3;
}

// ---------------------------------------------------------------- Shift legend
void HrChordArp::legend(int col, int row, HrCell& c) const {
  c.clear();
  char b[24] = {0};
  if (col == 7) {
    snprintf(b, sizeof b, "View%d", row + 1);
    c.set(HZ_MODE, b, row + 1 == view_);
    return;
  }
  if (col >= 8) {
    static const char* const k[16] = {"Vel-", "Vel+", "Tran-", "Tran+", "Key-", "Key+", "Minor", "Latch", "Beat", "Play", "Page 2", "", "View1", "View2", "View3", "View4"};
    int i = row * 4 + col - 8;
    if (!k[i][0]) return;
    bool on = (i == 6 && lockMinor_) || (i == 7 && latched_) || (i == 8 && qstart_ == 2) || (i == 9 && hostRunning_) || (i == 10 && page_ == 2) || (i >= 12 && i - 11 == view_);
    c.set(i >= 12 ? HZ_MODE : i >= 6 ? HZ_ACTION : HZ_ADJ, k[i], on);
    return;
  }
  const int x = col + 1, y = row + 1;
  if (view_ == 2) {
    static const char* const kDir[4] = {"Up", "Down", "UpDn", "Patt"};
    static const char* const kSp[4] = {"1/8", "1/8T", "1/16", "1/16T"};
    static const char* const kSz[4] = {"Size 5", "Size 4", "Size 3", "Size 2"};
    if (y == 1 && x <= 4) c.set(HZ_PANEL, kDir[x - 1], x == dir_);
    else if (y == 2 && x <= 4) c.set(HZ_PANEL, kSp[x - 1], x == speed_);
    else if (y == 3 && x <= 4) {
      static const char* const kT[4] = {"Chord", "Arp", "Free", "Lock"};
      c.set(x <= 2 ? HZ_PANEL : HZ_ACTION, kT[x - 1], x <= 2 ? mode_ == x : keylock_ == x - 2);
    } else if (y == 4 && x <= 4) c.set(HZ_PANEL, kSz[x - 1], kSizeOpt[x - 1] == size_);
    else if (x == 5 && y <= 3) c.set(HZ_ADJ, y == 1 ? "Oct+1" : y == 2 ? "Oct 0" : "Oct-1", (y == 1 && octave_ == 1) || (y == 2 && octave_ == 0) || (y == 3 && octave_ == -1));
    else if (x == 6 && y <= 3) c.set(HZ_ADJ, y == 1 ? "Inv+" : y == 2 ? "Inv 0" : "Inv-", y == 2 && inv_ == 0);
  } else if (view_ == 3) {
    if (x <= 4) {
      int idx = (y - 1) * 4 + x;
      snprintf(b, sizeof b, idx == 1 ? "Open" : "Gate%d", idx);
      c.set(HZ_PANEL, b, idx == gate_);
    } else if (x <= 7) {
      int idx = (y - 1) * 3 + (x - 4);
      snprintf(b, sizeof b, idx == 1 ? "Flat" : "Vel %d", idx);
      c.set(HZ_ACTION, b, idx == vg_);
    }
  } else if (view_ == 4) {
    if (x <= 4) {
      const Step& s = steps_[(page_ - 1) * 4 + x - 1];
      snprintf(b, sizeof b, "%d:%d", (page_ - 1) * 4 + x, y);
      c.set(HZ_STEP, b, s.used && s.dur == y);
    } else if (x == 5 && y <= 2) c.set(HZ_MODE, y == 1 ? "Page 1" : "Page 2", page_ == y);
    else if (x == 6 && y == 1) c.set(HZ_HOLD, "Clear", clearHeld_ != 0);
  } else {
    if (x <= 6) {
      cam::Scale cs;  // plan 028: the Camelot position of the root pad ("8A Am"); the pad itself is unchanged
      cam::resolve(cs, kRoots[y - 1][x - 1] % 12, (y & 1) == 0, 0, 0);
      char lb[8];
      cam::label(lb, sizeof lb, cs);
      c.set(HZ_NOTE, lb, hasRoot_ && rootX_ == x && rootY_ == y);
    } else if (x == 7) {
      static const char* const k[4] = {"Maj", "Min", "Dom7", "Sus4"};
      c.set(HZ_PANEL, k[y - 1], y == selK1_ || y == selK2_);
    }
  }
}

void HrChordArp::title(char* out, size_t n) const {
  static const char* const kView[4] = {"Chords", "Arp", "Gate/Vel", "Tracker"};
  static const char* const kDir[4] = {"Up", "Down", "UpDn", "Patt"};
  static const char* const kSp[4] = {"1/8", "1/8T", "1/16", "1/16T"};
  int k = hrAppend(out, n, 0, "HR CHORD/ARP  %s  %s  ", kView[view_ - 1], kTypeName[type_ - 1]);
  if (mode_ == 2) k = hrAppend(out, n, k, "Arp %s %s", kSp[speed_ - 1], kDir[dir_ - 1]);
  else k = hrAppend(out, n, k, "Chord");
  if (size_ != 5) k = hrAppend(out, n, k, "  Size %d", (int)size_);
  if (octave_) k = hrAppend(out, n, k, "  Oct %+d", (int)octave_);
  if (inv_) k = hrAppend(out, n, k, "  Inv %+d", (int)inv_);
  k = hrAppend(out, n, k, "  Vel %d  %s", (int)baseVel_, latched_ ? "Latch" : "Moment");
  if (keylock_ == 2) {
    cam::Scale ks;  // plan 028: the locked key as its Camelot position
    char kl[16];
    camKey(ks);
    cam::label(kl, sizeof kl, ks);
    hrAppend(out, n, k, "  Lock %s", kl);
  }
}

const char* HrChordArp::help() const {
  switch (view_) {
    case 2: return "Rows: dir, speed, mode/lock, size. Columns: Oct, Inv. Column 8 = view. Right: Vel Key Latch.";
    case 3: return "Left 4x4: gate pattern. Next 3 columns: velocity groove. Column 8 picks the view.";
    case 4: return "Tap a cell: store the last root (row = length). Clear + cell = empty. Play starts it.";
    default: return "Tap a root; hold a type key (two = more). Column 8 = view. Right: Vel Key Latch Play.";
  }
}

}  // namespace seqgen
