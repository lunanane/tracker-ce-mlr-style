// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Pure helper logic of the sequencer tracks glue. No Polyend includes, host-testable (core/seqtest).
#pragma once
#include <stdint.h>

namespace seqlogic {

enum TrackType : uint8_t { TYPE_OFF = 0, TYPE_CHORDARP = 1, TYPE_MONO = 2, TYPE_COUNT = 3 };
enum { kTracks = 16, kPads = 48, kPadCols = 12 };

// Pattern button tap: press and release with no other input in between (buttons, pad presses, jog wheel).
// The caller counts other input with other(); down() is armed only when no other button was held on press.
struct TapDetector
{
    bool armed = false;
    uint32_t activity = 0;
    uint32_t atDown = 0;
    void other() { activity++; }
    void down(bool nothingElseHeld)
    {
        armed = nothingElseHeld;
        atDown = activity;
    }
    bool up()
    {
        bool tap = armed && activity == atDown;
        armed = false;
        return tap;
    }
};

inline uint8_t padCol(uint8_t pad) { return (uint8_t)(pad % kPadCols); }
inline uint8_t padRow(uint8_t pad) { return (uint8_t)(pad / kPadCols); }

// Screen button 0..7 -> track 0..7, with Shift 8..15.
inline uint8_t trackFromScreenButton(uint8_t button, bool shift) { return (uint8_t)((button & 7) + (shift ? 8 : 0)); }

// Instr + jog wheel: move the highlighted type by one per detent (no wrap).
inline uint8_t stepTypeSelection(uint8_t cur, int delta)
{
    int v = (int)cur + (delta > 0 ? 1 : (delta < 0 ? -1 : 0));
    if (v < 0) v = 0;
    if (v > TYPE_COUNT - 1) v = TYPE_COUNT - 1;
    return (uint8_t)v;
}

// seqgen velocity 0..127 -> tracker volume 0..100.
inline int8_t velocityToVolume(uint8_t vel)
{
    if (vel > 127) vel = 127;
    return (int8_t)((vel * 100 + 63) / 127);
}

// Does a track take over (mute its pattern, play the generator)?
//  Chord/Arp: while a pad is held or a note still sounds (arp gaps are covered by the held pad).
//  Mono: while the transport runs and the sequence has at least one step.
inline bool wantTakeover(uint8_t type, int heldPads, int soundingNotes, bool monoHasSteps, bool playing)
{
    if (type == TYPE_CHORDARP) return heldPads > 0 || soundingNotes > 0;
    if (type == TYPE_MONO) return playing && monoHasSteps;
    return false;
}

// Pending type changes apply on the first pulse of a bar (96 pulses per bar).
inline bool isBarStart(uint32_t pulseInBar) { return pulseInBar == 0; }

}  // namespace seqlogic
