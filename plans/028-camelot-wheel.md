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

## Scale flavour per ring (owner 2026-11-07: "go beyond minor and major scales … per sequencer selecting which of the
## minor/major type scales is used for either A or B")
Reference: the owner's Model:Samples firmware already does this (local clone E:\claude\custom-modelsamples, private —
read only, never copy its files into this public repo; reimplement). See mod/src/rc3e/rc3e_ref.py `CAM_TYPES`,
`cam_root_b`, `scale_decode`, and port/arp/ref.py `SCALES`. Its rule, which we keep:
- Camelot number c = 1..12, root of cB = (11 + 7 (c - 1)) mod 12 (8B = C), cA = relative minor (cB - 3 semitones).
- A scale type keeps the **Camelot number = the key signature** and moves the tonic: NATURAL (B major on cB / A natural
  minor on cA), DORIAN (+2), PHRYGIAN (+4), MIXOLYDIAN (+7), PENTA (B major penta on cB / A minor penta on cA),
  BLUES (minor blues on cA), HARM MINOR (on cA). So `8A D DOR`, `7A C MIX` — the wheel position and the match marks
  stay exact for modes. We add LYDIAN (+5) on the B side.
- New for the Tracker (owner): two per-track settings, **A type** (used whenever the track plays an A key: NATURAL,
  DORIAN, PHRYGIAN, PENTA, BLUES, HARM MINOR) and **B type** (whenever it plays a B key: NATURAL, LYDIAN, MIXOLYDIAN,
  PENTA), so jumping around the wheel keeps the chosen colour per side. Defaults NATURAL = today (bit-exact tests).
- Applies to every scale-using sequencer (Chord/Arp chords/arps built from the scale degrees, HR types, Strings, Mono,
  followers). Labels as Model:Samples' indicator: `8A Am`, `8A D DOR`, `5B Eb`.
- Saved per track (seqtracks.bin new version, oldest-version rule).
- Owner 2026-11-07: the A/B scale choice is NOT on the pads — only in the on-screen parameter page (jog / screen
  buttons). The pad layouts stay as they are.
