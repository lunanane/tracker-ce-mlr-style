# Plan 019 — one flexible PolySynth engine, three instances, Model:Samples-style multi-edit (owner 2026-11-06)

Owner: "The optimum for me would be to have 1 flexible synth engine and to be able to load 3 instances of it (in the
instrument list). Maybe extend our synth engine with some simple concepts, like 2 sub-octave square waves, some other
filter designs, and envelopes for them, more LFO waveforms … maybe borrow some concepts from the other synth engines as
well, so maybe a chorus also etc., to have a distinct sound but variations. Then since we have the same synth engine for
different instruments we can toggle between those and change parameters for one, but we can also hold Shift and dial a
parameter and then it adds the value to all other synth engine instances — basically the best performance workflow of
the Model:Samples."

## 0 — make room first (flash ~21 KB left, internal RAM 86 %, EXTERNAL_RAM 98 %)
Free flash (more -Os on control-rate audio code that isn't per-sample, dead code, data tables) and EXTERNAL_RAM
(SAMPLE_MEMORY_SIZE, developer guide) before adding features; keep the image < 0xF0000 with ≥ 16 KB margin.

## A — three instances
- Instrument list: **PolySynth 1 / 2 / 3** (three fixed slots, e.g. MIDI instruments 14/15/16 internally; v0.3.x
  projects with PolySynth on 16 keep it as instance 1 or as their slot — decide and migrate). Each instance: own
  parameters, own per-pattern snapshots + morph, own CPU readout share.
- **Shared voice pool + CPU governor** (concept from tracker-ff): e.g. 8 voices total across instances (max 4 per
  instance), steal by priority/age; a governor reduces voices when the audio block load gets high (measured), never
  glitches.
- PolySynth view (Play mode and Instrument page): **toggle between instances** with a clearly labelled key; the view
  shows which instance is edited. Pitch view per instance (colour per instance).
- **Shift + dial a parameter = relative change on ALL instances** (each keeps its own value, the same delta is added,
  clamped per instance) — the Model:Samples multi-edit. Clearly shown ("ALL" marker) while Shift is held.

## B — engine extensions (same engine for all instances)
- **2 sub-oscillators**: square, −1 and −2 octaves, each with a level.
- **Filter designs**: selectable type — the current Plinky low-pass gate, plus SVF low-pass / band-pass / high-pass
  (12 dB) and a 24 dB ladder-style LP if affordable; cutoff/resonance/drive shared; **filter envelope** (ADSR + amount)
  separate from the amp envelope.
- **More LFO waveforms**: sine, triangle, saw up/down, square, sample & hold, smooth random; LFO targets as now + filter.
- **Chorus**: per instance, cheap (one modulated delay line per side, EXTERNAL_RAM raw storage), mix + rate/depth.
- Borrow concepts from tracker-ff engines where cheap (see plan 018), e.g. drive/fold shapes.
- All new parameters in the section UI (new sections Sub, Filter env, Chorus), morph-able, saved in plinky.bin
  (versioned; old files load, older mod versions read what they can).
- CPU: keep the per-voice cost close to today's; sub-oscillators and SVF ~cheap; ladder optional.
