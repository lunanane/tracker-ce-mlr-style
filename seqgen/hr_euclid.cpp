// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Ported 1:1 from HYPER_RASP (same owner) scripts/euclid_trigger.lua. See hr_euclid.h.
#include "hr_euclid.h"

#include <stdio.h>

namespace seqgen {

namespace {
// DRUM_GROUPINGS: the MIDI note of each row (DRUM_NOTE) and the label
struct Grouping { uint8_t note[4]; const char* label; };
const Grouping kGroupings[8] = {
    {{36, 38, 46, 42}, "bd sd oh ch"},  {{36, 38, 39, 46}, "bd sd clap oh"}, {{36, 39, 37, 42}, "bd clap rim ch"}, {{36, 41, 43, 45}, "bd toms"},
    {{36, 38, 43, 49}, "bd sd tom cym"}, {{37, 39, 53, 54}, "rim clap cow tamb"}, {{42, 46, 53, 54}, "hats cow tamb"}, {{36, 38, 42, 49}, "bd sd ch cym"}};
}  // namespace

HrEuclid::HrEuclid() : HrScript(HR_EUCLID), grouping_(1), vel_(100), swing_(0), chance_(100), playing_(0), pulseCount_(0), stepTick_(0), rng_(1) {
  for (int r = 0; r < kRows; ++r) {
    live_[r] = off_[r] = 0;
    chains_[r] = 1;
    recording_[r] = recChain_[r] = recStep_[r] = 0;
    sounding_[r] = -1;
    for (unsigned i = 0; i < sizeof stored_[r]; ++i) stored_[r][i] = 0;
  }
  applyGrouping(1);
}

void HrEuclid::applyGrouping(int index) {
  grouping_ = (uint8_t)(floormod(index - 1, 8) + 1);
  for (int r = 0; r < kRows; ++r) notes_[r] = kGroupings[grouping_ - 1].note[r];
}

bool HrEuclid::onset(int row, int idx) const {
  if (live_[row]) {
    uint8_t e[8];
    hrEuclid(live_[row], 8, e);
    return e[idx - 1] != 0;
  }
  int i = idx - 1;
  return (stored_[row][i >> 3] >> (i & 7)) & 1;
}

void HrEuclid::stopRow(int row) {
  if (sounding_[row] >= 0) {
    noteOff(sounding_[row]);
    sounding_[row] = -1;
  }
}

void HrEuclid::stopAll() {
  for (int r = 0; r < kRows; ++r) stopRow(r);
}

bool HrEuclid::rollsUnder(int percent) {
  if (percent >= 100) return true;
  if (percent <= 0) return false;
  rng_ = rng_ * 1664525u + 1013904223u;  // the stand-in of math.random(100)
  return (int)((rng_ >> 16) % 100u) + 1 <= percent;
}

void HrEuclid::playRow(int row) {
  stopRow(row);
  if (off_[row]) return;
  if (onset(row, stepIndex(row))) {
    if (!rollsUnder(chance_)) return;
    int note = notes_[row];
    noteOn(note, vel_);
    sounding_[row] = (int16_t)note;
  }
}

void HrEuclid::advanceRecording(int row) {
  if (!recording_[row] || recChain_[row] > kMaxChains) return;
  ++recStep_[row];
  if (recStep_[row] > kChain) {
    recStep_[row] = 1;
    ++recChain_[row];
    if (recChain_[row] > kMaxChains) return;
  }
  bool on = false;
  if (live_[row]) {
    uint8_t e[8];
    hrEuclid(live_[row], 8, e);
    on = e[recStep_[row] - 1] != 0;
  }
  int i = (recChain_[row] - 1) * kChain + recStep_[row] - 1;
  if (on) stored_[row][i >> 3] |= (uint8_t)(1 << (i & 7));
  else stored_[row][i >> 3] &= (uint8_t)~(1 << (i & 7));
  chains_[row] = recChain_[row];
}

void HrEuclid::startPlayback() {
  if (playing_) return;
  playing_ = 1;
  pulseCount_ = 0;
  stepTick_ = 0;
  for (int r = 0; r < kRows; ++r) playRow(r);
}

void HrEuclid::stopPlayback() {
  if (!playing_) return;
  playing_ = 0;
  stopAll();
  for (int r = 0; r < kRows; ++r) { live_[r] = 0; off_[r] = 0; recording_[r] = 0; }
}

void HrEuclid::pulse() {
  if (!playing_) return;
  if (++pulseCount_ >= 6) {
    pulseCount_ = 0;
    ++stepTick_;
    for (int r = 0; r < kRows; ++r) advanceRecording(r);
    for (int r = 0; r < kRows; ++r) playRow(r);
  }
}

bool HrEuclid::live() const {
  if (playing_)  // running AND something to play (a live preset or a stored chain with an onset on a row that is on)
    for (int r = 0; r < kRows; ++r) {
      if (off_[r]) continue;
      if (live_[r]) return true;
      for (int i = 0; i < chains_[r] * kChain; ++i)
        if (stored_[r][i >> 3] & (1 << (i & 7))) return true;
    }
  for (int r = 0; r < kRows; ++r) if (sounding_[r] >= 0) return true;
  return false;
}

void HrEuclid::grid(int x, int y, int z) {
  if (x < 1 || x > kHrCols || y < 1 || y > kRows) return;
  int row = y - 1;
  if (x == 8) {
    if (z == 1) {
      recording_[row] = 1;
      recChain_[row] = 1;
      recStep_[row] = 0;
    } else {
      recording_[row] = 0;
    }
    return;
  }
  if (z == 1 && x <= 7) {
    if (live_[row] == x && !off_[row]) {
      off_[row] = 1;
      stopRow(row);
    } else {
      live_[row] = (uint8_t)x;
      off_[row] = 0;
    }
  }
}

// ---------------------------------------------------------------- params and the extra pads
int HrEuclid::getParam(int id) const {
  switch (id) {
    case P_GROUPING: return grouping_;
    case P_VEL: return vel_;
    case P_SWING: return swing_;
    case P_CHANCE: return chance_;
    case P_NOTE1: case P_NOTE2: case P_NOTE3: case P_NOTE4: return notes_[id - P_NOTE1];
    default:
      if (id >= P_PRESET1 && id < P_PRESET1 + kRows) return live_[id - P_PRESET1];
      return 0;
  }
}

void HrEuclid::setParam(int id, int v) {
  switch (id) {
    case P_GROUPING:
      stopAll();
      applyGrouping(v);
      break;
    case P_VEL: vel_ = (uint8_t)(v < 1 ? 1 : v > 127 ? 127 : v); break;
    case P_SWING: swing_ = (uint8_t)(v < 0 ? 0 : v > 60 ? 60 : v); break;  // kept, as in the script (swing is not applied there either)
    case P_CHANCE: chance_ = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v); break;
    case P_NOTE1: case P_NOTE2: case P_NOTE3: case P_NOTE4:
      stopRow(id - P_NOTE1);
      notes_[id - P_NOTE1] = (uint8_t)(v < 24 ? 24 : v > 96 ? 96 : v);
      break;
    default: break;
  }
}

