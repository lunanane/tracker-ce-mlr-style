# Plan 030 — Live master recording to SD + multitrack audio over USB-C (owner 2026-11-07, investigation first)

Owner: "Can we actually record the master output to SD card, and can we bring in multi-track output via USB-C?
Such as MIDI over USB-C."

## Facts from the code (original Tracker, MK66)
- Stock: Export renders song / pattern / stems offline (mtAudioExport), not live. The Sample Recorder sources are
  line-in, radio, mic LG/HG (USB source only on the Tracker+ / IMX). No live master record.
- USB on the original is built as MIDI only (-DUSB_MIDI, teensy_core usb_desc.h). The USB audio code (output source
  kUsbInput, usb streams) is IMX-only. Teensy's USB_AUDIO mode exists in the core: stereo 16-bit, 44.1 kHz.
- The original's USB device runs at full speed (12 Mbit/s): one isochronous endpoint carries ≤ 1023 B per 1 ms frame.
  44.1 kHz × 16-bit = 88.2 B/ms per channel → at most ~11 channels at 16 bit, ~7 at 24 bit. 8 tracks + master at
  16 bit (10 ch = 882 B/ms) fits on paper.

## A — Live master recording to SD (likely feasible)
- Tap the master after the limiter (and/or line-in) into a ring; the main loop writes 16- or 24-bit WAV to /Recorded/.
- 44.1 kHz stereo 16-bit = 176 KB/s of SD writes; must share the SD bus with sample streaming and MLR streamed loops
  (reads): measure the worst case with the CPU monitor (blocking time, ring misses) before shipping.
- UI: Rec on the Master page (screen button, clearly labelled), time + file size shown; Stop transport does not stop it.

## B — Multitrack audio over USB-C (investigation, risky)
- Needs a composite USB device MIDI + Audio (UAC1, multichannel input to the computer, per-track post-fader taps like
  the stems export) with new descriptors; async clocking (Tracker = clock master, feedback endpoint or computer-side
  resampling); per-track taps cost CPU and RAM; class-compliant on macOS / Windows / iOS — test matrix needed.
- Open questions: is the USB-C port wired to USB0 (FS) or USBHS on the original? (HS would allow many more channels but
  the Teensy core has no HS device stack for audio.) Does the stock bootloader / firmware update over USB depend on the
  MIDI-only descriptor? Flash cost (~16 KB free now).
- Spike first: stereo master out over USB (Teensy USB_MIDI_AUDIO_SERIAL-style composite) on the device; then 10 ch.

## Feasibility study (2026-11-07, read-only)
A — master record to SD: GO. Tap the limiter outputs (no metronome/preview) with a small AudioStream sink copying
into a ring (~0.1-0.2 % of the audio block); the main loop writes one 16 KB chunk per pass to /Recorded/ as 16-bit
stereo WAV, deferred while the async stream read is busy or a streamed ring is low, forced when the record ring is
> 75 % full; header + f_sync every ~30 s; longest write / ring high-water / dropped blocks in the CPU monitor. The
audio ISR is never blocked (SD writes block only the main loop); the risk is card write stalls (20-250 ms) → ring of
~1 s = 192 KB EXTERNAL_RAM → SAMPLE_MEMORY_SIZE 45 → 44 units (-256 KB). Flash ~4-6 KB. Mutually exclusive with export
and Sample Recorder. Gate: 15-min device soak with 8 streamed MLR voices + PolySynth on a slow and a fast card.
B — USB audio: the original runs USB0 full speed (Teensy core, custom composite CDC + MIDI + RawHID, VID/PID
0x16D0:0x0E9F); the Tracker+ USB audio (NXP stack, HS, UAC2, 28 ch) does not port; Teensy usb_audio is not built.
Composite MIDI + UAC1 capture (device → computer only, Tracker = clock master, no feedback endpoint, packet size
servoed on the ring level because I2S runs at 44117.6 Hz) is possible. Full-speed limit: 11 ch at 16 bit → master
stereo + 8 mono tracks (post-fader taps at the voice amplifiers) = 10 ch; 8 stereo tracks do not fit. Risks: USB IRQ
priority 7 below audio (needs raising + measuring), Windows built-in UAC1 multichannel (fallback: stereo pairs),
re-enumeration, flash (stereo 6-8 KB, multichannel 10-12 KB). Firmware update is via SD, not affected.
Staged: 0 descriptor spike (enumeration on macOS / Windows / iOS, MIDI still works) → 1 stereo master → 2 10 ch.
Flash: A + B together > the current margin → flash savings first (%f printf ~8 KB, random-name tables ~14.7 KB).
