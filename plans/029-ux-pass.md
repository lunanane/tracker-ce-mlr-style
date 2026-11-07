# Plan 029 - UX pass on the new screens (owner 2026-11-07)

Owner: "the ux-expert should run over these screenshots and make those designs really neat, also taking the tracker colors into
account and using them with intent (not just nice coloring)."

Scope: Master fader pages (and the Perform fader pages, same control), MLR / CUT page, amp view, Play legend (small isolated
changes only). Visuals and on-screen text only: no pad layout, behaviour, hotkey or long-press change. The colour table is
docs/seq-ui-colours.md. Previews: `make preview master-preview legend-preview amp-preview` in core/seqtest (lane_preview.py
re-renders every .ops, so run `preview` first - /e/claude/ux-shots/render.sh does).

## Audit findings, ranked

1. Major - no colour meaning on the fader pages: stored (project) values were light grey, the page chip white, the unused-row
   text white; the same white/grey carried "selected", "stored" and "off". The eye had nothing to land on.
2. Major - the same sentence on every row ("project value (saved with the project)", "dialled in (DISENGAGE restores ...)")
   plus a loud white "PROJECT" state word on the most common state. Noise on every row, the information (a row is dialled in)
   was lost in it.
3. Major - clutter from controls that cannot act: a dim LATCH cell and label on three of four rows, two "(not used)" rows with
   full bars and DISENGAGE cells on High-pass and on Beat-repeat sub pages.
4. Minor - hierarchy inverted: name bold, value regular. The value is what the pad row sets; it is now the bold 17 px line.
5. Minor - footer repeated the DISENGAGE explanation on every page in the largest text, top right said "page pads: column 9".
6. Minor - MLR header repeated "A: T2 1/4  B: T8 1/4" which the lane labels already show; the state words (REC, LOOP) were all
   grey although REC and the loop bracket have colours.
7. Minor - amp view: "Jog: time scale, linear 8 s / total 30 s" too long for what it says; silent tracks ("--") looked as
   loud as playing ones; the first time label (30s) collided with the -48 dB label; hint "Amp: Close" duplicates the Close
   screen button (kept, see "Left").
8. Minor - Play legend: "tap track again to close" was yellow, but yellow means focus.

Checked and fine: label truncation (drawText fits to the cell, "DISENGAGE" fits in 78 px at 12 px), alignment of the bar cells
to pad columns 1-8 and the LATCH / DISENGAGE cells to columns 11 / 12, MLR slot identity colours (A teal, B purple, loop yellow,
ghost blue, rec red already followed the tracker keys).

## Decisions

- D1 Colour mapping as in docs/seq-ui-colours.md (focus yellow, stored value dimmed FX2 blue, live/dialled green, latched
  orange, danger/disengage red, off grey, track identity = key colours, slot identity A teal / B purple).
- D2 The fader view draws unusable rows as a faint panel with only the row number; LATCH only where the row can latch.
- D3 A row's state word is loud (bold, coloured) only when it differs from the stored value; PROJECT / OFF are small and grey.
- D4 Notes under a row exist only when they say something the state does not: nothing for a stored value; "DISENGAGE restores
  the earlier value" when dialled in. Footer lines keep only what is page specific.
- D5 Page chip: active = yellow with dark text (same as the selected cell on the settings page); dot = green (dark on the
  active chip).
- D6 MLR: header drops the duplicated line (kept in the pad legend, which has no grid text), Q moves to the left, state words
  coloured by meaning.
- D7 Amp: silent level labels dimmed, label collision fixed, shorter jog label. The hint stays: removing it (empty string)
  made one budget in the test_amp.cpp fitting sweep fail `within` by a pixel at one budget; test not touched, owner to decide.
- D8 Legend: hint colour grey (0x909090) instead of yellow.

## Spec for the settings page / Play legend (after the Camelot merge, plan 028)

- Camelot match marks use the table: match = green (same position), good/near = orange (one step or A/B switch), clash = red.
  Not yellow (focus) and not blue (stored value). Camelot's mark table must hold these three values in one place.
- The settings page values/bars stay cyan (= stored value). The selected cell stays yellow (focus). The wheel: the playing
  root is filled, never yellow unless it is also the selected cell.
- Row captions: "Hold a root. Type: chord shape (hold Root + type = more). 2 fingers on Speed/Dir: more." is 3 sentences at
  arm's length; keep the first, move the rest to Help.
- Group labels (Play / Pitch / Feel / Groove) are cyan like the value bars; make them white/grey so cyan only ever means a value.
- "Arrows move, jog edits, Del resets" (top right) as a grey hint, not white.

## As built

Files: core/interface/seqPerform/faderView.cpp (palette, layout), masterParams/masterFaderView.h and seqPerform/fSeqPerform.cpp
(note and footer strings), seqSession/mlrDraw.h, source/seqAmpViewLogic.h, seqPlay/seqPlayLegend.cpp, docs/seq-ui-colours.md.
Tests: no expectation changed. seqtest `make test` and seqgen `make test`: 0 failures. Master fader stages 488..1276 B
(limit 3840). Flash: image end 1,016,260 B before, 1,015,900 B after (-360 B); margin 15,932 -> 16,292 B.
