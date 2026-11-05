#!/bin/sh
# Sequencer tracks mod: fail the build if a C++ static constructor references EXTERNAL_RAM (SDRAM, 0x70000000).
# Static constructors run in ResetHandler before main() powers and configures the SDRAM; a write there HardFaults
# at boot (v0.3.3/v0.3.4 hang). Objects in __NOINIT(EXTERNAL_RAM) must be placement-new'd in TrackerImpl::init().
# Allowed: _GLOBAL__sub_I_measureBuffer (stock; only stores the SDRAM address into an internal-RAM object).
AXF="$1"; OBJDUMP="${2}objdump"
BAD=$("$OBJDUMP" -d "$AXF" | awk '/^[0-9a-f]+ <.*>:$/{f=$2} f ~ /_GLOBAL__sub_I/ && /\.word[ 	]+0x70/{print f,$NF}' | grep -v '_GLOBAL__sub_I_measureBuffer')
if [ -n "$BAD" ]; then
  echo "ERROR: static constructor touches EXTERNAL_RAM before SDRAM init (boot hang):"; echo "$BAD"; exit 1
fi
echo "static constructors: no EXTERNAL_RAM access (ok)"
