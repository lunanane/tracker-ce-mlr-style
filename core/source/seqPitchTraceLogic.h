// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// PolySynth pitch trace (plan 013 V): the main-loop follower of the engine ring of (pitch, envelope) per voice, the decimation to at most
// 64 points per voice, the auto range, the layout and the drawing over a tiny context (the same code runs on the FT81x display list in
// interface/seqPlay/pitchTrace.cpp and in the host tests: display-list size, bounds). No Polyend includes.
//
// v0.3.7 (plan 015 V): the engine writes a sample every 2nd audio block (5.8 ms), the follower keeps 672 of them (3.9 s) and the drawing
// shows 168 points per pitch line and 84 per envelope line (display list budget below); the plot uses the full height between the stock
// title bar and the strip, the auto range is tight around the sounding notes (at least one octave).
//
// Look: plain dark ground, no fills. x = time, ~3.9 s scrolling right to left; y = pitch on a light semitone grid, octave lines
// stronger, note names at the left. Per voice one thin line in a tracker key colour (Note teal, Instrument yellow, FX1 pink, FX2 blue)
// and a dimmer second line y = pitch + kEnvSemis * envelope hugging it (attack lifts it away, release lets it fall back). The current
// note name of every sounding voice sits at the right edge.
#ifndef SEQ_PITCH_TRACE_LOGIC_H_
#define SEQ_PITCH_TRACE_LOGIC_H_

#include <stdint.h>
#include <stdio.h>

#include "seqPalette.h"

