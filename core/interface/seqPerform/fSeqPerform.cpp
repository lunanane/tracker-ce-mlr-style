// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
#include "seqPerform/seqPerform.h"

#include <stdio.h>
#include <string.h>

#include "core/interface.h"
#include "fileManager.h"
#include "mtAudioEngine.h"
#include "mtConfig.h"
#include "mtPadBoard.h"
#include "mtPadsBacklight.h"
#include "mtSequencer.h"
#include "mtStructs.h"
#include "performanceMode/performanceEngine.h"
#include "performanceMode/performanceFx.h"
#include "performanceMode/performanceMode.h"
#include "seqTracks.h"
#include "seqFx.h"

using namespace seqperf;

cSeqPerform seqPerform;
static cSeqPerform* SP = &seqPerform;
static cPerformanceMode* PM = &performanceMode;

static void functSwitchModule(uint8_t button);
static void functPerformanceButton(uint8_t state);
static void functActionButton(uint8_t button, uint8_t state);
static void functPads(uint8_t pad, uint8_t state, int16_t velo);
static void functEncoder(int16_t value);
static void functPlayAction();

static const char* const kRateNames[19] = { "8",    "6",   "4",   "3",    "2",    "3/2",  "1",
                                            "3/4",  "1/2", "3/8", "1/3",  "1/4",  "3/16", "1/6",
                                            "1/8",  "1/12", "1/16", "1/24", "1/32" };

static void projectChanged()
{
    newFileManager.setProjectStructChanged();
}

// PING page, column 10, rows 3 and 4 (CELL_NONE in seqperf::mapPad): ping-pong on / off, tempo sync on / off
static constexpr uint8_t kPadPingPong = 2 * 12 + 9;
static constexpr uint8_t kPadSync = 3 * 12 + 9;

//----------------------------------------------------------------------------------------------
// Entry. The fader state (dialled-in FX) is NOT saved with the project and starts off. It lives in this long-lived object so FX
// that were left running (Pattern / Song views) can be picked up again; everything else starts clean from the perform data.
//----------------------------------------------------------------------------------------------
void cSeqPerform::prepareFromElsewhere()
{
    fromElsewhere = true;
    if (mtConfig.general.performanceSource < performanceDataSourceCount)
        performance.readPerfData(mtConfig.general.performanceSource);
}

void cSeqPerform::start(uint32_t options)
{
    (void)options;
    moduleRefresh = 1;

    ip->setButtonObj(interfaceButtonPerformance, functPerformanceButton); // Shift + press = the stock page
    ip->setButtonObj(interfaceButtonParams, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonFile, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonConfig, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonMaster, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSamplePlay, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleEdit, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleRec, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleLoad, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSong, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonPattern, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonGame, buttonPress, functSwitchModule);

    ip->setButtonObj(interfaceButtonPlay, buttonPress, functPlayAction);

    ip->setButtonObj(interfaceButton0, functActionButton);
    ip->setButtonObj(interfaceButton1, functActionButton);
    ip->setButtonObj(interfaceButton2, functActionButton);
    ip->setButtonObj(interfaceButton3, functActionButton);
    ip->setButtonObj(interfaceButton4, functActionButton);
    ip->setButtonObj(interfaceButton5, functActionButton);
    ip->setButtonObj(interfaceButton6, functActionButton);
    ip->setButtonObj(interfaceButton7, functActionButton);

    ip->setPotObj(interfacePot0, functEncoder);
    ip->setPadsGlobal(functPads);

    for (int i = 0; i < 48; i++)
        ledShown[i] = 0xFF;

    // like cPerformanceMode::start: the perform data (slots / FX) of the active source
    if (mtConfig.general.performanceSource < performanceDataSourceCount)
        performance.readPerfData(mtConfig.general.performanceSource);

    const bool handedOver = fromStock; // set by seqFaderShiftFromStock (Shift + Performance on the stock page)
    fromStock = false;
    flushSave();
    PM->dontClearPerformanceMode = 0; // set again only when we hand over to the stock page / Master / Params
    const bool entered = fromElsewhere;
    fromElsewhere = false;

    // FX that were left running (plan 005): pick them up if nothing turned performance mode off meanwhile and the slots still
    // sound what we remember; whatever is gone is dropped
    bool resume = false;
    if (fxKept && !handedOver)
    {
        for (uint8_t slot = 0; slot < 12; slot++)
        {
            uint8_t p = slot / 4, r = slot % 4;
            if (st.fxStep[p][r] && performance.getSlotTempValue(slot) != fxVal[slot])
            {
                st.fxStep[p][r] = 0;
                st.latch[p][r] = false;
                fxVal[slot] = 0;
                stockOn &= (uint16_t)~(1u << slot);
            }
        }
        resume = canResume(fxKept, sequencer.isPerformanceMode() && sequencer.seqPerfOwners.has(seqlogic::PERF_OWNER_FX),
                           st.anyEngaged());
    }
    fxKept = false;

    if (!resume)
    {
        st.clear();
        memset(fxVal, 0, sizeof(fxVal));
        memset(stockVal, 0, sizeof(stockVal));
        stockOn = 0;
        page = PAGE_FX1;
        if (!handedOver)
        {
            // nothing handed over: every track plays the actual pattern, so a stale per-track pattern cannot make
            // isPerformancePatternChanged() reload the pattern from SD
            for (uint8_t a = 0; a < kPerformanceTracks; a++)
                mtProject.perfTracksPatterns[a] = mtProject.values.actualPattern;
        }
        if (entered && !handedOver)
        {
            // as the stock page does when it starts with nothing handed over: no rows selected, no track affected
            performance.resetSelectedRows();
            for (uint8_t t = 0; t < kPerformanceTracks; t++)
                performance.setTrackState(t, 0);
        }
        performance.loadSelectedValuesToTempValues();
        // The stock page latches the pad row of a slot: FX already playing from it show up as lit faders
        for (uint8_t slot = 0; slot < 12; slot++)
        {
            int16_t mn, mx;
            int16_t v = performance.getSlotTempValue(slot);
            if (v != 0 && performance.getSelectedRow(slot) > 0 && performance.getSlotRange(slot, mn, mx))
            {
                stockOn |= (uint16_t)(1u << slot);
                stockVal[slot] = v;
                fxVal[slot] = v;
                st.fxStep[slot / 4][slot % 4] = nearestFxStep(v, mn, mx);
            }
        }
    }
    // the perform FX own performance mode (plan 005 C1); the launcher may own it as well. Idempotent: it also cancels a pending
    // turn-off at the pattern end that an earlier page exit may have armed.
    sequencer.enterPerformanceMode();

    padHeld = 0;
    shiftFromEntry = buttons->isButtonPressed(interfaceButtonShift); // still held from the entry press: ignored until released
    shiftView = false;
    leaveKind = LEAVE_END;
    shownKey = 0xFFFFFFFF;
    screenDirty = true;

    titleBar->show();
    titleBar->setTitle("Perform faders");

    refreshScreen();
    refreshPads();
}

