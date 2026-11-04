<!-- Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md) -->
# Sequencer tracks — on-device test list (Play mode, Record, save/load, Session CLIPS + CUT, alt Performance page)

Original Tracker (MK66) and Tracker Plus. There is no simulator, so these checks are the only proof. Flash the `.ptf`, load or create
any project, and go through the list in order. Write down anything that differs. Recovery if something is badly wrong: reset pinhole
and flash a known-good `.ptf` from `/Firmware/`. Section 12 (stock behaviour) is the regression check: run it last, and again after
any change.

## Start here: the highest-value on-device tests
Do these first (the full list below is the detail):
1. Shift+Rec a loop in Play mode, Stop, go to Performance, then Pattern, reopen the project: the whole recording is there.
2. Record on track 3, go to Session, launch another clip on track 3, then launch the own scene: the recording is intact.
3. Launch clips, wait 15 s, Save, Save As, power cycle: the original pattern files are unchanged and edits on other tracks are kept; also in song mode.
4. A project that never used the mod behaves like stock CE (section 12).
5. CUT jumps on track 1 while Chord/Arp runs elsewhere: the arp stays on the bar.
6. External MIDI clock start / stop / cable pull with Chord/Arp + Mono: no hung notes, a recording ends.
7. Clips running, visit Master, come back: the clips keep playing.
8. Reflash the stock .ptf to confirm the recovery path.

Contents: 1-6 Play mode | 7 Record | 8 save / load | 9 Session CLIPS | 10 Session CUT | 11 alt Performance page | 12 stock-behaviour checks.

## 1. Enter / leave Play mode
| Press | Expected |
|---|---|
| On the Pattern page tap **Pattern** (press and release, nothing else) | Title bar "Play mode". Middle of the screen: "Track N: Off" and three hint lines. The 8 bottom labels show `[1 -]` `2 -` ... (track number, type letter, `[ ]` around the current track). |
| Hold **Pattern** for about half a second, release (nothing else) | Stays on the pattern editor: only a quick tap (up to 400 ms) opens Play mode. |
| Open a note / FX / instrument popup on a step, then tap **Pattern** | The popup closes (as before); Play mode does NOT open. Tap **Pattern** once more: Play mode opens. |
| Reach the Pattern page without pressing Pattern (e.g. close a menu / come back from Song with another button), then tap **Pattern** once | Play mode opens at the first tap. |
| Pattern + arrow keys / Pattern held with other buttons | Same as stock, never Play mode. |
| Tap **Pattern** again | Session view (section 9; the cycle is Editor > Play > Session > Editor). Tap **Pattern** once more: back to the normal pattern editor, as before (cursor, view). Tap again: Play mode again. |
| In Play mode press Params / Song / File ... | The module opens as usual. Pattern from there: pattern editor (not Play mode). |
| Leave Play mode while a chord pad is held | The chord stops (no hanging note). |

## 2. Jump to a track
| Press | Expected |
|---|---|
| Screen buttons 1-8 | Brackets move to that track, title shows "Track N". |
| Hold **Shift** | Labels show 9-16; **Shift + screen button** selects tracks 9-16. Release Shift: labels back to 1-8. |
| Hold a pad, then press another screen button | The first track releases its pad (chord stops); the pad LEDs switch to the new track's layout. |

## 3. Pick a sequencer type (Instr + jog wheel)
| Press | Expected |
|---|---|
| Hold **Instr**, turn the jog wheel | A list Off / Chord/Arp / Mono appears over the current track's screen button; `>` marks the selection. |
| Release **Instr** (transport stopped) | The type applies at once: the label shows `C` (Chord/Arp) or `M` (Mono), `-` for Off. |
| Start the pattern (Play), hold Instr + jog to another type, release | Info line "Next bar: ..." ; the label changes on the next bar line (every 16 steps counted from Play). |
| Choose the type the track already has | Cancels a pending change. |
| Other tracks | Unaffected. |

