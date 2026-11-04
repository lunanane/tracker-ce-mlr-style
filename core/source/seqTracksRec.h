// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Record planner of the sequencer tracks (plans/001-sequencer-tracks.md, "Workflow: Play mode").
// Pure logic, no Polyend types: runs in the audio context, turns note events into step operations
// ("replace" semantics) that the main loop applies to the pattern. Host-tested in core/seqtest.
#pragma once
#include <stdint.h>
#include <string.h>

#include "mtChordDefs.h" // chordDef[], MAX_CHORD (29 interval bitmasks, bit n = n semitones above the bass note)

namespace seqrec {

enum OpKind : uint8_t
{
    OP_CLEAR = 0, // whole step cleared (a step passed without a sequencer note)
    OP_NOTE = 1,  // whole step replaced by a note (+ optional chord FX, volume, micromove)
    OP_OFF = 2,   // whole step replaced by a note-off
    OP_OFF_SOFT = 3 // note-off only if the step in the pattern holds no note (end of a recording)
};
enum { kNoChord = 0xFF, kNoMicro = -1, kNoVolume = -1, kMaxGroup = 8, kMaxSteps = 128 };

// Hints for the screen (sticky until the next recording starts)
enum Hint : uint8_t
{
    HINT_REDUCED = 1,   // a chord did not fit the Tracker chord table: highest notes dropped
    HINT_NEED_MIDI = 2, // chord on a sample track: only the lowest note is written
    HINT_OVERFLOW = 4   // the audio -> main queue was full: some steps of this recording were not written
};

struct Op
{
    uint8_t kind;
    uint8_t track;
    uint8_t step;
    uint8_t note;
    uint8_t instrument;
    uint8_t chord; // kNoChord or chordDef index 1..MAX_CHORD
    int8_t volume; // -1 = default
    int8_t micromove; // -1 = none
};

// Position of the recorded track's playhead (read in the audio context).
struct TrackPos
{
    uint8_t pos;   // current step
    uint8_t uStep; // 1..48 inside the step
    uint8_t last;  // last step index of the track's pattern (length - 1)
};

struct Params
{
    bool quantize; // nearest step (stock "rec microtiming" off); false: current step + micromove
    bool velocity; // write velocity as volume
};

// Chord -> Tracker chord FX. notes ascending, distinct, n >= 1.
// Returns the number of notes represented (the highest are dropped until the table has an exact match).
// idx = chordDef index (1..MAX_CHORD) or kNoChord when only the lowest note is left.
inline int chordLookup(const uint8_t* notes, int n, uint8_t& idx)
{
    idx = kNoChord;
    for (int k = n; k >= 2; --k)
    {
        uint16_t mask = 0;
        bool ok = true;
        for (int i = 0; i < k; ++i)
        {
            int d = (int)notes[i] - (int)notes[0];
            if (d > 14)
            {
                ok = false;
                break;
            }
            mask = (uint16_t)(mask | (1u << d));
        }
        if (!ok)
            continue;
        for (int c = 1; c <= MAX_CHORD; ++c)
            if (chordDef[c] == mask)
            {
                idx = (uint8_t)c;
                return k;
            }
    }
    return 1;
}

class Planner
{
  public:
    typedef void (*Sink)(void* user, const Op& op);
    enum State : uint8_t { IDLE = 0, ARMED = 1, PUNCH = 2, LOOP = 3 };

    Planner() : sink_(0), user_(0), ends_(0) { reset(); }
    void setSink(Sink f, void* user)
    {
        sink_ = f;
        user_ = user;
    }

    State state() const { return state_; }
    bool active() const { return state_ != IDLE; }
    uint8_t track() const { return track_; }
    uint8_t hints() const { return hints_; }
    uint8_t ends() const { return ends_; } // counts finished sessions (wraps)