void cSeqPerform::stop()
{
    moduleRefresh = 0;
    uint8_t kind = leaveKind;
    leaveKind = LEAVE_END;

    if (kind == LEAVE_KEEP_RUNNING && st.anyEngaged())
    {
        // plan 005: dialled-in FX keep sounding into Play mode / clip launcher / MLR; only the pads held right now end
        releaseMomentary();
        fxKept = true;
    }
    else
    {
        releaseAll();
        fxKept = false;
        // like cPerformanceMode::stop: when the target does not keep the perform FX they end here
        if (!PM->dontClearPerformanceMode)
        {
            // fader FX never change pattern data: reload only when per-track performance patterns were loaded
            const bool reload = sequencer.isPerformancePatternChanged();
            if (performance.turnOff() && reload)
            {
                newFileManager.loadWorkspacePatternNow(mtProject.values.actualPattern);
                sequencer.switchRamPatternsNow();
            }
            mtPadBoard.releaseAllInstruments();
        }
    }
    flushSave(); // only when the perform data changed, and never while playing (plan 005 M2)
    padsBacklight.clearGlobalPads(1, 1, 1);
}

void cSeqPerform::flushSave()
{
    if (saveGate.take(sequencer.isPlay()))
        performance.forceSaveConfig();
}

bool cSeqPerform::shiftHeld()
{
    bool pressed = buttons->isButtonPressed(interfaceButtonShift);
    if (shiftFromEntry)
    {
        if (!pressed)
            shiftFromEntry = false;
        return false;
    }
    return pressed;
}

//----------------------------------------------------------------------------------------------
void cSeqPerform::update()
{
    bool shift = shiftHeld();
    if (shift != shiftView)
    {
        shiftView = shift;
        screenDirty = true;
    }

    refreshScreen();

    static elapsedMillis ledTimer;
    if (ledTimer > 30)
    {
        ledTimer = 0;
        refreshPads();
    }
}

//----------------------------------------------------------------------------------------------
// fader logic
//----------------------------------------------------------------------------------------------
// project value -> step (1..8) shown by a PING / REV fader
uint8_t cSeqPerform::rowStep(uint8_t row) const
{
    auto& v = mtProject.values;
    if (page <= PAGE_FX3)
        return st.fxStep[page][row];
    if (row == 0)
        return st.sendStep[page - PAGE_PING];
    if (page == PAGE_PING)
    {
        if (row == 1)
            return nearestStep((v.delayParams & 0x40) ? P_RATE : P_TIME,
                               (v.delayParams & 0x40) ? (double)(v.delayParams & 0x3F) : (double)v.delayTime);
        if (row == 2)
            return nearestStep(P_FEEDBACK, v.delayFeedback);
        return nearestStep(P_FILTER, v.delayFilterFrequency);
    }
    if (row == 1)
        return nearestStep(P_SIZE, v.reverb.size);
    if (row == 2)
        return nearestStep(P_DAMP, v.reverb.damp);
    return nearestStep(P_PREDELAY, v.reverb.predelay);
}

// Real project values, set like the Master page does (same engine setters, project marked changed).
static void setParamStep(uint8_t pg, uint8_t row, uint8_t step)
{
    auto& v = mtProject.values;
    if (pg == PAGE_PING)
    {
        if (row == 1)
        {
            if (v.delayParams & 0x40)
            {
                v.delayParams = (uint8_t)((v.delayParams & 0xC0) | (uint8_t)paramValue(P_RATE, step));
                engine.setDelayParams(v.delayParams);
            }
            else
            {
                v.delayTime = (uint16_t)paramValue(P_TIME, step);
                engine.setDelayTime(v.delayTime);
            }
        }
        else if (row == 2)
        {
            v.delayFeedback = (uint8_t)paramValue(P_FEEDBACK, step);
            engine.setDelayFeedback(v.delayFeedback);
        }
        else
        {
            v.delayFilterFrequency = (uint16_t)paramValue(P_FILTER, step);
            engine.setDelayFilterFrequency(v.delayFilterFrequency);
        }
    }
    else
    {
        float f = (float)paramValue(row == 1 ? P_SIZE : (row == 2 ? P_DAMP : P_PREDELAY), step);
        if (row == 1)
        {
            v.reverb.size = f;
            engine.setReverbSize(f);
        }
        else if (row == 2)
        {
            v.reverb.damp = f;
            engine.setReverbDamp(f);
        }
        else
        {
            v.reverb.predelay = f;
            engine.setReverbPredelay(f);
        }
    }
    projectChanged();
}

