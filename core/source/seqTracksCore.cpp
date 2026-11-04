// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
#include "seqTracksCore.h"
#include <string.h>

namespace seqtracks {

using namespace seqlogic;

enum
{
    CMD_PAD = 1,
    CMD_TYPE = 2
};

Engine::Engine()
  : host_(0), head_(0), tail_(0), takenMask_(0), activeCount_(0), playing_(false), barPulse_(0), idleCount_(0), usAcc_(0)
{
    memset(ring_, 0, sizeof(ring_));
    for (int i = 0; i < kTracks; i++)
    {
        Track& t = t_[i];
        t.type = TYPE_OFF;
        t.pending = 0xFF;
        t.monoSteps = false;
        t.nSounding = 0;
        memset(t.padBits, 0, sizeof(t.padBits));
        memset(t.sounding, 0, sizeof(t.sounding));
        t.ca.setChannel((uint8_t)i);
        t.mo.setChannel((uint8_t)i);
        t.ca.setSink(&Engine::sink, this);
        t.mo.setSink(&Engine::sink, this);
        t.mo.setContext(&ctx_);
    }
    recomputeContext();
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
    }
}

int Engine::heldPads(const Track& t) const
{
    int n = 0;
    for (unsigned i = 0; i < sizeof(t.padBits); i++)
        for (uint8_t b = t.padBits[i]; b; b = (uint8_t)(b & (b - 1)))
            n++;
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
    else
    {
        t.mo.pad(col, row, down);
        scanMono(t);
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
        }
    memset(t.padBits, 0, sizeof(t.padBits));
    if (t.type == TYPE_CHORDARP)
        t.ca.stop();
    else if (t.type == TYPE_MONO)
        t.mo.stop();
    releaseAll(track);

    if (t.type == TYPE_OFF)
        activeCount_++;
    t.type = type;
    if (type == TYPE_OFF)
        activeCount_--;

    recomputeContext();
    if (type == TYPE_MONO)
        scanMono(t);
    refresh(track);
    if (playing_)
    {
        if (type == TYPE_CHORDARP)
            t.ca.start();
        else if (type == TYPE_MONO)
            t.mo.start();
    }
    refresh(track);
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
            if (t.type == TYPE_CHORDARP)
                t.ca.start();
            else
                t.mo.start();
        }
        else
        {
            if (t.type == TYPE_MONO)
                t.mo.stop(); // Chord/Arp keeps running while stopped (free-running clock)
            refresh((uint8_t)i);
        }
    }
}

void Engine::pulseAll()
{
    if (playing_)
    {
        if (isBarStart(barPulse_))
            applyPending();
        barPulse_ = (uint8_t)((barPulse_ + 1) % seqgen::kBarPulses);
    }
    for (int i = 0; i < kTracks; i++)
        if (t_[i].type == TYPE_CHORDARP)
        {
            t_[i].ca.pulse();
            refresh((uint8_t)i);
        }
    if (playing_)
        for (int i = 0; i < kTracks; i++)
            if (t_[i].type == TYPE_MONO)
            {
                t_[i].mo.pulse();
                refresh((uint8_t)i);
            }
}

void Engine::stepClock(uint16_t trackUStep)
{
    if (activeCount_ == 0)
        playing_ = true; // keep the transport flag in step while no track runs
    drain();
    if (activeCount_ == 0)
        return;
    setPlaying(true);
    if (((trackUStep - 1) % 8) == 0)
        pulseAll();
}

void Engine::idleTick()
{
    if (activeCount_ == 0)
        playing_ = false;
    drain();
    if (activeCount_ == 0)
        return;
    setPlaying(false);
    if (++idleCount_ >= 96)
    {
        idleCount_ = 0;
        pulseAll();
    }
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
                t_[i].ca.tick();
                refresh((uint8_t)i);
            }
            else if (t_[i].type == TYPE_MONO)
            {
                t_[i].mo.tick();
            }
        }
    }
}

// ------------------------------------------------------------------ takeover + events
void Engine::refresh(uint8_t track)
{
    Track& t = t_[track];
    bool want = wantTakeover(t.type, heldPads(t), t.nSounding, t.monoSteps, playing_);
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
    for (int n = 0; n < 128; n++)
        if (t.sounding[n >> 3] & (1u << (n & 7)))
        {
            t.sounding[n >> 3] = (uint8_t)(t.sounding[n >> 3] & ~(1u << (n & 7)));
            if (host_)
                host_->noteOff(track, (uint8_t)n);
        }
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
        if (!(byte & bit))
        {
            byte = (uint8_t)(byte | bit);
            t.nSounding++;
        }
        host_->noteOn(track, e.note, velocityToVolume(e.vel));
    }
    else if (byte & bit)
    {
        byte = (uint8_t)(byte & ~bit);
        t.nSounding--;
        host_->noteOff(track, e.note);
    }
}

} // namespace seqtracks
