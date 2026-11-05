// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Engine of the per-track sequencers (Chord/Arp, Mono, the ported HYPER_RASP scripts): state, takeover logic, command queue.
// No Polyend includes (only seqgen), so it is tested on the desktop (core/seqtest).
// Threads: postPad/postType and the getters are called from the main loop; everything else from the audio
// context (Sequencer::tick / tick_uStep). Main -> audio goes through a single-producer ring buffer.
#pragma once
#include "chordarp.h"
#include "mono.h"
#include "hr_scripts.h"
#include "seqTracksLogic.h"
#include "seqTracksRec.h"
#include "seqCut.h"
#include "seqPlinkySeqLogic.h"

namespace seqtracks {

using seqlogic::kTracks;

// What the engine needs from the firmware (implemented in seqTracks.cpp).
class Host
{
  public:
    virtual void takeoverBegin(uint8_t track) = 0; // silence the pattern on this track
    virtual void takeoverEnd(uint8_t track) = 0;
    virtual void noteOn(uint8_t track, uint8_t note, int8_t volume) = 0; // volume 0..100
    virtual void noteOff(uint8_t track, uint8_t note) = 0;
    virtual bool muted(uint8_t track) = 0;
    // record support (audio context)
    virtual void trackPos(uint8_t track, seqrec::TrackPos& out) = 0;
    virtual bool sampleTrack(uint8_t track) = 0;
    virtual uint8_t instrument(uint8_t track) = 0; // instrument a taken-over track plays
    virtual seqrec::Params recParams() = 0;
    // Pulse (0..95) of the bar the NEXT clock pulse falls on, derived from the pattern position; -1 = unknown (the engine counts
    // its own pulses from the transport start).
    virtual int16_t pulseInBar() { return -1; }
    // Short critical section (main loop only): keeps the audio context from changing a generator while its state is copied out
    // (saveState / stateHash). critEnter returns a token for critExit. Default: none (desktop tests).
    virtual uint32_t critEnter() { return 0; }
    virtual void critExit(uint32_t) {}

  protected:
    ~Host() {}
};

class Engine
{
  public:
    Engine();
    void setHost(Host* h) { host_ = h; }

    // ---- main loop ----
    bool postPad(uint8_t track, uint8_t pad, bool down, uint8_t velo = 0); // velo: pad pressure 1..127 for the Plinky type (0 = fixed)
    bool postType(uint8_t track, uint8_t type); // applied on the next bar while playing
    uint8_t type(uint8_t track) const { return track < kTracks ? t_[track].type : 0; }
    uint8_t pendingType(uint8_t track) const { return track < kTracks ? t_[track].pending : 0xFF; } // 0xFF = none
    bool takenOver(uint8_t track) const { return track < kTracks && ((takenMask_ >> track) & 1u); }
    void leds(uint8_t track, uint8_t out[seqlogic::kPads]) const;
    bool playing() const { return playing_; }

    // ---- Plinky type (plan 010 B): one track at a time plays the voice ----
    uint8_t plinkyOwner(bool withPending = true) const; // the Plinky track (or the one about to become it), 0xFF = none
    const seqplinky::Setup& plinkySetup() const { return pl_; }
    bool plinkyMenu() const { return plMenu_; }

    // ---- CUT (MLR) layer, see seqCut.h (the Cut object lives outside the engine: the glue places it in external RAM) ----
    void setCut(seqcut::Cut* c) { __atomic_store_n(&cut_, c, __ATOMIC_RELEASE); } // the object is fully constructed first
    seqcut::Cut* cut() const { return __atomic_load_n(&cut_, __ATOMIC_ACQUIRE); }
    bool postCut(uint8_t track, uint8_t arg); // arg = seqcut::packArg(...); main loop
    uint16_t cutMask() const { seqcut::Cut* c = cut(); return c ? c->hookMask() : 0; } // tracks the audio hooks must serve (driven + hand-back retrigger)
    uint16_t cutOffGridMask() const { seqcut::Cut* c = cut(); return c ? c->offGridMask() : 0; } // tracks really off the grid (not yet snapped by SYNC ALL): UI, R1 strip
    // any context: Play was pressed, the transport restarts at step fromPos (playheads continue from fromPos + 1)
    void cutPlayStarted(uint8_t fromPos = 0) { cutRestartPos_ = fromPos; cutRestart_ = true; }
    bool seqActive() const { return !loading_ && (activeCount_ != 0 || head_ != tail_ || rec_.active()); } // without CUT

