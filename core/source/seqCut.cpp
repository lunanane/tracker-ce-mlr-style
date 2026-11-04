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

Cut::Cut() : activeMask_(0), holdMask_(0), stopMask_(0)
{
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

void Cut::activate(uint8_t t)
{
    if ((activeMask_ >> t) & 1u) return;
    Playhead& p = ph_[t];
    p.reset();
    int steps = hooks_.trackSteps ? hooks_.trackSteps(hooks_.user, t) : 16;
    int pos = hooks_.trackPos ? hooks_.trackPos(hooks_.user, t) : 0;
    p.setLength(steps);
    p.restart((uint32_t)(pos + 1)); // the track continues where it is
    stock_[t] = (int16_t)pos;
    memset(&rt_[t], 0, sizeof(Rt));
    rt_[t].view = noView();
    activeMask_ = (uint16_t)(activeMask_ | (1u << t));
}

void Cut::deactivate(uint8_t t)
{
    activeMask_ = (uint16_t)(activeMask_ & ~(1u << t));
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
    return views_[track];
}

void Cut::apply(uint8_t t, uint8_t arg)
{
    if (argSub(arg) == SUB_RESET)
    {
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
        r.stopped = false;
        setStopped(t, false);
        r.held = 0;
        p.resync((uint32_t)globalPos);
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
        deactivate(t); // back on the global grid with nothing special left: the stock sequencer takes over again
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
    for (uint8_t t = 0; t < kCutTracks; t++)
    {
        if (!((activeMask_ >> t) & 1u)) continue;
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
