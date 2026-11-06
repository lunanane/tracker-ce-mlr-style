// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// The one call the playback path makes into the MLR cue cache (plan 016 A2), kept in its own tiny header (see seqMlr.h for the rest).
#pragma once
#include <stdint.h>

namespace seqmlr {
// Audio context (note-on of a streamed voice): a cached cue covering `startFrame`? addr = the cached audio at startFrame (this channel),
// frames = how many frames follow in the cache. false: not cached, the voice starts from the stream as before.
bool findCue(int instr, uint32_t startFrame, int channel, const int16_t*& addr, uint32_t& frames);
} // namespace seqmlr
