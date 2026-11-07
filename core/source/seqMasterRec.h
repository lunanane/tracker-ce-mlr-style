// Sequencer tracks mod - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Live master recording to the SD card (plan 030 A): device glue. The logic is in seqMasterRecLogic.h.
#pragma once
#include "seqMasterRecLogic.h"

class FileSystem;
class AudioStream;

namespace seqmasterrec {

AudioStream* tapStream();   // the sink the two limiter outputs are connected to (mtAudioEngine.cpp)
void init(FileSystem* fs);   // TrackerImpl::init: the ring (EXTERNAL_RAM raw storage) and the file access
void update();               // main loop, once per pass: writes the ring to the card while recording
bool active();               // a recording is running
seqrec::Err start();         // Rec button; E_NONE = running (the project name comes from the file manager)
seqrec::Err stop();          // Rec button; flushes and closes
const seqrec::Session& session();
const seqrec::Ring& ring();
// the other modules ask before they start (export / sample recorder refuse while this returns true)
inline bool blocksOthers() { return active(); }

} // namespace seqmasterrec
