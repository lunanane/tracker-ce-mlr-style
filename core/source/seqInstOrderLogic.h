// Sequencer tracks mod - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Display / selection ORDER of the instruments (plan 019 A, owner): the PolySynth instances 1 / 2 / 3 come first wherever instruments are
// listed or stepped through, then the stock instruments as before. Only the mapping list position <-> instrument index changes; the
// instrument indices stored in patterns and projects stay as they are, so old projects play the same. No Polyend includes, host-tested.
//
//   popup list (instrument picker):   PolySynth 1, 2, 3 | samples 0 .. nSamples-1 | the other MIDI instruments, ascending
//   jog order (Instrument + jog):     PolySynth 1, 2, 3 | MIDI instruments, descending (the stock order) | samples 0 ..
//                                     (position 0 is the top, a positive jog delta moves down the list, clamped at both ends)
// `synth` = the instrument indices of the three PolySynth instances (-1 = an instance without a slot is skipped).
#pragma once
#include <stdint.h>

namespace instorder {

enum { kMaxSynth = 3 };

struct Layout
{
    int nSamples;   // sample instruments, indices 0 .. nSamples - 1
    int nMidi;      // MIDI instruments, indices nSamples .. nSamples + nMidi - 1
    int synth[kMaxSynth]; // instrument index of PolySynth 1 / 2 / 3, -1 = none
    int total() const { return nSamples + nMidi; }
    int synthCount() const
    {
        int n = 0;
        for (int i = 0; i < kMaxSynth; i++)
            n += valid(i);
        return n;
    }
    bool valid(int i) const { return synth[i] >= nSamples && synth[i] < nSamples + nMidi; }
    // position (0 .. 2) of the instrument among the PolySynths, -1 = not a PolySynth
    int synthRank(int idx) const
    {
        int r = 0;
        for (int i = 0; i < kMaxSynth; i++)
        {
            if (!valid(i))
                continue;
            if (synth[i] == idx)
                return r;
            r++;
        }
        return -1;
    }
    // the instrument index of the r-th valid PolySynth
    int synthAt(int r) const
    {
        for (int i = 0; i < kMaxSynth; i++)
            if (valid(i) && r-- == 0)
                return synth[i];
        return -1;
    }
};

// ---- popup list: instrument index <-> list position ----
inline int popupPos(const Layout& L, int idx)
{
    const int ns = L.synthCount();
    const int r = L.synthRank(idx);
    if (r >= 0)
        return r;
    if (idx < L.nSamples)
        return ns + idx;
    int pos = ns + L.nSamples; // the other MIDI instruments follow the samples
    for (int m = L.nSamples; m < idx; m++)
        if (L.synthRank(m) < 0)
            pos++;
    return pos;
}
inline int popupIdx(const Layout& L, int pos)
{
    const int ns = L.synthCount();
    if (pos < 0)
        pos = 0;
    if (pos >= L.total())
        pos = L.total() - 1;
    if (pos < ns)
        return L.synthAt(pos);
    pos -= ns;
    if (pos < L.nSamples)
        return pos;
    pos -= L.nSamples; // the pos-th MIDI instrument that is not a PolySynth
    for (int m = L.nSamples; m < L.total(); m++)
        if (L.synthRank(m) < 0 && pos-- == 0)
            return m;
    return 0;
}

// ---- jog order ----
// Fills out[0 .. total - 1] with the instrument index at each position; returns total.
inline int jogOrder(const Layout& L, uint8_t* out)
{
    int n = 0;
    for (int r = 0; r < L.synthCount(); r++)
        out[n++] = (uint8_t)L.synthAt(r);
    for (int m = L.total() - 1; m >= L.nSamples; m--)
        if (L.synthRank(m) < 0)
            out[n++] = (uint8_t)m;
    for (int s = 0; s < L.nSamples; s++)
        out[n++] = (uint8_t)s;
    return n;
}

// The instrument `delta` positions further down the jog order, clamped; an index that is not in the order stays where the stock code put it (position 0).
inline int jogStep(const Layout& L, int actual, int delta)
{
    uint8_t order[256];
    const int n = jogOrder(L, order);
    int pos = 0;
    for (int i = 0; i < n; i++)
        if (order[i] == actual)
        {
            pos = i;
            break;
        }
    pos += delta;
    pos = pos < 0 ? 0 : (pos >= n ? n - 1 : pos);
    return order[pos];
}

} // namespace instorder
