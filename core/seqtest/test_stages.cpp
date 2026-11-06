// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// v0.3.9 host tests of the device path of the two trace views (seqTraceStages.h): the PolySynth pitch view and the master amp view are
// fed exactly like the device does it (engine ring / per block peak rings read every 40 ms of a jittery main loop, uint16 heads with
// wrap-around) and drawn the way cSeqTraceControl does it on the FT81x: two display stages, each one SPI burst into the 4096 byte
// command FIFO. Checked: every burst the co-processor gets is complete (an FT81x FIFO model), the two stages together are exactly the
// single stage drawing (nothing lost, nothing twice, the data frozen between the stages), and what is drawn: the pitch lines of all
// voices, the amp lines reaching the left axis once the 2 minute history exists (and only the real history before that).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "plinky_voice.h"
#include "seqAmpViewLogic.h"
#include "seqPitchTraceLogic.h"
#include "seqTraceStages.h"

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                \
    do                                                          \
    {                                                           \
        g_checks++;                                             \
        if (!(c))                                               \
        {                                                       \
            g_fail++;                                           \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
        }                                                       \
    } while (0)

// ---- the FT81x side -------------------------------------------------------------------------------------------------------------
// The device context of traceControl.cpp, as the FIFO sees it: every call is a list of 32 bit words (FT812.cpp), recorded as text so the
// stages can be compared with the single stage drawing. Strips keep their points for the drawing checks.
struct DevCtx
{
    std::vector<std::string> cmds;
    int bytes = 0;
    uint32_t col = 0;
    struct Strip { uint32_t col; std::vector<int> x, y; };
    std::vector<Strip> strips;
    void add(const std::string& s, int b) { cmds.push_back(s); bytes += b; }
    void color(uint32_t rgb) { col = rgb; add("C" + std::to_string(rgb), 4); }
    void width(int w) { add("W" + std::to_string(w), 4); }
    void rect(int x0, int y0, int x1, int y1)
    {
        add("R" + std::to_string(x0) + "," + std::to_string(y0) + "," + std::to_string(x1) + "," + std::to_string(y1), 4 + 4 + 8 + 4);
    }
    void lines(const int16_t* s, int n)
    {
        std::string t = "L";
        for (int i = 0; i < n * 4; i++) t += std::to_string(s[i]) + ",";
        add(t, 4 + 8 * n + 4);
    }
    void strip(const int16_t* xy, int n)
    {
        std::string t = "S";
        Strip st;
        st.col = col;
        for (int i = 0; i < n; i++)
        {
            t += std::to_string(xy[i * 2]) + "," + std::to_string(xy[i * 2 + 1]) + ";";
            st.x.push_back(xy[i * 2]);
            st.y.push_back(xy[i * 2 + 1]);
        }
        strips.push_back(st);
        add(t, 4 + 4 * n + 4);
    }
    void textPhase() { add("B", 4); }
    void text(int x, int y, int font, int opt, const char* s)
    {
        // CMD_TEXT, x | y, font | options, the string with its zero padded to 4 (API_SendString)
        add("T" + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(font) + "," + std::to_string(opt) + s,
            12 + (int)((strlen(s) + 1 + 3) / 4 * 4));
    }
    void finish() {}
};

// RAM_CMD: a 4096 byte ring. display.cpp step1 writes one stage as one burst at the write offset (the address wraps inside RAM_CMD) and
// then sets REG_CMD_WRITE = (offset + bytes) mod 4096; the co-processor executes from its read pointer up to that. Returns the bytes the
// co-processor really executes of this burst (== bytes when the burst fits; a burst of 4096 or more bytes is cut to bytes mod 4096).
struct Fifo
{
    uint16_t wr = 0, rd = 0;
    int burst(int bytes)
    {
        // the step only starts with an empty FIFO (isCoProEmpty): rd == wr
        const uint16_t nw = (uint16_t)((wr + bytes) % tracestage::kFifoBytes);
        const int seen = (nw - rd + tracestage::kFifoBytes) % tracestage::kFifoBytes;
        wr = rd = nw;
        return seen;
    }
};

