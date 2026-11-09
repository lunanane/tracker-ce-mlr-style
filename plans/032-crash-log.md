# Plan 032 — crash and hang log on the SD card (v0.4.9)

## Why

0.4.7 and 0.4.8 freeze on "Opening last project..." and 0.4.6 doesn't. Reading the code hasn't found the cause, so we need the device to report where it stops. Today a fault runs `fault_isr` in `platforms/mk66/3rdparty/teensy_core/mk20dx128.c`, which loops forever. The screen freezes and nothing is recorded.

## What (decided)

1. **Record in SRAM `.noinit`** (the `.noinit` section of `mk66fx1m0.ld` is in RAM and survives a software reset). Use one struct with a magic word, a kind (FAULT / HANG), PC, LR, xPSR, CFSR, HFSR, BFAR, MMFAR, the breadcrumb ring and a crc or second magic.
2. **Fault handlers:** define strong `hard_fault_isr`, `memmanage_fault_isr`, `bus_fault_isr` and `usage_fault_isr` in the mk66 platform (they are weak aliases now). Use a naked asm stub that picks MSP or PSP from EXC_RETURN and passes the stacked frame to a C function. That function fills the record and does a system reset (`SCB_AIRCR = 0x05FA0004`). It does no SD work and calls nothing heavy.
3. **Breadcrumbs:** `seqcrash::mark(uint8_t stage, uint16_t a, uint16_t b)` writes the last 8 marks to a ring in the record. It costs a few stores and is safe from any context. Place marks at:
   - the stages of `TrackerImpl::init`;
   - the start of opening the last project;
   - each instrument or sample load step in `cFileManager::loadSampleData` (idx, sampleLoadStep);
   - pattern and song loads;
   - each sequencer sidecar load (seqtracks.bin, seqbrpt.bin, others);
   - the master recording init;
   - "project open done".
4. **Hang detection (software watchdog):** the main loop (`TrackerImpl::update`) increments a heartbeat. The existing systick ISR (1 kHz) checks it **only while armed**, which is from the start of project open until "open done". If the heartbeat hasn't moved for 10 s, the ISR records kind HANG with the **interrupted PC/LR from its own exception frame** (that is where the main loop is stuck) and resets.
   - Don't use the hardware WDOG; long legitimate SD operations make it risky.
5. **Next boot:**
   - If the record is valid, append a short text block to `/crashlog.txt` on the SD after the card is mounted and before the last project is opened. The block holds:
     - the firmware version and build tag;
     - the kind;
     - the registers in hex;
     - the breadcrumbs.
   - Then clear the magic.
   - Then **do not open the last project** on this boot. Stop on a plain screen: "Crash logged to crashlog.txt. Power off and on to retry."
   - Never touch `Workspace/`: no new empty project and no save. The next power cycle (magic cleared) opens the project normally, so there is no crash loop.
   - If writing the file fails, still show the registers on that screen.
6. **Flash budget:** the image margin at 0.4.8 is 5,164 B. Target ≤ 2 KB for the whole feature and keep strings short. The `check_sdram_ctors` and `check_image_size` checks must pass.
7. **Testing:**
   - A host test in `core/seqtest` covers the record encode/validate and the text formatter.
   - Every build keeps its `.axf` (`platforms/mk66/build/PolyendTracker_<stamp>_SEQ_<ver>.axf`) so the PC can be mapped with `arm-none-eabi-addr2line`.

## Owner-facing

