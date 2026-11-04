# Plan 008 — usable sequencers, recording and MLR (owner 2026-11-04, after the v0.2.x hardware tests)

Owner: "none of the sequencer scripts is usable; the Shift tutorial only shows unknown three-letter descriptions;
the chord sequencer is not a good translation of grid composer; Mono is not close either; we completely lack the
HYPER_RASP sequencers." Recording: "Rec just doesn't do its job — it only works for a millisecond, and a new
recording overwrites the old one completely." MLR: "unusable so far — I can only retrigger a position; record holds
only ~10 ms, no sequence recording possible, no quantisation snapping."

## A — recording and MLR (bugs first, highest priority)
- **Play-mode Rec**: Rec is a toggle (press = take starts, press again = take ends; no holding). During a take the
  steps that pass are replaced by what the sequencer plays (punch region); steps outside the take stay untouched — a
  new take never wipes the whole track. Shift+Rec: exactly one loop (loop start → loop end), as planned in plan 001.
  Find why takes last ~1 ms on hardware (button event flow, immediate release, take ended by the router/panic/
  takeover logic, …) — reproduce in a host test of the real event sequence first.
- **MLR**: jumps, two-pad loops, reverse, rate must work as in plan 002 (owner can only retrigger a position).
  Playhead recording: Rec toggles a take (not hold); the gestures played during the take loop on as a sequence after
  the take ends, in any group. Same ~10 ms root cause likely. **Quantisation**: pad jumps snap to a grid (off / step /
  beat / bar, default beat), set on the MLR screen (screen button, shown on screen).
- Host tests that model the real hardware event sequence (press/hold/release timing as the keys driver sends it).

## B — flash room
Owner: build all non-audio stock code with -Os on MK66 (screens, file manager, display lib, …); audio path and
sequencer timing code stay -O2. Report the freed space and the image end (< 0xF0000).

## C — grid composer c34, faithful (owner priority 1 and 2)
Rework Chord/Arp (c34 page 3 without arranger) and Mono (c34 page 4) so they behave like c34 on the iii grid
(E:\claude\iii-grid-composer\monome_iii_c34.lua is the reference; the seqgen port + lua_compare exist — find where
behaviour and especially the pad layout / interaction diverge from c34 on a 16x8 grid vs the Tracker's 12x4 pads, and
choose a mapping that keeps c34's playing feel). The Shift legend must use real words (e.g. "Root", "Chord type",
"Octave", "Hold"), not three-letter codes; the legend explains how to play the script.

## B as built (branch v025-seq)
`core/makefile`: on MK66 every non-audio stock object is built with `-Os`: `interface/*` (all screens, the Play legend included),
`FileManager/*`, `Exporter/*`, `libs/NewDisplay`, `libs/fonts`, `libs/images`, `libs/FT812`, and the UI helpers `source/FileBrowser`,
`ThemeManager`, `Themes`, `mtRandomNameGenerator`, `mtGridEditor`, `mtConfig`, `mtSliceDetector`, `mtSliceManager`. Kept `-O2`:
`AudioEngine/`, `libs/Audio/`, `effector/`, `synth/`, `seqgen/`, all `source/mtSequencer*` and `seq*` (sequencer timing, cut, launcher),
`mtMidi`, `mtPadBoard`/`mtKeyboardManager`/`mtPadsBacklight`/`mtActions` (input), and the three UI files that hold `AUDIO_CORE_RAM`
functions (`interface/core/songTimer`, `interface/performanceMode/fPerformance`, `FileManager/fileManagerSong`). Checked by grep
(`AUDIO_CORE_RAM`, `FASTRUN`, `IntervalTimer`, `attachInterrupt`, `DMAMEM`, `section(`, `IRQ`): nothing else timing-critical in the `-Os` set.
Result (MK66 ORIGINAL, `arm-none-eabi-size` text / last data address of the `.ptf` hex):

| | before | after | freed |
|---|---|---|---|
| text | 976056 B | 938768 B | **37288 B (36.4 KB)** |
| image end | 0xEE4B8 | 0xE5310 | 0xF0000 limit: 44272 B (was 6984 B) of room |

