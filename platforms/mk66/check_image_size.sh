#!/bin/sh
# Sequencer tracks mod: fail the build when the flash image ends at or above the limit (default 0xFC000 = 1032192 B, plan 019).
# The old 0xF0000 rule was a misreading (v0.2 at 0xF04B0 ran fine; tracker-ff ships images to 0xFBB90, 0xFC2AC is known to boot).
# Image end = text + data (flash starts at 0). Prints the end and the margin to the limit; warns below 16 KB margin.
AXF="$1"; SIZE="${2}size"; LIMIT="${3:-1032192}"
END=$("$SIZE" "$AXF" | awk 'NR==2{print $1+$2}')
MARGIN=$((LIMIT - END))
printf "image end: 0x%X (%d B), limit 0x%X, margin %d B\n" "$END" "$END" "$LIMIT" "$MARGIN"
if [ "$MARGIN" -le 0 ]; then echo "ERROR: image reaches the flash limit"; exit 1; fi
if [ "$MARGIN" -lt 16384 ]; then echo "WARNING: less than 16 KB margin below the limit"; fi
