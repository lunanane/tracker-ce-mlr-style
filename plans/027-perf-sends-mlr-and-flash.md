# Plan 027 — Dropouts with sends / MLR + flash room (owner 2026-11-07)

Owner on v0.4.4: "It is still easy to drop out the synth engine if sends are on. With MLR very easy to drop out."
"I'm confused about the KB limit — apparently we are still limited in new features at this point, right?"

## Dropouts
- Measure with armbench + host profiles: PolySynth (1 and 3 instances, chords) with reverb/delay sends on, and with
  MLR streaming loops playing. Find what costs: ShimmerReverb, delay, per-voice send paths, MLR read/resample/warp,
  SD streaming blocking the audio ISR, governor behaviour (does the voice cap come back?).
- Fix to be stable at the owner's case: sends on + MLR playing + PolySynth chords → no dropout; when CPU is short the
  governor must reduce voices gracefully and recover, never kill the engine.
- Keep -O2 on all hot paths (audio ISR, SD/platform, per-pass main loop). Behaviour identical / bit-exact where
  possible.

## Flash room
- The real limit: image end < 0xFC000 (1 MB chip minus the bootloader/config area). v0.4.4 ends at 0xF48B8 → ~30 KB.
- Reclaim room safely: -Os only for cold code (UI pages, menus, file browser, sample editor, config, help text) —
  never hot paths; dead stock code; duplicated tables. Target ≥ 60 KB free. Measure the CPU impact of every flag change
  (UI redraw rate must not drop; the v0.4 regression was -Os on hot paths).

## As built (branch v045-perf, from seq-tracks 7aa8a23, merged with seq-tracks 825e09e)
No device was available: all CPU numbers are armbench (unicorn, approximate M4 cycles, SDRAM stalls modelled as 20 cycles per random read / 10 per write, word bursts about 7 / 2.5 per word); the owner must confirm with the CPU monitor.

**Findings (what costs, ranked)**: with sends on the reverb and delay stop skipping their idle path and run every block. The Plinky ring reverb is 60 k cycles of compute (11.5 %) plus about 3 840 single uncached SDRAM accesses per block (about 61 k stall cycles) = ~23 % of the block, on top of the PolySynth (19 % for one instance, 40 % for three) and the short delay (~6 %). A streamed (MLR) voice reads its SDRAM ring twice per sample through a per-sample generic loop (~20 k+ cycles per voice). The governor itself is not the cause: it only cuts on a block >= 97 % and its tests (owner decisions of v0.4.3) pin that, so it was left unchanged; the fix is to make the load smaller so the 97 % line is not reached.

**Changes (all bit-identical output)**
| # | Change | Before -> after (armbench) | Test |
| :-: | :--- | :--- | :--- |
| 1 | Ring reverb on SRAM tiles (`ring_tiles.h`, `BasicRingReverb::tileChunk`): the dozen runs of consecutive ring cells the block touches are copied to SRAM with word bursts (scratch = the idle stock-reverb buffer `sramReverbBuff2`, no new RAM), the loop runs on them, changed runs are written back; chunks split before a shimmer-tap jump; pre-delay lines tiled too (nothing read when pre-delay is 0); plain loop kept as fallback (SplitRing, no scratch, odd pointer) | SDRAM accesses 3 840 -> 2 350 words in bursts; modelled 23.3 % -> ~17.5 % of the block (compute 11.5 % -> 15.5 % incl. copies) | `test_ring_tiles_equal_plain` (3 500 blocks x 3 pre-delay variants, parameters changing, shimmer jumps; output, ring memory and pre-delay memory identical), armbench hash identical |
| 2 | Short delay: read and write runs of the heard lines go through SRAM copies when time >= block + 4 | ~60 cycles/sample of SDRAM -> bursts, est. 5 k cycles (~1 %) | existing `test_cpu` delay-vs-frozen-reference tests (0 failed) |
| 3 | Streamed voice, loop forward, normal direction, no glide, window inside the valid ring, not in the cue cache or pre-cache, no loop wrap in the block: the block window is copied from the ring to SRAM (`VoiceStreamManager::copyRingRun`) and the register kernel runs on it | est. ~15 k cycles (~3 %) per streamed voice (two uncached reads and ~25 loads per sample gone) | not host-testable (needs SD memory); kernel itself covered by `test_kernel`; the condition list reproduces `readSampleAtRel` / `getSampleFast` exactly |
Not done: any governor change (see above), streamed fast path for backward / ping-pong / slice modes, wrap blocks.
Stack: `tileChunk` uses ~0.5 KB in the audio interrupt, the delay ~1 KB (two runs x two lines), static window 1 KB (RAM now 234 992 B, 89.64 %; merged plans 025/026 account for most of the rise).

**Flash** (image end, `make clean && make original`)
- v0.4.4 baseline 0xF48B8 (margin 30 536 B). seq-tracks 825e09e (plans 025+026): margin 17 984 B.
- This branch: image end **0xF3D68, margin 33 432 B** (+15.4 KB over 825e09e, with the tile code and the other changes included; ~ -26 KB of savings before them).
- Savings: font + logo RLE streams re-packed (literal blocks + runs; 39 036 -> 26 751 B, **-12.3 KB**, decoder 20 lines, `CompressedData` is now a byte); -Os for `mtSequencer_patternEdit` (-4.6 KB), `mtSequencer` (stock step note handling, -4.4 KB), `seqBeatRepeat`, `seqClips`, `MixerAndFxs` (-3.8 KB). Not touched (hot): synth, audio engine, play_memory, seqTracksCore, chordarp, VoiceStreamManager.
- **Target 60 KB free not reached** (33 KB). Remaining options, all behaviour changes: halve the random project-name word tables (14.7 KB), drop `-u _printf_float` (needs ~45 `%f` call sites converted, ~8 KB), compress or reduce the 35 KB PolySynth wavetable set (random access per sample, would need RAM), LTO (risky for hot paths).
- `check_sdram_ctors.sh` ok. Tests: `core/seqtest make test` 172 tests 0 failed checks, seqgen `make test` 0 failures.

**Owner on device**: CPU monitor (Config > Info) with PolySynth chords + reverb type Plinky + delay sends: "Reverb" stage and ISR avg/peak vs v0.4.4; MLR streaming loops: "Samples streamed" %, ring misses (must stay 0), cap behaviour. Listen for clicks at the Plinky reverb (shimmer jumps, pre-delay moving) and on streamed loops (any difference would be a bug: output is meant to be identical). If the reverb type is Stock, change 1 does not apply.
