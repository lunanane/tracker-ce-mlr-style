# Plan 028 — Circle of fifths / Camelot wheel in the sequencer view and labels (owner 2026-11-07)

Owner: "For the sequencer visualization and also labels I want to reintroduce the circle of fifths / Camelot wheel and
label the keys as such when played."

Today: the Chord/Arp root pads are already the c31 Camelot grid (seqPlayLogic.h rootName, kCam: majors on rows 1+3,
minors on rows 2+4, columns along the circle of fifths), but the labels are plain note names (C, Am) and there is no
wheel on screen.

- **Labels**: keys/chords are named with their Camelot code plus the note name wherever a key or chord is shown
  (root pads legend, sequencer help/parameter page, harmony readout, pitch view note annotation): e.g. `8B C`,
  `8A Am`, `5A Cm`. One shared helper (pitch class + major/minor → Camelot number/letter), used everywhere.
- **Wheel**: a compact Camelot wheel (12 positions, outer ring B = major, inner ring A = minor) in the sequencer
  view: the playing key/chord root lit, the harmonic neighbours (±1 and A/B switch — the "safe" moves) marked, the
  last few roots as a short trail. Drawn with the existing primitives in tracker colours; its own display stage
  ≤ 3840 B (fifostub test).
- Followers' scale (from the lead, plan 011, later) can reuse the same wheel.
- Builds on plan 025 (sequencer parameter page) — start after it is merged. Flash: report the delta.