## 4. Chord/Arp live (track type `C`, transport stopped first, then playing)
Use a MIDI track (9-16) with a synth/hardware on its channel, and a sample track (1-8) with a sample instrument.
| Press | Expected |
|---|---|
| Hold a root pad in the left 6 columns | Chord sounds (needs a moment: next quarter at most). Label shows `C*`; info line "Sequencer plays this track (pattern muted)". |
| Release the pad | Notes stop, `*` disappears. No hanging notes. |
| While the pattern plays a note pattern on that track: hold a root pad | Pattern notes on that track go silent while you hold; other tracks keep playing. Release: the track's pattern is back at the next step. |
| Panel pads (cols 7-10), SETTINGS pad (col 12, row 1) | Chord / arp settings change as documented in plans/001 (types, T/S/D/Z rows). Pads show LEDs. |
| Sample track | The chord is monophonic on a sample track (last note wins). MIDI tracks send every chord note. |
| Stop the transport while holding a pad | Chord keeps sounding (free-running clock) until released. |

## 5. Mono live (track type `M`)
| Press | Expected |
|---|---|
| Stopped: tap pads in the left 8 columns | LEDs show the notes (steps). No sound while stopped, no `*`. |
| Press Play | The sequence plays on that track at 1/16; `M*`; the track's own pattern notes are muted. Playhead moves on the pads. |
| CLEAR pad (column 12, row 1) | The sequence is cleared, the note stops, `*` goes, the pattern plays again on that track. |
| Stop | Notes stop immediately, `*` goes. |
| Leave Play mode while Mono plays | Mono keeps playing (set the track to Off to silence it). |

## 6. Misc
| Check | Expected |
|---|---|
| Chord on a sample track (triad) | One note sounds (the lowest), without stutter or retriggering; it ends cleanly on release. A MIDI track plays all notes. |
| Muted track | A muted track makes no sound from the sequencer either. |
| Two Chord/Arp tracks | The lower-numbered one supplies the scale to Mono tracks (C major if none). |
| Transport running, Mono track with steps set while another Chord/Arp track already runs: change the type of a third track | The change happens at the start of a pattern bar (every 16 steps), not 16 steps after Play was pressed. |
| Reboot | Track types and sequencer settings come back (see section 8). |
| Pattern playback timing with some tracks set to Mono/Chord | Pattern timing of the other tracks is steady (no hiccups). |
| **External MIDI clock** (Config: clock source external, a drum machine as master). Transport stopped, hold a root pad on a Chord/Arp track, release it | The chord stops at the release, the pattern comes back. Pad releases never wait for a clock pulse. |
| External clock: Mono plays, then the master stops (Stop, or just pull the clock) | Mono goes quiet within half a second (no hanging note). Starting the master again resumes it. |
| External clock: start a punch-in Rec, then stop the master | The recording ends (info line clears). |

Known limits of Play mode: no parameter editing on the jog wheel (only the pads). Recording is section 7, saving section 8.

---

# Record (section 7) and save / load (section 8)

Prepare: a project with an empty pattern (or any pattern, you will see it being replaced), one **MIDI track** (9-16) with a synth or
hardware on its channel and one **sample track** (1-8). In Play mode give the MIDI track Chord/Arp and the sample track Mono.
All recording happens on the **current track** of Play mode (the bracketed one). Stock Rec outside Play mode is unchanged.

## 7a. How to get a 3- or 4-note chord (default is ONE note)
A new Chord/Arp track starts in the c31 default: chord type 1 = a single note (the root), size 5. Holding a root pad therefore sounds
one note until you pick a chord type. In the panel (columns 7-10, TYPES panel is the default; counting columns from 1 and rows from 1
at the top-left pad of the screen) press once, before or while holding a root:
| Pad (column, row) | Chord |
|---|---|
| 8, 1 | two-note (1-2) |
| 9, 1 | triad (1-3-5): **press this one for a 3-note chord** |
| 10, 1 | sus-like triad (1-4-5) |
| 7, 2 | 1-3-5-6 (four notes) |
| 8, 2 | 1-3-5-7 (**4-note chord**) |
| 9, 2 | 1-3-5-7-9 (five notes, needs size 5; the recorder keeps the first four that match the Tracker chord table) |
Then hold a root pad in columns 1-6. The selection stays when you release (it is saved with the project).

