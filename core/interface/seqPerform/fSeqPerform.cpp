// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
#include "seqPerform/seqPerform.h"

#include <stdio.h>
#include <string.h>

#include "fileManager.h"
#include "mtAudioEngine.h"
#include "mtPadBoard.h"
#include "mtPadsBacklight.h"
#include "mtSequencer.h"
#include "mtStructs.h"
#include "performanceMode/performanceEngine.h"
#include "performanceMode/performanceFx.h"
#include "performanceMode/performanceMode.h"
#include "seqTracks.h"

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
static const char* const kPageTitles[PAGE_COUNT] = { "Perform faders: FX 1-4",
                                                     "Perform faders: FX 5-8",
                                                     "Perform faders: FX 9-12",
                                                     "Perform faders: PING (delay)",
                                                     "Perform faders: REV (reverb)" };

static void projectChanged()
{
    newFileManager.setProjectStructChanged();
}

//----------------------------------------------------------------------------------------------
void cSeqPerform::start(uint32_t options)
{
    (void)options;
    moduleRefresh = 1;

    ip->setButtonObj(interfaceButtonPerformance, functPerformanceButton); // tap = back to the stock page
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
    PM->dontClearPerformanceMode = 0; // set again only when we hand over to the stock page / Master / Params
    memset(fxStep, 0, sizeof(fxStep));
    memset(fxVal, 0, sizeof(fxVal));
    memset(stockVal, 0, sizeof(stockVal));
    stockOn = 0;
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
            fxStep[slot / 4][slot % 4] = nearestFxStep(v, mn, mx);
        }
    }
    memset(sendStep, 0, sizeof(sendStep));
    memset(latch, 0, sizeof(latch));
    padHeld = 0;
    page = PAGE_FX1;
    shiftView = false;
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
    releaseAll();
    // like cPerformanceMode::stop: when the target does not keep the perform FX they end here
    if (!PM->dontClearPerformanceMode)
    {
        if (performance.turnOff())
        {
            newFileManager.loadWorkspacePatternNow(mtProject.values.actualPattern);
            sequencer.switchRamPatternsNow();
        }
        mtPadBoard.releaseAllInstruments();
    }
    performance.forceSaveConfig();
    padsBacklight.clearGlobalPads(1, 1, 1);
}

//----------------------------------------------------------------------------------------------
void cSeqPerform::update()
{
    bool shift = buttons->isButtonPressed(interfaceButtonShift);
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
        return fxStep[page][row];
    if (row == 0)
        return sendStep[page - PAGE_PING];
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

static void setParamDefault(uint8_t pg, uint8_t row)
{
    auto& v = mtProject.values;
    if (pg == PAGE_PING)
    {
        if (row == 1)
        {
            if (v.delayParams & 0x40)
            {
                v.delayParams &= 0xC0;
                engine.setDelayParams(v.delayParams);
            }
            else
            {
                v.delayTime = 500;
                engine.setDelayTime(v.delayTime);
            }
        }
        else if (row == 2)
        {
            v.delayFeedback = 50;
            engine.setDelayFeedback(v.delayFeedback);
        }
        else
        {
            v.delayFilterFrequency = DELAY_FILTER_FREQ_MAX;
            engine.setDelayFilterFrequency(v.delayFilterFrequency);
        }
    }
    else if (row == 1)
    {
        v.reverb.size = DEFAULT_REVERB_SIZE;
        engine.setReverbSize(v.reverb.size);
    }
    else if (row == 2)
    {
        v.reverb.damp = DEFAULT_REVERB_DAMP;
        engine.setReverbDamp(v.reverb.damp);
    }
    else
    {
        v.reverb.predelay = DEFAULT_REVERB_PREDELAY;
        engine.setReverbPredelay(v.reverb.predelay);
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
        fxStep[page][row] = step;
        fxVal[slot] = fxValue(step, mn, mx);
        performance.setSlotValueDirect(slot, fxVal[slot]);
        syncKnockedOut(slot);
    }
    else if (row == 0)
    {
        sendStep[page - PAGE_PING] = step;
        perfApplySend(page == PAGE_REV, sendValue(step));
    }
    else
    {
        setParamStep(page, row, step);
    }
    screenDirty = true;
}

// Fader row back to neutral (momentary release, RESET, LATCH off)
void cSeqPerform::releaseRow(uint8_t row)
{
    if (page <= PAGE_FX3)
    {
        uint8_t slot = slotOf(page, row);
        int16_t mn, mx;
        if (stockOn & (1u << slot))
        {
            // the stock page latched a pad row here: back to its value, not to silence
            fxVal[slot] = stockVal[slot];
            if (performance.getSlotRange(slot, mn, mx))
                fxStep[page][row] = nearestFxStep(stockVal[slot], mn, mx);
            performance.setSlotValueDirect(slot, stockVal[slot]);
        }
        else if (fxStep[page][row] != 0 || fxVal[slot] != 0)
        {
            fxStep[page][row] = 0;
            fxVal[slot] = 0;
            performance.releaseSlotDirect(slot);
        }
    }
    else if (row == 0 && sendStep[page - PAGE_PING] != 0)
    {
        sendStep[page - PAGE_PING] = 0;
        perfApplySend(page == PAGE_REV, 0);
    }
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
    if (momentary && !latch[page][row])
        releaseRow(row);
}

void cSeqPerform::faderReset(uint8_t row)
{
    latch[page][row] = false;
    if (page <= PAGE_FX3)
    {
        uint8_t slot = slotOf(page, row);
        if (stockOn & (1u << slot))
        {
            // RESET also clears the stock pad row
            stockOn &= (uint16_t)~(1u << slot);
            stockVal[slot] = 0;
            performance.setSelectedRow(slot, 0);
        }
    }
    if (page > PAGE_FX3 && row > 0)
        setParamDefault(page, row);
    else
        releaseRow(row);
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
        if (momentary && !latch[page][r])
            releaseRow(r);
    }
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
            if (fxStep[p][r] && fxVal[slot] != 0)
                performance.setSlotValueDirect(slot, fxVal[slot]);
        }
    }
    for (uint8_t i = 0; i < 2; i++)
    {
        if (sendStep[i])
            perfApplySend(i == 1, sendValue(sendStep[i]));
    }
}

