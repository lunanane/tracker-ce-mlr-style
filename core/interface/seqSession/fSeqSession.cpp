// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
#include "seqSession/seqSession.h"

#include <stdio.h>
#include <string.h>

#include "core/interfaceMenuPopup.h"
#include "core/songTimer.h"
#include "mtPadsBacklight.h"
#include "mtSequencer.h"
#include "seqLauncher.h"
#include "seqTracks.h"

cSeqSession seqSession;
static cSeqSession* SS = &seqSession;
static elapsedMillis s_clock; // never reset: time base of the blinking LEDs and messages

static void functSwitchModule(uint8_t button);
static void functActionButton(uint8_t button, uint8_t state);
static void functPads(uint8_t pad, uint8_t state, int16_t velo);
static void functEncoder(int16_t value);
static void functPlayAction();

//----------------------------------------------------------------------------------------------
void cSeqSession::start(uint32_t options)
{
    (void)options;
    moduleRefresh = 1;

    ip->setButtonObj(interfaceButtonPerformance, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonParams, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonFile, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonConfig, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonMaster, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSamplePlay, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleEdit, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleRec, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleLoad, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSong, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonPattern, buttonPress, functSwitchModule); // Pattern again: back to the editor
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
    {
        ledShown[i] = 0xFF;
        padHeld[i] = false;
    }
    memset(shownInfo, 0xFF, sizeof(shownInfo));
    shownTitle[0] = 0;
    memset(shownButtons, 0xFF, sizeof(shownButtons));

    titleBar->show();
    titleBar->setTitle("Session view");

    seqSessionActive(true);
    layers[layerIndex]->enter(groupFirst);
    refreshScreen(true);
    refreshPads();
}

void cSeqSession::stop()
{
    moduleRefresh = 0;
    for (int i = 0; i < 48; i++)
    {
        if (padHeld[i])
        {
            padHeld[i] = false;
            if (!seqsession::isLayerSwitchPad(seqlogic::padCol((uint8_t)i), seqlogic::padRow((uint8_t)i)))
                layers[layerIndex]->pad(seqlogic::padCol((uint8_t)i), seqlogic::padRow((uint8_t)i), false);
        }
    }
    layers[layerIndex]->leave();
    seqSessionActive(false);
    padsBacklight.clearGlobalPads(1, 1, 1);
}

//----------------------------------------------------------------------------------------------
void cSeqSession::update()
{
    layers[layerIndex]->update((uint32_t)s_clock);

    refreshScreen(false);

    static elapsedMillis ledTimer;
    if (ledTimer > 30)
    {
        ledTimer = 0;
        refreshPads();
    }
}

void cSeqSession::selectGroup(uint8_t first)
{
    if (first != 0 && first != 8)
        return;
    if (first != groupFirst)
    {
        groupFirst = first;
        layers[layerIndex]->setGroup(groupFirst);
    }
}

void cSeqSession::selectLayer(uint8_t index)
{
    if (index > 1 || index == layerIndex)
        return;
    // held pads of the old layer end here
    for (int i = 0; i < 48; i++)
    {
        if (padHeld[i])
        {
            padHeld[i] = false;
            if (!seqsession::isLayerSwitchPad(seqlogic::padCol((uint8_t)i), seqlogic::padRow((uint8_t)i)))
                layers[layerIndex]->pad(seqlogic::padCol((uint8_t)i), seqlogic::padRow((uint8_t)i), false);
        }
    }
    layers[layerIndex]->leave();
    layerIndex = index;
    layers[layerIndex]->enter(groupFirst);
    for (int i = 0; i < 48; i++)
        ledShown[i] = 0xFF;
}