## 7. Record in Play mode
| Press | Expected |
|---|---|
| Rec on a track whose type is Off | Info line "Pick a sequencer type first (Instr + jog wheel)". Nothing records, the transport does not start. |
| Transport stopped, Chord/Arp track: press **Rec** | The transport starts at step 1 and recording starts at once. Info line "REC track N: punch-in (Rec = end)". |
| (Triad chosen, see 7a) Hold a root pad for a few steps, release | The chord keeps sounding while held (Live). Press **Rec** again: the info line clears. Leave Play mode (tap Pattern): the pattern page shows a note with the chord FX (`MIDI chord`) on the first step of the chord, steps you passed with no chord are empty, **even where the pattern had notes before** (replace). |
| Quantise | A note that starts in the first half of a step (before the middle) lands on that step, in the second half on the next step (like stock live record). With rec microtiming ON in the Config the note stays on its step and gets a micromove value instead. |
| Velocity | With rec velocity ON in the Config the step volume follows the sequencer velocity (the c31 groove), otherwise the volume stays default. |
| Undo | In the pattern editor press the stock **Undo**: the whole recording disappears in **one** step and the previous pattern is back. Redo brings it back. |
| Punch while Mono plays | Mono track: press Rec while the sequence runs: its notes replace the steps one by one; steps without a Mono note are cleared. Rec again: stops, the rest of the pattern is untouched. |
| Chord on a **sample** track (Chord/Arp type on tracks 1-8) | Only the lowest chord note is written. Info line "Chords need a MIDI track (lowest note recorded)". |
| Chord that is not in the Tracker chord table (a 5-note chord, wide voicing) on a MIDI track | The highest notes are dropped until the table matches, the step gets the shorter chord FX. Info line "Chord reduced to fit the Tracker chord table". |
| Record a very busy arp (fastest speed, 1/32) for a long time | Normally nothing special. If the info line ever shows "Rec overflow: some steps were not recorded", note what you did (it means the main loop was too slow to take the steps). |
| Stop the transport while recording | Recording ends by itself. Steps written so far stay, one undo step. |
| End of a recording (Rec, loop end, stop) while the last note is still held/open | A note-off is written on the next step that holds no note, so the last note does not ring into the old pattern content; a step that already has a note is left alone. |
| Change the pattern (Song mode) while recording | Recording ends; nothing is written into the new pattern. |
| Set the recorded track to Off (Instr + jog) while recording | Recording ends when the change applies (next bar). |
| Rec held in other pages | Outside Play mode Rec is the stock Rec (edit mode). Leaving Play mode (tap Pattern, any other module button) ends a running recording (punch-out note-off as usual); the pattern is not touched while you edit it. |

## 7b. Shift + Rec: exactly one loop
| Press | Expected |
|---|---|
| Transport running, Chord/Arp or Mono track: **Shift + Rec** | Info line "REC track N: waiting for the loop start". Nothing is written until the pattern wraps to step 1. |
| Wait for the wrap | At step 1 the info line changes to "one loop, ends by itself"; the sequencer's notes replace every step of the loop. |
| At the next wrap | Recording stops by itself, info line clears. The whole loop is one undo step. A note near the end of the last step that would quantise to step 1 is NOT written to step 1. |
| Transport stopped: **Shift + Rec** | The transport starts at step 1 and the loop starts at once. |
| Press **Rec** twice quickly (double tap) | Recording starts and ends again (toggle); it does not restart. |
| **Rec** (without Shift) while armed or recording | Ends it at once (what was written stays). |

## 8. Save / load with the project
| Check | Expected |
|---|---|
| Set Chord/Arp on track 10 and Mono on track 3, change some settings (SETTINGS pads) and set Mono steps. Save the project (File > Save). Power off, power on, open the project. | Play mode shows `C` on track 10 and `M` on track 3; the settings and the Mono steps are back. Pads/LEDs show them. |
| Wait ten seconds after a change (autosave), power off without saving, power on | The Workspace is restored by the stock autosave: the sequencer state of the last autosave comes back with it (a change shorter than ~1 s before power-off may be missing). |
| Open a different project that never used a sequencer | All tracks Off (`-`). The previous project's sequencers are gone. |
| New project | All tracks Off. |
| Copy `seqtracks.bin` out of the project folder, delete it, open the project | Opens normally, all tracks Off (stock behaviour). Restore the file: the sequencers are back. |
| Damage the file (open it in a hex editor, change one byte, or cut it to 100 bytes) | The project opens normally, all tracks Off, no error message. |
| Open the project on stock CE (or the upstream firmware) | Opens normally; the extra file `seqtracks.bin` is ignored. The recorded notes (and chord FX) are ordinary pattern data and play. |
| A project that never used a sequencer: save it | No `seqtracks.bin` is created in its folder. |
| Save as (new name) | The new folder gets its own `seqtracks.bin`. |
| Project A used a sequencer. Open project B (never used one), Save as **over** the existing folder of A | A's old `seqtracks.bin` is removed from that folder (no stale sequencers come back when A's folder is opened). |
| Save (same name) of a project whose `seqtracks.bin` is unreadable (damaged / newer version) and that you never used a sequencer in | The file is left as it is. |

