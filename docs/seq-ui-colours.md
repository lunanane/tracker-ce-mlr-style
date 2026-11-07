# Sequencer UI colours (plan 029)

The mod's screens use the stock Tracker palette with one fixed meaning per colour. A colour is never decoration: if it is not in
this table it is neutral grey or white.

| Meaning | Colour | Where it is used |
|---|---|---|
| Focus / selection / "adjust this" | yellow (Instrument key, 0xFEF749) | active page chip, selected cell on the settings page, MLR loop bracket, Adjust pads in the legends |
| Stored value (project data) | blue (FX2 key, dimmed) | project-value fill of the fader bars, the value bars of the settings page (stock cyan) |
| Live / dialled in / playing (OK) | green | dialled or live fader rows, notes and steps in the legends, page-chip dot, Camelot "match" |
| Engaged / latched / mode | orange | LATCH cell lit, mode and hold pads in the legends, Camelot "near" |
| Danger / record / disengage | red | DISENGAGE pad with something to take off, REC bar, Action pads, "no tracks selected", Camelot "clash" |
| Off / disabled / unused | grey, dimmed (no fill, no label when unusable) | unusable rows, LATCH on a row that cannot latch, silent track levels |
| Track identity | the four Tracker key colours (teal, yellow, purple, blue), tracks 5-8 lightened | amp view lines and level labels |
| Slot identity | A = Note teal, B = FX1 purple | MLR lanes, labels and lit pads |

Rules

- Yellow means "where you are", never "good" or "warning". Warnings are orange (soft) or red (hard).
- A bright fill means something is happening now (live, latched, recording). Stored values are a calm dark blue so the eye lands on
  what is live first.
- Text colour follows the fill it explains: green state word next to green bars, dim grey for the default (PROJECT) state.
- Red is reserved for things that take something away or record. It is never used for a value.
- New screens re-use this table; a new meaning needs a new row here first.
