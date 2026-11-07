#ifndef AUDIOENGINE_VOICESTREAMMANAGER_H_
#define AUDIOENGINE_VOICESTREAMMANAGER_H_

#include <string.h>
#include "Platform.h"
#include "config.h"
#include "mtStructs.h"
#include <stdint.h>

class VoiceStreamManager
{
  public:
    static constexpr int kVoiceCount = 8;
    static constexpr int kMaxInstruments = 48;
#ifdef PLATFORM_IMX
    static constexpr int kMaxChannels = 2;
#else
    static constexpr int kMaxChannels = 1;
#endif
    static constexpr uint32_t kRingFrames = 16384; // 32 KB per channel (~371 ms ring buffer)
    static constexpr uint32_t kChunkFrames = 4096; // 8 KB (16 sectors) per async ADMA2 read
    static constexpr uint32_t kRingMask = kRingFrames - 1;
    static constexpr uint32_t kLoopWrapFrames = 4096; // 8 KB per channel loop wrap pre-cache (plan 016 B: was 2048; a warped loop reads ~2 grains past the loop start at once)
    static constexpr uint32_t kLookbehindFrames = 512;

    void init(FileSystem* fileSystem = nullptr);

    // Non-blocking background scheduler (call from main loop / updateSdStream)
    void update();

    // Synchronous pump for offline rendering (call from mtAudioExport::updateRender)
    void updateSynchronous();

    // Voice lifecycle notifications (safe to call from Audio ISR)
    void notifyNoteOn(int voiceIdx,
                      int instrIdx,
                      uint32_t startFrame,
                      uint32_t loopStartFrame,
                      uint32_t loopEndFrame,
                      uint32_t endFrame,
                      uint8_t playMode,
                      bool isReverse,
                      float pitchSpeed,
                      uint32_t sliceCachedFrames = 0);
    void notifyNoteOff(int voiceIdx);
    void invalidateInstrumentVoices(int instrIdx);

    AUDIO_CORE_RAM bool getSlicePreCache(int instrIdx,
                                         uint8_t sliceIdx,
                                         int channel,
                                         uint32_t actualStartFrame,
                                         const int16_t*& outAddr,
                                         uint32_t& outFrames) const;

    AUDIO_CORE_RAM void updateVoicePlayhead(int voiceIdx,
                                            uint32_t absFrame,
                                            int8_t direction,
                                            float pitchSpeed);

    AUDIO_CORE_RAM int16_t getSample(int voiceIdx,
                                     int channel,
                                     uint32_t absFrame,
                                     const int16_t* preCacheBase,
                                     uint32_t preCacheFrames);

    // plan 022 P2: the same value as getSample, the common cases inline (RAM pre-cache hit; ring hit once the fade-in is over); the
    // rest (fade-in, loop-wrap cache, a miss and its bookkeeping) goes to the out-of-line function. Bit-identical to getSample.
    __attribute__((always_inline)) inline int16_t getSampleFast(int voiceIdx,
                                                                int channel,
                                                                uint32_t absFrame,
                                                                const int16_t* preCacheBase,
                                                                uint32_t preCacheFrames)
    {
        if (absFrame < preCacheFrames && preCacheBase != nullptr)
        {
            return preCacheBase[absFrame];
        }
        if ((uint32_t)voiceIdx < (uint32_t)kVoiceCount)
        {
            const VoiceStreamState& v = voices[voiceIdx];
            if (v.active && v.fadeInCounter >= 32)
            {
                const uint32_t vStart = v.validStartFrame;
                const uint32_t vEnd = v.validEndFrame;
                if (absFrame >= vStart && absFrame < vEnd)
                {
                    return ring_[voiceIdx][(channel > 0 && v.isStereo) ? 1 : 0][absFrame & kRingMask];
                }
            }
        }
        return getSample(voiceIdx, channel, absFrame, preCacheBase, preCacheFrames);
    }

    // plan 027: one run of `count` frames from absFrame, copied from the ring into SRAM (the ring is uncached SDRAM: a single read
    // costs about 20 cycles, a word burst a few). Only when every frame is what getSampleFast would return from the ring (voice active,
    // fade-in over, the whole run inside the valid window); false otherwise and nothing is copied.
    bool copyRingRun(int voiceIdx, int channel, uint32_t absFrame, uint32_t count, int16_t* dst) const
    {
        if ((uint32_t)voiceIdx >= (uint32_t)kVoiceCount || count == 0 || count > kRingFrames / 2)
            return false;
        const VoiceStreamState& v = voices[voiceIdx];
        if (!v.active || v.fadeInCounter < 32)
            return false;
        const uint32_t vStart = v.validStartFrame;
        const uint32_t vEnd = v.validEndFrame;
        if (absFrame < vStart || absFrame + count > vEnd || absFrame + count < absFrame)
            return false;
        const int16_t* src = ring_[voiceIdx][(channel > 0 && v.isStereo) ? 1 : 0];
        uint32_t i = absFrame & kRingMask;
        uint32_t left = count;
        while (left)
        {
            uint32_t n = kRingFrames - i < left ? kRingFrames - i : left;
            // a leading odd frame, then word copies, then a trailing odd frame (the ring is 4 byte aligned)
            const int16_t* sp = src + i;
            int16_t* dp = dst + (count - left);
            uint32_t k = n;
            if ((i & 1u) && k)
            {
                *dp++ = *sp++;
                k--;
            }
            const uint32_t w = k & ~1u;
            if (w)
            {
                if (((uintptr_t)dp & 3u) == 0)
                    memcpy(dp, sp, w * sizeof(int16_t));
                else
                    for (uint32_t q = 0; q < w; q++)
                        dp[q] = sp[q];
                dp += w;
                sp += w;
                k -= w;
            }
            if (k)
                *dp = *sp;
            left -= n;
            i = (i + n) & kRingMask;
        }
        // the voice may have been restarted while we copied (the generation of the ring changed): re-check the window
        return v.active && absFrame >= v.validStartFrame && absFrame + count <= v.validEndFrame;
    }