// One refresh of the control the way display.cpp runs it: stage 0, the main loop in between (the view's update may try to change the
// data: `between`, refused while busy), stage 1. Returns the drawing of both stages in order.
struct Frame
{
    DevCtx all;      // stage 0 commands followed by the stage 1 commands
    int bytes[2] = { 0, 0 };
    int dropped = 0;
    bool fifoOk = true;
};
template <class DrawFn, class BetweenFn> static Frame refresh(Fifo& fifo, DrawFn draw, BetweenFn between)
{
    Frame f;
    uint32_t col = 0;
    for (int st = 0; st < tracestage::kStages; st++)
    {
        DevCtx d;
        d.col = col; // the stage DLs are concatenated: the colour of the end of stage 0 is still set at the start of stage 1
        tracestage::Split<DevCtx> c(d, st);
        draw(c);
        const int burst = d.bytes + tracestage::kOwnBytes; // + CMD_DLSTART and the control's blend function call
        f.bytes[st] = burst;
        f.dropped = c.dropped;
        f.fifoOk = f.fifoOk && fifo.burst(burst) == burst && burst <= tracestage::kStageBytes;
        for (auto& s : d.cmds) f.all.cmds.push_back(s);
        for (auto& s : d.strips) f.all.strips.push_back(s);
        f.all.bytes += d.bytes;
        col = d.col;
        if (st == 0) between();
    }
    return f;
}

// ---- amp view: AmpView::update as on the device ----------------------------------------------------------------------------------
namespace ampsim
{
enum { kRings = ampview::kTracks * 2 + 2, kRingMaster = ampview::kTracks * 2, kLen = 64, kMaxCatchUp = 1024 };
struct Device
{
    // the audio side: the PeakRing of every track analyzer channel and the two limiter channels (uint16 head, 64 values)
    uint16_t ring[kRings][kLen];
    uint16_t head[kRings];
    // the view
    ampview::Follower* f;
    uint16_t last[kRings];
    double audMs = 0;
    uint32_t busyMs = 0; // TraceViewData::busyMs
    uint32_t lastMs = 0;
    int tracks;
    Device(int nTracks) : tracks(nTracks)
    {
        f = new ampview::Follower(); // 17.6 KB
        memset(ring, 0, sizeof ring);
        for (int i = 0; i < kRings; i++) head[i] = last[i] = 0;
    }
    ~Device() { delete f; }
    // block peaks of a beat: track t a hit every (t + 2) * 0.125 s decaying; tracks >= `tracks` silent; the master their sum
    void audioUntil(double ms)
    {
        while (audMs < ms)
        {
            const double s = audMs / 1000.0;
            double mix = 0;
            for (int t = 0; t < ampview::kTracks; t++)
            {
                const double per = (t + 2) * 0.125;
                const double v = t < tracks ? 14000.0 * exp(-fmod(s + t * 0.03, per) / per * 5.0) : 0.0;
                mix += v;
                for (int ch = 0; ch < 2; ch++)
                {
                    const int r = t * 2 + ch;
                    ring[r][head[r] & (kLen - 1)] = (uint16_t)v;
                    head[r]++;
                }
            }
            for (int ch = 0; ch < 2; ch++)
            {
                const int r = kRingMaster + ch;
                ring[r][head[r] & (kLen - 1)] = (uint16_t)(mix * 0.5 > 32767 ? 32767 : mix * 0.5);
                head[r]++;
            }
            audMs += ampview::kBlockMs;
        }
    }
    // AmpView::update (ampView.cpp), the 40 ms cadence and the stage guard included
    bool update(uint32_t now)
    {
        if ((uint32_t)(now - lastMs) < 40u) return false;
        if (busyMs != 0 && (uint32_t)(now - busyMs) < 200u) return false;
        lastMs = now;
        const uint16_t headM = head[kRingMaster];
        uint32_t n = (uint16_t)(headM - last[kRingMaster]);
        if (n > (uint32_t)kMaxCatchUp) n = kMaxCatchUp;
        if (n == 0) return false;
        uint16_t held[kRings];
        for (int i = 0; i < kRings; i++)
        {
            uint32_t avail = (uint16_t)(head[i] - last[i]);
            if (avail > kLen - 4)
            {
                last[i] = (uint16_t)(head[i] - (kLen - 4));
                avail = kLen - 4;
            }
            held[i] = avail ? ring[i][(uint16_t)(last[i] - 1) & (kLen - 1)] : 0;
        }
        uint32_t avail[kRings];
        for (int i = 0; i < kRings; i++) avail[i] = (uint16_t)(head[i] - last[i]);
        for (uint32_t k = 0; k < n; k++)
        {
            uint16_t lv[ampview::kLanes];
            for (int t = 0; t < ampview::kTracks; t++)
            {
                uint16_t m = 0;
                for (int ch = 0; ch < 2; ch++)
                {
                    const int r = t * 2 + ch;
                    if (k < avail[r]) held[r] = ring[r][(uint16_t)(last[r] + k) & (kLen - 1)];
                    if (held[r] > m) m = held[r];
                }
                lv[t] = m;
            }
            uint16_t mm = 0;
            for (int ch = 0; ch < 2; ch++)
            {
                const int r = kRingMaster + ch;
                if (k < avail[r]) held[r] = ring[r][(uint16_t)(last[r] + k) & (kLen - 1)];
                if (held[r] > mm) mm = held[r];
            }
            lv[ampview::kTracks] = mm;
            f->push(lv);
        }
        for (int i = 0; i < kRings; i++) last[i] = (uint16_t)(last[i] + (avail[i] < n ? avail[i] : n));
        last[kRingMaster] = headM;
        return true;
    }
};
} // namespace ampsim

