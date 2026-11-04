# Plan 005 — UX sketches (draft for the owner)

Screen 800 × 480. Bottom bar y 425–480 = 8 labels of 100 px over the 8 screen buttons (as in
gSeqSession.cpp). One sketch column ≈ 12 px. Pads 12 × 4.

Shared principle for all four screens: **the screen is a map of the pads.** Whatever the pads do
is drawn in the same geometry (same columns, same rows), so the eye moves pad → screen without
translating. Live state (playing, queued, recording, bypassed) is shown by glyph + brightness,
never by text you have to read mid-performance.

## R4 — MLR / CUT screen (tracks horizontal, playheads)

```
y0  ┌──────────────────────────────────────────────────────────────────┐
    │ CUT   group 1–4                         ● REC held     bar 3.2   │ header 40
y40 ├──────────────────────────────────────────────────────────────────┤
    │ 1 Kick  │▓▓▓▓|    |    |    |    |    |    |    ││ ■  ◀  1x  ⟲ │ lane 90 px
    │         │    ┆▲                ˄ grid           ││              │  ▲ = playhead (smooth)
    │ 2 Snare │    |    |[═══ loop ═══]|    |    |    ││ ▶  ▶  ½   ● │  ˄ = where the track
    │         │                ┆▲                     ││              │    would be on the grid
    │ 3 Chop  │    |    |    |    |    |    |    |    ││ ■           │
    │         │  stopped (lane dimmed)                ││              │
    │ 4 Pad   │    |    |    |    |▓▓▓▓|    |    |    ││ ▶  ◀  2x  ↻ │  ↻ = recorded loop
    │         │                     ┆▲                ││              │      replaying
y425├──────────────────────────────────────────────────────────────────┤
    │ 1–4  │ 5–8  │ 9–12 │ 13–16│      │      │SYNC ALL│ CLIPS        │
    └──────────────────────────────────────────────────────────────────┘
       8 segments = pad cols 1–8          4 cells = pad cols 9–12 (STOP REV RATE RESYNC)
```
- **Easy way out (owner):** one action snaps every track out of its MLR loop / jammed position /
  recorded replay and back onto the global playhead: **screen button 7 = SYNC ALL** (always
  visible), and **Shift + any RESYNC pad** does the same. Immediate (no quantise wait): each track
  jumps to where the grid playhead is, so it lands in sync by definition. Reverse and ½/2× are
  reset too, so the result is exactly the stock playback. Provisional: recorded takes are dropped
  (devil's-advocate to weigh "keep last take for re-engage").
- One lane per pad row (4 tracks of the selected group), lane split into the 8 pad segments.
- **Playhead** = thin vertical line at the true 16-bit position (moves smoothly, not per segment);
  the segment it is in is filled. Loop = bracket over its segments.
- **Grid ghost** (˄): small tick where the track would be if it were not jammed. Shows at a glance
  how far off the grid a track is → when RESYNC is worth pressing.
- Right block mirrors pad cols 9–12 with the same glyphs as the LEDs.
- Rec held: header turns red, lanes being recorded get a red edge.
- Later (not v0.2): waveform outline as lane background.
- Cost: redraw only the playhead strips (4 × ~4 px wide rects) at ~30 fps; full redraw on events.

## R1 — Play mode (bottom-right Perform ×2)

```
y0  ┌──────────────────────────────────────────────────────────────────┐
    │  stock pattern view, block of 4 tracks (5–8 shown)                │
    │  ┃ 05 ......  ┃▌06 C-4 01 ▐┃ 07 ......  ┃ 08 ......  ┃            │  selected track
    │  ┃ ...        ┃▌   ...    ▐┃ ...        ┃ ...        ┃            │  column framed
    │  (pattern rows, a few fewer than stock)                          │
y330├──────────────────────────────────────────────────────────────────┤
    │ only while Shift held — legend of the pads for the selected seq: │
    │  ┌─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┐  roots ■  chord ▣  4×4 panel ▤        │  mini grid 12×4,
    │  └─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┘  octave ↕  hold ●  rec ◉            │  zones coloured
y425├──────────────────────────────────────────────────────────────────┤
    │1 CHRD●│2 MONO │3 ---  │4 ---  │5 ARP  │▌6 MONO▶▐│7 ---  │8 ---  │  seq strip
    │ C min │8 stp  │ stock │ stock │ 1/16  │ live   │       │       │  (tracks 1–8 / 9–16
    └──────────────────────────────────────────────────────────────────┘   via Left/Right)
```
- Bottom strip: per track the sequencer type + one key setting; ● = live takeover now,
  ◉ red = recording.
- Hold a screen button + wheel: a vertical type list pops up **above that cell**
  (--- / CHORD/ARP / MONO), current one highlighted; release = keep.
- Shift legend replaces nothing permanently: it overlays the pattern rows above the strip and
  disappears on release.

## R2 — alt Song = clip launcher (Song ×2)

```
y0  ┌──────────────────────────────────────────────────────────────────┐
    │ SESSION        launch: 1 bar  ▮▮▯▯                  scene 3 ▶    │  quantise countdown
y40 ├────┬──────────────────────────────┬───────────────┬───┬──────────┤
    │row │ 1  2  3  4  5  6  7  8       │ 9 … 16        │   │ scene    │
    │ 01 │ ■  ■  ·  ■  ·  ·  ■  ■       │ ░  ░  ░  ░    │   │  ▷       │  rows outside the
    │╔═══╪══════════════════════════════╗               │   │          │  frame dimmed
    │║02 │ ▶  ■  ■  ▶¹ ·  ·  ■  ·       ║  ░  ░         │   │  ▶       │
    │║   │ ▬▬▬                          ║               │   │          │  ▬ = loop progress
    │║03 │ ■  ◌  ■  ■  ·  ·  ·  ·       ║               │   │  ▷ 1×↓   │  ◌ = queued (blinks)
    │║04 │ ·  ·  ·  ·  ·  ·  ·  ·       ║               │   │  ▷       │  ¹ = play 1× (→ next
    │║05 │ ■  ■  ■  ■  ■  ■  ■  ■       ║               │   │  ▷       │      row down)
    │╚═══╧══════════════════════════════╝               │   │          │
    │ 06 │ ·  ·  ·  ·                   │               │   │          │
y425├────┴──────────────────────────────┴───────────────┴───┴──────────┤
    │ LOOP/1× │ QUANT │       │       │       │       │       │ CUT    │
    └──────────────────────────────────────────────────────────────────┘
```
- Grid = song rows × 16 tracks; the **double frame is the 8 × 4 pad window**; arrows move it
  (Left/Right: tracks 1–8 ↔ 9–16, Up/Down: rows). The empty pad column 9 is drawn as a gap.
- Cell glyphs: · empty, ■ has notes, ▶ playing, ◌ queued (blinks until the launch point),
  ¹ play-1× flag. A thin progress bar under a playing clip shows where the loop is (when it ends).
- Header: launch quantise and a countdown to the next launch point.
- Hold screen button 1 + wheel: LOOP / 1× for the clip under the last pressed pad;
  Shift + button 1: for the whole scene (scene column shows ▷ 1×↓).

## R3 — fader page (Shift + top Perform)

```
y0  ┌──────────────────────────────────────────────────────────────────┐
    │ FADERS                        sends affect: 1 2 3 ▌4▐ 5 …        │
y40 ├──────────────────────────────────────────────────────────────────┤
    │ 1 ┃FX1-4 ┃ FILTER  ███████████████░░░░░░░░░░░░░  62%  │  ON     │  row = pad row
    │   ┃      ┃ slot1                                     │         │
    │ 2 ┃PING  ┃ SEND    ████████░░░░░░░░░░░░░░░░░░░░  25%  │  ON     │  group tag = which
    │   ┃      ┃ time 1/8 · fb 40% · filter 6k · ping-pong │         │  column 9/10 pad
    │ 3 ┃REV   ┃ SEND    ████░░░░░░░░░░░░░░░░░░░░░░░░  12%  │  BYPASS │  is lit
    │   ┃      ┃ size 70 · damp 30 · predelay 20           │ (greyed)│
    │ 4 ┃FX9-12┃ STUTTER ░░░░░░░░░░░░░░░░░░░░░░░░░░░░   off  │  ON     │
y425├──────────────────────────────────────────────────────────────────┤
    │ T1   │ T2   │ T3   │▌T4▐  │ T5   │ T6   │ T7   │ T8   │ (stock)   │
    └──────────────────────────────────────────────────────────────────┘
```
- Four bands = four pad rows. Each band: group tag (cols 9/10), FX name, a long bar with the
  8 pad steps marked, value, and a second line with that group's other settings.
- Right cell = pad column 12: ON / BYPASS. Bypassed row is greyed, its fader position kept,
  so un-bypassing drops the effect back in exactly where it was (good for builds).
- Fader values + positions + bypass are saved with the project.
