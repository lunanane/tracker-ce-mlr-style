# Plan 006 — Live-set Song page: loop by default (owner 2026-11-04, after the v0.2 hardware test)

Owner: "By default we should always be in loop mode in the Song page and only when scenes or individual clips
deliberately have play next selected they should do so. We are optimizing for live sets."

## Behaviour
- **Song playback loops the current slot by default.** At the pattern end the song does not advance; the slot plays
  again. A slot advances to the next slot only when its scene is flagged **play next** (= Play 1x for the scene).
- Individual clips keep their per-clip Loop / Play 1x (plan 005 R2, follow action = next row down).
- **Moving on during the set (owner: both):**
  - Clip launcher: scene pads (column 10) as now.
  - Song page: pick a slot with arrows / jog and press it (provisional: the slot's pad / Enter-equivalent) to **queue**
    it; it switches at the pattern end (quantised like a scene launch). The queued slot is shown on the Song page.
- **Setting play next (owner: both places, same flag, shown on both screens):**
  - Launcher: Shift + hold screen button 1 + wheel on the row (as built in v0.2: today it writes per-clip modes —
    the scene flag becomes its own per-pattern value).
  - Song page: hold screen button 1 + wheel on the selected slot.
  - Marker on the Song page slot list and on the launcher scene column.
- Storage: per-pattern scene flag in seqclips.bin (new version; v0.2 files still load, flags default to Loop).
- Stock behaviour that stays: Song page layout, editing the playlist, Stop = panic, LIVE/BACK TO SONG (re-check:
  with loop-by-default, "LIVE" freeze may become unnecessary — investigation decides and reports).

## Open (provisional defaults)
- Song end: a play-next flag on the last slot → loops that slot (no stop).
- The stock "play song from selected slot" key combo keeps working, then follows the loop/play-next rule.
