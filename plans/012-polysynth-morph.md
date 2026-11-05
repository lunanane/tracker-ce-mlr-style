# Plan 012 — PolySynth parameter snapshots per pattern / clip with a timed morph (owner 2026-11-05)

Owner: "It sounds absolutely fantastic, the PolySynth! I need it to save parameters per clip or pattern, and have them
morph with a fixed transition time when changing the pattern or clip — like a 6 second linear morph between what's
dialled in per pattern (or clip if triggered individually)."

## Behaviour (lead defaults, owner may adjust)
- **A snapshot per pattern**: the PolySynth parameters belong to the pattern they were dialled in on. Editing a
  parameter (instrument page or Play-mode overlay) writes it into the snapshot of the pattern currently playing
  (or shown, when stopped). A pattern without its own snapshot inherits — switching to it changes nothing.
- **Morph on change**: when the playing pattern changes (stock pattern switch, song advance, launcher scene, queued
  Song-page slot, follow action) every continuous parameter moves **linearly** from its current value to the new
  pattern's snapshot over the **morph time** (default **6 s**). Stepped parameters (voices, LFO target, waveform if
  stepped) switch at the start of the morph.
- **Clips**: a clip launched individually on a track whose instrument is PolySynth morphs to the snapshot of the
  clip's source pattern (the engine is shared: the last launch wins).
- **Morph time**: one setting per project, 0–30 s (0 = jump), in the PolySynth Voice section; saved with the project.
- During a morph, turning a parameter by hand takes that parameter out of the morph (hand wins) and writes the snapshot.
- Storage: snapshots in the PolySynth sidecar (plinky.bin, new version, older files load as "one snapshot for all");
  in RAM in EXTERNAL_RAM (255 patterns × params), not internal SRAM (83 % used). No SD writes while playing.
- Display: the overlay/instrument title shows "Morph 3.2 s → P12" while a morph runs; a small mark on parameters
  that differ from the inherited value.
