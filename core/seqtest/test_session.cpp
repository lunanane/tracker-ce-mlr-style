// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Host tests of the Session view logic (seqSessionLogic.h).
#include "seqSessionLogic.h"
#include <stdio.h>
#include <string.h>

using namespace seqsession;

static int s_fail = 0, s_checks = 0;
#define SCHECK(c)                                                       \
    do                                                                  \
    {                                                                   \
        s_checks++;                                                     \
        if (!(c))                                                       \
        {                                                               \
            s_fail++;                                                   \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);         \
        }                                                               \
    } while (0)

static void test_quant()
{
    SCHECK(quantHit(Q_BAR, 0) && quantHit(Q_BAR, 16) && quantHit(Q_BAR, 32));
    SCHECK(!quantHit(Q_BAR, 1) && !quantHit(Q_BAR, 15) && !quantHit(Q_BAR, 17));
    SCHECK(quantHit(Q_PATTERN, 0) && !quantHit(Q_PATTERN, 16));
    SCHECK(quantHit(Q_NOW, 5) && quantHit(Q_NOW, 0));
    SCHECK(nextQuant(Q_BAR) == Q_PATTERN && nextQuant(Q_PATTERN) == Q_NOW && nextQuant(Q_NOW) == Q_BAR);
}

static void test_window()
{
    Window w;
    SCHECK(w.first == 1 && w.pattern(0) == 1 && w.pattern(3) == 4);
    SCHECK(!w.scroll(-1) && w.first == 1);
    SCHECK(w.scroll(1) && w.first == 2);
    SCHECK(w.scroll(4) && w.first == 6);
    SCHECK(w.scroll(1000) && w.first == 252 && w.pattern(3) == 255);
    SCHECK(!w.scroll(1) && w.first == 252);
    SCHECK(w.scroll(-1000) && w.first == 1);
    SCHECK(jogToScroll(3, false) == 1 && jogToScroll(-9, false) == -1 && jogToScroll(0, false) == 0);
    SCHECK(jogToScroll(1, true) == 4 && jogToScroll(-1, true) == -4);
}

static void test_leds_and_pads()
{
    SCHECK(clipState(false, false, false) == CS_EMPTY);
    SCHECK(clipState(true, false, false) == CS_NOTES);
    SCHECK(clipState(true, true, false) == CS_PLAYING);
    SCHECK(clipState(true, true, true) == CS_QUEUED);
    SCHECK(clipState(false, false, true) == CS_QUEUED);
    SCHECK(clipLevel(CS_PLAYING, false) == 15 && clipLevel(CS_PLAYING, true) == 15);
    SCHECK(clipLevel(CS_QUEUED, true) == 15 && clipLevel(CS_QUEUED, false) == 0);
    SCHECK(clipLevel(CS_NOTES, true) == 3 && clipLevel(CS_EMPTY, true) == 0);
    SCHECK(blinkPhase(0) != blinkPhase(150));

    // pad map of plan 005 R2: columns 1-8 clips, column 9 empty gap, column 10 scene launch, columns 11-12 empty
    // (STOP / QUANT / MODE moved to screen buttons: no pad of the old STOP-T / STOP-ALL / QUANT cells does anything any more)
    int clips = 0, scenes = 0, none = 0;
    for (uint8_t r = 0; r < 4; r++)
        for (uint8_t c = 0; c < 12; c++)
        {
            PadKind k = mapPad(c, r).kind;
            if (c < 8) SCHECK(k == PK_CLIP);
            else if (c == 9) SCHECK(k == PK_SCENE);
            else SCHECK(k == PK_NONE);
            switch (k)
            {
                case PK_CLIP: clips++; break;
                case PK_SCENE: scenes++; break;
                default: none++; break;
            }
        }
    SCHECK(clips == 32 && scenes == 4 && none == 12);
    SCHECK(mapPad(8, 0).kind == PK_NONE && mapPad(8, 3).kind == PK_NONE && mapPad(9, 0).kind == PK_SCENE && mapPad(9, 3).kind == PK_SCENE);
    SCHECK(mapPad(10, 0).kind == PK_NONE && mapPad(11, 0).kind == PK_NONE && mapPad(11, 3).kind == PK_NONE);
    // pad (col, row) carries its own coordinates
    SCHECK(mapPad(5, 2).col == 5 && mapPad(5, 2).row == 2);

    SCHECK(groupFromButton(0, 8) == 0 && groupFromButton(1, 8) == 8 && groupFromButton(2, 8) == 0xFF);
    SCHECK(groupFromButton(0, 4) == 0 && groupFromButton(1, 4) == 4 && groupFromButton(2, 4) == 8 && groupFromButton(3, 4) == 12);
    SCHECK(groupFromButton(4, 4) == 0xFF && groupFromButton(6, 4) == 0xFF && groupFromButton(0, 0) == 0xFF);
    SCHECK(alignGroup(12, 8) == 8 && alignGroup(4, 8) == 0 && alignGroup(8, 4) == 8 && alignGroup(0, 4) == 0);
    // host layout: 6 (button 6) belongs to the active layer, 7 is an empty gap, 8 switches CLIPS <-> CUT
    SCHECK(kButtonLayer == 5 && kButtonGap == 6 && kButtonSwitch == 7);
    SCHECK(kButtonLayer < kButtonGap && kButtonGap < kButtonSwitch && kButtonSwitch < 8);
    // the buttons that choose a group (CUT: 1-4) never collide with the layer button, the gap or the switch
    for (uint8_t b = 0; b < 8; b++)
        if (groupFromButton(b, 4) != 0xFF) SCHECK(b < kButtonLayer);
}

