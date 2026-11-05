// Sequencer tracks mod -- PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Per audio block peak ring of the amplitude view (plan 015 V): the audio interrupt writes ONE value per block (2.9 ms), the main loop reads
// the new ones behind `head`. The buffer belongs to the reader (the view puts it into EXTERNAL_RAM); without a buffer nothing is measured.
// kLen blocks = 186 ms of slack for the main loop; a reader that falls further behind loses blocks (the view fills the gap).
#ifndef LIBS_AUDIO_PEAK_RING_H_
#define LIBS_AUDIO_PEAK_RING_H_

#include <stdint.h>

struct PeakRing
{
    static constexpr uint16_t kLen = 64; // power of two
    uint16_t* volatile buf = nullptr;
    volatile uint16_t head = 0;
    bool active() const { return buf != nullptr; }
    void attach(uint16_t* b)
    {
        head = 0;
        buf = b;
    }
    void put(uint32_t pk)
    {
        uint16_t* b = buf;
        if (b == nullptr)
            return;
        const uint16_t h = head;
        b[h & (kLen - 1)] = (uint16_t)(pk > 65535u ? 65535u : pk);
        __atomic_store_n(&head, (uint16_t)(h + 1), __ATOMIC_RELEASE);
    }
    uint16_t headIndex() const { return __atomic_load_n(&head, __ATOMIC_ACQUIRE); }
};

#endif