void HrEuclid::ext(int c, int r, bool down) {
  if (!down) return;
  switch (r * 4 + c) {
    case 0: setParam(P_GROUPING, grouping_ - 1 < 1 ? 8 : grouping_ - 1); break;
    case 1: setParam(P_GROUPING, grouping_ + 1 > 8 ? 1 : grouping_ + 1); break;
    case 2: setParam(P_VEL, vel_ - 8); break;
    case 3: setParam(P_VEL, vel_ + 8); break;
    case 4: setParam(P_CHANCE, chance_ - 10); break;
    case 5: setParam(P_CHANCE, chance_ + 10); break;
    case 6: setParam(P_SWING, swing_ - 5); break;
    case 7: setParam(P_SWING, swing_ + 5); break;
    case 12: setParam(P_GROUPING, 1); break;
    case 13: setParam(P_VEL, 100); break;
    case 14: setParam(P_CHANCE, 100); break;
    case 15: setParam(P_SWING, 0); break;
    default: break;
  }
}

// ---------------------------------------------------------------- serialization (settings and the stored patterns; live presets, recording are performance state)
size_t HrEuclid::serialize(uint8_t* b, size_t cap) const {
  if (cap < (size_t)kSerialSize) return 0;
  b[0] = kSerialVersion; b[1] = grouping_; b[2] = vel_; b[3] = swing_; b[4] = chance_;
  for (int r = 0; r < kRows; ++r) {
    b[5 + r] = notes_[r];
    b[9 + r] = chains_[r];
    for (int i = 0; i < 8; ++i) b[13 + r * 8 + i] = stored_[r][i];
  }
  return kSerialSize;
}