static void test_launcher_bar()
{
    Launcher l;
    uint8_t p = 0;
    SCHECK(l.postClip(2, 7));
    SCHECK(l.queuedPattern(2) == 0); // not drained yet
    l.drain();
    SCHECK(l.queuedPattern(2) == 7 && l.queuedKind(2) == Launcher::K_CLIP && l.anyQueued());
    SCHECK(l.isQueued(2, 7, 1) && !l.isQueued(2, 1, 1));
    SCHECK(l.isPlaying(2, 1, 1) && !l.isPlaying(2, 7, 1));
    // not on a bar
    SCHECK(l.onStep(2, 5, p) == Launcher::A_NONE && l.queuedPattern(2) == 7);
    // other tracks are not affected
    SCHECK(l.onStep(3, 16, p) == Launcher::A_NONE);
    SCHECK(l.onStep(2, 16, p) == Launcher::A_CLIP && p == 7);
    SCHECK(l.current(2) == 7 && l.queuedPattern(2) == 0 && !l.anyQueued());
    SCHECK(l.isPlaying(2, 7, 1) && !l.isPlaying(2, 1, 1));
    SCHECK(l.onStep(2, 32, p) == Launcher::A_NONE);
}

static void test_launcher_quant_modes()
{
    Launcher l;
    uint8_t p = 0;
    l.setQuant(Q_PATTERN);
    l.postClip(0, 4);
    l.drain();
    SCHECK(l.onStep(0, 16, p) == Launcher::A_NONE);
    SCHECK(l.onStep(0, 0, p) == Launcher::A_CLIP && p == 4);
    l.setQuant(Q_NOW);
    l.postClip(0, 5);
    l.drain();
    SCHECK(l.onStep(0, 9, p) == Launcher::A_CLIP && p == 5 && l.current(0) == 5);
}

static void test_launcher_loading_blocks()
{
    Launcher l;
    uint8_t p = 0;
    l.postClip(1, 3);
    l.drain();
    l.beginLoad(1); // a newer clip is being read into the same buffer slot
    SCHECK(l.onStep(1, 16, p) == Launcher::A_NONE && l.queuedPattern(1) == 3);
    l.postClip(1, 9);
    l.drain();
    SCHECK(l.onStep(1, 32, p) == Launcher::A_NONE && l.queuedPattern(1) == 9);
    l.endLoad(1);
    SCHECK(l.onStep(1, 48, p) == Launcher::A_CLIP && p == 9);
    // a stop is not blocked by loading
    l.beginLoad(2);
    l.postStop(2);
    l.drain();
    SCHECK(l.onStep(2, 0, p) == Launcher::A_STOP);
    l.endLoad(2);
}

static void test_launcher_stop()
{
    Launcher l;
    uint8_t p = 0;
    l.postStop(4);
    l.drain();
    SCHECK(l.queuedKind(4) == Launcher::K_STOP && l.queuedPattern(4) == 0);
    SCHECK(l.isQueued(4, 1, 1)); // the playing clip blinks
    SCHECK(l.onStep(4, 3, p) == Launcher::A_NONE && !l.stopped(4));
    SCHECK(l.onStep(4, 16, p) == Launcher::A_STOP && l.stopped(4) && l.stoppedMask() == 0x10);
    SCHECK(!l.isPlaying(4, 1, 1));
    // a launch un-stops the track
    l.postClip(4, 2);
    l.drain();
    SCHECK(l.onStep(4, 32, p) == Launcher::A_CLIP && !l.stopped(4) && l.isPlaying(4, 2, 1));
    // stop all
    l.postStopAll();
    l.drain();
    int n = 0;
    for (uint8_t t = 0; t < 16; t++)
        if (l.onStep(t, 48, p) == Launcher::A_STOP) n++;
    SCHECK(n == 16 && l.stoppedMask() == 0xFFFF);
}

static void test_launcher_switch_and_transport()
{
    Launcher l;
    uint8_t p = 0;
    l.postClip(0, 6);
    l.postStop(1);
    l.drain();
    l.onStep(0, 0, p);
    l.postStop(2);
    l.drain();
    l.onStep(2, 0, p);
    SCHECK(l.stopped(2) && l.current(0) == 6);
    l.postClip(3, 8);
    l.drain();
    l.onPatternSwitched(); // scene / song switch
    SCHECK(l.current(0) == 0 && l.stoppedMask() == 0 && !l.anyQueued());
    SCHECK(l.isPlaying(0, 10, 10) && !l.isPlaying(0, 6, 10));

    // transport stop: stopped tracks play again, queued stops are dropped, queued clips are kept
    l.postStop(5);
    l.postClip(6, 3);
    l.drain();
    l.onStep(5, 0, p);
    l.postStop(7);
    l.drain();
    l.onTransportStop();
    SCHECK(l.stoppedMask() == 0 && l.queuedKind(7) == Launcher::K_NONE && l.queuedPattern(6) == 3);
    // while the transport is stopped a queued clip applies without quantise
    SCHECK(l.flush(6, p) == Launcher::A_CLIP && p == 3 && l.current(6) == 3);
    SCHECK(l.flush(6, p) == Launcher::A_NONE);
}

