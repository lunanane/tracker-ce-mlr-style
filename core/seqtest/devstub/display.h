// Sequencer tracks mod -- PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Host model of the stock display (libs/NewDisplay/display.cpp) for test_pitchdevice.cpp: the control table, the refresh queue with
// synchronizeRefresh / stopAppend, one step per display.update() call (step1: one stage of the control at the queue bottom, step2: the
// stage DL appended to the control's RAM_G slot, more stages or the next control, step3: the frame = every shown control in reverse
// table order, appended while the sum stays under 8172 bytes, else the rest is dropped). Ported line by line from display.cpp.
#pragma once
#include <stdint.h>
#include <vector>

#include "FT812.h"

enum { controlStyleShow = 1u << 0 };
enum { controlIdSeqTraceView = 1, controlIdDummy = 2 };

struct strControlProperties
{
    const char* text = nullptr;
    uint32_t style = 0;
    int x = 0, y = 0, w = 0, h = 0;
    void* data = nullptr;
};
struct strFont { int handle; };
static strFont fonts[8] = { { 0 }, { 1 }, { 2 }, { 3 }, { 4 }, { 5 }, { 6 }, { 7 } };

class cDisplayControl
{
  public:
    cDisplayControl(int, uint8_t stages) : stagesCount(stages) {}
    virtual ~cDisplayControl() {}
    virtual void update() = 0;
    virtual void update2() {}
    virtual void setStyle(uint32_t s) { style = s; }
    virtual void setText(const char*) {}
    virtual void setValue(int) {}
    virtual void setColors(const uint32_t*) {}
    virtual void setData(void*) {}
    const uint8_t stagesCount;
    int ramSize = 0;
    std::vector<DlOp> ram; // the ops of the slot (what CMD_APPEND would replay)
    bool autoRefresh = false, highPrioRefresh = false;
    uint8_t refresh = 0;
    uint32_t style = 0;
    int posX = 0, posY = 0, width = 0, height = 0, colorsCount = 0;
};

struct Frame
{
    int bytes = 0;
    std::vector<int> controls; // appended, in order
    std::vector<int> dropped;  // shown but over the budget
    std::vector<DlOp> ops;
};

class cDisplay
{
  public:
    enum { kMax = 64, kQueue = 64 };
    cDisplayControl* controlsTable[kMax] = {};
    int controlsCount = 0;
    int8_t refreshQueue[kQueue] = {};
    uint8_t refreshQueueTop = 0, refreshQueueBott = 0, synchQueuePosition = 0;
    bool stopAppend = false;
    int updateStep = 0, actualUpdatingStage = 0, actualUpdatingIndex = -1;
    cDisplayControl* actualUpdating = nullptr;
    Frame last;
    int frames = 0;

    template <class T> int createControl(strControlProperties* p)
    {
        controlsTable[controlsCount] = new T(p);
        return controlsCount++;
    }
    void destroyAll()
    {
        for (int i = 0; i < controlsCount; i++) delete controlsTable[i];
        *this = cDisplay();
    }
    // v0.4.7: what the other visible controls would add to the frame (the real display.h)
    uint32_t ramUsedByOthers(int h) const
    {
        uint32_t sum = 0;
        for (int i = 0; i < controlsCount; i++)
            if (i != h && controlsTable[i] && (controlsTable[i]->style & controlStyleShow) && controlsTable[i]->refresh) sum += controlsTable[i]->ramSize;
        return sum;
    }
    bool ok(int h) const { return h >= 0 && h < controlsCount && controlsTable[h]; }
    void setControlShow(int h) { if (ok(h)) controlsTable[h]->style |= controlStyleShow; }
    void setControlHide(int h) { if (ok(h)) controlsTable[h]->style &= ~controlStyleShow; }
    void refreshControl(int h)
    {
        if (!ok(h)) return;
        uint8_t i = refreshQueueBott;
        if (refreshQueue[i] == h && refreshQueueTop != i) i = (uint8_t)((i + 1) % kQueue);
        while (i != refreshQueueTop)
        {
            if (refreshQueue[i] == h) return;
            i = (uint8_t)((i + 1) % kQueue);
        }
        refreshQueue[refreshQueueTop] = (int8_t)h;
        refreshQueueTop = (uint8_t)((refreshQueueTop + 1) % kQueue);
    }
    void synchronizeRefresh()
    {
        if (refreshQueueTop == refreshQueueBott)
        {
            stopAppend = false;
            return;
        }
        synchQueuePosition = refreshQueueTop;
        stopAppend = true;
    }
    void update() // one main loop pass (the co-processor is always idle again by the next pass)
    {
        switch (updateStep)
        {
            case 0: step1(); break;
            case 1: step2(); break;
            case 2: step3(); break;
            default: updateStep = 0; break;
        }
    }

