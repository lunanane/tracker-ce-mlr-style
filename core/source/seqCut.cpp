// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
#include "seqCut.h"
#include <string.h>

namespace seqcut {

using seqgen::Playhead;

static View noView()
{
    View v;
    v.col = 0;
    v.loopLo = 1;
    v.loopHi = 0;
    v.flags = 0;
    return v;
}

Cut::Cut() : activeMask_(0), holdMask_(0), stopMask_(0), snapMask_(0), handMask_(0), quant_(Q_STEP)
{
    memset(hand_, 0, sizeof(hand_));
    memset(stock_, 0, sizeof(stock_));
    memset(&hooks_, 0, sizeof(hooks_));
    memset(scratch_, 0, sizeof(scratch_));
    memset(rt_, 0, sizeof(rt_));
    for (int i = 0; i < kCutTracks; i++)
    {
        rt_[i].view = noView();
        views_[i] = noView();
    }
}

void Cut::setHold(uint8_t t, bool h)
{
    uint16_t m = holdMask_;
    m = h ? (uint16_t)(m | (1u << t)) : (uint16_t)(m & ~(1u << t));
    holdMask_ = m;
}

void Cut::setStopped(uint8_t t, bool s)
{
    uint16_t m = stopMask_;
    m = s ? (uint16_t)(m | (1u << t)) : (uint16_t)(m & ~(1u << t));
    stopMask_ = m;
}

void Cut::setSnap(uint8_t t, bool s)
{
    uint16_t m = snapMask_;
    m = s ? (uint16_t)(m | (1u << t)) : (uint16_t)(m & ~(1u << t));
    snapMask_ = m;
    rt_[t].snap = s;
}

void Cut::activate(uint8_t t)
{
    if ((activeMask_ >> t) & 1u) return;
    handMask_ = (uint16_t)(handMask_ & ~(1u << t));
    Playhead& p = ph_[t];
    p.reset();
    int steps = hooks_.trackSteps ? hooks_.trackSteps(hooks_.user, t) : 16;
    int pos = hooks_.trackPos ? hooks_.trackPos(hooks_.user, t) : 0;
    p.setLength(steps);
    p.setQuantise(quantSteps(quant_)); // queued jumps / loops wait for the grid (plan 008 A)
    p.restart((uint32_t)(pos + 1)); // the track continues where it is (and the grid counter with it)
    stock_[t] = (int16_t)pos;
    memset(&rt_[t], 0, sizeof(Rt));
    rt_[t].view = noView();
    activeMask_ = (uint16_t)(activeMask_ | (1u << t));
}

void Cut::deactivate(uint8_t t)
{
    activeMask_ = (uint16_t)(activeMask_ & ~(1u << t));
    snapMask_ = (uint16_t)(snapMask_ & ~(1u << t));
    setHold(t, false);
    setStopped(t, false);
    memset(&rt_[t], 0, sizeof(Rt));
    rt_[t].view = noView();
    views_[t] = noView();
}

void Cut::publish(uint8_t t)
{
    Playhead& p = ph_[t];
    Rt& r = rt_[t];
    View v = noView();
    v.col = columnOf(p.position(), p.length(), p.columns());
    int n = 0, lo = -1, hi = -1;
    for (int c = 0; c < kPosCols; c++)
        if ((r.held >> c) & 1u)
        {
            n++;
            if (lo < 0) lo = c;
            hi = c;
        }
    if (n >= 2)
    {
        v.loopLo = (uint8_t)lo;
        v.loopHi = (uint8_t)hi;
    }
    uint8_t f = VF_ACTIVE;
    if (r.stopped) f |= VF_STOPPED;
    if (p.reverse()) f |= VF_REVERSE;
    if (p.recording()) f |= VF_REC;
    if (p.recorded()) f |= VF_RECORDED;
    f |= (uint8_t)((p.rate() & 3) << VF_RATE_SHIFT);
    v.flags = f;
    r.view = v;
    views_[t] = v;
}

View Cut::view(uint8_t track) const
{
    View v = noView();
    if (track >= kCutTracks) return v;
    if (!((activeMask_ >> track) & 1u))
    {
        v.flags = (uint8_t)(1 << VF_RATE_SHIFT); // rate 1
        return v;
    }
    if ((snapMask_ >> track) & 1u)
    {
        v.flags = (uint8_t)(1 << VF_RATE_SHIFT); // SYNC ALL done: shown as stock until the boundary / Play hands the track back
        return v;
    }
    return views_[track];
}

// SYNC ALL: one pass over all tracks (so they all switch in the same audio block). Every active track drops what keeps it off the
// grid: held pads, a recorded loop or open take, rate, reverse, STOP, a queued second half-step / retrigger. Its next step boundary
// aligns it to the global position (r.resync) and, being plain then, hands it back to the stock sequencer (boundary()).
void Cut::syncAll()
{
    for (uint8_t i = 0; i < kCutTracks; i++)
    {
        if (!((activeMask_ >> i) & 1u)) continue;
        Playhead& p = ph_[i];
        Rt& r = rt_[i];
        for (int col = 0; col < kPosCols; col++)
            if ((r.held >> col) & 1u) p.release(col); // the held pads (and with them a two-pad loop gesture) end
        r.held = 0;
        p.clearRecording();
        p.setRate(Playhead::RATE_NORMAL);
        p.setReverse(false);
        r.stopped = false;
        setStopped(i, false);
        setHold(i, false);
        r.has2 = false;
        r.retrig = false;
        r.resync = true;
        publish(i);
        setSnap(i, true);
    }
}

void Cut::apply(uint8_t t, uint8_t arg)
{
    if (argSub(arg) == SUB_RESET)
    {
        if (argIsSyncAll(arg))
        {
            syncAll();
            return;
        }
        if (argIsQuant(arg))
        {
            // one grid for all tracks; gestures already queued keep waiting, they now wait for the new grid
            quant_ = argQuant(arg);
            for (uint8_t i = 0; i < kCutTracks; i++)
                if ((activeMask_ >> i) & 1u)
                    ph_[i].setQuantise(quantSteps(quant_));
            return;
        }
        if (argCol(arg) != 0) return; // col 0 = RESET, col 1 = SYNC ALL, anything else is not a command
        for (uint8_t i = 0; i < kCutTracks; i++)
            if ((activeMask_ >> i) & 1u) deactivate(i);
        return;
    }
    if (t >= kCutTracks) return;
    const uint8_t sub = argSub(arg);
    const bool down = argDown(arg);
    const uint8_t col = argCol(arg);
    if (sub >= SUB_COUNT || (sub == SUB_PAD && col >= kPosCols)) return;
    const bool wasActive = ((activeMask_ >> t) & 1u) != 0;
    // releases, Rec up and Resync mean nothing to a track the playhead does not drive
    if (!wasActive && ((sub == SUB_PAD || sub == SUB_REC) && !down)) return;
    if (!wasActive && sub == SUB_RESYNC) return;
    activate(t);
    Playhead& p = ph_[t];
    Rt& r = rt_[t];
    if (down && sub != SUB_RESYNC) setSnap(t, false); // a new gesture after SYNC ALL: the track is wanted off the grid again (a late release is not one)
    switch (sub)
    {
        case SUB_PAD:
            if (down)
            {
                p.press(col);
                r.held = (uint16_t)(r.held | (1u << col));
            }
            else
            {
                p.release(col);
                r.held = (uint16_t)(r.held & ~(1u << col));
            }
            break;
        case SUB_REC:
            if (down) p.recDown();
            else p.recUp();
            break;
        case SUB_STOP:
            if (!r.stopped)
            {
                r.stopped = true;
                setStopped(t, true);
                if (hooks_.stopNote) hooks_.stopNote(hooks_.user, t);
            }
            else
                r.resync = true; // START: back on the global grid at the next step (clears the stopped state there)
            break;
        case SUB_REVERSE: p.setReverse(!p.reverse()); break;
        case SUB_RATE:
        {
            Playhead::Rate nr = p.rate() == Playhead::RATE_HALF
                                  ? Playhead::RATE_NORMAL
                                  : (p.rate() == Playhead::RATE_NORMAL ? Playhead::RATE_DOUBLE : Playhead::RATE_HALF);
            p.setRate(nr);
            break;
        }
        case SUB_RESYNC: r.resync = true; break;
        default: break;
    }
    publish(t);
}

static bool isPlain(const Playhead& p, uint16_t held)
{
    return p.rate() == Playhead::RATE_NORMAL && !p.reverse() && !p.recorded() && !p.recording() && !p.looping() &&
           !p.pending() && held == 0;
}

void Cut::planJump(uint8_t t, int steps, uint8_t pos, StepPlan& plan)
{
    if (!hooks_.noteMap || steps < 1 || steps > (int)sizeof(scratch_)) return;
    memset(scratch_, 0, (size_t)steps);
    hooks_.noteMap(hooks_.user, t, (uint8_t)steps, scratch_);
    int noteStep = 0, since = 0;
    if (!Playhead::noteOffsetFor(pos, scratch_, steps, noteStep, since))
    {
        if (hooks_.stopNote) hooks_.stopNote(hooks_.user, t); // no note anywhere: silence
        return;
    }
    if (since == 0) return; // the step itself has the note (or the note-off): the stock step does it all
    if (scratch_[noteStep] != 1)
    {
        if (hooks_.stopNote) hooks_.stopNote(hooks_.user, t); // the last event before pos was a note-off
        return;
    }
    plan.retrigger = true;
    plan.noteStep = (uint8_t)noteStep;
    plan.elapsed = (uint8_t)(since > 255 ? 255 : since);
}

StepPlan Cut::boundary(uint8_t t, int steps, int globalPos, bool frozen)
{
    StepPlan plan;
    memset(&plan, 0, sizeof(plan));
    if (t >= kCutTracks || !((activeMask_ >> t) & 1u) || steps < 1) return plan;
    Playhead& p = ph_[t];
    Rt& r = rt_[t];
    if (frozen)
    {
        plan.hold = true; // Session STOP: the playhead stays where it is, a pending resync / retrigger waits
        return plan;
    }
    if (p.length() != steps) p.setLength(steps);
    bool resynced = false;
    if (r.resync)
    {
        r.resync = false;
        setSnap(t, false);
        r.stopped = false;
        setStopped(t, false);
        r.held = 0;
        // the track's own stock position (its own length: performance track lengths, launched clips), not the global counter
        p.resync((uint32_t)(stock_[t] >= 0 && stock_[t] < steps ? stock_[t] : globalPos % steps));
        resynced = true;
    }
    else if (r.stopped)
    {
        plan.hold = true; // the stock position keeps running underneath, nothing is triggered
        r.retrig = false;
        r.has2 = false;
        publish(t);
        return plan;
    }
    const int prev = p.position();
    Playhead::StepOut s = p.step();
    plan.override = true;
    plan.pos = s.pos;
    plan.hold = s.hold;
    r.has2 = s.has2;
    r.pos2 = s.pos2;
    r.jumped2 = s.jumped2;
    bool jumped = s.jumped && !s.hold;
    // MIDI / synth tracks: reversed running is not a jump at every step (a note must not be struck again and again)
    if (jumped && p.reverse() && hooks_.sampleTrack && !hooks_.sampleTrack(hooks_.user, t))
        jumped = s.pos != (uint8_t)((prev + steps - 1) % steps);
    if (jumped) planJump(t, steps, s.pos, plan);
    setHold(t, plan.hold);
    r.retrig = plan.retrigger;
    r.noteStep = plan.noteStep;
    r.elapsed = plan.elapsed;
    publish(t);
    if (resynced && isPlain(p, r.held))
    {
        // back on the grid with nothing special left: the stock sequencer takes over again. Its retrigger ("as if at") survives
        // the hand-back: consumeRetrigger() still serves it at the step start (hookMask keeps the audio hooks on until then).
        const bool keep = plan.retrigger;
        deactivate(t);
        if (keep)
        {
            hand_[t].noteStep = plan.noteStep;
            hand_[t].elapsed = plan.elapsed;
            handMask_ = (uint16_t)(handMask_ | (1u << t));
        }
    }
    return plan;
}

bool Cut::mid(uint8_t t, int steps, StepPlan& out, bool frozen)
{
    memset(&out, 0, sizeof(out));
    if (frozen || t >= kCutTracks || !((activeMask_ >> t) & 1u)) return false;
    Rt& r = rt_[t];
    if (!r.has2 || r.stopped) return false;
    r.has2 = false;
    out.override = true;
    out.pos = steps > 0 ? (uint8_t)(r.pos2 % steps) : r.pos2; // the pattern may have become shorter since the boundary
    if (r.jumped2) planJump(t, steps, out.pos, out);
    r.retrig = out.retrigger;
    r.noteStep = out.noteStep;
    r.elapsed = out.elapsed;
    setHold(t, false);
    return true;
}

bool Cut::consumeRetrigger(uint8_t t, StepPlan& out)
{
    if (t < kCutTracks && ((handMask_ >> t) & 1u) && !((activeMask_ >> t) & 1u))
    {
        handMask_ = (uint16_t)(handMask_ & ~(1u << t));
        memset(&out, 0, sizeof(out));
        out.retrigger = true;
        out.noteStep = hand_[t].noteStep;
        out.elapsed = hand_[t].elapsed;
        return true;
    }
    if (t >= kCutTracks || !((activeMask_ >> t) & 1u) || !rt_[t].retrig) return false;
    rt_[t].retrig = false;
    memset(&out, 0, sizeof(out));
    out.retrigger = true;
    out.noteStep = rt_[t].noteStep;
    out.elapsed = rt_[t].elapsed;
    return true;
}

void Cut::launched(uint8_t t, int steps, int pos)
{
    if (t >= kCutTracks || !((activeMask_ >> t) & 1u) || steps < 1) return;
    Rt& r = rt_[t];
    if (pos < 0 || pos >= steps) pos = 0;
    ph_[t].setLength(steps);
    ph_[t].restart((uint32_t)pos); // the next boundary() lands on pos without a jump
    stock_[t] = (int16_t)pos;
    r.stopped = false;
    setStopped(t, false);
    r.retrig = false;
    r.has2 = false;
    r.resync = false;
    setHold(t, false);
    publish(t);
}

void Cut::transportStart(int fromPos)
{
    handMask_ = 0;
    for (uint8_t t = 0; t < kCutTracks; t++)
    {
        if (!((activeMask_ >> t) & 1u)) continue;
        if (rt_[t].snap)
        {
            deactivate(t); // SYNC ALL while stopped (or Stop = panic): no boundary came, Play starts the track on the grid
            continue;
        }
        Rt& r = rt_[t];
        ph_[t].restart((uint32_t)(fromPos + 1)); // the stock sequencer plays step fromPos right now
        stock_[t] = (int16_t)fromPos;
        r.retrig = false;
        r.has2 = false;
        r.resync = false;
        setHold(t, false);
        publish(t);
    }
}

} // namespace seqcut