static void test_launcher_ring_overflow()
{
    Launcher l;
    int ok = 0;
    for (int i = 0; i < 40; i++)
        if (l.postClip(i % 16, 2)) ok++;
    SCHECK(ok == 31); // ring of 32 keeps one slot free
    l.drain();
    SCHECK(l.postClip(0, 2));
    SCHECK(!l.postClip(16, 2) && !l.postClip(0, 0)); // bad track / pattern 0 rejected
}

static void test_save_merge_and_launched()
{
    Launcher l;
    uint8_t p = 0;
    SCHECK(l.launchedMask(5) == 0);
    l.postClip(2, 7);
    l.postClip(3, 5); // the loaded pattern: not "launched" (its data is the pattern's own)
    l.drain();
    l.onStep(2, 0, p);
    l.onStep(3, 0, p);
    SCHECK(l.launchedMask(5) == 0x0004);
    SCHECK(l.launchedMask(7) == 0x0008);
    l.postClip(15, 1);
    l.drain();
    l.onStep(15, 0, p);
    SCHECK(l.launchedMask(5) == 0x8004);
    SCHECK(saveSource(0x8004, 2, true) == SRC_FILE && saveSource(0x8004, 15, false) == SRC_CLEAR);
    SCHECK(saveSource(0x8004, 3, true) == SRC_RAM && saveSource(0, 0, false) == SRC_RAM);
    // read errors never count as "no file"
    SCHECK(mergeMode(-1) == MERGE_ABORT && mergeMode(0) == MERGE_NO_FILE && mergeMode(1) == MERGE_FROM_FILE);
    SCHECK(clearUndoOnMask(1, 0) && !clearUndoOnMask(0, 0) && !clearUndoOnMask(3, 1) && !clearUndoOnMask(0, 4));
    l.onPatternSwitched();
    SCHECK(l.launchedMask(5) == 0);
}

static void test_cancel_and_counter()
{
    Launcher l;
    uint8_t p = 0;
    l.postClip(1, 4);
    l.postStop(2);
    l.drain();
    l.postCancel(1);
    l.postCancel(2);
    SCHECK(!l.postCancel(16));
    l.drain();
    SCHECK(l.queuedKind(1) == Launcher::K_NONE && l.queuedKind(2) == Launcher::K_NONE);
    SCHECK(l.onStep(1, 16, p) == Launcher::A_NONE);

    // own counter follows the global position until the first launch, then runs on with the frozen length
    Launcher c;
    SCHECK(c.advance(0, 32) == 0 && c.advance(1, 32) == 1 && c.length() == 32);
    c.postClip(0, 3);
    c.drain();
    SCHECK(c.advance(16, 32) == 16);
    SCHECK(c.onStep(0, 16, p) == Launcher::A_CLIP); // frozen from here
    // the clip on track 1 is 8 steps long: the global position now wraps at 8, the counter keeps the old length
    uint16_t g = 17, last = 0;
    for (int i = 0; i < 15; i++)
    {
        last = c.advance((uint16_t)(g % 8), 8);
        g++;
    }
    SCHECK(last == 31);
    SCHECK(c.advance(0, 8) == 0); // wrapped at the frozen length 32
    SCHECK(c.advance(1, 8) == 1);
    c.onPatternSwitched();
    SCHECK(c.advance(5, 64) == 5 && c.length() == 64);
}

// ---------------------------------------------------------------------------------------------------------------------------
// Plan 005 R2: clip launcher (rows = patterns, window navigation, pad gestures, quantise, Play 1x follow action, LIVE, panic)

static void test_quant_cycle_and_countdown()
{
    // screen button 2 taps through the three global values and never lands on the follow-action quantise
    Quant q = Q_BAR;
    for (int i = 0; i < 9; i++)
    {
        SCHECK(q != Q_CLIPEND && q < Q_COUNT);
        q = nextQuant(q);
    }
    SCHECK(q == Q_BAR);
    SCHECK(Q_CLIPEND >= Q_COUNT && (int)Q_CLIPEND != (int)kQuantGlobal);
    SCHECK(quantShort(Q_BAR)[0] == 'b' && quantShort(Q_PATTERN)[0] == 'p' && quantShort(Q_NOW)[0] == 'n');

    // countdown to the next launch point (steps, >= 1)
    SCHECK(stepsToQuant(Q_NOW, 5, 32) == 1);
    SCHECK(stepsToQuant(Q_BAR, 0, 32) == 16 && stepsToQuant(Q_BAR, 1, 32) == 15 && stepsToQuant(Q_BAR, 15, 32) == 1);
    SCHECK(stepsToQuant(Q_BAR, 16, 32) == 16 && stepsToQuant(Q_BAR, 31, 32) == 1);
    SCHECK(stepsToQuant(Q_PATTERN, 0, 32) == 32 && stepsToQuant(Q_PATTERN, 20, 32) == 12 && stepsToQuant(Q_PATTERN, 31, 32) == 1);
    SCHECK(stepsToQuant(Q_BAR, 10, 12) == 2);       // short pattern: the wrap comes before the bar
    SCHECK(stepsToQuant(Q_BAR, 3, 0) == 1);         // unknown length never divides by zero
    SCHECK(stepsToQuant(Q_PATTERN, 99, 32) == 32);  // position beyond the length counts as 0
    // the countdown agrees with quantHit: after n steps the launch point is reached, and not before (except immediate)
    for (uint16_t pos = 0; pos < 40; pos++)
        for (int qi = 0; qi < Q_COUNT; qi++)
        {
            uint16_t len = 40, n = stepsToQuant((Quant)qi, pos, len);
            SCHECK(n >= 1 && quantHit((Quant)qi, (uint16_t)((pos + n) % len)));
            for (uint16_t k = 1; k < n; k++)
                SCHECK(!quantHit((Quant)qi, (uint16_t)((pos + k) % len)));
        }
}