// Current project value of a PING / REV fader 2-4 (what DISENGAGE restores later).
static float currentParam(uint8_t pg, uint8_t row)
{
    auto& v = mtProject.values;
    if (pg == PAGE_PING)
        return row == 1 ? (float)v.delayTime : (row == 2 ? (float)v.delayFeedback : (float)v.delayFilterFrequency);
    return row == 1 ? v.reverb.size : (row == 2 ? v.reverb.damp : v.reverb.predelay);
}

// DISENGAGE of a dialled project-value row: the value it had before the fader touched it comes back. Only what differs is set
// (no needless delay-time jump). PING fader 2 restores time and rate index and keeps the current ping-pong / sync bits.
static void restoreParam(uint8_t pg, uint8_t row, float pre, uint8_t preRate)
{
    auto& v = mtProject.values;
    if (pg == PAGE_PING)
    {
        if (row == 1)
        {
            if (v.delayTime != (uint16_t)pre)
            {
                v.delayTime = (uint16_t)pre;
                engine.setDelayTime(v.delayTime);
            }
            uint8_t np = (uint8_t)((v.delayParams & 0xC0) | (preRate & 0x3F));
            if (np != v.delayParams)
            {
                v.delayParams = np;
                engine.setDelayParams(v.delayParams);
            }
        }
        else if (row == 2)
        {
            if (v.delayFeedback != (uint8_t)pre)
            {
                v.delayFeedback = (uint8_t)pre;
                engine.setDelayFeedback(v.delayFeedback);
            }
        }
        else if (v.delayFilterFrequency != (uint16_t)pre)
        {
            v.delayFilterFrequency = (uint16_t)pre;
            engine.setDelayFilterFrequency(v.delayFilterFrequency);
        }
    }
    else if (row == 1)
    {
        v.reverb.size = pre;
        engine.setReverbSize(pre);
    }
    else if (row == 2)
    {
        v.reverb.damp = pre;
        engine.setReverbDamp(pre);
    }
    else
    {
        v.reverb.predelay = pre;
        engine.setReverbPredelay(pre);
    }
    projectChanged();
}

void cSeqPerform::faderPress(uint8_t row, uint8_t step)
{
    if (row >= 4 || step < 1 || step > 8)
        return;
    if (page <= PAGE_FX3)
    {
        uint8_t slot = slotOf(page, row);
        int16_t mn, mx;
        if (!performance.getSlotRange(slot, mn, mx))
            return; // empty slot: nothing to play
        st.fxStep[page][row] = step;
        fxVal[slot] = fxValue(step, mn, mx);
        performance.setSlotValueDirect(slot, fxVal[slot]);
        syncKnockedOut(slot);
    }
    else if (row == 0)
    {
        st.sendStep[page - PAGE_PING] = step;
        perfApplySend(page == PAGE_REV, sendValue(step));
    }
    else
    {
        st.dialParam(page, row, currentParam(page, row), (uint8_t)(mtProject.values.delayParams & 0x3F)); // remembers the value before the first touch
        setParamStep(page, row, step);
    }
    screenDirty = true;
}

// One fader row of a page back to neutral (momentary release, LATCH off, page change). Stock-latched rows go back to their stock
// value, not to silence.
static void releaseFxRow(cSeqPerform* s, uint8_t pg, uint8_t row)
{
    if (pg <= PAGE_FX3)
    {
        uint8_t slot = slotOf(pg, row);
        int16_t mn, mx;
        if (s->stockOn & (1u << slot))
        {
            s->fxVal[slot] = s->stockVal[slot];
            if (performance.getSlotRange(slot, mn, mx))
                s->st.fxStep[pg][row] = nearestFxStep(s->stockVal[slot], mn, mx);
            performance.setSlotValueDirect(slot, s->stockVal[slot]);
        }
        else if (s->st.fxStep[pg][row] != 0 || s->fxVal[slot] != 0)
        {
            s->st.fxStep[pg][row] = 0;
            s->fxVal[slot] = 0;
            performance.releaseSlotDirect(slot);
        }
    }
    else if (row == 0 && s->st.sendStep[pg - PAGE_PING] != 0)
    {
        s->st.sendStep[pg - PAGE_PING] = 0;
        perfApplySend(pg == PAGE_REV, 0);
    }
}

void cSeqPerform::releaseRow(uint8_t row)
{
    releaseFxRow(this, page, row);
    reapplyActive(); // a stock send FX / another fader may share the per-track override that was just cleared
    screenDirty = true;
}

void cSeqPerform::faderRelease(uint8_t row)
{
    // other pads of this fader still held: the value falls back to the highest of them
    for (int c = 7; c >= 0; c--)
    {
        if (padHeld & (1ULL << padOf(row, (uint8_t)c)))
        {
            faderPress(row, (uint8_t)(c + 1));
            return;
        }
    }
    // momentary like the stock page, except LATCH; project values (PING / REV faders 2-4) always stay
    bool momentary = page <= PAGE_FX3 || row == 0;
    if (momentary && !st.latch[page][row])
        releaseRow(row);
}

// DISENGAGE (pad column 12): this row's effect goes back to "not dialled in". Nothing is remembered: FX off (the stock pad row
// too), send off, LATCH off. A dialled project-value row (PING / REV faders 2-4) gets back the value it had before the fader.
void cSeqPerform::faderDisengage(uint8_t row)
{
    if (row >= 4)
        return;
    for (uint8_t c = 0; c < 8; c++)
        padHeld &= ~(1ULL << padOf(row, c)); // a pad of this fader still held must not bring it back on release
    st.latch[page][row] = false;
    if (page <= PAGE_FX3)
    {
        uint8_t slot = slotOf(page, row);
        if (stockOn & (1u << slot))
        {
            stockOn &= (uint16_t)~(1u << slot);
            stockVal[slot] = 0;
            performance.setSelectedRow(slot, 0);
            saveGate.mark(); // the perform data changed: saved once the transport is stopped
        }
        if (st.fxStep[page][row] != 0 || fxVal[slot] != 0)
        {
            fxVal[slot] = 0;
            performance.releaseSlotDirect(slot);
        }
    }
    else if (row == 0)
    {
        if (st.sendStep[page - PAGE_PING] != 0)
            perfApplySend(page == PAGE_REV, 0);
    }
    else if (st.paramState[page - PAGE_PING][row - 1] == PS_DIALLED)
    {
        restoreParam(page, row, st.pre[page - PAGE_PING][row - 1], st.preRate); // back to the value before the fader touched it
    }
    st.disengage(page, row);
    reapplyActive();
    screenDirty = true;
}

