# Plan 020 — Play-mode info: help legend hint, note annotation, pitch view, harmony readout (owner 2026-11-06)

Owner: "We need legends for how to bring up the sequencer help in Play mode, and let's show note annotation similar to
the pitch visualisation; if a synth is selected, show the pitch visualisation together with the descriptions; also the
root note, scale, playing note and chord type should show."

- **How-to legend, always visible in Play mode** (no hidden gestures): a compact key line, e.g.
  `tap track: help · hold track + jog: type · Instr: synth · Rec: take` — placed where it never covers the pattern's
  playhead row, shortened to fit; the selected track's strip cell keeps `tap: help`.
- **Note annotation band** (extends the v0.3.3 note band): the notes the selected track's sequencer plays shown like the
  pitch view — note names on a small pitch axis with short trails (recent notes), current notes bright.
- **Synth selected** (track instrument = PolySynth): show a compact pitch view (the real voice pitches incl. glide/LFO)
  next to/above the descriptions, in the Play-mode screen (not only behind the Instr key).
- **Harmony readout**: root note, scale, currently playing note(s), and the chord type (from the sequencer's state —
  Chord/Arp type name, HR Chord/Arp chord, Strings root/scale; or detected from the sounding notes: maj/min/7/sus/…),
  e.g. `Root D#  minor  ·  playing D# G A#  ·  Dm/D# min triad`.
- Budget: Play mode shows the pattern view; these additions must fit the frame budget (or switch the pattern to a
  compact view while the info is shown — decide with measurements).
Depends on v0.3.9 (pitch view device fix) — start after it is merged.
