// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// PolySynth pitch trace (plan 013 V): the main-loop follower of the engine ring of (pitch, envelope) per voice, the decimation to at most
// 64 points per voice, the auto range, the layout and the drawing over a tiny context (the same code runs on the FT81x display list in
// interface/seqPlay/pitchTrace.cpp and in the host tests: display-list size, bounds). No Polyend includes.
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
    kVoices = 4,
    kWin = 112,       // samples shown (the engine writes one per ~34.8 ms: 3.9 s)
    kSampleMs = 35,
    kMaxPts = 64,     // points per curve after decimation
    kX0 = 60,         // plot area
    kX1 = 712,
    kY0 = 60,
    kY1 = 404,
    kPanelY0 = 30,    // background panel (same band as the PolySynth parameter overlay)
    kPanelY1 = 424,
    kMaxGridSpan = 40, // above this span only the octave lines are drawn
    kMinRun = 5        // v0.3.6: a note stays on the screen for at least this many samples (~175 ms): staccato stabs are visible
};
constexpr float kEnvSemis = 3.0f; // full envelope lifts the second line by this many semitones
constexpr float kMinSpan = 14.0f;

struct Sample // identical layout to plinkyvoice::TraceSample (checked in pitchTrace.cpp)
{
    int16_t pitch; // semitones * 32
    uint8_t env;
    uint8_t on;
};

inline uint32_t voiceColor(int v)
{
    return v == 0 ? seqpal::kNote : v == 1 ? seqpal::kInstr : v == 2 ? seqpal::kFx1 : seqpal::kFx2;
}

// ---- follower: engine ring -> own window (also advances while the engine is idle) -------------------------------------
class Follower
{
  public:
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
        have_ = false;
        last_ = 0;
        lastMs_ = 0;
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
            return;
        }
        const uint32_t el = nowMs - lastMs_; // no new sample: idle engine - after 3 periods the time axis keeps running with silence
        if (el >= 3 * kSampleMs)
        {
            uint32_t steps = el / kSampleMs;
            lastMs_ += steps * kSampleMs;
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
        }
        head_ = (uint8_t)((head_ + 1) % kWin);
    }
    // age 0 = newest .. kWin - 1 = oldest
    const Sample& at(int v, int age) const { return s_[v][(head_ + kWin - 1 - age) % kWin]; }

  private:
    Sample s_[kVoices][kWin];
    uint8_t run_[kVoices];
    uint8_t hold_[kVoices];
    Sample lastOn_[kVoices];
    uint8_t head_;
    bool have_;
    uint16_t last_;
    uint32_t lastMs_;
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
    // grows at once, shrinks slowly (10 % per call); holds while nothing sounds
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
        float tlo = mn - 2.0f, thi = mx + 2.0f;
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
        lo = tlo < lo ? tlo : lo + (tlo - lo) * 0.1f;
        hi = thi > hi ? thi : hi + (thi - hi) * 0.1f;
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
    int nowY[kVoices];
    int nowNote[kVoices];
    for (int v = 0; v < kVoices; v++)
    {
        nowY[v] = -1;
        nowNote[v] = 0;
    }
    for (int v = 0; v < kVoices; v++)
    {
        const int n = decimate(f, v, pts, kMaxPts);
        for (int curve = 0; curve < 2; curve++) // 0 = envelope (behind), 1 = pitch
        {
            c.color(curve == 0 ? seqpal::dim(voiceColor(v)) : voiceColor(v));
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
    for (int k = 0; k < cnt; k++)
    {
        c.color(voiceColor(order[k]));
        c.text(kX1 + 12, ly[k], 3, 2, noteName(nowNote[order[k]], nb));
    }

    c.color(seqpal::kTitle);
    c.text(12, 42, 3, 2, title);
    c.color(seqpal::kText);
    c.text(788, 42, 1, 2 | 4, hint);
    c.finish();
}

} // namespace pitchtrace

#endif