void cSeqPerform::setPage(uint8_t newPage)
{
    if (newPage == page || newPage >= PAGE_COUNT)
        return;
    // pads still held end here: momentary faders of the old page are released, latched ones stay
    padHeld = 0;
    for (uint8_t r = 0; r < 4; r++)
    {
        bool momentary = page <= PAGE_FX3 || r == 0;
        if (momentary && !st.latch[page][r])
            releaseFxRow(this, page, r);
    }
    reapplyActive();
    page = newPage;
    for (int i = 0; i < 48; i++)
        ledShown[i] = 0xFF;
    screenDirty = true;
}

// A track was selected: the active (latched / held) perform FX and sends must reach it too
void cSeqPerform::reapplyActive()
{
    for (uint8_t p = 0; p <= PAGE_FX3; p++)
    {
        for (uint8_t r = 0; r < 4; r++)
        {
            uint8_t slot = slotOf(p, r);
            if (st.fxStep[p][r] && fxVal[slot] != 0)
                performance.setSlotValueDirect(slot, fxVal[slot]);
        }
    }
    for (uint8_t i = 0; i < 2; i++)
    {
        if (st.sendStep[i])
            perfApplySend(i == 1, sendValue(st.sendStep[i]));
    }
}

// Slots the engine switched off because another slot took the same FX: their faders go dark
void cSeqPerform::syncKnockedOut(uint8_t keepSlot)
{
    for (uint8_t s = 0; s < 12; s++)
    {
        if (s == keepSlot || fxVal[s] == 0 || st.fxStep[s / 4][s % 4] == 0)
            continue;
        if (performance.getSlotTempValue(s) == 0)
        {
            fxVal[s] = 0;
            st.fxStep[s / 4][s % 4] = 0;
            st.latch[s / 4][s % 4] = false;
            stockOn &= (uint16_t)~(1u << s);
        }
    }
}

// Leaving the page for a target that does not keep our FX: everything we dialled in ends (the stock page's own rows stay as the
// stock page expects them).
void cSeqPerform::releaseAll()
{
    for (uint8_t p = 0; p <= PAGE_FX3; p++)
    {
        for (uint8_t r = 0; r < 4; r++)
        {
            uint8_t slot = slotOf(p, r);
            if (stockOn & (1u << slot))
            {
                performance.setSlotValueDirect(slot, stockVal[slot]); // the stock page's own state stays
            }
            else if (st.fxStep[p][r] || fxVal[slot])
            {
                st.fxStep[p][r] = 0;
                fxVal[slot] = 0;
                performance.releaseSlotDirect(slot);
            }
        }
    }
    for (uint8_t i = 0; i < 2; i++)
    {
        if (st.sendStep[i])
        {
            st.sendStep[i] = 0;
            perfApplySend(i == 1, 0);
        }
    }
    st.clear();
    padHeld = 0;
}

// Leaving for Pattern / Song (Play mode, clip launcher, MLR): only what a held pad plays ends; latched rows and the rows the
// stock page had latched keep sounding.
void cSeqPerform::releaseMomentary()
{
    // a stock-latched row under a held pad goes back to its stock value (it keeps sounding, but not at the held value)
    if (page <= PAGE_FX3)
    {
        for (uint8_t r = 0; r < 4; r++)
        {
            bool held = false;
            for (uint8_t c = 0; c < 8; c++)
                held = held || (padHeld & (1ULL << padOf(r, c)));
            if (held && !st.latch[page][r] && (stockOn & (1u << slotOf(page, r))))
                releaseFxRow(this, page, r);
        }
    }
    padHeld = 0;
    const uint16_t drop = st.dropOnLeave(stockOn);
    for (uint8_t slot = 0; slot < 12; slot++)
    {
        if (drop & (1u << slot))
            releaseFxRow(this, slot / 4, slot % 4);
    }
    for (uint8_t i = 0; i < 2; i++)
    {
        if (drop & (1u << (12 + i)))
            releaseFxRow(this, (uint8_t)(PAGE_PING + i), 0);
    }
    reapplyActive();
}

// Every perform FX we hold ends, the faders go back to "not dialled in". alsoStockRows: the pad rows the stock page had
// latched (handed over to the fader page) end as well.
void cSeqPerform::endEngaged(bool alsoStockRows)
{
    for (uint8_t slot = 0; slot < 12; slot++)
    {
        uint8_t p = slot / 4, r = slot % 4;
        bool stock = (stockOn & (1u << slot)) != 0;
        if (stock && alsoStockRows)
        {
            stockOn &= (uint16_t)~(1u << slot);
            stockVal[slot] = 0;
            performance.setSelectedRow(slot, 0);
            saveGate.mark(); // the perform data changed: saved once the transport is stopped
            performance.releaseSlotDirect(slot);
        }
        else if (st.fxStep[p][r] || fxVal[slot])
        {
            performance.releaseSlotDirect(slot);
        }
        fxVal[slot] = 0;
    }
    for (uint8_t i = 0; i < 2; i++)
    {
        if (st.sendStep[i])
            perfApplySend(i == 1, 0);
    }
    for (uint8_t p = 0; p < 3; p++)
        for (uint8_t r = 0; r < 4; r++)
            st.fxStep[p][r] = 0;
    st.sendStep[0] = st.sendStep[1] = 0;
    for (uint8_t p = 0; p < PAGE_COUNT; p++)
        for (uint8_t r = 0; r < 4; r++)
            st.latch[p][r] = false;
    padHeld = 0;
}

