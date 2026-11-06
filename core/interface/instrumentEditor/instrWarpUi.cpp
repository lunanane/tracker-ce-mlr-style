// Sequencer tracks mod (plan 016 B) - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Warp on the Effects page of a sample instrument (the second page of "Instrument Parameters", Effects button): place 5 = Warp
// Off / On, place 6 = Bars. The values live in seqWarp (saved in the sidecar seqwarp.bin); the title shows the detected length
// of the sample at the project tempo and the CPU of the warped voices (like the PolySynth page).
#include "instrumentEditor/instrumentEditor.h"
#include "mtAudioEngine.h"
#include "seqTracks.h"
#include "seqWarp.h"
#include "Platform.h"
#include "config.h"
#include <stdio.h>
#include <string.h>

namespace
{
// frames of the played region (start .. end point) of the instrument
float regionFrames(uint8_t instr)
{
    const strInstrument& in = mtProject.instrument[instr];
    if (in.endPoint <= in.startPoint)
        return 0.0f;
    return (float)(in.endPoint - in.startPoint) / (float)MAX_16BIT * (float)in.sample.length;
}

float currentBpm()
{
    float bpm = engine.getTempoBpm();
    return bpm >= 10.0f ? bpm : mtProject.values.globalTempo;
}

bool modeWarps(uint8_t playMode)
{
    return playMode == playModeSingleShot || playMode == playModeLoopForward || playMode == playModeLoopBackward ||
           playMode == playModePingpong;
}
} // namespace

void cInstrumentEditor::showWarpControls()
{
    const uint8_t instr = mtProject.values.lastUsedInstrument;
    if (instr >= kSampleInstrumentsCount)
        return;
    const bool on = seqWarpOn(instr);
    const uint8_t setting = seqWarpBars(instr);
    char t2[12];
    if (on && !modeWarps(mtProject.instrument[instr].playMode))
        snprintf(t2, sizeof(t2), "On (n/a)"); // slice / granular / wavetable play un-warped
    else
        snprintf(t2, sizeof(t2), "%s", on ? "On" : "Off");
    display.setControlText(label[5], "Warp");
    display.setControlText2(label[5], t2);
    if (setting)
        snprintf(labelText[6], sizeof(labelText[6]), "%d", setting);
    else
        snprintf(labelText[6], sizeof(labelText[6]), "Auto (%d)", seqwarp::guessBars(regionFrames(instr), currentBpm()));
    display.setControlText(label[6], "Bars");
    display.setControlText2(label[6], labelText[6]);
    for (uint8_t i = 5; i < 7; i++)
    {
        display.setControlStyle2(label[i], controlStyleCenterX | controlStyleCenterY | controlStyleFont3);
        display.setControlColors(label[i], display.getTheme().activeButtonLabelsColors);
        display.refreshControl(label[i]);
    }
}

void cInstrumentEditor::changeWarp(int16_t value)
{
    const uint8_t instr = mtProject.values.lastUsedInstrument;
    if (instr >= kSampleInstrumentsCount || value == 0)
        return;
    seqWarpSetOn(instr, value > 0);
    showWarpControls();
    showTitleBar();
}

void cInstrumentEditor::changeWarpBars(int16_t value)
{
    const uint8_t instr = mtProject.values.lastUsedInstrument;
    if (instr >= kSampleInstrumentsCount || value == 0)
        return;
    uint8_t cur = seqWarpBars(instr);
    if (cur == 0) // Auto: the first step starts from the guess
        cur = seqwarp::guessBars(regionFrames(instr), currentBpm());
    int v = (int)cur + value;
    seqWarpSetBars(instr, (uint8_t)(v < 1 ? 1 : (v > seqwarp::kBarsMax ? seqwarp::kBarsMax : v)));
    showWarpControls();
    showTitleBar();
}

void cInstrumentEditor::setDefaultWarp()
{
    const uint8_t instr = mtProject.values.lastUsedInstrument;
    if (instr >= kSampleInstrumentsCount)
        return;
    seqWarpSetOn(instr, false);
    showWarpControls();
    showTitleBar();
}

void cInstrumentEditor::setDefaultWarpBars()
{
    const uint8_t instr = mtProject.values.lastUsedInstrument;
    if (instr >= kSampleInstrumentsCount)
        return;
    seqWarpSetBars(instr, 0);
    showWarpControls();
    showTitleBar();
}

// "Instrument Effects  Warp: 3.9 bars @120  CPU 5% (2)" while Warp is on for this instrument
bool cInstrumentEditor::warpTitleText(char* out, size_t cap)
{
    const uint8_t instr = mtProject.values.lastUsedInstrument;
    if (instr >= kSampleInstrumentsCount || !seqWarpOn(instr))
        return false;
    const float bpm = currentBpm();
    const float frames = regionFrames(instr);
    uint8_t voices = 0;
    const float cpu = seqWarpCpuPercent(&voices);
    char cpuText[20] = "";
    if (voices)
        snprintf(cpuText, sizeof(cpuText), "  CPU %d%% (%d)", (int)(cpu + 0.5f), (int)voices);
    const float exact = seqwarp::barsExact(frames, bpm);
    snprintf(out, cap, "Instrument Effects  Warp: sample = %d.%d bars @%d%s", (int)exact, ((int)(exact * 10.0f)) % 10, (int)(bpm + 0.5f), cpuText);
    return true;
}

void cInstrumentEditor::warpRefresh()
{
    if (!isEffectsWindow || mode != mtInstEditModeParams)
        return;
    const uint32_t now = seqNowMs();
    if (now - warpLastMs < 250)
        return;
    warpLastMs = now;
    char t[sizeof(warpTitle)];
    if (!warpTitleText(t, sizeof(t)))
        return;
    if (strcmp(t, warpTitle) != 0)
    {
        memcpy(warpTitle, t, sizeof(warpTitle));
        titleBar->setTitle(warpTitle);
    }
}
