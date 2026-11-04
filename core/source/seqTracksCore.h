// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Engine of the per-track sequencers (Chord/Arp, Mono): state, takeover logic, command queue.
// No Polyend includes (only seqgen), so it is tested on the desktop (core/seqtest).
// Threads: postPad/postType and the getters are called from the main loop; everything else from the audio
// context (Sequencer::tick / tick_uStep). Main -> audio goes through a single-producer ring buffer.
#pragma once
#include "chordarp.h"
#include "mono.h"
#include "seqTracksLogic.h"
#include "seqTracksRec.h"
#include "seqCut.h"

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

  protected:
    ~Host() {}
};

class Engine
{
  public:
    Engine();
    void setHost(Host* h) { host_ = h; }

    // ---- main loop ----
    bool postPad(uint8_t track, uint8_t pad, bool down);
    bool postType(uint8_t track, uint8_t type); // applied on the next bar while playing
    uint8_t type(uint8_t track) const { return track < kTracks ? t_[track].type : 0; }
    uint8_t pendingType(uint8_t track) const { return track < kTracks ? t_[track].pending : 0xFF; } // 0xFF = none
    bool takenOver(uint8_t track) const { return track < kTracks && ((takenMask_ >> track) & 1u); }
    void leds(uint8_t track, uint8_t out[seqlogic::kPads]) const;
    bool playing() const { return playing_; }

    // ---- CUT (MLR) layer, see seqCut.h (the Cut object lives outside the engine: the glue places it in external RAM) ----
    void setCut(seqcut::Cut* c) { cut_ = c; }
    seqcut::Cut* cut() const { return cut_; }
    bool postCut(uint8_t track, uint8_t arg); // arg = seqcut::packArg(...); main loop
    uint16_t cutMask() const { return cut_ ? cut_->activeMask() : 0; } // tracks driven by their playhead
    void cutPlayStarted() { cutRestart_ = true; } // any context: Play was pressed (transport restarts at step 0)

    // ---- record (main loop side) ----
    bool postRec(uint8_t track, uint8_t mode); // mode: REC_STOP / REC_PUNCH / REC_LOOP
    seqrec::Planner::State recState() const { return recState_; }
    uint8_t recTrack() const { return recTrack_; }
    uint8_t recHints() const { return recHints_; }
    uint8_t recEnds() const { return recEnds_; }
    bool popRecOp(seqrec::Op& out); // ops from the audio side, in order

    // ---- project sidecar (main loop, with the audio engine idle or stopped) ----
    // File = "SQTR", version, track count, payload length (u16), per track [type, lenCA, CA, lenMono, Mono], CRC16.
    enum { kSidecarVersion = 1, kSidecarMax = 8 + seqlogic::kTracks * (3 + seqgen::ChordArp::kSerialSize + seqgen::Mono::kSerialSize) + 2 };
    size_t saveState(uint8_t* buf, size_t cap) const; // 0 = buffer too small
    bool loadState(const uint8_t* buf, size_t len);   // false = invalid (nothing changed)
    void resetState();                                // all tracks Off, fresh state
    uint16_t stateHash() const;                       // CRC of the serialized state (change detection)

    // ---- audio context ----
    bool active() const { return !loading_ && (activeCount_ != 0 || head_ != tail_ || rec_.active() || cutMask() != 0 || cutRestart_); }
    bool isTakenOver(int row) const { return (takenMask_ >> row) & 1u; }
    void stepClock(uint16_t trackUStep); // transport running: once per uStep of track 0, before play_microStep
    void idleTick();                     // transport stopped: once per nano tick
    void advanceUs(uint32_t us);         // wall time of one audio block (gate timers, blinking)

    // Direct (non-queued) variants, also used by the host tests.
    void applyPad(uint8_t track, uint8_t pad, bool down);
    void applyType(uint8_t track, uint8_t type);
    void drain();
    enum { REC_STOP = 0, REC_PUNCH = 1, REC_LOOP = 2 };
    void applyRec(uint8_t track, uint8_t mode);

    seqgen::SeqContext& context() { return ctx_; }
    seqgen::ChordArp& chordArp(uint8_t t) { return t_[t].ca; }
    seqgen::Mono& mono(uint8_t t) { return t_[t].mo; }
    int soundingCount(uint8_t t) const { return t_[t].nSounding; }

  private:
    enum { kRing = 64 };
    struct Cmd
    {
        uint8_t kind, track, a;
    };
    struct Track
    {
        seqgen::ChordArp ca;
        seqgen::Mono mo;
        uint8_t type;
        uint8_t pending;
        bool monoSteps;
        uint8_t nSounding;
        uint8_t padBits[seqlogic::kPads / 8];
        uint8_t sounding[16];
    };

    bool push(const Cmd& c);
    void setPlaying(bool p);
    void pulseAll();
    void refresh(uint8_t track);
    void releaseAll(uint8_t track);
    void applyPending();
    void recomputeContext();
    void initTrack(uint8_t i);
    void recSink(const seqrec::Op& op);
    static void recSinkThunk(void* user, const seqrec::Op& op);
    void recPulseEnd();
    void recPublish();
    int heldPads(const Track& t) const;
    void scanMono(Track& t);
    static void sink(void* user, const seqgen::Event& e);
    void onEvent(const seqgen::Event& e);

    Host* host_;
    seqgen::SeqContext ctx_;
    Track t_[kTracks];
    Cmd ring_[kRing];
    volatile uint8_t head_, tail_;
    volatile uint16_t takenMask_;
    uint8_t activeCount_;
    bool playing_;
    uint8_t barPulse_;
    uint8_t idleCount_;
    uint32_t usAcc_;

    // record: planner + queue audio -> main
    enum { kRecRing = 32 };
    seqrec::Planner rec_;
    seqrec::Op recRing_[kRecRing];
    volatile uint8_t rHead_, rTail_;
    volatile seqrec::Planner::State recState_;
    volatile uint8_t recTrack_, recHints_, recEnds_;
    volatile bool loading_;
    seqcut::Cut* cut_;
    volatile bool cutRestart_;
};

} // namespace seqtracks
