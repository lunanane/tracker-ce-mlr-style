// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
#include "laneView.h"

#include <string.h>

#include "cutLayer.h"
#include "display.h"
#include "displayControls.h"
#include "laneDraw.h"
#include "mlrDraw.h"
#include "seqMlr.h"
#include "mtSequencer.h"
#include "seqSession/seqSession.h"
#include "seqTracks.h"
#include "FT812.h" // last: its CMD_* macros clash with enumerators of the session headers

using namespace seqcut;

class cSeqLanes : public cDisplayControl
{
  public:
    cSeqLanes(strControlProperties* properties = nullptr);
    virtual ~cSeqLanes() {}
    virtual void update();
    virtual void setStyle(uint32_t style) { this->style = style; }
    virtual void setText(const char*) {}
    virtual void setValue(int) {}
    virtual void setColors(const uint32_t* colors) { this->colors = colors; }
    virtual void setData(void* data) { model = (const seqcut::MlrModel*)data; }

  private:
    const seqcut::MlrModel* model;
};

class cSeqPlayheads : public cDisplayControl
{
  public:
    cSeqPlayheads(strControlProperties* properties = nullptr);
    virtual ~cSeqPlayheads() {}
    virtual void update();
    virtual void setStyle(uint32_t style) { this->style = style; }
    virtual void setText(const char*) {}
    virtual void setValue(int) {}
    virtual void setColors(const uint32_t* colors) { this->colors = colors; }
    virtual void setData(void* data) { model = (const seqcut::MlrHeadModel*)data; }

  private:
    const seqcut::MlrHeadModel* model;
};

namespace
{
// Drawing context over the FT81x display list (laneDraw.h). Rectangles and discs are batched per primitive; text ends the batch.
struct FtCtx
{
    uint8_t prim = 0; // 0 none, 1 RECTS, 2 POINTS
    void ensure(uint8_t want)
    {
        if (prim == want)
            return;
        if (prim)
            API_END();
        if (want == 1)
        {
            API_LINE_WIDTH(8);
            API_BEGIN(RECTS);
        }
        else if (want == 2)
            API_BEGIN(FTPOINTS);
        prim = want;
    }
    void color(uint32_t rgb) { API_COLOR(rgb); }
    void alpha(uint8_t a) { API_COLOR_A(a); }
    void rect(int x0, int y0, int x1, int y1)
    {
        ensure(1);
        API_VERTEX2F(x0, y0);
        API_VERTEX2F(x1 - 1, y1 - 1);
    }
    void disc(int cx, int cy, int r)
    {
        ensure(2);
        API_POINT_SIZE(r * 16);
        API_VERTEX2F(cx, cy);
    }
    void text(int x, int y, int font, int opt, const char* s)
    {
        ensure(0);
        uint16_t o = (uint16_t)(((opt & TX_CENTERX) ? OPT_CENTERX : 0) | ((opt & TX_CENTERY) ? OPT_CENTERY : 0) |
                                ((opt & TX_RIGHT) ? OPT_RIGHTX : 0));
        int f = font < 1 ? 0 : (font > displayFontCount ? displayFontCount - 1 : font - 1);
        API_CMD_TEXT(x, y, fonts[f].handle, o, s);
    }
    void finish() { ensure(0); }
};

View viewOf(seqcut::Cut* cut, uint8_t track)
{
    if (cut)
        return cut->view(track);
    View v;
    v.col = 0;
    v.loopLo = 1;
    v.loopHi = 0;
    v.flags = (uint8_t)(1 << VF_RATE_SHIFT);
    return v;
}

hControl s_hLanes = -1, s_hHeads = -1;
bool s_visible = false;
MlrModel s_lanes;
MlrHeadModel s_heads;
uint32_t s_lastStatic = 0, s_lastHead = 0;
bool s_have = false; // the models hold a frame that was handed to the display
} // namespace

//----------------------------------------------------------------------------------------------
cSeqLanes::cSeqLanes(strControlProperties* properties) : cDisplayControl(controlIdSeqLanes, 1), model(nullptr)
{
    if (properties == nullptr)
        return;
    posX = properties->x;
    posY = properties->y;
    width = properties->w;
    height = properties->h;
    model = (const MlrModel*)properties->data;
    setStyle(properties->style);
}

void cSeqLanes::update()
{
    if (model == nullptr)
        return;
    API_SAVE_CONTEXT();
    FtCtx c;
    drawMlr(c, *model);
    c.finish();
    API_RESTORE_CONTEXT();
}

cSeqPlayheads::cSeqPlayheads(strControlProperties* properties) : cDisplayControl(controlIdSeqPlayheads, 1), model(nullptr)
{
    if (properties == nullptr)
        return;
    posX = properties->x;
    posY = properties->y;
    width = properties->w;
    height = properties->h;
    model = (const MlrHeadModel*)properties->data;
    setStyle(properties->style);
}

void cSeqPlayheads::update()
{
    if (model == nullptr)
        return;
    API_SAVE_CONTEXT();
    FtCtx c;
    drawMlrHeads(c, *model);
    c.finish();
    API_RESTORE_CONTEXT();
}

//----------------------------------------------------------------------------------------------
void laneViewCreate()
{
    // Called once per module start (initDisplayControls): the display freed the controls of the last start, the old handles are void.
    s_hLanes = s_hHeads = -1;
    s_visible = false;
    // controls created first are drawn last (on top): the playheads go over the lanes
    memset(&s_lanes, 0, sizeof(s_lanes));
    memset(&s_heads, 0, sizeof(s_heads));
    strControlProperties prop;
    prop.x = 0;
    prop.y = 0;
    prop.w = 800;
    prop.h = 424;
    prop.style = 0; // hidden until the CUT layer is shown
    prop.data = &s_heads;
    s_hHeads = display.createControl<cSeqPlayheads>(&prop);
    prop.data = &s_lanes;
    s_hLanes = display.createControl<cSeqLanes>(&prop);
    s_have = false;
}