Known limits of step B: the sidecar is rewritten about once a second after a change (only while the Workspace autosave runs); Rec is
not written to patterns other than the one you started in; sample-track chords keep the lowest note only; no pad-velocity record.

# Session view: CLIPS (section 9) and CUT (section 10)
Reached with the Pattern tap cycle **Editor > Play > Session > Editor**. Screen buttons 7 / 8 switch between the CLIPS and the CUT layer; all 12 x 4 pads belong to the active layer. A *clip* = one track of one pattern, a *scene* = a whole
pattern. Needs patterns 1-8 (or any four in a row) that hold different notes on a few tracks, saved in the project (leave the pattern
page once so the Workspace files exist). Tracks that have no notes in a pattern are empty clips.

## 9. Session view, CLIPS layer
Layout (pads 12 x 4, 1-based): columns 1-8 = the 8 tracks of the group, rows 1-4 = 4 patterns of the window; column 9 = SCENE (one per row);
column 10 row 1 = STOP-T, row 4 = STOP-A; column 12 row 1 = QUANT. Screen buttons: 1-2 = group, 7 = CLIPS, 8 = CUT.
| Press | Expected |
|---|---|
| Play mode, tap **Pattern** | Title bar "Session view", title "Session: CLIPS   tracks 1-8". Pad LEDs: playing clips bright (the loaded pattern's row), clips that hold notes dim, empty clips off, scene pads of the loaded pattern bright. Bottom labels: `[1-8]` `9-16` and `[CLIPS]` `CUT`. |
| Tap **Pattern** again | Back to the pattern editor. A project that never entered Session plays exactly as before. |
| Screen button 2 | Group 9-16: title "tracks 9-16", the 8 pad columns show tracks 9-16. Button 1 = tracks 1-8. Buttons 3-6 do nothing. |
| Turn the jog wheel | The pattern window scrolls by one pattern per detent (Shift + jog = 4); info line "Patterns 5-8"; the LEDs show the new rows after a moment (the files are read one per pass). Not below 1, not above 255. |
| Transport running. Tap a dim clip (track T, pattern P) | The pad blinks; at the next **bar** (16 steps of the global position) track T plays its part of pattern P from its first step, the pad turns bright and the old clip of that track turns dim. Info line "Now ... T:P"; while queued "Next T>P". Other tracks keep playing. |
| Quantise: tap the QUANT pad (col 12 row 1) | Cycles next bar (dim) > pattern end > immediate (bright). Launch at the pattern end / at the next step of the track for "immediate". |
| Tap a clip while the pad of another clip of that track blinks | The newer launch wins. |
| Clip with a different length than the loaded pattern | The track loops with its own length (stock Performance behaviour). The pattern page title then shows 4444 (stock Performance marker). |
| Hold **STOP-T** and tap a clip pad of track T | Its pad blinks, at the quantise point track T goes silent (a held note ends) and no clip is bright for it. Info "Now T:--". Tap any clip of T: it plays again. |
| Tap an **empty** (off) clip pad | Stops that track like STOP-T. |
| **STOP-A** (col 10 row 4) | All 16 tracks stop at the quantise point. |
| Tap a **scene** pad (col 9) | The scene pad blinks; at the pattern end the whole pattern switches (stock pattern switch, like Pattern + left / right), all clip states reset to "the loaded pattern", stopped tracks play again. Scene launch is always at the pattern end whatever QUANT says. |
| Transport stopped: tap a clip / scene / STOP | Takes effect at once (nothing to wait for). Press Play: it plays. |
| Stop the transport with launches queued | Queued clips take effect, stopped tracks play again when you restart. |
| Screen button 8 (**CUT**) | Title "Session: CUT   tracks 1-4", the CUT pads (section 10), buttons 1-4 = tracks 1-4 / 5-8 / 9-12 / 13-16. Button 7 (**CLIPS**): back, clip states unchanged. Held pads are released when you switch. |
| Pattern file safety: launch a clip on a track, wait 15 s (autosave), launch a scene or switch pattern, look at the original pattern | The original pattern is unchanged (launched clips were not saved into it); edits you made to other tracks of it are kept. |
| Launch own pattern's clip on a track that has a queued clip | The queued clip is cancelled, the track keeps/returns to the pattern's own part. |
| Scene pad of the loaded pattern while clips run | At the pattern end all tracks are back on the loaded pattern. |
| Leave Session (any module button) with clips running | They keep running. Hold no pad: nothing hangs. |
| Open the pattern page and edit while clips run | **Known limit**: the pattern in RAM holds the launched clips; do not save/edit seriously until a scene launch or a pattern switch has reset it. |
| Chord/Arp or Mono track taken over (Play mode) and a clip is launched on that track | The sequencer keeps priority while it takes over; the clip plays once it ends. |
| Busy messages | "Busy - try again" appears when a pattern load / queued pattern switch is in progress. Tap again. |

Behaviour since the review fixes (M2, M3, S3, S6): a clip launch first writes the loaded pattern's unsaved edits to its file ("Busy" and no launch when the file manager cannot do that now); a pattern file that cannot be read aborts the save (the changed flag stays, nothing is overwritten) and the launched tracks are never cleared by a read error; Pattern + arrows and scene launch refuse with "Busy" while clips run and the save did not happen; leaving Play mode or Session view and the end of a recording ask for the project autosave (like leaving the stock editor); leaving to Master or Instrument keeps the Session clips playing (no turn-off of performance mode), and entering the stock Performance page while clips run keeps their lengths; undo history is cleared when the last launched clip goes away.

Known limits of C1: undo snapshots hold the RAM pattern; the "has notes" LEDs read the Workspace pattern files (an unsaved edit on a non-loaded pattern is not visible); clips
are copied into the loaded pattern in RAM (stock Performance mechanism); saving writes those tracks from the pattern file, so edits to a launched clip are lost.

## 10. Session CUT (MLR playhead, plan 002 S2)
Open it with Pattern (Play mode) > Pattern (Session) > screen button 8 (CUT). Title "Session: CUT   tracks 1-4"; screen buttons 1-4 choose the four tracks shown (1-4, 5-8, 9-12, 13-16); the info lines explain the pads. Use a project with: track 1 = ONE long
loop sample on step 1 only (e.g. a 2 s drum loop, pattern 16 steps, tempo so the loop fits the 16 steps, note at the sample's
original pitch, play mode One-shot or Loop), track 2 = a normal drum pattern, track 3 = a MIDI track with a few notes.
Pad grid of the layer (all 12 columns, none reserved): 4 rows = 4 tracks of the group; pads 1-8 of a row = 8 positions; pad 9 = STOP/START,
10 = REVERSE, 11 = RATE (1/2 -> 1 -> 2), 12 = RESYNC.

| Press | Expected |
|---|---|
| Layer open, transport running, no pad pressed | Tracks play exactly as in the pattern editor (a track the layer never touched is stock). The playhead LED of every row follows the track (bright), the rest of the row is dim. |
| Press position pad 5 on the loop-sample row | At the next step the loop sample jumps to the middle of the sample and keeps playing from there (not from its start, not silent). Then it carries on forward and wraps with the pattern. |
| Press pads 1, 3, 5, 7 one after the other | The sample is cut to the matching positions each time. |
| Press a pad on the drum row | The drum pattern jumps to that position; a drum hit on the target step plays, otherwise the drum track sounds like the stock pattern would at that point (the last note before the position is retriggered with the offset, so a long kick tail is cut in). |
| Jump on the MIDI row to a step without a note | The last MIDI note before that position is played again (no offset). Jump to a position after a note-off: silence. |
| Hold two pads of a row (e.g. 2 and 4) | A loop over positions 2..4 starts at the next step; the pads between are lit as loop. Release one pad: the loop ends and the row runs on from where it is. |
| Hold three pads | The loop covers the outer pads. |
| STOP pad (9) of a row | The row goes silent at once (sounding note stops); the pad LED is bright, the row's position pads go dark. The other rows keep playing. Press again: the row starts again at the global position (RESYNC). |
| REVERSE pad | The row plays its positions backwards (each step replays the sample from the matching offset, forward). Press again: forward. LED bright while reversed. |
| RATE pad | 1 press = double (the row steps through the pattern at 2x; two triggers per step), 2 presses = half (each position lasts 2 steps, nothing triggers on the second), 3 = normal again. |
| Jump around, then RESYNC pad | The row snaps back to the global position (retriggering the sample at that offset); loops, reverse, rate, stopped state and a recorded loop of the row are cleared; the row is stock again (RESYNC LED dim). |
| Hold Rec, jam jumps on row 1, release Rec | While Rec is held the RESYNC pad of the touched row blinks (recording). On release the row repeats the jumped sequence, step for step, forever; RESYNC LED bright = a recorded loop replays. Rows you did not touch while Rec was held are not changed. |
| With a recorded loop running: switch the group (rows show other tracks), switch layers / pages | The recorded loop keeps playing. Switch back: LEDs show it. |
| Press a pad on a row with a recorded loop | The pad overrides the recording while it is held (jump, loop); on release the row returns to the recorded loop in phase. |
| Stop and start the transport | Cut rows with a recorded loop restart the loop with the pattern; rows with only a jump/loop/reverse/rate keep that setting and restart at step 1. |
| Song mode / pattern chain with a cut row | The song advances at the real pattern end (not at the cut position); the cut row keeps its own position inside the new pattern (clamped to its length). Check: the next pattern loads and the song does not hang or skip. |
| Rate 1/2 on a row with a micromove (delayed) note | The delayed note still fires; the row never goes silent for good. |
| STOP a row, then START | The row is silent while stopped, restarts on the global position; a note you hold while live recording is not cut off. |
| Open another project / New project while rows are cut | All rows are stock again. |
| Track with a Slice / Beat-slice / Granular / Wavetable instrument | Jumps retrigger the note but without an offset (Position FX means something else there). |
| Long sample streamed from the SD card (>= 256 KB, original Tracker) | Jumping past the first ~0.7 s may start late or with a short gap (the stream buffer refills on the main loop). Note how long the gap is and whether the main-loop redraws make it worse (listen on the loop track while a screen redraws). |
| Pad jumps + an FX-heavy step (note step with 2 FX in use) | The retriggered note loses its second FX unless that is Slice / Reverse / Granular length (then it is replayed without offset). |
| Save the project, reload | CUT state is NOT saved (v0.1): all rows are stock after loading. Recorded playhead loops are lost with the power-off. |

Known limits (S8, S9): recording on a CUT-active or launched track is not supported in v0.1 (a launched track refuses Rec with "Clip launched - not recorded"; on a CUT-active track the written steps are odd); a CUT release (pad up) that was lost because the queue was full is retried only while the Session view is visible.

Known limits of the CUT layer v0.1: the offset assumes the sample plays at its original speed; the offset has full 16-bit resolution and follows the note pitch / tune;
reverse reverses the playhead order only (samples still play forward); a pattern switch (song mode / pattern chain) does not reset a cut row;
selection looping and Performance-mode track lengths are ignored by cut rows; swing is ignored for the rate-2 half step.

### 10b. CUT together with the clip launcher (integration)
Setup: patterns 1-4 with different notes on tracks 1-3 (CLIPS needs them), track 1 = the loop sample as above.
| Press | Expected |
|---|---|
| CUT: jump around on row 1 (track 1), switch to CLIPS (button 7), launch a clip (other pattern) on track 1 at the next bar | At the bar the track plays the new clip from its first step; the CUT playhead follows (jump pads count from the new clip's step 1 on). No stuck note, no silence. |
| CUT: STOP track 1 (pad 9), then CLIPS: launch a clip on track 1 | The CUT stop ends: the clip plays (a launch means "play this"). |
| CLIPS: hold STOP-T and tap a clip pad of track 2 (it stops at the bar). CUT, row 2: press position pads | Track 2 stays silent: Session STOP wins over CUT, the position pads do nothing audible, the playhead LED of row 2 stays where it was. |
| Then CLIPS: tap any clip on track 2 | Track 2 plays the clip again; the CUT position follows the clip's step 1. |
| CUT row with a recorded loop, CLIPS: STOP-A, then launch clips | While stopped all tracks are silent and the CUT loops do not run on; after a launch the track plays again. |
| CLIPS: launch a scene while CUT rows are active | At the pattern end the new pattern plays; CUT rows keep their settings (position inside the new pattern, clamped). |
| In CUT hold two pads (loop), then press screen button 7 (CLIPS) | The loop ends at once (pads are released by the layer switch). Rec held while switching: the take is closed. |
| Leave the Session view (any module button) while a CUT pad is held / Rec is held | Nothing hangs: loop ends, take closed. |

# Alt Performance page (8-step faders) — plans/004-perform-faders.md

Provisional parts (owner did not specify): LATCH / RESET pads, jog-wheel settings, the steps' value mapping.

## 11. Alt Performance page (double tap on Performance)
Setup: a project with tracks playing; on the stock Performance page give the slots a few FX (e.g. slot 1 low-pass, slot 2 sample volume,
slot 5 pan, slot 9 tune).

| Press | Expected |
|---|---|
| **Performance** (tap, from any page) | Stock Performance page opens (as always). |
| **Performance** tapped ONCE | Stock behaviour: the tracks page flips (tracks 1-8 / 9-16) as always. |
| **Performance** double tap (second tap within 350 ms, nothing else touched, no pad held) | Fader page (the tracks page is unchanged, the two taps flipped it twice). Title "Perform faders: FX 1-4"; pad rows 1-4 lit only where a slot has an FX; group pad FX1-4 (col 9, row 1) bright, FX5-8 / FX9-12 dim, row 4 of column 9 dark; PING (col 10 row 1) and REV (col 10 row 2) dim; LATCH column dim, RESET column dim. |
| **Performance** single tap on the fader page | Back on the stock Performance page; the 8 buttons keep their state; tracks page is the one you had; FX that were playing from the stock pad rows still sound and their pads are lit. |
| Leave the fader page for Pattern, then open the stock Performance page again | A fresh stock page (no old FX sounding, pad rows reset) - nothing leaks from the fader page. |
| Hold a pad, tap Performance twice | No fader page (a held pad is not a tap). |
| **Performance** held, then another button / a pad / the jog wheel, then release | Stock behaviour only (tracks page flip; Shift + Performance = mute view), the fader page does NOT open. |
| Never tap twice: use the stock page for a while | Everything works exactly as before (pads, jog, edit mode, tracks page). |

## 11b. Perform FX on the faders (FX1-4 / FX5-8 / FX9-12)
| Press | Expected |
|---|---|
| Select tracks 1 and 2 with the 8 buttons under the screen (label shows `[1]` `[2]`) | Selected tracks are the ones the FX affect (same as the stock page). Shift + button = tracks 9-16. |
| Hold pad 5 of fader 1 (row 1) | Bar lit from the left up to pad 5; info line shows the FX name and value; the FX is heard on tracks 1 and 2 only. |
| Slide to pad 8 | Value rises to the FX maximum; pad 1 is about 1/8 of the range (a signed range gives 0 on pad 4). |
| Release | Bar goes dark and the FX is cleared at once (momentary, like stock). |
| Hold pad 3 of fader 2, then also pad 6 (two pads), release pad 6 | Value falls back to the still-held pad 3 until the last pad is released. |
| Stock page: press a pad of slot 1 (the stock page latches its row), double tap Performance | Fader 1 shows the bar for that value and the FX keeps playing. Pressing a fader pad changes it; release returns to the stock value (not to silence). RESET of row 1 clears the FX and the stock pad row (dark on the stock page too). |
| Fader FX and a stock slot with the same FX | The newer fader takes over (stock rule); the older one goes dark. Releasing a fader re-applies the other active faders / sends. |
| Song mode, pattern switch while on the fader page; Shift + Play | Same as the stock page (track patterns follow the song; Shift + Play starts the song / toggles loop). MIDI CC 41-48 / 51-62 (perform CC) are NOT handled on the fader page (known limit). |
| Tap **LATCH** of row 1, then press and release pad 5 of fader 1 | FX stays; bar stays lit; info line shows LATCH. |
| Tap **RESET** of row 1 | FX cleared, LATCH off, bar dark. |
| LATCH on, then tap LATCH again | FX cleared when no pad is held. |
| With a latched FX, select track 3 with its button | The FX also reaches track 3. Deselect track 1: its FX ends. |
| Tap **FX5-8**, **FX9-12** | The four faders now control slots 5-8, 9-12; the group pad is bright. An empty slot has a dark row. Latched FX on the old page keep playing. |
| Hold a fader and tap another group pad | The held fader is released (unless LATCH). |

## 11c. PING (delay) and REV (reverb)
| Press | Expected |
|---|---|
| Tap **PING** | Title "PING (delay)"; faders show: 1 delay send (dark until used), 2 time (or rate when sync is on), 3 feedback, 4 filter - bars 2-4 show the current project values (default time 500 ms = pad 3, feedback 50 = pad 5, filter 20000 = pad 8). |
| Hold pad 6 of fader 1, release | Delay send of the selected tracks goes up (+63) while held, back to normal on release (LATCH keeps it). |
| Press pad 2 / 5 / 8 of fader 2 (sync off) | Delay time changes (about 219 / 1367 / 3500 ms) and STAYS after releasing; the info line shows ms. |
| Jog wheel right / left | Ping-pong on / off (line 5 shows it); audible stereo bounce. |
| Shift + jog wheel right | Sync on: fader 2 is now the rate (info line "Rate 1/8" etc.), pad 1 = "8", pad 8 = "1/32". Shift + jog left = sync off. |
| Fader 3 pad 8 | Feedback 99 (long repeats); fader 4 pad 1 = about 194 Hz (dull repeats). |
| RESET of row 2/3/4 | Time 500 ms (or rate 8), feedback 50, filter 20000 Hz. |
| Tap **REV** | Fader 1 reverb send of the selected tracks; 2 size, 3 damp, 4 predelay (0-100); jog wheel = diffusion; line 5 shows it. RESET returns the defaults (50 / 50 / 50). |
| Change PING / REV values, save the project, power off, open it | The values are saved with the project (same as changing them on the Master page); open the Master page: it shows the same values. |
| No volume or panning on this page | Correct (owner decision). |

LATCH pads of PING / REV rows 2-4 are dark and do nothing (project values always stay). Leaving the fader page (any other page): fader FX and sends are released and latches cleared; FX from the stock pad rows stay while the target keeps the perform FX (Master, Params, stock page), otherwise everything ends like on the stock page. PING / REV project values stay.
Known: delay time jumps (no smoothing in the delay engine, same as the Master page), so a press may click while playing.
Known limits: FX that exist only for synth tracks / global FX use the stock slot rules; two slots with the same FX exclude each other
(stock rule), the older fader bar may stay lit.

# Stock-behaviour checks (section 12) — run last, and after every change
A project with no sequencer set and no Session use must behave exactly like stock CE.

## 12. Stock behaviour (never enter Play mode, Session or the fader page)
| Press | Expected |
|---|---|
| Open a project you know, play it (Play), stop, edit steps, record, switch pages | Exactly as stock CE. |
| Hold **Pattern** + Left / Right / Up / Down (8-track view and others) | Same as stock; the screen does NOT switch to Play mode when you release Pattern. |
| Hold **Pattern**, turn the jog wheel or press a pad, release Pattern | Nothing new happens. |
| Hold **Shift** (or any button), tap **Pattern** | No Play mode. |
| Come from another page (e.g. Song) by pressing Pattern | Pattern page opens, no Play mode (the first press only switches pages). |
| Open a project, tap Pattern twice (Play mode, Session) and back to the editor without touching any pad; Save | No `seqtracks.bin` is created, the pattern files are unchanged, playback is as before (Session alone changes nothing until a clip is launched). |
| Tap Performance once, play with the stock Performance page | Exactly the stock page (the alt fader page only opens on a double tap). |
| Project with CUT used (rows cut, recorded loop), save and re-open | CUT is not saved in v0.1: all rows stock after loading; `seqtracks.bin` is only written when a sequencer type is set. |
