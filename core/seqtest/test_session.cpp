// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Host tests of the Session view logic (seqSessionLogic.h).
#include "seqSessionLogic.h"
#include <stdio.h>

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

    int clips = 0, scenes = 0, stopT = 0, stopA = 0, qu = 0, none = 0;
    for (uint8_t r = 0; r < 4; r++)
        for (uint8_t c = 0; c < 12; c++)
        {
            switch (mapPad(c, r).kind)
            {
                case PK_CLIP: clips++; break;
                case PK_SCENE: scenes++; break;
                case PK_STOP_T: stopT++; break;
                case PK_STOP_ALL: stopA++; break;
                case PK_QUANT: qu++; break;
                default: none++; break;
            }
        }
    SCHECK(clips == 32 && scenes == 4 && stopT == 1 && stopA == 1 && qu == 1 && none == 9);
    SCHECK(mapPad(9, 0).kind == PK_STOP_T && mapPad(9, 3).kind == PK_STOP_ALL && mapPad(11, 0).kind == PK_QUANT);

    SCHECK(groupFromButton(0, 8) == 0 && groupFromButton(1, 8) == 8 && groupFromButton(2, 8) == 0xFF);
    SCHECK(groupFromButton(0, 4) == 0 && groupFromButton(1, 4) == 4 && groupFromButton(2, 4) == 8 && groupFromButton(3, 4) == 12);
    SCHECK(groupFromButton(4, 4) == 0xFF && groupFromButton(6, 4) == 0xFF && groupFromButton(0, 0) == 0xFF);
    SCHECK(alignGroup(12, 8) == 8 && alignGroup(4, 8) == 0 && alignGroup(8, 4) == 8 && alignGroup(0, 4) == 0);
    SCHECK(kButtonClips == 6 && kButtonCut == 7);
}

static void test_cache()
{
    NoteCache c;
    c.invalidate(5);
    SCHECK(c.nextToScan() == 0 && !c.hasNotes(0, 3));
    c.set(0, 0x0008);
    SCHECK(c.hasNotes(0, 3) && !c.hasNotes(0, 4) && c.nextToScan() == 1);
    c.set(1, 0);
    c.set(2, 0xFFFF);
    c.set(3, 1);
    SCHECK(c.nextToScan() == -1 && c.hasNotes(2, 15) && c.hasNotes(3, 0));
    c.invalidateRow(2);
    SCHECK(c.nextToScan() == 2 && !c.hasNotes(2, 15));
    c.invalidate(6);
    SCHECK(c.nextToScan() == 0 && c.first == 6 && !c.hasNotes(0, 3));
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

int sessionTests(int& checks)
{
    test_quant();
    test_window();
    test_leds_and_pads();
    test_cache();
    test_launcher_bar();
    test_launcher_quant_modes();
    test_launcher_loading_blocks();
    test_launcher_stop();
    test_launcher_switch_and_transport();
    test_launcher_ring_overflow();
    test_save_merge_and_launched();
    test_cancel_and_counter();
    checks += s_checks;
    return s_fail;
}