    // Punch-in: records from now on. One loop: armed until the track's playhead is at step 0.
    // fromStart: the transport starts with this recording (step `curPos` has not been entered yet): that step belongs to the
    // pass and is cleared like any other; otherwise the step that is already playing is not cleared.
    void begin(uint8_t track, bool oneLoop, uint8_t curPos, bool fromStart = false)
    {
        reset();
        track_ = track;
        hints_ = 0;
        if (oneLoop)
        {
            state_ = ARMED;
        }
        else
        {
            state_ = PUNCH;
            if (!fromStart)
            {
                entered_ = curPos; // the step that is already playing is not cleared, notes may still replace it
                haveEntered_ = true;
            }
        }
    }

    void stop()
    {
        if (state_ == IDLE)
            return;
        flush();
        if (open_)
        {
            // the last note is still sounding: close it on the next step without a note (stock rule), so it
            // does not ring into the old pattern content
            unsigned span = span_ ? span_ : (unsigned)kMaxSteps;
            unsigned d = haveEntered_ ? entered_ : 0u;
            if (d == lastOn_)
                d = (d + 1u) % span;
            if (d < (unsigned)kMaxSteps && stateOf((uint8_t)d) == 0)
                emit(OP_OFF_SOFT, (uint8_t)d, 0, 0, kNoChord, kNoVolume, kNoMicro);
            open_ = false;
        }
        pending_ = 0;
        state_ = IDLE;
        ends_++;
    }

    // The playhead enters step `pos` (call when the track's uStep is 1).
    void enter(uint8_t pos)
    {
        if (state_ == IDLE || pos >= kMaxSteps)
            return;
        if (state_ == ARMED)
        {
            if (pos != 0)
                return;
            state_ = LOOP;
        }
        else if (state_ == LOOP && pos == 0 && stepsSeen_ > 0)
        {
            stop(); // exactly one loop
            return;
        }
        flush(); // nothing should be pending here
        cur_ = pre_[pos];
        pre_[pos] = 0;
        entered_ = pos;
        haveEntered_ = true;
        stepsSeen_++;
        if (cur_ == 0)
            emit(OP_CLEAR, pos, 0, 0, kNoChord, kNoVolume, kNoMicro);
    }

    // Sequencer note-on; chord notes of the same pulse are collected and written together by flush().
    void noteOn(uint8_t note, int8_t volume, const TrackPos& p, const Params& prm)
    {
        if (state_ != PUNCH && state_ != LOOP)
            return;
        span_ = (uint16_t)(p.last + 1u);
        uint8_t d = destStep(p, prm.quantize, false);
        int8_t mm = micro(p, prm.quantize);
        if (state_ == LOOP && d == 0 && haveEntered_ && entered_ != 0)
            return; // belongs to the next lap
        if (pending_ > 0 && (d != gDest_ || mm != gMicro_))
            flush();
        if (pending_ == 0)
        {
            gDest_ = d;
            gMicro_ = mm;
            gVol_ = -1;
        }
        if (pending_ < kMaxGroup)
        {
            bool dup = false;
            for (int i = 0; i < pending_; ++i)
                if (gNotes_[i] == note)
                    dup = true;
            if (!dup)
                gNotes_[pending_++] = note;
        }
        if (prm.velocity && volume > gVol_)
            gVol_ = volume;
    }

    // Writes the collected group (chord FX on MIDI tracks; sample tracks: lowest note only, see setTrackInfo).
    void flush()
    {
        bool sampleTrack = sample_;
        uint8_t instrument = instrument_;
        if (pending_ == 0)
            return;
        int n = pending_;
        pending_ = 0;
        // sort ascending (n <= 8)
        for (int i = 1; i < n; ++i)
        {
            uint8_t v = gNotes_[i];
            int j = i - 1;
            while (j >= 0 && gNotes_[j] > v)
            {
                gNotes_[j + 1] = gNotes_[j];
                --j;
            }
            gNotes_[j + 1] = v;
        }
        uint8_t chord = kNoChord;
        if (n > 1)
        {
            if (sampleTrack)
            {
                hints_ = (uint8_t)(hints_ | HINT_NEED_MIDI);
            }
            else
            {
                int kept = chordLookup(gNotes_, n, chord);
                if (kept < n)
                    hints_ = (uint8_t)(hints_ | HINT_REDUCED);
            }
        }
        claim(gDest_, 2);
        lastOn_ = gDest_;
        open_ = true;
        emit(OP_NOTE, gDest_, gNotes_[0], instrument, chord, gVol_, gMicro_);
    }

