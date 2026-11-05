// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Master amplitude view (plan 013 M / 014 B): feeds the follower with the track levels and the master peak, refreshes the control (ampView.h).
#include "masterParams/ampView.h"

#include <string.h>

#include "display.h"
#include "mtAudioEngine.h"
#include "mtSequencer.h"
#include "mtStructs.h"
#include "seqTracks.h"

AmpView ampView;

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
    control_ = display.createControl<cSeqTraceControl>(&prop);
}

void AmpView::forget()
{
    if (data_.kind != TraceViewData::NONE)
        engine.setMasterPeakTap(false);
    data_.kind = TraceViewData::NONE;
    control_ = -1;
}

void AmpView::close()
{
    if (data_.kind == TraceViewData::NONE)
        return;
    data_.kind = TraceViewData::NONE;
    engine.setMasterPeakTap(false);
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
    follower_.reset();
    data_.kind = TraceViewData::AMP;
    data_.amp = &follower_;
    strncpy(data_.title, "Track levels, dB", sizeof(data_.title) - 1);
    data_.title[sizeof(data_.title) - 1] = 0;
    strncpy(data_.hint, "Amp view: close", sizeof(data_.hint) - 1);
    data_.hint[sizeof(data_.hint) - 1] = 0;
    engine.setMasterPeakTap(true);
    display.setControlShow(control_);
    display.refreshControl(control_);
    display.synchronizeRefresh();
}

void AmpView::update(bool pageOk)
{
    if (data_.kind != TraceViewData::AMP)
        return;
    if (!pageOk)
    {
        close();
        return;
    }
    const uint32_t now = seqNowMs();
    if (!follower_.due(now))
        return;
    // the 8 sample tracks: the RMS meters are lazy (armed by the first read, -1 until a block was measured; the read also empties them)
    uint16_t lv[ampview::kLanes];
    for (int t = 0; t < ampview::kTracks; t++)
        lv[t] = ampview::levelOfRms(instrumentPlayer[t].getRMSValue());
    const uint32_t pl = engine.takeMasterPeak(0), pr = engine.takeMasterPeak(1);
    const uint32_t pm = pl > pr ? pl : pr;
    lv[ampview::kTracks] = (uint16_t)(pm > 65535u ? 65535u : pm);
    follower_.push(now, lv);
    ampview::Info& in = follower_.info;
    in.playing = sequencer.isPlay() ? 1 : 0;
    in.pattern = (uint8_t)mtProject.values.actualPattern;
    in.pos = (uint8_t)(sequencer.getActualPos() + 1);
    in.len = (uint8_t)sequencer.getPatternLength();
    in.bpm10 = (uint16_t)(sequencer.getCurrentTempo() * 10.0f + 0.5f);
    display.refreshControl(control_);
    display.synchronizeRefresh();
}