// Transport Stop (plan 005 M6): every perform FX we hold ends. Project values (PING / REV time, size ...) stay.
void cSeqPerform::panic()
{
    flushSave(); // the transport is stopped now: perform data deferred while playing is written
    const bool onScreen = mtInterface.isSeqPerformScreenActive();
    if (!onScreen && !fxKept)
        return; // nothing of ours is running (the stock page's own rows are not ours)
    endEngaged(true);
    if (onScreen)
    {
        for (int i = 0; i < 48; i++)
            ledShown[i] = 0xFF;
        screenDirty = true;
        return;
    }
    fxKept = false;
    // fader FX never change pattern data: no reload (it would drop unsaved pattern edits) unless per-track performance patterns
    // were loaded, and then only when the file manager is idle
    const bool reload = sequencer.isPerformancePatternChanged();
    if (performance.turnOff() && reload && newFileManager.isIdleForScratch())
    {
        newFileManager.loadWorkspacePatternNow(mtProject.values.actualPattern);
        sequencer.switchRamPatternsNow();
    }
}

// A project was loaded: whatever we held is gone with the old project. No pattern reload.
void cSeqPerform::projectLoaded()
{
    const bool owner = sequencer.seqPerfOwners.has(seqlogic::PERF_OWNER_FX);
    if (fxKept || owner || st.anyEngaged() || stockOn)
    {
        endEngaged(true);
        fxKept = false;
        if (owner && !mtInterface.isSeqPerformScreenActive())
            performance.turnOff();
    }
    st.clear();
    memset(fxVal, 0, sizeof(fxVal));
    memset(stockVal, 0, sizeof(stockVal));
    stockOn = 0;
    saveGate.dirty = false;
    screenDirty = true;
}

// Kept FX followed on from Pattern / Song to a page that does not keep them: the stock rule (at the pattern end while playing,
// else now). interface.cpp switchModuleByButton asks for this.
void cSeqPerform::endKeptStockRule()
{
    if (!fxKept)
        return;
    fxKept = false;
    st.clear();
    memset(fxVal, 0, sizeof(fxVal));
    memset(stockVal, 0, sizeof(stockVal));
    stockOn = 0;
    if (!sequencer.isPerformanceMode())
        return;
    if (sequencer.isPlay())
    {
        PM->turnOffAtEndOfPattern();
    }
    else
    {
        const bool reload = sequencer.isPerformancePatternChanged();
        if (performance.turnOff() && reload && newFileManager.isIdleForScratch())
        {
            newFileManager.loadWorkspacePatternNow(mtProject.values.actualPattern);
            sequencer.switchRamPatternsNow();
        }
    }
}

// The stock Performance page starts fresh: whatever we kept running ends before it resets its rows and track states.
void cSeqPerform::stockEntered()
{
    if (!fxKept)
        return;
    endEngaged(true);
    fxKept = false;
}

void cSeqPerform::padEvent(uint8_t pad, uint8_t state)
{
    PadInfo p = mapPad(pad);
    if (state == 1)
    {
        switch (p.cell)
        {
            case CELL_FADER:
                padHeld |= (1ULL << pad);
                faderPress(p.index, p.step);
                break;
            case CELL_GROUP:
            case CELL_PING:
            case CELL_REV: setPage(pageForCell(p.cell, p.index, page)); break;
            case CELL_LATCH:
                if (page > PAGE_FX3 && p.index > 0)
                    break; // project values stay anyway: LATCH only exists for FX and the send fader
                st.latch[page][p.index] = !st.latch[page][p.index];
                if (!st.latch[page][p.index])
                {
                    bool held = false;
                    for (uint8_t c = 0; c < 8; c++)
                        held = held || (padHeld & (1ULL << padOf(p.index, c)));
                    bool momentary = page <= PAGE_FX3 || p.index == 0;
                    if (!held && momentary)
                        releaseRow(p.index);
                }
                screenDirty = true;
                break;
            case CELL_DISENGAGE:
                if (shiftHeld())
                {
                    for (uint8_t r = 0; r < 4; r++) // Shift: the whole page
                        faderDisengage(r);
                }
                else
                {
                    faderDisengage(p.index);
                }
                break;
            default:
                // plan 007: the two free pads of column 10 on the PING page (they used to be the jog wheel): ping-pong and sync
                if (page == PAGE_PING && (pad == kPadPingPong || pad == kPadSync))
                {
                    auto& v = mtProject.values;
                    const uint8_t bit = pad == kPadPingPong ? 0x80 : 0x40;
                    v.delayParams = (uint8_t)(v.delayParams ^ bit);
                    engine.setDelayParams(v.delayParams);
                    if (pad == kPadSync && (v.delayParams & 0x40))
                        engine.setDelayTime(v.delayTime);
                    projectChanged();
                    screenDirty = true;
                }
                break;
        }
    }
    else if (state == 0 && p.cell == CELL_FADER)
    {
        if (padHeld & (1ULL << pad))
        {
            padHeld &= ~(1ULL << pad);
            faderRelease(p.index);
        }
    }
}

void cSeqPerform::trackButton(uint8_t button)
{
    bool shift = shiftHeld();
    uint8_t track = (uint8_t)(button + ((PM->tracksPage ? 1 : 0) ^ (shift ? 1 : 0)) * 8);
    if (track >= kPerformanceTracks)
        return;
    performance.toggleTrackState(track);
    if (performance.getTrackState(track))
        reapplyActive();
    screenDirty = true;
}

