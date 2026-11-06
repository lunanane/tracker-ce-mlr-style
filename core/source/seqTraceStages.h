// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// v0.3.9: the trace views (pitch, amp) are drawn in TWO display stages, each at most kStageBytes of co-processor commands.
//
// Why: cDisplay::step1 (libs/NewDisplay/display.cpp) writes one stage of a control as ONE SPI burst into the FT81x command FIFO
// (RAM_CMD, 4096 bytes ring) with beginCoProListNoCheck (no free space check) and moves REG_CMD_WRITE once at the end, modulo 4096.
// A stage of 4092 bytes or more therefore overruns the ring: the write pointer passes the read pointer, the co-processor sees only
// (bytes mod 4096) and executes garbage. Since v0.3.7 (plan 015 V) the views use up to 5400 / 5800 bytes in their single stage: the
// pitch view broke as soon as the four voices had lines in the window (4.6 KB), the amp view as soon as its lines covered more than
// about two thirds of the width (the list grows with the history). The host tests only checked the display list budget, not the FIFO.
//
// Split: the template draw runs once per stage over this context. Every command is counted with its exact FIFO size (the sizes of the
// FT812 API calls of traceControl.cpp); the commands go to stage 0 until the next one would exceed kStageBytes, from there on to stage 1;
// what would not fit stage 1 either is dropped (never an overrun). Both stages must see the same data: the views do not change their
// follower while a frame is half built (TraceViewData::busyMs). The stage DLs are concatenated in the control's RAM_G slot, so the
// graphics state (colour, width, blend) carries over from stage 0 to stage 1 like in any multi stage control.
#ifndef SEQ_TRACE_STAGES_H_
#define SEQ_TRACE_STAGES_H_

#include <stdint.h>
#include <string.h>

namespace tracestage
{
enum
{
    kStages = 2,
    kFifoBytes = 4096,  // FT81x RAM_CMD
    kStageBytes = 3840, // per stage, incl. the CMD_DLSTART of display.cpp and the blend calls of the control: well under 4092
    kOwnBytes = 8       // CMD_DLSTART (display.cpp) + one blend function call of the control per stage
};

// FIFO bytes of the commands the trace context issues (FT812.cpp): one word per DL command, CMD_TEXT 12 + the string with its zero
// padded to a multiple of 4
inline int textBytes(const char* s) { return 12 + (((int)strlen(s) + 1 + 3) & ~3); }

template <class C> struct Split
{
    C& out;
    int stage;       // 0 or 1: the stage being built
    int used[2];     // bytes of each stage so far
    int dropped;     // bytes that fit neither stage
    bool cut;        // stage 0 is closed
    Split(C& o, int st) : out(o), stage(st), dropped(0), cut(false) { used[0] = used[1] = kOwnBytes; }
    bool take(int bytes)
    {
        if (!cut && used[0] + bytes <= kStageBytes)
        {
            used[0] += bytes;
            return stage == 0;
        }
        cut = true;
        if (dropped == 0 && used[1] + bytes <= kStageBytes)
        {
            used[1] += bytes;
            return stage == 1;
        }
        dropped += bytes;
        return false;
    }
    void color(uint32_t rgb) { if (take(4)) out.color(rgb); }
    void width(int w16) { if (take(4)) out.width(w16); }
    void rect(int x0, int y0, int x1, int y1) { if (take(20)) out.rect(x0, y0, x1, y1); }
    void lines(const int16_t* s, int n) { if (take(8 + 8 * n)) out.lines(s, n); }
    void strip(const int16_t* xy, int n) { if (take(8 + 4 * n)) out.strip(xy, n); }
    void textPhase() { if (take(4)) out.textPhase(); }
    void text(int x, int y, int font, int opt, const char* s) { if (take(textBytes(s))) out.text(x, y, font, opt, s); }
    void finish() {}
};

// The same split for the batching contexts of the session screens (laneView.cpp FtCtx: color, alpha, rect, disc, text, finish; rects and
// discs are batched, BEGIN / END only when the primitive changes). The cost of a command includes the batch change it causes in a single
// stage drawing (END 4, LINE_WIDTH + BEGIN(RECTS) 8, BEGIN(POINTS) 4; a disc is POINT_SIZE + VERTEX2F). The stage's own context opens its
// batches itself, so a stage may use a few bytes more than counted: kBatchOwnBytes covers CMD_DLSTART, SAVE / RESTORE_CONTEXT, the batch
// reopened at the start of stage 1 and the END of the last batch.
enum { kBatchOwnBytes = 4 + 8 + 8 + 4 };
template <class C> struct BatchSplit
{
    C& out;
    int stage;
    int used[2];
    int dropped;
    bool cut;
    uint8_t prim; // the batch of the single stage drawing: 0 none, 1 RECTS, 2 POINTS
    BatchSplit(C& o, int st) : out(o), stage(st), dropped(0), cut(false), prim(0) { used[0] = used[1] = kBatchOwnBytes; }
    int change(uint8_t want)
    {
        if (prim == want) return 0;
        const int b = (prim ? 4 : 0) + (want == 1 ? 8 : (want == 2 ? 4 : 0));
        prim = want;
        return b;
    }
    bool take(int bytes)
    {
        if (!cut && used[0] + bytes <= kStageBytes)
        {
            used[0] += bytes;
            return stage == 0;
        }
        cut = true;
        if (dropped == 0 && used[1] + bytes <= kStageBytes)
        {
            used[1] += bytes;
            return stage == 1;
        }
        dropped += bytes;
        return false;
    }
    void color(uint32_t rgb) { if (take(4)) out.color(rgb); }
    void alpha(uint8_t a) { if (take(4)) out.alpha(a); }
    void rect(int x0, int y0, int x1, int y1) { if (take(change(1) + 8)) out.rect(x0, y0, x1, y1); }
    void disc(int cx, int cy, int r) { if (take(change(2) + 8)) out.disc(cx, cy, r); }
    void text(int x, int y, int font, int opt, const char* s) { if (take(change(0) + textBytes(s))) out.text(x, y, font, opt, s); }
    void finish() { change(0); }
};

} // namespace tracestage

#endif
