// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
#include "seqTracksCore.h"
#include <string.h>

#define SEQ_COMPILER_BARRIER() __asm__ __volatile__("" ::: "memory")

namespace seqtracks {

using namespace seqlogic;

static_assert((int)seqlogic::kHrScripts == (int)seqgen::HR_COUNT, "kHrScripts (seqTracksLogic.h) must equal seqgen::HR_COUNT");

enum
{
    CMD_PAD = 1,
    CMD_TYPE = 2,
    CMD_REC = 3,
    CMD_CUT = 4
};

Engine::Engine()
  : host_(0), head_(0), tail_(0), takenMask_(0), activeCount_(0), playing_(false), barPulse_(0), idleCount_(0), usAcc_(0), clockSilentUs_(0), epoch_(1),
    rHead_(0), rTail_(0), recDrops_(0), recState_(seqrec::Planner::IDLE), recTrack_(0), recHints_(0), recEnds_(0), loading_(false), cut_(0), cutRestart_(false), cutRestartPos_(0)
{
    memset(ring_, 0, sizeof(ring_));
    memset(recRing_, 0, sizeof(recRing_));
    for (int i = 0; i < kTracks; i++)
        initTrack((uint8_t)i);
    rec_.setSink(&Engine::recSinkThunk, this);
    recomputeContext();
}

void Engine::initTrack(uint8_t i)
{
    Track& t = t_[i];
    t.ca = seqgen::ChordArp();
    t.mo = seqgen::Mono();
    t.type = TYPE_OFF;
    t.pending = 0xFF;
    t.monoSteps = false;
    t.needStart = false;
    t.nSounding = 0;
    memset(t.padBits, 0, sizeof(t.padBits));
    memset(t.sounding, 0, sizeof(t.sounding));
    t.grpNote = -1;
    t.grpEpoch = 0;
    t.hr.clear();
    t.ca.setChannel(i);
    t.mo.setChannel(i);
    t.ca.setSink(&Engine::sink, this);
    t.mo.setSink(&Engine::sink, this);
    t.mo.setContext(&ctx_);
}

// ------------------------------------------------------------------ main loop side
bool Engine::push(const Cmd& c)
{
    uint8_t h = head_;
    uint8_t n = (uint8_t)((h + 1) % kRing);
    if (n == tail_)
        return false; // full: drop
    ring_[h] = c;
    __atomic_store_n(&head_, n, __ATOMIC_RELEASE);
    return true;
}

bool Engine::postPad(uint8_t track, uint8_t pad, bool down)
{
    if (track >= kTracks || pad >= kPads)
        return false;
    Cmd c = { CMD_PAD, track, (uint8_t)(pad | (down ? 0x80 : 0)) };
    return push(c);
}

bool Engine::postType(uint8_t track, uint8_t type)
{
    if (track >= kTracks || type >= TYPE_COUNT)
        return false;
    Cmd c = { CMD_TYPE, track, type };
    return push(c);
}

bool Engine::postRec(uint8_t track, uint8_t mode)
{
    if (track >= kTracks || mode > REC_LOOP)
        return false;
    Cmd c = { CMD_REC, track, mode };
    return push(c);
}

bool Engine::postCut(uint8_t track, uint8_t arg)
{
    if (track >= kTracks || seqcut::argSub(arg) >= seqcut::SUB_COUNT)
        return false;
    Cmd c = { CMD_CUT, track, arg };
    return push(c);
}

bool Engine::popRecOp(seqrec::Op& out)
{
    uint8_t t = rTail_;
    if (t == __atomic_load_n(&rHead_, __ATOMIC_ACQUIRE))
        return false;
    out = recRing_[t];
    rTail_ = (uint8_t)((t + 1) % kRecRing);
    return true;
}

void Engine::leds(uint8_t track, uint8_t out[kPads]) const
{
    memset(out, 0, kPads);
    if (track >= kTracks)
        return;
    const Track& t = t_[track];
    if (t.type == TYPE_CHORDARP)
        t.ca.leds(out);
    else if (t.type == TYPE_MONO)
        t.mo.leds(out);
    else if (isHrType(t.type) && t.hr.get())
        t.hr.get()->leds(out);
}

void Engine::createHr(uint8_t track, uint8_t type)
{
    Track& t = t_[track];
    seqgen::HrScript* s = t.hr.create((uint8_t)(type - TYPE_HR_FIRST));
    if (!s)
        return;
    s->setBus(&hrBus_);
    s->setChannel(track);
    s->setSink(&Engine::sink, this);
}

// ------------------------------------------------------------------ audio side
void Engine::drain()
{
    while (tail_ != __atomic_load_n(&head_, __ATOMIC_ACQUIRE))
    {
        Cmd c = ring_[tail_];
        tail_ = (uint8_t)((tail_ + 1) % kRing);
        if (c.kind == CMD_PAD)
        {
            applyPad(c.track, (uint8_t)(c.a & 0x7F), (c.a & 0x80) != 0);
        }
        else if (c.kind == CMD_TYPE)
        {
            if (playing_ && activeCount_ > 0 && t_[c.track].type != c.a)
                t_[c.track].pending = c.a; // next bar (immediately when no track runs yet)
            else
                applyType(c.track, c.a);
            if (c.a == t_[c.track].type)
                t_[c.track].pending = 0xFF; // choosing the current type again cancels a pending change
        }
        else if (c.kind == CMD_REC)
        {
            applyRec(c.track, c.a);
        }
        else if (c.kind == CMD_CUT)
        {
            if (seqcut::Cut* cu = cut())
                cu->apply(c.track, c.a);
        }
    }
}

// ------------------------------------------------------------------ record
void Engine::recSinkThunk(void* user, const seqrec::Op& op)
{
    static_cast<Engine*>(user)->recSink(op);
}

void Engine::recSink(const seqrec::Op& op)
{
    uint8_t h = rHead_;
    uint8_t n = (uint8_t)((h + 1) % kRecRing);
    if (n == rTail_)
    {
        if (recDrops_ < 255)
            recDrops_ = (uint8_t)(recDrops_ + 1); // full: drop, but say so ("Rec overflow" on the screen)
        return;
    }
    recRing_[h] = op;
    __atomic_store_n(&rHead_, n, __ATOMIC_RELEASE);
}

void Engine::recPublish()
{
    recState_ = rec_.state();
    recTrack_ = rec_.track();
    recHints_ = (uint8_t)(rec_.hints() | (recDrops_ ? seqrec::HINT_OVERFLOW : 0));
    recEnds_ = rec_.ends();
}

void Engine::applyRec(uint8_t track, uint8_t mode)
{
    if (mode == REC_STOP || track >= kTracks || !host_)
    {
        rec_.stop();
    }
    else if (t_[track].type != TYPE_OFF)
    {
        bool again = rec_.active() && rec_.track() == track;
        rec_.stop();
        if (!again) // a second Rec before the screen showed the first one ends the recording (toggle), it does not restart it
        {
            recDrops_ = 0;
            seqrec::TrackPos p;
            host_->trackPos(track, p);
            // transport not running yet (Rec started it): the first step belongs to the pass
            rec_.begin(track, mode == REC_LOOP, p.pos, !playing_);
        }
    }
    recPublish();
}

void Engine::recPulseEnd()
{
    if (rec_.active())
        rec_.flush();
    recPublish();
}

int Engine::heldPads(const Track& t) const
{
    // grid pads only: holding a panel / control pad does not take the track over
    int n = 0;
    for (unsigned i = 0; i < sizeof(t.padBits); i++)
        for (uint8_t b = t.padBits[i]; b; b = (uint8_t)(b & (b - 1)))
        {
            unsigned bit = 0;
            while (!((b >> bit) & 1u))
                bit++;
            if (isNotePad(t.type, (uint8_t)(i * 8 + bit)))
                n++;
        }
    return n;
}

void Engine::scanMono(Track& t)
{
    t.monoSteps = false;
    for (int i = 1; i <= seqgen::Mono::kMaxSteps; i++)
        if (t.mo.stepAt(i) != seqgen::Mono::kNoStep)
        {
            t.monoSteps = true;
            break;
        }
}

void Engine::applyPad(uint8_t track, uint8_t pad, bool down)
{
    if (track >= kTracks || pad >= kPads)
        return;
    Track& t = t_[track];
    if (t.type == TYPE_OFF)
        return;
    epoch_++;
    uint8_t& byte = t.padBits[pad >> 3];
    uint8_t bit = (uint8_t)(1u << (pad & 7));
    bool was = (byte & bit) != 0;
    if (down)
        byte = (uint8_t)(byte | bit);
    else
        byte = (uint8_t)(byte & ~bit);
    if (!down && !was)
        return; // stray release (e.g. after a type change)
    uint8_t col = padCol(pad), row = padRow(pad);
    if (t.type == TYPE_CHORDARP)
    {
        t.ca.pad(col, row, down);
    }
    else if (t.type == TYPE_MONO)
    {
        t.mo.pad(col, row, down);
        scanMono(t);
    }
    else if (t.hr.get())
    {
        t.hr.get()->pad(col, row, down);
    }
    refresh(track);
}

void Engine::recomputeContext()
{
    int primary = -1;
    for (int i = 0; i < kTracks; i++)
        if (t_[i].type == TYPE_CHORDARP)
        {
            primary = i;
            break;
        }
    for (int i = 0; i < kTracks; i++)
        t_[i].ca.setContext(i == primary ? &ctx_ : 0);
    ctx_.chord = primary >= 0 ? &t_[primary].ca : 0;
}

void Engine::applyType(uint8_t track, uint8_t type)
{
    if (track >= kTracks)
        return;
    Track& t = t_[track];
    epoch_++;
    t.pending = 0xFF;
    if (type >= TYPE_COUNT || type == t.type)
        return;

    // release the pads of the old generator, stop it, silence everything it sounded
    for (int pad = 0; pad < kPads; pad++)
        if (t.padBits[pad >> 3] & (1u << (pad & 7)))
        {
            if (t.type == TYPE_CHORDARP)
                t.ca.pad(padCol((uint8_t)pad), padRow((uint8_t)pad), false);
            else if (t.type == TYPE_MONO)
                t.mo.pad(padCol((uint8_t)pad), padRow((uint8_t)pad), false);
            else if (isHrType(t.type) && t.hr.get())
                t.hr.get()->pad(padCol((uint8_t)pad), padRow((uint8_t)pad), false);
        }
    memset(t.padBits, 0, sizeof(t.padBits));
    if (t.type == TYPE_CHORDARP)
        t.ca.stop();
    else if (t.type == TYPE_MONO)
        t.mo.stop();
    else if (isHrType(t.type) && t.hr.get())
        t.hr.get()->silence();
    releaseAll(track);

    if (t.type == TYPE_OFF)
        activeCount_++;
    t.type = type;
    if (type == TYPE_OFF)
    {
        activeCount_--;
        if (rec_.active() && rec_.track() == track)
        {
            rec_.stop();
            recPublish();
        }
    }

    if (isHrType(type))
    {
        if (!t.hr.get() || t.hr.get()->hrType() != type - TYPE_HR_FIRST) // loadState creates the script first, to restore its settings
            createHr(track, type);
    }
    else
        t.hr.clear();

    recomputeContext();
    if (type == TYPE_MONO)
        scanMono(t);
    t.needStart = playing_ && type != TYPE_OFF; // starts on the next pulse boundary, aligned to the bar (pulseAll)
    refresh(track);
}

// Pulse boundary of a playing transport: start the generator on the bar grid.
void Engine::startTrack(uint8_t track)
{
    Track& t = t_[track];
    epoch_++;
    t.needStart = false;
    if (t.type == TYPE_CHORDARP)
    {
        t.ca.start();
        t.ca.setBarPos(barPulse_); // phase of the bar the pulse about to run falls on
    }
    else if (t.type == TYPE_MONO)
    {
        t.mo.start();
    }
    else if (isHrType(t.type) && t.hr.get() && t.hr.get()->followsTransport())
    {
        t.hr.get()->start(); // start_playback
    }
}

void Engine::applyPending()
{
    for (int i = 0; i < kTracks; i++)
        if (t_[i].pending != 0xFF)
            applyType((uint8_t)i, t_[i].pending);
}

void Engine::setPlaying(bool p)
{
    if (p == playing_)
        return;
    playing_ = p;
    barPulse_ = 0;
    for (int i = 0; i < kTracks; i++)
    {
        Track& t = t_[i];
        if (t.type == TYPE_OFF)
            continue;
        if (p)
        {
            refresh((uint8_t)i);
            t.needStart = true; // the first pulse boundary starts it (the transport starts on one)
        }
        else
        {
            t.needStart = false;
            epoch_++;
            if (t.type == TYPE_MONO)
                t.mo.stop(); // Chord/Arp keeps running while stopped (free-running clock)
            else if (isHrType(t.type) && t.hr.get() && t.hr.get()->followsTransport())
                t.hr.get()->stop(); // HYPER_RASP scripts that follow the transport: stop_playback
            refresh((uint8_t)i);
        }
    }
}

void Engine::pulseAll()
{
    if (playing_)
    {
        int16_t hp = host_ ? host_->pulseInBar() : (int16_t)-1;
        if (hp >= 0)
            barPulse_ = (uint8_t)(hp % seqgen::kBarPulses); // position-derived: bars of the pattern, wherever we started
        if (isBarStart(barPulse_))
            applyPending();
    }
    for (int i = 0; i < kTracks; i++)
        if (t_[i].needStart && playing_)
            startTrack((uint8_t)i);
    for (int i = 0; i < kTracks; i++)
        if (t_[i].type == TYPE_CHORDARP)
        {
            epoch_++;
            t_[i].ca.pulse();
            refresh((uint8_t)i);
        }
    if (playing_)
    {
        barPulse_ = (uint8_t)((barPulse_ + 1) % seqgen::kBarPulses);
        for (int i = 0; i < kTracks; i++)
            if (t_[i].type == TYPE_MONO)
            {
                epoch_++;
                t_[i].mo.pulse();
                refresh((uint8_t)i);
            }
            else if (isHrType(t_[i].type) && t_[i].hr.get())
            {
                epoch_++;
                t_[i].hr.get()->pulse(); // event_midi(248): HYPER_RASP clocks its scripts while the transport plays
                refresh((uint8_t)i);
            }
    }
}

void Engine::stepClock(uint16_t trackUStep)
{
    clockSilentUs_ = 0;
    if (activeCount_ == 0)
        playing_ = true; // keep the transport flag in step while no track runs
    if (cutRestart_)
    {
        cutRestart_ = false;
        if (seqcut::Cut* c = cut())
            c->transportStart(cutRestartPos_);
    }
    drain();
    if (activeCount_ == 0)
        return;
    setPlaying(true);
    if (rec_.active() && host_)
    {
        seqrec::TrackPos p;
        host_->trackPos(rec_.track(), p);
        if (p.uStep == 1)
            rec_.enter(p.pos);
    }
    if (((trackUStep - 1) % 8) == 0)
        pulseAll();
    recPulseEnd();
}

void Engine::idleLogic()
{
    if (activeCount_ == 0)
        playing_ = false;
    drain();
    if (rec_.active())
    {
        rec_.stop(); // the transport stopped: recording ends
        recPublish();
    }
    if (activeCount_ == 0)
        return;
    setPlaying(false); // Mono silenced (Chord/Arp keeps its free-running clock)
}

void Engine::idleTick()
{
    clockSilentUs_ = 0;
    idleLogic();
    if (activeCount_ == 0)
        return;
    if (++idleCount_ >= 96)
    {
        idleCount_ = 0;
        pulseAll();
    }
}

void Engine::blockTick(bool transportPlaying, uint32_t us)
{
    drain();
    if (clockSilentUs_ < 0xFFFFFFFFu - us)
        clockSilentUs_ += us;
    if (!transportPlaying || clockSilentUs_ > (uint32_t)kClockTimeoutUs)
        idleLogic();
}

void Engine::advanceUs(uint32_t us)
{
    if (activeCount_ == 0)
        return;
    const uint32_t tickUs = (uint32_t)seqgen::kTickMs * 1000u;
    usAcc_ += us;
    while (usAcc_ >= tickUs)
    {
        usAcc_ -= tickUs;
        for (int i = 0; i < kTracks; i++)
        {
            if (t_[i].type == TYPE_CHORDARP)
            {
                epoch_++;
                t_[i].ca.tick();
                refresh((uint8_t)i);
            }
            else if (t_[i].type == TYPE_MONO)
            {
                t_[i].mo.tick();
            }
            else if (isHrType(t_[i].type) && t_[i].hr.get())
            {
                epoch_++;
                t_[i].hr.get()->tick(); // framework_tick
                refresh((uint8_t)i);
            }
        }
    }
    recPulseEnd();
}

// ------------------------------------------------------------------ takeover + events
void Engine::refresh(uint8_t track)
{
    Track& t = t_[track];
    bool want = isHrType(t.type) ? (t.hr.get() && wantTakeoverHr(t.hr.get()->live(), t.nSounding))
                                 : wantTakeover(t.type, heldPads(t), t.nSounding, t.monoSteps, playing_);
    bool taken = ((takenMask_ >> track) & 1u) != 0;
    if (want && !taken)
    {
        takenMask_ = (uint16_t)(takenMask_ | (1u << track));
        if (host_)
            host_->takeoverBegin(track);
    }
    else if (!want && taken)
    {
        releaseAll(track);
        takenMask_ = (uint16_t)(takenMask_ & ~(1u << track));
        if (host_)
            host_->takeoverEnd(track);
    }
}

void Engine::releaseAll(uint8_t track)
{
    Track& t = t_[track];
    if (t.nSounding == 0)
        return;
    if (rec_.active() && rec_.track() == track && host_)
    {
        seqrec::TrackPos p;
        host_->trackPos(track, p);
        rec_.noteOff(p, host_->recParams());
    }
    for (int by = 0; by < 16; by++)
        if (t.sounding[by])
            for (int bit = 0; bit < 8; bit++)
                if (t.sounding[by] & (1u << bit))
                {
                    t.sounding[by] = (uint8_t)(t.sounding[by] & ~(1u << bit));
                    if (host_)
                        host_->noteOff(track, (uint8_t)(by * 8 + bit));
                }
    t.grpNote = -1;
    t.nSounding = 0;
}

void Engine::sink(void* user, const seqgen::Event& e)
{
    static_cast<Engine*>(user)->onEvent(e);
}

void Engine::onEvent(const seqgen::Event& e)
{
    uint8_t track = e.chan;
    if (track >= kTracks || !host_ || e.note > 127)
        return;
    Track& t = t_[track];
    uint8_t& byte = t.sounding[e.note >> 3];
    uint8_t bit = (uint8_t)(1u << (e.note & 7));
    if (e.kind == seqgen::EV_NOTE_ON)
    {
        if (host_->muted(track))
            return;
        if (!((takenMask_ >> track) & 1u))
        {
            takenMask_ = (uint16_t)(takenMask_ | (1u << track));
            host_->takeoverBegin(track);
        }
        // A sample track has one voice: of the notes of one simultaneous group (a chord) only the lowest is sent
        // (the others would only churn the voice; the recording writes the lowest note too). Recording still sees them all.
        bool send = true;
        if (host_->sampleTrack(track))
        {
            if (t.grpEpoch == epoch_ && t.grpNote >= 0)
            {
                if (e.note >= t.grpNote)
                    send = false;
                else
                {
                    uint8_t& ob = t.sounding[t.grpNote >> 3];
                    uint8_t obit = (uint8_t)(1u << (t.grpNote & 7));
                    if (ob & obit)
                    {
                        ob = (uint8_t)(ob & ~obit);
                        t.nSounding--;
                        host_->noteOff(track, (uint8_t)t.grpNote);
                    }
                }
            }
            if (send)
            {
                t.grpEpoch = epoch_;
                t.grpNote = e.note;
            }
        }
        if (send)
        {
            if (!(byte & bit))
            {
                byte = (uint8_t)(byte | bit);
                t.nSounding++;
            }
            host_->noteOn(track, e.note, velocityToVolume(e.vel));
        }
        if (rec_.active() && rec_.track() == track)
        {
            seqrec::TrackPos p;
            host_->trackPos(track, p);
            rec_.setTrackInfo(host_->sampleTrack(track), host_->instrument(track));
            rec_.noteOn(e.note, velocityToVolume(e.vel), p, host_->recParams());
        }
    }
    else if (byte & bit)
    {
        byte = (uint8_t)(byte & ~bit);
        t.nSounding--;
        host_->noteOff(track, e.note);
        if (rec_.active() && rec_.track() == track)
        {
            seqrec::TrackPos p;
            host_->trackPos(track, p);
            rec_.setTrackInfo(host_->sampleTrack(track), host_->instrument(track));
            rec_.noteOff(p, host_->recParams());
        }
    }
}

// ------------------------------------------------------------------ project sidecar
static uint16_t crc16Update(uint16_t c, const uint8_t* b, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        c = (uint16_t)(c ^ ((uint16_t)b[i] << 8));
        for (int k = 0; k < 8; k++)
            c = (uint16_t)((c & 0x8000) ? ((c << 1) ^ 0x1021) : (c << 1));
    }
    return c;
}
static uint16_t crc16(const uint8_t* b, size_t n)
{
    return crc16Update(0xFFFF, b, n);
}

