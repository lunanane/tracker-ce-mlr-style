# Plan 013 — PolySynth pitch visualisation + tracker-key colour theme for the clip launcher (owner 2026-11-05)

## V — pitch trace view for PolySynth
Owner: a pitch visualisation mode for PolySynth mirroring the viral pitch-trace look; fully visualise pitch incl.
glide and LFO modulation; as a second curve around the pitch values, the ADSR, moving close to the pitch curves
(simply offset / multiplied from the pitch xy); sleek: lines and a clear background; tracker key colours.
- x = time, scrolling right-to-left (~4 s window), y = pitch on a light semitone grid (octave lines stronger,
  note names at the left edge). Each of the 4 voices is one thin line in its own tracker key colour
  (Note teal, Instrument yellow, FX1 pink, FX2 blue — the physical key colours of the original Tracker).
- The drawn pitch is the actual voice pitch: glide shows as slopes, LFO-to-pitch as wiggles (pitch sampled at
  control rate from the voice: base note + glide position + LFO pitch offset).
- ADSR: a second, dimmer curve per voice hugging its pitch line: y = pitch + k·envelope (offset above the line,
  distance proportional to the envelope level), so attack lifts it away, release lets it fall back onto the line.
- Plain dark background, no fills, thin lines; current values at the right edge (note name per voice).
- Where: a view mode of the PolySynth overlay (toggle with a clearly labelled key, no long press) and on the
  PolySynth instrument page. Must keep live playing possible (Play-mode overlay). Display list: polylines with
  decimated points (e.g. 64 points per voice) — host-check ≤ ~4 KB; refresh ~25 fps max, only while visible.
- Data: a small per-voice ring of (pitch, env) samples written at control rate by the engine (cheap), read on the
  main loop.

## C — clip launcher colour overhaul
Restyle the clip launcher grid (clipGrid / clipsLayer marks) with the same tracker-key palette and the sleek line
style: clean dark background, thin outlines, state by colour (playing / queued / play-1x / empty / scene column),
frame of the 8x4 pad window in a key colour; keep the display-list budget (planGrid).
