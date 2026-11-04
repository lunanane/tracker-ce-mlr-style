# Plan 004 — Alt Performance page: 8-step faders (owner 2026-11-04)

Status: APPROVED idea, layout provisional. Same concept as Play mode / Session view: on the Performance page,
**pressing Performance again** toggles between the stock Performance page and this fader page.

## Layout (12 × 4 pads) — revised by the owner 2026-11-04
```
        1  2  3  4  5  6  7  8 |  9     10   | 11     12
  r1    fader 1 (8 steps)      |  FX1-4  PING | LATCH1 RESET1
  r2    fader 2                |  FX5-8  REV  | LATCH2 RESET2
  r3    fader 3                |  FX9-12 —    | LATCH3 RESET3
  r4    fader 4                |  (gap)  —    | LATCH4 RESET4
```
- Columns 1–8: four **horizontal 8-step faders**, one per row; the lit pads show the value (bar from the left).
- Column 9: the three **stock perform groups** (slots 1–4, 5–8, 9–12); row 4 stays dark on purpose (owner: skip
  a pad so the send pages are visually distinct). Column 10: **PING** (ping-pong delay) and **REV** (reverb).
  The active group is lit.
- Columns 11–12 [provisional, owner did not specify]: LATCH (fader stays on release instead of snapping back
  like the stock momentary perform FX) and RESET (back to the neutral value).
- The 8 **buttons under the screen keep the stock meaning**: which tracks the perform FX / sends affect.
- No global volume / panning on this page (owner).

## Groups
| Group | Fader 1 | Fader 2 | Fader 3 | Fader 4 |
|---|---|---|---|---|
| FX1-4 | stock slot 1 | slot 2 | slot 3 | slot 4 |
| FX5-8 | slot 5 | slot 6 | slot 7 | slot 8 |
| FX9-12 | slot 9 | slot 10 | slot 11 | slot 12 |
| PING | **delay send** (selected tracks) | time (synced rate when sync is on) | feedback | filter |
| REV | **reverb send** (selected tracks) | size | damp | predelay |

Stock slots use whatever FX are chosen on the stock Performance page. Not on a fader yet [provisional]:
ping-pong on/off, sync on/off, reverb diffusion, return volumes — on the screen / jog wheel of the PING and REV
pages.

## Notes from the firmware
- Stock page: `mtProject.perfFxPlaces[12]` (which FX in each slot), `perfFxValues[12][4]` (4 preset values per
  slot = the pad rows); FX objects in `core/interface/performanceMode/performanceFx.cpp`.
- Delay: `delayFeedback`, `delayTime`, `delayParams` (bit 7 ping-pong, bit 6 sync, bits 0–5 sync rate),
  `delayFilterFrequency`; reverb: `mtProject.values.reverb {size, damp, predelay, diffusion}`; per-track
  `delaySend`, `reverbSend`; returns `delayVolume`, `reverbVolume`.
- Master values changed here are real project values (saved with the project like edits on the Master page)
  [provisional]; perform-FX faders are momentary unless LATCH, like the stock page.

## Release
Step D of plan 003: built after C (Session view); in v0.1 if it is solid in time, else v0.2.