namespace pitchtrace
{
enum
{
    kVoices = 8,      // plan 019: the pool voices of the three PolySynth instances (was 4)
    kWin = 672,       // samples kept (the engine writes one per 2 blocks = 5.805 ms: 3.9 s)
    kSampleUs = 5805,
    kIdleMs = 18,     // no new sample for this long: the engine is idle, the follower keeps the time axis running with silence
    kMaxPts = 168,    // points of a pitch curve after decimation (kWin / 4)
    kEnvPts = 84,     // points of an envelope curve (kWin / 8: every second pitch point)
    kX0 = 60,         // plot area: the full height between the stock title bar (28) and the strip (425)
    kX1 = 712,
    kY0 = 36,
    kY1 = 420,
    kPanelY0 = 30,    // background panel
    kPanelY1 = 424,
    kMaxGridSpan = 40, // above this span only the octave lines are drawn
    kMinRun = 30,      // v0.3.6: a note stays on the screen for at least this many samples (~175 ms): staccato stabs are visible
    kDisplayBudget = 5400 // bytes of display list (8172 - 400 margin - title bar 600 - strip / labels 1720); the pattern control is hidden
};
constexpr float kEnvSemis = 3.0f; // full envelope lifts the second line by this many semitones
constexpr float kMinSpan = 12.0f; // one octave: a single note or a chord fills most of the height

struct Sample // identical layout to plinkyvoice::TraceSample (checked in pitchTrace.cpp)
{
    int16_t pitch; // semitones * 32
    uint8_t env;
    uint8_t on;
};

// plan 019: one colour per PolySynth instance (also the colour of its tab): 1 Note teal, 2 Instrument yellow, 3 FX1 pink / violet.
// Sample.on bit 0 = sounds, bits 1 and 2 = the instance.
inline uint32_t instColor(int inst)
{
    return inst == 0 ? seqpal::kNote : inst == 1 ? seqpal::kInstr : seqpal::kFx1;
}
inline int instOfSample(const Sample& s) { return (s.on >> 1) & 3; }
// the line colour of the rank-th (0 .. 3) voice of an instance: the instance colour, each further simultaneous voice a little lighter
inline uint32_t lineColor(int inst, int rank) { return seqpal::mix(instColor(inst), 0xFFFFFF, (uint8_t)(rank < 0 ? 0 : (rank > 3 ? 3 : rank) * 36)); }

// ---- follower: engine ring -> own window (also advances while the engine is idle) -------------------------------------
class Follower
{
  public:
    uint16_t budget = kDisplayBudget; // v0.4.7: display list bytes the drawing may use (the view sets it from what the other controls leave)
    Follower() { reset(); }
    void reset()
    {
        for (int v = 0; v < kVoices; v++)
            for (int i = 0; i < kWin; i++)
            {
                s_[v][i].pitch = 0;
                s_[v][i].env = 0;
                s_[v][i].on = 0;
            }
        for (int v = 0; v < kVoices; v++)
        {
            run_[v] = 0;
            hold_[v] = 0;
            lastOn_[v] = Sample();
        }
        head_ = 0;
        fed_ = 0;
        sounding_ = 0;
        have_ = false;
        last_ = 0;
        lastMs_ = 0;
        remUs_ = 0;
    }
    // ring[v] points to the engine ringLen (power of two) samples of voice v, head = index of the next sample the engine writes
    void sync(const Sample* const* ring, int ringLen, uint16_t head, uint32_t nowMs)
    {
        if (!have_)
        {
            have_ = true;
            last_ = head;
            lastMs_ = nowMs;
            return;
        }
        uint16_t n = (uint16_t)(head - last_);
        if (n > ringLen - 4) // overrun (the view was not refreshed for seconds): keep the newest half
        {
            last_ = (uint16_t)(head - ringLen / 2);
            n = (uint16_t)(ringLen / 2);
        }
        if (n > 0)
        {
            const int mask = ringLen - 1;
            for (uint16_t k = 0; k < n; k++)
            {
                Sample col[kVoices];
                for (int v = 0; v < kVoices; v++)
                    col[v] = ring[v][(last_ + k) & mask];
                push(col);
            }
            last_ = head;
            lastMs_ = nowMs;
            remUs_ = 0;
            return;
        }
        const uint32_t el = nowMs - lastMs_; // no new sample: idle engine - after 3 periods the time axis keeps running with silence
        if (el >= (uint32_t)kIdleMs)
        {
            const uint32_t us = el * 1000u + remUs_;
            uint32_t steps = us / kSampleUs;
            remUs_ = us % kSampleUs;
            lastMs_ = nowMs;
            if (steps > kWin) steps = kWin;
            Sample none[kVoices];
            for (int v = 0; v < kVoices; v++)
            {
                none[v].pitch = 0;
                none[v].env = 0;
                none[v].on = 0;
            }
            while (steps--) push(none);
        }
    }
    // Minimum visible length (v0.3.6): a sounding run shorter than kMinRun samples is continued with copies of its last point (the
    // envelope falling off) until it is kMinRun long. Longer runs end exactly where the engine says.
    void push(const Sample* col)
    {
        int sounding = 0;
        for (int v = 0; v < kVoices; v++)
        {
            Sample s = col[v];
            if (s.on)
            {
                if (run_[v] < 255) run_[v]++;
                lastOn_[v] = s;
                hold_[v] = run_[v] < kMinRun ? (uint8_t)(kMinRun - run_[v]) : 0;
            }
            else if (hold_[v] > 0)
            {
                hold_[v]--;
                run_[v]++;
                lastOn_[v].env = (uint8_t)(lastOn_[v].env * 3 / 5);
                s = lastOn_[v];
            }
            else
                run_[v] = 0;
            s_[v][head_] = s;
            sounding += (s.on & 1);
        }
        head_ = (uint16_t)((head_ + 1) % kWin);
        fed_++;
        sounding_ = (uint8_t)sounding;
    }
    uint32_t fed() const { return fed_; }               // DEBUG readout: columns pushed since the reset
    int soundingNow() const { return sounding_; }       // ... voices "on" in the newest column
    // age 0 = newest .. kWin - 1 = oldest
    const Sample& at(int v, int age) const { return s_[v][(head_ + kWin - 1 - age) % kWin]; }

