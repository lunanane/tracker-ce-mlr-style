// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// PolySynth pitch trace view (plan 013 V): feeds the follower from the live pool voices and refreshes the control (see pitchTrace.h).
// v0.4.1: the live read (seqPitchTraceLogic.h LiveSampler) replaces the engine trace ring: no dependency on what the audio interrupt
// latched, every voice of every instance whatever played it.
#include "seqPlay/pitchTrace.h"

#include <new>
#include <string.h>

#include "cr_section_macros.h"
#include "display.h"
#include "seqTracks.h"
#ifdef FEATURE_PLINKY
#include "PlinkyVoice/plinky_voice.h"
#include "seqPlinky.h"
#endif

PitchTraceView pitchTraceView;

namespace
{
constexpr uint32_t kRefreshMs = 40; // 25 fps at most, only while visible
// Raw storage only (an object with a constructor in EXTERNAL_RAM gets a static constructor that runs before the SDRAM is up): the follower
// is built by placement new on the first show.
__NOINIT(EXTERNAL_RAM) alignas(pitchtrace::Follower) uint8_t s_followerMem[sizeof(pitchtrace::Follower)];
}

void PitchTraceView::createControl()
{
    strControlProperties prop;
    prop.text = nullptr;
    prop.style = 0; // hidden until used
    prop.x = 0;
    prop.y = 0;
    prop.w = 800;
    prop.h = 480;
    prop.data = &data_;
    data_.kind = TraceViewData::NONE;
    data_.busyMs = 0;
    control_ = display.createControl<cSeqTraceControl>(&prop);
}

void PitchTraceView::forget()
{
    data_.kind = TraceViewData::NONE;
    control_ = -1;
}

void PitchTraceView::show(bool on, const char* hint)
{
    if (control_ < 0)
        return;
    if (!on)
    {
        if (data_.kind != TraceViewData::NONE)
        {
            data_.kind = TraceViewData::NONE;
            display.setControlHide(control_);
            display.refreshControl(control_);
            display.synchronizeRefresh();
        }
        return;
    }
    const bool was = data_.kind != TraceViewData::NONE;
    data_.kind = TraceViewData::PITCH;
    if (follower_ == nullptr)
        follower_ = new (s_followerMem) pitchtrace::Follower();
    data_.pitch = follower_;
    data_.range = &range_;
    strncpy(data_.title, "PolySynth pitch  (dim line: envelope)", sizeof(data_.title) - 1);
    data_.title[sizeof(data_.title) - 1] = 0;
    strncpy(data_.hint, hint ? hint : "", sizeof(data_.hint) - 1);
    data_.hint[sizeof(data_.hint) - 1] = 0;
    if (!was)
    {
        follower_->reset();
        sampler_.reset();
        range_ = pitchtrace::Range();
        lastMs_ = 0;
        display.setControlShow(control_);
    }
    display.refreshControl(control_);
    display.synchronizeRefresh();
}

void PitchTraceView::update()
{
    if (data_.kind != TraceViewData::PITCH || control_ < 0 || follower_ == nullptr)
        return;
    const uint32_t now = seqNowMs();
    // v0.3.9: between its two display stages the control must draw the same samples (seqTraceStages.h): the follower stays frozen
    const bool frozen = data_.busyMs != 0 && (uint32_t)(now - data_.busyMs) < 200u;
#ifdef FEATURE_PLINKY
    static_assert(pitchtrace::kVoices == plinkyvoice::kPool, "voices");
    pitchtrace::LiveVoice lv[pitchtrace::kVoices];
    for (int v = 0; v < pitchtrace::kVoices; v++)
        seqPlinkyLiveVoice(v, &lv[v].on, &lv[v].inst, &lv[v].pitch, &lv[v].env, &lv[v].serial);
    sampler_.sample(lv, now, *follower_, frozen); // every pass: one column per 5.8 ms of time
#endif
    if (frozen)
        return;
    if (lastMs_ != 0 && (uint32_t)(now - lastMs_) < kRefreshMs)
        return;
    lastMs_ = now ? now : 1;
    range_.follow(*follower_);
    display.refreshControl(control_);
    display.synchronizeRefresh();
}
