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

## Match indicator (owner 2026-11-07: "no matter if lead or follower we will know whether we are doing good matched")
- Every sequencer track, lead or follower, shows how its playing key/chord sits against the others on the wheel:
  **match** (same Camelot position), **good** (±1 or A/B switch — harmonic neighbour), **clash** (anything further),
  in tracker colours (green / amber / red is the provisional mapping) on the wheel, in the strip cell and in the
  parameter page. The reference is the lead's live key when there is a lead (plan 011), otherwise the other
  sequencer tracks that are sounding right now; a track compared with nothing shows no mark.
- Pure logic in a host-testable header (Camelot distance, mark per track), with tests.