    // ---- record (main loop side) ----
    bool postRec(uint8_t track, uint8_t mode); // mode: REC_STOP / REC_PUNCH / REC_LOOP
    seqrec::Planner::State recState() const { return recState_; }
    uint8_t recTrack() const { return recTrack_; }
    uint8_t recHints() const { return recHints_; }
    uint8_t recEnds() const { return recEnds_; }
    uint8_t recWhy() const { return recWhy_; } // why the last take ended (seqrecui::EndWhy)
    bool popRecOp(seqrec::Op& out); // ops from the audio side, in order

    // ---- project sidecar (main loop, with the audio engine idle or stopped) ----
    // File = "SQTR", version, track count, payload length (u16), per track [type, lenCA, CA, lenMono, Mono, (version 2:) lenHR, HR], CRC16.
    // Version 1 and 2 share one layout; version 2 may carry the HR trailer. Both load. Saving writes version 1 while no track uses a
    // HYPER_RASP type (so older mod versions can still read the file) and version 2 otherwise. The HR block is the settings of the
    // track's HYPER_RASP script.
    enum { kSidecarVersionV1 = 1, kSidecarVersion = 2, kSidecarMax = 8 + seqlogic::kTracks * (4 + seqgen::ChordArp::kSerialSize + seqgen::Mono::kSerialSize + seqgen::HrScript::kMaxSerial) + 2 };
    size_t saveState(uint8_t* buf, size_t cap) const; // 0 = buffer too small
    bool loadState(const uint8_t* buf, size_t len);   // false = invalid frame (nothing changed); a bad track block only turns that track Off
    void resetState();                                // all tracks Off, fresh state
    uint16_t stateHash() const;                       // CRC of the serialized settings (change detection; runtime-only fields masked)

    // ---- audio context ----
    bool active() const { return !loading_ && (activeCount_ != 0 || head_ != tail_ || rec_.active() || cutMask() != 0 || cutRestart_); }
    bool isTakenOver(int row) const { return (takenMask_ >> row) & 1u; }
    void stepClock(uint16_t trackUStep); // transport running: once per uStep of track 0, before play_microStep
    void idleTick();                     // transport stopped: once per nano tick (internal clock; external clock: only per clock pulse)
    // Once per audio block, any clock source: drains the command queue (pad releases must not wait for a clock pulse) and,
    // while the transport is stopped or the clock has been silent for kClockTimeoutUs, runs the stopped-transport logic
    // (Mono silenced, recording ended).
    void blockTick(bool transportPlaying, uint32_t us);
    enum { kClockTimeoutUs = 500000 };
    void advanceUs(uint32_t us);         // wall time of one audio block (gate timers, blinking)

    // Direct (non-queued) variants, also used by the host tests.
    void applyPad(uint8_t track, uint8_t pad, bool down, uint8_t velo = 0);
    void applyType(uint8_t track, uint8_t type);
    void drain();
    enum { REC_STOP = 0, REC_PUNCH = 1, REC_LOOP = 2, REC_STOP_PATTERN = 3 }; // REC_STOP_PATTERN: ends the take, the reason is "pattern changed"
    void applyRec(uint8_t track, uint8_t mode);

    seqgen::SeqContext& context() { return ctx_; }
    seqgen::ChordArp& chordArp(uint8_t t) { return t_[t].ca; }
    seqgen::Mono& mono(uint8_t t) { return t_[t].mo; }
    const seqgen::HrScript* hr(uint8_t t) const { return t < kTracks ? t_[t].hr.get() : 0; } // the HYPER_RASP script of an HR-type track, else 0
    seqgen::HrScript* hr(uint8_t t) { return t < kTracks ? t_[t].hr.get() : 0; }
    // Main loop: runs fn(script or 0, user) with the audio context held off. A pending type change can construct another script class
    // into the same slot from the audio interrupt, so a reader of title() / legend() must not run unprotected. Keep fn short.
    void withHr(uint8_t t, void (*fn)(const seqgen::HrScript*, void*), void* user) const;
    seqgen::HrBus& hrBus() { return hrBus_; }
    int soundingCount(uint8_t t) const { return t_[t].nSounding; }
    // Main loop, display only (v0.3.3): the notes the track's sequencer sounds now, as a 128 bit mask (bit n of byte n / 8). Plain byte
    // reads of the audio side's own mask: a torn read shows one frame of a note too many or too few, nothing else.
    void soundingNotes(uint8_t t, uint8_t out[16]) const
    {
        if (t >= kTracks)
        {
            for (int i = 0; i < 16; i++)
                out[i] = 0;
            return;
        }
        for (int i = 0; i < 16; i++)
            out[i] = t_[t].sounding[i];
    }

