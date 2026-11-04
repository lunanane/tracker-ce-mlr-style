// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
#include "laneView.h"

#include <string.h>

#include "cutLayer.h"
#include "display.h"
#include "displayControls.h"
#include "laneDraw.h"
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
    virtual void setData(void* data) { model = (const seqcut::LaneModel*)data; }

  private:
    const seqcut::LaneModel* model;
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
    virtual void setData(void* data) { model = (const seqcut::HeadModel*)data; }

  private:
    const seqcut::HeadModel* model;
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

Palette palette()
{
    Theme& t = display.getTheme();
    return makePalette(t.backgroundColor, contrastFg(t.backgroundColor), t.red, t.orange, t.cyan);
}

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
LaneModel s_lanes;
HeadModel s_heads;
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
    model = (const LaneModel*)properties->data;
    setStyle(properties->style);
}

void cSeqLanes::update()
{
    if (model == nullptr)
        return;
    API_SAVE_CONTEXT();
    FtCtx c;
    drawLanes(c, *model, palette());
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
    model = (const HeadModel*)properties->data;
    setStyle(properties->style);
}

void cSeqPlayheads::update()
{
    if (model == nullptr)
        return;
    API_SAVE_CONTEXT();
    FtCtx c;
    drawHeads(c, *model, palette());
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
// are created once per module start and only shown / hidden; the display frees them when the module stops.
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

bool laneViewRefresh(bool force, uint8_t firstTrack)
{
    if (!s_visible || s_hLanes < 0 || s_hHeads < 0)
        return false;
    const uint32_t now = seqNowMs();
    bool changed = false;
    const uint8_t first = groupFirst(firstTrack);
    seqcut::Cut* cut = seqEngine.cut();

    // static part: rebuilt at most every 100 ms (the note scan is 4 x up to 128 steps), redrawn only when it differs
    if (force || !s_have || (uint32_t)(now - s_lastStatic) >= 100)
    {
        s_lastStatic = now;
        LaneModel m;
        memset(&m, 0, sizeof(m));
        m.first = first;
        m.recHeld = cutRecHeld() ? 1 : 0;
        for (uint8_t r = 0; r < kRows; r++)
        {
            const uint8_t t = (uint8_t)(first + r);
            const View v = viewOf(cut, t);
            LaneInfo& l = m.lane[r];
            l.track = t;
            l.flags = v.flags;
            l.loopLo = v.loopLo;
            l.loopHi = v.loopHi;
            int steps = sequencer.seqCutSteps(t);
            l.steps = (uint8_t)(steps > kMaxSteps ? kMaxSteps : steps);
            uint8_t map[kMaxSteps];
            memset(map, 0, sizeof(map));
            sequencer.seqCutNoteMap(t, l.steps, map);
            for (int i = 0; i < l.steps; i++)
                if (map[i] == 1)
                    noteBitSet(l.notes, i);
        }
        if (force || !s_have || memcmp(&m, &s_lanes, sizeof(m)) != 0)
        {
            s_lanes = m;
            display.refreshControl(s_hLanes);
            changed = true;
        }
    }

    // moving part: the lines move smoothly (1/48 step), the redraw is capped at 25 fps
    HeadModel h;
    memset(&h, 0, sizeof(h));
    for (uint8_t r = 0; r < kRows; r++)
    {
        const uint8_t t = (uint8_t)(first + r);
        uint8_t pos = 0, uStep = 1, last = 0;
        sequencer.seqTrackPos(t, pos, uStep, last);
        h.h[r] = makeHead(viewOf(cut, t), pos, kRedrawPerStep ? 1 : uStep, sequencer.seqCutStockPos(t), s_lanes.lane[r].steps); // kRedrawPerStep: the line only moves at a step
    }
    barBeat(sequencer.seqCutStockPos(0), h.bar, h.beat);
    h.recHeld = s_lanes.recHeld;
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

void laneViewRequestGroup(uint8_t firstTrack)
{
    seqSession.selectGroup(firstTrack);
}

bool laneViewAnyOffGrid()
{
    seqcut::Cut* c = seqEngine.cut();
    return c != nullptr && c->offGridMask() != 0; // a snapped (SYNC ALL done) track is on the grid
}
