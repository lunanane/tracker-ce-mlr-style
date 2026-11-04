// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
#include "seqSession/clipsLayer.h"

#include <stdio.h>
#include <string.h>

#include "mtSequencer.h"
#include "seqClips.h"
#include "seqLauncher.h"

extern Sequencer sequencer;

namespace seqsession {

namespace
{
constexpr uint32_t kBuildMs = 40;  // the grid data is rebuilt at most this often
constexpr uint32_t kMarksMs = 80;  // the animated marks are redrawn at most this often
constexpr uint32_t kMsgMs = 2000;
} // namespace

bool ClipsLayer::hasNotes(uint8_t pattern, uint8_t track) const
{
    return track < kTracks && ((seqSessionPatternMask(pattern) >> track) & 1u);
}

namespace
{
uint8_t refusalMsg()
{
    return seqSessionLastRefusal() == REFUSE_STOP_TO_SAVE ? clipgrid::MSG_STOP_TO_SAVE : clipgrid::MSG_BUSY;
}
} // namespace

void ClipsLayer::say(uint8_t msg)
{
    msg_ = msg;
    msgUntil_ = nowMs_ + kMsgMs;
    dirty_ = true;
}

void ClipsLayer::enter(uint8_t firstTrack)
{
    first_ = firstTrack;
    modeHeld_ = false;
    stopHeld_ = false;
    uint8_t own = seqSessionOwnPattern();
    if (own < window_.first || own > window_.first + (kWinRows - 1))
        window_.first = Window::clampFirst(own); // show the loaded pattern
    if (focusPattern_ == 0)
    {
        focusPattern_ = window_.first;
        focusTrack_ = first_;
    }
    seqSessionTakeUndoCleared(); // a note from before the view was entered is stale
    showControls(true);
    dirty_ = true;
}

void ClipsLayer::leave()
{
    modeHeld_ = false;
    stopHeld_ = false;
    showControls(false);
}

void ClipsLayer::setGroup(uint8_t firstTrack)
{
    first_ = firstTrack;
    dirty_ = true;
}

//----------------------------------------------------------------------------------------------
// pads
void ClipsLayer::pad(uint8_t col, uint8_t row, bool down)
{
    PadAction a = mapPad(col, row);
    if (!down || a.kind == PK_NONE)
        return;
    uint8_t pattern = window_.pattern(row);
    uint8_t track = (uint8_t)(first_ + col);
    // the focused clip is the last clip pad pressed; a scene pad focuses the row (Shift + button 1 + wheel sets all its clips)
    focusPattern_ = pattern;
    if (a.kind == PK_CLIP)
        focusTrack_ = track;
    dirty_ = true;

    bool ok = true;
    switch (padOp(a.kind, modeHeld_, stopHeld_, a.kind == PK_CLIP && hasNotes(pattern, track)))
    {
        case OP_STOP_TRACK:
            // hold 3 + pad, or an empty pad (Ableton rule; the pad shows it with a dim light)
            if (seqLauncher.stopped(track) && seqLauncher.queuedKind(track) == Launcher::K_NONE)
                return;
            ok = seqSessionStopTrack(track);
            break;
        case OP_LAUNCH: ok = seqSessionLaunchClip(track, pattern); break;
        case OP_STOP_ALL: ok = seqSessionStopAll(); break;
        case OP_SCENE: ok = seqSessionLaunchSceneQ(pattern, kQuantGlobal); break;
        default: return; // OP_FOCUS: button 1 held, the pads only focus
    }
    if (!ok)
        say(refusalMsg());
}

void ClipsLayer::leds(uint8_t out[48])
{
    bool blink = blinkPhase(nowMs_);
    uint8_t own = seqSessionOwnPattern();
    uint8_t nextScene = seqSessionQueuedScene();
    uint16_t masks[kWinRows];
    for (uint8_t r = 0; r < kWinRows; r++)
        masks[r] = seqSessionPatternMask(window_.pattern(r));

    bool sounds[kTracks]; // the track plays a clip with content: its empty pads are dimly lit (they would stop it)
    for (uint8_t t = 0; t < kTracks; t++)
    {
        uint8_t cp = seqLauncher.current(t) ? seqLauncher.current(t) : own;
        sounds[t] = !seqLauncher.stopped(t) && ((seqSessionPatternMask(cp) >> t) & 1u);
    }

    for (uint8_t row = 0; row < kRows; row++)
    {
        uint8_t pattern = window_.pattern(row);
        for (uint8_t col = 0; col < kCols; col++)
        {
            uint8_t level = 0;
            PadAction a = mapPad(col, row);
            if (a.kind == PK_CLIP && row < kWinRows)
            {
                uint8_t t = (uint8_t)(first_ + col);
                bool notes = (masks[row] >> t) & 1u;
                bool playing = seqLauncher.isPlaying(t, pattern, own);
                bool queued = seqLauncher.isQueued(t, pattern, own);
                bool follow = queued && seqLauncher.queuedQuant(t) == Q_CLIPEND;
                ClipState s = clipState(notes, playing, queued, follow);
                level = s == CS_EMPTY ? emptyPadLevel(sounds[t]) : clipLevel(s, blink);
                if (modeHeld_ && pattern == focusPattern_ && t == focusTrack_ && level < 10)
                    level = 10; // the focused clip while the mode is being set
            }
            else if (a.kind == PK_SCENE && row < kWinRows)
            {
                bool any = masks[row] != 0;
                level = clipLevel(clipState(any, pattern == own, nextScene == pattern), blink);
            }
            out[row * kCols + col] = level;
        }
    }
}

//----------------------------------------------------------------------------------------------
// jog, arrows, screen buttons
bool ClipsLayer::jog(int16_t value, bool shift)
{
    if (modeHeld_)
    {
        // button 1 held: the wheel sets the play mode of the focused clip; with Shift it sets the SCENE flag of the row instead
        // (plan 006: play next = the Song page goes on to the next slot after this pattern; the clips' own modes stay as they are)
        if (focusPattern_ == 0 || value == 0)
            return true;
        if (shift)
            seqClipSetPlayNext(focusPattern_, stepClipMode(seqClipPlayNext(focusPattern_) ? seqclips::CLIP_PLAY1X : seqclips::CLIP_LOOP, value) == seqclips::CLIP_PLAY1X);
        else
            seqClipSetMode(focusPattern_, focusTrack_, stepClipMode(seqClipMode(focusPattern_, focusTrack_), value));
        dirty_ = true;
        return true;
    }
    if (window_.scroll(jogToScroll(value, shift)))
        dirty_ = true;
    return true;
}

bool ClipsLayer::arrow(ArrowDir dir, bool shift)
{
    if (dir == ARROW_UP || dir == ARROW_DOWN)
    {
        if (window_.scroll(arrowToScroll(dir == ARROW_DOWN, shift)))
            dirty_ = true;
        return true;
    }
    return false; // Left / Right: the host pages the tracks 1-8 / 9-16
}

bool ClipsLayer::button(uint8_t i, bool down)
{
    switch (i)
    {
        case kBtnMode:
            modeHeld_ = down;
            dirty_ = true;
            return true;
        case kBtnQuant:
            if (down)
            {
                seqLauncher.setQuant(nextQuant(seqLauncher.quant())); // each tap steps on: bar, pattern end, immediate
                dirty_ = true;
            }
            return true;
        case kBtnStop:
            stopHeld_ = down;
            dirty_ = true;
            return true;
        default: return false;
    }
}

bool ClipsLayer::buttonLabel(uint8_t i, char out[12])
{
    switch (i)
    {
        case kBtnMode:
            snprintf(out, 12, "%s%s", modeHeld_ ? "[" : "", focusPattern_ && seqClipMode(focusPattern_, focusTrack_) == seqclips::CLIP_PLAY1X ? "MODE 1X" : "MODE LOOP");
            if (modeHeld_)
                strcat(out, "]");
            return true;
        case kBtnQuant:
            snprintf(out, 12, "Q: %s", quantShort(seqLauncher.quant()));
            return true;
        case kBtnStop:
            snprintf(out, 12, "%s", stopHeld_ ? "[STOP]" : "STOP");
            return true;
        case 3:
        case 4:
        case 5:
            out[0] = 0;
            return true;
        default: return false; // 7 = gap, 8 = CLIPS <-> CUT switch: the host
    }
}

//----------------------------------------------------------------------------------------------
// screen
void ClipsLayer::createControls()
{
    // The marks control is created first: the display composes the lowest control index on top, so it overlays the grid.
    strControlProperties p;
    p.text = nullptr;
    p.value = 0;
    p.style = 0; // hidden until the layer is shown
    p.x = 0;
    p.y = 29;
    p.w = 800;
    p.h = 394;
    p.data = &marksData_;
    marks_ = display.createControl<cSeqClipMarks>(&p);
    p.data = &view_;
    grid_ = display.createControl<cSeqClipGrid>(&p);
    memset(&view_, 0, sizeof(view_));
    memset(&marksData_, 0xFF, sizeof(marksData_)); // never equal to a built state: the first refresh draws
    dirty_ = true;
}

void ClipsLayer::destroyControls()
{
    grid_ = -1; // the display destroys the module's controls itself
    marks_ = -1;
}

void ClipsLayer::showControls(bool on)
{
    if (grid_ < 0 || marks_ < 0)
        return;
    if (on)
    {
        display.setControlShow(marks_);
        display.setControlShow(grid_);
        memset(&marksData_, 0xFF, sizeof(marksData_)); // force a redraw of both (the grid too: its data did not change while hidden)
        memset(&view_, 0xFF, sizeof(view_));
    }
    else
    {
        display.setControlHide(marks_);
        display.setControlHide(grid_);
        display.refreshControl(grid_); // something in the queue so the screen is composed again without us
        display.synchronizeRefresh();
    }
}

bool ClipsLayer::refresh(bool force)
{
    if (grid_ < 0 || marks_ < 0)
        return false;
    if (!force && !dirty_ && (uint32_t)(nowMs_ - lastBuild_) < kBuildMs)
        return false;
    lastBuild_ = nowMs_;
    dirty_ = false;

    clipgrid::View v;
    clipgrid::Marks m;
    build(v, m);

    bool changed = false;
    if (force || memcmp(&v, &view_, sizeof(v)) != 0)
    {
        view_ = v;
        display.refreshControl(grid_);
        changed = true;
    }
    if (force || (memcmp(&m, &marksData_, sizeof(m)) != 0 && (uint32_t)(nowMs_ - lastMarks_) >= kMarksMs))
    {
        marksData_ = m;
        lastMarks_ = nowMs_;
        display.refreshControl(marks_);
        changed = true;
    }
    return changed;
}

void ClipsLayer::build(clipgrid::View& v, clipgrid::Marks& m)
{
    memset(&v, 0, sizeof(v));
    memset(&m, 0, sizeof(m));
    using namespace clipgrid;

    uint8_t own = seqSessionOwnPattern();
    top_ = viewTop(top_, window_.first);
    v.top = top_;
    v.winFirst = window_.first;
    v.winTrack = first_;
    v.focusPattern = focusPattern_;
    v.focusTrack = focusTrack_;
    v.focusMode = focusPattern_ ? seqClipMode(focusPattern_, focusTrack_) : 0;
    v.quant = seqLauncher.quant();
    v.focusScene = focusPattern_ && seqClipPlayNext(focusPattern_) ? 1 : 0;
    v.modeHeld = modeHeld_;
    v.stopHeld = stopHeld_;

    if (seqSessionTakeUndoCleared())
        say(MSG_UNDO_CLEARED);
    if (msg_ != MSG_NONE && (int32_t)(nowMs_ - msgUntil_) >= 0)
        msg_ = MSG_NONE;
    v.msg = msg_;

    // content of the visible rows, plus the row below the last one (the Play 1x end marker looks at it)
    uint16_t masks[kVisRows + 1];
    for (uint8_t r = 0; r <= kVisRows; r++)
    {
        uint16_t p = (uint16_t)(top_ + r);
        masks[r] = p <= kPatternMax ? seqSessionPatternMask((uint8_t)p) : 0;
    }

    uint8_t cp[kTracks]; // the pattern whose clip plays on the track
    bool sounds[kTracks];
    for (uint8_t t = 0; t < kTracks; t++)
    {
        cp[t] = seqLauncher.current(t) ? seqLauncher.current(t) : own;
        int rel = (int)cp[t] - (int)top_;
        uint16_t mask = (rel >= 0 && rel <= kVisRows) ? masks[rel] : seqSessionPatternMask(cp[t]);
        sounds[t] = !seqLauncher.stopped(t) && ((mask >> t) & 1u);
    }

    for (uint8_t r = 0; r < kVisRows; r++)
    {
        uint8_t pattern = (uint8_t)(top_ + r);
        for (uint8_t t = 0; t < kTracks; t++)
        {
            bool has = (masks[r] >> t) & 1u;
            bool playing = seqLauncher.isPlaying(t, pattern, own);
            bool queued = seqLauncher.isQueued(t, pattern, own);
            bool follow = queued && seqLauncher.queuedQuant(t) == Q_CLIPEND;
            switch (clipState(has, playing, queued, follow))
            {
                case CS_PLAYING: v.cell[r][t] = CELL_PLAYING; break;
                case CS_QUEUED: v.cell[r][t] = CELL_QUEUED; break;
                case CS_FOLLOW: v.cell[r][t] = CELL_FOLLOW; break;
                case CS_NOTES: v.cell[r][t] = CELL_NOTES; break;
                default: v.cell[r][t] = CELL_EMPTY; break;
            }
            uint8_t fl = 0;
            if (has && seqClipMode(pattern, t) == seqclips::CLIP_PLAY1X) // the notch only marks clips that have content
            {
                fl |= F_ONEX;
                bool nextHas = pattern < kPatternMax && ((masks[r + 1] >> t) & 1u);
                if (!nextHas)
                    fl |= F_END; // nothing below: the track stops after this clip
            }
            bool inWindow = (int)r >= (int)window_.first - (int)top_ && (int)r < (int)window_.first - (int)top_ + kWinRows && t >= first_ && t < first_ + 8;
            if (!has && sounds[t] && inWindow) // the dim square only where a pad reaches
                fl |= F_STOPMARK;
            v.flags[r][t] = fl;
        }
        bool queuedScene = seqSessionQueuedScene() == pattern;
        v.scene[r] = queuedScene ? CELL_QUEUED : (pattern == own ? CELL_PLAYING : (masks[r] ? CELL_NOTES : CELL_EMPTY));
        v.sceneFlags[r] = seqClipPlayNext(pattern) ? F_ONEX : 0; // the scene's own "play next" flag (plan 006)
    }

    // ---- animated marks
    bool playing = sequencer.isPlay();
    bool anyBlink = false, userQueued = false;
    for (uint8_t t = 0; t < kTracks; t++)
    {
        m.playRow[t] = 0xFF;
        m.qRow[t] = 0xFF;
        int rel = (int)cp[t] - (int)top_;
        if (!seqLauncher.stopped(t) && rel >= 0 && rel < kVisRows)
        {
            m.playRow[t] = (uint8_t)rel;
            if (playing)
            {
                uint8_t pos = 0, ustep = 1, last = 0;
                sequencer.seqTrackPos(t, pos, ustep, last);
                m.prog[t] = progressPx(pos, ustep, (uint16_t)(last + 1));
            }
        }
        Launcher::Kind k = seqLauncher.queuedKind(t);
        if (k != Launcher::K_NONE)
        {
            int qrel = k == Launcher::K_CLIP ? (int)seqLauncher.queuedPattern(t) - (int)top_ : rel;
            bool follow = seqLauncher.queuedQuant(t) == Q_CLIPEND;
            if (qrel >= 0 && qrel < kVisRows)
            {
                m.qRow[t] = (uint8_t)qrel;
                m.qKind[t] = follow ? MK_FOLLOW : MK_BLINK;
            }
            if (!follow)
            {
                anyBlink = anyBlink || (qrel >= 0 && qrel < kVisRows);
                userQueued = true;
            }
        }
    }
    uint8_t scene = seqSessionQueuedScene();
    m.sceneRow = 0xFF;
    if (scene && (int)scene >= (int)top_ && (int)scene < (int)top_ + kVisRows)
    {
        m.sceneRow = (uint8_t)(scene - top_);
        anyBlink = true;
    }
    m.songRow = 0xFF;
    if (playing && (int)own >= (int)top_ && (int)own < (int)top_ + kVisRows)
        m.songRow = (uint8_t)(own - top_);
    m.blink = anyBlink && blinkPhase(nowMs_) ? 1 : 0;

    m.countdown = 0xFF;
    if (playing && (userQueued || scene))
    {
        uint16_t len = seqLauncher.length(), pos = seqLauncher.position();
        uint16_t n = 0xFFFFu;
        if (scene)
            n = seqLauncher.scenePending() // a scene with bar / now quantise counts like a clip, from the pattern's own position
                  ? stepsToQuant(seqLauncher.sceneQuant(), seqLauncher.globalPosition(), seqLauncher.globalLength())
                  : stepsToQuant(Q_PATTERN, pos, len);
        if (userQueued)
        {
            uint16_t q = stepsToQuant(seqLauncher.quant(), pos, len);
            if (q < n) n = q;
        }
        m.countdown = n > 254 ? 254 : (uint8_t)n;
    }
}

} // namespace seqsession