bool HrEuclid::deserialize(const uint8_t* b, size_t len) {
  if (len < (size_t)kSerialSize || b[0] != kSerialVersion) return false;
  if (b[1] < 1 || b[1] > 8 || b[2] < 1 || b[2] > 127 || b[3] > 60 || b[4] > 100) return false;
  for (int r = 0; r < kRows; ++r) if (b[5 + r] < 24 || b[5 + r] > 96 || b[9 + r] < 1 || b[9 + r] > kMaxChains) return false;
  stopAll();
  playing_ = 0; pulseCount_ = 0; stepTick_ = 0;
  grouping_ = b[1]; vel_ = b[2]; swing_ = b[3]; chance_ = b[4];
  for (int r = 0; r < kRows; ++r) {
    live_[r] = off_[r] = recording_[r] = 0;
    notes_[r] = b[5 + r]; chains_[r] = b[9 + r];
    for (int i = 0; i < 8; ++i) stored_[r][i] = b[13 + r * 8 + i];
  }
  return true;
}

// ---------------------------------------------------------------- LEDs
int HrEuclid::bgLevel(int row) const {
  int index = recording_[row] ? recChain_[row] : chains_[row];
  if (index > kMaxChains) index = kMaxChains;
  return (index * 15 * 2 + kMaxChains) / (kMaxChains * 2);
}

void HrEuclid::paint(uint8_t lv[32], uint8_t role[32]) const {
  for (int row = 0; row < kRows; ++row) {
    int idx = playing_ ? stepIndex(row) : 0;
    int bg = bgLevel(row);
    for (int col = 1; col <= 7; ++col) {
      int level;
      if (col == live_[row] && off_[row]) level = 3;
      else if (col == live_[row]) level = playing_ ? (onset(row, idx) ? 15 : 6) : 10;
      else level = 1;
      lv[row * 8 + col - 1] = (uint8_t)(level ? level : bg);  // grid_led_bg shows where the foreground is dark (never, here: idle is 1)
      role[row * 8 + col - 1] = ROLE_DEGREE;
    }
    lv[row * 8 + 7] = recording_[row] ? 15 : 4;
    role[row * 8 + 7] = ROLE_CONTROL;
  }
}

void HrEuclid::paintExt(uint8_t lv[16]) const {
  for (int i = 0; i < 16; ++i) lv[i] = 0;
  for (int i = 0; i < 8; ++i) lv[i] = 3;
  for (int i = 12; i < 16; ++i) lv[i] = 3;
}

// ---------------------------------------------------------------- Shift legend
void HrEuclid::legend(int col, int row, HrCell& c) const {
  c.clear();
  char b[24] = {0};
  if (col < 7) {
    snprintf(b, sizeof b, "E(%d,8)", col + 1);
    c.set(HZ_PANEL, b, live_[row] == col + 1 && !off_[row]);
  } else if (col == 7) {
    c.set(HZ_ACTION, "Rec", recording_[row] != 0);
  } else {
    static const char* const k[16] = {"Grp-", "Grp+", "Vel-", "Vel+", "Cha-", "Cha+", "Swg-", "Swg+", "", "", "", "", "Grp 1", "Vel100", "Cha100", "Swg 0"};
    int i = row * 4 + col - 8;
    if (k[i][0]) c.set(i >= 12 ? HZ_ACTION : HZ_ADJ, k[i]);
  }
}

void HrEuclid::title(char* out, size_t n) const {
  int k = hrAppend(out, n, 0, "EUCLID  %s  %s  Vel %d", playing_ ? "Playing" : "Stopped", kGroupings[grouping_ - 1].label, (int)vel_);
  if (chance_ < 100) k = hrAppend(out, n, k, "  Chance %d%%", (int)chance_);
  int rec = 0;
  for (int r = 0; r < kRows; ++r) rec |= recording_[r];
  if (rec) hrAppend(out, n, k, "  REC");
}

const char* HrEuclid::help() const {
  return "Each row is a drum: tap E(k,8) for its rhythm, again to mute. Hold Rec to store it.";
}

}  // namespace seqgen