// Jog wheel (the screen buttons select tracks). Plan 007: PING: jog = the delay bandpass centre (the same value as fader 4),
// Shift + jog = its width (1/3 .. 3 octaves). REV: jog = the reverb highpass (low cut), Shift + jog = diffusion. Ping-pong and
// tempo sync moved to two pads of the PING page (padEvent).
void cSeqPerform::jog(int16_t value)
{
    auto& v = mtProject.values;
    if (value == 0)
        return;
    if (page == PAGE_PING)
    {
        if (shiftHeld())
        {
            seqFxSetBandwidth(seqfx::jogWidth(seqFxBandwidth(), value)); // saved with the project (sidecar), never written while playing
        }
        else
        {
            v.delayFilterFrequency = seqfx::jogCenter(v.delayFilterFrequency, value);
            engine.setDelayFilterFrequency(v.delayFilterFrequency);
            projectChanged();
        }
    }
    else if (page == PAGE_REV)
    {
        if (shiftHeld())
        {
            float d = v.reverb.diffusion + 0.0085f * value;
            if (d > REVERB_DIFFUSION_MAX)
                d = REVERB_DIFFUSION_MAX;
            if (d < REVERB_DIFFUSION_MIN)
                d = REVERB_DIFFUSION_MIN;
            v.reverb.diffusion = d;
            engine.setReverbDiffusion(d);
            projectChanged();
        }
        else
        {
            seqFxSetHighpass(seqfx::jogHp(seqFxHighpass(), value));
        }
    }
    screenDirty = true;
}

// Shift + Performance: to the stock Performance page (FX state stays as the stock page expects it)
void cSeqPerform::leaveToStock()
{
    leaveKind = LEAVE_HAND_OVER;
    releaseAll();
    PM->dontClearPerformanceMode = 1;
    // stock state and sound must match: reapply the selected pad rows on the selected tracks
    for (uint8_t t = 0; t < kPerformanceTracks; t++)
    {
        if (performance.getTrackState(t))
            performance.loadAllSlotsFxOnTrack(t);
    }
    uint8_t b = interfaceButtonPerformance;
    eventFunct(eventSwitchModule, this, &b, 0);
}

// Any other module button. Pattern / Song keep dialled-in FX running (Play mode, clip launcher, MLR); Master / Params hand the
// performance mode on like the stock page; the rest ends the FX like the stock page (at the pattern end while playing).
void cSeqPerform::leaveTo(uint8_t button)
{
    Dest d = DEST_OTHER;
    if (button == interfaceButtonMaster || button == interfaceButtonParams)
        d = DEST_STOCK_KEEPS;
    else if (button == interfaceButtonPattern || button == interfaceButtonSong)
        d = DEST_SEQ_VIEWS;
    leaveKind = leaveFor(d, st.anyEngaged());

    if (leaveKind != LEAVE_KEEP_RUNNING && sequencer.isPerformanceMode())
    {
        if (PM->canPerformanceModeContinueAfterSwitchingTo(button))
            PM->dontClearPerformanceMode = 1;
        else if (sequencer.isPlay())
            PM->turnOffAtEndOfPattern();
        else
            PM->turnOff();
    }
    eventFunct(eventSwitchModule, this, &button, 0);
}

//----------------------------------------------------------------------------------------------
void cSeqPerform::refreshPads()
{
    uint8_t out[48];
    for (int i = 0; i < 48; i++)
        out[i] = 0;
    for (uint8_t row = 0; row < 4; row++)
    {
        bool usable = true;
        if (page <= PAGE_FX3)
        {
            int16_t mn, mx;
            usable = performance.getSlotRange(slotOf(page, row), mn, mx);
        }
        uint8_t mask = usable ? barMask(rowStep(row)) : 0;
        for (uint8_t c = 0; c < 8; c++)
        {
            out[padOf(row, c)] = (mask & (1u << c)) ? 24 : 0;
        }
        out[padOf(row, 10)] = (page > PAGE_FX3 && row > 0) ? 0 : (st.latch[page][row] ? 30 : 4);
        out[padOf(row, 11)] = usable ? (padIsLit(mapPad(padOf(row, 11)), page, false, st.engaged(page, row)) ? 30 : 4) : 0; // DISENGAGE: bright = it will take something off
    }
    if (page == PAGE_PING)
    {
        out[kPadPingPong] = (mtProject.values.delayParams & 0x80) ? 30 : 4;
        out[kPadSync] = (mtProject.values.delayParams & 0x40) ? 30 : 4;
    }
    const uint8_t dialled = st.pageMask();
    for (uint8_t i = 0; i < 48; i++)
    {
        PadInfo p = mapPad(i);
        if (p.cell == CELL_GROUP || p.cell == CELL_PING || p.cell == CELL_REV)
        {
            uint8_t pg = (p.cell == CELL_GROUP) ? p.index : (p.cell == CELL_PING ? (uint8_t)PAGE_PING : (uint8_t)PAGE_REV);
            // the page you are on is bright; a page with something dialled in is medium
            out[i] = padIsLit(p, page, false, true) ? 30 : ((dialled & (1u << pg)) ? 14 : 6);
        }
    }
    for (int i = 0; i < 48; i++)
    {
        if (out[i] != ledShown[i])
        {
            ledShown[i] = out[i];
            padsBacklight.setBackLayer(out[i] > 0 ? 1 : 0, out[i], i);
        }
    }
}

static void khz(char* out, size_t n, int hz)
{
    if (hz >= 1000)
        snprintf(out, n, "%d.%d kHz", hz / 1000, (hz % 1000) / 100);
    else
        snprintf(out, n, "%d Hz", hz);
}

// the delay bandpass center: the top of the range is "open" (the filter is out of the signal path, as in stock)
static void centreText(char* out, size_t n, int hz)
{
    if (seqfx::centerOpen((uint16_t)hz))
        snprintf(out, n, "open");
    else
        khz(out, n, hz);
}

