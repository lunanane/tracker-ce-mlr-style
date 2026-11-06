// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Cue cache of the MLR slots (plan 016 A2): the pure bookkeeping. No Polyend includes, host-tested in core/seqtest.
//
// A pad press is a jump: a new note-on whose start lies somewhere in a long, streamed sample. The voice can only play after the
// SD card has delivered the first ring chunk. The cue cache holds kFrames of audio at the start of every step (cue) of the CURRENT
// section of both slots, plus the next section's cues once the playhead nears the end of the section. A jump that lands on a cached
// cue plays from the cache at once; the stream is told that the cached frames are covered (VoiceStreamManager::notifyNoteOn
// sliceCachedFrames), so its ring starts right behind the cache: a sample-accurate join.
//
// Memory: kEntries x kChannels x kFrames x 2 bytes in EXTERNAL_RAM (seqMlr.cpp owns the raw storage; nothing in here is constructed
// there). This class is plain bookkeeping and lives in internal RAM (~1 KB).
#pragma once
#include <stdint.h>

namespace seqcue {

enum { kSlots = 2, kBanks = 2, kCues = 16, kEntries = kSlots * kBanks * kCues, kChannels = 2, kFrames = 3072 };

// What one cue should hold. on = 0: nothing to cache (resident sample, inside the RAM pre-cache, silence, past the pattern end).
struct Want
{
    uint32_t frame;
    uint32_t hash;
    uint8_t instr, stereo, on, pad_;
};

// The step -> sample-frame mapping, supplied by the firmware glue (the same one that plays a retrigger): false = no cue
// (resident sample, silence, ...). `streamedFrom` = the first frame that is not in the RAM pre-cache (earlier frames need no cue).
typedef bool (*FrameFn)(void* user, int pos, uint8_t& instr, uint32_t& frame, uint32_t& hash, uint8_t& stereo);

// The wants of the 16 pads of section `sec` (pad k = step sec * 16 * spp + k * spp).
inline void computeWants(FrameFn fn, void* user, int sec, int spp, int len, uint32_t streamedFrom, Want out[kCues])
{
    for (int k = 0; k < kCues; k++)
    {
        Want w = { 0, 0, 0, 0, 0, 0 };
        const int pos = sec * kCues * spp + k * spp;
        uint8_t instr = 0, stereo = 0;
        uint32_t frame = 0, hash = 0;
        if (pos < len && fn(user, pos, instr, frame, hash, stereo) && frame >= streamedFrom)
        {
            w.frame = frame;
            w.hash = hash;
            w.instr = instr;
            w.stereo = stereo;
            w.on = 1;
        }
        out[k] = w;
    }
}

class Cache
{
  public:
    struct Entry
    {
        uint32_t frame, hash;
        uint8_t instr, need, have, wanted; // need / have: channel bit masks; ready = wanted && have == need
    };

    static int index(int slot, int bank, int cue) { return (slot * kBanks + bank) * kCues + cue; }

    void reset()
    {
        for (int i = 0; i < kEntries; i++) e_[i] = Entry();
    }

    // Main loop: what (slot, bank) should hold now. An entry whose key (frame, hash, instrument) changed is stale and is loaded again;
    // one that is still right keeps its data (also when the playhead moves on and the bank becomes the current one).
    void want(int slot, int bank, const Want w[kCues])
    {
        for (int k = 0; k < kCues; k++)
        {
            Entry& e = e_[index(slot, bank, k)];
            if (!w[k].on)
            {
                e.wanted = 0;
                e.have = 0;
                continue;
            }
            const uint8_t need = (uint8_t)(w[k].stereo ? 3 : 1);
            if (e.wanted && e.frame == w[k].frame && e.hash == w[k].hash && e.instr == w[k].instr && e.need == need) continue;
            e.have = 0; // stale first: the audio side stops using it before the data is rewritten
            e.frame = w[k].frame;
            e.hash = w[k].hash;
            e.instr = w[k].instr;
            e.need = need;
            e.wanted = 1;
        }
    }
    void clearAll() { reset(); }

