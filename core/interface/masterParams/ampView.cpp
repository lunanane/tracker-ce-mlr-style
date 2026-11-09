// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Master amplitude view (plan 013 M / 014 B / 015 V): takes the per block peaks of the tracks and the master from the audio taps, feeds the
// follower and refreshes the control (ampView.h).
#include "masterParams/ampView.h"

#include <new>
#include <string.h>

#include "cr_section_macros.h"
#include "display.h"
#include "mtAudioEngine.h"
#include "mtSequencer.h"
#include "mtStructs.h"
#include "seqTracks.h"

AmpView ampView;

namespace
{
// v0.3.7: the history of the 9 lanes (v0.4.1: about 4.1 KB each; v0.4.3: + the drawing scratch of about 20 KB) and the per block peak rings
// (64 values for every track channel and the two master channels, 2.3 KB) live in EXTERNAL_RAM. Raw storage only: an object with a constructor
// in EXTERNAL_RAM would get a static constructor that runs before the SDRAM is up (boot hang of v0.3.4); the follower is built by placement
// new in begin() (first main loop pass).
__NOINIT(EXTERNAL_RAM) alignas(ampview::Follower) uint8_t s_followerMem[sizeof(ampview::Follower)];
__NOINIT(EXTERNAL_RAM) uint16_t s_rings[ampview::kTracks * 2 + 2][PeakRing::kLen];
constexpr uint32_t kRefreshMs = 40; // feed and draw at most 25 times per second
constexpr int kMaxCatchUp = 1024;   // blocks per pass (3 s): a main loop that stood still must not burst
} // namespace

void AmpView::createControl()
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
    data_.lastComplete = false; // a new control: no display list of a last frame to measure
    data_.measured = false;
    control_ = display.createControl<cSeqTraceControl>(&prop);
}

// the first main loop pass (tick) or the Master module (start): the history starts, once; it runs from then on
void AmpView::begin()
{
    if (running_)
        return;
    if (follower_ == nullptr)
        follower_ = new (s_followerMem) ampview::Follower(); // the SDRAM is up long before the module is first entered
    follower_->reset();
    memset(s_rings, 0, sizeof(s_rings));
    for (int i = 0; i < kRings; i++)
        last_[i] = 0;
    data_.amp = follower_;
    // attach the rings: from here the audio side writes one peak per block
    for (int t = 0; t < ampview::kTracks; t++)
        for (int ch = 0; ch < 2 && ch < instrumentPlayer[t].peakTapChannels(); ch++)
            instrumentPlayer[t].setPeakTap(ch, s_rings[t * 2 + ch]);
    engine.setMasterPeakRing(0, s_rings[kRingMaster]);
    engine.setMasterPeakRing(1, s_rings[kRingMaster + 1]);
    lastMs_ = seqNowMs();
    drawnBlocks_ = 0;
    corr_ = 0;
    running_ = true;
}

// every main loop pass: the history runs in every module (v0.4.3)
void AmpView::tick()
{
    if (!running_)
        begin();
    feed(seqNowMs());
}

// the Master module is left: its controls are destroyed (the view's too); the history keeps running
void AmpView::forget()
{
    data_.kind = TraceViewData::NONE;
    data_.busyMs = 0;
    control_ = -1;
}

void AmpView::close()
{
    if (data_.kind == TraceViewData::NONE)
        return;
    data_.kind = TraceViewData::NONE;
    if (control_ >= 0)
    {
        display.setControlHide(control_);
        display.refreshControl(control_);
        display.synchronizeRefresh();
    }
}

void AmpView::toggle()
{
    if (control_ < 0)
        return;
    if (data_.kind == TraceViewData::AMP)
    {
        close();
        return;
    }
    begin(); // normally running already (cMasterParams::start); a no-op then
    data_.kind = TraceViewData::AMP;
    data_.amp = follower_;
    strncpy(data_.title, "Track levels, dB", sizeof(data_.title) - 1);
    data_.title[sizeof(data_.title) - 1] = 0;
    strncpy(data_.hint, "Amp: Close", sizeof(data_.hint) - 1);
    data_.hint[sizeof(data_.hint) - 1] = 0;
    display.setControlShow(control_);
    display.refreshControl(control_);
    display.synchronizeRefresh();
}

// v0.4.3: the step is applied by update() (never between the two display stages of a frame: both must draw the same scale)
void AmpView::jog(int16_t delta)
{
    if (data_.kind != TraceViewData::AMP || !running_ || delta == 0)
        return;
    const int p = pendingJog_ + (delta > 0 ? 1 : -1);
    pendingJog_ = (int8_t)(p > 8 ? 8 : (p < -8 ? -8 : p));
}