// The audio context may change a generator at any time: each track's two blocks are serialized inside one short critical
// section (a few microseconds), so every block is internally consistent (a torn block would fail deserialize).
size_t Engine::saveState(uint8_t* buf, size_t cap) const
{
    if (cap < (size_t)kSidecarMax)
        return 0;
    buf[0] = 'S';
    buf[1] = 'Q';
    buf[2] = 'T';
    buf[3] = 'R';
    buf[4] = kSidecarVersion;
    buf[5] = kTracks;
    size_t p = 8;
    for (int i = 0; i < kTracks; i++)
    {
        const Track& t = t_[i];
        uint32_t cs = host_ ? host_->critEnter() : 0;
        buf[p++] = t.type;
        size_t n = t.ca.serialize(buf + p + 1, cap - p - 1);
        buf[p] = (uint8_t)n;
        p += 1 + n;
        n = t.mo.serialize(buf + p + 1, cap - p - 1);
        buf[p] = (uint8_t)n;
        p += 1 + n;
        if (host_)
            host_->critExit(cs);
    }
    // trailer (only when a track runs a HYPER_RASP script): [lenHR, HR] per track
    bool anyHr = false;
    for (int i = 0; i < kTracks; i++)
        anyHr = anyHr || isHrType(t_[i].type);
    if (anyHr)
        for (int i = 0; i < kTracks; i++)
        {
            const Track& t = t_[i];
            uint32_t cs = host_ ? host_->critEnter() : 0;
            size_t n = (isHrType(t.type) && t.hr.get()) ? t.hr.get()->serialize(buf + p + 1, cap - p - 1) : 0;
            buf[p] = (uint8_t)n;
            p += 1 + n;
            if (host_)
                host_->critExit(cs);
        }
    size_t plen = p - 8;
    buf[6] = (uint8_t)(plen & 0xFF);
    buf[7] = (uint8_t)(plen >> 8);
    uint16_t c = crc16(buf, p);
    buf[p++] = (uint8_t)(c & 0xFF);
    buf[p++] = (uint8_t)(c >> 8);
    return p;
}