// bandwidth in octaves, one decimal ("1.0 oct")
static void widthText(char* out, size_t n, uint8_t twelfths)
{
    int tenths = (twelfths * 10 + 6) / 12;
    snprintf(out, n, "%d.%d oct", tenths / 10, tenths % 10);
}

void cSeqPerform::refreshScreen()
{
    auto& v = mtProject.values;
    uint32_t key = 2166136261u;
    auto mix = [&key](uint32_t x) { key = (key ^ x) * 16777619u; };
    mix(page);
    mix(shiftView ? 1u : 0u);
    mix(PM->tracksPage);
    for (uint8_t t = 0; t < 16; t++)
        mix(performance.getTrackState(t));
    for (uint8_t p = 0; p < 3; p++)
        for (uint8_t r = 0; r < 4; r++)
            mix(st.fxStep[p][r]);
    mix(st.sendStep[0]);
    mix(st.sendStep[1]);
    for (uint8_t p = 0; p < PAGE_COUNT; p++)
        for (uint8_t r = 0; r < 4; r++)
            mix(st.latch[p][r] ? 1u : 0u);
    for (uint8_t i = 0; i < 2; i++)
        for (uint8_t r = 0; r < 3; r++)
            mix(st.paramState[i][r]);
    mix(stockOn);
    for (uint8_t r = 0; r < 4; r++)
        mix(rowStep(r));
    mix(v.delayParams);
    mix(v.delayTime);
    mix(v.delayFeedback);
    mix(v.delayFilterFrequency);
    mix(seqFxBandwidth());
    mix(seqFxHighpass());
    mix(seqFxReverbType());
    mix((uint32_t)(v.reverb.size * 1000));
    mix((uint32_t)(v.reverb.damp * 1000));
    mix((uint32_t)(v.reverb.predelay * 1000));
    mix((uint32_t)(v.reverb.diffusion * 1000));
    if (key == shownKey && !screenDirty)
        return;
    shownKey = key;
    screenDirty = false;

    // the 8 labels under the screen: the tracks the FX and sends affect
    uint8_t first = ((PM->tracksPage ? 1 : 0) ^ (shiftView ? 1 : 0)) * 8;
    for (uint8_t i = 0; i < 8; i++)
    {
        uint8_t t = (uint8_t)(first + i);
        bool on = t < kPerformanceTracks && performance.getTrackState(t);
        snprintf(buttonText[i], sizeof(buttonText[i]), "%s%d%s", on ? "[" : "", t + 1, on ? "]" : "");
        display.setControlText(label[i], buttonText[i]);
        display.refreshControl(label[i]);
    }

    // the screen: four bands = the four pad rows
    strFaderView& vw = viewData;
    memset(&vw, 0, sizeof(vw));
    vw.page = page;
    vw.pageMask = st.pageMask();
    uint16_t tmask = 0;
    for (uint8_t t = 0; t < 16 && t < kPerformanceTracks; t++)
    {
        if (performance.getTrackState(t))
            tmask = (uint16_t)(tmask | (1u << t));
    }
    vw.noTracks = tmask == 0;
    char list[24];
    formatTracks(list, sizeof(list), tmask);
    if (tmask == 0)
        snprintf(vw.tracks, sizeof(vw.tracks), "no track selected (buttons below)");
    else
        snprintf(vw.tracks, sizeof(vw.tracks), "affects tracks %s", list);

    for (uint8_t r = 0; r < 4; r++)
    {
        strFaderBand& b = vw.band[r];
        b.status = st.status(page, r);
        b.engaged = st.engaged(page, r) ? 1 : 0;
        b.latched = st.latch[page][r] ? 1 : 0;
        b.step = rowStep(r);
        b.usable = 1;
        b.canLatch = 1;
        if (page <= PAGE_FX3)
        {
            uint8_t slot = slotOf(page, r);
            int16_t mn, mx;
            if (!performance.getSlotRange(slot, mn, mx))
            {
                b.usable = 0;
                b.canLatch = 0;
                b.engaged = 0;
                b.step = 0;
                snprintf(b.name, sizeof(b.name), "(empty slot)");
                snprintf(b.note, sizeof(b.note), "slot %d: choose an FX on the stock Performance page", slot + 1);
                snprintf(b.state, sizeof(b.state), "NO FX");
                continue;
            }
            snprintf(b.name, sizeof(b.name), "%s", performance.getSlotFxName(slot));
            if (st.fxStep[page][r])
                snprintf(b.value, sizeof(b.value), "%+d", (int)fxValue(st.fxStep[page][r], mn, mx));
            else
                snprintf(b.value, sizeof(b.value), "off");
            snprintf(b.note, sizeof(b.note), "slot %d   range %d .. %d%s", slot + 1, (int)mn, (int)mx,
                     (stockOn & (1u << slot)) ? "   (stock pad row)" : "");
        }
        else if (r == 0)
        {
            bool rev = page == PAGE_REV;
            snprintf(b.name, sizeof(b.name), "%s", rev ? "Reverb send" : "Delay send");
            if (st.sendStep[page - PAGE_PING])
                snprintf(b.value, sizeof(b.value), "+%d", (int)sendValue(st.sendStep[page - PAGE_PING]));
            else
                snprintf(b.value, sizeof(b.value), "off");
            snprintf(b.note, sizeof(b.note), "added to the send of the affected tracks");
        }
        else
        {
            b.canLatch = 0;
            if (page == PAGE_PING)
            {
                bool sync = (v.delayParams & 0x40) != 0;
                if (r == 1 && sync)
                {
                    snprintf(b.name, sizeof(b.name), "Rate");
                    snprintf(b.value, sizeof(b.value), "%s", kRateNames[(v.delayParams & 0x3F) < 19 ? (v.delayParams & 0x3F) : 18]);
                }
                else if (r == 1)
                {
                    snprintf(b.name, sizeof(b.name), "Time");
                    snprintf(b.value, sizeof(b.value), "%d ms", (int)v.delayTime);
                }
                else if (r == 2)
                {
                    snprintf(b.name, sizeof(b.name), "Feedback");
                    snprintf(b.value, sizeof(b.value), "%d", (int)v.delayFeedback);
                }
                else
                {
                    snprintf(b.name, sizeof(b.name), "BP center");
                    centreText(b.value, sizeof(b.value), (int)v.delayFilterFrequency);
                }
            }
            else
            {
                float f = r == 1 ? v.reverb.size : (r == 2 ? v.reverb.damp : v.reverb.predelay);
                snprintf(b.name, sizeof(b.name), "%s", r == 1 ? "Size" : (r == 2 ? "Damp" : "Predelay"));
                snprintf(b.value, sizeof(b.value), "%d", (int)(f * 100 + 0.5f));
            }
            const char* how = b.status == RS_DIALLED ? "dialled in with the fader (DISENGAGE restores the earlier value)"
                                                     : "project value (saved with the project)";
            if (page == PAGE_PING && r == 3)
            {
                char w[16];
                widthText(w, sizeof(w), seqFxBandwidth());
                snprintf(b.note, sizeof(b.note), "jog = center   Shift+jog = width %s   %s", w,
                         b.status == RS_DIALLED ? "dialled in" : "project value");
            }
            else if (page == PAGE_REV && r == 3)
            {
                char h[16];
                if (seqFxHighpass() == 0)
                    snprintf(h, sizeof(h), "off");
                else
                    khz(h, sizeof(h), (int)(seqfx::hpHz(seqFxHighpass()) + 0.5f));
                snprintf(b.note, sizeof(b.note), "jog = low cut %s   Shift+jog = diffusion   %s", h,
                         b.status == RS_DIALLED ? "dialled in" : "project value");
            }
            else
            {
                snprintf(b.note, sizeof(b.note), "%s", how);
            }
        }
        static const char* const kState[5] = { "DISENGAGED", "ENGAGED", "LATCHED", "PROJECT", "DIALLED" };
        snprintf(b.state, sizeof(b.state), "%s", kState[b.status]);
    }

    int dialledFx = 0;
    for (uint8_t p = 0; p < 3; p++)
        for (uint8_t r = 0; r < 4; r++)
            dialledFx += st.fxStep[p][r] ? 1 : 0;
    dialledFx += (st.sendStep[0] ? 1 : 0) + (st.sendStep[1] ? 1 : 0);
    if (page == PAGE_PING)
    {
        char f[16], w[16];
        centreText(f, sizeof(f), (int)v.delayFilterFrequency);
        widthText(w, sizeof(w), seqFxBandwidth());
        snprintf(vw.summary, sizeof(vw.summary), "PING  %s center %s   %s width %s   ping-pong %s  sync %s",
                 shiftView ? "jog:" : "[jog]", f, shiftView ? "[Shift+jog]" : "Shift+jog:", w,
                 (v.delayParams & 0x80) ? "ON" : "off", (v.delayParams & 0x40) ? "ON" : "off");
    }
    else if (page == PAGE_REV)
    {
        char h[16];
        if (seqFxHighpass() == 0)
            snprintf(h, sizeof(h), "off");
        else
            khz(h, sizeof(h), (int)(seqfx::hpHz(seqFxHighpass()) + 0.5f));
        snprintf(vw.summary, sizeof(vw.summary), "REV  %s low cut %s   %s diffusion %d   type %s", shiftView ? "jog:" : "[jog]", h,
                 shiftView ? "[Shift+jog]" : "Shift+jog:", (int)(v.reverb.diffusion * 100 / 0.85f + 0.5f),
                 seqfx::reverbTypeName(seqFxReverbType()));
    }
    else
    {
        snprintf(vw.summary, sizeof(vw.summary), "FX %d-%d   %d dialled in   DISENGAGE: pad 12 (Shift = all four)",
                 page * 4 + 1, page * 4 + 4, dialledFx);
    }

    display.setControlData(viewCtrl, &viewData);
    display.refreshControl(viewCtrl);
    display.synchronizeRefresh();
}

