# Plan 015 — higher-resolution views + Plinky wavetables in PolySynth (owner 2026-11-06, v0.3.6 hardware)

Owner: "Things look great." Then:

## V — pitch view and amp view need much higher resolution
- Both views: more points, finer lines. They now run as view switches (the pattern control is hidden while they show),
  so the frame budget is the view + title + strip — use it (measure `cDisplay::shownRamBytes()` worst cases; raise
  the per-view budget from ~4 KB accordingly, keep a safety margin under 8172 B).
- Pitch: sample the latched voice state every block (or every 2nd) instead of ~35 ms; more points per voice
  (e.g. 160–256 across the width), smooth glide/LFO.
- Amp: measure per track every audio block (2.9 ms) instead of 40 ms (block peak / RMS per track at the source, cheap),
  so the incoming stream is dense; draw each track as a contour band (average … peak) with thin outlines, many points.
- **Amp time axis is wrong**: values come in fast but are already compressed after ~10 % of their way to the left.
  The linear part near "now" must be much longer and the compression must build up gradually towards the left axis
  (like the reference). Retune the age→x mapping (larger time constant / blended linear+log / power curve) so roughly
  the right half shows the last few seconds almost linearly and the left half holds the long history compressed;
  optionally the jog wheel adjusts the compression on the view (shown as a label).

## W — Plinky wavetables in PolySynth
Owner: "where are the wavetables from Plinky? Currently we have saws and squares, which sound great, but I'd like the
wavetables in there as well." Port Plinky's wavetable oscillator mode (shape parameter morphs saw → square → the
wavetable set, as on Plinky) from plinky_public (MIT). Assess the wavetable data size first: embed in flash if it fits
(image must stay < 0xF0000, now 0xDCC44 ≈ 78 KB left; compress/decimate if needed), or generate at boot into
EXTERNAL_RAM if Plinky builds them procedurally (raw storage + init in TrackerImpl::init — never a static constructor
in EXTERNAL_RAM). CPU per voice must stay close to the current cost (interpolated table lookup, band-limited tables).
