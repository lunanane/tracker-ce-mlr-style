# Plan 003 — First firmware release (v0.1, target 2026-11-05)

Owner goal (2026-11-04): a first firmware version pushed tomorrow; slow pace, with agents.
Order is sequential (one implementation agent at a time, lead reviews + builds between steps).

## Baseline
- `make original` builds on this PC (fix: ar response file, 348a5f7). Unchanged CE: FLASH 893,444 B of 1 MB
  (85.2 %, ~155 KB free), RAM 132,004 B of 256 KB, EXTERNAL_RAM 92.9 %.

## Scope v0.1 (in this order; anything not done by release moves to v0.2)
1. **A — Play mode + Live** (plan 001 P2): Pattern tap toggles Play mode; screen buttons 1–8 / Shift 9–16 jump to
   tracks; Instr + jog wheel type list (Off / Chord-Arp / Mono); pads drive seqgen; takeover while playing,
   pattern back instantly; next-bar type change while playing.
2. **B — Record** (plan 001 P3): Rec punch-in (replace), Shift + Rec one exact loop, one undo step; sidecar
   `seqtracks.bin` save/load with the project.
3. **C — Session view** (plan 002 S2): Pattern tap cycle Editor → Play → Session; 4 tracks × columns, jumps,
   two-pad loops, playhead recording with seqgen::Playhead; retrigger with sample offset. Cut cache (S3) → v0.2.
4. **D — Alt Performance page** (plan 004): Performance tap toggles; 4 horizontal 8-step faders, 8 function
   groups (stock perform FX, sends, ping-pong, reverb). In v0.1 only if solid in time.
5. **Release**: fork pushed to the owner's GitHub (PolyForm NC Required Notice kept, README section "Sequencer
   tracks mod" + on-device test list + recovery steps), tag `seq-v0.1`, `.ptf` built from the tag.

## Safety rules (every step)
- A project with no sequencer set must behave exactly like stock CE (no change to `.mtp`, pattern struct).
- No work in the audio callback beyond O(µs); no SD from audio context; no heap.
- `make original` and `make plus` must build with zero new warnings in touched files; seqgen `make test` green.
- Recovery documented: reset pinhole + known-good `.ptf` in /Firmware/.

## License (owner 2026-11-04)
Our add-in (seqgen/ and new glue files, each with a short header) is **PolyForm Noncommercial 1.0.0** (owner first
said CC BY-NC, then "Polyform is good"). Polyend relicensed CE upstream on 2026-11-03 (commit 9cc7aae) to MIT
with a "no wholesale commercial clones" restriction; edits inside Polyend files follow that. The release README
states both. Push target: https://github.com/lunanane/tracker-ce-mlr-style (its `main` = upstream 9cc7aae, three
commits ahead of our base 9b9a13b → merge mod/main into seq-tracks after step A, before the release build).
