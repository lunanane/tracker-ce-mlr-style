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
static void functRecButton(uint8_t state);
static void functLeft(uint8_t state);
static void functRight(uint8_t state);
static void functUp(uint8_t state);
static void functDown(uint8_t state);

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
    ip->setButtonObj(interfaceButtonRec, functRecButton); // forwarded to the active layer (press and release)

    ip->setButtonObj(interfaceButtonLeft, functLeft);
    ip->setButtonObj(interfaceButtonRight, functRight);
    ip->setButtonObj(interfaceButtonUp, functUp);
    ip->setButtonObj(interfaceButtonDown, functDown);

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
    hostTextHidden = false;

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
    releaseHeldPads();
    layers[layerIndex]->leave();
    seqSessionActive(false);
    padsBacklight.clearGlobalPads(1, 1, 1);
    seqTracksViewLeft();
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

void cSeqSession::releaseHeldPads()
{
    for (int i = 0; i < 48; i++)
    {
        if (padHeld[i])
        {
            padHeld[i] = false;
            layers[layerIndex]->pad(seqlogic::padCol((uint8_t)i), seqlogic::padRow((uint8_t)i), false);
        }
    }
}

void cSeqSession::selectGroup(uint8_t first)
{
    if (first >= seqsession::kTracks)
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
    releaseHeldPads(); // held pads of the old layer end here
    layers[layerIndex]->leave();
    layerIndex = index;
    groupFirst = seqsession::alignGroup(groupFirst, layers[layerIndex]->groupSize());
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
        layers[layerIndex]->pad(col, row, true);
    }
    else if (state == 0)
    {
        if (!padHeld[pad])
            return;
        padHeld[pad] = false;
        layers[layerIndex]->pad(col, row, false);
    }
}

void cSeqSession::jog(int16_t value)
{
    value = interfaceGlobals.invertValForList ? -value : value;
    layers[layerIndex]->jog(value, buttons->isButtonPressed(interfaceButtonShift));
}

void cSeqSession::screenButton(uint8_t button, bool down)
{
    if (layers[layerIndex]->button(button, down))
        return; // the layer owns this button now (hold gestures, quantise ...)
    if (!down)
        return;
    if (button == seqsession::kButtonClips)
        selectLayer(0);
    else if (button == seqsession::kButtonCut)
        selectLayer(1);
    else
    {
        uint8_t first = seqsession::groupFromButton(button, layers[layerIndex]->groupSize());
        if (first != 0xFF)
            selectGroup(first);
    }
}

void cSeqSession::arrowKey(seqsession::ArrowDir dir)
{
    layers[layerIndex]->arrow(dir, buttons->isButtonPressed(interfaceButtonShift));
}

//----------------------------------------------------------------------------------------------
void cSeqSession::refreshPads()
{
    uint8_t out[48];
    layers[layerIndex]->leds(out);
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

    bool own = layers[layerIndex]->drawsOwnScreen();
    if (own != hostTextHidden)
    {
        hostTextHidden = own;
        if (own)
            display.setControlHide(titleLabel);
        else
            display.setControlShow(titleLabel);
        display.refreshControl(titleLabel);
        for (int i = 0; i < 4; i++)
        {
            if (own)
                display.setControlHide(infoLabel[i]);
            else
                display.setControlShow(infoLabel[i]);
            display.refreshControl(infoLabel[i]);
        }
        changed = true;
    }

    uint8_t gs = layers[layerIndex]->groupSize();
    snprintf(titleText, sizeof(titleText), "Session: %s   tracks %d-%d", layers[layerIndex]->name(), groupFirst + 1, groupFirst + gs);
    if (strcmp(titleText, shownTitle) != 0)
    {
        strcpy(shownTitle, titleText);
        display.setControlText(titleLabel, titleText);
        display.refreshControl(titleLabel);
        changed = true;
    }

    // screen buttons: group choice (1-2 for 8 tracks, 1-4 for 4 tracks), 7 = CLIPS, 8 = CUT
    for (uint8_t i = 0; i < 8; i++)
    {
        if (layers[layerIndex]->buttonLabel(i, buttonText[i]))
        {
            // the layer labels this button itself
        }
        else if (i < seqsession::kTracks / gs)
        {
            bool sel = groupFirst == i * gs;
            snprintf(buttonText[i], sizeof(buttonText[i]), "%s%d-%d%s", sel ? "[" : "", i * gs + 1, i * gs + gs, sel ? "]" : "");
        }
        else if (i == seqsession::kButtonClips || i == seqsession::kButtonCut)
        {
            bool sel = layerIndex == (i == seqsession::kButtonClips ? 0 : 1);
            snprintf(buttonText[i], sizeof(buttonText[i]), "%s%s%s", sel ? "[" : "", i == seqsession::kButtonClips ? "CLIPS" : "CUT", sel ? "]" : "");
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
    if (layers[layerIndex]->refresh(force))
        changed = true;
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
        SS->screenButton(button, true);
    else if (state == buttonRelease)
        SS->screenButton(button, false);
}

static void functLeft(uint8_t state)
{
    if (state == buttonPress)
        SS->arrowKey(seqsession::ARROW_LEFT);
}

static void functRight(uint8_t state)
{
    if (state == buttonPress)
        SS->arrowKey(seqsession::ARROW_RIGHT);
}

static void functUp(uint8_t state)
{
    if (state == buttonPress)
        SS->arrowKey(seqsession::ARROW_UP);
}

static void functDown(uint8_t state)
{
    if (state == buttonPress)
        SS->arrowKey(seqsession::ARROW_DOWN);
}

static void functPads(uint8_t pad, uint8_t state, [[maybe_unused]] int16_t velo)
{
    SS->padEvent(pad, state);
}

static void functEncoder(int16_t value)
{
    SS->jog(value);
}

static void functRecButton(uint8_t state)
{
    SS->layers[SS->layerIndex]->rec(state == buttonPress);
}

static void functPlayAction()
{
    if (sequencer.getSeqState() == Sequencer::SEQ_STATE_STOP)
        sequencer.playPattern();
    else
        sequencer.stop();
}
