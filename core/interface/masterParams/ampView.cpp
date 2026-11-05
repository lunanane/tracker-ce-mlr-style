// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Master amplitude view (plan 013 M): feeds the follower with the block peaks of the final output, refreshes the control (ampView.h).
#include "masterParams/ampView.h"

#include <string.h>

#include "display.h"
#include "mtAudioEngine.h"
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
    strncpy(data_.title, "Master level, dB  (L above, R below)", sizeof(data_.title) - 1);
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
    follower_.push(now, (uint16_t)engine.takeMasterPeak(0), (uint16_t)engine.takeMasterPeak(1));
    display.refreshControl(control_);
    display.synchronizeRefresh();
}
