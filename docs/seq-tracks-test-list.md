<!-- Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md) -->
# Sequencer tracks — on-device test list (Play mode, Record, save/load, Session CLIPS + CUT, alt Performance page)

Original Tracker (MK66) and Tracker Plus. There is no simulator, so these checks are the only proof. Flash the `.ptf`, load or create
any project, and go through the list in order. Write down anything that differs. Recovery if something is badly wrong: reset pinhole
and flash a known-good `.ptf` from `/Firmware/`. Section 12 (stock behaviour) is the regression check: run it last, and again after
any change.

## Start here: the highest-value on-device tests
Do these first (the full list below is the detail):
1. Shift+Rec a loop in Play mode, Stop, go to Performance, then Pattern, reopen the project: the whole recording is there.
2. Record on track 3, go to the launcher (Song double tap), launch another clip on track 3, then launch the own scene: the recording is intact.
3. Launch clips, wait 15 s, Save, Save As, power cycle: the original pattern files are unchanged and edits on other tracks are kept; also in song mode.
4. A project that never used the mod behaves like stock CE (section 12).
5. CUT jumps on track 1 while Chord/Arp runs elsewhere: the arp stays on the bar.
6. External MIDI clock start / stop / cable pull with Chord/Arp + Mono: no hung notes, a recording ends.
7. Clips running, visit Master, come back: the clips keep playing.
8. Reflash the stock .ptf to confirm the recovery path.
9. R2: Song double tap opens the launcher; with the song playing launch a clip (LIVE, song frozen), BACK:SONG resumes; a Play 1x clip chains into the next row; Stop = panic (section 9, 9b, 9c).

Contents: 1-6 Play mode | 7 Record | 8 save / load | 9 Session CLIPS | 10 Session CUT | 11 alt Performance page | 12 stock-behaviour checks.

## 1. Enter / leave Play mode (v0.2: Play mode is the 4th action-button page of the pattern editor)
Naming: the bottom-right key printed "Perform" is the firmware's **Pattern** key. The pattern view (tracks, playhead, title bar, mutes) stays on screen in Play mode; only the 8 labels under the screen and the pads change.
| Press | Expected |
|---|---|
| On the Pattern page tap **Pattern** (press and release, nothing else) | The pattern view stays. The 8 bottom labels become the strip: line 1 `1 ---` ... (track number and type: `---` Off, `CHRD`, `MONO`; `*` after it while that track is taken over), line 2 the state (`pattern` for an Off track). The selected track is in brackets, e.g. `[1 ---]`, and framed in the pattern view. No "Play mode" screen, no extra text in the middle. |
| The pattern editor was in edit mode (red) | Play mode switches it off (green); Rec is the sequencer's Rec now. |
| Tap **Pattern** again | Back to the normal pattern editor on the action-button page you entered from (Pattern / Length / Step ... or the other pages), the stock pad lights; edit mode stays off. |
| Tap **Pattern** while a fill popup / menu / note keyboard is open | Play mode does not open and the title bar flashes red (the tap was refused). |
| Enter Play mode with the cursor on track 7 | The pattern view shows tracks 5-8 with the frame on track 7 at once. |
| Hold **Pattern** for about half a second, release (nothing else) | Stays as it was: only a quick tap (up to 400 ms) toggles Play mode. |
| Open a note / FX / instrument popup on a step, then tap **Pattern** | The popup closes (as before); Play mode does NOT open. Tap **Pattern** once more: it opens. |
| Pattern + arrow keys / Pattern held with other buttons | Same as stock, never toggles Play mode. The title bar flashes red once for a refused quick press. |
| Hold a pad (a chord in Play mode), tap **Pattern** | The tap still toggles (pads do not block it). Leaving Play mode releases the held chord. |
| In Play mode press Params / Song / File / Performance ... | The module opens as usual and Play mode is off when you come back to the pattern editor. A chord pad that was held is released, a running recording ends. |
| Menu button (the editor popup) in Play mode | The menu works as in stock; pads / arrows / Rec belong to the menu while it is open. |
| Leave Play mode while a chord pad is held | The chord stops (no hanging note). |
| Stop (transport) in Play mode | Panic: a recording ends, held pads are released, an open type list closes; the chord / Mono stop. Stop then Play gives plain pattern playback again. |

