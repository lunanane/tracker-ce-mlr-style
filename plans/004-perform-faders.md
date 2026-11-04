# Plan 004 — Alt Performance page: 8-step faders (owner 2026-11-04)

Status: APPROVED idea, layout provisional. Same concept as Play mode / Session view: on the Performance page,
**pressing Performance again** toggles between the stock Performance page and this fader page.

## Layout (12 × 4 pads)
```
        1  2  3  4  5  6  7  8 |  9    10  | 11     12
  r1    fader 1 (8 steps)      |  G1   G5  | LATCH1 RESET1
  r2    fader 2                |  G2   G6  | LATCH2 RESET2
  r3    fader 3                |  G3   G7  | LATCH3 RESET3
  r4    fader 4                |  G4   G8  | LATCH4 RESET4
```
- Columns 1–8: four **horizontal 8-step faders**, one per row; the lit pads show the value (bar from the left).
- Columns 9–10: **function groups** G1–G8 (owner: "two columns of switching function groups"); the active group
  is lit, the faders show that group's four parameters.
- Columns 11–12 [provisional, owner did not specify]: LATCH (fader stays on release instead of snapping back
  like the stock momentary perform FX) and RESET (back to the neutral value).
- The 8 **buttons under the screen keep the stock meaning**: which tracks the perform FX / sends affect.

## Groups
| Group | Four faders | Applies to |
|---|---|---|
| G1 | stock perform slots 1–4 (the FX chosen on the stock page) | selected tracks |
| G2 | stock perform slots 5–8 | selected tracks |
| G3 | **sends**: delay send, reverb send, volume, panning | selected tracks |
| G4 | **ping-pong delay**: time (synced rate when sync is on), feedback, filter, delay return volume | master |
| G5 | **reverb**: size, damp, predelay, diffusion | master |
| G6 | stock perform slots 9–12 | selected tracks |
| G7–G8 | free (later: return volumes / dry mix, ping-pong on/off + sync as toggle pads) | — |

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
