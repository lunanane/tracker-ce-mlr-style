// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Master fader pages (plan 024), see masterFaders.h.
#include "masterParams/masterFaders.h"

#include <stdio.h>
#include <string.h>

#include "core/interface.h"
#include "core/modulesBase.h"
#include "display.h"
#include "fileManager.h"
#include "mtAudioEngine.h"
#include "mtPadsBacklight.h"
#include "mtStructs.h"
#include "seqBeatRepeat.h"
#include "seqPerform/faderParams.h"
#include "seqTracks.h"

using namespace seqmf;

cMasterFaders masterFaders;

namespace
{
constexpr uint32_t kScreenGapMs = 80, kLedGapMs = 30;

uint8_t* levelPtr(uint8_t page)
{
    return page == MP_DELAY ? &mtProject.values.delayVolume : &mtProject.values.reverbVolume;
}

void applyLevel(uint8_t page)
{
    if (page == MP_DELAY)
        engine.refreshDelayVolume();
    else
        engine.refreshReverbVolume();
    newFileManager.setProjectStructChanged();
}

void brSettings()
{
    seqbr::Result r = { seqbr::BA_SETTINGS, 0 };
    seqBrAct(r); // pushes the values to the audio side and marks the settings file dirty
}
} // namespace

void cMasterFaders::createControl()
{
    strControlProperties prop;
    prop.colors = nullptr;
    prop.text = nullptr;
    prop.style = 0; // hidden until opened
    prop.x = 0;
    prop.y = 29;
    prop.w = 800;
    prop.h = 394;
    prop.value = 0;
    prop.data = &view_;
    control_ = display.createControl<cSeqFaderView>(&prop);
    display.setControlData(control_, &view_);
}

// the Master module is left: its controls are gone; nothing is recorded ahead any more, nothing stays dialled in
void cMasterFaders::forget()
{
    if (open_)
    {
        open_ = false;
        preroll();
        if (seqBrUi().releaseAll())
        {
            seqbr::Result r = { seqbr::BA_UP, 0 };
            seqBrAct(r);
        }
        padsBacklight.clearGlobalPads(1, 1, 1);
    }
    control_ = -1;
    st_.clear();
    retDial_ = 0;
    haveShown_ = false;
}

void cMasterFaders::open()
{
    if (control_ < 0 || open_)
        return;
    open_ = true;
    haveShown_ = false;
    memset(ledShown_, 0xFF, sizeof(ledShown_));
    display.setControlShow(control_);
    preroll();
    refreshScreen(true);
    leds();
}

void cMasterFaders::close()
{
    if (!open_)
        return;
    open_ = false;
    preroll();
    if (seqBrUi().releaseAll()) // a grid pad in the hand is let go (a latched repeat goes on)
    {
        seqbr::Result r = { seqbr::BA_UP, 0 };
        seqBrAct(r);
    }
    if (control_ >= 0)
    {
        display.setControlHide(control_);
        display.refreshControl(control_);
        display.synchronizeRefresh();
    }
    padsBacklight.clearGlobalPads(1, 1, 1);
}

// record ahead only while the beat repeat pads are on the screen
void cMasterFaders::preroll()
{
    seqBrPreroll(open_ && page_ == MP_BR);
}

void cMasterFaders::setPage(uint8_t page)
{
    if (page >= MP_COUNT || page == page_)
        return;
    if (page_ == MP_BR && seqBrUi().releaseAll())
    {
        seqbr::Result r = { seqbr::BA_UP, 0 };
        seqBrAct(r);
    }
    page_ = page;
    preroll();
    memset(ledShown_, 0xFF, sizeof(ledShown_));
    haveShown_ = false;
}

void cMasterFaders::fader(uint8_t row, uint8_t step)
{
    auto& v = mtProject.values;
    if (isFxPage(page_))
    {
        const uint8_t pg = perfPage(page_);
        if (row == 0)
        {
            const uint8_t bit = (uint8_t)(1u << (page_ == MP_DELAY ? 0 : 1));
            uint8_t* lv = levelPtr(page_);
            if (!(retDial_ & bit))
                retPre_[page_ == MP_DELAY ? 0 : 1] = *lv; // the level before the first touch
            retDial_ = (uint8_t)(retDial_ | bit);
            *lv = levelOf(step);
            applyLevel(page_);
        }
        else
        {
            st_.dialParam(pg, row, seqperf::currentParam(pg, row), (uint8_t)(v.delayParams & 0x3F));
            seqperf::applyParamStep(pg, row, step);
        }
    }
    else if (page_ == MP_HP && row < 2)
    {
        seqbr::BrParams& p = seqBrUi().prm;
        if (row == 0)
            p.hpCut = step;
        else
            p.hpRes = step;
        brSettings();
    }
}

