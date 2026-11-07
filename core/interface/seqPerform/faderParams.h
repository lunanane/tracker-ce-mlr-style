// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// The delay / reverb project values behind the PING / REV fader rows 2-4 of the Perform fader page (plans/004, 005 R3), shared with the Master
// fader pages (plan 024): the same faders, the same value tables, the same engine setters, one copy of the code.
#ifndef INTERFACE_SEQPERFORM_FADERPARAMS_H_
#define INTERFACE_SEQPERFORM_FADERPARAMS_H_

#include <stddef.h>
#include <stdint.h>

#include "seqPerformLogic.h"

namespace seqperf {

// pg = PAGE_PING (delay) or PAGE_REV (reverb), row 1 .. 3. Real project values, set like the Master page does (the engine setters, project marked changed).
void applyParamStep(uint8_t pg, uint8_t row, uint8_t step);
float currentParam(uint8_t pg, uint8_t row);                        // the value DISENGAGE has to restore later
void restoreParam(uint8_t pg, uint8_t row, float pre, uint8_t preRate); // only what differs is set; PING row 1 restores time and rate index
uint8_t paramRowStep(uint8_t pg, uint8_t row);                       // project value -> step 1 .. 8 shown by the fader
void paramRowText(uint8_t pg, uint8_t row, char* name, size_t nameCap, char* value, size_t valueCap); // "Time" "500 ms", "Size" "70" ...
void khzText(char* out, size_t n, int hz);                           // "850 Hz", "1.4 kHz"
void centreText(char* out, size_t n, int hz);                        // the delay band-pass centre: the top of the range is "open"

} // namespace seqperf

#endif
