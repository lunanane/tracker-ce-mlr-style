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

## As built (v0.2.3)
**Song playback.** `mtSequencer.cpp` has three small hooks, all in the sequencer interrupt and all a flag lookup (`seqSong.cpp`, pure rule in
`seqSongLogic.h` `seqsong::advance`): (1) at the last step, where the stock song loads the next pattern, a slot that stays loads nothing;
(2) at the pattern end the song branch does no switch (`isNextPatternAvailable = true`, so no song end) and the pattern wraps like a pattern loop;
(3) the armed scene switch no longer clears `songMode` (the song mode goes on), and `switchRamPatternsNow` drops it only when the new pattern is
not in the song at all. The answer of hook 1 is latched (`seqsong::Latch`) so the pattern end never switches to a buffer nobody loaded when the
flag flips in between. Stay = not flagged, or flagged on the **last** slot (the song never ends); a pattern break effect and an audio export of the
song (`onSongEnd == cbkSetCurrentExporterOnLastStep`) keep the stock advance. The flag looked at is the one of the loaded pattern
(`values.actualPattern`).

**Scene flag storage.** `seqclips.bin` version 2 = the 1020 byte clip-mode table + 32 bytes (1 bit per pattern, bit 255 unused). Version 1 files
load with every flag = Loop and are rewritten as version 2 at the next save; a newer version is left alone as before. `ClipModes::playNext /
setPlayNext`, `seqClipPlayNext / seqClipSetPlayNext`; the flag is part of the dirty/hash logic (no SD write while playing).

**Setting the flag.** Launcher: Shift + hold screen button 1 + wheel now writes the **scene flag** of the focused row only (before: the mode of every
clip in the row; per-clip modes stay on button 1 + wheel without Shift). Song page: screen button 1 ("play pattern") held + wheel sets the flag of
the selected slot's pattern; a plain tap of button 1 still does the stock loop / song toggle, now when the button is released (a wheel turn while
held cancels the tap). Marks: an arrow at the right end of the slot line (`localList_t::slotMarks`, drawn by `cSongPlayer::drawIcon`: shapes only)
and a bar along the top of the launcher's scene cell + the line `Scene P: PLAY NEXT / LOOP` in the info column.

**Queue on the Song page.** The press is **Insert** (unshifted; it had no plain function there, Shift + Insert = Home is kept). I did not take
Play (it stops), button 2 (stock "play song from slot", it stops when playing) or the pads (they play notes). While the song plays, select a slot
and press Insert: `seqSongQueueSlot` uses the existing scene mechanism (`seqSessionLaunchScene`: `sequencialSwitch_*`, the pattern is loaded by the
file manager's async load at the last step, nothing blocking). The queued slot (red square) is remembered and wins over the stock search in
`switchRamPatternsNow` (`seqsong::slotAfterSwitch`), so a pattern that sits in several slots goes to the slot you picked. Insert on the playing slot
cancels a pending queue, else nothing; a later scene pad / the Pattern page / Stop drops it (the square shows only while the stock switch is armed
for that slot's pattern). Moving the cursor while playing needs the red edit mode (Rec), as in stock.

**LIVE / BACK TO SONG.** Removed. With the song looping its slot, a launch has nothing to freeze: clips live until the pattern switches (scene pad,
queued slot, play-next). `cbkLive`, the freeze requests, `seqSessionLive/LiveSlot/BackToSong`, the info line and the button 4 label are gone;
`seqsession::LiveState` stays in `seqSessionLogic.h` because its host tests stay (unused by the firmware; the `View.live` field is kept for the
struct layout). Play 1x follow actions now also run in song mode while the slot loops. Stop = panic still clears the queue, the last-step answer and
everything of plan 005; the performance page's "reload at pattern end" counts a looping slot as no song.

**Tests.** `test_mod.cpp`: `test_scene_flags` (flag set / clear / bounds / byte edges, version 2 round trip, v1 file loads with all Loop, wrong
length, bit 255, newer version, bad CRC), `test_song_advance` (advance decision incl. last slot / break / export, latch, queue op, queued shown,
slot after switch). One existing check of `test_clip_modes` (reserved mode values read back as Loop) builds a **version 1** frame by hand: it now writes it with
`kVersionV1` and its length expectation `== kFileMax` reads `== kPayload + kOverhead` (the v1 frame size). A direct consequence of the version
bump; same frame, same remaining expectations (and v1 loading is now tested explicitly).
