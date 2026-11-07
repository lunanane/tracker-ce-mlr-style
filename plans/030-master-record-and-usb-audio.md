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