// Slots the engine switched off because another slot took the same FX: their faders go dark
void cSeqPerform::syncKnockedOut(uint8_t keepSlot)
{
    for (uint8_t s = 0; s < 12; s++)
    {
        if (s == keepSlot || fxVal[s] == 0 || fxStep[s / 4][s % 4] == 0)
            continue;
        if (performance.getSlotTempValue(s) == 0)
        {
            fxVal[s] = 0;
            fxStep[s / 4][s % 4] = 0;
            stockOn &= (uint16_t)~(1u << s);
        }
    }
}

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
            else if (fxStep[p][r] || fxVal[slot])
            {
                fxStep[p][r] = 0;
                fxVal[slot] = 0;
                performance.releaseSlotDirect(slot);
            }
        }
    }
    for (uint8_t i = 0; i < 2; i++)
    {
        if (sendStep[i])
        {
            sendStep[i] = 0;
            perfApplySend(i == 1, 0);
        }
    }
    memset(latch, 0, sizeof(latch));
    padHeld = 0;
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
                latch[page][p.index] = !latch[page][p.index];
                if (!latch[page][p.index])
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
            case CELL_RESET: faderReset(p.index); break;
            default: break;
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
    bool shift = buttons->isButtonPressed(interfaceButtonShift);
    uint8_t track = (uint8_t)(button + ((PM->tracksPage ? 1 : 0) ^ (shift ? 1 : 0)) * 8);
    if (track >= kPerformanceTracks)
        return;
    performance.toggleTrackState(track);
    if (performance.getTrackState(track))
        reapplyActive();
    screenDirty = true;
}

// Jog wheel (the screen buttons select tracks): PING: ping-pong on/off, Shift = tempo sync on/off;
// REV: diffusion.
void cSeqPerform::jog(int16_t value)
{
    auto& v = mtProject.values;
    if (value == 0)
        return;
    if (page == PAGE_PING)
    {
        if (buttons->isButtonPressed(interfaceButtonShift))
        {
            if (value > 0)
                v.delayParams |= 0x40;
            else
                v.delayParams &= (uint8_t)~0x40;
            engine.setDelayParams(v.delayParams);
            if (value > 0)
                engine.setDelayTime(v.delayTime);
        }
        else
        {
            if (value > 0)
                v.delayParams |= 0x80;
            else
                v.delayParams &= (uint8_t)~0x80;
            engine.setDelayParams(v.delayParams);
        }
        projectChanged();
    }
    else if (page == PAGE_REV)
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
    screenDirty = true;
}