// the new block peaks of the rings into the history (every 40 ms at most)
void AmpView::feed(uint32_t now)
{
    if (!running_)
        return;
    if ((uint32_t)(now - lastMs_) < kRefreshMs)
        return;
    if (data_.kind == TraceViewData::AMP && data_.busyMs != 0 && (uint32_t)(now - data_.busyMs) < 200u)
        return; // v0.3.9: the control is between its two display stages, both must draw the same history (seqTraceStages.h)
    lastMs_ = now;

    // the clock is the master ring (left): its new blocks since the last pass
    const uint16_t headM = engine.masterPeakHead(0);
    uint32_t n = (uint16_t)(headM - last_[kRingMaster]);
    if (n > (uint32_t)kMaxCatchUp)
        n = kMaxCatchUp;
    if (n == 0)
        return;
    // per ring: the new values (at most the newest kLen - 4; blocks lost to a stalled main loop repeat the last value, the time axis keeps its length)
    uint16_t heads[kRings];
    for (int t = 0; t < ampview::kTracks; t++)
        for (int ch = 0; ch < 2; ch++)
            heads[t * 2 + ch] = ch < instrumentPlayer[t].peakTapChannels() ? instrumentPlayer[t].peakTapHead(ch) : last_[t * 2 + ch];
    heads[kRingMaster] = headM;
    heads[kRingMaster + 1] = engine.masterPeakHead(1);
    uint16_t held[kRings];
    for (int i = 0; i < kRings; i++)
    {
        uint32_t avail = (uint16_t)(heads[i] - last_[i]);
        if (avail > PeakRing::kLen - 4)
        {
            last_[i] = (uint16_t)(heads[i] - (PeakRing::kLen - 4));
            avail = PeakRing::kLen - 4;
        }
        held[i] = avail ? s_rings[i][(uint16_t)(last_[i] - 1) & (PeakRing::kLen - 1)] : 0;
    }
    uint32_t avail[kRings];
    for (int i = 0; i < kRings; i++)
        avail[i] = (uint16_t)(heads[i] - last_[i]);
    for (uint32_t k = 0; k < n; k++)
    {
        uint16_t lv[ampview::kLanes];
        for (int t = 0; t < ampview::kTracks; t++)
        {
            uint16_t m = 0;
            for (int ch = 0; ch < 2; ch++)
            {
                const int r = t * 2 + ch;
                if (k < avail[r])
                    held[r] = s_rings[r][(uint16_t)(last_[r] + k) & (PeakRing::kLen - 1)];
                if (held[r] > m)
                    m = held[r];
            }
            lv[t] = m;
        }
        uint16_t mm = 0;
        for (int ch = 0; ch < 2; ch++)
        {
            const int r = kRingMaster + ch;
            if (k < avail[r])
                held[r] = s_rings[r][(uint16_t)(last_[r] + k) & (PeakRing::kLen - 1)];
            if (held[r] > mm)
                mm = held[r];
        }
        lv[ampview::kTracks] = mm;
        follower_->push(lv);
    }
    for (int i = 0; i < kRings; i++)
        last_[i] = (uint16_t)(last_[i] + (avail[i] < n ? avail[i] : n));
    last_[kRingMaster] = headM; // the clock ring is consumed completely (blocks beyond kMaxCatchUp are dropped)
}

void AmpView::update(bool pageOk)
{
    if (!running_)
        begin();
    const bool shown = data_.kind == TraceViewData::AMP;
    if (shown && !pageOk)
        close();
    const uint32_t now = seqNowMs();
    feed(now);
    if (data_.kind != TraceViewData::AMP || control_ < 0)
    {
        pendingJog_ = 0;
        return; // history only: nothing to draw while the view is closed
    }
    if (data_.busyMs != 0 && (uint32_t)(now - data_.busyMs) < 200u)
        return; // the last frame is still half built
    const bool jogged = pendingJog_ != 0;
    for (; pendingJog_ > 0; pendingJog_--) follower_->jog(1);
    for (; pendingJog_ < 0; pendingJog_++) follower_->jog(-1);
    if (!jogged && (follower_->blocks() == drawnBlocks_ || (uint32_t)(now - drawMs_) < kRefreshMs))
        return;
    // v0.4.3: what the view's last complete display list really took against its own estimate (the control notes both when it starts the
    // next frame): the texts' estimate is an upper bound, the difference goes back into the budget; anything the estimate misses makes it smaller
    if (data_.measured)
    {
        int32_t c = data_.lastRam - data_.lastEst;
        c = c < -1500 ? -1500 : (c > 1500 ? 1500 : c);
        corr_ = (int16_t)c;
        data_.measured = false;
    }
    const uint32_t others = display.shownRamBytesExcept(control_);
    drawMs_ = now;
    drawnBlocks_ = follower_->blocks();
    ampview::Info& in = follower_->info;
    in.playing = sequencer.isPlay() ? 1 : 0;
    in.pattern = (uint8_t)mtProject.values.actualPattern;
    in.pos = (uint8_t)(sequencer.getActualPos() + 1);
    in.len = (uint8_t)sequencer.getPatternLength();
    in.bpm10 = (uint16_t)(sequencer.getCurrentTempo() * 10.0f + 0.5f);
    // the display list of the view takes what the other shown controls (title bar, labels; the mixer page under the panel is hidden while
    // the view is open, cMasterParams::showMixerDelayReverbScreen) leave of the frame; the lines fit it exactly (seqAmpViewLogic.h)
    follower_->budget = (uint16_t)ampview::traceBudget((int)others, corr_);
    display.refreshControl(control_);
    display.synchronizeRefresh();
}
