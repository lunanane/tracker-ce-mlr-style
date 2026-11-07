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

## As built (branch v045-camelot, merged with seq-tracks at plan 027)

- **Helper** `seqgen/camelot.h` (+ out-of-line code in `chordarp.cpp`, one copy): wheel position of a key, the scale flavours, `resolve(pad key, A type, B type)`,
  `label` ("8A Am", "8A D DOR", "5B Eb"), `markOf` (MATCH same position, GOOD number +-1 on the same side or the other side of the same number, CLASH
  else; 12 and 1 are neighbours) and `marks()` (per track against the others that sound, worst relation; a lead index is a parameter, `Engine::leadTrack_`
  is 0xFF until plan 011 exists). Rule as the Model:Samples firmware (reimplemented, nothing copied): root of cB = (11 + 7 (c - 1)) mod 12, cA = cB - 3,
  a type keeps the number and moves the tonic (+2 Dorian, +4 Phrygian, +5 Lydian, +7 Mixolydian). A types: Natural, Dorian, Phrygian, Penta, Blues, Harm min;
  B types: Natural, Lydian, Mixolyd, Penta.
- **Per-track A / B type** (engine setting, table cells "A type" / "B type", default Natural = bit-exact; all earlier tests pass): Chord/Arp (scale degrees,
  lock key, chord pool), Mono, HR Arp Lines, Chord Stabs, HR Chord/Arp (key lock), HR Bass, Step Follow. A publisher (Stabs, HR Chord/Arp) puts its key and
  types on the HrBus; a follower (Bass, Step Follow, Arp Lines follower, Mono) with Natural takes the leader's flavour, with another type it plays its own
  over the same key. Not in the table: Strings (its own Scale list already holds every flavour; its key is shown as a Camelot position), Arp Seq, Euclid,
  Grid Seq (no scale). Pads are unchanged; the choice is only on the page. Chord/Arp page rows are now Play 7, Key 5, Shape 7, Groove 6 (25 cells).
- **Labels**: Chord/Arp and HR Chord/Arp root pad legend ("12A C#m"; 7 characters), HR Stabs / Arp Seq / HR Chord/Arp titles, parameter page title, legend
  caption. The strip cell of a non-selected sounding track shows "key mark" (= match, ~ neighbour, x clash) in the match colour.
- **Wheel**: its own control (`cSeqPlayLegend` mode WHEEL, bottom right above the note band, shown with the band): outer ring B, inner ring A, selected key
  lit in the match colour, neighbours outlined, other sounding tracks as dots, last 3 keys as a trail. Worst stage 904 FIFO bytes (limit 3840).
  The one colour table is `seqwheel::markColor` (green / amber / red, provisional).
- **seqtracks.bin v4**: v3 layout + one byte (A | B << 4) per track; written only when a scale-using track has a non-Natural type, otherwise the oldest
  version; v1..v3 load; cut file / bad CRC refused.
- **Tests** (`core/seqtest/test_camelot.inc`): all 24 keys x all types against the written-out Model:Samples rule, labels, marks (incl. lead), types in
  Chord/Arp, Mono, HR Stabs / Bass / HR Chord/Arp, engine params, v4 round trip, wheel stage. Updated expectations (consequences of the plan, same strictness):
  Chord/Arp cursor rows in `test_params.inc`; the root pad label "12A C#m" and the cell text limit 6 -> 7 in `test_play.cpp` / `seqgen/test/test_hr.inc`.
- **Numbers**: image end 0xF96CC (1,021,644 B) vs seq-tracks 0xF81C4 (1,016,260 B): +5,384 B flash, margin 10,548 B; no static constructors. Host tests 0 failures.
- Previews: `core/seqtest/build/preview/legend_{chordarp,hr_chordarp,wheel,...}.png` (`make legend-preview`).
- Open: not run on a device; the wheel position and 7-character cell labels need a look on hardware; HR Chord/Arp root grid row 3 has a duplicate note (63) from the
  original table (not changed).