## C as built — Chord/Arp and Mono pads (12x4 mapping of c34)
What was wrong in the first port, found by playing c34 on paper against the pads: (1) c34 shows **everything at once** (roots, chord
types, type/speed/direction/size, groove, velocity, swing, lanes, inversion, octave) and you play with both hands; the port hid chord
types, speed/direction and groove behind four panels you had to switch to, so changing the chord shape and the arp speed took
panel changes in between; (2) the c34 "hold pad 1 + pad N" gesture for the 7 extended chord types was replaced by a 15-pad panel,
(3) swing, gate length, velocity pattern, gate lane, octave range, play mode / lock key, transpose, mute and the "off" type existed only
as engine params "for a screen/encoder" that the firmware never offers: **unreachable on the device**, (4) Mono had blind +/- pads for
speed, velocity and page (no way to see the value; c34 has 12 / 16 / 8 direct pads in its menu), Clear next to Half, (5) the legend
said TYP SET GRV PRM / I+ O- / HLF CLR LP. The new map keeps c34's regions and gestures; what does not fit lives one pad-press away.

Pads are `(col,row)`, row 0 = top; the root grid is on every page. Full per-pad table: `seqgen/README.md`, "Pad layouts".

**Chord/Arp** (cols 6-11 show one page; `Page` pad `(11,0)` on Play, tab column `(11,0..3)` on the others)

| Pad(s) | Page | Function | c34 region |
|---|---|---|---|
| cols 0-5 × rows 0-3 | all | 24 roots (rows 0/2 major, 1/3 minor) held = chord/arp; Grid mode = scale degrees | x1-4, y1-6 |
| cols 6-7 × rows 0-3 | Play | chord types Root Sus2 Triad Sus4 / 5th 6th 7th 9th; hold Root + type = Dom7 Maj7 Dim Aug 7b5 7#5 b9 (as in c34, pressing Root selects type 1 at once; checked against the Lua by lua_compare) | x1-4, y7-8 (mod_held) |
| (8..10,0) | Play | Chord / Arp / ArpOct (queued to the bar) | x5-7, y1 |
| (8..11,1) | Play | speed 1/8 1/8T 1/16 1/16T, two fingers = the 6 extra speeds | x5-8, y2 |
| (8..11,2) | Play | direction Up Down UpDn Zigzag, two fingers = the 6 extra | x5-8, y3 |
| (8,3) (9,3) | Play | Inv- Inv+, both = reset | x15, y5-7 |
| (10,3) (11,3) | Play | Oct- Oct+, both = 0 | x16, y5-7 |
| cols 6-9 × rows 0-3 | Groove | 16 grooves, with Param on: tier weights + archetype | x5-8, y5-8 |
| (10,0) (10,1) | Groove | Param on/off, Gate normal/short/legato | (15,8) (14,8) |
| (10,2) (10,3) | Groove | Swing - / + | x13-16, y3 |
| cols 6-9 × rows 0-1 | Feel | velocity pattern 1..8, second finger = blend | x13-16, y1-2 |
| cols 6-9 × row 2 | Feel | gate lane 1..4, again = off, second finger = both | x13-16, y4 |
| cols 6-9 × row 3 | Feel | arp octave range 1..4 | x16 centre held + up/down |
| (10,0..3) | Feel | chord size 5 / 4 / 3 / 2 | x5-8, y4 |
| (6..8,0) | Setup | Key / Locked (hold + tap a root = lock key) / Grid | x14, y5-7 |
| (6,1) (7,1) (8,1) | Setup | Tran- Tran+ Tran 0 | page 1 arp row |
| (6,2) (7,2) | Setup | Mute, Off (type 4, from the next bar) | page 1 mute, x8 y1 |
| (11,0..3) | edit pages | tabs Play / Groove / Feel / Setup | - |

**Mono** (NOTE view; Menu `(11,3)` switches to the MENU view and back)

