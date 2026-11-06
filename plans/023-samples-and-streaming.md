# Plan 023 — long samples stream automatically, more sample slots (owner 2026-11-06)

Owner: "Long samples can be added to the sample list — maybe we can extend the sample list as well, 48 is not enough?
And longer samples that don't fit in RAM should just be set to streaming — is this possible?" Device check by the owner:
importing a sample into a slot is still **refused** when the RAM pool would be exceeded. Original Tracker (MK66) only;
build only `make original`. Part A ships first (separate commit), part B is decided by the owner.

## Investigation

### What exists (hybrid streaming, stock CE)
- `kStreamThresholdBytes` = 256 KB, `kStreamPreCacheFrames` = 32768 (64 KB mono / 128 KB stereo kept in the pool).
  A streamed instrument keeps its pre-cache in `externalRamSampleMemory`, the rest stays in
  `Workspace/instruments/instrument_XX.pti` and is refilled by `VoiceStreamManager` (16384-frame ring per voice).
- `isPlayModeStreamable()`: One-Shot and the three loop modes only on MK66; Slice, Beat Slice, Wavetable, Granular are
  resident-only.
- Pool: `SAMPLE_MEMORY_SIZE` = 11.25 MB (`max_memory`), `used_memory` counts the bytes the instruments hold in it
  (`getSampleMemorySize()`: `ramFrames` for streamed, else the length).