static void test_navigation()
{
    // Up / Down move the pad window by one row (pattern), Shift by a whole window
    SCHECK(arrowToScroll(true, false) == 1 && arrowToScroll(false, false) == -1);
    SCHECK(arrowToScroll(true, true) == 4 && arrowToScroll(false, true) == -4);
    Window w;
    SCHECK(!w.scroll(arrowToScroll(false, false)) && w.first == 1);        // clamped at the top
    SCHECK(w.scroll(arrowToScroll(true, true)) && w.first == 5);
    SCHECK(w.scroll(arrowToScroll(false, false)) && w.first == 4);
    w.first = 252;
    SCHECK(!w.scroll(arrowToScroll(true, false)) && w.first == 252 && w.pattern(3) == 255); // clamped at the end
    SCHECK(!w.scroll(arrowToScroll(true, true)) && w.first == 252);

    // Left / Right: tracks 1-8 <-> 9-16, clamped, no wrap
    SCHECK(stepGroup(0, true, 8) == 8 && stepGroup(8, true, 8) == 8);
    SCHECK(stepGroup(8, false, 8) == 0 && stepGroup(0, false, 8) == 0);
    SCHECK(stepGroup(0, true, 4) == 4 && stepGroup(12, true, 4) == 12 && stepGroup(12, false, 4) == 8);
    SCHECK(stepGroup(5, true, 8) == 8 && stepGroup(11, false, 8) == 0); // an unaligned start is aligned first
    SCHECK(stepGroup(3, true, 0) == 3 && stepGroup(3, true, 17) == 3);

    // the grid shows 12 patterns and keeps the 4-row window inside
    SCHECK(viewTop(1, 1) == 1 && viewTop(1, 9) == 1);          // window 9-12 is the last visible rows
    SCHECK(viewTop(1, 10) == 2 && viewTop(1, 20) == 12);       // scrolls just enough
    SCHECK(viewTop(20, 5) == 5 && viewTop(20, 17) == 17 && viewTop(20, 18) == 18 && viewTop(20, 21) == 20 && viewTop(20, 29) == 21);
    SCHECK(viewTop(1, 252) == 244 && viewTop(250, 252) == 244 && viewTop(255, 1) == 1);
    for (int top = 1; top <= 244; top += 7)
        for (int wf = 1; wf <= 252; wf++)
        {
            int t = viewTop((uint8_t)top, (uint8_t)wf);
            SCHECK(t >= 1 && t + kVisRows - 1 <= kPatternMax && wf >= t && wf + kWinRows - 1 <= t + kVisRows - 1);
        }
}

static void test_pad_gestures()
{
    PadKind clip = PK_CLIP, scene = PK_SCENE;
    // plain: launch a clip with content, an empty pad stops its track, the scene pad launches the row
    SCHECK(padOp(clip, false, false, true) == OP_LAUNCH);
    SCHECK(padOp(clip, false, false, false) == OP_STOP_TRACK);
    SCHECK(padOp(scene, false, false, false) == OP_SCENE && padOp(scene, false, false, true) == OP_SCENE);
    // hold 3 (stop): clip pad = stop that track (also with content), scene pad = stop all
    SCHECK(padOp(clip, false, true, true) == OP_STOP_TRACK && padOp(clip, false, true, false) == OP_STOP_TRACK);
    SCHECK(padOp(scene, false, true, true) == OP_STOP_ALL);
    // hold 1 (mode): pads only focus, they never launch or stop
    SCHECK(padOp(clip, true, false, true) == OP_FOCUS && padOp(clip, true, false, false) == OP_FOCUS);
    SCHECK(padOp(scene, true, false, true) == OP_FOCUS && padOp(clip, true, true, true) == OP_FOCUS && padOp(scene, true, true, true) == OP_FOCUS);
    SCHECK(padOp(PK_NONE, false, false, true) == OP_NONE && padOp(PK_NONE, true, true, false) == OP_NONE);

    // screen buttons of the CLIPS layer: 1 mode, 2 quantise, 3 stop; 7 / 8 stay the layer buttons of the host
    SCHECK(kBtnMode == 0 && kBtnQuant == 1 && kBtnStop == 2);
    SCHECK((int)kBtnStop < (int)kButtonLayer);

    // pad LEDs: follow action armed = steady medium, the dim light of an empty pad of a sounding track
    SCHECK(clipState(true, true, true, true) == CS_FOLLOW && clipState(true, true, true, false) == CS_QUEUED);
    SCHECK(clipState(true, false, false, true) == CS_NOTES); // follow only means something while queued
    SCHECK(clipLevel(CS_FOLLOW, true) == 8 && clipLevel(CS_FOLLOW, false) == 8);
    SCHECK(emptyPadLevel(true) == 1 && emptyPadLevel(false) == 0);
}

