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

## Owner decisions 2026-11-07
- SAMPLE_MEMORY_SIZE -256 KB for the record ring: yes.
- Focus: on-device master recording (part A) now; USB stays MIDI (must keep working). USB audio (part B) later — the
  owner has it implemented in another fork, look at that first.
- Provisional (lead): /Recorded/ folder, limiter tap (no metronome/preview), header 44100 Hz; flash room from the
  %f printf conversion (no visible change) if needed.

## As built - part A (branch v046-masterrec, Original Tracker only)
**Tap.** `MasterTap` (an `AudioStream` sink with two inputs, `seqMasterRec.cpp`, update in `AUDIO_CORE_RAM`) is connected to both limiter outputs (`connectOutputToExportQueue`), so the metronome and the sample preview are not in the file. Per block it packs 128 frames
(L low half, R high half) into a ring with one 32-bit store per frame (null block = silence); while not recording it only releases the two blocks. **Ring** `seqrec::Ring` (`seqMasterRecLogic.h`): 192 KB raw storage in EXTERNAL_RAM
(`__NOINIT`, no constructor, `check_sdram_ctors.sh` ok), 384 blocks = 1.114 s, capacity = size - 1 block; a block that does not fit is dropped and counted; the high-water mark is tracked. `SAMPLE_MEMORY_SIZE` 45 -> 44 units (-256 KB; EXTERNAL_RAM net -64 KB: 99.70 % -> 99.31 %;
the resident sample pool is 7.25 MB now, an older project that used more than that is cut by the load memory check).
**Writer.** `seqmasterrec::update()` runs in `TrackerImpl::update` right after the first `engine.updateSdStream()`; only while recording. One 16 KB chunk per pass at most; `shouldWrite`: needs >= 16 KB, **forced when the ring is > 75 % full**, otherwise
not while `FileSystem::isAsyncReadBusy()` (a stream refill is in flight) and not within 6 ms of the last write. (VoiceStreamManager is untouched; the SD driver itself waits for a running async read before a blocking write, so the busy check is politeness, not safety.)
The WAV header (16-bit stereo, **44100 Hz** as decided; the codec really runs at 44 117.6 Hz, 0.04 % slow) is written at the start with 0 bytes, rewritten + `f_sync` every 30 s and on stop (seek 0, write 44 B, sync, seek back: the seek walks the FAT chain, so a long file adds a
few ms per rewrite; it is counted in "longest write"). File `/Recorded/<project>_<nnn>.wav` (folder created; nnn = first free 001..999; characters illegal in FAT replaced by `_`); the start is refused with < 8 MB free (`f_getfree`, may take
a moment on a big card: the first call scans the FAT unless FSINFO is valid). Errors (short write = card full / removed, 4 GB file limit with an 8 MB margin) stop cleanly: tap off, header + close, a popup with the reason. Stop flushes the ring (<= 12 chunks).
Sleep (power button) stops and closes the file. New `FileSystem::syncFile` / `getFreeBytes` (defaults in the base class so the IMX file system still builds; MK66 implements them with `f_sync` / `f_getfree`).
**Exclusive** with the export (`startExport`, `startExportSelection`, `cFileManager::exportSound*` refuse, with a popup in the UI) and the Sample Recorder (record start refuses); Rec refuses while either runs. Transport Stop does not stop the recording.
**UI.** Global Mixer 3/3, screen button 6 (between the empty 5 and Faders 7): "Rec" / "Stop rec" in the red-fill label colours (`performActiveTrackLabelsColors`, see `docs/seq-ui-colours.md`); the right label of the title bar shows `REC mm:ss  size` while recording
(set when the text changes, checked every 50 ms by the Master page). No long-press, no hotkey. A REC indicator on other modules was **not** added (the title bar of every module would need a hook: not cheap).
**CPU monitor.** New last line: `Master rec: N writes   longest write X ms   ring peak P%   dropped blocks D` (values of the running / last recording).
**Tests.** `core/seqtest/test_masterrec.cpp` (6099 checks): header bytes, ring interleave / wrap / full / drop count / silence, file names + sanitising + numbering, scheduler truth table, session in simulated time with a fake card (65 s plain: 0 dropped, ring high-water 8 %, file
byte-identical to what the audio produced; **a 250 ms write stall every 7th write for 120 s: 0 dropped, high-water 31 %; the card busy all the time and the first forced write stalling 250 ms: 0 dropped, high-water 97 %**; a 1.3 s stall does drop and is counted), errors
(no card, no space, create failed, short write, 4 GB limit, restart), exclusivity codes, time / size text. `make rec-preview` (Pillow) draws `rec_idle.png` / `rec_on.png` (a schematic of Global Mixer 3/3 using the real label / title strings). The seqcpu monitor test is unchanged (the extra
line fits the display budget). No existing check was changed.
**Image.** FLASH 1 016 800 -> 1 022 020 B (+5.2 KB), RAM 238 736 -> 239 024 B, image end 0xF9564 -> 0xFA9D4 (margin 10 908 -> 5 676 B; clean build; the `%f` printf conversion was not needed), `check_sdram_ctors.sh` ok. Nothing on the audio ISR / SD path is -Os (seqMasterRec.cpp is -O2; the UI is -Os like the rest of the Master page).
**Margin worry.** The worst simulated case (streams keep the card busy, the first forced write takes 250 ms) peaks at 97 % of the ring: about 30 ms of headroom. A 300 ms stall at the wrong moment would drop blocks (counted in the monitor). If the soak shows "dropped" > 0, lower the forced
threshold (kForceNum 3/4 -> 5/8) before enlarging the ring.
**Not done / needs the device.** Everything on hardware: real write times on a slow / fast card, the 15 min soak with 8 streamed MLR voices + PolySynth (dropped blocks, ring misses, governor), `f_getfree` time on a large card, USB MIDI during a recording (no USB code touched),
the real label / title bar look (the preview is a schematic), the sleep path. Part B (USB audio) not started.
