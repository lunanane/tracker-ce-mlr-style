// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Ported 1:1 from HYPER_RASP (same owner) scripts/arp_lines.lua and scripts/chord_stabs.lua. See hr_stab.h.
#include "hr_stab.h"

#include <stdio.h>

namespace seqgen {

namespace {
const uint8_t kChordLen[8] = {3, 4, 4, 5, 4, 3, 3, 3};
const int8_t kMajor[8][5] = {{0, 4, 7}, {0, 4, 7, 9}, {0, 4, 7, 11}, {0, 4, 7, 11, 14}, {0, 4, 7, 14}, {0, 2, 7}, {0, 5, 7}, {0, 7, 12}};
const int8_t kMinor[8][5] = {{0, 3, 7}, {0, 3, 7, 9}, {0, 3, 7, 10}, {0, 3, 7, 10, 14}, {0, 3, 7, 14}, {0, 2, 7}, {0, 5, 7}, {0, 7, 12}};
const char* const kChordName[8] = {"triad", "6", "7", "9", "add9", "sus2", "sus4", "5"};
const char* const kChordWord[8] = {"Triad", "6th", "7th", "9th", "add9", "Sus2", "Sus4", "5th"};

// patterns 1..10. Stab rhythms ('X' 1.0, 'x' .78, 'o' .45, '.' rest) and the euclid ones (k, n, repeats)
struct StabPat { const char* name; const char* word; const char* grid; uint8_t gate, k, n, rep; };
const StabPat kStab[10] = {
    {"charleston", "Charl", "X.....x.........", 10, 0, 0, 0}, {"dub", "Dub", "....X.......x...", 5, 0, 0, 0},
    {"offbeat", "Offbt", "..X...x...X...x.", 3, 0, 0, 0},      {"push", "Push", "...x...X...x...X", 4, 0, 0, 0},
    {"two-step", "2step", "X..x..o...X..x..", 6, 0, 0, 0},      {"rolling", "Roll", "..Xx..xx..Xx..xx", 2, 0, 0, 0},
    {"tresillo", "Tresil", 0, 5, 3, 8, 2},                       {"cinquillo", "Cinq", 0, 3, 5, 8, 2},
    {"five", "Five", 0, 6, 5, 16, 1},                            {"seven", "Seven", 0, 3, 7, 16, 1}};

struct ArpPat { const char* name; const char* word; uint8_t gate, k, n, rep; const char* accent; uint8_t nd; uint8_t steps[16]; };
const ArpPat kArp[10] = {
    {"runner", "Runner", 5, 0, 0, 0, 0, 16, {1, 2, 3, 4, 5, 6, 7, 6, 5, 4, 3, 2, 1, 2, 3, 4}},
    {"acid", "Acid", 4, 0, 0, 0, "...X..X.....X...", 16, {1, 0, 1, 4, 1, 0, 4, 0, 1, 0, 1, 2, 4, 0, 3, 0}},
    {"pluck", "Pluck", 6, 0, 0, 0, 0, 16, {0, 0, 1, 0, 0, 0, 3, 0, 0, 0, 2, 0, 0, 0, 4, 0}},
    {"3-roll", "3roll", 3, 0, 0, 0, 0, 16, {1, 2, 3, 0, 4, 5, 6, 0, 3, 2, 1, 0, 2, 3, 4, 0}},
    {"octave", "Octave", 4, 0, 0, 0, "X...X...X...X...", 16, {1, 0, 4, 0, 1, 0, 4, 0, 1, 0, 4, 0, 1, 0, 5, 0}},
    {"pendulum", "Pendul", 3, 0, 0, 0, 0, 16, {1, 6, 2, 5, 3, 4, 4, 3, 5, 2, 6, 1, 1, 6, 2, 5}},
    {"tresillo", "Tresil", 5, 3, 8, 2, 0, 6, {1, 3, 5, 2, 4, 6}},
    {"cinquillo", "Cinq", 3, 5, 8, 2, 0, 5, {1, 4, 2, 5, 3}},
    {"five", "Five", 6, 5, 16, 1, 0, 5, {1, 5, 2, 6, 3}},
    {"seven", "Seven", 3, 7, 16, 1, 0, 7, {1, 2, 3, 4, 3, 2, 5}}};
}  // namespace

HrStab::HrStab(Kind k)
    : HrScript(k == KIND_ARP ? HR_ARP_LINES : HR_CHORD_STABS), latched_(0), quality_(1), chord_(1), pattern_(k == KIND_ARP ? 1 : 3), role_(ROLE_LEADER),
      blink_(0), rate_(6), vel_(100), octave_(0), transp_(0), pos_(0), published_(0), nv_(0) {
  for (int i = 0; i < 6; ++i) { voices_[i].note = 0; voices_[i].left = 0; }
}

int HrStab::patternSteps() const {
  if (!arp()) { const StabPat& p = kStab[pattern_ - 1]; return p.grid ? 16 : p.n * p.rep; }
  const ArpPat& p = kArp[pattern_ - 1];
  return p.k ? p.n * p.rep : 16;
}

// intervals() of the chord / quality, applied to `note` (unclamped)
int HrStab::chordNotes(int note, int* out) const {
  const int8_t* iv = quality_ == 2 ? kMinor[chord_ - 1] : kMajor[chord_ - 1];
  int n = kChordLen[chord_ - 1];
  for (int i = 0; i < n; ++i) out[i] = note + iv[i];
  return n;
}

void HrStab::stopVoices() {
  for (int i = 0; i < nv_; ++i) noteOff(voices_[i].note);
  nv_ = 0;
}

void HrStab::ageVoices() {
  int i = 0;
  while (i < nv_) {
    if (--voices_[i].left <= 0) {
      noteOff(voices_[i].note);
      for (int j = i; j + 1 < nv_; ++j) voices_[j] = voices_[j + 1];
      --nv_;
    } else {
      ++i;
    }
  }
}

// ---------------------------------------------------------------- follower: the scale of the shared bus
void HrStab::scaleContext(uint8_t* scale, int* n, int* root) const {
  const HrBus& b = bus();
  int r = b.root >= 0 ? b.root : 60;
  if (b.nScale == 0) {
    for (int i = 0; i < 12; ++i) scale[i] = (uint8_t)((r + i) % 12);
    *n = 12;
  } else {
    for (int i = 0; i < b.nScale; ++i) scale[i] = b.scale[i];
    *n = b.nScale;
  }
  *root = r;
}

static int scaleBase(const uint8_t* sc, int root) { return root - floormod(root - sc[0], 12); }
static int noteForDegree(int degree, const uint8_t* sc, int n, int root) {
  int interval = floormod(sc[floormod(degree, n)] - sc[0], 12);
  return scaleBase(sc, root) + 12 * floordiv(degree, n) + interval;
}
static int degreeForNote(int note, const uint8_t* sc, int n, int root) {
  int rel = note - scaleBase(sc, root);
  int oct = floordiv(rel, 12), pc = floormod(rel, 12);
  int best = -1, bestIndex = 0;
  for (int i = 0; i < n; ++i) {
    int d = iabs(floormod(sc[i] - sc[0], 12) - pc);
    if (best < 0 || d < best) { best = d; bestIndex = i; }
  }
  if ((12 - pc) < best) return oct * n + n;
  return oct * n + bestIndex;
}

// chord_pool(pitch_class)
int HrStab::chordPool(int pc, int* notes) const {
  if (!follower()) return chordNotes(rootNote(pc), notes);
  uint8_t sc[12];
  int n, root;
  scaleContext(sc, &n, &root);
  int origin = 0;
  if (quality_ == 2 && n >= 6) {  // key_origin
    int third = floormod(sc[2] - sc[0], 12);
    origin = third == 4 ? 5 : third == 3 ? 2 : 0;
  }
  int degree = (pc - 1) + origin;
  degree = degreeForNote(noteForDegree(degree, sc, n, root) + transp_, sc, n, root);
  const int8_t* iv = quality_ == 2 ? kMinor[chord_ - 1] : kMajor[chord_ - 1];
  int cnt = kChordLen[chord_ - 1];
  for (int i = 0; i < cnt; ++i) notes[i] = noteForDegree(degree + (iv[i] * n + 6) / 12, sc, n, root) + 12 * octave_;
  return cnt;
}

// ---------------------------------------------------------------- sounding
void HrStab::trigger(int degree, int pct, int gate) {
  if (!latched_) return;
  int pool[5];
  if (arp()) {
    int size = chordPool(latched_, pool);
    int note = pool[floormod(degree - 1, size)] + 12 * floordiv(degree - 1, size);
    note = note < 0 ? 0 : note > 127 ? 127 : note;  // clamp_note
    stopVoices();
    noteOn(note, hrScaleVel(vel_, pct));
    voices_[nv_].note = (uint8_t)note; voices_[nv_].left = (int8_t)gate; ++nv_;
    return;
  }
  stopVoices();
  int vel = hrScaleVel(vel_, pct);
  int n = chordNotes(rootNote(latched_), pool);
  for (int i = 0; i < n; ++i) {
    int nt = pool[i] < 0 ? 0 : pool[i] > 127 ? 127 : pool[i];
    noteOn(nt, vel);
    voices_[nv_].note = (uint8_t)nt; voices_[nv_].left = (int8_t)gate; ++nv_;
  }
}

void HrStab::revoice() {
  if (arp()) trigger(1, 100, kArp[pattern_ - 1].gate);
  else trigger(0, 78, kStab[pattern_ - 1].gate);
}

void HrStab::publishScale() {
  if (arp() && role_ != ROLE_LEADER) return;
  if (!latched_) return;
  static const uint8_t kMaj[7] = {0, 2, 4, 5, 7, 9, 11}, kMin[7] = {0, 2, 3, 5, 7, 8, 10};
  const uint8_t* iv = quality_ == 2 ? kMin : kMaj;
  int tonic = (latched_ - 1) % 12;
  uint16_t sig = (uint16_t)(1 + tonic * 2 + (quality_ == 2 ? 1 : 0));
  if (sig == published_) return;
  published_ = sig;
  if (bus_) {
    uint8_t pcs[7];
    for (int i = 0; i < 7; ++i) pcs[i] = (uint8_t)((tonic + iv[i]) % 12);
    bus_->setScale(pcs, 7);
  }
}

void HrStab::publishRoot(int pc) {
  if (arp() && role_ != ROLE_LEADER) return;
  if (bus_) bus_->root = (int16_t)rootNote(pc);
}

void HrStab::pressRoot(int pc) {
  if (latched_ == pc && !hold_) {
    stopVoices();
    latched_ = 0;
    blink_ = 0;
    return;
  }
  latched_ = (uint8_t)pc;
  publishScale();
  publishRoot(pc);
  revoice();
  blink_ = 3;
}

void HrStab::pressQuality(int q) {
  if (quality_ == q) return;
  quality_ = (uint8_t)q;
  publishScale();
  revoice();
}

void HrStab::pressChord(int i) {
  if (chord_ == i) return;
  chord_ = (uint8_t)i;
  revoice();
}

void HrStab::grid(int x, int y, int z) {
  if (z != 1) {
    if (hold_ && z == 0 && y <= 2 && x <= 6 && latched_ == (y - 1) * 6 + x) unlatchRoot();
    return;
  }
  if (y <= 2 && x <= 6) pressRoot((y - 1) * 6 + x);
  else if (x == 1 && y == 3) pressQuality(1);
  else if (x == 1 && y == 4) pressQuality(2);
  else if (x >= 2 && x <= 6 && y >= 3) pattern_ = (uint8_t)((y - 3) * 5 + (x - 2) + 1);
  else if (x >= 7 && x <= 8 && y <= 4) pressChord((x - 7) * 4 + y);
}

// The level in percent (0 = rest) of step `index` (1-based), its degree (arp) and the pattern's gate in pulses
int HrStab::stepPct(int index, int* degree, int* gate) const {
  *degree = 0;
  if (!arp()) {
    const StabPat& p = kStab[pattern_ - 1];
    *gate = p.gate;
    char c;
    if (p.grid) {
      c = p.grid[index - 1];
    } else {
      uint8_t on[16];
      hrEuclid(p.k, p.n, on);
      int i0 = (index - 1) % p.n;
      c = on[i0] ? (i0 == 0 ? 'X' : 'x') : '.';
    }
    return c == 'X' ? 100 : c == 'x' ? 78 : c == 'o' ? 45 : 0;
  }
  const ArpPat& p = kArp[pattern_ - 1];
  *gate = p.gate;
  if (!p.k) {
    *degree = p.steps[index - 1];
    if (p.accent) return p.accent[index - 1] == 'X' ? 100 : 78;
    return ((index - 1) % 4 == 0) ? 100 : 78;
  }
  uint8_t on[16];
  hrEuclid(p.k, p.n, on);
  int i0 = (index - 1) % p.n, perRep = 0, upTo = 0;
  for (int i = 0; i < p.n; ++i) {
    perRep += on[i];
    if (i <= i0) upTo += on[i];
  }
  if (!on[i0]) return 0;
  int taken = ((index - 1) / p.n) * perRep + upTo;
  *degree = p.steps[(taken - 1) % p.nd];
  return i0 == 0 ? 100 : 78;
}

void HrStab::fireStep(int index) {
  int degree, gate;
  int pct = stepPct(index, &degree, &gate);
  if (!pct || (arp() && !degree)) return;
  trigger(degree, pct, gate);
  if (latched_) blink_ = 3;
}

void HrStab::pulse() {
  ageVoices();
  if (blink_ > 0) --blink_;
  int total = patternPulses();
  int position = pos_ % total;
  if (position % rate_ == 0) fireStep(position / rate_ + 1);
  pos_ = (uint16_t)((pos_ + 1) % patternPulses());
}

// ---------------------------------------------------------------- params (set_param) and the extra pads
int HrStab::getParam(int id) const {
  switch (id) {
    case P_OCTAVE: return octave_;
    case P_RATE: return rate_;
    case P_VEL: return vel_;
    case P_TRANSP: return transp_;
    case P_ROLE: return role_;
    case P_PATTERN: return pattern_;
    case P_CHORD: return chord_;
    case P_QUALITY: return quality_;
    case P_ROOT: return latched_;
    default: return 0;
  }
}

void HrStab::setParam(int id, int v) {
  switch (id) {
    case P_OCTAVE: octave_ = (int8_t)(v < -2 ? -2 : v > 2 ? 2 : v); break;
    case P_TRANSP: transp_ = (int8_t)(v < -12 ? -12 : v > 12 ? 12 : v); break;
    case P_RATE:
      rate_ = (uint8_t)(v < 1 ? 1 : v > 24 ? 24 : v);
      pos_ = (uint16_t)(pos_ % patternPulses());
      return;
    case P_VEL: vel_ = (uint8_t)(v < 1 ? 1 : v > 127 ? 127 : v); return;
    case P_ROLE:
      if (!arp()) return;
      role_ = v == ROLE_FOLLOWER ? ROLE_FOLLOWER : ROLE_LEADER;
      return;
    case P_PATTERN: if (v >= 1 && v <= 10) pattern_ = (uint8_t)v; return;
    case P_CHORD: if (v >= 1 && v <= 8) pressChord(v); return;
    case P_QUALITY: if (v == 1 || v == 2) pressQuality(v); return;
    default: return;
  }
  if (latched_) {
    publishRoot(latched_);
    revoice();
  }
}

void HrStab::ext(int c, int r, bool down) {
  if (!down) return;
  switch (r * 4 + c) {
    case 0: setParam(P_OCTAVE, octave_ - 1); break;
    case 1: setParam(P_OCTAVE, octave_ + 1); break;
    case 2: setParam(P_TRANSP, transp_ - 1); break;
    case 3: setParam(P_TRANSP, transp_ + 1); break;
    case 4: setParam(P_RATE, hrRateStep(rate_, -1)); break;
    case 5: setParam(P_RATE, hrRateStep(rate_, 1)); break;
    case 6: setParam(P_VEL, vel_ - 8); break;
    case 7: setParam(P_VEL, vel_ + 8); break;
    case 8: setParam(P_ROLE, ROLE_LEADER); break;
    case 9: setParam(P_ROLE, ROLE_FOLLOWER); break;
    case 12: setParam(P_OCTAVE, 0); break;
    case 13: setParam(P_RATE, 6); break;
    case 14: setParam(P_VEL, 100); break;
    case 15: setParam(P_TRANSP, 0); break;
    default: break;
  }
}

// ---------------------------------------------------------------- serialization (settings only)
size_t HrStab::serialize(uint8_t* b, size_t cap) const {
  if (cap < (size_t)kSerialSize) return 0;
  b[0] = kSerialVersion; b[1] = (uint8_t)(arp() ? KIND_ARP : KIND_STABS); b[2] = quality_; b[3] = chord_; b[4] = pattern_; b[5] = role_;
  b[6] = (uint8_t)(octave_ + 2); b[7] = rate_; b[8] = vel_; b[9] = (uint8_t)(transp_ + 12);
  return kSerialSize;
}

bool HrStab::deserialize(const uint8_t* b, size_t len) {
  if (len < (size_t)kSerialSize || b[0] != kSerialVersion || b[1] != (arp() ? KIND_ARP : KIND_STABS)) return false;
  if (b[2] < 1 || b[2] > 2 || b[3] < 1 || b[3] > 8 || b[4] < 1 || b[4] > 10 || b[5] < 1 || b[5] > 2 || b[6] > 4 || b[7] < 1 || b[7] > 24 ||
      b[8] < 1 || b[8] > 127 || b[9] > 24)
    return false;
  stopVoices();
  latched_ = 0; blink_ = 0; published_ = 0;
  quality_ = b[2]; chord_ = b[3]; pattern_ = b[4]; role_ = arp() ? b[5] : (uint8_t)ROLE_LEADER;
  octave_ = (int8_t)(b[6] - 2); rate_ = b[7]; vel_ = b[8]; transp_ = (int8_t)(b[9] - 12);
  pos_ = (uint16_t)(pos_ % patternPulses());
  return true;
}

// ---------------------------------------------------------------- LEDs
int HrStab::rootLevel(int pc) const {
  if (latched_ != pc) return 3;
  return blink_ > 0 ? 15 : 8;
}

void HrStab::paint(uint8_t lv[32], uint8_t role[32]) const {
  uint8_t sc[12];
  int n = 1, root = 0;
  if (follower()) scaleContext(sc, &n, &root);
  for (int pc = 1; pc <= 12; ++pc) {
    int i = ((pc - 1) / 6) * 8 + (pc - 1) % 6;
    lv[i] = (uint8_t)rootLevel(pc);
    role[i] = (follower() && (pc - 1) % n == 0) ? ROLE_DEGREE : ROLE_ROOT_MAJOR;
  }
  lv[2 * 8] = quality_ == 1 ? 15 : 3; role[2 * 8] = ROLE_CONTROL;
  lv[3 * 8] = quality_ == 2 ? 15 : 3; role[3 * 8] = ROLE_CONTROL;
  for (int k = 1; k <= 10; ++k) {
    int i = (2 + (k - 1) / 5) * 8 + 1 + (k - 1) % 5;
    lv[i] = k == pattern_ ? 15 : 3; role[i] = ROLE_PANEL;
  }
  for (int k = 1; k <= 8; ++k) {
    int i = ((k - 1) % 4) * 8 + 6 + (k - 1) / 4;
    lv[i] = k == chord_ ? 15 : 3; role[i] = ROLE_PANEL;
  }
}

void HrStab::paintExt(uint8_t lv[16]) const {
  for (int i = 0; i < 16; ++i) lv[i] = 3;
  if (arp()) { lv[8] = role_ == ROLE_LEADER ? 15 : 3; lv[9] = role_ == ROLE_FOLLOWER ? 15 : 3; }
  else lv[8] = lv[9] = 0;
  lv[10] = lv[11] = 0;
}

// ---------------------------------------------------------------- Shift legend
void HrStab::legend(int col, int row, HrCell& c) const {
  c.clear();
  char b[16] = {0};
  if (col < 6 && row < 2) {
    int pc = row * 6 + col + 1;
    if (follower()) snprintf(b, sizeof b, "%d", pc);
    else snprintf(b, sizeof b, "%s", hrNoteName(pc - 1));
    c.set(HZ_NOTE, b, latched_ == pc);
  } else if (col == 0 && row == 2) {
    c.set(HZ_PANEL, "Major", quality_ == 1);
  } else if (col == 0 && row == 3) {
    c.set(HZ_PANEL, "Minor", quality_ == 2);
  } else if (col >= 1 && col <= 5 && row >= 2) {
    int k = (row - 2) * 5 + col;
    c.set(HZ_PANEL, arp() ? kArp[k - 1].word : kStab[k - 1].word, k == pattern_);
  } else if (col == 6 || col == 7) {
    int k = (col - 6) * 4 + row + 1;
    c.set(HZ_ACTION, kChordWord[k - 1], k == chord_);
  } else if (col >= 8) {
    static const char* const k[16] = {"Oct-", "Oct+", "Tran-", "Tran+", "Slower", "Faster", "Vel-", "Vel+", "Lead", "Follow", "", "", "Oct 0", "1/16", "Vel100", "Tran 0"};
    int i = row * 4 + col - 8;
    if (i == 8 || i == 9) {
      if (arp()) c.set(HZ_PANEL, k[i], role_ == (i == 8 ? ROLE_LEADER : ROLE_FOLLOWER));
    } else if (k[i][0]) {
      c.set(i >= 12 ? HZ_ACTION : HZ_ADJ, k[i]);
    }
  }
}

void HrStab::title(char* out, size_t n) const {
  // "ARP LINES  C# min  cinquillo  add9  1/16  Oct -2  Vel 100  Tran +12  Follow" (Oct, Tran: only when not 0)
  int k = hrAppend(out, n, 0, "%s  ", arp() ? "ARP LINES" : "CHORD STABS");
  if (!latched_) k = hrAppend(out, n, k, "no root");
  else if (follower()) k = hrAppend(out, n, k, "key %d %s", latched_, quality_ == 1 ? "maj" : "min");
  else k = hrAppend(out, n, k, "%s %s", hrNoteName(latched_ - 1), quality_ == 1 ? "maj" : "min");
  k = hrAppend(out, n, k, "  %s  %s  %s  Vel %d", arp() ? kArp[pattern_ - 1].name : kStab[pattern_ - 1].name, kChordName[chord_ - 1], hrRateName(rate_), (int)vel_);
  if (octave_) k = hrAppend(out, n, k, "  Oct %+d", (int)octave_);
  if (transp_) k = hrAppend(out, n, k, "  Tran %+d", (int)transp_);
  if (follower()) hrAppend(out, n, k, "  Follow");
}

const char* HrStab::help() const {
  return arp() ? "Tap a root (again: stop). Pattern + Chord pick the line. Right: Oct Rate Vel Tran Role."
               : "Tap a root (again: stop). Pattern = rhythm, Chord = voicing. Right: Oct Rate Vel Tran.";
}

}  // namespace seqgen
