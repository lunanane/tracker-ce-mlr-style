// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
#include "seqPlay/seqPlay.h"

#include <stdio.h>
#include <string.h>

#include "core/interfaceMenuPopup.h"
#include "core/songTimer.h"
#include "mtPadsBacklight.h"
#include "mtSequencer.h"
#include "seqTracks.h"

cSeqPlay seqPlay;
static cSeqPlay* SP = &seqPlay;

static void functSwitchModule(uint8_t button);
static void functActionButton(uint8_t button, uint8_t state);
static void functPads(uint8_t pad, uint8_t state, int16_t velo);
static void functEncoder(int16_t value);
static void functPlayAction();

static const char* const kTypeNames[seqlogic::TYPE_COUNT] = { "Off", "Chord/Arp", "Mono" };
static const char kTypeLetters[seqlogic::TYPE_COUNT] = { '-', 'C', 'M' };

//----------------------------------------------------------------------------------------------
void cSeqPlay::start(uint32_t options)
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
        padOwner[i] = kNoOwner;
        ledShown[i] = 0xFF;
    }
    listOpen = false;
    shiftView = false;
    shownKey = 0xFFFFFFFF;
    screenDirty = true;
    if (track >= seqlogic::kTracks)
        track = 0;

    titleBar->show();
    titleBar->setTitle("Play mode");

    refreshScreen();
    refreshPads();
}

void cSeqPlay::stop()
{
    moduleRefresh = 0;
    releaseAllPads();
    padsBacklight.clearGlobalPads(1, 1, 1);
}

//----------------------------------------------------------------------------------------------
void cSeqPlay::update()
{
    // Shift (tracks 9-16) and Instr (type list) are polled: no event handlers needed
    bool shift = buttons->isButtonPressed(interfaceButtonShift);
    if (shift != shiftView)
    {
        shiftView = shift;
        screenDirty = true;
    }

    static bool instrWasDown = false;
    bool instr = buttons->isButtonPressed(interfaceButtonInstr);
    if (instrWasDown && !instr)
        instrChanged(false);
    instrWasDown = instr;

    refreshScreen();

    static elapsedMillis ledTimer;
    if (ledTimer > 30)
    {
        ledTimer = 0;
        refreshPads();
    }
}

void cSeqPlay::releaseAllPads()
{
    for (int i = 0; i < 48; i++)
    {
        if (padOwner[i] != kNoOwner)
        {
            seqEngine.postPad(padOwner[i], (uint8_t)i, false);
            padOwner[i] = kNoOwner;
        }
    }
}

void cSeqPlay::selectTrack(uint8_t t)
{
    if (t >= seqlogic::kTracks)
        return;
    listOpen = false;
    if (t != track)
    {
        releaseAllPads();
        track = t;
        for (int i = 0; i < 48; i++)
            ledShown[i] = 0xFF;
        refreshPads();
    }
    screenDirty = true;
}

void cSeqPlay::padEvent(uint8_t pad, uint8_t state)
{
    if (pad >= 48)
        return;
    if (state == 1)
    {
        if (seqEngine.type(track) == seqlogic::TYPE_OFF)
            return;
        padOwner[pad] = track;
        seqEngine.postPad(track, pad, true);
    }
    else if (state == 0)
    {
        if (padOwner[pad] != kNoOwner)
        {
            seqEngine.postPad(padOwner[pad], pad, false);
            padOwner[pad] = kNoOwner;
        }
    }
}

// Instr released: apply the chosen type (next bar while the transport runs)
void cSeqPlay::instrChanged(bool down)
{
    if (!down && listOpen)
    {
        seqEngine.postType(track, listSel);
        listOpen = false;
        screenDirty = true;
    }
}

void cSeqPlay::jog(int16_t value)
{
    if (!buttons->isButtonPressed(interfaceButtonInstr))
        return;
    value = interfaceGlobals.invertValForList ? -value : value;
    if (!listOpen)
    {
        listOpen = true;
        uint8_t pend = seqEngine.pendingType(track);
        listSel = pend != 0xFF ? pend : seqEngine.type(track);
    }
    listSel = seqlogic::stepTypeSelection(listSel, value);
    screenDirty = true;
}