Version 0.4.9. Copy the `.ptf` only to `D:\OneDrive\CodeLibraries\tracker\`.

## Implementation notes (v0.4.9)

Files: `core/source/seqCrashLogic.h` (record, crc, ring, text; host-tested in `core/seqtest/test_crash.cpp`), `core/source/seqCrash.h/.cpp` (noinit record, fault stub, systick hook, report), `platforms/mk66/3rdparty/teensy_core/EventResponder.cpp` (systick_isr), marks in `Tracker.cpp`, `WorkspaceProjectLoad.cpp`, `fileManagerInstrument.cpp`, `seqSidecar.cpp`, `interface.cpp`, `interfaceMisc.cpp`.

Design:
- Record = 96 B in SRAM `.noinit` (0x1FFF6CE8 in 0.4.9). Magic is written last, FNV-1a over the rest. `begin()` (first line of `TrackerImpl::init`) keeps a valid record and zeroes anything else. `mark()` is a no-op while a valid record waits, so the boot after a crash does not overwrite the marks before they are logged.
- Fault stub: naked `seqcrash_fault_stub` (strong `hard_fault_isr`, `memmanage_fault_isr`, `bus_fault_isr`, `usage_fault_isr` as aliases) does `tst lr,#4; ite eq; mrseq r0,msp; mrsne r0,psp; b seqcrash_fault_c`. The C function saves PC, LR, xPSR (from the stacked frame, only if the frame address is in SRAM), the frame address as SP, CFSR, HFSR, BFAR, MMFAR, the exception number (IPSR, "vec"), seals, and calls `restartMCU()`. `restartMCU` (not a bare AIRCR write) is used on purpose: it saves the start state 1 in the EEPROM first, else the reset would boot into the power-off wait. A fault inside the handler does a bare AIRCR reset.
- Hang watch: `TrackerImpl::update` calls `beat()`. `systick_isr` is now a naked stub that passes the interrupted frame (MSP or PSP) to `systick_c`, which keeps the old body and calls `seqcrash_tick(frame)`. Armed by `Interface::processOperatingMode` right before `openStartupProject()` (review fix), disarmed when the start screen finishes (or the card is pulled). 10000 ticks without a heartbeat = HANG with the interrupted PC/LR, recorded only on a tick that interrupted thread mode (EXC_RETURN bit 3; the stub passes lr in r1), so the PC is the stuck main loop, not another ISR.
- Next boot: in `mtOperatingModeOpenProject`, with the card in and a valid record, `showCrashScreen()` appends the block to `/crashlog.txt` (CRLF text), clears the record, hides the start screen, shows "Crash logged to crashlog.txt. Power off and on to retry." (or, if the write failed, the kind, PC, LR, CFSR) and the mode becomes the new `mtOperatingModeCrashHalt`. The last project is not opened, nothing is saved, Workspace is untouched. The text buffers are the idle `externalRamScratchBuff` (no extra SRAM). The power button is accepted in this mode with the shutdown save disabled (otherwise `handlePowerButtonAction` ignores it outside Run and the user could not power off); releasing it returns to the crash screen. Without a card the record stays until a card is inserted.
- Decision: the crash screen is not shown for a record found while the card is absent.

Log format (`/crashlog.txt`, appended):
```
--- crash ---
fw 1.0.0 CE-SEQ0.4.9 <build tag>
HANG|FAULT vec=<hex exception no.> pc=.. lr=.. psr=.. sp=..
cfsr=.. hfsr=.. bfar=.. mmfar=..
marks: SS:AAAA:BBBB ...      (oldest first, last 8, hex)
```
Map pc/lr with `arm-none-eabi-addr2line -f -C -e platforms/mk66/build/PolyendTracker_<stamp>_SEQ_0.4.9.axf <pc>` (use the .axf of the build whose tag is in the `fw` line; a Thumb pc has bit 0 set, addr2line ignores it).

Stage table (SS in the marks, a = AAAA, b = BBBB):
| SS (hex) | dec | where | a / b |
|---|---|---|---|
| 01 | 1 | `TrackerImpl::init` start | |
| 02 | 2 | after engine init, before sequencer init | |
| 03 | 3 | after master rec, profiler, cpu counter init, before `initConfig` | |
| 04 | 4 | after `mtInterface.begin`, before `newFileManager.init` | |
| 05 | 5 | `TrackerImpl::init` done | |
| 0A | 10 | card present, about to open the last project | |
| 0B | 11 | `openProjectFromWorkspace` returned | a = 1 opened / 0 no project (a new one is created) |
| 0C | 12 | project open done (start screen finished) | |
| 14..19 | 20..25 | `WorkspaceProjectLoad` entered step: 14 init, 15 project file, 16 instrument struct, 17 instrument sample, 18 pattern file, 19 finish | a = current instrument index (0-based) |
| 1E | 30 | `loadSampleData` begins a sample | a = instrument, b = size in KB |
| 1F | 31 | after the pool check | a = instrument, b = step it goes on with (1 new, 2 old format, 0 skipped) |
| 21 | 33 | sample loaded ok | a = instrument, b = samples / 256 |
| 22 | 34 | sample skipped or not valid | a = instrument, b = load status |
| 28..2E | 40..46 | sidecar loads, mark is written BEFORE each: 28 tracks, 29 clips, 2A plinky, 2B beat repeat, 2C warp, 2D fx, 2E faders | |
| 32 | 50 | finish step starts | |
| 33 | 51 | `switchRamPatternsNow` done | |
| 34 | 52 | all sidecars done | |
| 35 | 53 | `startAudioEngine` done | |
| 36 | 54 | patterns metadata loaded | |
| 37 | 55 | synth patches done, before `loadProjectValues` | |
| 38 | 56 | `loadProjectValues` done (load finished) | |

Reading a log: the last mark is the last thing that completed before the stop (for 0x28..0x2E the sidecar named by the mark was just entered). A pc inside an SD or memcpy routine with the last mark 1E/1F points at that instrument's sample load.

Limits: a hang is only caught while armed (project open) and when the 1 kHz systick still runs; a main loop that keeps running but never finishes loading is not detected. A stack overflow that corrupts memory without faulting is not caught. The ISR hang reset calls the EEPROM start-state write from interrupt context (same call the REBOOT path always made).

Cost: flash 1,832 B (margin 5,164 -> 3,332 B, image end 0xFB2FC); SRAM `_ebss` 0x2002A5B0 -> 0x2002A630 (+128 B: 96 B record + flags).
