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

## As built (v0.4.5, branch v045-master; Original Tracker only)
**Beat repeat (audio).** `beat_repeat_core.h`: the slice ring is circular now. Command `C_PRE` (glue `seqBrPreroll`) records the master bus into it all the time while a beat
repeat page is shown (CUT layer "BeatRpt", or the Master BEAT RPT page); the stage is then never bypassed. A press starts the repeat at the next grid boundary with the grid cell that has
just ended (already in the ring): the first repeat sounds at most one grid after the press (before: capture a grid, then repeat: up to two). Less history than one cell (page just
opened, a repeat just ended, ring too small for the grid) falls back to the capture of plan 021, so nothing is garbage. Writes pause while a repeat reads the ring. With the page closed
(pre-roll off) behaviour is bit-identical to v0.4.4 (all old tests unchanged). The auto trigger uses the pre-roll too. **MIX level** (settings.mix, MIX mode only, ramped 1/256 per frame).
**Band-pass** on the repeats: `seqfx::FilterControl` / `bandpass` / `bwToQ` / `bandMix` of the delay (same range: centre 100..20000 Hz, >= 19000 = open = out of the path, width 4..36
twelfths of an octave), per-sample crossfade. **Master high-pass** (`MasterHp`, same SVF, `hpHz` index 13..90 = 60 Hz..5.1 kHz, resonance Q 0.707..5.7, bypass crossfade) runs in the same
stage wrapper after the repeats; off = the wrapper does not call it (zero cost).
**UI.** The "Faders" screen button (button 7) of Global Mixer 3/3 opens `masterFaders` (overlay like the amp view; the Master / other screens close it). Pad menu = column 9 rows 1-4:
REVERB, DELAY, HIGH-PASS, BEAT RPT (also screen buttons 1-4, "Next page" = 7, "Close" = 8, button 5 = beat repeat sub page REPEAT / AUTO / FILTER). REVERB / DELAY rows 2-4 are the Perform
REV / PING rows, code shared (`seqPerform/faderParams.cpp`, moved out of fSeqPerform.cpp unchanged); row 1 is the **return level** (not the Perform send: a send needs the performance
mode and the track selection, the Master page has neither). DISENGAGE (column 12, Shift = whole page) restores the value from before the fader touched it; ping-pong / sync pads as on
Perform (column 10 rows 3-4 of DELAY). HIGH-PASS: cutoff (pad 1..8, DISENGAGE = off), resonance. BEAT RPT: the CUT page controller (`seqbr::Ui`, same pads and the master menu in col 9);
new page FILTER on both (MIX LEVEL, BP CENTRE, BP WIDTH; CUT page: pad column 9 row 3). The screen is the Perform fader screen control (`faderView.cpp`, `master = 1`: four chips,
per-band LATCH / DISENGAGE words).
**Sidecar.** `seqbrpt.bin` (SBRP): v1 8 bytes, v2 (plan 026) 11 bytes, **v3 (this plan) 16 bytes** = v2 + MIX step, BP centre step, BP width step, HP cutoff step (0 = off), HP resonance
step. The oldest version that fits is written (v1 / v2 / v3); all load; a file newer than 3 is left alone. The master high-pass is saved there (project settings file, autosave after Stop).
**Tests.** `test_beatrepeat.cpp` + `test_br024.inc`: pre-roll timing sample exact at 97.3 / 120 / 168 BPM, content = the cell that ended, one grid sooner than without, ring wrap, short
history fallback, release and re-press, MIX level, band-pass (open / centre / 100 Hz rejected, width), no clicks, HP (-40 dB at a decade, -3 dB corner, flat pass band, resonance, crossfade
and inactive once off). `test_br.inc`: FILTER page, values, sidecar v1 / v2 / v3. `test_masterfaders.cpp` (new): pad map, values, LEDs, screen model, the real `faderView.cpp` in two
stages (worst 888 B / 1452 B of 3840; Perform view 888 / 832). Existing checks changed as direct consequences, equally strict: `test_br.inc` page pad count 2 -> 3, `mapPad(8,2)` is the FILTER
page pad, LED of that pad dim instead of off, "newer file" test uses version 4 (3 was plan 026's), `test_cut.inc` got a `seqBrPreroll` stand-in. Previews: `make master-preview`
(`mf_*.png` in core/seqtest/build/preview).
**Not done / not device tested.** Everything on hardware (listening, pad feel, CPU readouts, the FT81x frame with the overlay); HP slope is fixed 12 dB/oct; jog on the Master fader pages
does nothing (not in the spec); the track send of the Perform pages is not on the Master pages.
