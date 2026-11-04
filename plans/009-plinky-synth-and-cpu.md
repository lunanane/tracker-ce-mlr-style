# Plan 009 — Plinky synth voice on the original Tracker + CPU savings (owner 2026-11-05)

Owner: "Let's do it, and let's try to optimise the Plinky code even more for low CPU usage on the way, with 4 voices
max; also let's identify if there's any CPU saving in the current Tracker code that can help after all."
Target: ORIGINAL Tracker (MK66) only.

## S — Plinky synth engine (spike → instrument)
- Enable the CE synth framework (`core/synth`, `Synth.cpp`, registry, engines; today `-DFEATURE_SYNTH` only on IMX)
  on MK66, and add a **Plinky** engine: the base synth voice of github.com/plinkysynth/plinky_public (software MIT —
  keep the notice; credit, not a product name). No sampler/arp/sequencer/effects of Plinky — just the voice
  (oscillators, noise, filter, envelopes, LFO/mod as far as it is part of the voice).
- **4 voices max**, polyphonic, played by a Tracker track (and by our sequencer types) like the Chiptune engine on IMX.
- Optimise for CPU on the way: block processing, fixed-point or single-precision float with the M4F FPU, no per-sample
  divides/transcendentals (tables, polynomial approximations), shared LFOs, skip silent voices, control-rate
  parameter updates, oscillator anti-aliasing chosen for cost (polyBLEP / table), consider an internal rate below
  44.1 kHz only if it sounds acceptable.
- **CPU readout** on the instrument page (average + peak share of the audio block, like Reverb CPU), so the owner can
  measure 1–4 voices with a normal project.
- Flash: image must stay < 0xF0000 (check the last `:02000002` record); report cost; free more space with -Os of
  further non-audio code if needed.

## P — CPU savings in the existing Tracker code (audit, then safe changes)
Find where the audio interrupt and the main loop spend time on MK66 (sample voice mixing, interpolation, envelopes,
filters, FX chain incl. our delay bandpass / reverb types, master processing, display/SD work competing with audio),
rank savings by size and risk, and implement only the safe ones (bit-exact or inaudible) after the owner sees the list.