// Never display.destroyControl() at runtime (plan 007 B1): it frees the slot while the control may still sit in the display's refresh
// queue (laneViewShow(false) queues it), and the queue then stalls on the empty slot for good - nothing on the screen is drawn again
// ("stuck in MLR"); it also shifts the slots (and so the handles and the cached display lists) of every control above it. The controls
// are created once per module start and only shown / hidden; the display frees them when the module stops (display.destroyModulesControls;
// this function is only reached through destroyControls(), which nothing calls today - laneViewCreate resets the handles at every start).
void laneViewForget()
{
    s_hLanes = s_hHeads = -1;
    s_visible = false;
}

void laneViewShow(bool show)
{
    s_visible = show;
    s_have = false; // the next refresh draws everything again
    const hControl handles[2] = { s_hLanes, s_hHeads };
    for (int i = 0; i < 2; i++)
    {
        if (handles[i] < 0)
            continue;
        if (show)
            display.setControlShow(handles[i]);
        else
            display.setControlHide(handles[i]);
        display.refreshControl(handles[i]);
    }
}

bool laneViewRefresh(bool force)
{
    if (!s_visible || s_hLanes < 0 || s_hHeads < 0)
        return false;
    const uint32_t now = seqNowMs();
    bool changed = false;
    seqcut::Cut* cut = seqEngine.cut();
    uint8_t pos[kSlots], uStep[kSlots];

    // static part: rebuilt at most every 100 ms, redrawn only when it differs (the legend follows the playheads: its model holds them)
    if (force || !s_have || (uint32_t)(now - s_lastStatic) >= 100)
    {
        s_lastStatic = now;
        MlrModel m;
        memset(&m, 0, sizeof(m));
        m.recHeld = cutRecHeld() ? 1 : 0;
        m.legend = cutLegendShown() ? 1 : 0;
        m.pickSlot = (uint8_t)cutPickSlot();
        const uint16_t has = cutPickSlot() >= 0 ? seqCutTracksWithNotes() : 0;
        for (uint8_t k = 0; k < kSlots; k++)
        {
            const uint8_t t = cutSlotTrack(k);
            const View v = viewOf(cut, t);
            SlotInfo& l = m.slot[k];
            l.track = t;
            l.flags = v.flags;
            l.flags2 = v.flags2;
            l.spp = (uint8_t)((v.flags2 & V2_GRID8) ? 2 : 4);
            l.sec = v.sec;
            l.nsec = v.nsec;
            l.pads = v.pads;
            l.loopLo = v.loopLo;
            l.loopHi = v.loopHi;
            l.picking = cutPickSlot() == (int8_t)k;
            l.muted = seqCutTrackMuted(t);
            l.rec = cutTrackRecArmed(t) ? 1 : 0;
            l.has = has;
            l.cacheMask = seqmlr::cacheMask(k);
            const WaveCol* w = seqmlr::wave(k);
            l.waveOk = w != nullptr;
            if (w)
                memcpy(l.wave, w, sizeof(l.wave));
            // note marks: a pad whose steps hold a note
            const int steps = sequencer.seqCutSteps(t);
            uint8_t map[kMaxSteps];
            memset(map, 0, sizeof(map));
            sequencer.seqCutNoteMap(t, (uint8_t)(steps > kMaxSteps ? kMaxSteps : steps), map);
            const int base = v.sec * sectionSteps(l.spp);
            for (int p = 0; p < kMlrPads; p++)
                for (int i = 0; i < l.spp; i++)
                {
                    const int st = base + p * l.spp + i;
                    if (st < steps && st < kMaxSteps && map[st] == 1)
                        l.noteMask |= (uint16_t)(1u << p);
                }
            uint8_t last = 0;
            sequencer.seqTrackPos(t, pos[k], uStep[k], last);
            m.legendPad[k] = padOfPos(pos[k], v.sec, l.spp);
        }
        if (force || !s_have || memcmp(&m, &s_lanes, sizeof(m)) != 0)
        {
            s_lanes = m;
            display.refreshControl(s_hLanes);
            changed = true;
        }
    }

    // moving part: the lines move smoothly (1/48 step), the redraw is capped at 25 fps
    MlrHeadModel h;
    memset(&h, 0, sizeof(h));
    for (uint8_t k = 0; k < kSlots; k++)
    {
        const uint8_t t = cutSlotTrack(k);
        uint8_t p = 0, u = 1, last = 0;
        sequencer.seqTrackPos(t, p, u, last);
        h.h[k] = makeMlrHead(viewOf(cut, t), p, kRedrawPerStep ? 1 : u, sequencer.seqCutStockPos(t));
    }
    barBeat(sequencer.seqCutStockPos(cutSlotTrack(0)), h.bar, h.beat);
    h.recHeld = s_lanes.recHeld;
    h.legend = s_lanes.legend;
    const bool moved = !s_have || memcmp(&h, &s_heads, sizeof(h)) != 0;
    if (force || redrawDue(moved, now, s_lastHead))
    {
        s_lastHead = now;
        s_heads = h;
        display.refreshControl(s_hHeads);
        changed = true;
    }
    s_have = true;
    return changed;
}

bool cutShiftHeld()
{
    return ModuleBase::buttons->isButtonPressed(interfaceButtonShift);
}

bool laneViewAnyOffGrid()
{
    seqcut::Cut* c = seqEngine.cut();
    return c != nullptr && c->offGridMask() != 0; // a snapped (SYNC ALL done) track is on the grid
}
