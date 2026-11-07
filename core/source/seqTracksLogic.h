// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Pure helper logic of the sequencer tracks glue. No Polyend includes, host-testable (core/seqtest).
#pragma once
#include <stdint.h>

namespace seqlogic {

// Types 3.. are the ported HYPER_RASP pad scripts (seqgen/hr.h, HrType order); kHrScripts must equal seqgen::HR_COUNT (static_assert in seqTracksCore.cpp).
enum { kHrScripts = 8 };
enum TrackType : uint8_t { TYPE_OFF = 0, TYPE_CHORDARP = 1, TYPE_MONO = 2, TYPE_HR_FIRST = 3,
                         TYPE_PLINKY = TYPE_HR_FIRST + kHrScripts, // plan 010 B: plays the Plinky voice from the pads (appended: old files load as before)
                         TYPE_COUNT = TYPE_PLINKY + 1 };
inline bool isHrType(uint8_t type) { return type >= TYPE_HR_FIRST && type < TYPE_PLINKY; }
enum { kTracks = 16, kPads = 48, kPadCols = 12 };

// Mode button tap (Pattern / Performance / Song): press and release with no other BUTTON in between, held for at most
// kTapMaxMs. Only buttons break a tap (plan 005 M4): the caller counts them with other(); pads and the jog wheel do not count, so
// a performer with a hand on the pads can still tap. down() is armed only when no other button was held on press and the press
// did not just close a popup. A quick press that did not become a tap is "refused" (refusedLast() after up()), so the screen can
// flash.
struct TapDetector
{
    enum { kTapMaxMs = 400 };
    bool armed = false;
    bool pressed = false;
    bool refused = false;
    uint32_t activity = 0;
    uint32_t atDown = 0;
    uint32_t t0 = 0;
    void other() { activity++; }
    void down(bool plainPress, uint32_t nowMs)
    {
        armed = plainPress;
        pressed = true;
        atDown = activity;
        t0 = nowMs;
    }
    bool up(uint32_t nowMs)
    {
        bool quick = (uint32_t)(nowMs - t0) <= (uint32_t)kTapMaxMs;
        bool tap = armed && activity == atDown && quick;
        refused = pressed && !tap && quick;
        armed = false;
        pressed = false;
        return tap;
    }
    bool refusedLast() const { return refused; } // the last up() was a quick press that is not a tap
};

// Performance key (owner 2026-11-04, plan 005 / 007 B3): the one place that decides where the key goes.
//   any other page:      plain -> fader page,            Shift -> stock Performance page
//   fader page (ours):   plain -> nothing (stays),       Shift -> stock Performance page
//   stock Performance:   plain -> stock (flips tracks),  Shift -> fader page
enum PerformRoute : uint8_t { PERFORM_STOCK_SWITCH = 0, PERFORM_TO_FADER = 1, PERFORM_TO_STOCK_PAGE = 2, PERFORM_STAY = 3, PERFORM_FLIP_TRACKS = 4 };
inline PerformRoute performKeyRoute(bool onFaderPage, bool onStockPage, bool shift)
{
    if (onFaderPage)
        return shift ? PERFORM_TO_STOCK_PAGE : PERFORM_STAY;
    if (onStockPage)
        return shift ? PERFORM_TO_FADER : PERFORM_FLIP_TRACKS;
    return shift ? PERFORM_STOCK_SWITCH : PERFORM_TO_FADER; // the stock switch of the Performance key goes to the stock page
}

// Song key toggle (owner 2026-11-04, replaces the v0.2.2 double tap): on the stock Song page a plain Song press + release opens the
// clip launcher; in the launcher Song goes back to the Song page; from any other page Song is stock. No timing window: a press is
// "plain" when no other button was held at the press and none was used before the release, and it was not held (the stock hold, a
// momentary peek, is decided by the hold event the keys driver sends, not by a time).
struct SongToggle
{
    bool armed = false;
    bool pressed = false;
    bool held = false;
    uint32_t activity = 0;
    uint32_t atDown = 0;
    void other() { activity++; }
    void down(bool plainPress)
    {
        armed = plainPress;
        pressed = true;
        held = false;
        atDown = activity;
    }
    void hold() { held = true; }
    bool up()
    {
        bool toggle = pressed && armed && !held && activity == atDown;
        armed = false;
        pressed = false;
        held = false;
        return toggle;
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
    if (type == TYPE_PLINKY) return !(col == 11 && padRow(pad) == 3); // every pad but the Setup pad
    if (type == TYPE_HR_FIRST + 4) return col < 8 && padRow(pad) < 3;  // HR Bass (seqgen::HR_BASS): the 24 scale keys (the preset row and the extras are controls); only the Latch uses this
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

// v0.3.3: the volume a live note of a sequencer gets. The recorder stores a volume only when the stock "record velocity" option is on
// (recParams().velocity); otherwise the recorded step has none (-1 = the instrument's own default). A live note must sound like the
// step it records, so without the option it gets -1 as well (it used to get the generator's velocity: full scale, "always too loud").
inline int8_t liveVolume(int8_t volume, bool recordVelocity) { return recordVelocity ? volume : (int8_t)-1; }

// Does a track take over (mute its pattern, play the generator)?
//  Chord/Arp: while a pad is held or a note still sounds (arp gaps are covered by the held pad).
//  Mono: while the transport runs and the sequence has at least one step.
inline bool wantTakeover(uint8_t type, int heldPads, int soundingNotes, bool monoHasSteps, bool playing)
{
    if (type == TYPE_CHORDARP || type == TYPE_PLINKY) return heldPads > 0 || soundingNotes > 0; // Plinky: only while a pad is held or a note sounds
    if (type == TYPE_MONO) return playing && monoHasSteps;
    return false;
}

// A HYPER_RASP script takes its track over while it is performing (latched / running, script.live()) or a note of it still sounds.
inline bool wantTakeoverHr(bool live, int soundingNotes) { return live || soundingNotes > 0; }

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


// ---- plan 025: Latch. A pad press toggles a held-key sequencer on / off instead of only sounding while the pad is held.
// ENGINE latch (the engine filters the pads): Chord/Arp, Strings (Plinky) and the HR Bass; default Off.
// SCRIPT latch (the script latches a root on a tap, Latch off = it sounds while the pad is held): HR Arp Lines, Chord Stabs, Arp Seq, Chord/Arp;
// default On (the script's own behaviour). Step sequencers (Mono, Euclid, Grid Seq, Step Follow) have no latch.
enum LatchKind : uint8_t { LATCH_NONE = 0, LATCH_ENGINE = 1, LATCH_SCRIPT = 2 };
inline uint8_t latchKind(uint8_t type)
{
    if (type == TYPE_CHORDARP || type == TYPE_PLINKY) return LATCH_ENGINE;
    if (!isHrType(type)) return LATCH_NONE;
    const uint8_t hr = (uint8_t)(type - TYPE_HR_FIRST); // seqgen::HrType order: Arp Lines, Chord Stabs, Arp Seq, Chord Arp, Bass, Euclid, Grid Seq, Step Follow
    return hr <= 3 ? LATCH_SCRIPT : hr == 4 ? LATCH_ENGINE : LATCH_NONE;
}
inline bool latchDefault(uint8_t type) { return latchKind(type) == LATCH_SCRIPT; }

// The engine latch: physical pads in, "logical" pad events out. A press that starts a new gesture (no pad is down) either switches off
// everything latched (it hits a latched pad) or replaces the latched pads by the new one; presses while a finger is already down join the
// group (a chord / several strings). Releases are never forwarded. Pure bit logic: the engine applies the result.
struct PadLatch
{
    uint64_t phys = 0;  // pads physically down
    uint64_t held = 0;  // pads the sequencer sees as pressed
    // returns the pads to release; *press = true when the pad is to be pressed
    uint64_t down(uint8_t pad, bool* press)
    {
        const uint64_t bit = (uint64_t)1 << pad;
        uint64_t rel = 0;
        *press = true;
        if (phys == 0)
        {
            rel = held;
            if (held & bit)
                *press = false; // the same pad (or one of the chord) again: off
            held = 0;
        }
        else if (held & bit)
            *press = false; // a second finger on a latched pad: nothing new
        if (*press)
            held |= bit;
        phys |= bit;
        return rel;
    }
    void up(uint8_t pad) { phys &= ~((uint64_t)1 << pad); }
    uint64_t clear()
    {
        const uint64_t r = held;
        held = 0;
        return r;
    }
};

}  // namespace seqlogic
