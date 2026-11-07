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
  pressing a new chord while latched switches to it. Stop = panic ends latched sequences.
- Per-track setting **Latch on/off** in the new parameter page. Default: Off (current behaviour) unless the owner says
  otherwise. Step sequencers have no latch setting.
- Saved per track (new seqtracks.bin field → new version, oldest-version rule).

## Limits
Flash ~30 KB left — report the size delta. Display stages ≤ 3840 B. No long-press, no new hidden gestures.
