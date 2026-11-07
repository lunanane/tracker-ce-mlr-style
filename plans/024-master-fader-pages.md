# Plan 024 — Master page as pad fader pages + beat repeat fixes (owner feedback on v0.4.4, 2026-11-07)

Owner: "Beat repeat works but takes too long to buffer, so we need a buffer that runs once the user is on its page.
The mix-in volume needs to be adjustable and I'd like some band-pass functionality like in the delay.
Master page: I like to use the pads better, so bring fader pages there. We can also use beat repeat, delay and
reverb settings from there via faders; also introduce a high-pass filter section for the master. The pages are
navigable via a screen button that toggles between them and a pad menu similar to the Perform page: reverb and delay
exactly (as on Perform), and high-pass and beat repeat next to it as two pages."

## Beat repeat
- **Pre-roll**: the capture buffer runs while the beat-repeat pad page (CUT/MLR toggle or the new master page) is
  open, so the first press has audio immediately. Leaving the page stops the capture again (off = zero cost) unless a
  repeat is latched. Measure the capture cost with the CPU monitor numbers.
- **Mix level** fader (0–100 %, default as now) for the Mix mode; Insert keeps replacing.
- **Band-pass** on the repeats like the delay's filter (same filter code and parameter range as the delay band-pass,
  reuse it, not a copy): low cut + high cut, or centre + width — whichever the delay uses.
- Sidecar: new fields go in a new seqbrpt version; write the oldest version that fits, keep loading older ones.

## Master page fader pages
- Same horizontal 8-step pad faders and page pads as the Perform page (plans 004/005 R3; reuse its code).
- Pages: **Reverb**, **Delay** (exactly the Perform page's reverb and delay pages, same parameters),
  **High-pass** (new master HP: cutoff, resonance, slope or mix — keep simple, 2-pole SVF/biquad, bypassed at 0 = zero
  cost), **Beat repeat** (grid / gate / decay / pitch / mix / band-pass faders).
- Navigation: a clearly labelled screen button toggles between the pages, plus page pads in the pad menu like Perform.
  No long-press, no hidden gestures; on-screen labels say how.
- Existing jog/screen-button master editing keeps working.
- Master HP is saved with the project (master params or a sidecar, oldest-version rule).

## Limits
Flash ~30 KB left (image end must stay < 0xFC000) — report the size delta. Display stages ≤ 3840 B (fifostub tests).
