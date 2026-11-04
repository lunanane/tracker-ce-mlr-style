// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
#include "seqSession/clipsLayer.h"

#include <stdio.h>
#include <string.h>

#include "seqLauncher.h"

namespace seqsession {

void ClipsLayer::enter(uint8_t firstTrack)
{
    first_ = firstTrack;
    stopHeld_ = false;
    uint8_t own = seqSessionOwnPattern();
    if (own < window_.first || own > window_.first + (kWinRows - 1))
        window_.first = Window::clampFirst(own); // show the loaded pattern
    seqSessionSetWindow(window_.first);
}

void ClipsLayer::pad(uint8_t col, uint8_t row, bool down)
{
    PadAction a = mapPad(col, row);
    switch (a.kind)
    {
        case PK_STOP_T: stopHeld_ = down; break;
        case PK_CLIP:
        {
            if (!down) break;
            uint8_t track = (uint8_t)(first_ + col);
            uint8_t pattern = window_.pattern(row);
            const NoteCache& c = seqSessionCache();
            bool ok;
            if (stopHeld_)
                ok = seqSessionStopTrack(track);
            else if (c.first == window_.first && c.isKnown(row) && !c.hasNotes(row, track))
                ok = seqSessionStopTrack(track); // an empty slot stops the track, like in Ableton
            else
                ok = seqSessionLaunchClip(track, pattern);
            if (!ok)
            {
                msg_ = "Busy - try again";
                msgUntil_ = nowMs_ + 1500;
            }
            break;
        }
        case PK_SCENE:
            if (down && !seqSessionLaunchScene(window_.pattern(row)))
            {
                msg_ = "Busy - try again";
                msgUntil_ = nowMs_ + 1500;
            }
            break;
        case PK_STOP_ALL:
            if (down && !seqSessionStopAll())
            {
                msg_ = "Busy - try again";
                msgUntil_ = nowMs_ + 1500;
            }
            break;
        case PK_QUANT:
            if (down)
                seqLauncher.setQuant(nextQuant(seqLauncher.quant()));
            break;
        default: break;
    }
}

void ClipsLayer::leds(uint8_t out[48])
{
    bool blink = blinkPhase(nowMs_);
    const NoteCache& cache = seqSessionCache();
    bool cacheOk = cache.first == window_.first;
    uint8_t own = seqSessionOwnPattern();
    uint8_t nextScene = seqSessionQueuedScene();

    for (uint8_t row = 0; row < kRows; row++)
    {
        uint8_t pattern = window_.pattern(row);
        for (uint8_t col = 0; col < kCols; col++)
        {
            uint8_t level = 0;
            PadAction a = mapPad(col, row);
            switch (a.kind)
            {
                case PK_CLIP:
                {
                    uint8_t t = (uint8_t)(first_ + col);
                    bool notes = cacheOk && cache.hasNotes(row, t);
                    level = clipLevel(clipState(notes, seqLauncher.isPlaying(t, pattern, own), seqLauncher.isQueued(t, pattern, own)), blink);
                    break;
                }
                case PK_SCENE:
                {
                    bool any = false;
                    if (cacheOk && cache.isKnown(row) && cache.mask[row])
                        any = true;
                    level = clipLevel(clipState(any, pattern == own, nextScene == pattern), blink);
                    break;
                }
                case PK_STOP_T: level = stopHeld_ ? 15 : 4; break;
                case PK_STOP_ALL: level = 4; break;
                case PK_QUANT: level = (uint8_t)(5 + 5 * seqLauncher.quant()); break; // next bar 5, pattern end 10, immediate 15
                default: break;
            }
            out[row * kCols + col] = level;
        }
    }
}

bool ClipsLayer::jog(int16_t value, bool shift)
{
    if (window_.scroll(jogToScroll(value, shift)))
        seqSessionSetWindow(window_.first);
    return true;
}

void ClipsLayer::info(char lines[4][48])
{
    uint8_t own = seqSessionOwnPattern();
    snprintf(lines[0], 48, "Patterns %d-%d (jog = scroll)   Quant: %s", window_.first, window_.first + kWinRows - 1, quantName(seqLauncher.quant()));

    // what every track of the group plays now: pattern number, "--" = stopped
    char* p = lines[1];
    int n = snprintf(p, 48, "Now");
    for (uint8_t i = 0; i < 8 && n < 40; i++)
    {
        uint8_t t = (uint8_t)(first_ + i);
        if (seqLauncher.stopped(t))
            n += snprintf(p + n, 48 - n, " %d:--", t + 1);
        else
            n += snprintf(p + n, 48 - n, " %d:%d", t + 1, seqLauncher.current(t) ? seqLauncher.current(t) : own);
    }

    // queued launches of the group + the queued scene
    p = lines[2];
    n = 0;
    p[0] = 0;
    for (uint8_t i = 0; i < 8 && n < 36; i++)
    {
        uint8_t t = (uint8_t)(first_ + i);
        if (seqLauncher.queuedKind(t) == Launcher::K_CLIP)
            n += snprintf(p + n, 48 - n, "%s%d>%d", n ? " " : "Next ", t + 1, seqLauncher.queuedPattern(t));
        else if (seqLauncher.queuedKind(t) == Launcher::K_STOP)
            n += snprintf(p + n, 48 - n, "%s%d>stop", n ? " " : "Next ", t + 1);
    }
    uint8_t scene = seqSessionQueuedScene();
    if (scene && n < 36)
        snprintf(p + n, 48 - n, "%sscene %d", n ? "  " : "Next ", scene);

    if (msg_ && nowMs_ < msgUntil_)
        snprintf(lines[3], 48, "%s", msg_);
    else
        snprintf(lines[3], 48, "%s", stopHeld_ ? "STOP + clip = stop that track" : "Tap a clip to launch, scene pad = whole pattern");
}

} // namespace seqsession