static void test_play_mode_wheel()
{
    // the wheel steps Loop <-> Play 1x, clamped, one detent = one step whatever the speed
    SCHECK(stepClipMode(seqclips::CLIP_LOOP, 1) == seqclips::CLIP_PLAY1X);
    SCHECK(stepClipMode(seqclips::CLIP_LOOP, 7) == seqclips::CLIP_PLAY1X);
    SCHECK(stepClipMode(seqclips::CLIP_PLAY1X, 1) == seqclips::CLIP_PLAY1X);
    SCHECK(stepClipMode(seqclips::CLIP_PLAY1X, -1) == seqclips::CLIP_LOOP && stepClipMode(seqclips::CLIP_LOOP, -3) == seqclips::CLIP_LOOP);
    SCHECK(stepClipMode(seqclips::CLIP_LOOP, 0) == seqclips::CLIP_LOOP && stepClipMode(seqclips::CLIP_PLAY1X, 0) == seqclips::CLIP_PLAY1X);
    SCHECK(clipModeName(seqclips::CLIP_LOOP)[0] == 'L' && clipModeName(seqclips::CLIP_PLAY1X)[0] == 'P');

    // wheel + focus: one clip (per-clip data, other rows untouched; the scene flag is its own value since plan 006)
    seqclips::ClipModes cm;
    SCHECK(cm.mode(7, 3) == seqclips::CLIP_LOOP); // default playback is loop
    SCHECK(cm.setMode(7, 3, stepClipMode(cm.mode(7, 3), 1)));
    SCHECK(cm.mode(7, 3) == seqclips::CLIP_PLAY1X && cm.mode(7, 4) == seqclips::CLIP_LOOP && cm.mode(8, 3) == seqclips::CLIP_LOOP);
}

static void test_clip_end_quant()
{
    Launcher l;
    uint8_t p = 0;
    // the clip on track 2 starts at once (bar quant, position 0) and is 4 steps long
    l.postClip(2, 7);
    l.drain();
    SCHECK(l.onStep(2, 0, p, 4) == Launcher::A_CLIP && p == 7 && l.applied(2) == 1);
    // follow action: the next clip waits for the end of this one, not for a bar or the pattern end
    SCHECK(l.postClip(2, 8, Q_CLIPEND));
    l.drain();
    SCHECK(l.queuedKind(2) == Launcher::K_CLIP && l.queuedPattern(2) == 8 && l.queuedQuant(2) == Q_CLIPEND);
    SCHECK(l.onStep(2, 16, p, 4) == Launcher::A_NONE); // a bar boundary does not fire it
    SCHECK(l.onStep(2, 17, p, 4) == Launcher::A_NONE);
    SCHECK(l.onStep(2, 18, p, 4) == Launcher::A_NONE);
    SCHECK(l.onStep(2, 19, p, 4) == Launcher::A_CLIP && p == 8); // the 4th step after the start: the clip would restart here
    SCHECK(l.current(2) == 8 && l.applied(2) == 2 && l.queuedKind(2) == Launcher::K_NONE);
    SCHECK(l.queuedQuant(2) == l.quant()); // the command's own quantise is gone with it

    // the counter keeps running without a command: queue late, the hit lands on a multiple of the clip length
    Launcher m;
    m.postClip(5, 3);
    m.drain();
    SCHECK(m.onStep(5, 0, p, 3) == Launcher::A_CLIP);
    for (int i = 0; i < 7; i++) // 7 steps = two loops + 1
        SCHECK(m.onStep(5, (uint16_t)(1 + i), p, 3) == Launcher::A_NONE);
    m.postStop(5, Q_CLIPEND);
    m.drain();
    SCHECK(m.queuedKind(5) == Launcher::K_STOP && m.queuedQuant(5) == Q_CLIPEND && m.isQueued(5, 3, 1));
    SCHECK(m.onStep(5, 8, p, 3) == Launcher::A_NONE); // 8 steps after the start: not a multiple of 3
    SCHECK(m.onStep(5, 9, p, 3) == Launcher::A_STOP && m.stopped(5)); // 9 = three loops: the track stops instead of looping again

    // clip length unknown (0): the clip end never hits (old callers)
    Launcher n;
    n.postClip(1, 2, Q_CLIPEND);
    n.drain();
    for (int i = 0; i < 40; i++)
        SCHECK(n.onStep(1, (uint16_t)i, p) == Launcher::A_NONE);
    SCHECK(n.flush(1, p) == Launcher::A_CLIP); // transport stopped: pending commands apply at once whatever their quantise

    // the newest tap wins: a performer launch replaces an armed follow action and uses the global quantise again
    Launcher o;
    o.postClip(0, 4, Q_CLIPEND);
    o.drain();
    o.postClip(0, 6);
    o.drain();
    SCHECK(o.queuedPattern(0) == 6 && o.queuedQuant(0) == Q_BAR);
    o.setQuant(Q_NOW);
    SCHECK(o.queuedQuant(0) == Q_NOW); // follows the global setting while the command has none of its own
    o.postClip(0, 4, Q_CLIPEND);
    o.drain();
    o.postCancel(0); // tap on the playing clip: the armed follow action is dropped, quantise reset
    o.drain();
    SCHECK(o.queuedKind(0) == Launcher::K_NONE && o.queuedQuant(0) == Q_NOW);
    // stop all resets every own quantise
    o.postClip(3, 4, Q_CLIPEND);
    o.postStopAll();
    o.drain();
    SCHECK(o.queuedKind(3) == Launcher::K_STOP && o.queuedQuant(3) == Q_NOW);

    // a pattern switch (scene, song) restarts every track: the step counter and the instance epoch
    Launcher s;
    s.postClip(4, 5);
    s.drain();
    s.onStep(4, 0, p, 4);
    s.onStep(4, 1, p, 4);
    s.onStep(4, 2, p, 4);
    uint8_t e0 = s.epoch();
    s.onPatternSwitched();
    SCHECK(s.epoch() == (uint8_t)(e0 + 1));
    s.postStop(4, Q_CLIPEND);
    s.drain();
    SCHECK(s.onStep(4, 1, p, 4) == Launcher::A_NONE && s.onStep(4, 2, p, 4) == Launcher::A_NONE && s.onStep(4, 3, p, 4) == Launcher::A_NONE);
    SCHECK(s.onStep(4, 4, p, 4) == Launcher::A_STOP); // 4 steps after the switch, not 1
    // Stop of the transport: the next Play counts from the step it starts at (advance is called once per step by track 1)
    Launcher u;
    u.postClip(6, 5);
    u.drain();
    u.onStep(6, 0, p, 4);
    u.onStep(6, 1, p, 4);
    u.onTransportStop();
    u.postStop(6, Q_CLIPEND);
    u.drain();
    for (uint16_t g = 1; g <= 3; g++)
    {
        u.advance(g, 40);
        SCHECK(u.onStep(6, g, p, 4) == Launcher::A_NONE);
    }
    u.advance(4, 40);
    SCHECK(u.onStep(6, 4, p, 4) == Launcher::A_STOP); // Play from step 0: the clip ends after 4 steps

    // Play started away from step 0 (at 6, clip of 4 steps): the clip end is where the clip wraps (8), not 4 steps after Play
    Launcher w;
    w.postClip(7, 5);
    w.drain();
    w.onStep(7, 0, p, 4);
    w.onTransportStop();
    w.postStop(7, Q_CLIPEND);
    w.drain();
    w.advance(7, 40); // first step boundary after Play from 6
    SCHECK(w.onStep(7, 7, p, 4) == Launcher::A_NONE);
    w.advance(8, 40);
    SCHECK(w.onStep(7, 8, p, 4) == Launcher::A_STOP);

    // the other quantise values are unchanged by the clip length argument
    Launcher v;
    v.postClip(8, 9);
    v.drain();
    SCHECK(v.onStep(8, 5, p, 4) == Launcher::A_NONE && v.onStep(8, 16, p, 4) == Launcher::A_CLIP);
}

