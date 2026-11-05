/* Audio Library for Teensy 3.X
 * Copyright (c) 2014, Paul Stoffregen, paul@pjrc.com
 *
 * Development of this audio library was funded by PJRC.COM, LLC by sales of
 * Teensy and Audio Adaptor boards.  Please support PJRC's efforts to develop
 * open source software by purchasing Teensy or other PJRC products.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice, development funding notice, and this permission
 * notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "monoToStereo.h"
#include "dspApplyGain.h"

AUDIO_CORE_FLASH void MonoToStereo::update(void)
{
    audio_block_t* block[Channel::channelCount];

    audio_block_t* in = receiveReadOnly(0);

    if (in == nullptr)
    {
        // plan 009 P#2: a missing block is a block of zeros: the pending gain ramps are used up as before
        for (int channel = 0; channel < Channel::channelCount; channel++)
            previous_multiplier[channel] = multiplier[channel];
        return;
    }

    // plan 009 P#6: both channels are written straight from the (shared, read-only) input: no copy of the input, no memcpy for the right
    // channel, one pass per channel
    block[Channel::left] = allocate();
    block[Channel::right] = allocate();
    if (block[Channel::left] == nullptr || block[Channel::right] == nullptr)
    {
        if (block[Channel::left] != nullptr)
            release(block[Channel::left]);
        if (block[Channel::right] != nullptr)
            release(block[Channel::right]);
        release(in);
        return;
    }

    for (int channel = 0; channel < Channel::channelCount; channel++)
    {
        applyGainFrom(in->data, block[channel]->data, multiplier[channel], previous_multiplier[channel]);
        transmit(block[channel], channel);
        release(block[channel]);
    }
    release(in);
}
