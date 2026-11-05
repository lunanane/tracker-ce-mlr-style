// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Ported 1:1 from HYPER_RASP (same owner) scripts/grid_seq.lua. See hr_gridseq.h.
#include "hr_gridseq.h"

#include <stdio.h>

namespace seqgen {

namespace {
struct Grouping { uint8_t note[4]; const char* label; };
const Grouping kGroupings[8] = {
    {{36, 38, 46, 42}, "bd sd oh ch"},  {{36, 38, 39, 46}, "bd sd clap oh"}, {{36, 39, 37, 42}, "bd clap rim ch"}, {{36, 41, 43, 45}, "bd toms"},
    {{36, 38, 43, 49}, "bd sd tom cym"}, {{37, 39, 53, 54}, "rim clap cow tamb"}, {{42, 46, 53, 54}, "hats cow tamb"}, {{36, 38, 42, 49}, "bd sd ch cym"}};
const uint8_t kRatchetBySpread[5] = {6, 12, 8, 6, 4};  // RATCHET_DIVISION_BY_SPREAD, 3 for a wider spread
const int kGraceTicks = 24;                           // RATCHET_GRACE_PULSES = PULSES_PER_QUARTER

// short word of a drum note, for the legend
const char* voiceWord(int note) {
  switch (note) {
    case 36: return "BD";
    case 37: return "Rim";
    case 38: return "SD";
    case 39: return "Clap";
    case 41: return "TomL";
    case 42: return "CH";
    case 43: return "TomM";
    case 45: return "TomH";
    case 46: return "OH";
    case 49: return "Cym";
    case 53: return "Cow";
    case 54: return "Tamb";
    default: return "Drum";
  }
}
}  // namespace

HrGridSeq::HrGridSeq()
    : HrScript(HR_GRIDSEQ), grouping_(1), vel_(100), div_(6), transp_(0), playing_(0), col_(1), pulseCount_(0), start_(1), end_(kSteps) {
  for (int r = 0; r < kRows; ++r) {
    steps_[r] = 0; sounding_[r] = -1; held_[r] = 0; anchor_[r] = 0; division_[r] = 0; spread_[r] = -1;
    rowPulse_[r] = grace_[r] = ratcheted_[r] = multi_[r] = 0;
  }
  applyGrouping(1);
  for (int r = 0; r < kRows; ++r) note_[r] = voice_[r];
}

void HrGridSeq::applyGrouping(int index) {
  grouping_ = (uint8_t)(floormod(index - 1, 8) + 1);
  for (int r = 0; r < kRows; ++r) voice_[r] = kGroupings[grouping_ - 1].note[r];
}

void HrGridSeq::stopRow(int row) {
  if (sounding_[row] >= 0) {
    noteOff(sounding_[row]);
    sounding_[row] = -1;
  }
}

void HrGridSeq::stopSounding() {
  for (int r = 0; r < kRows; ++r) stopRow(r);
}

void HrGridSeq::playRowNote(int row, int col) {
  stopRow(row);
  if ((steps_[row] >> (col - 1)) & 1) {
    int note = note_[row];
    noteOn(note, vel_);
    sounding_[row] = (int16_t)(note < 0 || note > 127 ? -1 : note);  // an out-of-range note was dropped by emit: nothing to end
  }
}

void HrGridSeq::playColumn(int col) {
  for (int r = 0; r < kRows; ++r)
    if (anchor_[r] == 0) playRowNote(r, col);
}

int HrGridSeq::ratchetDivision(int spread) const {
  int reference = spread >= 0 && spread <= 4 ? kRatchetBySpread[spread] : 3;
  int scaled = (reference * div_ + 3) / 6;  // math.floor(reference * pulses_per_step / 6 + 0.5)
  return scaled < 1 ? 1 : scaled;
}

void HrGridSeq::recalcRatchet(int row) {
  bool was = anchor_[row] != 0;
  int minx = 0, maxx = 0, count = 0;
  for (int c = 1; c <= kSteps; ++c) {
    if (!((held_[row] >> (c - 1)) & 1)) continue;
    ++count;
    if (!minx || c < minx) minx = c;
    if (!maxx || c > maxx) maxx = c;
  }
  if (count == 0) {
    anchor_[row] = 0; division_[row] = 0; spread_[row] = -1; grace_[row] = 0;
    return;
  }
  anchor_[row] = (uint8_t)minx;
  spread_[row] = (int8_t)(maxx - minx);
  division_[row] = (uint8_t)ratchetDivision(spread_[row]);
  rowPulse_[row] = 0;
  if (!was) grace_[row] = kGraceTicks;
}

void HrGridSeq::startPlayback() {
  if (playing_) return;
  playing_ = 1;
  col_ = start_;
  pulseCount_ = 0;
  playColumn(col_);
}

void HrGridSeq::stopPlayback() {
  if (!playing_) return;
  playing_ = 0;
  stopSounding();
}

void HrGridSeq::pulse() {
  if (!playing_) return;
  for (int r = 0; r < kRows; ++r) {
    if (anchor_[r] == 0) continue;
    if (grace_[r] > 0) {
      --grace_[r];
      if (grace_[r] == 0) rowPulse_[r] = division_[r];
    } else {
      ++rowPulse_[r];
    }
    if (rowPulse_[r] >= division_[r]) {
      rowPulse_[r] = 0;
      playRowNote(r, anchor_[r]);
      ratcheted_[r] = 1;
    }
  }
  if (++pulseCount_ >= div_) {
    pulseCount_ = 0;
    ++col_;
    if (col_ > end_ || col_ < start_) col_ = start_;
    playColumn(col_);
  }
}

bool HrGridSeq::live() const {
  if (playing_)  // running AND at least one step set
    for (int r = 0; r < kRows; ++r) if (steps_[r]) return true;
  for (int r = 0; r < kRows; ++r) if (sounding_[r] >= 0) return true;
  return false;
}

void HrGridSeq::grid(int x, int y, int z) {
  if (x < 1 || x > kSteps || y < 1 || y > kRows) return;
  int row = y - 1;
  uint8_t bit = (uint8_t)(1 << (x - 1));
  if (z == 1) {
    if (playing_ && held_[row]) multi_[row] = 1;
    held_[row] |= bit;
    recalcRatchet(row);
    if (playing_ && anchor_[row] == x) playRowNote(row, x);
  } else {
    bool was = (held_[row] & bit) != 0;
    held_[row] &= (uint8_t)~bit;
    if (was && !ratcheted_[row] && !multi_[row]) steps_[row] ^= bit;
    recalcRatchet(row);
    if (!held_[row]) { ratcheted_[row] = 0; multi_[row] = 0; }
  }
}

// ---------------------------------------------------------------- params and the extra pads
int HrGridSeq::getParam(int id) const {
  switch (id) {
    case P_GROUPING: return grouping_;
    case P_VEL: return vel_;
    case P_TRANSP: return transp_;
    case P_DIV: return div_;
    case P_START: return start_;
    case P_END: return end_;
    case P_RANGE: return start_ * 10 + end_;
    case P_NOTE1: case P_NOTE2: case P_NOTE3: case P_NOTE4: return note_[id - P_NOTE1];
    case P_COLUMN: return col_;
    default: return 0;
  }
}

void HrGridSeq::setParam(int id, int v) {
  switch (id) {
    case P_GROUPING:
      stopSounding();
      applyGrouping(v);
      for (int r = 0; r < kRows; ++r) note_[r] = voice_[r] + transp_;
      break;
    case P_NOTE1: case P_NOTE2: case P_NOTE3: case P_NOTE4:
      note_[id - P_NOTE1] = (int16_t)((v < 24 ? 24 : v > 96 ? 96 : v) + transp_);
      break;
    case P_DIV: {
      div_ = (uint8_t)(v < 1 ? 1 : v > 24 ? 24 : v);
      for (int r = 0; r < kRows; ++r) if (spread_[r] >= 0) division_[r] = (uint8_t)ratchetDivision(spread_[r]);
      break;
    }
    case P_VEL: vel_ = (uint8_t)(v < 1 ? 1 : v > 127 ? 127 : v); break;
    case P_TRANSP: {
      int n = v < -24 ? -24 : v > 24 ? 24 : v;
      int delta = n - transp_;
      transp_ = (int8_t)n;
      for (int r = 0; r < kRows; ++r) note_[r] = (int16_t)(note_[r] + delta);
      break;
    }
    case P_RANGE: case P_START: case P_END: {  // the script's "range" (lo * 10 + hi); the extra pads set one end at a time
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

void HrGridSeq::ext(int c, int r, bool down) {
  if (!down) return;
  switch (r * 4 + c) {
    case 0: setParam(P_GROUPING, grouping_ - 1 < 1 ? 8 : grouping_ - 1); break;
    case 1: setParam(P_GROUPING, grouping_ + 1 > 8 ? 1 : grouping_ + 1); break;
    case 2: setParam(P_VEL, vel_ - 8); break;
    case 3: setParam(P_VEL, vel_ + 8); break;
    case 4: setParam(P_DIV, hrRateStep(div_, -1)); break;
    case 5: setParam(P_DIV, hrRateStep(div_, 1)); break;
    case 6: setParam(P_TRANSP, transp_ - 1); break;
    case 7: setParam(P_TRANSP, transp_ + 1); break;
    case 8: setParam(P_START, start_ - 1); break;
    case 9: setParam(P_START, start_ + 1); break;
    case 10: setParam(P_END, end_ - 1); break;
    case 11: setParam(P_END, end_ + 1); break;
    case 12: setParam(P_GROUPING, 1); break;
    case 13: setParam(P_VEL, 100); break;
    case 14: setParam(P_DIV, 6); break;
    case 15: setParam(P_TRANSP, 0); break;
    default: break;
  }
}

// ---------------------------------------------------------------- serialization (settings, the notes and the steps)
size_t HrGridSeq::serialize(uint8_t* b, size_t cap) const {
  if (cap < (size_t)kSerialSize) return 0;
  b[0] = kSerialVersion; b[1] = grouping_; b[2] = vel_; b[3] = (uint8_t)(transp_ + 24); b[4] = div_; b[5] = start_; b[6] = end_;
  for (int r = 0; r < kRows; ++r) {
    int n = note_[r];
    b[7 + r] = (uint8_t)(n < 0 ? 0 : n > 127 ? 127 : n);
    b[11 + r] = steps_[r];
  }
  return kSerialSize;
}

bool HrGridSeq::deserialize(const uint8_t* b, size_t len) {
  if (len < (size_t)kSerialSize || b[0] != kSerialVersion) return false;
  if (b[1] < 1 || b[1] > 8 || b[2] < 1 || b[2] > 127 || b[3] > 48 || b[4] < 1 || b[4] > 24 || b[5] < 1 || b[6] < b[5] || b[6] > kSteps) return false;
  for (int r = 0; r < kRows; ++r) if (b[7 + r] > 127) return false;
  stopSounding();
  playing_ = 0; pulseCount_ = 0;
  for (int r = 0; r < kRows; ++r) { held_[r] = 0; anchor_[r] = 0; division_[r] = 0; spread_[r] = -1; rowPulse_[r] = grace_[r] = ratcheted_[r] = multi_[r] = 0; }
  applyGrouping(b[1]);
  vel_ = b[2]; transp_ = (int8_t)(b[3] - 24); div_ = b[4]; start_ = b[5]; end_ = b[6]; col_ = start_;
  for (int r = 0; r < kRows; ++r) { note_[r] = b[7 + r]; steps_[r] = b[11 + r]; }
  return true;
}

// ---------------------------------------------------------------- LEDs
void HrGridSeq::paint(uint8_t lv[32], uint8_t role[32]) const {
  for (int r = 0; r < kRows; ++r) {
    for (int c = 1; c <= kSteps; ++c) {
      int level;
      if ((steps_[r] >> (c - 1)) & 1) level = 15;
      else if (anchor_[r] == c) level = 8;
      else if (playing_ && c == col_) level = 4;
      else if (c >= start_ && c <= end_) level = 1;
      else level = 0;
      lv[r * 8 + c - 1] = (uint8_t)level;
      role[r * 8 + c - 1] = playing_ && c == col_ ? ROLE_PLAYHEAD : ROLE_STEP;
    }
  }
}

void HrGridSeq::paintExt(uint8_t lv[16]) const {
  for (int i = 0; i < 16; ++i) lv[i] = 3;
}

// ---------------------------------------------------------------- Shift legend
void HrGridSeq::legend(int col, int row, HrCell& c) const {
  c.clear();
  if (col < kSteps) {
    c.set(HZ_STEP, voiceWord(note_[row] - transp_), ((steps_[row] >> col) & 1) != 0);
  } else {
    static const char* const k[16] = {"Grp-", "Grp+", "Vel-", "Vel+", "Div-", "Div+", "Tran-", "Tran+", "Lo-", "Lo+", "Hi-", "Hi+", "Grp 1", "Vel100", "1/16", "Tran 0"};
    int i = row * 4 + col - 8;
    c.set(i >= 12 ? HZ_ACTION : HZ_ADJ, k[i]);
  }
}

void HrGridSeq::title(char* out, size_t n) const {
  int k = hrAppend(out, n, 0, "GRID SEQ  %s  %s  %s  Vel %d  Steps %d-%d", playing_ ? "Playing" : "Stopped", kGroupings[grouping_ - 1].label, hrRateName(div_), (int)vel_,
                   (int)start_, (int)end_);
  if (transp_) hrAppend(out, n, k, "  Tran %+d", (int)transp_);
}

const char* HrGridSeq::help() const {
  return "Tap: step on/off. Hold: ratchet (2 cells = faster). Right: Grp Vel Div Tran Lo Hi.";
}

}  // namespace seqgen