static int minStripX(const DevCtx& d)
{
    int m = 99999;
    for (auto& s : d.strips)
        for (int x : s.x) m = x < m ? x : m;
    return m;
}

static void test_amp_device_path()
{
    // the Global Mixer 3/3 page under the view uses little of the frame: the budget clamps at its maximum (the worst case for the FIFO)
    const int scenarios[3][2] = { { 3, 5800 }, { 8, 5800 }, { 2, 4400 } };
    for (auto& sc : scenarios)
    {
        ampsim::Device dev(sc[0]);
        dev.f->budget = (uint16_t)sc[1];
        Fifo fifo;
        unsigned rnd = 12345;
        double now = 1000.0; // ms of the main loop
        dev.audMs = now;
        bool fifoOk = true, same = true, inHistory = true, frozen = true;
        int maxOld = 0, maxStage = 0, dropped = 0, x5 = 99999, x10 = 99999, x30 = 99999, xEnd = 99999;
        const double t0 = now;
        while (now - t0 < 130000.0)
        {
            rnd = rnd * 1664525u + 1013904223u;
            now += 5 + (rnd >> 24) % 12; // a main loop pass every 5 .. 16 ms
            dev.audioUntil(now);
            if (!dev.update((uint32_t)now)) continue;
            // the refresh the update asked for: two stages, the main loop runs in between (another update attempt, refused)
            const uint32_t blocksBefore = dev.f->blocks();
            Frame fr = refresh(fifo, [&](tracestage::Split<DevCtx>& c) {
                ampview::draw(c, *dev.f, "Track levels, dB", "Amp: Close");
                if (c.stage == 0) dev.busyMs = (uint32_t)now | 1u;
                else dev.busyMs = 0;
            }, [&]() {
                dev.audioUntil(now + 45);
                dev.update((uint32_t)(now + 45)); // 45 ms later: due, but the control is between its stages
                frozen = frozen && dev.f->blocks() == blocksBefore;
            });
            fifoOk = fifoOk && fr.fifoOk;
            dropped += fr.dropped;
            maxStage = fr.bytes[0] > maxStage ? fr.bytes[0] : maxStage;
            maxStage = fr.bytes[1] > maxStage ? fr.bytes[1] : maxStage;
            // the single stage drawing of v0.3.7 / v0.3.8 (what the FIFO got before), the same frozen data
            DevCtx one;
            ampview::draw(one, *dev.f, "Track levels, dB", "Amp: Close");
            const int oldBurst = one.bytes + 8;
            maxOld = oldBurst > maxOld ? oldBurst : maxOld;
            same = same && one.cmds == fr.all.cmds;
            // only the real history: no point older than what the cascade holds (+ one column of slack)
            const float histMs = (float)dev.f->blocks() * ampview::kBlockMs;
            const int mx = minStripX(fr.all);
            int colW = 0; // the column width at the left end of the lines (one column of slack: a column is drawn when it starts inside the history)
            for (auto& s : fr.all.strips)
                if (s.x.size() >= 2 && s.x[0] == mx) colW = s.x[1] - s.x[0];
            if (mx < 99999) inHistory = inHistory && mx >= (int)ampview::xOfAge(histMs) - colW - 2;
            const double el = now - t0;
            if (el < 5200 && el > 4800) x5 = mx < x5 ? mx : x5;
            if (el < 10200 && el > 9800) x10 = mx < x10 ? mx : x10;
            if (el < 30200 && el > 29800) x30 = mx < x30 ? mx : x30;
            if (el > 125000) xEnd = mx < xEnd ? mx : xEnd;
        }
        printf("  amp device path, %d tracks, budget %d: single stage up to %d FIFO bytes (FIFO %d), now stages <= %d; left end of the "
               "lines after 5 s x=%d, 10 s x=%d, 30 s x=%d, 125 s x=%d (axis %d)\n",
               sc[0], sc[1], maxOld, (int)tracestage::kFifoBytes, maxStage, x5, x10, x30, xEnd, (int)ampview::kX0);
        if (sc[1] == 5800) CHECK(maxOld > tracestage::kFifoBytes - 4); // what broke on the device: the single stage overran the FIFO
        CHECK(fifoOk);           // every burst is executed completely
        CHECK(dropped == 0);     // nothing had to be dropped
        CHECK(same);             // stage 0 + stage 1 == the single stage drawing
        CHECK(frozen);           // no history pushed while a frame was half built
        CHECK(inHistory);        // no fake fill: the lines span the real history only
        CHECK(x5 < (ampview::kX0 + ampview::kX1) / 2 + 20 && x5 > ampview::kX0 + 200); // ~4.4 s of ring: about the centre
        CHECK(x10 < x5 && x30 < x10 && x30 < ampview::kX0 + 200);                         // the line moves on to the left
        CHECK(xEnd == ampview::kX0);                                                      // 2 minutes: the left axis
    }
}