## 2. Strip and track selection
| Press | Expected |
|---|---|
| Screen buttons 1-8 | That track is selected: brackets move, the pattern view shows the block of four tracks that contains it (1-4, 5-8, 9-12, 13-16) with the cursor frame on that track's column, pad LEDs switch to its sequencer. Works with the transport stopped and playing, and when edit mode was off. |
| **Right** | Strip shows tracks 9-16; **Left** shows 1-8. The pattern view and the selected track do not change by themselves. |
| Select a track on strip 9-16, then **Left**, then a screen button | The pattern view jumps back to the block of four of the newly selected track. |
| Hold **Shift** (a quick tap, under a quarter of a second) | Stock mute buttons on the 8 labels (names Mute / Unmute); no legend flashes. Shift + screen button mutes that track as in stock; the labels return to the strip when Shift is released. |
| Hold **Shift** about a quarter of a second without pressing a screen button, track has a sequencer | A legend of 12 x 4 cells appears over the lower pattern rows (not over the playhead row), above the mute labels. See section 2b. |
| Hold a pad, then press another screen button | The first track releases its pad (chord stops); the pad LEDs switch to the new track's layout. |
| **Pattern held** + Left / Right in Play mode | Stock pattern queue / change (not swallowed). Plain Left / Right switch the strip. |
| Hold **Rec**, press **Play** in Play mode | Plain play / stop (no stock live-rec arm, edit mode stays off). |
| Select a track on strip 9-16, press **Left** | The strip shows 1-8 and a yellow `T12 >` marker sits at its right edge (`< T3` at the left edge for the other direction). Pressing Shift snaps the strip to the selected track's group so the mute labels match. |
| A track plays a launched clip or an MLR jam | Only the strip says so (`clip` / `cut` on line 2). The pattern column is NOT dimmed (a dim would look like a mute); the pattern view shows the edit pattern, not what that track sounds. |
| Note / Instr / FX1 / FX2 / Copy / Insert / Delete in Play mode | Nothing happens (no popups over the strip). Up / Down and Play work as in stock. |
| Hold a pad, tap Pattern (leave), keep the finger down, then lift | No stray note-off / stock pad event on lift; the chord stopped at the tap. |
| Pads on a track whose type is Off | Nothing (no stock notes either). |
| Strip line 2 on a track that plays a launched Session clip / an MLR (CUT) jam | `clip` / `cut`. Rec on a launched-clip track shows `on a clip` on that cell for two seconds. |