// DISENGAGE (pad column 12): the row goes back to what it was before the fader touched it
void cMasterFaders::disengage(uint8_t row)
{
    if (isFxPage(page_))
    {
        const uint8_t pg = perfPage(page_);
        if (row == 0)
        {
            const uint8_t bit = (uint8_t)(1u << (page_ == MP_DELAY ? 0 : 1));
            if (retDial_ & bit)
            {
                *levelPtr(page_) = retPre_[page_ == MP_DELAY ? 0 : 1];
                retDial_ = (uint8_t)(retDial_ & ~bit);
                applyLevel(page_);
            }
        }
        else if (st_.paramState[pg - seqperf::PAGE_PING][row - 1] == seqperf::PS_DIALLED)
        {
            seqperf::restoreParam(pg, row, st_.pre[pg - seqperf::PAGE_PING][row - 1], st_.preRate);
            st_.disengage(pg, row);
        }
    }
    else if (page_ == MP_HP && row < 2)
    {
        seqbr::BrParams& p = seqBrUi().prm;
        if (row == 0)
            p.hpCut = 0;
        else
            p.hpRes = 1;
        brSettings();
    }
}

void cMasterFaders::padEvent(uint8_t pad, uint8_t state)
{
    const PadInfo p = mapPad(pad, page_);
    if (p.cell == MC_BR)
    {
        seqbr::Result r[2];
        const int n = seqBrUi().pad((uint8_t)(pad % 12), (uint8_t)(pad / 12), state == 1, r);
        for (int i = 0; i < n; i++)
            seqBrAct(r[i]);
        return;
    }
    if (state != 1)
        return;
    switch (p.cell)
    {
        case MC_PAGE: setPage(p.index); break;
        case MC_FADER: fader(p.index, p.step); break;
        case MC_RESET:
            if (ModuleBase::buttons->isButtonPressed(interfaceButtonShift))
            {
                for (uint8_t r = 0; r < 4; r++) // Shift: the whole page
                    disengage(r);
            }
            else
                disengage(p.index);
            break;
        case MC_PINGPONG:
        case MC_SYNC:
        {
            auto& v = mtProject.values;
            const uint8_t bit = p.cell == MC_PINGPONG ? 0x80 : 0x40;
            v.delayParams = (uint8_t)(v.delayParams ^ bit);
            engine.setDelayParams(v.delayParams);
            if (p.cell == MC_SYNC && (v.delayParams & 0x40))
                engine.setDelayTime(v.delayTime);
            newFileManager.setProjectStructChanged();
            break;
        }
        default: break;
    }
}

cMasterFaders::Result cMasterFaders::buttonEvent(uint8_t button)
{
    switch (button)
    {
        case BT_REVERB:
        case BT_DELAY:
        case BT_HP:
        case BT_BR: setPage(button); return R_RELABEL;
        case BT_SUB:
            if (page_ != MP_BR)
                return R_NONE;
            if (seqBrUi().releaseAll())
            {
                seqbr::Result r = { seqbr::BA_UP, 0 };
                seqBrAct(r);
            }
            seqBrUi().page = nextSub(seqBrUi().page);
            memset(ledShown_, 0xFF, sizeof(ledShown_));
            return R_RELABEL;
        case BT_NEXT: setPage(nextPage(page_)); return R_RELABEL;
        case BT_CLOSE: return R_CLOSE;
        default: return R_NONE;
    }
}

void cMasterFaders::labels(char out[8][10]) const
{
    static const char* const names[4] = { "Reverb", "Delay", "High-pass", "Beat rpt" };
    for (uint8_t i = 0; i < 8; i++)
        out[i][0] = 0;
    for (uint8_t i = 0; i < 4; i++)
        snprintf(out[i], 10, i == page_ ? "[%s]" : "%s", names[i]);
    if (page_ == MP_BR)
        snprintf(out[BT_SUB], 10, "%s >", seqbr::pageName(seqBrUi().page));
    snprintf(out[BT_NEXT], 10, "Next page");
    snprintf(out[BT_CLOSE], 10, "Close");
}

