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