| Pad(s) | View | Function | c34 region |
|---|---|---|---|
| cols 0-7 × rows 0-3 | Notes | 8 steps × 4 degrees, tap = note on / off | x1-16, y1-7 |
| (8,0) (8,1) | Notes, Menu (10,1) (11,1) | Window up / down | menu (15,5) (15,6) |
| (8,2) | Notes | Half: steps 1-8 / 9-16 | - (16 wide) |
| (8,3) | Notes | Loop: hold, tap first and last step | y8 loop row |
| (9,0) (9,1) | Notes | Page - / + | row 6 pages |
| (9,2) | Notes, Menu (8,1) | Tie | (9,6) |
| (10,0) (10,1) | Notes | Slower / Faster | menu row 1 |
| (10,2) (11,2) | Notes | Velocity pattern - / + | menu row 7 |
| row 0, cols 0-11 | Menu | speed 1/1 … 1/64T, direct | menu row 1 |
| row 1, cols 0-7 | Menu | edit page (tap), hold Pg 1 + Pg N = N pages | row 6 |
| row 2, cols 0-11 | Menu | velocity pattern 1..12 (9..12 = 1..4 blended with 1) | row 7 |
| (0,3) (1,3) (2,3) | Menu | Tran- Tran+ Mute | performance page |
| (7,3) | Menu | Clear all | (14,6) |

**Shift legend** (Play mode): real words, ≤ 6 characters per cell, plus a two-line caption: line 1 = `CHORD/ARP  Page: [Play] Groove Feel Setup`
(active page in brackets) or `MONO  Notes 1-8  Page 1/2  Speed 1/16  Vel 1  Tie off  Window +0`; line 2 = one sentence on how to play the
shown page. The page tab / current speed / page / velocity / tie are lit white. Colour zones: green notes and steps, cyan selectors,
orange page / hold, yellow +/-, red one-shot actions.

**Diverges from c34** (and why / how it is handled): 8 steps × 4 degrees visible instead of 16 × 7 (Half + Window scroll; the loop
may still cover 16 steps); Oct is Oct-/Oct+ stepping -1..+1 instead of a 3-pad radio with a centre (both held = centre); the c34
"hold octave centre + tap" range is four direct pads on Feel; no arranger / run-stop (plan 001: the transport is the Tracker's);
Mono has one instance per track (no S1..S3 select); the "Off" type moved to Setup so the Play type row keeps room for the Page pad;
Mono Clear is menu-only (destructive). Engine behaviour is unchanged and compared against the real c34 Lua through the pads:
`make lua-compare` 43/43 scripts identical, fuzz seeds 1-5 × 100 = 515 scripts, 0 mismatches (the known `n%0` in c34 `c_arp`).

**Tests**: `seqgen/test/test_pads.inc` (17 tests: every Chord/Arp page, tab/page-pad flow, release-on-the-page-of-the-press, extended
type gesture, pair / blend / lane gestures, lock gesture, Mono note + menu views, page-pad gesture, view switch ending gestures,
LEDs), `core/seqtest/test_play.cpp` (legend words, <= 6 characters, no old codes, titles with the active page / view, captions).
Consequential expectation changes (same checks, new pad positions): `seqgen/test/test_main.cpp` Rig helpers, `test_cases2/3.inc` pad
coordinates, `core/seqtest/test_main.cpp` pad numbers (triad pad 30, Mono clear via the menu, Mono HALF pad 32, page tab pad 11),
`core/seqtest/test_play.cpp` legend texts (old three-letter codes replaced by the words above).

## D — HYPER_RASP sequencers (owner priority 3 then 4)
Port from E:\claude\_HYPER_RASP\scripts (8x4 pad scripts — map to the Tracker's 12x4 with 4 extra columns):
3. arp_lines, arp_seq, chord_arp, chord_stabs; 4. bassline_seq, euclid_trigger, grid_seq, step_follower.
Prefer porting HYPER_RASP's pad-script API once (C++) and the scripts onto it 1:1, with differential tests against
the Lua (as seqgen/test/lua_compare does for c34). Each becomes a sequencer type in the type list.
