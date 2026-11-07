# Plan 025 — Sequencer settings as on-screen parameters + latch (owner feedback on v0.4.4, 2026-11-07)

Owner: "When calling the sequencer help, let's add all the sequencer settings as parameters on screen that can also be
navigated and changed like the PolySynth. I also like the latch in some of the synths and would like to bring latch in
for Chord/Arp — basically the sequence is toggled on/off instead of only hold. But all sequencers need a setting to
disable this as well. This doesn't count for the step sequencers of course."

## Parameter page
- The sequencer help screen (Play mode, tap track) gets the selected track's sequencer settings as a parameter list,
  navigated and changed exactly like the PolySynth instrument page (same controls, jog changes the value, screen
  buttons/arrows move). The help legend stays visible (shortened) so the how-to is never hidden.
- Every setting the sequencer type has (type, root/scale, rate, octave, range, gate, pattern/mode, etc.) — taken from
  the existing per-track state, saved as today (seqtracks.bin, oldest-version rule if a field is added).

## Latch
- Chord/Arp and the other live-played (non-step) sequencer types (HR Chord/Arp, Chord Stabs, Strings, Mono/c34 —
  check which are held-key types): **Latch** = a pad press toggles the sequence on/off instead of only while held;
  pressing a new chord while latched switches to it. **Stop does not clear them** (owner clarification, 2026-11-07): transport
  Stop only stops the sequences; a latched sequence (and its chord / state) stays latched and plays again when Play is
  pressed. Held pads are released on Stop as today; only an explicit un-latch press (or turning Latch off) ends a latched
  sequence.
- Per-track setting **Latch on/off** in the new parameter page. Default: Off (current behaviour) unless the owner says
  otherwise. Step sequencers have no latch setting.
- Saved per track (new seqtracks.bin field → new version, oldest-version rule).

## Limits
Flash ~30 KB left — report the size delta. Display stages ≤ 3840 B. No long-press, no new hidden gestures.

## As built (branch v045-seqparams)

### Parameter page
- Tap the selected track again (the pad legend): the page **above the legend** shows the track's sequencer settings as rows of
  cells (name, bar, value) like the PolySynth page: **Left / Right** move between the cells of a row, **Up / Down** between
  the rows, the **jog wheel** changes the value (Shift = fine; 4 per detent on the 60+ ranges), **Delete** resets the value
  under the cursor. The legend with its help lines stays visible below (unchanged; screen-button hold + jog still opens the type
  list). The pattern control is hidden while the page is shown (as for the PolySynth view). No new keys.
- Tables per type in `core/source/seqParamsLogic.h` (pure, host tested): Chord/Arp 23 settings in 4 rows (Play: Type Speed Dir Size
  Chord Octaves Latch; Pitch: Oct Inv Transp Mode Lock key Minor; Feel: Groove Velocity Lane Swing Gate; Groove: Param Arch
  Down 8th 16th), Mono 8, Strings 5 (Root Scale Octave Interval Latch), the HYPER_RASP scripts 4..16 (Arp Lines / Chord Stabs,
  Arp Seq, Chord/Arp, Bass, Euclid, Grid Seq, Step Follow). Not shown: view / page state (Mono edit page and half, Chord/Arp page),
  mute (the stock mute), the groove seed, the Grid Seq voice notes (the engine reads them back with the Transpose added), the
  Arp Seq chord type (set by the chord keys).
- Defaults in the tables are checked against a fresh generator of every type (`test_params_tables`), the ends of every range
  are accepted by the generator, every value has a text.
- Engine: `postParam(track, row, value)` (a new command kind of the existing ring, applied at the next audio block),
  `paramValues(track, out)` (one critical section), `applyParam`. The page shows a just-posted value for 60 ms so two fast jog
  steps do not read the old value. The HR Arp Seq got setters for its cycle values (mode / speed / direction / groove /
  octave / quality), they were only on its pads.

### Latch
- **Engine latch** (Chord/Arp, Strings, HR Bass; default Off): `seqlogic::PadLatch` filters the note pads: a press that starts a
  new gesture switches the latched pads off (when it hits one of them) or replaces them; presses while a finger is down join
  the group (chord / several strings); releases are not forwarded. Panel / control pads and the Strings setup pad are never
  latched. Latch turned on while pads are held keeps them held; Latch off (or a type change) releases them.
- **Script latch** (HR Arp Lines, Chord Stabs, Arp Seq, Chord/Arp): these scripts always latched a root on a tap, so their
  setting is **On by default** (unchanged behaviour); Off = the root sounds while its pad is held (`HrScript::setLatch`). The HR
  Chord/Arp already had the setting (`P_LATCH`, the Latch pad); the page shows the same value.
- Step sequencers (Mono, Euclid, Grid Seq, Step Follow) have no Latch cell and no latch.
- **Stop (owner clarification)**: Stop does not clear a latched sequence. A latched Chord/Arp is silenced by the Stop (its
  free running clock is paused) but stays latched; **Play sounds it again with the same chord** (`ChordArp::retrigger()` for the
  held-chord type; the arp types resume on their pulses). A new press while stopped plays as before (free running) and latches.
  HR scripts already stop with the transport and keep their latched root. Held pads are released on Stop as before (a latched
  pad is not "held": the physical release was swallowed). Host test `test_latch_stop_play`.
- Saved: `seqtracks.bin` **version 3** adds one Latch byte per track after the HR trailer; it is written only when some Latch
  differs from its type default (the default projects still write version 1 / 2 byte for byte), version 1 and 2 files load as
  before (defaults). A version 3 header over a version 2 body, a cut or a bad CRC is refused. The Latch is part of the change hash.

### Numbers
- Image end 0xF7038 = 1,011,768 B (base 0xF48B8 = 1,001,656 B): **+10,112 B flash**, margin to 0xFC000 20,424 B; RAM +1,888 B
  (two more view copies of the overlay); EXTERNAL_RAM unchanged. The flash is mostly setters that were not linked before
  (Chord/Arp, Mono, the scripts), the tables (about 1.3 KB), the page drawing and the latch code.
- Display: the page is its own control (a second `cSeqPlayLegend`, mode PARAMS, drawn below the legend control): one stage of
  its own, worst case **1,480 FIFO bytes** (limit 3,840, `test_params_draw`, every type at every cursor position, bounds
  inside the 800 x 394 band). The legend stage is unchanged.
- Host: core/seqtest `make test` and seqgen `make test` 0 failures (new: `test_params.inc`, 3 seqgen tests).
- Previews: core/seqtest `make legend-preview` (real `seqPlayLegend.cpp` against stubbed display list macros) writes
  `build/preview/legend_{chordarp,mono,strings,hr_chordarp,hr_stabs}.png`.

### Not done / open
- Not measured on the device. The page is not drawn while the PolySynth view is open (that view has its own page).
- The Strings page repeats the Setup pad root / scale / octave / interval (the pad stays).
