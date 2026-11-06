# Plan 016 — immediate MLR on long loops + time-stretch (warp) to BPM (owner 2026-11-06)

Owner: "We want to improve the sample syncing, warping and make MLR really immediate." Answers: warp = time-stretch
to BPM (pitch kept); MLR: "the sound starts late"; "the limitation of the pads is that the longer patterns I use most
only have 8 cue points in MLR, so the solution would be to use higher-resolution cue points and basically to limit to
two samples being chopped at once"; material: long loops (> 6 s, streamed from SD).

## A — MLR: 2 tracks × 16 cue points, cue cache for instant starts
- MLR layer: each track uses **two pad rows = 16 cue points** (row a = cues 1–8, row b = cues 9–16); 2 tracks per
  screen (groups 1–2, 3–4, … via screen buttons), pad columns 9–12 stay the per-track controls (stop/rev/rate/resync)
  on the track's first row; the second row's cols 9–12: loop/rec helpers or empty (decide, keep simple, label on
  screen). Lane screen: 2 lanes with 16 segments each. Two-pad loops, latch, rec takes, quantise, SYNC ALL keep working
  with 16 positions (positions become 1/16 of the loop).
- **Cue cache** for the (at most 2) active MLR tracks with streamed samples: preload ~0.25–0.4 s of audio at each of
  the 16 cue points into EXTERNAL_RAM (raw storage, set up in TrackerImpl::init / on MLR entry — no static ctors in
  EXTERNAL_RAM), filled in the background by the file manager (never blocking the audio ISR); a jump plays from the
  cache immediately and hands over to the stream once the VoiceStreamManager has refilled from the new position
  (seamless join). Budget ≈ 2 tracks × 16 × 0.3 s × 44.1 kHz × 2 B × channels; if EXTERNAL_RAM is short, take it from
  SAMPLE_MEMORY_SIZE (dev guide: up to 3.5 MB can be freed on MK66). Resident (non-streamed) samples need no cache.
- Measure: press-to-sound latency for streamed loops before/after (host model + device-test row).

## B — Warp: time-stretch loops to the project tempo (pitch kept)
- Per track (or per instrument) **Warp** on/off + loop length in **bars** (auto-guess from sample length at the
  current BPM, editable); playback ratio = (bars × 4 × 60 / BPM) / sample duration; pitch unchanged.
- Algorithm for the Cortex-M4: granular overlap-add / WSOLA-lite — grains ~30–50 ms, 2 overlapping grains with
  equal-power or Hann crossfade, grain start positions advanced by the tempo ratio; optional transient-aware grain
  alignment later. Works with streamed samples (read ahead). Must coexist with MLR (cue positions are positions in the
  warped timeline) and launcher clips.
- CPU: measure per warped voice (readout like PolySynth); cap the number of simultaneously warped voices (start with 2)
  if needed. Sound: no clicks at grain joins, tempo change follows smoothly.