    bool ready(int idx) const { return e_[idx].wanted && e_[idx].have == e_[idx].need; }
    const Entry& entry(int idx) const { return e_[idx]; }

    // Background loader: the next (entry, channel) to read. Current banks first; the other bank of a slot only when prefetch[slot].
    // Within a bank the cues nearest after `from[slot]` (the playhead's pad) come first.
    bool next(const uint8_t curBank[kSlots], const uint8_t prefetch[kSlots], const uint8_t from[kSlots], int& idx, int& ch) const
    {
        for (int pass = 0; pass < 2; pass++)
            for (int s = 0; s < kSlots; s++)
            {
                if (pass == 1 && !prefetch[s]) continue;
                const int bank = (curBank[s] ^ pass) & 1;
                for (int i = 0; i < kCues; i++)
                {
                    const int k = (from[s] + 1 + i) % kCues;
                    const int id = index(s, bank, k);
                    const Entry& e = e_[id];
                    if (!e.wanted || e.have == e.need) continue;
                    idx = id;
                    ch = (e.have & 1) ? 1 : 0;
                    return true;
                }
            }
        return false;
    }
    void loaded(int idx, int ch) { e_[idx].have = (uint8_t)(e_[idx].have | (1u << ch)); }

    // Audio side: a note-on of `instr` at `startFrame` - the cached entry that covers it, and how far into it the start is.
    bool find(int instr, uint32_t startFrame, int& idx, uint32_t& delta) const
    {
        for (int i = 0; i < kEntries; i++)
        {
            const Entry& e = e_[i];
            if (e.wanted && e.have == e.need && e.need && e.instr == instr && startFrame >= e.frame && startFrame < e.frame + kFrames)
            {
                idx = i;
                delta = startFrame - e.frame;
                return true;
            }
        }
        return false;
    }

    uint16_t readyMask(int slot, int bank) const
    {
        uint16_t m = 0;
        for (int k = 0; k < kCues; k++)
            if (ready(index(slot, bank, k))) m = (uint16_t)(m | (1u << k));
        return m;
    }
    // Cue pads of the section that need no cache entry count as cached on the screen (resident sample, inside the pre-cache, ...).
    int pending(int slot, int bank) const
    {
        int n = 0;
        for (int k = 0; k < kCues; k++)
        {
            const Entry& e = e_[index(slot, bank, k)];
            if (e.wanted && e.have != e.need) n++;
        }
        return n;
    }

  private:
    Entry e_[kEntries];
};

// ---- latency model (plan 016 A3) ----------------------------------------------------------------------------------------------
// Press-to-sound of a jump in a streamed loop, in milliseconds: the pad is seen by the next step boundary / audio tick (quantise
// "off": the next tick), then the voice needs audio. Without the cache that is the first ring chunk from the SD card; with a cache hit
// it is the next audio block. All numbers in ms; blockMs = one audio block (128 frames at 44.1 kHz = 2.9 ms).
struct LatencyIn
{
    float tickWaitMs;     // wait for the engine tick that applies the jump (quantise off: <= tickMs; else the grid wait, same for both)
    float mainLoopMs;     // worst gap between two VoiceStreamManager::update() passes
    float sdFirstChunkMs; // seek + read of the first ring chunk (kChunkFrames) from the SD card
    float blockMs;
};
inline float latencyUncachedMs(const LatencyIn& l) { return l.tickWaitMs + l.mainLoopMs + l.sdFirstChunkMs + l.blockMs; }
inline float latencyCachedMs(const LatencyIn& l) { return l.tickWaitMs + l.blockMs; }
// The cache window must outlast the time the stream needs to deliver (mainLoop + SD) at the fastest playback speed.
inline bool windowCoversStream(const LatencyIn& l, float speed, float sampleRate)
{
    return (float)kFrames / (speed < 0.01f ? 0.01f : speed) / sampleRate * 1000.0f > l.mainLoopMs + l.sdFirstChunkMs;
}

} // namespace seqcue