// Change detection hashes the SETTINGS only. Fields that change while performing (and are not worth an autosave) are masked:
//  Chord/Arp: the lock root / minor flag follow the last played root in key mode, the visible panel is a view.
//  Mono: the edit page and the visible half are views.
// Offsets are those of serialization version 2 (see chordarp.cpp / mono.cpp); saveState always stores everything.
static_assert((int)seqgen::ChordArp::kSerialVersion == 2 && (int)seqgen::Mono::kSerialVersion == 2, "stateHash masks assume the v2 block layout");
static_assert((int)seqgen::ChordArp::kSerialSize <= (int)seqgen::Mono::kSerialSize, "stateHash uses one buffer of the Mono block size for both");
static_assert((int)seqgen::Mono::kSerialSize < 256 && (int)seqgen::ChordArp::kSerialSize < 256, "block lengths are stored in one byte");

uint16_t Engine::stateHash() const
{
    // incremental: no full-size buffer (the MK66 stack is 4 KB)
    uint8_t blk[seqgen::Mono::kSerialSize];
    uint16_t c = 0xFFFF;
    for (int i = 0; i < kTracks; i++)
    {
        const Track& t = t_[i];
        size_t nc, nm, nh;
        uint8_t type;
        uint8_t hblk[seqgen::HrScript::kMaxSerial];
        uint32_t cs = host_ ? host_->critEnter() : 0;
        type = t.type;
        nc = t.ca.serialize(blk, sizeof(blk));
        if (host_)
            host_->critExit(cs);
        if (nc >= 25 && blk[1] == 1)
            blk[2] = blk[3] = blk[4] = 0; // key mode: lock root follows the played root
        if (nc >= 25)
            blk[24] = 0; // panel
        c = crc16Update(c, &type, 1);
        c = crc16Update(c, blk, nc);
        cs = host_ ? host_->critEnter() : 0;
        nm = t.mo.serialize(blk, sizeof(blk));
        nh = (isHrType(t.type) && t.hr.get()) ? t.hr.get()->serialize(hblk, sizeof(hblk)) : 0;
        if (host_)
            host_->critExit(cs);
        if (nm >= 11)
            blk[5] = blk[10] = 0; // edit page, visible half
        c = crc16Update(c, blk, nm);
        c = crc16Update(c, hblk, nh); // HYPER_RASP script: settings only (its serialize leaves out the runtime and view state)
    }
    return c;
}

