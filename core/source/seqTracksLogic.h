// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Pure helper logic of the sequencer tracks glue. No Polyend includes, host-testable (core/seqtest).
#pragma once
#include <stdint.h>

namespace seqlogic {

enum TrackType : uint8_t { TYPE_OFF = 0, TYPE_CHORDARP = 1, TYPE_MONO = 2, TYPE_COUNT = 3 };
enum { kTracks = 16, kPads = 48, kPadCols = 12 };

// Pattern button tap: press and release with no other input in between (buttons, pad presses, jog wheel), held for at most
// kTapMaxMs. The caller counts other input with other(); down() is armed only when no other button was held on press and the
// press did not just close a popup.
struct TapDetector
{
    enum { kTapMaxMs = 400 };
    bool armed = false;
    uint32_t activity = 0;
    uint32_t atDown = 0;
    uint32_t t0 = 0;
    void other() { activity++; }
    void down(bool plainPress, uint32_t nowMs)
    {
        armed = plainPress;
        atDown = activity;
        t0 = nowMs;
    }
    bool up(uint32_t nowMs)
    {
        bool tap = armed && activity == atDown && (uint32_t)(nowMs - t0) <= (uint32_t)kTapMaxMs;
        armed = false;
        return tap;
    }
};

inline uint8_t padCol(uint8_t pad) { return (uint8_t)(pad % kPadCols); }
inline uint8_t padRow(uint8_t pad) { return (uint8_t)(pad / kPadCols); }

// Pads that count as "played" for a takeover: the note grid only. Chord/Arp: the root grid (columns 0..5). Mono: the step grid
// (columns 0..7). Panel / control pads (types, settings, loop, page ...) never start or keep a takeover.
inline bool isNotePad(uint8_t type, uint8_t pad)
{
    uint8_t col = padCol(pad);
    if (type == TYPE_CHORDARP) return col <= 5;
    if (type == TYPE_MONO) return col < 8;
    return false;
}

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

// Pad releases that could not be posted (command queue full) are kept here and retried until they are accepted: a lost
// release would leave a chord / takeover on forever. Presses may be dropped.
struct PendingReleases
{
    uint64_t bits[kTracks];
    PendingReleases() { clear(); }
    void clear()
    {
        for (int i = 0; i < kTracks; i++) bits[i] = 0;
    }
    void add(uint8_t t, uint8_t pad) { if (t < kTracks && pad < kPads) bits[t] |= (uint64_t)1 << pad; }
    void remove(uint8_t t, uint8_t pad) { if (t < kTracks && pad < kPads) bits[t] &= ~((uint64_t)1 << pad); }
    bool has(uint8_t t, uint8_t pad) const { return t < kTracks && pad < kPads && ((bits[t] >> pad) & 1u); }
    bool any() const
    {
        for (int i = 0; i < kTracks; i++) if (bits[i]) return true;
        return false;
    }
    // post(track, pad) -> true when accepted (the release is then forgotten)
    template <class F> void retry(F post)
    {
        for (uint8_t t = 0; t < kTracks; t++)
            for (uint8_t p = 0; bits[t] && p < kPads; p++)
                if (((bits[t] >> p) & 1u) && post(t, p))
                    bits[t] &= ~((uint64_t)1 << p);
    }
};

// What Save / Save As does with <project>/seqtracks.bin.
//  WRITE:  the project used a sequencer -> write the current state.
//  KEEP:   never used, and this is the project the sidecar was loaded from (it may be an unknown / newer file): leave it.
//  REMOVE: never used and it is another project (Save as over an existing folder): a stale file of the overwritten project.
enum SidecarAction : uint8_t { SIDE_WRITE = 0, SIDE_KEEP = 1, SIDE_REMOVE = 2 };
inline SidecarAction sidecarAction(bool used, bool sameProjectAsLoaded)
{
    if (used) return SIDE_WRITE;
    return sameProjectAsLoaded ? SIDE_KEEP : SIDE_REMOVE;
}

// Identity of a project folder (FNV-1a over path, '/', name).
inline uint32_t projectId(const char* path, const char* name)
{
    uint32_t h = 2166136261u;
    for (const char* p = path; p && *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    h = (h ^ (uint8_t)'/') * 16777619u;
    for (const char* p = name; p && *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    return h;
}

}  // namespace seqlogic