  private:
    enum { kRing = 64 };
    struct Cmd
    {
        uint8_t kind, track, a, b;
    };
    struct Track
    {
        seqgen::ChordArp ca;
        seqgen::Mono mo;
        seqgen::HrSlot hr; // the script of an HR type (constructed in place on a type change)
        uint8_t type;
        uint8_t pending;
        bool monoSteps;
        bool needStart; // started mid-playback: generator starts on the next pulse boundary, aligned to the bar
        uint8_t nSounding;
        uint8_t padBits[seqlogic::kPads / 8];
        uint8_t sounding[16];
        int16_t grpNote;    // sample track: lowest note of the current simultaneous group (-1 none)
        uint32_t grpEpoch;  // group the note belongs to
    };

    bool push(const Cmd& c);
    void setPlaying(bool p);
    void pulseAll();
    void idleLogic();
    void refresh(uint8_t track);
    void releaseAll(uint8_t track);
    void applyPending();
    void startTrack(uint8_t track);
    void recomputeContext();
    void initTrack(uint8_t i);
    void recSink(const seqrec::Op& op);
    static void recSinkThunk(void* user, const seqrec::Op& op);
    void recPulseEnd();
    void recFeedOn(uint8_t track, const seqgen::Event& e);
    void recFeedOff(uint8_t track);
    void recPublish();
    int heldPads(const Track& t) const;
    bool recLive(uint8_t track) const { return ((takenMask_ >> track) & 1u) || heldPads(t_[track]) > 0; } // the sequencer is played on the track (also while it is muted)
    void scanMono(Track& t);
    void padPlinky(uint8_t track, uint8_t pad, bool down, uint8_t velo);
    bool plinkyNoteHeld(uint8_t note, uint8_t exceptPad) const;
    void createHr(uint8_t track, uint8_t type);
    static void sink(void* user, const seqgen::Event& e);
    void onEvent(const seqgen::Event& e);

    Host* host_;
    seqgen::SeqContext ctx_;
    seqgen::HrBus hrBus_; // scale / root shared by the HYPER_RASP scripts (their set_scale / set_root_note)
    Track t_[kTracks];
    seqplinky::Setup pl_; // Plinky type: root / scale / octave / string interval (one voice, so one set)
    bool plMenu_;         // Plinky type: the setup view is shown
    uint8_t plNote_[seqplinky::kPads]; // note each held Plinky pad started
    Cmd ring_[kRing];
    volatile uint8_t head_, tail_;
    volatile uint16_t takenMask_;
    uint8_t activeCount_;
    bool playing_;
    uint8_t barPulse_;
    uint8_t idleCount_;
    uint32_t usAcc_;
    uint32_t clockSilentUs_;
    uint32_t epoch_; // bumped before every call into a generator: events of one call form one simultaneous group

    // record: planner + queue audio -> main
    enum { kRecRing = 128 }; // 8 bytes each: 1 KB; must stay <= 256 (8-bit indices)
    seqrec::Planner rec_;
    seqrec::Op recRing_[kRecRing];
    volatile uint8_t rHead_, rTail_;
    volatile uint8_t recDrops_; // operations dropped because the queue was full (this recording)
    volatile seqrec::Planner::State recState_;
    volatile uint8_t recTrack_, recHints_, recEnds_, recWhy_;
    volatile bool loading_;
    seqcut::Cut* cut_;
    volatile bool cutRestart_;
    volatile uint8_t cutRestartPos_;
};

} // namespace seqtracks
