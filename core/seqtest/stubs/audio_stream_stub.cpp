// host stub (plan 009 P tests): a minimal AudioStream so the real effect_*.cpp files compile and run on the desktop.
// Blocks come from a small pool, transmit() records the last block per output so a test can read it.
#include "AudioStream.h"
#include <stdlib.h>
#include <string.h>

bool AudioStream::update_scheduled = false;
AudioStream* AudioStream::first_update = nullptr;
uint16_t AudioStream::memory_used = 0;
uint16_t AudioStream::memory_used_max = 0;
bool AudioStream::renderMode = false;

// the test puts the input blocks into stubIn[index] (consumed by the next receive*), transmit() records into stubOut[index]
audio_block_t* stubIn[8] = {};
audio_block_t* stubOut[8] = {};
static int s_alloc = 0;

audio_block_t* AudioStream::allocate(void)
{
    audio_block_t* b = (audio_block_t*)calloc(1, sizeof(audio_block_t));
    b->ref_count = 1;
    s_alloc++;
    return b;
}
void AudioStream::release(audio_block_t* block)
{
    if (!block)
        return;
    if (--block->ref_count == 0)
    {
        s_alloc--;
        free(block);
    }
}
void AudioStream::transmit(audio_block_t* block, unsigned char index)
{
    if (index > 7)
        return;
    if (stubOut[index])
        release(stubOut[index]);
    block->ref_count++;
    stubOut[index] = block;
}
audio_block_t* AudioStream::receiveReadOnly(unsigned int index)
{
    audio_block_t* b = stubIn[index];
    stubIn[index] = nullptr;
    return b;
}
audio_block_t* AudioStream::receiveWritable(unsigned int index)
{
    audio_block_t* b = stubIn[index];
    stubIn[index] = nullptr;
    if (b && b->ref_count > 1) // shared: copy, like the real one
    {
        audio_block_t* copy = allocate();
        memcpy(copy->data, b->data, sizeof(b->data));
        release(b);
        b = copy;
    }
    return b;
}
int stubAllocated() { return s_alloc; }
void AudioStream::initialize_memory(audio_block_t*, unsigned int) {}
