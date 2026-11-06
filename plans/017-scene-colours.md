# Plan 017 — Scene colours (owner 2026-11-06)

Owner: "The clip launcher — can we add a function to change the colour per scene in the stock Song mode when arranging the clips, and
then the colours show up on the clip launcher. For the colours use the tracker colours and a couple of fitting extra ones."

## Scope
- Palette (`seqPalette.h`): None + the four tracker key colours (Note teal, Instrument yellow, FX1 purple, FX2 blue) + four extras
  (orange, coral red, green, white). Index order is stored in files: only append.
- A colour per **pattern** (= scene = launcher row), stored in `seqclips.bin` (version 3), old files load plain.
- Set on the stock Song page without a long press; shown on the Song page slot list and on the launcher.
- Launcher: scene cell + row tint, playing / queued states stay readable, `planGrid` / `marksCost` budgets kept.
- Out of scope: per-clip colours, colours on other pages, MLR / warp (other agents), auto layout.

## As built (v0.3.8)
**Palette.** `seqpal::kSceneRgb[9]` = none (0), Note 0x00E8BE, Instrument 0xFEF749, FX1 0xBB58F1, FX2 0x57F1FF, orange 0xFF8A30,
red 0xFF4D6A, green 0x6EE06A, white 0xE6E6E6 (all clearly brighter than the 0x050505 ground). `sceneRgb / sceneName / sceneStep`
(wraps through None, sign of the wheel value = one step), `sceneCellFill(idx, cls)` (colour mixed into the dark scene ground at 56 / 110 / 190 of
256 for empty / content / playing), `sceneClipFill(idx)` (64 of 256 into the grey content fill).

**Storage.** `seqclips.bin` version 3 = 1020 byte clip modes + 32 byte scene flags + **128 bytes of colours** (4 bits per pattern, low nibble =
the odd pattern number; the unused nibble of pattern 256 is dropped on load). Version 1 and 2 files still load (flags / colours default),
and are written as version 3 at the next save; a newer version is left alone as before. `ClipModes::color / setColor`, `seqClipSceneColor /
seqClipSetSceneColor` (a stored value outside this palette reads as None; the table keeps the number). The colour is part of the hash, the dirty
gate and `anyNonDefault`, so a project that only has colours still writes the sidecar; nothing is written while the transport plays (same
deferred path as the play-next flag). `seqClipsLogic.h` kFileMax grew to 1180 + overhead; the sidecar io buffer is derived from it.
The table sits in the existing OCRAM bss `ClipModes` object (+128 bytes, no new object, no constructor in EXTERNAL_RAM).

**Setting it (Song page).** The Song page has all eight screen buttons in use, so the colour rides on the plan 006 gesture: **hold screen
button 1 (play pattern) + Shift + wheel** steps the colour of the selected slot's pattern (button 1 + wheel without Shift is still the play-next
flag; a plain tap and every stock key are untouched). While button 1 is held its label reads `Wheel:Next` / `Shift:Color`; with Shift down
or after a colour step it reads `Color:` + the colour name; on release the stock label returns (`holdKey`, `cSongEditor::refreshButtonsLabels`).
No new hotkey, no long press. The slot list shows a **colour chip** (18 x 16 px in the colour, on a list-coloured patch) left of the play-next /
queue marks: `localList_t::slotMarks` now carries the colour in bits 4-7 (`seqsong::slotMarks / markFlags / markColor`), drawn in
`cSongPlayer::drawIcon`; the old flag bits and the patch behind them are unchanged.

**Launcher.** `clipgrid::View::sceneColor[12]`, filled from `seqClipSceneColor(pattern)` in `ClipsLayer::build` (the View is compared with
memcmp, so a colour change redraws the grid). `cSeqClipGrid::update`: per colour in use one batch for the row's clips with content
(tinted grey-towards-colour), three for the scene cell (empty / content / playing), one for a 2 px line under the whole row (in the gap between the
rows); those clips and scene cells are left out of the grey batches (same rect count). Empty and **playing clips keep their state fills**, the
marks control (teal outline + progress, blinking yellow queue outline, song row bar) is unchanged, so playing / queued stay readable. None =
today's look, byte for byte the same draw ops.

**Budgets.** `clipgrid::sceneColorCost(View)` = 5 batches per colour in use + one rect per coloured row (counted in the mandatory part of
`planGrid`, before the optional parts). Worst case 12 rows in 8 colours: +736 bytes; with the existing worst case (every optional part lit)
the plan is 5132 of 5500 bytes (before: 5124), marks 1196 of 1600 unchanged. Host numbers printed by `test_trace`.

**Preview.** `make -C core/seqtest grid-preview` compiles the real `clipGrid.cpp` against stubbed display list macros (`gridstub/`,
`preview_grid.cpp`) and writes `build/preview/grid_plain.png` / `grid_colors.png` (needs Pillow; `grid_preview.py`).

**Tests.** `test_mod.cpp` `test_scene_colors`: accessor bounds / nibble neighbours / independence from modes and flags, round trip, bad CRC,
version 1 and 2 files load with no colours (also over set colours), re-save as version 3, unused nibble, later-palette values, newer version, palette
(distinct, bright enough, distinct fills, names), stepping (wrap both ways, any value = one step), slot-mark packing. `test_trace.cpp`
`test_launcher_budgets`: colours in the budget (12 rows x 8 colours, exact cost, no cost without colours, mandatory part leaves the optional parts room).
**Test changes (consequences of version 3, equally strict).** `test_scene_flags`: version expectations `kVersion == 2` -> `== 3` (+ `kVersionV2`, `kPayloadV3`),
saved frame byte `buf[4] == 2` -> `3` (three places), the two hand-built "version 2" frames now use `kVersionV2` (they are version 2 files; their
length expectation `kFileMax` -> `kPayloadV2 + kOverhead`), the refusal case "version 2 frame with a v1 length" now says version 3 and one more
refusal (version 3 with the v2 length) was added. `test_session.cpp` `sizeof(View)` expectation `+ 2 * kVisRows` -> `+ 3 * kVisRows` (the new byte array, still
"no padding"). Nothing was removed or loosened.

**Not verified on a device.** The label text fit on screen button 1, the chip over long pattern names, the look of the tints on the real panel,
the hold / Shift / wheel gesture feel.