  private:
    Sample s_[kVoices][kWin];
    uint8_t run_[kVoices];
    uint8_t hold_[kVoices];
    Sample lastOn_[kVoices];
    uint16_t head_;
    uint32_t fed_;
    uint8_t sounding_;
    bool have_;
    uint16_t last_;
    uint32_t lastMs_;
    uint32_t remUs_;
};

// ---- v0.4.1: the live read of the engine's pool voices -------------------------------------------------------------------
// The device view (interface/seqPlay/pitchTrace.cpp) no longer depends on the trace ring the audio interrupt writes: every main loop
// pass it reads the voices themselves (Engine::liveVoice: sounding, instance, pitch incl. glide + LFO / bend, envelope, note serial) and
// pushes one column per kSampleUs of elapsed time into the follower. Whatever played the note (pattern, Play mode sequencer, pads,
// launcher, external MIDI) it is a pool voice, so it is shown. A note that began and ended between two reads (a stab shorter than a
// main loop pass) is caught by its serial and gets one sounding column; the follower's minimum run keeps it visible.
struct LiveVoice
{
    bool on;
    uint8_t inst;
    float pitch; // semitones
    float env;   // 0 .. 1
    uint32_t serial;
};
class LiveSampler
{
  public:
    LiveSampler() { reset(); }
    void reset()
    {
        have_ = false;
        lastMs_ = 0;
        remUs_ = 0;
        for (int v = 0; v < kVoices; v++) serial_[v] = 0;
    }
    // frozen: the follower must not change now (the control is between its two display stages); the time is caught up afterwards.
    // Returns the columns pushed.
    int sample(const LiveVoice* lv, uint32_t nowMs, Follower& f, bool frozen)
    {
        if (!have_)
        {
            have_ = true;
            lastMs_ = nowMs;
            remUs_ = 0;
            for (int v = 0; v < kVoices; v++) serial_[v] = lv[v].serial;
            return 0;
        }
        if (frozen) return 0;
        const uint32_t us = (nowMs - lastMs_) * 1000u + remUs_;
        uint32_t steps = us / kSampleUs;
        if (steps == 0) return 0;
        remUs_ = us % kSampleUs;
        lastMs_ = nowMs;
        if (steps > (uint32_t)kWin) steps = kWin;
        Sample col[kVoices];
        for (int v = 0; v < kVoices; v++)
        {
            const bool fresh = lv[v].serial != serial_[v];
            serial_[v] = lv[v].serial;
            if (!lv[v].on && !fresh)
            {
                col[v].pitch = 0;
                col[v].env = 0;
                col[v].on = 0;
                continue;
            }
            float p = lv[v].pitch * 32.0f;
            p = p > 32000.0f ? 32000.0f : (p < -32000.0f ? -32000.0f : p);
            float e = lv[v].env * 255.0f;
            e = e > 255.0f ? 255.0f : (e < 0.0f ? 0.0f : e);
            col[v].pitch = (int16_t)(p < 0 ? p - 0.5f : p + 0.5f);
            col[v].env = (uint8_t)e;
            col[v].on = (uint8_t)(1 | ((lv[v].inst & 3) << 1));
        }
        for (uint32_t k = 0; k < steps; k++) f.push(col);
        return (int)steps;
    }