static void test_follow_action()
{
    // Play 1x: the next row down, or the track stops when that row has nothing for the track (or there is no row left)
    FollowTarget f = followTarget(7, true);
    SCHECK(f.launch && f.pattern == 8);
    f = followTarget(7, false);
    SCHECK(!f.launch);
    f = followTarget(255, true);
    SCHECK(!f.launch); // last row: nothing below
    f = followTarget(254, true);
    SCHECK(f.launch && f.pattern == 255);
    f = followTarget(0, true);
    SCHECK(!f.launch);

    // the follow action is queued once per instance of the playing clip
    FollowArms a;
    SCHECK(a.needs(3, 7, 1, 0));
    a.mark(3, 7, 1, 0);
    SCHECK(!a.needs(3, 7, 1, 0));            // already queued: a tap that cancelled it must not bring it back
    SCHECK(a.needs(3, 8, 1, 0));             // another clip plays
    SCHECK(a.needs(3, 7, 2, 0));             // the same clip started again (a command was applied)
    SCHECK(a.needs(3, 7, 1, 1));             // the pattern switched
    SCHECK(a.needs(4, 7, 1, 0));             // per track
    a.mark(15, 255, 255, 255);
    SCHECK(!a.needs(15, 255, 255, 255) && a.needs(15, 255, 255, 0));
    a.reset();
    SCHECK(a.needs(3, 7, 1, 0) && a.needs(15, 255, 255, 255) && !a.needs(16, 1, 1, 1));

    // end to end with the launcher: a 1x clip of 4 steps, then the next row, then the track stops
    Launcher l;
    FollowArms arms;
    uint8_t p = 0;
    l.postClip(1, 5);
    l.drain();
    l.onStep(1, 0, p, 4);
    int launches = 0, stops = 0;
    for (int tick = 1; tick < 40; tick++)
    {
        // main loop between steps: queue the follow action when the instance has none yet
        uint8_t cp = l.current(1);
        if (!l.stopped(1) && l.queuedKind(1) == Launcher::K_NONE && cp >= 5 && arms.needs(1, cp, l.applied(1), l.epoch()))
        {
            FollowTarget ft = followTarget(cp, cp < 7); // clips 6 and 7 have content, 8 has none
            if (ft.launch)
                l.postClip(1, ft.pattern, Q_CLIPEND);
            else
                l.postStop(1, Q_CLIPEND);
            arms.mark(1, cp, l.applied(1), l.epoch());
            l.drain();
        }
        Launcher::Action act = l.onStep(1, (uint16_t)tick, p, 4);
        if (act == Launcher::A_CLIP) launches++;
        if (act == Launcher::A_STOP) stops++;
    }
    SCHECK(launches == 2 && stops == 1 && l.stopped(1) && l.current(1) == 7);
}

