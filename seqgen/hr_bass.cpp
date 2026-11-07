// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Ported 1:1 from HYPER_RASP (same owner) scripts/bassline_seq.lua. See hr_bass.h.
#include "hr_bass.h"

#include <stdio.h>

namespace seqgen {

namespace {
const uint8_t kArpSq[8] = {1, 3, 2, 4, 3, 5, 4, 6};  // ARP_SEQUENCE
const char* const kDirName[4] = {"UP", "DOWN", "UP/DN", "PATT"};
}  // namespace

HrBass::HrBass()
    : HrScript(HR_BASS), octave_(0), dir_(1), groove_(1), heldN_(0), playing_(0), pulseCount_(0), livePreset_(0), stepTick_(0), storedChains_(1),
      recording_(0), recChain_(0), recStep_(0), sounding_(-1), arpStep_(0), phase_(1) {
  for (int i = 0; i < kKeys; ++i) held_[i] = 0;
  for (unsigned i = 0; i < sizeof stored_; ++i) stored_[i] = 0;
}

// context(): the shared scale (none = chromatic) and root (default 60); note_for_key(index)
int HrBass::noteForKey(int index) const {
  const HrBus& b = bus();
  int root = b.root >= 0 ? b.root : 60;
  int note;
  uint8_t sc[12];
  const int n = busScale(sc);
  if (n == 0) {
    note = root + index + 12 * octave_;
  } else {
    int interval = floormod(sc[index % n] - sc[0], 12);
    int base = root - floormod(root - sc[0], 12);
    note = base + 12 * (index / n) + interval + 12 * octave_;
  }
  return note < 0 || note > 127 ? -1 : note;
}

bool HrBass::isRootKey(int index) const {
  uint8_t sc[12];
  const int n = busScale(sc);
  return n == 0 ? index % 12 == 0 : index % n == 0;
}

bool HrBass::isHeld(int index) const {
  for (int i = 0; i < heldN_; ++i) if (held_[i] == index) return true;
  return false;
}

int HrBass::buildPool(int16_t* pool) const {
  int n = 0;
  for (int i = 0; i < heldN_; ++i) {
    int note = noteForKey(held_[i]);
    if (note >= 0) pool[n++] = (int16_t)note;
  }
  return n;
}

bool HrBass::onset(int idx) const {
  if (livePreset_) {
    uint8_t e[8];
    hrEuclid(livePreset_, 8, e);
    return e[idx - 1] != 0;
  }
  int i = idx - 1;
  return (stored_[i >> 3] >> (i & 7)) & 1;
}

void HrBass::stopSounding() {
  if (sounding_ >= 0) {
    noteOff(sounding_);
    sounding_ = -1;
  }
}

void HrBass::playStep() {
  stopSounding();
  if (!onset(stepIndex())) return;
  ++arpStep_;
  int16_t pool[kKeys];
  int n = buildPool(pool);
  if (n == 0) return;
  int index;
  if (dir_ == 2) {
    index = n - floormod(arpStep_ - 1, n);
  } else if (dir_ == 3) {
    int cycle = n * 2, ph = floormod(arpStep_ - 1, cycle);
    index = ph < n ? ph + 1 : cycle - ph;
  } else if (dir_ == 4) {
    index = floormod(kArpSq[floormod(arpStep_ - 1, 8)] - 1, n) + 1;
  } else {
    index = floormod(arpStep_ - 1, n) + 1;
  }
  int note = pool[index - 1];
  noteOn(note, hrGrooveVel(groove_, phase_, 100));
  sounding_ = (int16_t)note;
}

void HrBass::advanceRecording() {
  if (!recording_ || recChain_ > kMaxChains) return;
  ++recStep_;
  if (recStep_ > kChain) {
    recStep_ = 1;
    ++recChain_;
    if (recChain_ > kMaxChains) return;
  }
  bool on = false;
  if (livePreset_) {
    uint8_t e[8];
    hrEuclid(livePreset_, 8, e);
    on = e[recStep_ - 1] != 0;
  }
  int i = (recChain_ - 1) * kChain + recStep_ - 1;
  if (on) stored_[i >> 3] |= (uint8_t)(1 << (i & 7));
  else stored_[i >> 3] &= (uint8_t)~(1 << (i & 7));
  storedChains_ = recChain_;
}

void HrBass::startPlayback() {
  if (playing_) return;
  playing_ = 1;
  pulseCount_ = 0;
  stepTick_ = 0;
  playStep();
}

void HrBass::stopPlayback() {
  if (!playing_) return;
  playing_ = 0;
  stopSounding();
  livePreset_ = 0;
  recording_ = 0;
}

void HrBass::pulse() {
  phase_ = (uint8_t)(phase_ % 16 + 1);
  if (!playing_) return;
  if (++pulseCount_ >= 6) {
    pulseCount_ = 0;
    ++stepTick_;
    advanceRecording();
    playStep();
  }
}

void HrBass::grid(int x, int y, int z) {
  if (x < 1 || x > kHrCols || y < 1 || y > kHrRows) return;
  if (y <= 3) {
    int index = (3 - y) * 8 + (x - 1);
    if (z == 1) {
      if (!isHeld(index) && heldN_ < kKeys) held_[heldN_++] = (uint8_t)index;
    } else {
      for (int i = 0; i < heldN_; ++i)
        if (held_[i] == index) {
          for (int k = i; k + 1 < heldN_; ++k) held_[k] = held_[k + 1];
          --heldN_;
          break;
        }
      if (heldN_ == 0) stopSounding();
    }
    return;
  }
  if (x == 8) {
    if (z == 1) {
      recording_ = 1;
      recChain_ = 1;
      recStep_ = 0;
    } else {
      recording_ = 0;
    }
    return;
  }
  if (z == 1 && x <= 7) livePreset_ = (uint8_t)x;
}

// ---------------------------------------------------------------- params and the extra pads
int HrBass::getParam(int id) const {
  switch (id) {
    case P_OCTAVE: return octave_;
    case P_DIR: return dir_;
    case P_GROOVE: return groove_;
    case P_PRESET: return livePreset_;
    case P_CHAINS: return storedChains_;
    default: return 0;
  }
}

void HrBass::setParam(int id, int v) {
  switch (id) {
    case P_OCTAVE: octave_ = (int8_t)(v < -2 ? -2 : v > 2 ? 2 : v); break;
    case P_DIR: dir_ = (uint8_t)(v < 1 ? 1 : v > 4 ? 4 : v); break;
    case P_GROOVE: groove_ = (uint8_t)(v < 1 ? 1 : v > 12 ? 12 : v); break;
    default: break;
  }
}

void HrBass::ext(int c, int r, bool down) {
  if (!down) return;
  switch (r * 4 + c) {
    case 0: setParam(P_OCTAVE, octave_ - 1); break;
    case 1: setParam(P_OCTAVE, octave_ + 1); break;
    case 2: setParam(P_DIR, dir_ - 1); break;
    case 3: setParam(P_DIR, dir_ + 1); break;
    case 4: setParam(P_GROOVE, groove_ - 1); break;
    case 5: setParam(P_GROOVE, groove_ + 1); break;
    case 12: setParam(P_OCTAVE, 0); break;
    case 13: setParam(P_DIR, 1); break;
    case 14: setParam(P_GROOVE, 1); break;
    default: break;
  }
}

// ---------------------------------------------------------------- serialization (settings and the stored pattern; the live preset, recording, held keys are performance state)
size_t HrBass::serialize(uint8_t* b, size_t cap) const {
  if (cap < (size_t)kSerialSize) return 0;
  b[0] = kSerialVersion; b[1] = (uint8_t)(octave_ + 2); b[2] = dir_; b[3] = groove_; b[4] = storedChains_;
  for (int i = 0; i < 8; ++i) b[5 + i] = stored_[i];
  return kSerialSize;
}

bool HrBass::deserialize(const uint8_t* b, size_t len) {
  if (len < (size_t)kSerialSize || b[0] != kSerialVersion) return false;
  if (b[1] > 4 || b[2] < 1 || b[2] > 4 || b[3] < 1 || b[3] > 12 || b[4] < 1 || b[4] > kMaxChains) return false;
  stopSounding();
  heldN_ = 0; playing_ = 0; pulseCount_ = 0; livePreset_ = 0; recording_ = 0; stepTick_ = 0;
  octave_ = (int8_t)(b[1] - 2); dir_ = b[2]; groove_ = b[3]; storedChains_ = b[4];
  for (int i = 0; i < 8; ++i) stored_[i] = b[5 + i];
  return true;
}

// ---------------------------------------------------------------- LEDs
int HrBass::bgLevel() const {
  int index = recording_ ? recChain_ : storedChains_;
  if (index > kMaxChains) index = kMaxChains;
  return (index * 15 * 2 + kMaxChains) / (kMaxChains * 2);  // math.floor(index / 8 * 15 + 0.5)
}

void HrBass::paint(uint8_t lv[32], uint8_t role[32]) const {
  for (int y = 1; y <= 3; ++y) {
    for (int x = 1; x <= 8; ++x) {
      int index = (3 - y) * 8 + (x - 1);
      bool root = isRootKey(index);
      int level = noteForKey(index) < 0 ? 0 : isHeld(index) ? 15 : root ? 9 : 6;
      lv[(y - 1) * 8 + x - 1] = (uint8_t)level;
      role[(y - 1) * 8 + x - 1] = root ? ROLE_ROOT_MAJOR : ROLE_DEGREE;
    }
  }
  int idx = playing_ ? stepIndex() : 0;
  int bg = bgLevel();
  for (int col = 1; col <= 7; ++col) {
    int level;
    if (col == livePreset_) level = playing_ ? (onset(idx) ? 15 : 6) : 10;
    else level = 1;
    lv[24 + col - 1] = (uint8_t)(level ? level : bg);  // grid_led_bg shows where the foreground is dark
    role[24 + col - 1] = ROLE_DEGREE;
  }
  lv[24 + 7] = recording_ ? 15 : 4;
  role[24 + 7] = ROLE_CONTROL;
}

void HrBass::paintExt(uint8_t lv[16]) const {
  for (int i = 0; i < 16; ++i) lv[i] = 0;
  lv[0] = lv[1] = lv[2] = lv[3] = lv[4] = lv[5] = 3;
  lv[12] = lv[13] = lv[14] = 3;
}

// ---------------------------------------------------------------- Shift legend
void HrBass::legend(int col, int row, HrCell& c) const {
  c.clear();
  char b[24] = {0};
  if (col < 8 && row < 3) {
    int index = (2 - row) * 8 + col;
    int note = noteForKey(index);
    if (note >= 0) c.set(HZ_NOTE, hrNoteName(note), isHeld(index));
  } else if (col < 7 && row == 3) {
    snprintf(b, sizeof b, "E(%d,8)", col + 1);
    c.set(HZ_PANEL, b, col + 1 == livePreset_);
  } else if (col == 7 && row == 3) {
    c.set(HZ_ACTION, "Rec", recording_ != 0);
  } else if (col >= 8) {
    static const char* const k[16] = {"Oct-", "Oct+", "Dir-", "Dir+", "Grv-", "Grv+", "", "", "", "", "", "", "Oct 0", "Dir UP", "Grv 1", ""};
    int i = row * 4 + col - 8;
    if (k[i][0]) c.set(i >= 12 ? HZ_ACTION : HZ_ADJ, k[i]);
  }
}

void HrBass::title(char* out, size_t n) const {
  int k = hrAppend(out, n, 0, "BASS SEQ  %s", playing_ ? "Playing" : "Stopped");
  if (livePreset_) k = hrAppend(out, n, k, "  E(%d,8)", (int)livePreset_);
  else k = hrAppend(out, n, k, "  Stored %d", (int)storedChains_ * kChain);
  k = hrAppend(out, n, k, "  %s  Groove %d  Oct %+d", kDirName[dir_ - 1], (int)groove_, (int)octave_);
  if (recording_) hrAppend(out, n, k, "  REC");
}

const char* HrBass::help() const {
  return "Hold keys: notes in press order. Pick E(k,8) for the rhythm. Hold Rec to store it.";
}

}  // namespace seqgen