// ---- pitch view: the engine ring through PitchTraceView::update ------------------------------------------------------------------
using namespace plinkyvoice;

static void test_pitch_device_path()
{
    // PitchTraceView::show(true): follower reset, range reset; then update() every 40 ms of the main loop: sync with the engine ring
    // (head arithmetic in uint16), range.follow; the refresh is drawn in two stages, the view's update is refused in between
    Engine* e = new Engine();
    e->setParam(P_VOICES, 4);
    pitchtrace::Follower* f = new pitchtrace::Follower();
    pitchtrace::Range rg;
    f->reset();
    double ms = 5000.0;
    uint32_t lastMs = 0, busyMs = 0;
    Fifo fifo;
    int16_t l[kBlock], r[kBlock];
    auto view = [&](uint32_t now) -> bool { // PitchTraceView::update
        if (lastMs != 0 && (uint32_t)(now - lastMs) < 40u) return false;
        if (busyMs != 0 && (uint32_t)(now - busyMs) < 200u) return false;
        lastMs = now ? now : 1;
        const pitchtrace::Sample* rings[pitchtrace::kVoices];
        for (int v = 0; v < pitchtrace::kVoices; v++) rings[v] = (const pitchtrace::Sample*)e->traceRing(v);
        f->sync(rings, kTraceLen, e->traceHeadIndex(), now);
        rg.follow(*f);
        return true;
    };
    bool fifoOk = true, same = true, frozen = true;
    int maxOld = 0, maxStage = 0, dropped = 0, frames = 0;
    int pts[pitchtrace::kVoices] = { 0, 0, 0, 0 };
    std::vector<DevCtx::Strip> lastFrame;
    // four bars: a chord, two bars of an arpeggio over the voices (voice rotation), a held chord
    const uint8_t chord[4] = { 60, 64, 67, 71 };
    int block = 0;
    for (int bar = 0; bar < 4; bar++)
    {
        for (int step = 0; step < 16; step++)
        {
            if (bar == 0 && step == 0)
                for (int i = 0; i < 4; i++) e->noteOn(chord[i], 100);
            if (bar == 0 && step == 12)
                for (int i = 0; i < 4; i++) e->noteOff(chord[i]);
            if (bar == 1 || bar == 2) e->noteOn((uint8_t)(chord[step % 4] + 12 * (bar & 1)), 100);
            if (bar == 3 && step == 0) // the last bar: a held four note chord, all four lines across the window
                for (int i = 0; i < 4; i++) e->noteOn((uint8_t)(chord[i] - 5), 100);
            for (int b = 0; b < 43; b++) // 16th notes at 120 BPM: 125 ms = 43 blocks
            {
                e->render(l, r);
                ms += kBlockSec * 1000.0;
                if (++block % 3 != 0) continue; // a main loop pass every ~9 ms
                if (!view((uint32_t)ms)) continue;
                Frame fr = refresh(fifo, [&](tracestage::Split<DevCtx>& c) {
                    pitchtrace::draw(c, *f, rg, "PolySynth pitch  (dim line: envelope)", "FX2: close");
                    busyMs = c.stage == 0 ? ((uint32_t)ms | 1u) : 0;
                }, [&]() {
                    e->render(l, r); // audio goes on
                    e->render(l, r);
                    pitchtrace::Range before = rg;
                    const bool did = view((uint32_t)ms + 50); // due by time, refused while busy
                    frozen = frozen && !did && before.lo == rg.lo && before.hi == rg.hi;
                });
                frames++;
                fifoOk = fifoOk && fr.fifoOk;
                dropped += fr.dropped;
                maxStage = fr.bytes[0] > maxStage ? fr.bytes[0] : maxStage;
                maxStage = fr.bytes[1] > maxStage ? fr.bytes[1] : maxStage;
                DevCtx one;
                pitchtrace::draw(one, *f, rg, "PolySynth pitch  (dim line: envelope)", "FX2: close");
                maxOld = one.bytes + 8 > maxOld ? one.bytes + 8 : maxOld;
                same = same && one.cmds == fr.all.cmds;
                lastFrame = fr.all.strips;
            }
            if (bar == 1 || bar == 2) e->noteOff((uint8_t)(chord[step % 4] + 12 * (bar & 1)));
        }
    }
    // the points of the last frame (both stages) per voice colour
    for (auto& s : lastFrame)
        for (int v = 0; v < pitchtrace::kVoices; v++)
            if (s.col == pitchtrace::voiceColor(v)) pts[v] += (int)s.x.size();
    printf("  pitch device path: %d frames, single stage up to %d FIFO bytes (FIFO %d), now stages <= %d; points per voice %d %d %d %d\n",
           frames, maxOld, (int)tracestage::kFifoBytes, maxStage, pts[0], pts[1], pts[2], pts[3]);
    CHECK(frames > 100);
    CHECK(maxOld > tracestage::kFifoBytes - 4); // the case that broke on the device: the single stage did not fit the FIFO
    CHECK(fifoOk);
    CHECK(dropped == 0);
    CHECK(same);
    CHECK(frozen);
    for (int v = 0; v < pitchtrace::kVoices; v++) CHECK(pts[v] >= 60); // every voice a pitch line with many points
    delete f;
    delete e;
}