static void test_panic()
{
    Launcher l;
    uint8_t p = 0;
    l.postClip(0, 6);
    l.postStop(1);
    l.postClip(4, 3, Q_CLIPEND);
    l.drain();
    l.onStep(0, 0, p, 4);
    l.postStopAll();
    l.postClip(2, 9);       // still in the ring when the panic comes
    l.panic();
    l.drain();              // nothing is left to drain
    for (uint8_t t = 0; t < 16; t++)
        SCHECK(l.queuedKind(t) == Launcher::K_NONE && !l.stopped(t));
    SCHECK(!l.anyQueued() && l.stoppedMask() == 0);

    // launched clips stay known until the caller restored the tracks, then the pattern switch ends them
    Launcher m;
    m.postClip(3, 8);
    m.drain();
    m.onStep(3, 0, p, 4);
    m.postStop(5);
    m.drain();
    m.onStep(5, 0, p, 4);
    SCHECK(m.stopped(5) && m.launchedMask(2) == 0x0008);
    m.panic();
    SCHECK(!m.stopped(5) && m.launchedMask(2) == 0x0008 && !m.anyQueued());
    m.onPatternSwitched();
    SCHECK(m.launchedMask(2) == 0);

    // panic is idempotent and the launcher keeps working afterwards
    m.panic();
    m.panic();
    SCHECK(m.postClip(1, 4));
    m.drain();
    SCHECK(m.onStep(1, 16, p, 4) == Launcher::A_CLIP && m.current(1) == 4);
}

static void test_grid_geometry()
{
    using namespace clipgrid;
    // everything stays inside the content area: x 0-800, y 29-422 (title bar above, screen button labels below)
    SCHECK(colX(0) == kX0 && colX(15) + kCellW <= sceneX());
    SCHECK(sceneX() + kSceneW < kInfoX && kInfoX < 800 - 100);
    SCHECK(rowY(0) >= 29 + 28 && rowY(kVisRows - 1) + kCellH <= 422 - 24);
    SCHECK(kX0 > kRowNumX + 30); // room for a 3 digit pattern number
    // columns never overlap, the gap after track 8 is wider than the pitch gap, nothing is cut off
    for (uint8_t t = 1; t < 16; t++)
        SCHECK(colX(t) - colX(t - 1) >= kCellW + 1);
    SCHECK(colX(8) - (colX(7) + kCellW) == kBlockGap + (kPitch - kCellW) && colX(1) - (colX(0) + kCellW) == kPitch - kCellW);
    SCHECK(kBlockGap > kPitch - kCellW);
    for (uint8_t r = 1; r < kVisRows; r++)
        SCHECK(rowY(r) - rowY(r - 1) >= kCellH + 1);
    // the pad window frame (3 px outside the cells) of the right-most column fits on the screen
    SCHECK(colX(15) + kCellW + 3 < 800 && sceneX() + kSceneW + 3 < 800);

    // progress bar inside the cell
    SCHECK(progressPx(0, 1, 16) == 0 && progressPx(8, 1, 16) == kCellW / 2);
    SCHECK(progressPx(15, 48, 16) == (uint8_t)(((15u * 48 + 47) * kCellW) / (16 * 48)));
    SCHECK(progressPx(200, 48, 16) == kCellW && progressPx(3, 1, 0) == 0);
    for (uint8_t pos = 0; pos < 128; pos++)
        for (uint8_t u = 1; u <= 48; u += 7)
            SCHECK(progressPx(pos, u, 128) <= kCellW);

    // the structs are compared with memcmp: no padding allowed
    SCHECK(sizeof(View) == 11 + 2 * kVisRows * kTracks + 2 * kVisRows);
    SCHECK(sizeof(Marks) == 4 * kTracks + 4);
    SCHECK(msgText(MSG_NONE)[0] == 0 && msgText(MSG_BUSY)[0] != 0 && msgText(MSG_UNDO_CLEARED)[0] != 0);
}

// ---------------------------------------------------------------------------------------------------------------------------
// Plan 005 R2 review fixes: undrained taps, join phase, pattern-switch ticks, display list budget

static void test_undrained_tap_vs_follow()
{
    Launcher l;
    uint8_t p = 0;
    l.postClip(0, 5);
    l.drain();
    l.onStep(0, 0, p, 4);
    SCHECK(followMayQueue(l, 0)); // quiet: the follow action may be queued

    // a tap of the performer is in the ring but the audio side has not drained it: kind is still NONE
    SCHECK(l.ringFree() == 31);
    SCHECK(l.postClip(0, 9));
    SCHECK(l.ringPending() && l.queuedKind(0) == Launcher::K_NONE && l.ringFree() == 30);
    SCHECK(!followMayQueue(l, 0) && !followMayQueue(l, 3)); // no track: the ring is shared, the buffer slot is not safe
    l.drain();
    SCHECK(!l.ringPending() && l.queuedKind(0) == Launcher::K_CLIP && l.queuedPattern(0) == 9);
    SCHECK(!followMayQueue(l, 0));   // the tap is queued now: the newest tap wins
    SCHECK(followMayQueue(l, 3));    // other tracks are free again

    // a stopped track and a bad track never take a follow action
    l.postStop(2);
    l.drain();
    l.onStep(2, 0, p, 4);
    SCHECK(l.stopped(2) && !followMayQueue(l, 2) && !followMayQueue(l, 16));

    // the ring tells how many commands fit (a batch of follow actions checks it before it starts)
    Launcher f;
    int n = 0;
    while (f.ringFree() > 0 && f.postClip(n % 16, 2))
        n++;
    SCHECK(n == 31 && f.ringFree() == 0 && !f.postStop(0));
    f.drain();
    SCHECK(f.ringFree() == 31);
}