    // Per-instrument EXTERNAL_RAM metadata accessors
    char* getInstrumentStreamPath(int instrIdx);
    const char* getInstrumentStreamPath(int instrIdx) const;
    uint32_t* getInstrumentClmt(int instrIdx);
    const uint32_t* getInstrumentClmt(int instrIdx) const;
    WaveformPeakPoint* getInstrumentWaveformPeaks(int instrIdx, int channel);
    const WaveformPeakPoint* getInstrumentWaveformPeaks(int instrIdx, int channel) const;

    void clearInstrumentMetadata(int instrIdx);
    void copyInstrumentMetadata(int srcInstrIdx, int dstInstrIdx);
    void buildWaveformPeaksFromRam(int instrIdx,
                                   int channel,
                                   const int16_t* samples,
                                   uint32_t totalFrames);
    bool setupInstrumentStream(int instrIdx,
                               const char* filePath,
                               uint32_t dataOffset,
                               uint32_t totalFrames,
                               bool isStereo);
    bool rebindInstrumentStreamFile(int instrIdx,
                                    const char* filePath,
                                    uint32_t dataOffset);
    void refreshInstrumentSliceCache(int instrIdx);
    void refreshInstrumentSingleSliceCache(int instrIdx, uint8_t sliceIdx);

    // Blocking read of arbitrary frame range from a streamed instrument's backing file
    // (used for zoomed waveform view, Sample Editor staging, ProjectSave, and Sample Export)
    bool readInstrumentFramesBlocking(int instrIdx,
                                      int channel,
                                      uint32_t startFrame,
                                      uint32_t frameCount,
                                      int16_t* dest);

    // Translate file byte offset to physical SD card LBA sector using instrument's CLMT
    bool translateOffsetToLba(int instrIdx,
                              uint32_t fileByteOffset,
                              uint32_t& outLbaSector,
                              uint32_t& outMaxContigSectors) const;

  private:
    struct VoiceStreamState
    {
        volatile bool active = false;
        volatile uint8_t instrIdx = 0;
        volatile uint8_t playMode = 0;
        volatile int8_t direction = 1;
        volatile bool isStereo = false;
        volatile uint32_t startFrame = 0;
        volatile uint32_t endFrame = 0;
        volatile uint32_t loopStartFrame = 0;
        volatile uint32_t loopEndFrame = 0;
        volatile uint32_t preCacheFrames = 0;
        volatile uint32_t totalFrames = 0;
        volatile uint32_t currentFrame = 0;
        volatile float pitchSpeed = 1.0f;

        volatile uint32_t validStartFrame = 0;
        volatile uint32_t validEndFrame = 0;
        volatile uint32_t ringSampleHash = 0;

        volatile bool loopWrapReady = false;
        volatile uint32_t loopWrapStartFrame = 0;
        volatile uint32_t loopWrapEndFrame = 0;

        volatile uint32_t generation = 0;
        volatile uint16_t fadeInCounter = 32;
    };

    enum class AsyncTargetType : uint8_t
    {
        kNone = 0,
        kRingForward,
        kRingBackward,
        kLoopWrap
    };

    struct AsyncJob
    {
        bool inProgress = false;
        bool transferLaunched = false;
        int voiceIdx = -1;
        uint8_t instrIdx = 0;
        uint8_t channel = 0;
        uint8_t totalChannels = 1;
        AsyncTargetType targetType = AsyncTargetType::kNone;
        uint32_t fetchStartFrame = 0;
        uint32_t fetchFrameCount = 0;
        uint32_t intraSectorByteOffset = 0;
        uint32_t expectedGeneration = 0;
    };

    // ring buffers of the voices (EXTERNAL_RAM, defined in VoiceStreamManager.cpp)
    static int16_t ring_[kVoiceCount][kMaxChannels][kRingFrames];

    FileSystem* fs = nullptr;
    VoiceStreamState voices[kVoiceCount];
    // plan 022 P2: MLR cue cache / waveform work (seqmlr::idle) only at quiet moments of the stream
    volatile uint32_t jumpSeq_ = 0;  // counts note-ons of streamed voices (audio context)
    uint32_t seenJumpSeq_ = 0, quietUntilMs_ = 0, lastIdleMs_ = 0;
    AsyncJob currentJob;

    bool readInstrumentFramesBlockingImpl(int instrIdx, int channel, uint32_t startFrame, uint32_t frameCount, int16_t* dest);
    bool startAsyncSectorReadForJob(uint8_t channel);
    void completeChannelTransfer();
    bool scheduleNextVoiceJob(bool synchronous);
    void fillJobSynchronously(const AsyncJob& job);
};

extern VoiceStreamManager voiceStreamManager;

#endif /* AUDIOENGINE_VOICESTREAMMANAGER_H_ */