//----------------------------------------------------------------------------------------------
void cSeqPlay::refreshPads()
{
    uint8_t out[48];
    seqEngine.leds(track, out);
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

void cSeqPlay::refreshScreen()
{
    // signature of everything the screen shows
    uint32_t key = 2166136261u;
    auto mix = [&key](uint32_t v) { key = (key ^ v) * 16777619u; };
    for (uint8_t t = 0; t < seqlogic::kTracks; t++)
    {
        mix(seqEngine.type(t));
        mix(seqEngine.pendingType(t));
        mix(seqEngine.takenOver(t) ? 1u : 0u);
    }
    mix(track);
    mix(shiftView ? 1u : 0u);
    mix(listOpen ? 100u + listSel : 0u);
    mix(seqEngine.playing() ? 1u : 0u);
    if (key == shownKey && !screenDirty)
        return;
    shownKey = key;
    screenDirty = false;

    uint8_t first = shiftView ? 8 : 0;
    for (uint8_t i = 0; i < 8; i++)
    {
        uint8_t t = (uint8_t)(first + i);
        uint8_t type = seqEngine.type(t);
        snprintf(buttonText[i],
                 sizeof(buttonText[i]),
                 "%s%d %c%s%s",
                 t == track ? "[" : "",
                 t + 1,
                 kTypeLetters[type < seqlogic::TYPE_COUNT ? type : 0],
                 seqEngine.takenOver(t) ? "*" : "",
                 t == track ? "]" : "");
        display.setControlText(label[i], buttonText[i]);
        display.refreshControl(label[i]);
    }

    uint8_t type = seqEngine.type(track);
    if (type >= seqlogic::TYPE_COUNT)
        type = 0;
    snprintf(titleText, sizeof(titleText), "Track %d: %s", track + 1, kTypeNames[type]);
    display.setControlText(titleLabel, titleText);
    display.refreshControl(titleLabel);

    const char* hint = type == seqlogic::TYPE_CHORDARP ? "Hold a root pad to play chords" :
                       type == seqlogic::TYPE_MONO     ? "Tap pads to set the steps" :
                                                         "Hold Instr + jog wheel to pick a sequencer";
    snprintf(infoText[0], sizeof(infoText[0]), "%s", hint);

    uint8_t pend = seqEngine.pendingType(track);
    if (pend != 0xFF && pend < seqlogic::TYPE_COUNT)
        snprintf(infoText[1], sizeof(infoText[1]), "Next bar: %s", kTypeNames[pend]);
    else
        infoText[1][0] = 0;

    snprintf(infoText[2],
             sizeof(infoText[2]),
             "%s",
             type == seqlogic::TYPE_OFF  ? "Pattern plays this track" :
             seqEngine.takenOver(track)  ? "Sequencer plays this track (pattern muted)" :
                                           "Pattern plays until you play");
    for (int i = 0; i < 3; i++)
    {
        display.setControlText(infoLabel[i], infoText[i]);
        display.refreshControl(infoLabel[i]);
    }

    // type list in the track's column
    for (int i = 0; i < 3; i++)
    {
        if (listOpen)
        {
            static char listText[3][16];
            snprintf(listText[i], sizeof(listText[i]), "%s%s", i == listSel ? ">" : "", kTypeNames[i]);
            display.setControlPosition(listLabel[i], (800 / 8) * (track % 8) + (800 / 16), 330 + 32 * i);
            display.setControlText(listLabel[i], listText[i]);
            display.setControlShow(listLabel[i]);
            display.refreshControl(listLabel[i]);
        }
        else
        {
            display.setControlHide(listLabel[i]);
            display.refreshControl(listLabel[i]);
        }
    }
    display.synchronizeRefresh();
}

//##############################################################################################
static void functSwitchModule(uint8_t button)
{
    SP->eventFunct(eventSwitchModule, SP, &button, 0);
}

static void functActionButton(uint8_t button, uint8_t state)
{
    if (state == buttonPress)
    {
        bool shift = SP->buttons->isButtonPressed(interfaceButtonShift);
        SP->selectTrack(seqlogic::trackFromScreenButton(button, shift));
    }
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
    if (sequencer.getSeqState() == Sequencer::SEQ_STATE_STOP)
        sequencer.playPattern();
    else
        sequencer.stop();
}
