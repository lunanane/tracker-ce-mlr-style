// Sequencer tracks mod - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Crash and hang log (plan 032): breadcrumbs, the fault / hang record kept in SRAM across the reset, and its report to the SD card.
#pragma once
#include <stdint.h>

class FileSystem;

namespace seqcrash {

void begin();                                    // first thing in TrackerImpl::init: keeps a valid record, clears anything else
void mark(uint8_t stage, uint16_t a = 0, uint16_t b = 0); // breadcrumb (a few stores, any context); no-op while a record waits
void beat();                                     // main loop heartbeat
void arm(bool on);                               // the hang watch runs from project open until open done
bool pending();                                  // a valid record from the previous run waits
// appends it to /crashlog.txt, clears it, and puts the screen text into msg (msgCap bytes). scratch: a buffer for the log text.
void report(FileSystem* fs, const char* fw, char* scratch, uint32_t scratchCap, char* msg, uint32_t msgCap);

} // namespace seqcrash