    // The host tells flush() what it needs through these (set before the first noteOn of a pulse).
    void setTrackInfo(bool sampleTrack, uint8_t instrument)
    {
        sample_ = sampleTrack;
        instrument_ = instrument;
    }

    // Sequencer note-off: the gate ends. Written as a note-off on an unclaimed step (stock rules).
    void noteOff(const TrackPos& p, const Params& prm)
    {
        if (state_ != PUNCH && state_ != LOOP)
            return;
        flush();
        span_ = (uint16_t)(p.last + 1u);
        uint8_t d = destStep(p, prm.quantize, true);
        if (state_ == LOOP && d == 0 && haveEntered_ && entered_ != 0)
            return;
        if (stateOf(d) != 0)
            return; // a note (or an off) already owns that step
        claim(d, 1);
        open_ = false;
        emit(OP_OFF, d, 0, 0, kNoChord, kNoVolume, micro(p, prm.quantize));
    }

  private:
    void reset()
    {
        state_ = IDLE;
        track_ = 0;
        hints_ = 0;
        memset(pre_, 0, sizeof(pre_));
        cur_ = 0;
        entered_ = 0;
        haveEntered_ = false;
        stepsSeen_ = 0;
        lastOn_ = 0xFF;
        open_ = false;
        span_ = 0;
        pending_ = 0;
        gDest_ = 0;
        gMicro_ = kNoMicro;
        gVol_ = -1;
        sample_ = false;
        instrument_ = 0;
    }
    uint8_t destStep(const TrackPos& p, bool quantize, bool off) const
    {
        unsigned span = (unsigned)p.last + 1u;
        unsigned d = p.pos;
        if (quantize && p.uStep > 24)
            d = (d + 1u) % span;
        if (off && d == lastOn_)
            d = (d + 1u) % span;
        return (uint8_t)d;
    }
    static int8_t micro(const TrackPos& p, bool quantize)
    {
        if (quantize)
            return kNoMicro;
        int v = ((int)p.uStep - 1) * 100 / 47;
        return (int8_t)(v < 0 ? 0 : (v > 100 ? 100 : v));
    }
    uint8_t stateOf(uint8_t step) const { return (haveEntered_ && step == entered_) ? cur_ : pre_[step]; }
    void claim(uint8_t step, uint8_t what)
    {
        if (haveEntered_ && step == entered_)
            cur_ = what;
        else if (step < kMaxSteps)
            pre_[step] = what;
    }
    void emit(uint8_t kind, uint8_t step, uint8_t note, uint8_t instrument, uint8_t chord, int8_t volume, int8_t micromove)
    {
        if (!sink_)
            return;
        Op o;
        o.kind = kind;
        o.track = track_;
        o.step = step;
        o.note = note;
        o.instrument = instrument;
        o.chord = chord;
        o.volume = volume;
        o.micromove = micromove;
        sink_(user_, o);
    }

    Sink sink_;
    void* user_;
    uint8_t ends_;
    State state_;
    uint8_t track_;
    uint8_t hints_;
    uint8_t pre_[kMaxSteps]; // claims on steps the playhead has not reached yet: 1 off, 2 note
    uint8_t cur_;            // claim on the step the playhead is in
    uint8_t entered_;
    bool haveEntered_;
    uint16_t stepsSeen_;
    uint8_t lastOn_;
    bool open_;      // a note was written and no note-off yet
    uint16_t span_;  // steps of the recorded track
    uint8_t pending_;
    uint8_t gNotes_[kMaxGroup];
    uint8_t gDest_;
    int8_t gMicro_;
    int8_t gVol_;
    bool sample_;
    uint8_t instrument_;
};

} // namespace seqrec