//##############################################################################################
static void functSwitchModule(uint8_t button)
{
    SP->leaveTo(button);
}

// Performance press on the fader page: back to the stock page. The press is enough (the v0.1 double tap is gone, plan 005 R3).
static void functPerformanceButton(uint8_t state)
{
    // Shift + Performance = the stock Performance page; a plain press stays (the fader page is the default Performance page).
    // shiftHeld() ignores a Shift that is still held from the entry press.
    if (state == buttonPress && SP->shiftHeld())
        SP->leaveToStock();
}

static void functActionButton(uint8_t button, uint8_t state)
{
    if (state == buttonPress)
        SP->trackButton(button);
}

static void functPads(uint8_t pad, uint8_t state, [[maybe_unused]] int16_t velo)
{
    SP->padEvent(pad, state);
}

static void functEncoder(int16_t value)
{
    SP->jog(value);
}

static void functPlayAction()
{
    bool shift = SP->shiftHeld();
    if (sequencer.getSeqState() == Sequencer::SEQ_STATE_STOP)
    {
        if (!shift)
        {
            sequencer.playPattern();
        }
        else
        {
            sequencer.playSong();
            for (uint8_t a = 0; a < kPerformanceTracks; a++)
                mtProject.perfTracksPatterns[a] = mtProject.values.actualPattern;
        }
    }
    else if (shift)
    {
        sequencer.setOnGlobalStep(cbkPerformancePlaybackModeToggle);
    }
    else
    {
        sequencer.stop();
    }
}