## 2b. Pad legend (hold Shift)
| Press | Expected |
|---|---|
| Chord/Arp track selected, hold Shift for 250 ms | Caption "CHORD/ARP ..." and 12 x 4 cells: columns 1-6 green (roots), columns 7-10 cyan (4x4 panel), columns 11-12: TYP SET / GRV PRM (orange), I- I+ / O- O+ (yellow). |
| Mono track selected, hold Shift | Columns 1-8 green with 1-8 in the top row (steps); HLF TIE W+ W- (column 9), P- S- LP (column 10), P+ S+ V- (column 11), CLR and V+ (column 12). |
| Off track selected | No legend. |
| Chord/Arp legend | The 24 root cells show root names (E, C#m, ...); the mode pad of the visible panel (TYP / SET / GRV / PRM) is white; the caption names the panel. |
| Mono legend | Steps show 1-8 or 9-16 following HALF; the caption shows the degree window offset. |
| **Display budget** (DEBUG build or a log of `lastMemoryUsed` in `display.cpp`) | With the legend open the summed display-list size of all shown controls stays below 8172 B (above it the display drops controls). Note the number with and without the legend and with the type list open. |
| Release Shift | Legend gone, strip labels back. Press a screen button while Shift is held: legend hides until Shift is released. |

## 3. Pick a sequencer type (hold the track's screen button + jog wheel)
| Press | Expected |
|---|---|
| Hold a track's screen button, turn the wheel **one detent** | A list Off / Chord/Arp / Mono (3 boxes) appears above that button, highlight on the track's **current** type, a small square marks the current one. The first detent only opens the list, it never changes the type. |
| Release the button without turning further | Nothing changes (the type stays; a running track is not touched). |
| Hold the button, turn the wheel 2 detents, release (transport stopped) | The type applies at once: the strip shows `CHRD` or `MONO` (`---` for Off). |
| Start the pattern (Play), hold the button, wheel to another type, release | Line 2 of that cell shows `next: MONO`; the type changes at the next bar. |
| With a change pending: hold the button, wheel back to the current type, release | The pending change is cancelled (`next:` disappears). |
| Open the list, then press Shift (or **Left / Right**) before releasing | The list closes and nothing is applied. |
| Wheel with no screen button held | The stock wheel behaviour (not a type change). |
| Other tracks | Unaffected. |

## 4. Chord/Arp live (track type `C`, transport stopped first, then playing)
Use a MIDI track (9-16) with a synth/hardware on its channel, and a sample track (1-8) with a sample instrument.
| Press | Expected |
|---|---|
| Hold a root pad in the left 6 columns | Chord sounds (needs a moment: next quarter at most). The strip shows `CHRD*` with `live` under it (pattern muted on that track). |
| Release the pad | Notes stop, `*` disappears. No hanging notes. |
| While the pattern plays a note pattern on that track: hold a root pad | Pattern notes on that track go silent while you hold; other tracks keep playing. Release: the track's pattern is back at the next step. |
| Panel pads (cols 7-10), SETTINGS pad (col 12, row 1) | Chord / arp settings change as documented in plans/001 (types, T/S/D/Z rows). Pads show LEDs. |
| Sample track | The chord is monophonic on a sample track (last note wins). MIDI tracks send every chord note. |
| Stop the transport while holding a pad | Chord keeps sounding (free-running clock) until released. |

## 5. Mono live (track type `M`)
| Press | Expected |
|---|---|
| Stopped: tap pads in the left 8 columns | LEDs show the notes (steps). No sound while stopped, no `*`. |
| Press Play | The sequence plays on that track at 1/16; `MONO*` / `live`; the track's own pattern notes are muted. Playhead moves on the pads. |
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
| External clock: start a punch-in Rec, then stop the master | The recording ends (the red `REC` goes). |

Known limits of Play mode: no parameter editing on the jog wheel (only the pads). Recording is section 7, saving section 8.

---

# Record (section 7) and save / load (section 8)

Prepare: a project with an empty pattern (or any pattern, you will see it being replaced), one **MIDI track** (9-16) with a synth or
hardware on its channel and one **sample track** (1-8). In Play mode give the MIDI track Chord/Arp and the sample track Mono.
All recording happens on the **selected track** of Play mode (the bracketed one). Stock Rec outside Play mode is unchanged.

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
| Rec on a track whose type is Off | The selected strip cell shows `pick a type` for two seconds. Nothing records, the transport does not start. |
| Transport stopped, Chord/Arp track: press **Rec** | The transport starts at step 1 and recording starts at once. The recorded track's cell turns red and shows `REC`. After it ends the cell shows `rec overflow` / `MIDI only` / `reduced` while the last recording had that hint. |
| (Triad chosen, see 7a) Hold a root pad for a few steps, release | The chord keeps sounding while held (Live). Press **Rec** again: the red `REC` goes. Leave Play mode (tap Pattern): the pattern page shows a note with the chord FX (`MIDI chord`) on the first step of the chord, steps you passed with no chord are empty, **even where the pattern had notes before** (replace). |
| Quantise | A note that starts in the first half of a step (before the middle) lands on that step, in the second half on the next step (like stock live record). With rec microtiming ON in the Config the note stays on its step and gets a micromove value instead. |
| Velocity | With rec velocity ON in the Config the step volume follows the sequencer velocity (the c31 groove), otherwise the volume stays default. |
| Undo | In the pattern editor press the stock **Undo**: the whole recording disappears in **one** step and the previous pattern is back. Redo brings it back. |
| Punch while Mono plays | Mono track: press Rec while the sequence runs: its notes replace the steps one by one; steps without a Mono note are cleared. Rec again: stops, the rest of the pattern is untouched. |
| Chord on a **sample** track (Chord/Arp type on tracks 1-8) | Only the lowest chord note is written. (The v0.1 hint texts are no longer shown on screen; the lowest note is still the one written.) |
| Chord that is not in the Tracker chord table (a 5-note chord, wide voicing) on a MIDI track | The highest notes are dropped until the table matches, the step gets the shorter chord FX. (No on-screen hint any more.) |
| Record a very busy arp (fastest speed, 1/32) for a long time | Normally nothing special. If steps are missing in a long busy recording, note what you did (it means the main loop was too slow to take the steps). |
| Stop the transport while recording | Recording ends by itself. Steps written so far stay, one undo step. |
| End of a recording (Rec, loop end, stop) while the last note is still held/open | A note-off is written on the next step that holds no note, so the last note does not ring into the old pattern content; a step that already has a note is left alone. |
| Change the pattern (Song mode) while recording | Recording ends; nothing is written into the new pattern. |
| Set the recorded track to Off (screen button + wheel) while recording | Recording ends when the change applies (next bar). |
| Rec held in other pages | Outside Play mode Rec is the stock Rec (edit mode). Leaving Play mode (tap Pattern, any other module button) ends a running recording (punch-out note-off as usual); the pattern is not touched while you edit it. |

## 7b. Shift + Rec: exactly one loop
| Press | Expected |
|---|---|
| Transport running, Chord/Arp or Mono track: **Shift + Rec** | The track's cell turns red (`REC`). Nothing is written until the pattern wraps to step 1. |
| Wait for the wrap | At step 1 the sequencer's notes replace every step of the loop. |
| At the next wrap | Recording stops by itself, the red `REC` goes. The whole loop is one undo step. A note near the end of the last step that would quantise to step 1 is NOT written to step 1. |
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
**Plan 005 R2.** The clip launcher is reached with a **Song double tap**: press **Song** to arrive on the Song page, then tap **Song** again (press and release inside 0.4 s, no other button). Press **Song** in the launcher to go back to the Song page. Screen buttons 7 / 8 switch between the CLIPS and the CUT layer; all 12 x 4 pads belong to the active layer. A *clip* = one track of one pattern, a *scene* = a whole
pattern. **Rows are patterns** (row n = pattern n, moved by hand, independent of the song arrangement). Needs patterns 1-8 that hold different notes on a few tracks, saved in the project. Tracks that have no notes in a pattern are empty clips.

## 9. Session view, CLIPS layer (plan 005 R2)
Pads (1-based): columns 1-8 = the 8 tracks of the window, **column 9 empty (gap)**, **column 10 = scene launch** of the row, columns 11-12 empty; rows 1-4 = 4 patterns. Screen buttons: **1 MODE** (hold + wheel), **2 QUANT** (tap), **3 STOP** (hold + pad), **4 BACK TO SONG** (only while LIVE), 5 empty, 6 belongs to the active layer (CUT: SYNC ALL), 7 empty gap, **8 switches CLIPS <-> CUT** (its label shows the other layer). Left / Right = tracks 1-8 / 9-16, Up / Down = rows (Shift = 4 rows), jog = rows.
Screen (ASCII text and shapes only): a grid of 12 pattern rows x 16 tracks (gap after track 8) and the scene column; **a red frame round the 8 x 4 pad window** (and one round its scene cells); empty cell dark, clip with notes grey, playing green with a dark progress bar, queued blinking white, follow action armed = steady green outline; a notch in the top right corner = Play 1x (red notch = the next row is empty, the track stops after this clip); a dim square in an empty cell of a sounding track = pressing that pad stops the track; a white outline = the focused clip; a green bar on the left edge = the row the transport plays. Right column: launch quantise, countdown ("next in N steps"), LIVE state, focused clip and its mode, short help.
| Press | Expected |
|---|---|
| Song page, tap **Song** twice quickly | The launcher opens: grid, red frame round tracks 1-8 x rows 1-4, labels `MODE LOOP` `Q: bar` `STOP` (button 4 empty) and `CUT` on 8. Pad LEDs: playing clips bright (the loaded pattern's row), clips with notes dim, empty clips off (a dimmer light for empty pads of a sounding track), scene pads of the loaded pattern bright. |
| Press **Song** once (not a tap: hold it > 0.4 s, or press another button with it) | No launcher; the stock Song page behaves as before (slot list focus while held). |
| **Pattern**, **Performance**, any other module button in the launcher | Leaves the launcher for that page; the clips keep playing. **Song** returns to the Song page. |
| **Right** / **Left** | The red frame and the pads jump to tracks 9-16 / 1-8; the pad columns show those tracks. At tracks 9-16 **Right** does nothing, at 1-8 **Left** does nothing. |
| **Down** / **Up** (Shift + key) | The frame moves one row (4 rows) down / up; the grid scrolls when the frame would leave the 12 shown rows. Not above pattern 1, not below pattern 255. The jog wheel does the same (Shift = 4). |
| Transport running. Tap a dim clip (track T, pattern P) | The cell blinks; at the next **bar** (global position) track T plays its part of pattern P from its first step, the cell turns green and the old clip turns grey. Countdown "next in N steps" counts down. Other tracks keep playing. |
| Screen button 2 (tap) | The label cycles `Q: bar` > `Q: pattern` > `Q: now` > `Q: bar`. Launch at the bar / pattern end / next step of the track. The right column shows the value. |
| Tap a clip while another clip of that track is queued | The newer launch wins. |
| Clip with a different length than the loaded pattern | The track loops with its own length (stock Performance behaviour). |
| Hold screen button **3** (label `[STOP]`) and tap a clip pad of track T | At the quantise point track T goes silent (a held note ends); no clip is green for it; empty pads of T are not dimly lit any more. Tap any clip of T: it plays again. |
| Hold **3** and tap a **scene** pad | All 16 tracks stop at the quantise point. |
| Tap an **empty** clip pad (dark cell) | Stops that track (same as hold 3 + pad). The empty cells of a sounding track carry a dim square so this is visible. A track that is already stopped ignores it. |
| Tap a **scene** pad (column 10) | The scene pad blinks; at the pattern end the whole pattern switches (stock pattern switch), all clip states reset to "the loaded pattern", stopped tracks play again. Scene launch is always at the pattern end whatever QUANT says. |
| Transport stopped: tap a clip / scene / empty pad | Takes effect at once (nothing to wait for). Press Play: it plays. |
| Stop the transport with launches queued | **Stop is the panic**: queued launches and stops are dropped, launched clips end, every track plays its own pattern again (the pattern is restored from its file), LIVE ends. Play again = stock pattern playback. Pressing Stop with nothing launched changes nothing. |
| Screen button 8 (label `CUT`) | The CUT layer (section 10); the label now reads `CLIPS`, button 8 again: back, clip states unchanged. Held pads are released when you switch. |
| Pattern file safety: launch a clip on a track, wait 15 s (autosave), launch a scene or switch pattern, look at the original pattern | The original pattern is unchanged (launched clips were not saved into it); edits you made to other tracks of it are kept. |
| Launch own pattern's clip on a track that has a queued clip or follow action | The queued clip is cancelled, the track keeps/returns to the pattern's own part. |
| Leave the launcher (any module button) with clips running | They keep running. Hold no pad: nothing hangs. |
| Chord/Arp or Mono track taken over (Play mode) and a clip is launched on that track | The sequencer keeps priority while it takes over; the clip plays once it ends. |
| Busy messages | "Busy - try again" (red, bottom of the grid) when the file manager is busy or the command ring is full. Tap again. A launch on the last step of a song pattern is not refused: the freeze (LIVE) is done on the next step. **Tracker Plus / Mini only**: "Unsaved edits: stop and save first" when the loaded pattern has unsaved edits and the transport plays (those units have no room for the own-data snapshot). |
| "Undo history cleared" | Shown for 2 s when the last launched clip went away and the pattern undo history was dropped (a recording followed by a launch loses its undo step). |

Behaviour since the review fixes: a clip launch copies the own data of the tracks it replaces into a RAM snapshot (about 1 KB per track, Tracker original) and **writes nothing to the card while the transport plays**; saving (after Stop, or an explicit Save) and the restore after Stop / panic take those tracks from the snapshot, not from the file. A pattern file that cannot be read aborts the save (the changed flag stays, nothing is overwritten) and the launched tracks are never cleared by a read error; scene launch refuses with "Busy" while clips run and the save did not happen; leaving the Session view and the end of a recording ask for the project autosave (like leaving the stock editor); leaving to Master or Instrument keeps the Session clips playing (no turn-off of performance mode), and entering the stock Performance page while clips run keeps their lengths; undo history is cleared when the last launched clip goes away (the view says "Undo history cleared").


### 9b. Play mode per clip (Loop / Play 1x) and the follow action
Default playback is **Loop**. The mode belongs to the clip (pattern x track), is saved in `seqclips.bin` and applies however the clip was started (pad, scene, follow action).
| Press | Expected |
|---|---|
| Hold screen button **1** and tap a clip pad | The pad (and its cell, white outline) is **focused**, nothing launches or stops while button 1 is held. The right column shows "Clip P  track T" and LOOP. |
| Still holding 1, turn the wheel one detent clockwise | The focused clip becomes **PLAY 1X**: notch in its cell, label `MODE 1X`. One detent back: LOOP. No wrap, no change beyond the two modes. |
| Hold **Shift + 1**, wheel clockwise (focus any pad or scene pad of the row) | Every clip of that **row** (all 16 tracks) becomes PLAY 1X, the scene cell shows the notch; wheel back: all LOOP. Other rows are unchanged. |
| Release 1, save the project, power cycle, open it | The modes are still set (`seqclips.bin` next to the project; not written while the transport plays - they are saved after Stop). A project from before has all clips on Loop. |
| Transport running, launch a Play 1x clip (pattern P) on track T and let it play | When it has played once, **track T continues with the clip of pattern P+1** (same track, the row below) at the clip end, without a gap. While it plays, the next cell shows a steady green outline (armed), not blinking. |
| Same, but pattern P+1 has no notes on track T (or P = 255) | After one pass track T **stops** (silent). The notch in the cell is red from the start (END marker). |
| Chain: clips P, P+1, P+2 all Play 1x with notes, P+3 empty on that track | The track plays P, P+1, P+2 once each and stops. The clip length of each decides when the next one starts. |
| While a Play 1x clip plays, tap the playing clip again (or another clip of that track) | Tapping the playing clip cancels the follow action (it loops on); another clip replaces it. The follow action does not come back for the same clip instance. |
| A scene (row) that is all Play 1x | Every track plays its clip once and continues in the next row; a track without a clip below stops. |
| Stop the transport during a follow chain | Panic: tracks are back on the loaded pattern; Play starts stock playback. |
| File manager busy at the clip end (e.g. while a save runs) | The clip plays once more and the follow action follows at the next clip end (never silence, never a switch in the middle of a step). |

### 9c. LIVE: launching while the song plays
| Press | Expected |
|---|---|
| Play the **song** (Song page, Play), open the launcher, launch a clip | The song **freezes on its slot** (the pattern now loops): the right column shows "LIVE - song frozen, slot N", button 4 reads `BACK:SONG`. The clip plays and is not wiped at the next pattern end. A green bar on the left edge marks the row of the playing pattern. |
| Launch a scene while the song plays | Same: LIVE, the scene switches at the pattern end and keeps looping. |
| Press screen button **4** (BACK:SONG) | LIVE ends at once in the display; at the next pattern end all clips end and the **song continues with the slot after the playing one**. A queued scene is dropped. |
| Press **Stop** while LIVE | Panic (see above). Play again: stock pattern playback, no LIVE. |
| Start the song again by hand (Song page) while LIVE | LIVE ends by itself. |
| Launch while only a pattern loops (not song mode) | No LIVE, nothing frozen. |
| Launch on the last step of a song pattern | Not refused: the clip is queued and the song freezes on the next step (LIVE appears one step later). |

Known limits (R2): in song playback a Play 1x follow action is not queued (the song advances by itself; launch freezes the song, then follow actions work). A Play 1x clip counts its first pass from the step Play started at (or the last pattern switch / join). The follow actions of tracks that continue into the same row share one read of the card (at most one attempt per step; after three read errors in a row the track stops at the clip end instead). A follow action is not started on the last step of the pattern, while a scene is queued, or while one of your taps is still on its way to the audio side. Undo snapshots hold the RAM pattern; the "has notes" display of a pattern that is not loaded comes from the stock Song bitmasks (updated when that pattern is saved); clips are copied into the loaded pattern in RAM (stock Performance mechanism), so edits to a launched clip are lost on save/restore. The Tracker Plus / Mini keep no snapshot: there the file is the source and a launch while playing is refused when the pattern has unsaved edits.

### 9d. Display list measurement (device, once)
The clip screen is budgeted in software (grid at most about 5.5 KB of the 8 KB frame, optional parts dropped first: armed outlines, notches, dim squares, help lines). Verify on the unit with a **DEBUG build**: `display.lastMemoryUsed` (display.cpp step3) must stay below 8172 B in the worst case - all 16 tracks playing, many Play 1x notches, LIVE and the help lines (hold button 1 or 3), a message showing. If the screen loses its last controls (labels or the grid's right column) the budget is too high.


## 10. Session CUT (MLR playhead, plan 002 S2)
Open it with a Song double tap (launcher) > screen button 8 (CUT). Title "Session: CUT   tracks 1-4"; screen buttons 1-4 choose the four tracks shown (1-4, 5-8, 9-12, 13-16); the info lines explain the pads. Use a project with: track 1 = ONE long
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
| CUT: jump around on row 1 (track 1), switch to CLIPS (button 8), launch a clip (other pattern) on track 1 at the next bar | At the bar the track plays the new clip from its first step; the CUT playhead follows (jump pads count from the new clip's step 1 on). No stuck note, no silence. |
| CUT: STOP track 1 (pad 9), then CLIPS: launch a clip on track 1 | The CUT stop ends: the clip plays (a launch means "play this"). |
| CLIPS: hold STOP-T and tap a clip pad of track 2 (it stops at the bar). CUT, row 2: press position pads | Track 2 stays silent: Session STOP wins over CUT, the position pads do nothing audible, the playhead LED of row 2 stays where it was. |
| Then CLIPS: tap any clip on track 2 | Track 2 plays the clip again; the CUT position follows the clip's step 1. |
| CUT row with a recorded loop, CLIPS: STOP-A, then launch clips | While stopped all tracks are silent and the CUT loops do not run on; after a launch the track plays again. |
| CLIPS: launch a scene while CUT rows are active | At the pattern end the new pattern plays; CUT rows keep their settings (position inside the new pattern, clamped). |
| In CUT hold two pads (loop), then press screen button 8 (CLIPS) | The loop ends at once (pads are released by the layer switch). Rec held while switching: the take is closed. |
| Leave the Session view (any module button) while a CUT pad is held / Rec is held | Nothing hangs: loop ends, take closed. |

# Alt Performance page (8-step faders) — plans/004-perform-faders.md, reworked in plan 005 R3 (v0.2)

Entry is **Shift + the top-row Performance button** (firmware "Performance"), from the stock Performance page AND from every other page.
Fader positions and active FX are NOT saved with the project: after loading a project every perform FX is off.
Provisional parts (owner did not specify): LATCH pads, jog-wheel settings, the steps' value mapping, what DISENGAGE does to PING / REV project values (restores the earlier value).

## 11. Alt Performance page (Shift + Performance)
Setup: a project with tracks playing; on the stock Performance page give the slots a few FX (e.g. slot 1 low-pass, slot 2 sample volume,
slot 5 pan, slot 9 tune).

| Press | Expected |
|---|---|
| **Performance** (plain press, from any page) | Stock Performance page opens (as always). |
| **Performance** pressed twice quickly on the stock page | Stock behaviour only: the tracks page flips and flips back (1-8 / 9-16). The fader page does NOT open. |
| **Shift + Performance** on the stock Performance page | Fader page. The tracks page and the 8 track buttons are as you left them; FX that were latched on the stock pad rows show as lit rows. |
| **Shift + Performance** from Pattern, Song, Master, Params, Sample pages, File ... | Fader page. No track is affected yet (buttons under the screen show 1-8 without brackets, the header says "no track selected" in a warning colour), every row is DISENGAGED. |
| **Performance** (plain press) on the fader page | Back on the stock Performance page; the 8 buttons keep their state; FX that were playing from the stock pad rows still sound and their pads are lit. |
| Screen layout | Below the title bar: a chip row (FX 1-4, FX 5-8, FX 9-12 | PING, REV = pad columns 9 / 10; the page you are on is filled, a green dot marks a page with something dialled in) and on the right "affects tracks 1-4 7". Under the chips the step numbers 1-8. Four bands = the four pad rows: row number, FX name, current value, a long bar of 8 steps (lit from the left), a LATCH and a DISENGAGE cell (pad columns 11 / 12), the state word under them (DISENGAGED / ENGAGED / LATCHED; PING / REV project rows: PROJECT / DIALLED) and a second line (slot number and range). A strip at the bottom shows the group line (FX pages: how many are dialled in; PING: time / rate, feedback, filter, ping-pong, sync; REV: size, damp, predelay, diffusion). An empty slot is drawn dim with "(empty slot)". |
| Pad LEDs | Pad rows 1-4 lit only where a slot has an FX; column 9 group pads: current page bright, a page with something dialled in medium, others dim (row 4 dark on purpose); PING / REV (column 10) likewise; LATCH column dim (bright when latched); DISENGAGE column (12) bright where its row has something to take off, dim otherwise. |
| Hold a pad, plain Performance press | Leaves at once (a held pad does not block it). |

## 11b. Perform FX on the faders (FX1-4 / FX5-8 / FX9-12)
| Press | Expected |
|---|---|
| Select tracks 1 and 2 with the 8 buttons under the screen (label shows `[1]` `[2]`) | Selected tracks are the ones the FX affect (same as the stock page); header "affects tracks 1 2". Shift + button = tracks 9-16. |
| Hold pad 5 of fader 1 (row 1) | Bar lit from the left up to pad 5; the band shows the FX name, the value, the state ENGAGED; the FX is heard on tracks 1 and 2 only. |
| Slide to pad 8 | Value rises to the FX maximum; pad 1 is about 1/8 of the range (a signed range gives 0 on pad 4). |
| Release | Bar goes dark, DISENGAGED, and the FX is cleared at once (momentary, like stock). |
| Hold pad 3 of fader 2, then also pad 6 (two pads), release pad 6 | Value falls back to the still-held pad 3 until the last pad is released. |
| Stock page: press a pad of slot 1 (the stock page latches its row), Shift + Performance | Fader 1 shows the bar for that value (note "stock pad row") and the FX keeps playing. Pressing a fader pad changes it; release returns to the stock value (not to silence). **DISENGAGE** of row 1 clears the FX and the stock pad row (dark on the stock page too). |
| Fader FX and a stock slot with the same FX | The newer fader takes over (stock rule); the older one goes dark. Releasing a fader re-applies the other active faders / sends. |
| Song mode, pattern switch while on the fader page; Shift + Play | Same as the stock page (track patterns follow the song; Shift + Play starts the song / toggles loop). MIDI CC 41-48 / 51-62 (perform CC) are NOT handled on the fader page (known limit). |
| Tap **LATCH** (column 11) of row 1, then press and release pad 5 of fader 1 | FX stays; bar stays lit; state LATCHED (orange bar, orange LATCH cell). |
| Tap **DISENGAGE** (column 12) of row 1 | FX cleared, LATCH off, bar dark, state DISENGAGED. Nothing is remembered: pressing a pad starts from scratch. The DISENGAGE pad is dim again. |
| **Shift + DISENGAGE** of any row | All four rows of the current page are disengaged. Other pages are untouched. |
| LATCH on, then tap LATCH again | FX cleared when no pad is held. |
| Hold a pad of fader 2, tap DISENGAGE of row 2, release the pad | The FX stays off (the held pad does not bring it back). |
| With a latched FX, select track 3 with its button | The FX also reaches track 3. Deselect track 1: its FX ends. |
| Tap **FX5-8**, **FX9-12** | The four faders now control slots 5-8, 9-12; the group pad is bright; the chip of the page with the latched FX shows a green dot and its pad is medium. An empty slot has a dark row. Latched FX on the old page keep playing. |
| Hold a fader and tap another group pad | The held fader is released (unless LATCH). |
| Click check: engage a latched filter / volume FX and disengage it a few times while playing | Same sound behaviour as releasing a stock pad row; no extra click from the page (listen on headphones). |

## 11c. PING (delay) and REV (reverb)
| Press | Expected |
|---|---|
| Tap **PING** | Chip PING filled; faders: 1 delay send (DISENGAGED until used), 2 time (or rate when sync is on), 3 feedback, 4 filter - bars 2-4 show the current project values, state PROJECT (default time 500 ms = pad 3, feedback 50 = pad 5, filter 20000 = pad 8). The bottom strip lists time, fb, filter, ping-pong and sync. |
| Hold pad 6 of fader 1, release | Delay send of the selected tracks goes up (+63) while held, back to normal on release (LATCH keeps it). |
| Press pad 2 / 5 / 8 of fader 2 (sync off) | Delay time changes (about 219 / 1367 / 3500 ms) and STAYS after releasing; the band shows ms and the state DIALLED. |
| Jog wheel right / left | Ping-pong on / off (bottom strip shows it); audible stereo bounce. |
| Shift + jog wheel right | Sync on: fader 2 is now the rate (band "Rate 1/8" etc.), pad 1 = "8", pad 8 = "1/32". Shift + jog left = sync off. |
| Fader 3 pad 8 | Feedback 99 (long repeats); fader 4 pad 1 = about 194 Hz (dull repeats). |
| **DISENGAGE** of row 2/3/4 after you moved it with the fader | The value it had BEFORE your first fader press comes back (time and rate index, feedback, filter), state PROJECT again; a second DISENGAGE does nothing. On a row you never touched DISENGAGE does nothing. Row 1 (send): off. |
| Tap **REV** | Fader 1 reverb send of the selected tracks; 2 size, 3 damp, 4 predelay (0-100); jog wheel = diffusion; the bottom strip shows it. DISENGAGE restores the value from before the fader touched it. |
| Change PING / REV values, save the project, power off, open it | The values are saved with the project (same as changing them on the Master page); open the Master page: it shows the same values. The fader positions / latches / engaged state are NOT saved: after loading, the fader page is all DISENGAGED / PROJECT. |
| No volume or panning on this page | Correct (owner decision). |

LATCH pads of PING / REV rows 2-4 are dark and do nothing (project values always stay).
Known: delay time jumps (no smoothing in the delay engine, same as the Master page), so a press may click while playing.
Known limits: FX that exist only for synth tracks / global FX use the stock slot rules; two slots with the same FX exclude each other
(stock rule), the older fader bar may go dark.

## 11d. Leaving the fader page, Stop panic (plan 005 owner decisions)
Setup: tracks 1 and 2 selected, a filter on slot 1, a volume FX on slot 2; latch rows 1 and 2 and press pads so both are ENGAGED / LATCHED.

| Press | Expected |
|---|---|
| **Pattern** (opens the pattern editor), then tap Pattern again (Play mode) | The latched FX keep sounding in the pattern editor and in Play mode. Pads in Play mode drive the sequencer as in R1. |
| **Song**, then the clip launcher (Song tap), MLR layer | The latched FX keep sounding in the launcher and in MLR; launching clips does not end them. |
| Back with **Shift + Performance** from any of those | The fader page opens with the same rows ENGAGED / LATCHED (positions, latch, page you were on), nothing jumps. |
| Hold a pad (not latched) and press Pattern | The held pad ends (momentary), the latched rows stay. |
| Latched FX running, press **File**, **Config**, Sample pages | Stock rule: the FX end (at the pattern end while playing); the fader page is clean the next time. |
| Latched FX running, press **Master** or **Params** | Stock rule: performance mode stays (as the stock Performance page does), the fader FX end. |
| Latched FX running, press plain **Performance** | Stock Performance page; the FX that were kept from Pattern / Song end (the stock page starts fresh); a fader row you handed over from the stock page keeps working there. |
| Latched FX running, any page, press **Stop** | Every perform FX ends (panic): fader rows DISENGAGED (also latches), rows handed over from the stock page too. PING / REV project values stay. Stop then Play = plain playback. |
| Same on the fader page | Press Stop: all rows go DISENGAGED at once, the page stays open. |
| Launch clips, open the fader page, dial an FX, leave and press Stop | Clips are stopped by the launcher panic, the FX ends; no pattern is reloaded while clips still own performance mode. |
| Save the project with FX engaged, load it | All perform FX are off after loading (also a latched FX kept into the Pattern editor, even if you load while stopped); performance mode is not left on. |
| Latched FX kept into the Pattern editor, edit steps (unsaved), press Stop | The FX end; the pattern is NOT reloaded from the SD card, the unsaved edits stay (reload only happens when per-track performance patterns were loaded on the stock page). |
| From the Pattern editor (FX kept) go on to File / Config / Sample pages | The kept FX follow the stock rule: they end at the pattern end while playing, at once when stopped; Master / Params / Song / Pattern / Performance keep or hand over as above. |
| Hold Shift while pressing Performance, keep Shift down and press a pad / jog | Shift is ignored until you release it once (no accidental Shift + DISENGAGE or Shift jog). |
| Leave the fader page while playing (any button) | No SD / config write at that moment; DISENGAGE of a stock pad row marks the perform data, written after Stop. Device test: moving a PING / REV fader 2-4 sets the project changed flag, so the stock 10 s autosave can run while playing (same as the Master page). Watch for stream hiccups. |

# Stock-behaviour checks (section 12) — run last, and after every change
A project with no sequencer set and no Session use must behave exactly like stock CE.

## 12. Stock behaviour (never enter Play mode, Session or the fader page)
| Press | Expected |
|---|---|
| Open a project you know, play it (Play), stop, edit steps, record, switch pages | Exactly as stock CE. |
| Hold **Pattern** + Left / Right / Up / Down (8-track view and others) | Same as stock; the screen does NOT switch to Play mode when you release Pattern. |
| Hold **Pattern**, turn the jog wheel or press a pad, release Pattern | Nothing new happens. |
| Hold **Shift** (or any button), tap **Pattern** | Play mode does not toggle. |
| Come from another page (e.g. Song) by pressing Pattern | Pattern page opens, no Play mode (the first press only switches pages). |
| Open a project, tap Pattern twice (Play mode, Session) and back to the editor without touching any pad; Save | No `seqtracks.bin` is created, the pattern files are unchanged, playback is as before (Session alone changes nothing until a clip is launched). |
| Tap Performance once, play with the stock Performance page | Exactly the stock page (the alt fader page only opens with Shift + Performance). |
| Project with CUT used (rows cut, recorded loop), save and re-open | CUT is not saved in v0.1: all rows stock after loading; `seqtracks.bin` is only written when a sequencer type is set. |