### Where a long sample was refused or dropped (before this plan)
1. **Import into the sample list (the owner's refusal).** `SampleImport` loads the whole converted WAV into the pool
   (`loadSampleDataToMemeory`), saves it to the workspace, and only then demotes it to a stream
   (`importSamplesToWorkspaceFinish` -> `demoteRamInstrumentToStream`). So a sample had to fit the **free pool in full**
   even though it ends up costing 64 KB. The UI checks it before the import starts: `calcImportIsEnoughMemoryNormal/LoFi`
   (`(end - start) * 2 < importMemoryFree - 10000`) and the memory bar / `fullMemoryFlag` use
   `currentFolderMemoryFileUsage[]` (full converted size), which blocks `importSamples()`.
   Also in the same screens: the size of the sample being replaced was counted as `2 * length` even if it is streamed.
2. **Project load.** `startNewInstrumentSampleLoad` streams only when size >= 256 KB **and** the play mode is streamable.
   A long sample in Slice / Beat Slice / Wavetable / Granular (or one whose mode was switched) was loaded resident; if it
   did not fit, `loadSampleData` ("memory usage check") set `fileTransferFileNoValid` and the **instrument was dropped**.
3. Not a refusal but a limit: switching a streamed sample to a resident-only mode needs the whole sample in the pool
   (`promoteStreamedInstrumentToRam`); when it does not fit the mode silently stayed unchanged.
4. Recorder (`fSampleRec`, `used_memory + recorder.getSize() > max_memory`) records into RAM by design; unchanged.
5. `.pti` import (instrument files) and `.mod` / `.it` import: loaded resident first; unchanged (a long `.pti` made by
   this firmware is already a header + PCM file and loads as a stream through the project path).

### Places that assume a resident sample (audit)
Already handle streamed samples in stock CE: sample editor / offline effects (`mtEffect` reads through
`readInstrumentFramesBlocking`, limit `EDIT_BUFF_SIZE / 2` -> "too long" popup), waveform display (peak cache),
auto-slice (`mtSliceManager`), export (`fileManagerExport`, `fileManagerExportToMod`), save (`saveSampleDataToWorkspace`),
copy (`InstrumentCopy`), project save, MLR cue cache (`seqMlr` reads `isStreamed && frame >= preCacheFrames` from SD),
warp (works for streamed samples, plan 016). Gaps found and handled here: the mode-switch refusal (3) had no message
(now a popup), the importer's memory estimates (1), the instrument lists had no mark (now "SD ").
Wavetable / Granular read straight from the resident data and cannot stream: a sample that must be streamed gets the
play mode One-Shot (see policy).

## Part A — design: a sample that does not fit is streamed, never refused
Policy (`core/source/sampleMemLogic.h`, host-tested):
- **Import (WAV)**: free pool = `max_memory - used_memory` (after the replaced slots are released, minus 10 000 B slack).
  Fits resident -> old path, unchanged (demoted to a stream afterwards when >= 256 KB and streamable).
  Does not fit and >= 256 KB and the pre-cache fits -> **streamed import**: the WAV is read in chunks (32 KB), converted
  (same `convertAudioData` as before) and written straight to `Workspace/instruments/instrument_XX.pti` (header first,
  then the left block, then the right block for stereo — the `.pti` layout), the pool only reserves the pre-cache, then
  `setupInstrumentStream` loads the pre-cache, builds the CLMT and the waveform peaks. The converted sample is never
  resident. Smaller than 256 KB and no room -> still refused (the pool really is full of small samples).
  If the slot keeps a Wavetable / Granular mode the play mode becomes One-Shot (those cannot stream).
- **UI**: the file list / memory bar count the **pre-cache** for files >= 256 KB (what stays in the pool); the replaced
  slot counts what it really holds (`getSampleMemorySize`).
- **Project load**: >= 256 KB and streamable -> streamed (as before); otherwise resident if it fits; if it does not fit
  and >= 256 KB -> streamed (Slice / Beat Slice keep their mode, Wavetable / Granular become One-Shot and the instrument
  is marked changed so the next save keeps it). Previously the instrument was dropped.
- **Mode switch** to a resident-only mode that does not fit: unchanged behaviour plus the popup
  "Streamed sample (SD) / too long for this mode".
- **Mark**: "SD " in front of the name in the importer's slot list, the instrument picker popup and the pattern editor
  instrument names.
- Cost: no new RAM except code (~1.5 KB flash); chunk buffers reuse `externalRamFileManagerSrcBuff` / `BuffL`.
- Coordination with the performance agent: **no edits in `VoiceStreamManager.*`, the playback loops or the governor**; the
  streamed import only calls `setupInstrumentStream` like every other streamed load.

## Part B — more sample slots: findings and options (not implemented, owner decides)
Facts:
- Instruments are one index space in the pattern step (`uint8_t instrument`): samples 0..47, MIDI 48..63 (PolySynth
  instances are MIDI 14..16 = indices 61..63 in this mod), `kTotalInstrumentsCount` = 64 (+3 synths on Plus/Mini).
  Stored patterns, clips, MLR sidecars and `seqInstOrderLogic` all use these indices.
- Arrays sized by `kSampleInstrumentsCount` (48): `mtProject.instrument[]`, `sampleMemory[]`, `instrumentsToSave[]`,
  `interfaceGlobals.intrumentsNames[]`, `VoiceStreamManager::kMaxInstruments`, per-slot stream tables, engine blocking
  flags; `kSampleInstrumentsCount` / `MIDI_INSTRUMENTS_START_IDX` appear in ~210 places in 45 files, ~110 of them range
  checks `instrument < kSampleInstrumentsCount` that mean "is a sample" and assume samples are the low contiguous block.
- Files: `instrument_%02d.pti`/`.mti` (two digits, 01..48 -> 49..96 fits the format); the project file does not store
  an instrument count; `strInstrument` is 372 B in the `.pti`.
RAM per extra slot: `strInstrument` 372 + `strSampleMemory` 36 + name buffer 37 = ~445 B internal RAM; the stream tables
(`s_instrumentStreamPaths` 256 + CLMT 256 + waveform peaks 2400 + slice cache 384) ~3.3 KB EXTERNAL_RAM. For +32 slots:
**~14 KB of the 25.7 KB free internal RAM (87.9 % -> 93.5 %)** and **~105 KB EXTERNAL_RAM, only ~70 KB free** (the
difference has to come out of `SAMPLE_MEMORY_SIZE`, e.g. -128 KB of the 11.25 MB pool, documented trick). Instrument
structs could instead live in EXTERNAL_RAM raw storage (no constructors) at the price of touching every `mtProject.instrument[i]`
access that is on the audio path.
Options:
1. **Slots 64..95 after the MIDI range** (stored patterns keep their meaning, old projects/firmware unaffected: a new
   firmware opening an old project sees empty slots 49..96; an old firmware opening a new project ignores the extra
   `.pti` files and a step with instrument >= 64 would play nothing / be clamped — it must not be sent to MIDI/PolySynth
   logic that does `instrument - 48`). Needs an index <-> slot mapping layer (`isSample(i)`, `sampleSlot(i)`) replacing the
   ~110 range checks, the order logic, the pattern popups and a paged sample list; roughly a v0.5 sized change.
2. **Slots 48..95 and move MIDI to 96..111**: cleaner, but changes the meaning of stored patterns -> needs a project
   conversion and breaks old firmware. Rejected.
3. **Banks**: keep 48 slots, add a bank number per project (sample list shows bank 1/2). Same RAM as today only if banks
   are swapped (instrument files + pool re-load on bank change) — not instant, rejected for live use.
4. Do nothing: with long samples streaming (part A) the pool is no longer the limit; 48 slots x 64 KB pre-cache = 3 MB.
Recommendation: ship part A, let the owner use it for a while; if 48 slots are still the limit take option 1 in its own
plan with the RAM trade above (needs the owner's call on -128 KB pool and +14 KB internal RAM).

## Tests / checks
- Host: `core/seqtest/test_sampmem.cpp` (decisions: legacy refusal reproduced, new streamed placement, thresholds,
  slack, mode fallbacks, load policy, promote).
- Device list: `docs/seq-tracks-test-list.md` section 11o.
- Build: `make original` (image size and EXTERNAL_RAM constructor checks pass), seqtest and seqgen `make test`.

## As built (part A)
- `core/source/sampleMemLogic.h`: `decideImport`, `decideLoad`, `importResidentBytes`, `canPromote`,
  `legacyImportFits` (the old rule, kept only so the host test can show the refusal). `playModeNeedsResident()` added in
  `mtStructs.h`.
- `FileManager/SampleImport.cpp`: `loadImportingSamplesData` decides per slot (running total of the pool bytes already
  planned for the batch); new `streamConvertToWorkspace` state machine (one 32 KB chunk per `update()`, left block then
  a second pass for the right block, the converted chunk buffer is `externalRamFileManagerBuffL`, output <= 8160 frames
  per channel per chunk, `endFile` truncates a stale tail, then `setupInstrumentStream`). `setInstrumentSampleAddress`
  got an optional `streamPreCacheFrames` so the slot is reserved as pre-cache only before the memory move.
  `SampleLoadData.streamImport` (plain member, no constructor on the EXTERNAL_RAM buffer).
- Project load: `startNewInstrumentSampleLoad` uses `decideLoad`; a changed play mode marks the instrument struct changed.
- Importer UI: `processDirFileSizes` stores the resident estimate for WAVs; free / replaced-slot figures use
  `getSampleMemorySize`; the SD mark and the mode-switch popup as designed.
- Not changed: `VoiceStreamManager.*`, playback loops, governor, recorder, `.pti` / `.mod` / `.it` import (still resident
  first), the 48 slot count.
- Checks: `make original` clean (image end 0xEC59C, margin 64 100 B, RAM 87.87 %, EXTERNAL_RAM 99.58 %, no constructor on
  EXTERNAL_RAM), `core/seqtest` and `seqgen` `make test` pass. **Not run on a device**: the streamed import path (SD write
  while converting, stereo second pass, range import, lo-fi) is device-only code; test list 11o.
- Known limits: the streamed import converts in 32 KB chunks as the resident import did (chunk-edge resampling is the
  same as before); stereo reads the WAV twice (once per channel block); a streamed import onto a slot with a very
  large old file relies on `endFile` to truncate.

## Part B decision needed (owner)
Not implemented: see "Part B" above. The cheapest safe step is option 1 in its own plan after part A has been used; it
costs ~14 KB internal RAM (87.9 % -> 93.5 %) and ~105 KB EXTERNAL_RAM (taken from the sample pool) for +32 slots.
