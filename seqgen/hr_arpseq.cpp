// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Ported 1:1 from HYPER_RASP (same owner) scripts/arp_seq.lua. See hr_arpseq.h.
#include "hr_arpseq.h"

#include <stdio.h>

namespace seqgen {

namespace {
// CHORD_TYPES 1..15: intervals (-1 terminates)
const int8_t kTypes[15][6] = {{0, 4, 7, -1}, {0, 3, 7, -1}, {0, 4, 7, 11, -1}, {0, 3, 7, 10, -1}, {0, 2, 7, -1}, {0, 5, 7, -1}, {0, 4, 7, 9, -1}, {0, 4, 7, 10, -1},
                              {0, 3, 6, -1}, {0, 4, 8, -1}, {0, 4, 7, 11, 14}, {0, 3, 7, 10, 14}, {0, 3, 6, 9, -1}, {0, 3, 7, 9, -1}, {0, 3, 6, 10, -1}};
const char* const kTypeName[15] = {"maj", "min", "maj7", "min7", "sus2", "sus4", "6", "dom7", "dim", "aug", "maj9", "min9", "dim7", "min6", "m7b5"};
// CHORD_GESTURES: sequences of chord keys (digits) -> type
struct Gesture { const char* keys; uint8_t type; };
const Gesture kGestures[15] = {{"1", 1}, {"2", 2}, {"3", 8}, {"12", 3}, {"21", 4}, {"13", 5}, {"23", 6}, {"31", 7}, {"32", 14},
                               {"123", 11}, {"213", 12}, {"132", 9}, {"231", 10}, {"312", 13}, {"321", 15}};
const uint8_t kSpeeds[4] = {12, 8, 6, 4};         // ARP_SPEEDS, pulses per arp step
const uint8_t kArpSq[8] = {1, 3, 2, 4, 3, 5, 4, 6};  // ARP_SEQUENCE
const int8_t kOct[3] = {-1, 0, 1};
const uint8_t kChordOrderLevel[3] = {15, 10, 6};
}  // namespace

HrArpSeq::HrArpSeq()
    : HrScript(HR_ARP_SEQ), latched_(0), manual_(0), quality_(1), ctype_(0), seqN_(0), chordDown_(0), poolN_(0), soundN_(0), arpStep_(0), arpPulse_(0),
      seqPulse_(0), phase_(1), fade_(0), start_(1), end_(8), current_(1), playing_(0), heldSteps_(0), gestureN_(0), gFirst_(0), gLo_(0), gHi_(0) {
  seq_[0] = seq_[1] = seq_[2] = 0;
  cyc_[MODE] = 1; cyc_[SPEED] = 3; cyc_[DIRECTION] = 1; cyc_[GROOVE] = 1; cyc_[OCTAVE] = 2;
  for (int i = 0; i < 5; ++i) { pool_[i] = 0; sounding_[i] = 0; }
  for (int i = 0; i < 8; ++i) { Step z = {}; steps_[i] = z; }
}

const int8_t* HrArpSeq::chordIntervals(int* n) const {
  static const int8_t kMaj[3] = {0, 4, 7}, kMin[3] = {0, 3, 7};
  if (ctype_) {
    const int8_t* t = kTypes[ctype_ - 1];
    int k = 0;
    while (k < 5 && t[k] >= 0) ++k;
    *n = k;
    return t;
  }
  *n = 3;
  return quality_ == 2 ? kMin : kMaj;
}

int HrArpSeq::velocityNow() const { return hrGrooveVel(cyc_[GROOVE], phase_, 100); }

void HrArpSeq::stopSounding() {
  for (int i = 0; i < soundN_; ++i) noteOff(sounding_[i]);
  soundN_ = 0;
}

int HrArpSeq::arpNoteAt(int direction, int step) const {
  int n = poolN_;
  int index;
  if (direction == 2) {
    index = n - floormod(step - 1, n);
  } else if (direction == 3) {
    int cycle = n * 2, phase = floormod(step - 1, cycle);
    index = phase < n ? phase + 1 : cycle - phase;
  } else if (direction == 4) {
    index = floormod(kArpSq[floormod(step - 1, 8)] - 1, n) + 1;
  } else {
    index = floormod(step - 1, n) + 1;
  }
  return pool_[index - 1];
}

void HrArpSeq::soundLatched() {
  if (!latched_) return;
  int root = 60 + latched_ - 1;
  if (bus_) bus_->root = (int16_t)root;
  int n;
  const int8_t* iv = chordIntervals(&n);
  int shift = 12 * kOct[cyc_[OCTAVE] - 1];
  poolN_ = (uint8_t)n;
  for (int i = 0; i < n; ++i) pool_[i] = (int16_t)(root + iv[i] + shift);
  arpStep_ = 1;
  arpPulse_ = 0;
  if (cyc_[MODE] == 2) {  // chord: the whole voicing, and it holds
    for (int i = 0; i < n; ++i) {
      noteOn(pool_[i], velocityNow());
      if (soundN_ < 5) sounding_[soundN_++] = (uint8_t)pool_[i];
    }
    return;
  }
  int note = arpNoteAt(cyc_[DIRECTION], arpStep_);
  noteOn(note, velocityNow());
  sounding_[0] = (uint8_t)note;
  soundN_ = 1;
}

void HrArpSeq::pressRoot(int pc) {
  stopSounding();
  if (latched_ == pc && manual_) {
    latched_ = 0;
    manual_ = 0;
    poolN_ = 0;
    return;
  }
  latched_ = (uint8_t)pc;
  manual_ = 1;
  soundLatched();
}

void HrArpSeq::toggleQuality() {
  quality_ = quality_ == 1 ? 2 : 1;
  ctype_ = 0;
  seqN_ = 0;
  fade_ = 16;
  stopSounding();
  soundLatched();
}

void HrArpSeq::pressCycle(int c) {
  static const uint8_t kCount[5] = {2, 4, 4, 12, 3};
  cyc_[c] = (uint8_t)(cyc_[c] % kCount[c] + 1);
  stopSounding();
  soundLatched();
}

void HrArpSeq::selectChord(int key) {
  if (chordDown_ == 0) seqN_ = 0;
  if (chordDown_ < 255) ++chordDown_;
  for (int i = 0; i < seqN_; ++i)
    if (seq_[i] == key) return;
  if (seqN_ >= 3) return;  // cannot happen (three keys, no repeats); keeps the array safe
  seq_[seqN_++] = (uint8_t)key;
  int selected = 0;
  for (const Gesture& g : kGestures) {
    int k = 0;
    while (g.keys[k]) ++k;
    if (k != seqN_) continue;
    bool same = true;
    for (int i = 0; i < k; ++i) if (g.keys[i] - '0' != seq_[i]) same = false;
    if (same) { selected = g.type; break; }
  }
  if (selected == ctype_) {  // the same gesture again clears back to the triad
    ctype_ = 0;
    seqN_ = 0;
  } else {
    ctype_ = (uint8_t)selected;
  }
  stopSounding();
  soundLatched();
}

int HrArpSeq::chordPosition(int key) const {
  for (int i = 0; i < seqN_; ++i)
    if (seq_[i] == key) return i + 1;
  return 0;
}

void HrArpSeq::captureStep(int col) {
  Step& s = steps_[col - 1];
  s.used = 1;
  s.root = latched_;
  s.quality = quality_;
  s.ctype = ctype_;
  s.seqN = seqN_;
  for (int i = 0; i < 3; ++i) s.seq[i] = seq_[i];
  for (int i = 0; i < 5; ++i) s.cyc[i] = cyc_[i];
}

void HrArpSeq::applyStep(const Step& s) {
  quality_ = s.quality;
  ctype_ = s.ctype;
  seqN_ = s.seqN;
  for (int i = 0; i < 3; ++i) seq_[i] = s.seq[i];
  for (int i = 0; i < 5; ++i) cyc_[i] = s.cyc[i];
  stopSounding();
  latched_ = s.root;
  if (latched_) soundLatched();
}

void HrArpSeq::setPlay(bool running) {
  if ((playing_ != 0) == running) return;
  playing_ = running ? 1 : 0;
  if (running && !manual_) {
    if (steps_[current_ - 1].used) {
      Step s = steps_[current_ - 1];
      applyStep(s);
    }
  }
}

void HrArpSeq::grid(int x, int y, int z) {
  bool chordKey = (x == 7 && y <= 3);
  bool stepKey = (y == 4 && x <= 8);
  if (z != 1) {
    if (chordKey && chordDown_ > 0) --chordDown_;
    if (stepKey) {
      if (heldSteps_ > 0) --heldSteps_;
      if (heldSteps_ == 0) {
        if (gestureN_ == 1) captureStep(gFirst_);
        gestureN_ = 0;
      }
    }
    return;
  }
  if (y <= 2 && x <= 6) pressRoot((y - 1) * 6 + x);
  else if (x == 1 && y == 3) toggleQuality();
  else if (y == 3 && x >= 2 && x <= 6) pressCycle(x - 2);
  else if (chordKey) selectChord(y);
  else if (stepKey) {
    if (heldSteps_ < 255) ++heldSteps_;
    if (gestureN_ == 0) { gFirst_ = (uint8_t)x; gLo_ = gHi_ = (uint8_t)x; }
    else {
      if (x < gLo_) gLo_ = (uint8_t)x;
      if (x > gHi_) gHi_ = (uint8_t)x;
    }
    if (gestureN_ < 255) ++gestureN_;
    if (heldSteps_ >= 2) { start_ = gLo_; end_ = gHi_; }
  } else if (x == 8 && y == 3) setPlay(!playing_);
}

void HrArpSeq::advanceArp() {
  if (!latched_ || poolN_ == 0) return;
  if (cyc_[MODE] == 2) return;
  if (++arpPulse_ < kSpeeds[cyc_[SPEED] - 1]) return;
  arpPulse_ = 0;
  ++arpStep_;
  stopSounding();
  int note = arpNoteAt(cyc_[DIRECTION], arpStep_);
  noteOn(note, velocityNow());
  sounding_[0] = (uint8_t)note;
  soundN_ = 1;
}

void HrArpSeq::advanceSequencer() {
  if (!playing_) return;
  if (++seqPulse_ < 384) return;  // 4 bars per step
  seqPulse_ = 0;
  ++current_;
  if (current_ > end_ || current_ < start_) current_ = start_;
  if (steps_[current_ - 1].used && !manual_) {
    Step s = steps_[current_ - 1];
    applyStep(s);
  }
}

void HrArpSeq::pulse() {
  phase_ = (uint8_t)(phase_ % 16 + 1);
  advanceArp();
  advanceSequencer();
}

void HrArpSeq::tick() {
  if (fade_ > 0) --fade_;
}

// ---------------------------------------------------------------- params
int HrArpSeq::getParam(int id) const {
  switch (id) {
    case P_ROOT: return latched_;
    case P_QUALITY: return quality_;
    case P_CHORD_TYPE: return ctype_;
    case P_MODE: return cyc_[MODE];
    case P_SPEED: return cyc_[SPEED];
    case P_DIR: return cyc_[DIRECTION];
    case P_GROOVE: return cyc_[GROOVE];
    case P_OCT: return cyc_[OCTAVE];
    case P_PLAYING: return playing_;
    case P_START: return start_;
    case P_END: return end_;
    case P_CURRENT: return current_;
    default: return 0;
  }
}

void HrArpSeq::setParam(int id, int v) {
  if (id == P_PLAYING) setPlay(v != 0);
}

// ---------------------------------------------------------------- serialization (settings: the programmed steps and the loop range)
size_t HrArpSeq::serialize(uint8_t* b, size_t cap) const {
  if (cap < (size_t)kSerialSize) return 0;
  for (int i = 0; i < kSerialSize; ++i) b[i] = 0;
  b[0] = kSerialVersion; b[1] = quality_; b[2] = ctype_; b[3] = seqN_; b[4] = (uint8_t)(seq_[0] | seq_[1] << 4); b[5] = seq_[2];
  for (int i = 0; i < 5; ++i) b[6 + i] = cyc_[i];
  b[11] = start_; b[12] = end_; b[13] = current_;
  for (int i = 0; i < 8; ++i) {
    const Step& s = steps_[i];
    uint8_t* p = b + 14 + i * 8;
    p[0] = s.used; p[1] = s.root; p[2] = s.quality; p[3] = s.ctype;
    p[4] = (uint8_t)(s.seqN | s.seq[0] << 2 | s.seq[1] << 4 | s.seq[2] << 6);
    p[5] = (uint8_t)(s.cyc[0] | s.cyc[1] << 4); p[6] = (uint8_t)(s.cyc[2] | s.cyc[3] << 4); p[7] = s.cyc[4];
  }
  return kSerialSize;
}

bool HrArpSeq::deserialize(const uint8_t* b, size_t len) {
  if (len < (size_t)kSerialSize || b[0] != kSerialVersion) return false;
  static const uint8_t kMax[5] = {2, 4, 4, 12, 3};
  if (b[1] < 1 || b[1] > 2 || b[2] > 15 || b[3] > 3 || (b[4] & 15) > 3 || (b[4] >> 4) > 3 || b[5] > 3) return false;
  for (int i = 0; i < 5; ++i) if (b[6 + i] < 1 || b[6 + i] > kMax[i]) return false;
  if (b[11] < 1 || b[11] > 8 || b[12] < b[11] || b[12] > 8 || b[13] < 1 || b[13] > 8) return false;
  Step st[8];
  for (int i = 0; i < 8; ++i) {
    const uint8_t* p = b + 14 + i * 8;
    Step& s = st[i];
    s.used = p[0]; s.root = p[1]; s.quality = p[2]; s.ctype = p[3];
    s.seqN = p[4] & 3; s.seq[0] = (p[4] >> 2) & 3; s.seq[1] = (p[4] >> 4) & 3; s.seq[2] = (p[4] >> 6) & 3;
    s.cyc[0] = p[5] & 15; s.cyc[1] = p[5] >> 4; s.cyc[2] = p[6] & 15; s.cyc[3] = p[6] >> 4; s.cyc[4] = p[7];
    if (!s.used) { Step z = {}; s = z; continue; }
    if (s.used != 1 || s.root > 12 || s.quality < 1 || s.quality > 2 || s.ctype > 15) return false;
    for (int k = 0; k < 5; ++k) if (s.cyc[k] < 1 || s.cyc[k] > kMax[k]) return false;
  }
  stopSounding();
  latched_ = 0; manual_ = 0; playing_ = 0; poolN_ = 0; chordDown_ = 0; heldSteps_ = 0; gestureN_ = 0; fade_ = 0; seqPulse_ = 0;
  quality_ = b[1]; ctype_ = b[2]; seqN_ = b[3]; seq_[0] = b[4] & 15; seq_[1] = b[4] >> 4; seq_[2] = b[5];
  for (int i = 0; i < 5; ++i) cyc_[i] = b[6 + i];
  start_ = b[11]; end_ = b[12]; current_ = b[13];
  for (int i = 0; i < 8; ++i) steps_[i] = st[i];
  return true;
}

// ---------------------------------------------------------------- LEDs
void HrArpSeq::paint(uint8_t lv[32], uint8_t role[32]) const {
  for (int pc = 1; pc <= 12; ++pc) {
    int i = ((pc - 1) / 6) * 8 + (pc - 1) % 6;
    lv[i] = latched_ == pc ? 15 : 4;
    role[i] = ROLE_ROOT_MAJOR;
  }
  int q = 12;
  if (fade_ > 0) q = fade_ > 8 ? (12 * (fade_ - 8)) / 8 : (12 * (8 - fade_)) / 8;
  lv[2 * 8] = (uint8_t)q; role[2 * 8] = ROLE_CONTROL;
  for (int c = 0; c < 5; ++c) { lv[2 * 8 + 1 + c] = 12; role[2 * 8 + 1 + c] = ROLE_PANEL; }
  for (int k = 1; k <= 3; ++k) {
    int pos = chordPosition(k);
    lv[(k - 1) * 8 + 6] = pos ? kChordOrderLevel[pos - 1] : 3;
    role[(k - 1) * 8 + 6] = ROLE_PANEL;
  }
  lv[2 * 8 + 7] = playing_ ? 15 : 3; role[2 * 8 + 7] = ROLE_CONTROL;
  for (int col = 1; col <= 8; ++col) {
    const Step& s = steps_[col - 1];
    bool cur = col == current_ && playing_;
    int l;
    if (s.used) l = cur ? 15 : 10;
    else if (cur) l = 4;
    else if (col >= start_ && col <= end_) l = 1;
    else l = 0;
    lv[3 * 8 + col - 1] = (uint8_t)l;
    role[3 * 8 + col - 1] = cur ? ROLE_PLAYHEAD : ROLE_STEP;
  }
}

// ---------------------------------------------------------------- Shift legend
void HrArpSeq::legend(int col, int row, HrCell& c) const {
  c.clear();
  char b[16] = {0};
  if (col >= 8) return;
  if (row < 2 && col < 6) {
    int pc = row * 6 + col + 1;
    c.set(HZ_NOTE, hrNoteName(pc - 1), latched_ == pc);
  } else if (row < 3 && col == 6) {
    static const char* const k[3] = {"Maj", "Min", "Dom7"};
    c.set(HZ_PANEL, k[row], chordPosition(row + 1) != 0);
  } else if (row == 2 && col == 0) {
    c.set(HZ_ACTION, quality_ == 1 ? "Major" : "Minor");
  } else if (row == 2 && col >= 1 && col <= 5) {
    static const char* const kDir[4] = {"Up", "Down", "UpDn", "Zigzag"};
    static const char* const kOctN[3] = {"Oct-1", "Oct 0", "Oct+1"};
    switch (col) {
      case 1: c.set(HZ_PANEL, cyc_[MODE] == 1 ? "Arp" : "Chord"); break;
      case 2: c.set(HZ_PANEL, hrRateName(kSpeeds[cyc_[SPEED] - 1])); break;
      case 3: c.set(HZ_PANEL, kDir[cyc_[DIRECTION] - 1]); break;
      case 4: snprintf(b, sizeof b, "Vel %d", cyc_[GROOVE]); c.set(HZ_PANEL, b); break;
      default: c.set(HZ_PANEL, kOctN[cyc_[OCTAVE] - 1]); break;
    }
  } else if (row == 2 && col == 7) {
    c.set(HZ_ACTION, playing_ ? "Stop" : "Play", playing_ != 0);
  } else if (row == 3) {
    snprintf(b, sizeof b, "Step%d", col + 1);
    c.set(HZ_STEP, b, col + 1 == current_ && playing_);
  }
}

void HrArpSeq::title(char* out, size_t n) const {
  static const char* const kDir[4] = {"Up", "Down", "UpDn", "Zigzag"};
  char root[12] = "no root", mode[24] = "Chord";
  if (latched_) snprintf(root, sizeof root, "%s %s", hrNoteName(latched_ - 1), quality_ == 1 ? "major" : "minor");
  if (cyc_[MODE] == 1) snprintf(mode, sizeof mode, "Arp %s %s", hrRateName(kSpeeds[cyc_[SPEED] - 1]), kDir[cyc_[DIRECTION] - 1]);
  snprintf(out, n, "ARP SEQ  %s  %s  %s  Vel %d  Oct %+d  Steps %d-%d  %s", root, ctype_ ? kTypeName[ctype_ - 1] : "triad", mode, (int)cyc_[GROOVE],
           (int)kOct[cyc_[OCTAVE] - 1], (int)start_, (int)end_, playing_ ? "Playing" : "Stopped");
}

const char* HrArpSeq::help() const {
  return "Tap a root. Tap a step to store it; hold 2 steps = loop range. Play runs the steps.";
}

}  // namespace seqgen
