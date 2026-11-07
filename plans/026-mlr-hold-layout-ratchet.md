# Plan 026 — MLR: deliberate engage, slot layout matching the pads, jog quantise + ratchet (owner 2026-11-07)

Owner: "The MLR often triggers the loop accidentally, so maybe we need a bit of a long press for two buttons to engage
it. Also the settings on screen for each slot don't match the 4x2 layout of the buttons, so it's confusing; adapt the
layout to match the buttons. Also I'd like to use the jog wheel to dial in quantisation and, based on it, bring in a
ratchet when holding at a position."

- **Engage**: the accidental trigger is the one the owner means — find which pad/button press starts a loop by
  accident (the two-button combo that engages MLR / a slot) and require both held ~0.3–0.4 s to engage, with an
  on-screen progress/label ("hold both"). Owner exception to the no-long-press rule, only for this engage. Normal pad
  jamming inside a running loop stays instant.
- **Slot settings layout**: the on-screen per-slot settings are drawn in the same 4x2 arrangement as the physical
  buttons that change them, so each setting sits where its button is.
- **Jog quantise**: on the MLR page the jog wheel dials the jump quantisation (off, 1/32 … 1 bar, shown on screen).
- **Ratchet**: holding a step pad retriggers that position at the quantise rate (ratchet) while held; release returns
  to normal playback. Off when quantise is off. Stop = panic.
- Saved with the project (seqclips/seqwarp or the MLR sidecar, oldest-version rule).

Limits: flash ~30 KB left — report the size delta; display stages ≤ 3840 B.
