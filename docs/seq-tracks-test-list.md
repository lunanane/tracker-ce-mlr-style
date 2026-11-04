<!-- Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md) -->
# Sequencer tracks — on-device test list (step A: Play mode + Live)

Original Tracker (MK66). There is no simulator, so these checks are the only proof. Flash the `.ptf`, load or create any
project, and go through the list. Write down anything that differs. Recovery if something is badly wrong: reset pinhole
and flash a known-good `.ptf` from `/Firmware/`.

## 0. Stock behaviour (do this first, never enter Play mode)
| Press | Expected |
|---|---|
| Open a project you know, play it (Play), stop, edit steps, record, switch pages | Exactly as stock CE. |
| Hold **Pattern** + Left / Right / Up / Down (8-track view and others) | Same as stock; the screen does NOT switch to Play mode when you release Pattern. |
| Hold **Pattern**, turn the jog wheel or press a pad, release Pattern | Nothing new happens. |
| Hold **Shift** (or any button), tap **Pattern** | No Play mode. |
| Come from another page (e.g. Song) by pressing Pattern | Pattern page opens, no Play mode (the first press only switches pages). |

## 1. Enter / leave Play mode
| Press | Expected |
|---|---|
| On the Pattern page tap **Pattern** (press and release, nothing else) | Title bar "Play mode". Middle of the screen: "Track N: Off" and three hint lines. The 8 bottom labels show `[1 -]` `2 -` ... (track number, type letter, `[ ]` around the current track). |
| Tap **Pattern** again | Back to the normal pattern editor, as before (cursor, view). Tap again: Play mode again. |
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

Known limits of step A: no parameter editing on the jog wheel (only the pads). Recording and saving are step B (sections 7 and 8).

---

# Step B: Record (section 7) and save / load (section 8)

Prepare: a project with an empty pattern (or any pattern, you will see it being replaced), one **MIDI track** (9-16) with a synth or
hardware on its channel and one **sample track** (1-8). In Play mode give the MIDI track Chord/Arp and the sample track Mono.
All recording happens on the **current track** of Play mode (the bracketed one). Stock Rec outside Play mode is unchanged.

## 6b. How to get a 3- or 4-note chord (default is ONE note)
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
| (Triad chosen, see 6b) Hold a root pad for a few steps, release | The chord keeps sounding while held (Live). Press **Rec** again: the info line clears. Leave Play mode (tap Pattern): the pattern page shows a note with the chord FX (`MIDI chord`) on the first step of the chord, steps you passed with no chord are empty, **even where the pattern had notes before** (replace). |
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
