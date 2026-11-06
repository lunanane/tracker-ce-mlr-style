# Polyend Tracker Community Edition (CE)

Welcome to the **Polyend Tracker Community Edition** repository!

Built directly from the original Polyend firmware source code, Tracker CE opens up the Polyend Tracker platform to musicians, sound designers, and developers around the world. Whether you are here to flash the latest Community Edition firmware, want to build your own idea on the Tracker, design custom themes and fonts, write Sample Editor plugins, or build new synthesizer engines, we are excited to have you here.

This unified codebase powers all three hardware instruments in the Tracker family:
* **Original Polyend Tracker** (`NXP Kinetis MK66FX1M0`, Cortex-M4F @ 180 MHz)
* **Polyend Tracker+ & Tracker Mini** (`NXP i.MX RT1051/RT1064`, Cortex-M7 @ 600 MHz)

### What's New:
See [`CHANGELOG.md`](CHANGELOG.md) for the full list of new features and improvements.


## Sequencer tracks mod (MLR style)

This branch adds sequencers per track and a clip launcher on top of Tracker CE. It is a mod by a community member, not Polyend software.

**What it is**
* **Play mode**: every track gets a sequencer type, **Chord/Arp** or **Mono**, played from the pads, with **Record** (**Rec** toggles a take: press to start, press again to end; steps are replaced only while you play on the track (idle steps and old laps keep their notes); **Shift + Rec** records exactly one loop) into the track's tracker steps. It is a page of the pattern editor: the bottom labels show the sequencers of 8 tracks.
* **Session view**: an Ableton-style **clip launcher** (CLIPS: a clip is one track of another pattern; scene launch, stop) and an **MLR CUT layer** (v0.3.8: **two slots**, each holding one track you pick with *Assign slot 1 / 2*; 16 pads per slot = 16 steps of a musical grid, 1/4 or 1/8, of the section the playhead is in; STOP / REV / RATE / RSYNC and GRID / SECT / REC / MUTE pads, a *Help* legend of every pad; a playhead per track with playhead recording: **Rec** toggles a take, the jumps played during it loop on after it ends; two pads make a latched loop, a single pad ends it; jump quantise off (at once) / step / beat / bar on screen button 5). On the original Tracker a **cue cache** (768 KB of external RAM) preloads the cue points of the shown section of both slots, so a jump into a long SD-streamed loop starts at once.
* **Alt Performance page**: a fader page for the perform FX.
* **FX filters (v0.2.4)**: the ping-pong delay filter is a **bandpass** (center = the stock delay filter value, on the PING page's jog wheel and fader 4; **Shift + jog** = width, 1/3 to 3 octaves, default 1; the top of the range is "open" (19 kHz and above) = filter out, as before, and the band fades in over the top octave, so one click below "open" is still almost unfiltered. **Note:** a project whose stock delay filter was lowered (it was a lowpass that darkened the repeats) now gets a band around that frequency, so the repeats sound thin instead of dark). The reverb gets a **low cut** (highpass, off to 5.1 kHz since v0.3; it was 1.8 kHz) on its input, on the REV page's jog wheel (**Shift + jog** = diffusion). A second reverb type, **ShimmerReverb** (Master > Reverb > Type, Stock is the default; v0.3 adds its **Shimmer** amount 0-100 % as an item on the same screen, saved in `seqfx.bin`), is based on the reverb of the Plinky synthesizer firmware by mmalex (a port) (MIT, see below; on the Tracker Plus / Mini, whose external RAM is full, it borrows the two idle buffers of the stock reverb and has a shorter pre-delay, up to 23 ms). Width, low cut, type, shimmer and (v0.3) the fader page's track selection are saved with the project in `seqfx.bin` (version 2; older files load with the old sound) next to it (a project without that file sounds as before) and are never written while the transport plays. On the PING page two pads (row 3 and 4 of column 10) switch ping-pong and tempo sync, which the jog wheel did before.
* **Live-set Song page (v0.2.3)**: the song **loops the playing slot by default**. A slot only hands over to the next one when its scene (pattern) is flagged **play next**. You move on during a set by scene pads in the launcher or by **queueing a slot** on the Song page (select it, press **Insert**; it takes over at the pattern end, shown as a red square). The play-next flag is set with **screen button 1 held + wheel** on the selected Song page slot, or with **Shift + screen button 1 held + wheel** on a row of the launcher; it shows as an arrow on the Song page slot list and as a bar on the launcher's scene cell. The LIVE freeze / BACK TO SONG of v0.2 are gone (the loop makes them unnecessary). While the song plays you can move the Song page cursor without the red edit mode (the hint `Ins: queue` shows). A pattern break effect shortens a pass of a looping slot instead of leaving it. Audio export of the song and the stock playlist editing are unchanged. A `seqclips.bin` saved by a newer mod version is left alone (no downgrade of such projects).
* **Scene colours (v0.3.8)**: arrange the song in colour. On the Song page hold **screen button 1** (its label reads `Wheel:Next` / `Shift:Color`) and turn the wheel **with Shift down**: the selected slot's scene steps through **None, Teal, Yellow, Purple, Blue** (the four tracker key colours) and **Orange, Red, Green, White**. The label then names the colour; the slot shows a colour chip left of its play-next / queue marks. The colour belongs to the pattern (every slot with that pattern shows it) and the clip launcher uses it for the row: a tinted scene cell, a tint on the row's clips and a thin line under the row; playing and queued clips keep their outlines. None is the plain look of before. Saved in `seqclips.bin` (version 3; older files load with no colours).

**How to enter**
* Pattern page: tap the bottom-right key (**Perform**, the firmware's Pattern key) to switch Play mode on and off. Left / Right switch the strip between tracks 1-8 and 9-16; a screen button selects a track; hold a screen button and turn the wheel to pick its sequencer type; **tap the screen button of the already selected track** to show / hide the pad map (the legend; v0.3.3, Shift is only the stock mute gesture). Since v0.3.2 the pattern editor's own keys keep working in Play mode: **Note / Instrument / FX1 / FX2** choose the column of the cursor step (hold one and turn the wheel to edit that column, e.g. Instrument + wheel picks the instrument), **Copy / Insert / Delete** (with Shift: Paste / Home / Backspace), **Up / Down** and the wheel are stock; only the pads, Rec / Shift + Rec, the screen buttons, Left / Right and Shift are the sequencer's (Undo, Fill and the page-2 actions sit on the screen buttons: tap Pattern to leave Play mode). The selected sequencer cell shows `tap: help` as a reminder. A band above the strip shows the notes the selected track's sequencer sounds now (12 keys, one octave folded, note names, the root of a Strings track marked), v0.3.3.
* Clip launcher (Session view): the **Song** key toggles between the stock Song page and the launcher (v0.2.4). From any other page Song goes to the Song page as in stock; on the Song page a plain press and release of Song opens the launcher (no timing, nothing else held); in the launcher (CLIPS or CUT layer) Song returns to the Song page. A hold is still the stock momentary peek and a hold on the Song page does nothing. The bottom-right **Perform** key (the firmware's Pattern key) only toggles Play mode.
* Performance: the top-row **Perform** key (the firmware's Performance key) opens the fader page; **Shift + Perform** opens the stock Performance page (and back).

**Install and recovery**
1. Build (`make original`, `make plus`, `make mini`) or take a release: `PolyendTracker_SEQ_0.2.3.ptf`, `TrackerPlus_SEQ_0.2.3.ptf`, `TrackerMini_SEQ_0.2.3.ptf` (the device shows the version as `1.0.0 CE-SEQ0.2.3`).
2. Copy the `.ptf` to `/Firmware/` on the microSD card and run **Config > Firmware > Firmware update** (see [Flashing on Hardware & Emergency Recovery](#4-flashing-on-hardware--emergency-recovery)).
3. Recovery: keep the stock CE `.ptf` for your device on the card. If the mod misbehaves, flash the stock `.ptf` the same way; if it does not boot, use the reset pinhole procedure described in the Emergency Recovery note linked above.

**Status**: v0.2.3, **untested on hardware**. Use at your own risk and **back up your projects** first. Test plan: [`docs/seq-tracks-test-list.md`](docs/seq-tracks-test-list.md) (start with its "Start here" section); design plans: [`plans/`](plans/).

**PolySynth voice (plan 009 S, Original Tracker only)**: a polyphonic synth voice (4 voices max, after the base voice of the Plinky synthesizer firmware by mmalex, MIT, see below) that a MIDI instrument plays. Since v0.3.2 the voice is an instrument of its own: the entry **PolySynth** in the instrument list is MIDI instrument 16 (a project of v0.3 / v0.3.1 keeps the MIDI instrument it had claimed; nothing is sent to MIDI channel 16 from that instrument any more). Pick it like any instrument (Instrument key + wheel); its Instrument Parameters page shows three pages of voice parameters with the CPU share in the title. Several tracks can use it and share its 4 voices. The pattern editor, the Chord/Arp and Mono tracks and the clips all play it; the values are saved next to the project in `plinky.bin`. MIDI CCs on its channel (cutoff, resonance, envelope, ... reverb / delay send) modulate the sound without overwriting the saved values. A project that never used it is unchanged (Channel 0 = off). Test list: [`docs/seq-tracks-test-list.md`](docs/seq-tracks-test-list.md) section 11f; design and numbers: [`plans/009-plinky-synth-and-cpu.md`](plans/009-plinky-synth-and-cpu.md) ("S as built").

**Three PolySynths (plan 019 A, v0.4, Original Tracker only)**: the PolySynth is now one engine with **three instances, PolySynth 1 / 2 / 3** (MIDI instruments 16, 15 and 14; a v0.3 project that claimed another channel keeps it for PolySynth 1 and the others take the highest free channels). Each instance has its own parameters, per-pattern sounds and morph. They share a **pool of 8 voices** (at most 4 per instance, the **Voices** parameter is per instance) and a **CPU governor**: it reads the measured load of the whole audio block and fades voices out one at a time when the block gets too busy (steal order: quietest releasing voice, then the oldest note), and gives them back after two seconds of headroom; the voice cap is shown as `V8` next to the CPU readout. The PolySynth view has a headline with the tabs **SYNTH 1 | SYNTH 2 | SYNTH 3** (teal, yellow, violet: the same colours as the lines in the pitch view). The **Instrument Parameters** key opens the view on the instance of the selected track and each further press steps SYNTH 1 -> 2 -> 3 -> 1 (on the instrument page the same key steps the instance; Play mode is left with the Pattern key as before). **Shift + dial** a parameter adds the same change to that parameter of ALL instances (each keeps its own value, clamped per instance); a bright ALL tag shows while Shift is held. This replaces the old Shift = fine step. The three PolySynths are listed first (PolySynth 1, 2, 3) in the instrument picker and in the Instrument + jog stepping, then the stock instruments as before; only the order changed, the stored instrument numbers are the same, so old projects play the same. `plinky.bin` keeps the old frames for PolySynth 1 and adds one `PLVI` frame per further instance; a v0.3.x file loads (instances 2 and 3 get their defaults) and an older mod still reads PolySynth 1 from a new file.

**PolySynth engine extensions (plan 019 B, v0.4, Original Tracker only)**: all three PolySynths got the same new building blocks, every one a parameter in a new section of the view (the view now has eight sections, five rows are visible and scroll with the cursor; the instrument page buttons name the five visible rows). **Sub**: two square sub oscillators, one and two octaves below the note, each with a level. **Filter** gets a **Type**: the Plinky low pass gate (default, the old sound), an SVF low pass / band pass / high pass (12 dB per octave) and a 24 dB ladder low pass; cutoff, resonance and drive are shared. **Filter env**: its own ADSR and an Amount (-5 .. +5 octaves of cutoff, the middle value 50 = off) for every filter type, separate from the amplitude envelope. **LFO** waveforms: sine, triangle, saw up, saw down, square, sample & hold, smooth random; the target list gets Level (tremolo) next to Pitch, Shape and Cutoff. **Chorus** (per instance): Mix, Rate, Depth, a modulated delay per side (two taps half a cycle apart). At the default values of the new parameters every old sound is bit-for-bit the same. All new values are saved per instance in the PLVI frames of plinky.bin, per pattern (morph) like the others. The ladder is the most expensive filter (about one and a half times the cost of the old voice, Width 0 halves it); the hardware CPU readout and the V (voice cap) value show what a project can carry.

**PolySynth wavetables (plan 015 W, v0.3.7, Original Tracker only)**: the **Shape** parameter of the PolySynth (Oscillator row) now covers the Plinky wavetables as well. Values **0 .. 100 are exactly the sounds of v0.3.6** (Saws, then Pulse 1 .. 100); a project saved before keeps its sound (bit-exact, `plinky.bin` is unchanged and still version 1). Above that the value moves through the 17 wavetables of the Plinky firmware (MIT): **WT1 Saw, WT2 Sine, WT3 Buzz ... WT17 Hash**, eight steps from one table to the next (`WT3+50%` = half way from table 3 to 4; the sound is the linear blend of the two tables), 229 at most. The tables are band-limited per octave (9 mip levels, no harmonic above Nyquist) and interpolated; in this mode one table oscillator plays per side instead of the saw pair (Detune and Interval act like in Pulse). The Shape morph (plan 012) and the LFO to Shape cross table boundaries smoothly. Cost: 35 054 bytes of flash for the tables (no RAM); CPU per voice about the same as the saw pair. Test list section 11k; design and numbers: [`plans/015-viz-resolution-and-wavetables.md`](plans/015-viz-resolution-and-wavetables.md) ("W as built").

**Long samples stream automatically (plan 023 A, Original Tracker only)**: importing a sample into a slot is no longer refused because the RAM pool is too small for it. A WAV of 256 KB or more that does not fit the free pool is converted straight to the SD card (`Workspace/instruments/instrument_XX.pti`, in chunks, never fully in RAM) and kept as a **streamed sample**: only its 0.74 s attack pre-cache (64 KB mono / 128 KB stereo) uses the pool, the rest plays from the SD card. The file list and memory bar of the importer count only that pre-cache. Streamed samples are marked **SD** in front of the name in the importer slot list, the instrument picker and the pattern editor. When a project loads, a long sample that does not fit RAM in its play mode (Slice, Beat Slice) is streamed instead of dropped; a Wavetable or Granular instrument that cannot be resident becomes One-Shot. Switching a streamed sample to a mode that needs it in RAM shows "Streamed sample (SD) / too long for this mode" when it does not fit. Smaller samples still need free pool space. Test list section 11o; design and the findings on more than 48 sample slots: [`plans/023-samples-and-streaming.md`](plans/023-samples-and-streaming.md).

**Warp: loops follow the project tempo, pitch kept (plan 016 B, v0.3.8, Original Tracker only)**: a sample instrument can be time-stretched to the tempo of the project. **Instrument Parameters > Effects** (the second page): the two buttons right of "Instrument MIDI Mapping" are **Warp** (Off / On) and **Bars**. Bars is the length of the played part of the sample (start point to end point) in bars of 4 beats; **Auto** takes the sample length at the current tempo rounded to the nearest of 1 / 2 / 4 / 8 / 16 bars (shown as `Auto (4)`), the jog wheel sets 1 .. 32 (Delete = Auto again). With Warp on, the title shows what the sample is at the current tempo (`sample = 3.9 bars @120`) and the CPU of the warped voices. A warped voice plays the sample in `bars x 4 x 60 / BPM` seconds whatever the tempo is, **tune / finetune / glide still change the pitch** on top, and a tempo change (also a tempo ramp) is followed smoothly. It works for One-shot and the three loop modes, for samples in memory and for long samples streamed from the SD card, with start / end / loop points and with the MLR jumps (the sound starts at the cue point; the stretch stays that of the whole instrument). The setting is saved with the project in the sidecar `seqwarp.bin` (a project without it plays exactly as before; the stock project files are not changed). **At most 2 voices warp at once**; further notes play un-warped (the list of the limits is in `plans/016-mlr-immediate-and-warp.md`, "B as built"). Method: two overlapping Hann grains of 11 .. 35 ms with a phase alignment search of +-4.4 ms (WSOLA-lite), so a steady tone keeps its pitch; transients are smeared a little, stretches beyond 0.5 .. 2 times repeat or skip material. Slice, Beat Slice, Granular, Wavetable and the reverse step FX play un-warped.

**Beat repeat on the master bus, jammed from the CUT page (plan 021, v0.4, Original Tracker only)**: an Ableton style beat repeat as a master insert (after the tracks, reverb and delay returns, before the limiter; the Model:Samples design). On the **CUT / MLR page** the screen button **BeatRpt** (button 4) switches the pads and the screen to the beat repeat controls and back; MLR keeps playing underneath. The pads use the layout of the fader page: four horizontal 8-step faders on rows 1-4, page pads in column 9, output mode in column 10, LATCH / OFF / RESET in columns 11-12. Page **REPEAT**: row 1 GRID (momentary pads 1/4 1/8 1/16 1/32 1/4T 1/8T 1/16T 1/32T: **hold = repeat at that grid**, jam between pads), row 2 GATE, row 3 DECAY (100 = -6 dB per repeat), row 4 PITCH DECAY (100 = -3 semitones per repeat, resampled, the rhythm stays on the grid). Page **AUTO** (automatic repeats): INTERVAL (1/32 .. 4 bars), CHANCE, VARIATION, the grid of the automatic repeats. Output **MIX** (the default, repeats on top of the running audio), **REPLACE** (the audio is muted while repeating) or **GATE** (only the repeats pass). **LATCH** keeps a repeat going after the release, **OFF** ends it, **Stop ends every repeat**. As in the Model:Samples design nothing is recorded continuously: the slice is captured when the repeat triggers (the audio passes meanwhile), so the stage costs nothing while idle; slice starts are exact to the sequencer clock. Memory: a 128 KiB ring in the SDRAM (EXTERNAL_RAM), so at tempos below 81 BPM the longest grids step down (1/4 down to 81 BPM, 1/4T to 54, 1/8 to 40, 1/8T to 27; the screen says so). The settings are saved with the project in `seqbrpt.bin`; the Help button shows the pad legend. Test list section 11m.

**Perform FX and the fader page's track selection (v0.3)**: dialled-in perform FX (the fader page) stay engaged on **every** module switch (Pattern, Song, Master, Params, File, Config, sample pages ...), the stock rule that ended them at the pattern end is gone: they end only by DISENGAGE, UNLATCH, the Stop panic, or loading another project. The track selection of the fader page always stays what you last set (Stop/Play, disengage, module switches, re-entry and project loads do not clear it); it is saved in `seqfx.bin` and read from the first project loaded after power-on. **Memory (v0.3)**: the unused 32 KB SDRAM reverb array is given back on the Original Tracker; the Config page shows the lowest free heap since power-on ("free heap: A, min B B"); the limiter and the reverb treat a missing input block as silence.

**Known limits (v0.2.3)**: the CUT state is not saved with the project; on the original Tracker, jumps in long SD-streamed samples start at once only on a cached cue (dot on the lane; the cache needs a second or two after a section change), others may start late; MIDI perform CCs are ignored on the fader page; the delay time click is not fixed; recording on a track that holds a launched clip or is CUT-active is not supported; the stock 10 s autosave still writes while playing (R1 recordings, PING / REV jog on the fader page); kept fader FX continue into event-opened pages until Stop, and the Song page Play toggle back to song mode ends them (stock behaviour); on the Tracker Plus / Mini a clip launch while playing is refused when the pattern has unsaved edits.

**Reverb size and delay feedback caps (v0.2.4, raised in v0.3)**: the top step of the reverb size and the delay feedback crackled on the v0.2.3 hardware test (size 1.0 is an infinite decay, and the 16 bit delay line hard-clipped a loud sustained signal at feedback 0.8 and above). v0.2.4 capped them at 0.90 / 88; **v0.3 raises both to 95 %** ("up to 95 % overall" was the owner request) and makes that clean: besides the delay soft limit, the stock reverb tank (every write inside its loop) and its return, and every write of the ShimmerReverb ring, saturate softly (identity up to 5/8 of full scale, so quiet and moderate sound is bit-identical to before; above that the signal compresses with a smooth knee instead of hard-clipping or wrapping). The fader page tops out at **size 0.95** and **feedback 95**, the engine setters clamp to the same two constants (`core/source/seqFxCaps.h`), so the Master page can show higher numbers but plays at the cap: **projects saved above the cap play at the cap** (nothing in the project is rewritten). The delay feedback write also has a soft limit (linear up to 5/8 of full scale, a quarter of the slope above) instead of the hard 16 bit clip, so a loud sustained signal compresses instead of crackling.

**ShimmerReverb type (the Plinky reverb), deviations from the original code**: shimmer is a Master > Reverb item since v0.3 (0-100 %, default 25 % = the value 32 of 127 that was fixed before) and wobble is fixed at 0.3 (the Plinky firmware has them as controls), the ring writes saturate softly instead of hard-clipping (v0.3), a silence bypass (no input for a ring length and no tail: nothing is computed), size/damp/diffusion/predelay mapped from the Master controls, wet level reduced to about the stock reverb's loudness, and on the Tracker Plus / Mini the pre-delay is 1024 samples (23 ms). Master > Reverb shows "Reverb CPU" (average and peak percent of the audio block period) so Stock and Plinky can be compared on the device.

**Pitch trace, amplitude view, launcher colours (plan 013, v0.3.4, Original Tracker only)**: the PolySynth gets a pitch visualisation: a plain dark panel with a light semitone grid (octave lines stronger, note names at the left), time scrolling right to left over about 4 s, one thin line per voice in the colours of the tracker keys (Note teal, Instrument yellow, FX1 pink, FX2 blue) showing the real pitch (glide as slopes, LFO as wiggles) and a dimmer second line hugging it for the envelope. Open it with **FX2** while the PolySynth parameter overlay is open in Play mode (pads keep playing; the overlay hint line says "FX2 pitch"), or with the screen button **Pitch view** on page 4/4 of the PolySynth instrument page (reach it with Next; it reads **Params** to go back). On the Master key's page 3/3 (Global Mixer) the screen button **Amp view** shows the levels of the 8 sample tracks and the master (v0.3.6, plan 014 B: redesigned): one thin line per track in the key colours and their lighter variants (a dimmer line behind it shows the peaks), the master in grey, dB on a vertical scale (0 to -60), time on an exponential axis (new sound enters at the right edge and is pushed to the left and compressed: marks 1 s, 5 s, 15 s, 60 s, 2 min), header boxes with pattern / step, BPM and title; press the button again to close. The clip launcher uses the same palette (dark cells, thin outlines: teal playing, yellow queued, pink follow, blue pad window). Test list section 11i; design: [`plans/013-pitch-viz-and-colours.md`](plans/013-pitch-viz-and-colours.md) ("As built").

**Strings sequencer type and PolySynth parameter overlay (plan 010 B, v0.3.2)**: in Play mode a track's type list has the entry **Strings** (called PolySynth in v0.3 / v0.3.1; old files load it as Strings): the pads are 12 "strings" (columns, each a 4th higher in a minor scale) of 4 notes (rows); the bottom right pad opens a setup view (root, scale, octave, string spacing; saved with the project). It plays whatever instrument the track has (samples, MIDI or the PolySynth instrument), with the pad pressure as velocity, and recording writes ordinary notes of that instrument; one Strings track at a time (its setup is shared). With a track that plays the PolySynth instrument, press the **Instrument Parameters** key (v0.3.3; or hold the track's screen button and press Instrument) to open a parameter overlay with all voice parameters in five section rows (Oscillator, Filter, Envelope, LFO, Voice): Left / Right choose the parameter in a row, Up / Down the row, the jog wheel changes the value (Shift = fine), Default resets it, the Instrument key closes it; the title shows the voice's CPU share. (A plain Instrument press is the stock key.) Test list section 11g; design: [`plans/010-v0.3-feedback.md`](plans/010-v0.3-feedback.md) ("B as built").

**PolySynth sounds per pattern with a timed morph (plan 012, v0.3.3, Original Tracker only)**: every pattern can have its own PolySynth sound. Dial a sound in on a pattern (Instrument Parameters page of the PolySynth instrument, or the Play-mode overlay): the values are stored with the pattern that plays (or is shown while stopped) and a `*` after a parameter name marks the values that differ from the pattern it inherits from. A pattern without a sound of its own inherits: switching to it changes nothing. When the playing pattern changes (stock pattern switch, song advance, launcher scene, queued Song-page slot, follow action) every continuous parameter moves **linearly** to the new pattern's sound over the **Morph** time (default 6 s, 0 - 30 s in half seconds, 0 = jump; the item sits next to Delay in the overlay's Voice row and on page 3 of the instrument page; saved with the project). Voices, Interval and LFO target switch at the start. A clip launched on a track that plays the PolySynth morphs to the sound of the clip's source pattern (last launch wins). Turning a parameter by hand during a morph takes it out of the morph. The title shows `T2  Morph 3.2s > P12` while it runs. The sounds live in `plinky.bin` after the old block (an older mod reads the file as before and ignores them). Stopped, a pattern change applies the sound at once. Test list section 11i; design: [`plans/012-polysynth-morph.md`](plans/012-polysynth-morph.md).

**Play mode keys (v0.3.3)**: the **Instrument Parameters** key (top left) on a track that plays the PolySynth opens / closes the section overlay over Play mode (the pads keep playing); on any other instrument it is the stock instrument page as before. **Shift + Instrument Parameters** jumps to the next PolySynth track (cycles, brings it into view) and opens the overlay; no PolySynth track: the strip says `no PolySynth`. Tapping the selected track's screen button toggles the pad legend. The overlay also shows the four voices (note name, `~` = releasing, envelope bar). Live notes of the sequencers now play with the instrument's default volume unless the stock record-velocity option is on, exactly like the steps they record (before, live notes used the generator's velocity: too loud). The PolySynth output goes through the master limiter and is ducked by the limiter sidechain like a sample track (the sidechain source stays a sample track / input).

**Third-party notice (reverb type "ShimmerReverb", based on the Plinky reverb by mmalex, MIT)**: `core/libs/Audio/Polyverb/ring_reverb.h` is a port of the reverb of the Plinky firmware, <https://github.com/plinkysynth/plinky_public> (`sw/Core/Src/plinky.c`, `Reverb2`), Copyright (c) the Plinky authors, MIT License; the copyright and permission notice is kept in the file and listed in [`NOTICE.md`](NOTICE.md). The reverb type is called ShimmerReverb in the UI; the name Plinky here only credits the algorithm; this firmware is not a Plinky product, and the Plinky name, logo and design are not part of the MIT grant.

**Third-party notice (synth voice)**: `core/libs/Audio/PlinkyVoice/plinky_voice.h` follows the base synth voice of the Plinky firmware, <https://github.com/plinkysynth/plinky_public> (`sw/Core/Src/plinky.c`, `RunVoice`), Copyright (c) the Plinky authors, MIT License (the full notice is kept in the file and in [`NOTICE.md`](NOTICE.md) section 11); the wavetable numbers in `plinky_wavetable.h` come from `sw/Core/Src/wavetable.h` of the same repository (same MIT notice). The Plinky name, logo and front panel design are not part of that grant; this firmware is not a Plinky product, the name only credits the algorithm.

**License**: the mod files (`seqgen/`, and the new `seq*` files and their headers) are **PolyForm Noncommercial 1.0.0** (see [`seqgen/LICENSE.md`](seqgen/LICENSE.md)). The Polyend files stay under Polyend's [`LICENSE`](LICENSE) (MIT with the CE restrictions).

---

## Notice: Official Support, Warranty & Hardware Disclaimer

> **A Quick Note Before Flashing or Modifying Firmware**
>
> Thanks to the Tracker's isolated bootloader and hardware emergency recovery button (see [Flashing on Hardware & Emergency Recovery](#4-flashing-on-hardware--emergency-recovery)), experimenting with Community Edition builds is very safe. Even if a custom build fails to boot, you can easily re-flash a stable `.ptf` image from your microSD card. That said, we still need to include the standard legal disclaimer just in case:
>
> * **Provided "AS IS" Without Support Obligation**: While the foundation of this repository was originally developed and released by **Polyend**, the **Polyend Tracker Community Edition (CE)** firmware, source code, and any community-developed modifications or plugins are provided **"AS IS"**, without warranty of any kind, express or implied. **Polyend has no obligation to provide official technical support, maintenance, bug fixes, or customer service** for Community Edition firmware or custom builds.
> * **Use at Your Own Risk**: Flashing, modifying, or running custom firmware is done at your own discretion and risk. Polyend is not liable for any data loss, project corruption, malfunction, or hardware damage (including "bricking" a device or issues caused by code modifications) arising from the use of this software.
> * **Out-of-Warranty Repairs Available**: In the unlikely event that something ever goes wrong with your hardware while using custom firmware, it is **not covered under standard warranty**. However, **Polyend is always happy to help get your instrument back up and running through its official out-of-warranty repair service** (standard service, parts, and shipping fees apply. See [How to Contact Polyend Customer Support](https://backstage.polyend.com/t/how-to-contact-the-polyend-customer-support/15510)).

---

## Repository Structure

```text
.
├── Makefile                # Unified top-level build & version management
├── version.mk              # Centralized version & build number configuration
├── CHANGELOG.md            # Release history & feature highlights
├── CONTRIBUTING.md         # Contribution rules & hardware testing workflow
├── LICENSE                 # MIT License (+ No Wholesale Commercial Clones & Precompiled Synth Terms)
├── NOTICE.md               # Third-party open-source attributions
├── core/                   # Shared Tracker application, audio engine, UI & open synth engines
├── platforms/
│   ├── mk66/               # Original Tracker (Kinetis MK66) HAL, drivers & linker scripts
│   └── imx/                # Tracker+ & Tracker Mini (i.MX RT1051) HAL, drivers & linker scripts
├── synth/
│   ├── include/PolySynth/  # PolySynth public headers & Open Synth SDK (synth/include/PolySynth/sdk/)
│   └── lib/                # Precompiled libPolySynth.a & libPolyDSP.a + LICENSE-PROPRIETARY.txt
├── util/                   # Shared C++ utility headers & third-party header libraries (ETL, magic_enum)
├── editor-fx-sdk/          # .ptfx Sample Editor plugin SDK, builder & effects/ sources
└── docs/                   # User guides, SDK tutorials & architecture documentation
```

---

## Guiding Principles of the Community Edition Firmware

This repository prioritizes workflow continuity: our aim is to respect existing muscle memory, introduce features, optimizations and improvements that fit the original vision of the device, while fixing bugs along the way.

If you have ideas for major workflow adjustments or structural changes, please open a [GitHub Discussion](https://github.com/polyend/tracker-ce/discussions) or a topic on the [Polyend Backstage Forum](https://backstage.polyend.com/) first, so we can align on direction together with the community.

For radical experiments or reimagined interfaces, we actively encourage community forks!

---

## Contributing

We welcome community contributions! ❤️

From bug fixes and workflow improvements to new open-source synth engines and `.ptfx` Sample Editor plugins. Please read [`CONTRIBUTING.md`](CONTRIBUTING.md) for our project philosophy, pull request rules, hardware testing workflow, and technical checklist before opening a Pull Request.

---

## Building Firmware

### 1. Prerequisites
* **[GNU Arm Embedded Toolchain `10.3-2021.10`](https://developer.arm.com/downloads/-/gnu-rm)**:
  * Direct official downloads from Arm:
    * **Linux (`x86_64`)**: [`gcc-arm-none-eabi-10.3-2021.10-x86_64-linux.tar.bz2`](https://developer.arm.com/-/media/Files/downloads/gnu-rm/10.3-2021.10/gcc-arm-none-eabi-10.3-2021.10-x86_64-linux.tar.bz2)
    * **Linux (`aarch64` / ARM64)**: [`gcc-arm-none-eabi-10.3-2021.10-aarch64-linux.tar.bz2`](https://developer.arm.com/-/media/Files/downloads/gnu-rm/10.3-2021.10/gcc-arm-none-eabi-10.3-2021.10-aarch64-linux.tar.bz2)
    * **macOS (`x86_64` / Apple Silicon via Rosetta)**: [`gcc-arm-none-eabi-10.3-2021.10-mac.tar.bz2`](https://developer.arm.com/-/media/Files/downloads/gnu-rm/10.3-2021.10/gcc-arm-none-eabi-10.3-2021.10-mac.tar.bz2)
    * **Windows (ZIP)**: [`gcc-arm-none-eabi-10.3-2021.10-win32.zip`](https://developer.arm.com/-/media/Files/downloads/gnu-rm/10.3-2021.10/gcc-arm-none-eabi-10.3-2021.10-win32.zip)
  * Ensure `arm-none-eabi-gcc` is on your `PATH`, extract the toolchain into `./agt/10.3/`, or pass `TOOLCHAIN=/path/to/arm-none-eabi-` to `make`.
* `make` (for building firmware) and `python3` (used by `make ptfx` to package `.ptfx` Sample Editor plugins and by the `make bump-*` version helpers; standard library only).

### 2. Build Commands
From the repository root:

```bash
# Build all three devices (Original Tracker, Tracker+, Tracker Mini)
make all

# Or build a single target:
make original   # -> build/PolyendTracker_SEQ_0.2.ptf
make plus       # -> build/TrackerPlus_SEQ_0.2.ptf
make mini       # -> build/TrackerMini_SEQ_0.2.ptf

# Build reference .ptfx Sample Editor effects into editor-fx-sdk/build/Effects/:
make ptfx

# Clean all build directories:
make clean
```

### 3. Version Management (`version.mk`)
All firmware version numbers are defined in [`version.mk`](version.mk). The **Build** identifier displayed on the hardware under **Config > Firmware > Build** is automatically populated from the short Git commit hash (`git rev-parse --short=7 HEAD`):

```bash
# Show current version, Git build hash & output filenames
make version

# Increment patch version (e.g. 1.0.0 -> 1.0.1)
make bump-patch TARGET=all        # or TARGET=original | TARGET=plus | TARGET=mini

# Increment minor version (e.g. 1.0.x -> 1.1.0)
make bump-minor TARGET=all        # or TARGET=original | TARGET=plus | TARGET=mini
```

### 4. Flashing on Hardware & Emergency Recovery

1. Copy the generated `.ptf` file from `build/` (`PolyendTracker_*.ptf`, `TrackerPlus_*.ptf`, or `TrackerMini_*.ptf`) to the `/Firmware/` folder on your Tracker's microSD card.
   * *Note*: Keep `.ptf` filenames at **31 characters or fewer** so the Original Tracker's 32-byte `/Firmware/_fwinfo` bootloader buffer remains null-terminated.
2. On the device, open **Config > Firmware > Firmware update**, select the `.ptf` file, and confirm.

> **Bootloader Resilience & Emergency Firmware Recovery**:
> * The Polyend Tracker bootloader is isolated from the application firmware:
>   * On the **Original Tracker (`MK66`)**, the bootloader runs on a separate dedicated microcontroller (`kBootloaderTrig`).
>   * On **Tracker+ and Tracker Mini (`IMX`)**, the bootloader resides in a protected region at the bottom of QSPI Flash (`0x60000000`–`0x6002FFFF`), while `.ptf` application builds are linked starting at `0x60030000` (do not modify the flash `ORIGIN` address in the linker scripts).
> * **Emergency Recovery**: If a custom firmware build ever hangs or fails to boot into the UI, you can always recover your device:
>   1. Place a known-good, stable `.ptf` firmware file for your device inside the `/Firmware/` folder on your microSD card (ideally as the only `.ptf` file in `/Firmware/`).
>   2. Insert the microSD card into your Tracker and power it on.
>   3. Use a paperclip or SIM-eject pin to press the **recessed hardware Reset / Emergency Bootloader pinhole button** (located on the bottom of the Original Tracker / Tracker+, or next to the USB-C port on Tracker Mini). The hardware bootloader will automatically pick up the `.ptf` file from `/Firmware/` and flash it.

---

## Documentation & Developer Guides

Full user and developer documentation is available in [`docs/`](docs/Home.md) and on the repository **Wiki**:

* **User Guides**:
  * [Custom Themes & Fonts](docs/custom_themes_and_fonts.md) + [Live Web Editor](https://polyend.github.io/tracker-theme-editor/) ([`polyend/tracker-theme-editor`](https://github.com/polyend/tracker-theme-editor))
  * [Wavetable Envelopes, LFOs & Step FX](docs/wavetable_envelopes.md)
  * [SD Card Image Viewer](docs/image_viewer.md)
* **Developer & Architecture Guides**:
  * [Firmware Architecture, Hardware Limits & Developer Gotchas](docs/developer_guide.md)
  * [Custom Synth Engine SDK](docs/custom_synth_sdk.md)
  * [Modular Sample Editor Effects & `.ptfx` Developer SDK](docs/modular_effects_system.md)

---

## Polyend Tracker Open-Source Ecosystem

This repository is part of a growing open-source ecosystem around the Polyend Tracker family:

* **[`polyend/tracker-theme-editor`](https://github.com/polyend/tracker-theme-editor)** ([Live Web App](https://polyend.github.io/tracker-theme-editor/)): Browser-based visual **Theme & Font Editor** with a hardware screen simulator.
* **[`polyend/tracker-lib`](https://github.com/polyend/tracker-lib)**: Open-source library and tools for reading, writing, and inspecting Polyend Tracker project (`.mt`), pattern (`.mtp`), and instrument (`.pti`) files.
* **[Polyend Backstage Forum](https://backstage.polyend.com/)**: Community forum for sharing custom themes, fonts, `.ptfx` plugins, synth engines, and discussing workflows.
* **[The Tracker Overflow (Discord)](https://discord.com/invite/uEPTnwckJV)**: Unofficial Polyend Tracker Discord where users and developers can join, chat, and find like-minded people.

---

## License

* **Source Code, SDKs, Open Synth Engines (`core/synth/engines/`), `.ptfx` Sample Editor SDK (`editor-fx-sdk/`), Drivers & Utilities**: Licensed under the **MIT License with a Wholesale Commercial Clone Restriction** (see [`LICENSE`](LICENSE)).
  * **Musicians & Creators**: Using the firmware for commercial music production, albums, paid live performances/gigs, sample packs, and monetized streaming/YouTube content is **expressly permitted**.
  * **Developers & Plugin Authors**: Reusing individual source files, drivers, DSP algorithms, UI widgets, or open-source C++ synth engines (`CHIP`, etc.) in your own open-source or commercial projects—and selling `.ptfx` Sample Editor plugins—is **expressly permitted** under the MIT License.
  * **No Wholesale Commercial Clones**: You may **not** sell compiled `.ptf` firmware builds, manufacture/sell commercial clone hardware running the Tracker firmware, or sell a direct port/wholesale clone of the complete Tracker application as a paid software product.
* **Precompiled Synth & DSP Libraries (`synth/lib/libPolySynth.a`, `synth/lib/libPolyDSP.a`)**: Provided under the [Polyend Precompiled Synthesizer & DSP Library License](synth/lib/LICENSE-PROPRIETARY.txt), permitting free use on authentic Polyend hardware as well as personal/DIY non-commercial projects (commercial embedding of the precompiled `.a` binaries is prohibited).
* **Third-Party Components**: See [`NOTICE.md`](NOTICE.md).
