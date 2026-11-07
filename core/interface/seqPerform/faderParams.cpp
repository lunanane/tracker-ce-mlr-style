// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Shared by the Perform fader page (fSeqPerform.cpp) and the Master fader pages (masterParams/masterFaders.cpp), plan 024.
#include "seqPerform/faderParams.h"

#include <stdio.h>

#include "fileManager.h"
#include "mtAudioEngine.h"
#include "mtStructs.h"
#include "seqFx.h"

namespace seqperf {

static const char* const kRateNames[19] = { "8",   "6",   "4",   "3",   "2",    "3/2",  "1",    "3/4",  "1/2", "3/8",
                                            "1/3", "1/4", "3/16", "1/6", "1/8", "1/12", "1/16", "1/24", "1/32" };

static void projectChanged()
{
    newFileManager.setProjectStructChanged();
}

uint8_t paramRowStep(uint8_t pg, uint8_t row)
{
    auto& v = mtProject.values;
    if (pg == PAGE_PING)
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

void applyParamStep(uint8_t pg, uint8_t row, uint8_t step)
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

float currentParam(uint8_t pg, uint8_t row)
{
    auto& v = mtProject.values;
    if (pg == PAGE_PING)
        return row == 1 ? (float)v.delayTime : (row == 2 ? (float)v.delayFeedback : (float)v.delayFilterFrequency);
    return row == 1 ? v.reverb.size : (row == 2 ? v.reverb.damp : v.reverb.predelay);
}

void restoreParam(uint8_t pg, uint8_t row, float pre, uint8_t preRate)
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

void khzText(char* out, size_t n, int hz)
{
    if (hz >= 1000)
        snprintf(out, n, "%d.%d kHz", hz / 1000, (hz % 1000) / 100);
    else
        snprintf(out, n, "%d Hz", hz);
}

// the delay bandpass center: the top of the range is "open" (the filter is out of the signal path, as in stock)
void centreText(char* out, size_t n, int hz)
{
    if (seqfx::centerOpen((uint16_t)hz))
        snprintf(out, n, "open");
    else
        khzText(out, n, hz);
}

void paramRowText(uint8_t pg, uint8_t row, char* name, size_t nameCap, char* value, size_t valueCap)
{
    auto& v = mtProject.values;
    if (pg == PAGE_PING)
    {
        const bool sync = (v.delayParams & 0x40) != 0;
        if (row == 1 && sync)
        {
            snprintf(name, nameCap, "Rate");
            snprintf(value, valueCap, "%s", kRateNames[(v.delayParams & 0x3F) < 19 ? (v.delayParams & 0x3F) : 18]);
        }
        else if (row == 1)
        {
            snprintf(name, nameCap, "Time");
            snprintf(value, valueCap, "%d ms", (int)v.delayTime);
        }
        else if (row == 2)
        {
            snprintf(name, nameCap, "Feedback");
            snprintf(value, valueCap, "%d", (int)v.delayFeedback);
        }
        else
        {
            snprintf(name, nameCap, "BP center");
            centreText(value, valueCap, (int)v.delayFilterFrequency);
        }
    }
    else
    {
        float f = row == 1 ? v.reverb.size : (row == 2 ? v.reverb.damp : v.reverb.predelay);
        snprintf(name, nameCap, "%s", row == 1 ? "Size" : (row == 2 ? "Damp" : "Predelay"));
        snprintf(value, valueCap, "%d", (int)(f * 100 + 0.5f));
    }
}

} // namespace seqperf