// The worst cases of both drawings fit the two stages without dropping anything (budget headroom of the split)
static void test_stage_capacity()
{
    CHECK(tracestage::kStageBytes < tracestage::kFifoBytes - 4);
    CHECK(2 * (tracestage::kStageBytes - tracestage::kOwnBytes) >= ampview::kDefaultBudget + 400 + 808);
    CHECK(2 * (tracestage::kStageBytes - tracestage::kOwnBytes) >= 5800 + 808); // the amp budget clamp (ampView.cpp) + the largest strip
    CHECK(2 * (tracestage::kStageBytes - tracestage::kOwnBytes) >= pitchtrace::kDisplayBudget + 808);
    // a drawing that is too big for both stages is cut, never overruns the FIFO
    DevCtx d0, d1;
    tracestage::Split<DevCtx> a(d0, 0), b(d1, 1);
    int16_t pts[400];
    for (int i = 0; i < 400; i++) pts[i] = (int16_t)i;
    for (int i = 0; i < 20; i++) { a.strip(pts, 200); b.strip(pts, 200); }
    CHECK(d0.bytes + tracestage::kOwnBytes <= tracestage::kStageBytes && d1.bytes + tracestage::kOwnBytes <= tracestage::kStageBytes);
    CHECK(a.dropped > 0 && a.dropped == b.dropped);
}

int main()
{
    printf("trace view stages (v0.3.9)\n");
    test_stage_capacity();
    test_amp_device_path();
    test_pitch_device_path();
    printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