  private:
    bool have_;
    uint32_t lastMs_, remUs_;
    uint32_t serial_[kVoices];
};

// The window of one voice, oldest first, reduced to at most maxPts points (the last sample of each bucket). Returns the count.
inline int decimate(const Follower& f, int v, Sample* out, int maxPts)
{
    const int n = maxPts < kWin ? maxPts : kWin;
    for (int i = 0; i < n; i++)
    {
        const int src = (i + 1) * kWin / n - 1; // oldest-first index
        const int first = i * kWin / n;
        out[i] = f.at(v, kWin - 1 - src);
        // v0.3.6: a bucket that contains a sounding sample is "on" (the last sounding one), so a short note is never decimated away
        if (!out[i].on)
            for (int j = src - 1; j >= first; j--)
                if (f.at(v, kWin - 1 - j).on)
                {
                    out[i] = f.at(v, kWin - 1 - j);
                    break;
                }
    }
    return n;
}

// ---- auto range around the sounding notes -------------------------------------------------------------------------------
struct Range
{
    float lo = 48.0f, hi = 72.0f;
    bool init = false;
    // grows at once, shrinks slowly (6 % per call = a time constant of about 0.6 s at 25 calls per second); holds while nothing sounds
    void follow(const Follower& f)
    {
        float mn = 1e9f, mx = -1e9f;
        for (int v = 0; v < kVoices; v++)
            for (int a = 0; a < kWin; a++)
            {
                const Sample& s = f.at(v, a);
                if (!s.on) continue;
                const float p = s.pitch * (1.0f / 32.0f);
                const float top = p + kEnvSemis * (s.env * (1.0f / 255.0f));
                if (p < mn) mn = p;
                if (top > mx) mx = top;
            }
        if (mx < mn) return;
        float tlo = mn - 1.0f, thi = mx + 1.0f;
        if (thi - tlo < kMinSpan)
        {
            const float c = (thi + tlo) * 0.5f;
            tlo = c - kMinSpan * 0.5f;
            thi = c + kMinSpan * 0.5f;
        }
        if (!init)
        {
            lo = tlo;
            hi = thi;
            init = true;
            return;
        }
        lo = tlo < lo ? tlo : lo + (tlo - lo) * 0.06f;
        hi = thi > hi ? thi : hi + (thi - hi) * 0.06f;
    }
    int ilo() const { return (int)(lo < 0 ? lo - 0.999f : lo); } // floor
    int ihi() const { return (int)(hi < 0 ? hi : hi + 0.999f); } // ceil
};

// "C#4" (MIDI 60 = C4); buf >= 8 bytes
inline const char* noteName(int midi, char* buf)
{
    static const char* const kN[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int n = ((midi % 12) + 12) % 12;
    const int o = (midi - n) / 12 - 1;
    snprintf(buf, 8, "%s%d", kN[n], o);
    return buf;
}
inline bool isNatural(int midi)
{
    const int n = ((midi % 12) + 12) % 12;
    return n == 0 || n == 2 || n == 4 || n == 5 || n == 7 || n == 9 || n == 11;
}

inline int yOf(float semis, float lo, float hi)
{
    float t = (semis - lo) / (hi - lo);
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    return (int)(kY1 - t * (kY1 - kY0) + 0.5f);
}

// The drawing. C: color(rgb) width(w16) rect(x0,y0,x1,y1) lines(xyxy, nseg) strip(xy, npts) textPhase() text(x,y,font,opt,s) finish();
// text opt: 1 centre x, 2 centre y, 4 right aligned. rect fills (opaque panel); everything after textPhase() is blended normally.
template <class C> void draw(C& c, const Follower& f, const Range& r, const char* title, const char* hint)
{
    char nb[8];
    c.color(seqpal::kBg);
    c.rect(0, kPanelY0, 800, kPanelY1);
    c.textPhase();

    const int ilo = r.ilo(), ihi = r.ihi();
    const float lo = (float)ilo, hi = (float)(ihi > ilo ? ihi : ilo + 1);
    const int span = ihi - ilo;

    // grid: semitone lines light, octave (C) lines stronger
    c.width(16);
    int16_t seg[(kMaxGridSpan + 1) * 4];
    for (int pass = 0; pass < 2; pass++)
    {
        int n = 0;
        for (int m = ilo; m <= ihi; m++)
        {
            const bool oct = ((m % 12) + 12) % 12 == 0;
            if (oct != (pass == 1)) continue;
            if (pass == 0 && span > kMaxGridSpan) continue;
            if (n >= kMaxGridSpan + 1) break;
            const int y = yOf((float)m, lo, hi);
            seg[n * 4 + 0] = kX0;
            seg[n * 4 + 1] = (int16_t)y;
            seg[n * 4 + 2] = kX1;
            seg[n * 4 + 3] = (int16_t)y;
            n++;
        }
        if (n > 0)
        {
            c.color(pass == 0 ? seqpal::kGrid : seqpal::kGridHi);
            c.lines(seg, n);
        }
    }
    // note names at the left: every C, and the natural notes while the range is small
    for (int m = ilo; m <= ihi; m++)
    {
        const bool oct = ((m % 12) + 12) % 12 == 0;
        if (!oct && !(span <= 24 && isNatural(m))) continue;
        c.color(oct ? seqpal::kText : seqpal::dim(seqpal::kText));
        c.text(kX0 - 6, yOf((float)m, lo, hi), 1, 2 | 4, noteName(m, nb));
    }

    // curves: the envelope line first (dim), then the pitch line
    Sample pts[kMaxPts];
    int16_t xy[kMaxPts * 2];
    const int npe = kEnvPts < kMaxPts ? kEnvPts : kMaxPts;
    int nowY[kVoices];
    int nowNote[kVoices];
    int vInst[kVoices], vRank[kVoices];
    bool vData[kVoices];
    // plan 019: up to 8 voices instead of 4 share the display list: the points per line shrink with the number of voices that drew
    // anything in the window (4 or fewer: as before)
    int act = 0;
    for (int v = 0; v < kVoices; v++)
    {
        nowY[v] = -1;
        nowNote[v] = 0;
        vInst[v] = vRank[v] = 0;
        vData[v] = false;
        for (int a = 0; a < kWin; a++)
            if (f.at(v, a).on)
            {
                vInst[v] = instOfSample(f.at(v, a)); // the instance of the voice's newest sounding sample
                vData[v] = true;
                act++;
                break;
            }
        if (vData[v])
            for (int u = 0; u < v; u++)
                vRank[v] += vData[u] && vInst[u] == vInst[v];
    }
    const int scale = act > 4 ? act : 4;
    // v0.4.7: a smaller budget (the other controls leave less of the frame) thins the curves in proportion; the full budget draws as before
    const int pct = f.budget >= kDisplayBudget ? 100 : f.budget * 100 / kDisplayBudget;
    const int ptsMaxFull = kMaxPts * 4 / scale * pct / 100;
    const int ptsEnvFull = npe * 4 / scale * pct / 100;
    const int ptsMax = ptsMaxFull < (pct < 100 ? 4 : 16) ? (pct < 100 ? 4 : 16) : ptsMaxFull;
    const int ptsEnv = ptsEnvFull < (pct < 100 ? 3 : 8) ? (pct < 100 ? 3 : 8) : ptsEnvFull;
    for (int v = 0; v < kVoices; v++)
    {
        for (int curve = 0; curve < 2; curve++) // 0 = envelope (behind), 1 = pitch
        {
            const int n = decimate(f, v, pts, curve == 0 ? ptsEnv : ptsMax);
            c.color(curve == 0 ? seqpal::dim(lineColor(vInst[v], vRank[v])) : lineColor(vInst[v], vRank[v]));
            c.width(curve == 0 ? 12 : 20);
            int cnt = 0;
            for (int i = 0; i <= n; i++)
            {
                if (i < n && pts[i].on)
                {
                    float p = pts[i].pitch * (1.0f / 32.0f);
                    if (curve == 0) p += kEnvSemis * (pts[i].env * (1.0f / 255.0f));
                    xy[cnt * 2] = (int16_t)(kX0 + i * (kX1 - kX0) / (n - 1));
                    xy[cnt * 2 + 1] = (int16_t)yOf(p, lo, hi);
                    cnt++;
                    continue;
                }
                if (cnt >= 2) c.strip(xy, cnt);
                cnt = 0;
            }
        }
        const Sample& now = f.at(v, 0);
        if (now.on)
        {
            const float p = now.pitch * (1.0f / 32.0f);
            nowY[v] = yOf(p, lo, hi);
            nowNote[v] = (int)(p < 0 ? p - 0.5f : p + 0.5f);
        }
    }

    // current note per voice at the right edge, kept 14 px apart (insertion sort of the 4 labels by y)
    int order[kVoices], cnt = 0;
    for (int v = 0; v < kVoices; v++)
        if (nowY[v] >= 0)
        {
            int j = cnt++;
            while (j > 0 && nowY[order[j - 1]] > nowY[v])
            {
                order[j] = order[j - 1];
                j--;
            }
            order[j] = v;
        }
    int ly[kVoices];
    for (int k = 0; k < cnt; k++)
    {
        ly[k] = nowY[order[k]];
        if (k > 0 && ly[k] < ly[k - 1] + 14) ly[k] = ly[k - 1] + 14;
    }
    for (int k = cnt - 1; k >= 0; k--) // stacked labels must not leave the plot at the bottom
    {
        const int lim = k == cnt - 1 ? kY1 : ly[k + 1] - 14;
        if (ly[k] > lim) ly[k] = lim;
    }
    for (int k = 0; k < cnt; k++)
    {
        c.color(lineColor(vInst[order[k]], vRank[order[k]]));
        c.text(kX1 + 12, ly[k], 3, 2, noteName(nowNote[order[k]], nb));
    }

    c.color(seqpal::kTitle);
    c.text(kX0 + 6, kY0 + 10, 1, 2, title);
    c.color(seqpal::kText);
    c.text(kX1 - 6, kY0 + 10, 1, 2 | 4, hint);
    c.finish();
}

} // namespace pitchtrace

#endif