// Performance tap: back to the stock Performance page (FX state stays as the stock page expects)
void cSeqPerform::leaveToStock()
{
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
        out[padOf(row, 10)] = (page > PAGE_FX3 && row > 0) ? 0 : (latch[page][row] ? 30 : 4);
        out[padOf(row, 11)] = 8;
    }
    for (uint8_t i = 0; i < 48; i++)
    {
        PadInfo p = mapPad(i);
        if (p.cell == CELL_GROUP || p.cell == CELL_PING || p.cell == CELL_REV)
            out[i] = padIsLit(p, page, false) ? 30 : 6;
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
    for (uint8_t r = 0; r < 4; r++)
    {
        mix(rowStep(r));
        mix(latch[page][r] ? 1u : 0u);
    }
    mix(v.delayParams);
    mix(v.delayTime);
    mix(v.delayFeedback);
    mix(v.delayFilterFrequency);
    mix((uint32_t)(v.reverb.size * 1000));
    mix((uint32_t)(v.reverb.damp * 1000));
    mix((uint32_t)(v.reverb.predelay * 1000));
    mix((uint32_t)(v.reverb.diffusion * 1000));
    if (key == shownKey && !screenDirty)
        return;
    shownKey = key;
    screenDirty = false;

    uint8_t first = ((PM->tracksPage ? 1 : 0) ^ (shiftView ? 1 : 0)) * 8;
    for (uint8_t i = 0; i < 8; i++)
    {
        uint8_t t = (uint8_t)(first + i);
        bool on = t < kPerformanceTracks && performance.getTrackState(t);
        snprintf(buttonText[i], sizeof(buttonText[i]), "%s%d%s", on ? "[" : "", t + 1, on ? "]" : "");
        display.setControlText(label[i], buttonText[i]);
        display.refreshControl(label[i]);
    }

    display.setControlText(titleLabel, kPageTitles[page]);
    display.refreshControl(titleLabel);

    for (uint8_t r = 0; r < 4; r++)
    {
        const char* lat = latch[page][r] ? "  LATCH" : "";
        char* out = infoText[r];
        size_t n = sizeof(infoText[r]);
        if (page <= PAGE_FX3)
        {
            uint8_t slot = slotOf(page, r);
            int16_t mn, mx;
            if (!performance.getSlotRange(slot, mn, mx))
            {
                snprintf(out, n, "%d: (empty slot, set FX on stock page)", r + 1);
            }
            else if (fxStep[page][r])
            {
                snprintf(out, n, "%d: %s  %+d%s", r + 1, performance.getSlotFxName(slot), fxValue(fxStep[page][r], mn, mx), lat);
            }
            else
            {
                snprintf(out, n, "%d: %s  ---%s", r + 1, performance.getSlotFxName(slot), lat);
            }
        }
        else if (page == PAGE_PING)
        {
            bool sync = (v.delayParams & 0x40) != 0;
            if (r == 0)
                snprintf(out, n, "1: Delay send  %s%s", sendStep[0] ? "" : "---", lat);
            else if (r == 1 && sync)
                snprintf(out, n, "2: Rate  %s", kRateNames[(v.delayParams & 0x3F) < 19 ? (v.delayParams & 0x3F) : 18]);
            else if (r == 1)
                snprintf(out, n, "2: Time  %d ms", (int)v.delayTime);
            else if (r == 2)
                snprintf(out, n, "3: Feedback  %d", (int)v.delayFeedback);
            else
                snprintf(out, n, "4: Filter  %d Hz", (int)v.delayFilterFrequency);
            if (r == 0 && sendStep[0])
                snprintf(out, n, "1: Delay send  +%d%s", (int)sendValue(sendStep[0]), lat);
        }
        else
        {
            if (r == 0)
                snprintf(out, n, "1: Reverb send  %s%s", sendStep[1] ? "" : "---", lat);
            else if (r == 1)
                snprintf(out, n, "2: Size  %d", (int)(v.reverb.size * 100 + 0.5f));
            else if (r == 2)
                snprintf(out, n, "3: Damp  %d", (int)(v.reverb.damp * 100 + 0.5f));
            else
                snprintf(out, n, "4: Predelay  %d", (int)(v.reverb.predelay * 100 + 0.5f));
            if (r == 0 && sendStep[1])
                snprintf(out, n, "1: Reverb send  +%d%s", (int)sendValue(sendStep[1]), lat);
        }
    }
    if (page == PAGE_PING)
        snprintf(infoText[4], sizeof(infoText[4]), "Ping-pong %s (jog)   Sync %s (Shift+jog)",
                 (v.delayParams & 0x80) ? "ON" : "off", (v.delayParams & 0x40) ? "ON" : "off");
    else if (page == PAGE_REV)
        snprintf(infoText[4], sizeof(infoText[4]), "Diffusion %d (jog)", (int)(v.reverb.diffusion * 100 / 0.85f + 0.5f));
    else
        snprintf(infoText[4], sizeof(infoText[4]), "Buttons below: tracks affected (Shift = 9-16)");

    for (int i = 0; i < 5; i++)
    {
        display.setControlText(infoLabel[i], infoText[i]);
        display.refreshControl(infoLabel[i]);
    }
    display.synchronizeRefresh();
}

//##############################################################################################
static void functSwitchModule(uint8_t button)
{
    // same as the stock Performance page: leaving it ends the perform FX unless the target keeps them
    if (sequencer.isPerformanceMode())
    {
        if (PM->canPerformanceModeContinueAfterSwitchingTo(button))
            PM->dontClearPerformanceMode = 1;
        else if (sequencer.isPlay())
            PM->turnOffAtEndOfPattern();
        else
            PM->turnOff();
    }
    SP->eventFunct(eventSwitchModule, SP, &button, 0);
}

static void functPerformanceButton(uint8_t state)
{
    if (state == buttonPress)
    {
        bool othersHeld = false; // held pads do not block a tap (plan 005 M4)
        for (int b = 0; b < interfaceButtonsCount; b++)
        {
            if (b != interfaceButtonPerformance && SP->buttons->isButtonPressed(b))
            {
                othersHeld = true;
                break;
            }
        }
        seqPerformTap.down(!othersHeld, seqNowMs());
    }
    else if (state == buttonRelease)
    {
        if (seqTapUp(seqPerformTap))
            SP->leaveToStock();
    }
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
    bool shift = SP->buttons->isButtonPressed(interfaceButtonShift);
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