void Engine::resetState()
{
    loading_ = true;
    SEQ_COMPILER_BARRIER(); // t_ / activeCount_ are plain variables: do not let the compiler move their writes out of the guard
    for (uint8_t i = 0; i < kTracks; i++)
    {
        if (t_[i].type != TYPE_OFF)
            applyType(i, TYPE_OFF);
        initTrack(i);
    }
    hrBus_.reset();
    rec_.stop();
    recPublish();
    tail_ = head_;
    activeCount_ = 0;
    takenMask_ = 0;
    recomputeContext();
    if (cut())
        postCut(0, seqcut::packArg(seqcut::SUB_RESET, true, 0)); // project load / new: every cut row back to stock
    SEQ_COMPILER_BARRIER();
    loading_ = false;
}

bool Engine::loadState(const uint8_t* buf, size_t len)
{
    if (!buf || len < 10 || buf[0] != 'S' || buf[1] != 'Q' || buf[2] != 'T' || buf[3] != 'R' || buf[4] != kSidecarVersion ||
        buf[5] != kTracks)
        return false;
    size_t plen = (size_t)buf[6] | ((size_t)buf[7] << 8);
    size_t end = 8 + plen;
    if (end + 2 > len || end + 2 > (size_t)kSidecarMax)
        return false;
    uint16_t c = crc16(buf, end);
    if (buf[end] != (uint8_t)(c & 0xFF) || buf[end + 1] != (uint8_t)(c >> 8))
        return false;

    // pass 1: the frame must be well-formed (lengths add up); the content of a block is judged per track in pass 2
    {
        size_t p = 8;
        for (int i = 0; i < kTracks; i++)
        {
            if (p + 2 > end)
                return false;
            p++; // type
            size_t n = buf[p++];
            if (p + n + 1 > end)
                return false;
            p += n;
            n = buf[p++];
            if (p + n > end)
                return false;
            p += n;
        }
        if (p != end) // an optional trailer: one [lenHR, HR] block per track
        {
            for (int i = 0; i < kTracks; i++)
            {
                if (p + 1 > end)
                    return false;
                size_t n = buf[p++];
                if (p + n > end)
                    return false;
                p += n;
            }
            if (p != end)
                return false;
        }
    }
    // where the trailer starts (0 = none): after the 16 track records
    size_t trailer = 0;
    {
        size_t p = 8;
        for (int i = 0; i < kTracks; i++)
        {
            p++;
            p += 1 + buf[p];
            p += 1 + buf[p];
        }
        trailer = p < end ? p : 0;
    }

    // pass 2: apply; a track whose type or blocks are invalid is left Off with fresh state, the others load
    resetState();
    loading_ = true;
    SEQ_COMPILER_BARRIER();
    size_t p = 8;
    for (uint8_t i = 0; i < kTracks; i++)
    {
        uint8_t type = buf[p++];
        size_t n = buf[p++];
        bool ok = type < TYPE_COUNT;
        if (n)
            ok = t_[i].ca.deserialize(buf + p, n) && ok;
        p += n;
        n = buf[p++];
        if (n)
            ok = t_[i].mo.deserialize(buf + p, n) && ok;
        p += n;
        if (isHrType(type))
        {
            // a script type needs its trailer block; the script exists before its settings are restored (applyType keeps it)
            size_t hn = 0, hp = 0;
            if (trailer)
            {
                hp = trailer;
                for (uint8_t k = 0; k < i; k++)
                    hp += 1 + buf[hp];
                hn = buf[hp++];
            }
            if (ok)
            {
                createHr(i, type);
                if (!t_[i].hr.get() || (hn && !t_[i].hr.get()->deserialize(buf + hp, hn)))
                    ok = false;
            }
        }
        if (!ok)
            initTrack(i); // invalid block: this track Off, defaults
        else if (type != TYPE_OFF)
            applyType(i, type);
    }
    SEQ_COMPILER_BARRIER();
    loading_ = false;
    return true;
}

} // namespace seqtracks