  private:
    bool validate()
    {
        if (actualUpdatingIndex < 0 || actualUpdatingIndex >= controlsCount || actualUpdating == nullptr ||
            actualUpdatingStage >= actualUpdating->stagesCount)
        {
            if (refreshQueueTop != refreshQueueBott) refreshQueueBott = (uint8_t)((refreshQueueBott + 1) % kQueue);
            updateStep = 0;
            actualUpdatingStage = 0;
            return false;
        }
        return true;
    }
    void step1()
    {
        if (refreshQueueTop == refreshQueueBott)
        {
            if (stopAppend)
            {
                stopAppend = false;
                updateStep = 2;
            }
            return;
        }
        if (!ok(refreshQueue[refreshQueueBott]))
        {
            refreshQueueBott = (uint8_t)((refreshQueueBott + 1) % kQueue);
            return;
        }
        if (actualUpdatingStage > 0 && !validate()) return;
        g_dl = DlBuild();
        g_dl.bytes = 4; // CMD_DLSTART
        if (actualUpdatingStage == 0)
        {
            actualUpdatingIndex = refreshQueue[refreshQueueBott];
            actualUpdating = controlsTable[actualUpdatingIndex];
            actualUpdating->update();
        }
        else if (actualUpdatingStage == 1)
            actualUpdating->update2();
        updateStep = 1;
    }
    void step2()
    {
        if (!validate()) return;
        if (actualUpdatingStage == 0)
        {
            actualUpdating->ramSize = 0;
            actualUpdating->ram.clear();
        }
        actualUpdating->ramSize += g_dl.bytes;
        actualUpdating->ram.insert(actualUpdating->ram.end(), g_dl.ops.begin(), g_dl.ops.end());
        actualUpdatingStage++;
        if (actualUpdatingStage < actualUpdating->stagesCount)
        {
            updateStep = 0;
            return;
        }
        refreshQueueBott = (uint8_t)((refreshQueueBott + 1) % kQueue);
        if (stopAppend && synchQueuePosition == refreshQueueBott) stopAppend = false;
        if (actualUpdating->autoRefresh && (actualUpdating->style & controlStyleShow)) refreshControl(actualUpdatingIndex);
        actualUpdating->refresh = 1;
        actualUpdating = nullptr;
        actualUpdatingIndex = -1;
        actualUpdatingStage = 0;
        updateStep = 2;
    }
    void step3()
    {
        if (stopAppend)
        {
            updateStep = 0;
            step1();
            return;
        }
        Frame f;
        int used = 0;
        bool full = false;
        for (int i = controlsCount - 1; i >= 0; i--)
        {
            cDisplayControl* p = controlsTable[i];
            if (!p || !(p->style & controlStyleShow) || !p->refresh) continue;
            if (!full && p->ramSize + used < 8172)
            {
                used += p->ramSize;
                f.controls.push_back(i);
                f.ops.insert(f.ops.end(), p->ram.begin(), p->ram.end());
            }
            else
            {
                full = true;
                f.dropped.push_back(i);
            }
        }
        f.bytes = used;
        last = f;
        frames++;
        updateStep = 0;
    }
};
extern cDisplay display;