void cSeqSession::padEvent(uint8_t pad, uint8_t state)
{
    if (pad >= 48)
        return;
    uint8_t col = seqlogic::padCol(pad), row = seqlogic::padRow(pad);
    if (state == 1)
    {
        padHeld[pad] = true;
        if (seqsession::isLayerSwitchPad(col, row))
            selectLayer(row); // row 0 = CLIPS, row 1 = CUT
        else
            layers[layerIndex]->pad(col, row, true);
    }
    else if (state == 0)
    {
        if (!padHeld[pad])
            return;
        padHeld[pad] = false;
        if (!seqsession::isLayerSwitchPad(col, row))
            layers[layerIndex]->pad(col, row, false);
    }
}

void cSeqSession::jog(int16_t value)
{
    value = interfaceGlobals.invertValForList ? -value : value;
    layers[layerIndex]->jog(value, buttons->isButtonPressed(interfaceButtonShift));
}

//----------------------------------------------------------------------------------------------
void cSeqSession::refreshPads()
{
    uint8_t out[48];
    layers[layerIndex]->leds(out);
    // the layer-switch pads belong to the module
    out[0 * 12 + 10] = layerIndex == 0 ? 15 : 4;
    out[1 * 12 + 10] = layerIndex == 1 ? 15 : 4;
    for (int i = 0; i < 48; i++)
    {
        uint8_t level = out[i] > 15 ? 15 : out[i];
        uint8_t pwm = (uint8_t)(level * 2);
        if (pwm != ledShown[i])
        {
            ledShown[i] = pwm;
            padsBacklight.setBackLayer(pwm > 0 ? 1 : 0, pwm, i);
        }
    }
}

void cSeqSession::refreshScreen(bool force)
{
    bool changed = force;

    snprintf(titleText, sizeof(titleText), "Session: %s   tracks %d-%d", layers[layerIndex]->name(), groupFirst + 1, groupFirst + 8);
    if (strcmp(titleText, shownTitle) != 0)
    {
        strcpy(shownTitle, titleText);
        display.setControlText(titleLabel, titleText);
        display.refreshControl(titleLabel);
        changed = true;
    }

    // screen buttons: button 1 = tracks 1-8, button 2 = tracks 9-16
    for (uint8_t i = 0; i < 8; i++)
    {
        if (i < 2)
        {
            bool sel = groupFirst == (i == 0 ? 0 : 8);
            snprintf(buttonText[i], sizeof(buttonText[i]), "%s%s%s", sel ? "[" : "", i == 0 ? "1-8" : "9-16", sel ? "]" : "");
        }
        else
        {
            buttonText[i][0] = 0;
        }
        if (strcmp(buttonText[i], shownButtons[i]) != 0)
        {
            strcpy(shownButtons[i], buttonText[i]);
            display.setControlText(label[i], buttonText[i]);
            display.refreshControl(label[i]);
            changed = true;
        }
    }

    for (int i = 0; i < 4; i++)
        infoText[i][0] = 0;
    layers[layerIndex]->info(infoText);
    for (int i = 0; i < 4; i++)
    {
        if (strcmp(infoText[i], shownInfo[i]) != 0)
        {
            strcpy(shownInfo[i], infoText[i]);
            display.setControlText(infoLabel[i], infoText[i]);
            display.refreshControl(infoLabel[i]);
            changed = true;
        }
    }
    if (changed)
        display.synchronizeRefresh();
}

//##############################################################################################
static void functSwitchModule(uint8_t button)
{
    SS->eventFunct(eventSwitchModule, SS, &button, 0);
}

static void functActionButton(uint8_t button, uint8_t state)
{
    if (state == buttonPress)
    {
        bool shift = SS->buttons->isButtonPressed(interfaceButtonShift);
        uint8_t first = seqsession::groupFromButton(button, shift);
        if (first != 0xFF)
            SS->selectGroup(first);
    }
}

static void functPads(uint8_t pad, uint8_t state, [[maybe_unused]] int16_t velo)
{
    SS->padEvent(pad, state);
}

static void functEncoder(int16_t value)
{
    SS->jog(value);
}

static void functPlayAction()
{
    if (sequencer.getSeqState() == Sequencer::SEQ_STATE_STOP)
        sequencer.playPattern();
    else
        sequencer.stop();
}