static void test_join_phase_and_switch_ticks()
{
    uint8_t p = 0;
    // Q_NOW join: the clip enters its loop at step 2 (position 6 in a clip of 4), so its end comes after 2 more steps, not 4
    Launcher a;
    a.setQuant(Q_NOW);
    a.postClip(3, 5);
    a.drain();
    SCHECK(a.onStep(3, 6, p, 8) == Launcher::A_CLIP);
    a.setPhase(3, (uint16_t)(6 % 4)); // what seqSessionApplyClip does for a join (restart = false)
    a.postStop(3, Q_CLIPEND);
    a.drain();
    SCHECK(a.onStep(3, 7, p, 4) == Launcher::A_NONE);   // step 3 of the clip
    SCHECK(a.onStep(3, 8, p, 4) == Launcher::A_STOP);   // step 0 again: the clip would restart here
    // a restart (bar) keeps the counter at the first step: the end comes after the whole clip
    Launcher b;
    b.postClip(3, 5);
    b.drain();
    b.onStep(3, 16, p, 8);
    b.postStop(3, Q_CLIPEND);
    b.drain();
    for (int i = 1; i < 4; i++)
        SCHECK(b.onStep(3, (uint16_t)(16 + i), p, 4) == Launcher::A_NONE);
    SCHECK(b.onStep(3, 20, p, 4) == Launcher::A_STOP);
    SCHECK(Launcher().ticks() == 0);

    // The pattern switches in the middle of a step: tracks visited before the switch (0) and after it (15) are both on their first
    // step in this tick, so their clip ends stay in line
    Launcher l;
    l.advance(1, 12);
    l.onStep(0, 1, p, 3);
    l.onPatternSwitched();
    l.onStep(15, 0, p, 3);
    l.postStop(0, Q_CLIPEND);
    l.postStop(15, Q_CLIPEND);
    l.drain();
    for (uint16_t tick = 2; tick <= 3; tick++)
    {
        l.advance(tick, 12);
        SCHECK(l.onStep(0, tick, p, 3) == Launcher::A_NONE);
        SCHECK(l.onStep(15, tick, p, 3) == Launcher::A_NONE);
    }
    l.advance(4, 12);
    Launcher::Action a0 = l.onStep(0, 4, p, 3), a15 = l.onStep(15, 4, p, 3);
    SCHECK(a0 == Launcher::A_STOP && a15 == Launcher::A_STOP); // both end on the same step

    // ticks follow the music: the retry rate limit of the main loop
    Launcher t;
    SCHECK(t.ticks() == 0);
    t.advance(1, 16);
    t.advance(2, 16);
    SCHECK(t.ticks() == 2);
}

static void test_grid_budget()
{
    using namespace clipgrid;
    View v;
    memset(&v, 0, sizeof(v));
    GridPlan empty = planGrid(v);
    SCHECK(empty.outlines && empty.notches && empty.stops && !empty.help); // the help lines need button 1 or 3
    SCHECK(empty.cost > 2000 && empty.cost < kGridBudget);

    v.modeHeld = 1;
    GridPlan held = planGrid(v);
    SCHECK(held.help && held.cost > empty.cost && held.cost <= kGridBudget);
    v.modeHeld = 0;
    v.stopHeld = 1;
    SCHECK(planGrid(v).help);
    v.stopHeld = 0;

    // worst case: every cell armed, every clip Play 1x, every empty cell marked, a message, a button held
    View w;
    memset(&w, 0, sizeof(w));
    for (uint8_t r = 0; r < kVisRows; r++)
    {
        for (uint8_t t = 0; t < kTracks; t++)
        {
            w.cell[r][t] = CELL_FOLLOW;
            w.flags[r][t] = F_ONEX | F_STOPMARK | F_END;
        }
        w.sceneFlags[r] = F_ONEX;
    }
    w.msg = MSG_UNDO_CLEARED;
    w.modeHeld = 1;
    GridPlan worst = planGrid(w);
    SCHECK(worst.cost <= kGridBudget);                 // never above the budget whatever is lit
    SCHECK(!worst.outlines && !worst.stops); // 192 armed cells and 192 dim squares do not fit: dropped, not truncated
    // the mandatory part alone is far below the budget, so the optional parts are what gets dropped
    SCHECK(empty.cost + 7 * textCost(22) < kGridBudget);
    // realistic worst case: one window of 32 stop squares, a dozen notches, a few armed
    View r1;
    memset(&r1, 0, sizeof(r1));
    for (uint8_t r = 0; r < 4; r++)
        for (uint8_t t = 0; t < 8; t++)
            r1.flags[r][t] = F_STOPMARK;
    for (uint8_t t = 0; t < 12; t++)
        r1.flags[5][t] = F_ONEX;
    for (uint8_t t = 0; t < 4; t++)
        r1.cell[6][t] = CELL_FOLLOW;
    GridPlan real = planGrid(r1);
    SCHECK(real.outlines && real.notches && real.stops && real.cost < 4500);
    // costs add up in whole display list words
    SCHECK(kRectCost % 4 == 0 && kOutlineCost % 4 == 0 && textCost(5) % 4 == 0 && kNumberCost % 4 == 0);
}

int sessionTests(int& checks)
{
    test_quant();
    test_window();
    test_leds_and_pads();
    test_launcher_bar();
    test_launcher_quant_modes();
    test_launcher_loading_blocks();
    test_launcher_stop();
    test_launcher_switch_and_transport();
    test_launcher_ring_overflow();
    test_save_merge_and_launched();
    test_cancel_and_counter();
    test_quant_cycle_and_countdown();
    test_navigation();
    test_pad_gestures();
    test_play_mode_wheel();
    test_clip_end_quant();
    test_follow_action();
    test_panic();
    test_grid_geometry();
    test_undrained_tap_vs_follow();
    test_join_phase_and_switch_ticks();
    test_grid_budget();
    checks += s_checks;
    return s_fail;
}