// the values as the screen shows them
void cMasterFaders::snapshot(Snap& s) const
{
    auto& v = mtProject.values;
    s = Snap();
    s.page = page_;
    const seqbr::Ui& ui = seqBrUi();
    if (isFxPage(page_))
    {
        const uint8_t pg = perfPage(page_);
        const uint8_t lv = *(page_ == MP_DELAY ? &v.delayVolume : &v.reverbVolume);
        s.step[0] = levelStep(lv);
        s.dialled[0] = (retDial_ >> (page_ == MP_DELAY ? 0 : 1)) & 1;
        snprintf(s.name[0], sizeof(s.name[0]), "%s", page_ == MP_DELAY ? "Delay return" : "Reverb return");
        snprintf(s.value[0], sizeof(s.value[0]), "%d %%", (int)lv);
        for (uint8_t r = 1; r < 4; r++)
        {
            s.step[r] = seqperf::paramRowStep(pg, r);
            s.dialled[r] = st_.paramState[pg - seqperf::PAGE_PING][r - 1] == seqperf::PS_DIALLED;
            seqperf::paramRowText(pg, r, s.name[r], sizeof(s.name[r]), s.value[r], sizeof(s.value[r]));
        }
    }
    s.hpCut = hpCutStep(ui.prm);
    s.hpRes = ui.prm.hpRes;
    s.pingpong = (v.delayParams & 0x80) != 0;
    s.sync = (v.delayParams & 0x40) != 0;
    s.brSub = ui.page;
    s.brMode = ui.prm.mode;
    s.brLatch = ui.latch ? 1 : 0;
    s.brHeld = ui.heldMask();
    memcpy(s.brStep, ui.prm.step, sizeof(s.brStep));
    beatrepeat::Status st;
    memset(&st, 0, sizeof(st));
    if (seqBrStatus(&st))
    {
        s.brState = st.state;
        s.brEventGrid = st.grid;
        s.repeatIdx = (uint8_t)(st.repeatIdx > 255 ? 255 : st.repeatIdx);
        const float cpu = seqBrCpuPercent() * 10.0f;
        s.cpu10 = (uint8_t)(cpu > 255.0f ? 255.0f : cpu);
    }
    s.preroll = page_ == MP_BR;
    // the page chips: a dot where something is dialled in
    if (isFxPage(page_))
    {
        bool d = false;
        for (uint8_t r = 0; r < 4; r++)
            d = d || s.dialled[r];
        s.pageDot = d ? (uint8_t)(1u << page_) : (uint8_t)0;
    }
    if (s.hpCut || s.hpRes != 1)
        s.pageDot |= 1u << MP_HP;
    if (s.brState != beatrepeat::S_IDLE || s.brLatch)
        s.pageDot |= 1u << MP_BR;
}

void cMasterFaders::refreshScreen(bool force)
{
    Snap s;
    snapshot(s);
    if (!force && haveShown_ && memcmp(&s, &shown_, sizeof(s)) == 0)
        return;
    shown_ = s;
    haveShown_ = true;
    fillView(view_, s);
    display.setControlData(control_, &view_);
    display.refreshControl(control_);
    display.synchronizeRefresh();
}

void cMasterFaders::leds()
{
    Snap s;
    snapshot(s);
    LedIn in;
    memset(&in, 0, sizeof(in));
    in.page = page_;
    for (uint8_t r = 0; r < 4; r++)
    {
        in.step[r] = s.step[r];
        in.engaged[r] = s.dialled[r];
    }
    if (page_ == MP_HP)
    {
        in.step[0] = s.hpCut;
        in.step[1] = s.hpRes;
        in.engaged[0] = s.hpCut != 0;
        in.engaged[1] = s.hpRes != 1;
    }
    in.pingpong = s.pingpong;
    in.sync = s.sync;
    in.pageDot = s.pageDot;
    if (page_ == MP_BR)
    {
        const seqbr::Ui& ui = seqBrUi();
        seqbr::LedState ls;
        memset(&ls, 0, sizeof(ls));
        ls.page = ui.page;
        ls.mode = ui.prm.mode;
        ls.latch = ui.latch ? 1 : 0;
        ls.heldMask = ui.heldMask();
        ls.eventGrid = s.brEventGrid;
        ls.state = s.brState;
        memcpy(ls.step, ui.prm.step, sizeof(ls.step));
        seqbr::fillLeds(ls, ((seqNowMs() / 250) & 1) != 0, in.br);
    }
    uint8_t out[48];
    fillLeds(in, out);
    for (int i = 0; i < 48; i++)
    {
        if (out[i] != ledShown_[i])
        {
            ledShown_[i] = out[i];
            padsBacklight.setBackLayer(out[i] > 0 ? 1 : 0, out[i], i);
        }
    }
}

void cMasterFaders::update()
{
    if (!open_)
        return;
    const uint32_t now = seqNowMs();
    if ((uint32_t)(now - ledMs_) >= kLedGapMs)
    {
        ledMs_ = now;
        leds();
    }
    if ((uint32_t)(now - drawMs_) >= kScreenGapMs)
    {
        drawMs_ = now;
        refreshScreen(false);
    }
}
