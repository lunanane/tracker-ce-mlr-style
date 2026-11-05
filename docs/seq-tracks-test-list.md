<!-- Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md) -->
# Sequencer tracks — on-device test list (Play mode, Record, save/load, Session CLIPS + CUT, alt Performance page)

Original Tracker (MK66) and Tracker Plus. There is no simulator, so these checks are the only proof. Flash the `.ptf`, load or create
any project, and go through the list in order. Write down anything that differs. Recovery if something is badly wrong: reset pinhole
and flash a known-good `.ptf` from `/Firmware/`. Section 12 (stock behaviour) is the regression check: run it last, and again after
any change.

## Start here: the highest-value on-device tests
Do these first (the full list below is the detail):
1. Shift+Rec a loop in Play mode, Stop, go to Performance, then Pattern, reopen the project: the whole recording is there.
2. Record on track 3, go to the launcher (Song page, then Song again), launch another clip on track 3, then launch the own scene: the recording is intact.
3. Launch clips, wait 15 s, Save, Save As, power cycle: the original pattern files are unchanged and edits on other tracks are kept; also in song mode.
4. A project that never used the mod behaves like stock CE (section 12).
5. CUT jumps on track 1 while Chord/Arp runs elsewhere: the arp stays on the bar.
6. External MIDI clock start / stop / cable pull with Chord/Arp + Mono: no hung notes, a recording ends.
7. Clips running, visit Master, come back: the clips keep playing.
8. Reflash the stock .ptf to confirm the recovery path.
9. R2: Song on the Song page opens the launcher (Song again returns); with the song playing launch a clip (the song keeps looping its slot, no freeze); a Play 1x clip chains into the next row; Stop = panic (section 9, 9b, 9c).
10. Plan 006 (v0.2.3): play the song: the slot **loops** until its scene is flagged play next; queue another slot with Insert on the Song page; set the flag with screen button 1 + wheel (section 9e).

Contents: 1-6 Play mode | 7 Record | 8 save / load | 9 Session CLIPS (9e live-set Song page) | 10 Session CUT (10c lane screen, SYNC ALL, panic) | 11 alt Performance page | 12 stock-behaviour checks.

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
| Hold a track's screen button, turn the wheel **one detent** | A list Off / Chord/Arp / Mono / Arp Lines / Chord Stabs / Arp Seq / HR Chord/Arp / Bassline Seq / Euclid Drums / Grid Seq / Step Follow (11 boxes, bottom-aligned above the button, original Tracker) appears above that button, highlight on the track's **current** type, a small square marks the current one. The first detent only opens the list, it never changes the type. |
| Release the button without turning further | Nothing changes (the type stays; a running track is not touched). |
| Hold the button, turn the wheel 2 detents, release (transport stopped) | The type applies at once: the strip shows `CHRD`, `MONO`, `ARPL`, `STAB`, `ARSQ`, `HRCA`, `BASS`, `EUCL`, `GRID` or `STEP` (`---` for Off). |
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
| Stop the transport while holding a pad | Stop is the panic: the chord ends and the pad is released (the lift afterwards does nothing), as in the Stop row of section 2. |

## 5. Mono live (track type `M`)
| Press | Expected |
|---|---|
| Stopped: tap pads in the left 8 columns | LEDs show the notes (steps). No sound while stopped, no `*`. |
| Press Play | The sequence plays on that track at 1/16; `MONO*` / `live`; the track's own pattern notes are muted. Playhead moves on the pads. |
| CLEAR pad (column 12, row 1) | The sequence is cleared, the note stops, `*` goes, the pattern plays again on that track. |
| Stop | Notes stop immediately, `*` goes. |
| Leave Play mode while Mono plays | Mono keeps playing (set the track to Off to silence it). |

# HYPER_RASP types (plan 008 D) - original Tracker only
These eight types are the scripts of HYPER_RASP, ported 1:1. Select the type as in section 3 (the wheel walks the whole list), tap pads in the
**left 8 columns** (the HYPER_RASP 8 x 4 pad); columns 9-12 ("extras") hold what the script keeps on encoders. Hold **Shift** for the legend
(words per pad, the caption shows the state and how to play). General for all of them: they get the clock only while the transport plays,
the track is taken over (`*` / `live`, pattern muted) while the script is performing, and Stop silences them. Extras: `-` / `+` pads step a
value, the bottom row resets it. Colours: dim = idle, bright = active; where the script lights a "current" step the pad is full bright.

## 5a. Arp Lines (`ARPL`) and 5b. Chord Stabs (`STAB`) - same layout, different voice
| Press | Expected |
|---|---|
| Tap a root (cols 1-6, rows 1-2: C .. B) | The chord of the current Major/Minor + chord pad is latched and the pattern runs on the transport: Arp Lines plays one note per hit (an arpeggio figure), Chord Stabs plays the whole chord on the hits. The root pad flashes on every hit. |
| Tap the same root again | Unlatches, notes stop, `*` goes. |
| Col 1, rows 3 / 4 | Major / Minor. The sound changes at once. |
| Cols 2-6, rows 3-4 | One of the ten patterns (figures for Arp Lines, stab rhythms for Chord Stabs); the lit pad is the current one. |
| Cols 7-8, rows 1-4 | One of the eight chords (triad, 7th, add9 ...). |
| Extras (cols 9-12) | Row 1: Oct- Oct+ Tran- Tran+. Row 2: Slower Faster Vel- Vel+. Arp Lines only, row 3: Lead / Follow (Follow: the twelve root pads are the degrees of the scale another track published, e.g. a Chord/Arp with key lock, and the played root is published back). Row 4: reset Oct / rate (1/16) / Vel / Tran. |
| Caption (Shift) | `ARP LINES  C min  <pattern>  <chord>  1/16  Vel 100` |

## 5c. Arp Seq (`ARSQ`)
| Press | Expected |
|---|---|
| Tap a root (cols 1-6, rows 1-2) | Latches it and arpeggiates the chord at the current setting (first note at once); tap it again to stop. |
| Row 3: col 1 Major/Minor, cols 2-6 five cycle pads | Each tap cycles: mode (Arp / Chord), speed, direction, groove (velocity pattern 1-12), octave (-1 / 0 / +1). The legend shows the current value. |
| Col 7, rows 1-3 (chord keys) | Hold one, or overlap two or three in a sequence: the order picks one of 15 chord types (e.g. 1 then 2 = maj7). The pads show the order 15 / 10 / 6. |
| Col 8, row 3 | PLAY: the 8 steps run, each lasting 4 bars. |
| Row 4 (steps 1-8) | Tap = store the current setting in that step. Hold two steps = loop range. Bright = stored, brighter = current. |
| Extras | none (the script has no encoder parameters). |

## 5d. HR Chord/Arp (`HRCA`) - HYPER_RASP chord_arp.lua, not the grid-composer Chord/Arp
| Press | Expected |
|---|---|
| Col 8, rows 1-4 (function column) | A tap on one key = view 1-4: roots + chord types / arp settings / gate and groove / chord tracker. Keys 3+4 together toggle the tracker page, 1+2 together = tracker play / stop. (The mute combo 1+3 is not implemented: use the Tracker's mute.) |
| View 1: tap a root (cols 1-6) | Chord sounds and arps per the view 2 settings. Col 7: four chord-type keys, hold two for the six combo types. |
| Views 2-4 | As in the script: direction / speed / mode / key lock / size, octave and inversion; gate patterns and the velocity groove; eight chord steps with durations (page keys, clear key). |
| Extras | Row 1: Vel- Vel+ Tran- Tran+. Row 2: Key- Key+ Scale Latch. Row 3: Start Play Page. Row 4: View 1-4 directly. |

## 5e. Bassline Seq (`BASS`)
| Press | Expected |
|---|---|
| Rows 1-3 (24 keys, row 3 lowest) | Scale degrees of the shared scale (default D# minor) from the tonic at or below the shared root (default 60). Tonic keys are brighter. Keys that leave MIDI are dark. Hold keys: nothing sounds yet. |
| Row 4 cols 1-7 | Euclid presets E(1,8) .. E(7,8): the lit one is the rhythm. With the transport playing, each onset plays the held keys **in the order you pressed them** (one key = repeats it, several = arpeggio). |
| Release the last key | The ringing note stops at once. |
| Row 4 col 8 (Rec), hold | While held (transport playing) the preset steps are stored, 8 at a time up to 8 chains; after you release, stop and play: with no preset selected the stored rhythm plays. Stop clears the live preset. |
| Extras | Row 1: Oct- Oct+ Dir- Dir+ (UP / DOWN / UP/DN / PATT). Row 2: Grv- Grv+ (velocity groove 1-12). Row 4: reset Oct / Dir / Grv. |
| With a Chord/Arp (key lock) or Arp Lines (Lead) on another track | The published scale / root move this keyboard without reloading. |

## 5f. Euclid Drums (`EUCL`)
| Press | Expected |
|---|---|
| Rows 1-4 | Four drum voices (kit "bd sd oh ch": notes 36 / 38 / 46 / 42 on the track's channel). Tap E(k,8) in cols 1-7 to give that row a Euclid rhythm (transport playing); tap the lit one again to mute the row (dim). |
| Col 8 (Rec), hold | Stores that row's rhythm (8 steps per chain, up to 8). |
| Extras | Row 1: Grp- Grp+ (8 kit groupings) Vel- Vel+. Row 2: Cha- Cha+ (chance of a hit, 10 % steps; below 100 the hits are random) Swg- Swg+ (kept, not applied, as in the script). Row 4: reset. |
| Stop | All drums end, the live presets are cleared (the recorded rhythms stay). |

## 5g. Grid Seq (`GRID`)
| Press | Expected |
|---|---|
| Rows 1-4 x steps 1-8 | Tap a cell: the step toggles (bright); the playhead is a dim cell. A step plays that row's drum voice (same kit groupings as Euclid Drums). |
| Hold a cell | After about one quarter note the voice retriggers from that step; hold two cells of a row = faster ratchet (the spread picks the rate). A hold that ratcheted does not toggle the step. |
| Extras | Row 1: Grp- Grp+ Vel- Vel+. Row 2: Div- Div+ Tran- Tran+. Row 3: Lo- Lo+ Hi- Hi+ (loop range, the pads outside it go dark). Row 4: reset. |

## 5h. Step Follow (`STEP`)
| Press | Expected |
|---|---|
| Rows 1-4 x steps 1-8 | A window of four scale degrees (row 4 lowest). Tap to light a step; a step with several lit degrees plays them together. Tonic rows are brighter. The notes follow the scale / root another track published (none = chromatic from the root). |
| Col 8, rows 3 / 4 | Page the window up / down by two degrees (dim = at the limit of 12..120). |
| Extras | Row 1: Oct- Oct+ Vel- Vel+. Row 2: Div- Div+ Lo- Lo+. Row 3: Hi- Hi+. Row 4: reset (Oct, Vel, Div, whole loop). |
| Limits | 24 distinct degrees are kept; further ones are ignored. |

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
| **Rec is a toggle** (plan 008 A): press **Rec** and let go, keep playing for several steps, then press **Rec** again | The first press starts the take (red `REC` in the cell), it keeps running after you let go of the key, also while the key is held for a second or more; the second press ends it. Holding the key does nothing (the keys driver sends hold events, they are ignored). A bounce (two presses within 60 ms) counts once. |
| **Punch region**: Chord/Arp track, pattern with notes on every step. Press Rec at step 5, wait, play a chord from step 7 to step 9, let go, press Rec at step 12 | Only the steps that passed **while the chord was played** (7-9; a step already playing when a note starts may get the note) are replaced by what the sequencer played; everything else, including the idle steps 5-6 and 10-12 of the take, keeps its old notes. A take in which you play nothing, even over several laps, changes nothing. A second lap only rewrites steps that get new playing; a chord that crosses the loop end keeps its notes when the next lap is idle. A new take never wipes the track. |
| Mute the recorded track (Shift + its screen button), take as above | Silent, but the take records what the pads play. |
| A take that ends by itself (transport Stop, track set Off, project load) | The cell shows for two seconds why: `rec: Stop event` (Stop), `rec: transport stopped`, `rec: track set Off`, `rec: reset`, `rec: pattern changed`. A loop that is complete (Shift + Rec) and your own Rec press show nothing. If a take ever ends by itself without a reason you caused, note the text: it names the cause. |
| (Triad chosen, see 7a) Hold a root pad for a few steps, release | The chord keeps sounding while held (Live). Press **Rec** again: the red `REC` goes. Leave Play mode (tap Pattern): the pattern page shows a note with the chord FX (`MIDI chord`) on the first step of the chord; steps that passed **while the chord was played** are replaced (a gap in the arp is an empty step), steps that passed while nothing was played keep their old notes. |
| Quantise | A note that starts in the first half of a step (before the middle) lands on that step, in the second half on the next step (like stock live record). With rec microtiming ON in the Config the note stays on its step and gets a micromove value instead. |
| Velocity | With rec velocity ON in the Config the step volume follows the sequencer velocity (the c31 groove), otherwise the volume stays default. |
| Undo | In the pattern editor press the stock **Undo**: the whole recording disappears in **one** step and the previous pattern is back. Redo brings it back. |
| Punch while Mono plays | Mono track: press Rec while the sequence runs: its notes replace the steps one by one; steps without a Mono note are cleared. Rec again: stops, the rest of the pattern is untouched. |
| Chord on a **sample** track (Chord/Arp type on tracks 1-8) | Only the lowest chord note is written. (The v0.1 hint texts are no longer shown on screen; the lowest note is still the one written.) |
| Chord that is not in the Tracker chord table (a 5-note chord, wide voicing) on a MIDI track | The highest notes are dropped until the table matches, the step gets the shorter chord FX. (No on-screen hint any more.) |
| Record a very busy arp (fastest speed, 1/32) for a long time | Normally nothing special. If steps are missing in a long busy recording, note what you did (it means the main loop was too slow to take the steps). |
| Stop the transport while recording | Recording ends by itself. Steps written so far stay, one undo step. |
| End of a recording (Rec, loop end, stop) while the last note is still held/open | A note-off is written on the next step that holds no note, so the last note does not ring into the old pattern content; a step that already has a note is left alone. |
| Change the pattern (Song mode, scene, queue) while recording | Recording ends at once (also if nothing was played), the cell shows `rec: pattern changed` for two seconds; the few steps still in the queue at the switch are dropped, nothing is written into the new pattern. |
| Set the recorded track to Off (screen button + wheel) while recording | Recording ends when the change applies (next bar). |
| Rec held in other pages | Outside Play mode Rec is the stock Rec (edit mode). Leaving Play mode (tap Pattern, any other module button) ends a running recording (punch-out note-off as usual); the pattern is not touched while you edit it. |

## 7b. Shift + Rec: exactly one loop
| Press | Expected |
|---|---|
| Transport running, Chord/Arp or Mono track: **Shift + Rec** | The track's cell turns red (`REC`). Nothing is written until the pattern wraps to step 1. |
| Wait for the wrap | At step 1 the sequencer's notes replace every step of the loop. |
| At the next wrap | Recording stops by itself, the red `REC` goes. The whole loop is one undo step. A note near the end of the last step that would quantise to step 1 is NOT written to step 1. |
| Transport stopped: **Shift + Rec** | The transport starts at step 1 and the loop starts at once. |
| Press **Rec** twice quickly (double tap, 0.1 s or more apart) | Recording starts and ends again (toggle); it does not restart. |
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
**Plan 005 R2.** The clip launcher is reached with the **Song key as a toggle** (v0.2.4): Song from any other page goes to the stock Song page; on the Song page a plain press and release of Song (no other button held, not held down, no timing) opens the launcher; Song in the launcher (either layer) returns to the Song page. A hold of Song from another page is still the stock momentary peek; a hold on the Song page itself does nothing. Screen button 8 switches between the CLIPS and the CUT layer; all 12 x 4 pads belong to the active layer. A *clip* = one track of one pattern, a *scene* = a whole
pattern. **Rows are patterns** (row n = pattern n, moved by hand, independent of the song arrangement). Needs patterns 1-8 that hold different notes on a few tracks, saved in the project. Tracks that have no notes in a pattern are empty clips.

## 9. Session view, CLIPS layer (plan 005 R2)
Pads (1-based): columns 1-8 = the 8 tracks of the window, **column 9 empty (gap)**, **column 10 = scene launch** of the row, columns 11-12 empty; rows 1-4 = 4 patterns. Screen buttons: **1 MODE** (hold + wheel), **2 QUANT** (tap), **3 STOP** (hold + pad), 4 and 5 empty (v0.2.3: the LIVE freeze and BACK TO SONG are gone, section 9c), 6 belongs to the active layer (CUT: SYNC ALL), 7 empty gap, **8 switches CLIPS <-> CUT** (its label shows the other layer). Left / Right = tracks 1-8 / 9-16, Up / Down = rows (Shift = 4 rows), jog = rows.
Screen (ASCII text and shapes only): a grid of 12 pattern rows x 16 tracks (gap after track 8) and the scene column; **a red frame round the 8 x 4 pad window** (and one round its scene cells); empty cell dark, clip with notes grey, playing green with a dark progress bar, queued blinking white, follow action armed = steady green outline; a notch in the top right corner = Play 1x (red notch = the next row is empty, the track stops after this clip); a dim square in an empty cell of a sounding track = pressing that pad stops the track; a white outline = the focused clip; a green bar on the left edge = the row the transport plays. Right column: launch quantise, countdown ("next in N steps"), the scene flag of the focused row ("Scene P: PLAY NEXT" / "LOOP"), focused clip and its mode, short help. A **bar along the top of a scene cell** = that scene is flagged **play next** (v0.2.3, section 9e).
| Press | Expected |
|---|---|
| Go to the Song page (Song from any page), then press and release **Song** once more (also with the slot list or the pattern list focused) | The launcher opens: grid, red frame round tracks 1-8 x rows 1-4, labels `MODE LOOP` `Q: bar` `STOP` (buttons 4 and 5 empty) and `CUT` on 8. Pad LEDs: playing clips bright (the loaded pattern's row), clips with notes dim, empty clips off (a dimmer light for empty pads of a sounding track), scene pads of the loaded pattern bright. |
| v0.2.4 Song toggle: in the launcher (CLIPS, then CUT) press Song | The stock Song page opens each time. Press Song again: the launcher. Repeat five times. |
| Song page: press Song while holding any other button (Shift, an arrow) | No launcher. |
| Song page: hold Song (about a second) and release | Nothing happens (no launcher, no page change). |
| From the Pattern page hold Song, release | The Song page is shown while held and the previous page returns on release (stock peek). A quick Song press from the Pattern page lands on the Song page and does not jump on to the launcher. |
| Press **Song** once (a single quick tap on the Song page or another page), or hold it > 0.4 s, or press another button with it, or tap twice slower than 0.35 s | No launcher; the stock behaviour as before (single press = Song page, hold = momentary peek and back on release). |
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
| Tap a **scene** pad (column 10) | v0.2.4: the scene follows the launch quantise like a clip. `Q: bar` / `Q: now`: the pattern is read at once (no stall, the grid keeps drawing); the scene pad blinks and "next in N steps" counts to the next bar (<= 16) / the next step. At that step the whole pattern switches in the middle of the pattern: every track goes on at the global position inside its new length (the groove stays in phase), all clip states reset to "the loaded pattern", stopped tracks play again. If the read is not finished at that point the switch waits for the next bar; in the last two steps of the pattern the stock switch takes over (it reads again and swaps at the pattern end), so the scene never gets lost. If no bar comes before the pattern end the pattern end switches (stock). `Q: pattern`: as before, at the pattern end, starting at step 1. While the song plays, the song mode goes on (v0.2.3): the song stands on the new pattern's slot, and the slot loops again. |
| Transport stopped: tap a clip / scene / empty pad | Takes effect at once (nothing to wait for). Press Play: it plays. |
| Stop the transport with launches queued | **Stop is the panic**: queued launches and stops are dropped, launched clips end, every track plays its own pattern again (the pattern is restored from its file), a queued Song page slot is dropped. Play again = stock playback (the song loops its slot, v0.2.3). Pressing Stop with nothing launched changes nothing. |
| Screen button 8 (label `CUT`) | The CUT layer (section 10); the label now reads `CLIPS`, button 8 again: back, clip states unchanged. Held pads are released when you switch. |
| v0.2.4 (B1): press button 8 CLIPS -> CUT -> CLIPS ten times in a row, with the transport playing and stopped | Every switch draws the other layer at once: the MLR lanes are gone after going back and the launcher grid (marks, countdown, labels) is complete; never "stuck in MLR". Leave the Session (e.g. Song), press Song on the Song page again: the layer you left is shown (CUT stays CUT), button 8 still switches both ways. |
| v0.2.4 (B2): pattern of 128 steps (or longer), Q: `bar`: tap a clip pad of a track that plays | The clip starts at the next bar (within 16 steps), "next in N steps" counts down from at most 16, not from the pattern length. With Q: `now` it starts at the next step. Only Q: `pattern` waits for the pattern end (128 steps here). Check with a track whose clip is 128 steps long and another that is 16. |
| v0.2.4 (B2): same 128-step pattern, tap a **scene** pad with Q `bar`, then `now`, then `pattern` | bar: switches at the next bar within 16 steps (the countdown starts at 16 or less, not 128), `now`: at the next step, `pattern`: at the pattern end. Check the new pattern starts in phase (a pattern with a steady kick on the beat does not shift), the launch does not glitch the audio, and a scene tapped twice quickly (other pattern) retargets. Song page: queue a slot with the jog / Enter: still at the pattern end (Song queue is not quantised). Stop with a scene queued: the scene is dropped (Stop is the panic). Edit a step while a bar scene counts down: the edit is saved by a new read and is in the new pattern after the switch (the old pattern keeps it). With Rec on, the scene is a pattern-end scene. If the song plays and the slot advances by itself, the scene still lands at the pattern end. A Play 1x follow action waits for the end of its clip by design (`clip end`); a tap of your own on that track replaces it and follows Q. |
| Pattern file safety: launch a clip on a track, wait 15 s (autosave), launch a scene or switch pattern, look at the original pattern | The original pattern is unchanged (launched clips were not saved into it); edits you made to other tracks of it are kept. |
| Launch own pattern's clip on a track that has a queued clip or follow action | The queued clip is cancelled, the track keeps/returns to the pattern's own part. |
| Leave the launcher (any module button) with clips running | They keep running. Hold no pad: nothing hangs. |
| Chord/Arp or Mono track taken over (Play mode) and a clip is launched on that track | The sequencer keeps priority while it takes over; the clip plays once it ends. |
| Busy messages | "Busy - try again" (red, bottom of the grid) when the file manager is busy or the command ring is full. Tap again. **Tracker Plus / Mini only**: "Unsaved edits: stop and save first" when the loaded pattern has unsaved edits and the transport plays (those units have no room for the own-data snapshot). |
| "Undo history cleared" | Shown for 2 s when the last launched clip went away and the pattern undo history was dropped (a recording followed by a launch loses its undo step). |

Behaviour since the review fixes: a clip launch copies the own data of the tracks it replaces into a RAM snapshot (about 1 KB per track, Tracker original) and **writes nothing to the card while the transport plays**; saving (after Stop, or an explicit Save) and the restore after Stop / panic take those tracks from the snapshot, not from the file. A pattern file that cannot be read aborts the save (the changed flag stays, nothing is overwritten) and the launched tracks are never cleared by a read error; scene launch refuses with "Busy" while clips run and the save did not happen; leaving the Session view and the end of a recording ask for the project autosave (like leaving the stock editor); leaving to Master or Instrument keeps the Session clips playing (no turn-off of performance mode), and entering the stock Performance page while clips run keeps their lengths; undo history is cleared when the last launched clip goes away (the view says "Undo history cleared").


### 9b. Play mode per clip (Loop / Play 1x) and the follow action
Default playback is **Loop**. The mode belongs to the clip (pattern x track), is saved in `seqclips.bin` and applies however the clip was started (pad, scene, follow action).
| Press | Expected |
|---|---|
| Hold screen button **1** and tap a clip pad | The pad (and its cell, white outline) is **focused**, nothing launches or stops while button 1 is held. The right column shows "Clip P  track T" and LOOP. |
| Still holding 1, turn the wheel one detent clockwise | The focused clip becomes **PLAY 1X**: notch in its cell, label `MODE 1X`. One detent back: LOOP. No wrap, no change beyond the two modes. |
| Hold **Shift + 1**, wheel clockwise (focus any pad or scene pad of the row) | v0.2.3: sets the **scene flag** of that row, not the clips: the right column reads `Scene P: PLAY NEXT`, the scene cell gets a bar along its top; wheel back: `LOOP`, bar gone. The clips' own modes of the row are unchanged (section 9e). |
| Release 1, save the project, power cycle, open it | The modes are still set (`seqclips.bin` next to the project; not written while the transport plays - they are saved after Stop). A project from before has all clips on Loop. |
| Transport running, launch a Play 1x clip (pattern P) on track T and let it play | When it has played once, **track T continues with the clip of pattern P+1** (same track, the row below) at the clip end, without a gap. While it plays, the next cell shows a steady green outline (armed), not blinking. |
| Same, but pattern P+1 has no notes on track T (or P = 255) | After one pass track T **stops** (silent). The notch in the cell is red from the start (END marker). |
| Chain: clips P, P+1, P+2 all Play 1x with notes, P+3 empty on that track | The track plays P, P+1, P+2 once each and stops. The clip length of each decides when the next one starts. |
| While a Play 1x clip plays, tap the playing clip again (or another clip of that track) | Tapping the playing clip cancels the follow action (it loops on); another clip replaces it. The follow action does not come back for the same clip instance. |
| A scene (row) that is all Play 1x | Every track plays its clip once and continues in the next row; a track without a clip below stops. |
| Stop the transport during a follow chain | Panic: tracks are back on the loaded pattern; Play starts stock playback. |
| File manager busy at the clip end (e.g. while a save runs) | The clip plays once more and the follow action follows at the next clip end (never silence, never a switch in the middle of a step). |

### 9c. Launching while the song plays (v0.2.3: no LIVE freeze any more)
The song loops its current slot by default (section 9e), so a launch does not have to stop the song from running away. The `LIVE: song frozen` line, button 4 `BACK:SONG` and the freeze of plan 005 are removed.
| Press | Expected |
|---|---|
| Play the **song** (Song page, Play), open the launcher, launch a clip | The clip plays; **the song stays in song mode and keeps looping its slot**, so the clip is **not** wiped at the pattern end (the slot repeats; the clips only end when the pattern switches). No "LIVE" text, button 4 is empty. A green bar on the left edge marks the row of the playing pattern. |
| Launch a scene while the song plays | The scene blinks, at the pattern end the pattern switches (clips end), the song stays in song mode and now stands on the slot that holds that pattern (the first slot with it from the current position on; none in the song: the pattern loops as a stock pattern loop). |
| Play 1x clip while the song loops its slot | The follow action works as in pattern playback (next row down at the clip end). On a slot flagged play next the song advances itself and follow actions are not queued. |
| Press **Stop** | Panic (see above). Play again: stock playback, the song loops its slot. |

Known limits (R2): on a slot flagged play next (the song advances by itself) a Play 1x follow action is not queued. A Play 1x clip counts its first pass from the step Play started at (or the last pattern switch / join). The follow actions of tracks that continue into the same row share one read of the card (at most one attempt per step; after three read errors in a row the track stops at the clip end instead). A follow action is not started on the last step of the pattern, while a scene is queued, or while one of your taps is still on its way to the audio side. Undo snapshots hold the RAM pattern; the "has notes" display of a pattern that is not loaded comes from the stock Song bitmasks (updated when that pattern is saved); clips are copied into the loaded pattern in RAM (stock Performance mechanism), so edits to a launched clip are lost on save/restore. The Tracker Plus / Mini keep no snapshot: there the file is the source and a launch while playing is refused when the pattern has unsaved edits.

### 9d. Display list measurement (device, once)
The clip screen is budgeted in software (grid at most about 5.5 KB of the 8 KB frame, optional parts dropped first: armed outlines, notches, dim squares, help lines). Verify on the unit with a **DEBUG build**: `display.lastMemoryUsed` (display.cpp step3) must stay below 8172 B in the worst case - all 16 tracks playing, many Play 1x notches, the scene flags and the help lines (hold button 1 or 3), a message showing. If the screen loses its last controls (labels or the grid's right column) the budget is too high.

### 9e. Live-set Song page: loop by default, play next, queue a slot (plan 006, v0.2.3)
The song no longer runs away: **at the pattern end the playing slot plays again** unless its scene (pattern) is flagged **play next**; then the song goes on with the next slot as stock. The flag belongs to the **pattern** (every slot that holds the pattern shows it), is saved in `seqclips.bin` (version 2; a v0.2 file loads with every scene on Loop) and is not written while the transport plays. Use a project with a song of 3+ slots with different patterns (e.g. slots 1-4 = patterns 1, 2, 3, 2).
| Press | Expected |
|---|---|
| Song page, **Play** (song) with no flags set | Slot 1 plays, at its end **it plays again**, endlessly. The song position does not move; the play icon stays on slot 1. |
| Hold screen button **1** (play pattern) and turn the wheel clockwise (any page position, nothing in edit mode needed) | The selected slot gets an **arrow** at the right end of its line: the scene of that slot is flagged **play next**. Every other slot with the same pattern shows the arrow too. Wheel back: arrow gone. Releasing button 1 after a wheel turn does **not** toggle song / pattern mode. |
| Tap button 1 without the wheel (stock: song <-> loop toggle) | Works as stock but on **release** of the button (a tap is a press and release without a wheel turn; **Shift is sampled at the release**, so Shift + Play still works but Shift must be down when button 1 comes up). |
| Flag slot 1 while the song plays on it | At its pattern end the song goes on to slot 2, which loops (unless it is flagged). The flag also works when set just before the pattern end (the last step decides: set it a step earlier to be sure). |
| Flag the **last** slot only, play the song from the last slot | The song does **not** end or wrap to slot 1: the last slot loops (a flag on the last slot changes nothing). |
| Play the song with **no** flag on the last slot, slot 1 flagged, slot 2 flagged ... the last one flagged | Slots advance in order, the last slot then loops forever. |
| While the song plays slot 1 (edit mode **off**), move the cursor with **Up / Down** or the wheel to slot 3, then press **Insert** | The cursor stays on slot 3 (it does not snap back to the playing slot) and the label under `Stop` reads `Ins: queue` until Stop or the song moves to another slot. Slot 3 gets a **red square** at the right end of its line (queued). At slot 1's pattern end the song switches to slot 3 (loading is the stock scene mechanism, no stall), the square disappears, slot 3 loops. The cursor and play icon follow slot 3. |
| Queue slot 3, then queue slot 4 before the pattern end | The red square moves to slot 4; only slot 4 is played next. |
| Queue slot 3, select the **playing** slot, press Insert | The queue is cancelled (square gone), the song stays on the playing slot. Insert on the playing slot with nothing queued does nothing. |
| Queue a slot that holds the same pattern as another slot | The queued slot is the one that plays next (the song position goes to it, not to the first slot with that pattern). |
| Queue a slot that holds the **loaded** pattern (e.g. slots 2 and 4 are both pattern 2, playing slot 2, queue slot 4) | Red square on slot 4; no reload or switch happens; at the pattern end the song position (and the play icon) moves to slot 4. |
| Press Insert exactly at the end of the pattern (the last step) or while the switch is already loaded | Nothing is queued and `Busy` shows under `Stop` for a moment: press again. |
| Play the song, tap a scene pad of a pattern that is **not** in the song, then a scene pad of a pattern that is | The first switch makes the pattern loop (song mode off, as stock); switching back to a pattern of the song puts the song mode back, and the slot loops / advances again. A deliberate loop (button 1 `Loop Pattern`) stays a loop. |
| Queue a slot, then tap a **scene pad** in the launcher / press Stop | The queue is dropped (the square disappears); the scene / stop wins. |
| Insert while the transport is stopped | Nothing (select a slot and press Play to start there as stock). |
| Launcher: hold button 1, tap a scene pad (or clip pad of the row), **Shift** held, wheel clockwise | Right column: `Scene P: PLAY NEXT`, bar along the top of the scene cell; on the Song page the slots with pattern P show the arrow. Both screens always show the same flag. |
| Launcher: hold button 1 and turn the wheel **without** Shift | Sets the focused clip's Loop / Play 1x only (section 9b); the scene flag is unchanged. |
| Save the project, power cycle, open it | The scene flags are still set (`seqclips.bin` next to the project). A project from v0.2 has no flags (all loop). A `seqclips.bin` written by a **newer** mod version is left untouched (not loaded, not overwritten): no downgrade. |
| Song page: edit the playlist (Add / Delete slot, + / - pattern, copy / paste) | Works as stock; the marks follow the pattern numbers (they belong to the pattern, not the slot). |
| Pattern break effect (jump to next pattern) on a looping slot | The pass is shortened and the slot plays again (a break is a short loop in a live set); on a slot flagged play next it goes on to the next slot. |
| Export audio of the song (Master / Export) | The song plays through once as stock (loop and flags are ignored while exporting); as stock the export renders the **whole playlist**. |
| Song page: use the stock "loop <-> song" toggle (button 1 tap / Shift + Play) | Stock: song mode off = the pattern loops with no flags; on = song mode with the loop-by-default rule. |
| Pattern page: Pattern + Right / Left while the song plays | The pattern switches at its end; **the song mode goes on** (v0.2.3; stock ended it): it only ends when the new pattern is not in the song. |
| Performance page with per-track patterns changed, leave it while the song plays | At the pattern end the pattern is reloaded (the changed tracks are put back), also in song mode; a slot flagged play next then plays one more time before it advances. |

Known limits (plan 006): the play-next flag and the queue use the cursor slot; marks of long pattern names can overlap the names. A slot queued at the very last step of a pattern is taken one pattern later (the stock scene arm rule).

## 10. Session CUT (MLR playhead, plan 002 S2)
Open it with Song, Song (Song page, then launcher) > screen button 8 (CUT). Title "Session: CUT   tracks 1-4"; screen buttons 1-4 choose the four tracks shown (1-4, 5-8, 9-12, 13-16); the CUT layer draws its own lane screen (no info lines). Use a project with: track 1 = ONE long
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
| Press two pads of a row (e.g. 2 and 4) | A loop over positions 2..4 starts at the next grid point (see Q); the pads between are lit as loop. **The loop latches (MLR style):** let go of the pads and it keeps turning. Tapping **one** pad in that row jumps there and ends the loop (so do RESYNC and SYNC ALL). Releasing both pads before the grid point still starts the loop. |
| Press three pads | The loop covers the outer pads. |
| STOP pad (9) of a row | The row goes silent at once (sounding note stops); the pad LED is bright, the row's position pads go dark. The other rows keep playing. Press again: the row starts again at the global position (RESYNC). |
| REVERSE pad | The row plays its positions backwards (each step replays the sample from the matching offset, forward). Press again: forward. LED bright while reversed. |
| RATE pad | 1 press = double (the row steps through the pattern at 2x; two triggers per step), 2 presses = half (each position lasts 2 steps, nothing triggers on the second), 3 = normal again. |
| Jump around, then RESYNC pad | The row snaps back to the global position (retriggering the sample at that offset); loops, reverse, rate, stopped state and a recorded loop of the row are cleared; the row is stock again (RESYNC LED dim). |
| **Rec toggle** (plan 008 A): press Rec once and let go (the key may be held for seconds, it does not matter), jam jumps / loops / reverse / rate on row 1, press Rec again | Screen button 7 shows `[REC]` while the take is armed (the header turns red). The first pad of a row starts that row's take (RESYNC pad blinks); with Q beat / bar the take starts at the beat where that first gesture takes effect, not at the press. Letting go of Rec does not end anything. The second Rec press closes all takes on the next grid point (the length is a whole number of beats / bars): the row repeats what you played during the take, step for step (also rate x2 with its second half-steps, reverse), forever (RESYNC LED bright = a recorded loop replays). Rows you did not touch during the take are not changed. A take in a group you are not looking at is closed by the same press. |
| **Jump quantise** (screen button 5, label `Q beat`): tap it | The label cycles `Q bar`, `Q off`, `Q step`, `Q beat` (default beat). With beat a pad jump (and the start of a two-pad loop) waits for the next beat (4 steps), with bar for the next bar (16 steps; a pattern that is not a whole number of bars uses its own length as the bar), with `step` for the very next step, with `off` it is played at once (the next audio tick, even in the middle of a step). A pad released before the grid still jumps. STOP / REVERSE / RATE / RESYNC pads act at the next step, not on the grid. Counting is from the pattern start. |
| Header of the lane screen (no take armed) | Shows the pad functions: `2 pads = loop  9 stop 10 rev 11 speed 12 resync  Rec = rec`; while Rec is armed it reads `REC  play, press Rec again to loop it`. |
| With a recorded loop running: switch the group (rows show other tracks), switch layers / pages | The recorded loop keeps playing. Switch back: LEDs show it. |
| Press a pad on a row with a recorded loop | The pad overrides the recording while it is held (jump, loop); on release the row returns to the recorded loop in phase. |
| Stop and start the transport | Stop is the panic: SYNC ALL ends everything CUT (jumps, loops, reverse, rate, recorded loops); Play gives the stock pattern. |
| Song mode / pattern chain with a cut row | The song advances at the real pattern end (not at the cut position); the cut row keeps its own position inside the new pattern (clamped to its length). Check: the next pattern loads and the song does not hang or skip. |
| Rate 1/2 on a row with a micromove (delayed) note | The delayed note still fires; the row never goes silent for good. |
| STOP a row, then START | The row is silent while stopped, restarts on the global position; a note you hold while live recording is not cut off. |
| Open another project / New project while rows are cut | All rows are stock again. |
| Track with a Slice / Beat-slice / Granular / Wavetable instrument | Jumps retrigger the note but without an offset (Position FX means something else there). |
| Long sample streamed from the SD card (>= 256 KB, original Tracker) | Jumping past the first ~0.7 s may start late or with a short gap (the stream buffer refills on the main loop). Note how long the gap is and whether the main-loop redraws make it worse (listen on the loop track while a screen redraws). |
| Pad jumps + an FX-heavy step (note step with 2 FX in use) | The retriggered note loses its second FX unless that is Slice / Reverse / Granular length (then it is replayed without offset). |
| Save the project, reload | CUT state is NOT saved: all rows are stock after loading. Recorded playhead loops are lost with the power-off. |

Known limits (S8, S9): recording on a CUT-active or launched track is not supported (a launched track refuses Rec with "Clip launched - not recorded"; on a CUT-active track the written steps are odd); a CUT release (pad up) that was lost because the queue was full is retried on every page (cutUpdate).

Known limits of the CUT layer: the offset assumes the sample plays at its original speed; the offset has full 16-bit resolution and follows the note pitch / tune;
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
| In CUT hold two pads (loop), then press screen button 8 (CLIPS) | The loop ends at once (pads are released by the layer switch). Rec armed while switching: the take is closed. |
| Leave the Session view (any module button) while a CUT pad is held / Rec is armed | Nothing hangs: loop ends, take closed. |

### 10c. CUT lane screen, SYNC ALL, survival and panic (plan 005 R4, v0.2)
The CUT layer draws its own screen: header (y 32-62) "CUT 1-4", **four lanes** (one per pad row), each split into the 8 pad segments (alternating shade), note marks, beat / bar ticks on the lower edge, a thin **playhead line** that moves smoothly (not
per segment) and the segment it is in lightly filled, a cyan **grid ghost** (small pin) where the track would be without the jam, an orange **loop bracket** over the held loop, and the right block (two by two cells = pad columns 9-12: STOP, REVERSE, RATE, RESYNC).
Left of a lane: the track number and what makes it special (`OFF GRID`, `LOOP`, `REPLAY`, `STOP`, `REC`, `REV`, `1/2 speed`, `2x speed`). Nothing is drawn over the stock title bar. Bottom labels in CUT: `1-4 | 5-8 | 9-12 | 13-16 | Q beat | SYNC ALL | (empty or [REC]) | CLIPS`
(jump quantise on screen button 5, SYNC ALL on button 6, button 7 shows `[REC]` while a take is armed, button 8 switches back to CLIPS).
Use the project of section 10 (track 1 loop sample, track 2 drums, track 3 MIDI) and a second group with a few more tracks.

| Press | Expected |
|---|---|
| Open CUT, transport running, touch nothing | Four lanes, one playhead line per lane running left to right in step with the pattern, all in the same segment rhythm (bar.beat in the header counts on). Tracks you have not touched show no label under the number and no ghost. |
| Pattern with 12, 24, 40, 100 steps | The line always stays inside the segment of its pad (LED column = filled segment); with fewer steps than 8 the extra segments stay empty. |
| Jam: press pad 5 on lane 1 | The line jumps to segment 5; label `OFF GRID`; a cyan ghost keeps running where the stock pattern would be; the RESYNC cell fills. Press RESYNC: line and ghost meet again, label gone. |
| Hold two pads | Orange bracket over the loop segments, label `LOOP`; release: bracket gone. |
| Reverse / rate / STOP | The matching right-block cell lights (reverse triangle points backwards, 2x / 1/2 text bright, STOP = square and the lane dimmed, line dim); labels `REV`, `1/2 speed`, `2x speed`, `STOP`. |
| Press Rec (toggle), jam | Header turns red with `REC` and screen button 7 shows `[REC]`; the touched lanes get a red edge and a red RESYNC disc; press Rec again: header normal, the lane shows `REPLAY` and a ring in the RESYNC cell while the recorded loop runs. |
| Pattern edit (another view) and back | Note marks follow the notes (they are re-read about every 100 ms, redraw only when they changed). |
| Group 5-8 (button 2 or Right), 13-16, Left | Header and lanes show the group; Left / Right page the groups, nothing happens past the ends. Jams of other groups keep running. |
| Playhead smoothness / sequencer timing | While the transport runs, the line moves smoothly (about 25 frames per second) and pads, arps, Mono and the pattern stay on time. Listen for audible stalls with long SD-streamed samples (the main loop also refills the streams): note them. |
| **SYNC ALL** (screen button 6) with several lanes jammed, reversed, 2x, looped, one recorded, one stopped | Within one step (next step boundary, never mid-note) every track is back on the stock position: lines on the ghost, all labels gone, all right-block cells normal, the bracket gone, the stopped lane plays again. The label of button 6 shows `[SYNC ALL]` only while some track is off the grid. Check by ear: all tracks play exactly the stock pattern. |
| Same with a track of another group (open 9-12 first, jam, go back to 1-4, press SYNC ALL, open 9-12 again) | That track was synced too (SYNC ALL covers all 16 tracks). |
| Shift + RESYNC pad of any row | Same as SYNC ALL (all tracks, not only that row). RESYNC without Shift: that row only. |
| SYNC ALL while holding a pad / while a take is open | The held pads and the open take are dropped; letting go afterwards does nothing (no stray loop, no late take). Pressing a pad again starts a new jam. |
| SYNC ALL with the transport stopped, then Play | Every track starts on the grid (no jam left over). |
| SYNC ALL while a Session STOP (clip launcher) holds a track | That track stays silent until a clip is launched; the snap waits for it (Session STOP wins). |
| Jam on rows with CUT, switch to CLIPS (screen button 8) and launch clips, switch back | **M5:** the jams, reverse, rate, STOP and recorded loops of the CUT tracks kept running while CLIPS was shown (only a pad held in your hand is released and an open take closed). The lanes show them again. |
| Leave the Session view with jams running, play the pattern editor | The jammed tracks still play their jam (stock editor does not touch them) until SYNC ALL or Stop. |
| **Transport Stop** (panic, plan 005 M6) with jams, reverse, a recorded loop, held pads, Rec held | Everything CUT ends: nothing is left off the grid; **Stop then Play always gives the stock pattern**. Releasing the pads / Rec afterwards does nothing. Works from any view (check from the pattern editor and from CLIPS too). |
| Stop pressed when CUT was never used | Nothing visible happens (the command is a no-op). |
| **SYNC ALL landing on a note** (jam a long loop sample on track 1 to another position, press SYNC ALL) | The sample is struck again from the matching offset at the boundary: it does not keep playing off the grid. Same for plain RESYNC. |
| Panic (Stop) while stopped, with jams | Lanes and the R1 strip show the tracks as normal at once (no OFF GRID, label without brackets). |
| Display-list worst case: 128-step patterns on all four lanes, every flag set (stopped, reverse, rate, Rec) | No truncated or missing shapes (the whole frame must fit 8172 bytes: read `lastMemoryUsed` in a DEBUG build; host estimate for the lanes alone is about 3.6 KB). |
| Main-loop gap with the 25 fps playhead (Profiler: longest main-loop pass while CUT is visible and the transport runs, long SD-streamed sample playing) | No audible stream underrun. If there is one, set `kRedrawPerStep = true` in `seqCutLogic.h` (one redraw per step instead of 25 fps) and repeat. |

# Alt Performance page (8-step faders) — plans/004-perform-faders.md, reworked in plan 005 R3 (v0.2)

Entry is the **top-row Performance button** (firmware "Performance") from every other page; the stock Performance page is **Shift + Performance** (owner change 2026-11-04). On the stock page itself a plain press keeps its stock meaning (flips the tracks page) and Shift + Performance goes to the fader page.
Fader positions and active FX are NOT saved with the project: after loading a project every perform FX is off.
Provisional parts (owner did not specify): LATCH pads, jog-wheel settings, the steps' value mapping, what DISENGAGE does to PING / REV project values (restores the earlier value).

## 11. Alt Performance page (the Performance key; stock page = Shift + Performance)
Setup: a project with tracks playing; on the stock Performance page give the slots a few FX (e.g. slot 1 low-pass, slot 2 sample volume,
slot 5 pan, slot 9 tune).

| Press | Expected |
|---|---|
| **Performance** (plain press, from any other page) | The fader page opens. |
| **Shift + Performance** (from any other page) | The stock Performance page opens (as always). |
| **Performance** pressed twice quickly on the stock page | Stock behaviour only: the tracks page flips and flips back (1-8 / 9-16). The fader page does NOT open. |
| **Performance** (plain press) on the fader page | Nothing happens; the fader page stays (no flip). |
| **Shift + Performance** on the stock Performance page | Fader page. The tracks page and the 8 track buttons are as you left them; FX that were latched on the stock pad rows show as lit rows. |
| **Performance** (plain) from Pattern, Song, Master, Params, Sample pages, File ... | Fader page. v0.3: the **track selection is the one you last set** (the brackets under the screen and the "affects tracks ..." header show it; on a fresh start, with nothing ever selected, the header says "no track selected" in a warning colour). Rows you left dialled in are still ENGAGED (see 11d); otherwise every row is DISENGAGED. |
| **Shift + Performance** on the fader page | Back on the stock Performance page; the 8 buttons keep their state; FX that were playing from the stock pad rows still sound and their pads are lit. |
| Screen layout | Below the title bar: a chip row (FX 1-4, FX 5-8, FX 9-12 | PING, REV = pad columns 9 / 10; the page you are on is filled, a green dot marks a page with something dialled in) and on the right "affects tracks 1-4 7". Under the chips the step numbers 1-8. Four bands = the four pad rows: row number, FX name, current value, a long bar of 8 steps (lit from the left), a LATCH and a DISENGAGE cell (pad columns 11 / 12), the state word under them (DISENGAGED / ENGAGED / LATCHED; PING / REV project rows: PROJECT / DIALLED) and a second line (slot number and range). A strip at the bottom shows the group line (FX pages: how many are dialled in; PING: time / rate, feedback, filter, ping-pong, sync; REV: size, damp, predelay, diffusion). An empty slot is drawn dim with "(empty slot)". |
| Pad LEDs | Pad rows 1-4 lit only where a slot has an FX; column 9 group pads: current page bright, a page with something dialled in medium, others dim (row 4 dark on purpose); PING / REV (column 10) likewise; LATCH column dim (bright when latched); DISENGAGE column (12) bright where its row has something to take off, dim otherwise. |
| Hold a pad, Shift + Performance press | Leaves at once (a held pad does not block it). |

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
| Tap **PING** | Chip PING filled; faders: 1 delay send (DISENGAGED until used), 2 time (or rate when sync is on), 3 feedback, 4 bandpass center - bars 2-4 show the current project values, state PROJECT (default time 500 ms = pad 3, feedback 50 = pad 5, center 20000 = pad 8 = "open", the filter is out). The bottom strip shows what the jog wheel changes (v0.2.4, see 11e) and ping-pong / sync. |
| Hold pad 6 of fader 1, release | Delay send of the selected tracks goes up (+63) while held, back to normal on release (LATCH keeps it). |
| Press pad 2 / 5 / 8 of fader 2 (sync off) | Delay time changes (about 219 / 1367 / 3500 ms) and STAYS after releasing; the band shows ms and the state DIALLED. |
| Pad row 3, column 10 (below REV; v0.2.4: it used to be the jog wheel) | Ping-pong on / off (the pad is bright when on, the bottom strip shows it); audible stereo bounce. |
| Pad row 4, column 10 | Sync on: fader 2 is now the rate (band "Rate 1/8" etc.), pad 1 = "8", pad 8 = "1/32". Tap again = sync off. (Both pads are dark on the other pages.) |
| Fader 3 pad 8 | Feedback 95 (the cap; v0.3: it was 88, and 99 before v0.2.4 which crackled with loud sustained sounds; the delay soft-limits, so it compresses instead); fader 4 pad 1 = a band around 194 Hz (dull, thin repeats), see 11e. |
| **DISENGAGE** of row 2/3/4 after you moved it with the fader | The value it had BEFORE your first fader press comes back (time and rate index, feedback, bandpass center), state PROJECT again; a second DISENGAGE does nothing. On a row you never touched DISENGAGE does nothing. Row 1 (send): off. |
| Tap **REV** | Fader 1 reverb send of the selected tracks; 2 size, 3 damp, 4 predelay (0-100); jog wheel = low cut (highpass), Shift + jog = diffusion (v0.2.4, see 11e); the bottom strip shows it. DISENGAGE restores the value from before the fader touched it. |
| Change PING / REV values, save the project, power off, open it | The values are saved with the project (same as changing them on the Master page); open the Master page: it shows the same values. The fader positions / latches / engaged state are NOT saved: after loading, the fader page is all DISENGAGED / PROJECT. |
| No volume or panning on this page | Correct (owner decision). |

LATCH pads of PING / REV rows 2-4 are dark and do nothing (project values always stay).
Known: delay time jumps (no smoothing in the delay engine, same as the Master page), so a press may click while playing.
Known limits: FX that exist only for synth tracks / global FX use the stock slot rules; two slots with the same FX exclude each other
(stock rule), the older fader bar may go dark.

Known limits (v0.2 review): the stock 10 s autosave (fileManager) still writes while playing: R1 recordings set the pattern-changed flag and a PING / REV jog on the fader page sets the project-changed flag, so a save can run during play (watch for stream hiccups). On the Song page, the Play toggle back to song mode ends kept fader FX (stock behaviour). Kept fader FX continue into pages opened by an event (popups) until Stop.

## 11d. Leaving the fader page, Stop panic (plan 005 owner decisions)
Setup: tracks 1 and 2 selected, a filter on slot 1, a volume FX on slot 2; latch rows 1 and 2 and press pads so both are ENGAGED / LATCHED.

| Press | Expected |
|---|---|
| **Pattern** (opens the pattern editor), then tap Pattern again (Play mode) | The latched FX keep sounding in the pattern editor and in Play mode. Pads in Play mode drive the sequencer as in R1. |
| **Song**, then the clip launcher (Song page, Song), MLR layer | The latched FX keep sounding in the launcher and in MLR; launching clips does not end them. |
| Back with the plain **Performance** key from any of those | The fader page opens with the same rows ENGAGED / LATCHED (positions, latch, page you were on), nothing jumps. |
| Hold a pad (not latched) and press Pattern | The held pad ends (momentary), the latched rows stay. |
| Latched FX running, press **File**, **Config**, Sample pages (v0.3, plan 010) | **The FX keep sounding** on every module, also these (the stock rule that ended them at the pattern end is gone). They end only by DISENGAGE, UNLATCH or Stop. Go from there on to other pages, then back with the plain Performance key: the rows are still ENGAGED / LATCHED. |
| Latched FX running, press **Master** or **Params**, then the Instrument parameters, then leave them to any page (v0.3) | The FX keep sounding the whole way (those stock pages no longer turn performance mode off while the fader FX are kept). |
| Latched FX running, press **Shift + Performance** | Stock Performance page; the FX that were kept from Pattern / Song end (the stock page starts fresh); a fader row you handed over from the stock page keeps working there. |
| Latched FX running, any page, press **Stop** | Every perform FX ends (panic): fader rows DISENGAGED (also latches), rows handed over from the stock page too. PING / REV project values stay. Stop then Play = plain playback. |
| Same on the fader page | Press Stop: all rows go DISENGAGED at once, the page stays open. |
| Launch clips, open the fader page, dial an FX, leave and press Stop | Clips are stopped by the launcher panic, the FX ends; no pattern is reloaded while clips still own performance mode. |
| Save the project with FX engaged, load it | All perform FX are off after loading (also a latched FX kept into the Pattern editor, even if you load while stopped); performance mode is not left on. |
| Latched FX kept into the Pattern editor, edit steps (unsaved), press Stop | The FX end; the pattern is NOT reloaded from the SD card, the unsaved edits stay (reload only happens when per-track performance patterns were loaded on the stock page). |
| From the Pattern editor (FX kept) go on to File / Config / Sample pages | v0.3: the kept FX stay engaged there too (no end at the pattern end). Only DISENGAGE, UNLATCH or Stop end them. |
| **Track selection stays (v0.3, plan 010).** Select tracks 2 and 5 on the fader page, engage a row. Leave to any page and come back; press Stop; press Play and Stop again; DISENGAGE the row (also Shift = all); Save and load a project; switch to the stock Performance page and back | After every one of these, the fader page still shows tracks 2 and 5 selected (brackets and "affects tracks 2 5"). Only a press on a track button changes it. A selection made on the stock page and handed over with Shift + Performance becomes the new selection. |
| Select tracks 2 and 5, power off, power on, open any project, open the fader page | The same two tracks are selected (the selection is stored in `seqfx.bin` of the project that was open and is read from the first project loaded after power-on; later project loads keep the current selection). A project saved before v0.3 has no selection: the one you had stays; on a fresh start none is selected. |
| On the stock page hold Shift while pressing Performance (to the fader page), keep Shift down and press a pad / jog | Shift is ignored until you release it once (no accidental Shift + DISENGAGE or Shift jog). |
| Leave the fader page while playing (any button) | No SD / config write at that moment; DISENGAGE of a stock pad row marks the perform data, written after Stop. Device test: moving a PING / REV fader 2-4 sets the project changed flag, so the stock 10 s autosave can run while playing (same as the Master page). Watch for stream hiccups. |

## 11e. FX filters: delay bandpass, reverb low cut, reverb type "ShimmerReverb" (plan 007, v0.2.4; v0.3 changes: plan 010)
Listen on headphones or monitors, with a drum loop or a held noisy sound sent to the delay and the reverb (send faders up). Lower the
return volumes first: a bandpass, a low cut or the other reverb type changes the level.

| Check | Expected |
|---|---|
| **Delay bandpass.** PING page, ping-pong on, feedback about 60, a full-range loop in the delay. Turn the jog wheel left from the top (the bottom strip shows `[jog] center ...`, the fader 4 bar follows) | At the top ("open", 20 kHz) the delay sounds as before v0.2.4. A step down from there the repeats lose lows and highs; the further down, the more the repeats are a narrow band that moves with the wheel. Each click is one semitone; no clicks or zipper noise while turning fast, also with high feedback. |
| Press fader 4 pads 1 .. 8 while repeats ring | The center jumps in octaves (194 Hz ... 20 kHz open) and the repeats follow smoothly (no click); the jog wheel then continues from that value, the Master page delay filter shows the same value in Hz (and the other way round). |
| **Width.** Hold **Shift** and turn the jog wheel (the strip shows `[Shift+jog] width 1.0 oct`) | Right = wider (up to 3.0 oct, almost the whole spectrum), left = narrower (down to 0.3 oct, a ringing thin band). Default 1.0 oct. The value stops at both ends. No clicks while turning. |
| Narrow (0.3 oct) with the center at about 1 kHz and feedback 80 | The repeats turn into a whistle-like band, they never get louder than the input (no runaway). |
| Center to the top end again (the last 12 clicks below 19 kHz) | The band fades in / out smoothly over the top octave; one click below "open" the repeats are still almost unfiltered; "open" shows as `open` on the Master delay page and the PING page. No click when it reaches or leaves "open" (the mix is ramped per sample). |
| **Saved.** Set center 800 Hz and width 2.0 oct, Save, power off, load the project | Same center and width after loading (the width is stored in `seqfx.bin` next to the project). A project made before v0.2.4 (no `seqfx.bin`) loads with the width 1.0 oct and its old center; if that was 20000 the delay sounds exactly as before. |
| Turn the width while the transport plays, do not stop for 30 s | No SD write happens while playing (no stream hiccups); the file is written some moments after Stop (or on Save). |
| **Caps.** Loud sustained sound (a held chord or a bass) sent fully to delay and reverb. PING fader 3 top step, then the Master page delay feedback 95 and 99; REV fader 2 top step, then Master reverb size 95 and 100 (v0.3: both caps are **95**), with the reverb type Stock and with ShimmerReverb | No crackle or hard-clip distortion at the top steps; a loud tone in the delay repeats compresses and keeps going; the stock reverb tank and the ShimmerReverb ring saturate softly (a sustained loud bass or kick train gets denser and compressed, never a crackle or a wrapped (inverted) burst). The fader tops are feedback 95 and size 95 (bar full); the Master page may show 96-99 / 100 but plays at the cap, also for a project saved above it. Size 95 rings very long: it is the reverb tail, switching the type clears it. |
| **Reverb low cut.** REV page, a sound with plenty of bass (kick, bass line) in the reverb send | Jog wheel right: the low cut rises from off (strip shows `low cut off`, then 30 Hz ... 5.1 kHz in semitone steps; v0.3: the range ended at 1.8 kHz); the rumble of the reverb leaves first, then the body. Left = lower, back to off at the left end. No clicks while turning, also while a tail rings. |
| Low cut at about 500 Hz, then Shift + jog | Shift + jog now changes the diffusion (strip: `[Shift+jog] diffusion NN`), jog alone still the low cut. The REV fader 4 note shows both values (`jog = low cut ... Shift+jog = diffusion`). |
| Low cut turned all the way up (about 5.1 kHz) on a drum loop | Only the top of the reverb is left (hi-hats, air); no click while it is turned; the low-cut values saved by an older version are unchanged. |
| Low cut with the reverb type Stock and with ShimmerReverb | It works with both (it filters the reverb input); predelay and damp keep working as before. |
| **Saved.** Set a low cut, Save, power off, load | The low cut is back; a project without `seqfx.bin` has no low cut. |
| **Reverb type.** Master page > Reverb: the new last item **Type** (screen button 6): Stock / ShimmerReverb (v0.3 name; internally the Plinky reverb); the wheel changes it, the Default key (wheel press) sets Stock | Switching Stock -> ShimmerReverb while a tail rings: the tail fades out within about 3 ms, then about 30 ms of silence, then the new reverb fades in. No click, no noise burst, no sound stuck from the old reverb. ShimmerReverb -> Stock the same. Switch back and forth quickly (10 times): never a click or a hung sound. |
| ShimmerReverb: size 0 / 50 / 100 | No tail / medium (a few seconds) / long (more than 5 s, it still dies out). Level is about the same as Stock at the same send. |
| ShimmerReverb: damp 0 / 100 | Bright / dark (about 18 kHz down to 400 Hz). |
| ShimmerReverb: predelay 0 / 100 | Adds up to about 90 ms before the reverb starts (the Stock reverb: up to about 110 ms). On the Tracker Plus / Mini the ShimmerReverb has only up to about 23 ms (memory), the pad / bar still spans 0-100. |
| **Shimmer (v0.3).** Master > Reverb with the type ShimmerReverb: the 7th item (screen button 7, where the CPU readout is with Stock) is **Shimmer** 0-100 % (default 25 = the former fixed sound). Play a held pad or chord into the reverb, turn the wheel from 0 to 100 and back | 0: a plain reverb tail; the more, the more the tail gets octave-ish, glassy shimmering repeats (100 = very strong, may ring on). No click, zipper or jump while turning fast (the amount glides within about 0.2 s). The Default key (wheel press) sets 25. With the type Stock the item is not there (the CPU readout is). |
| Shimmer item and the CPU readout | With ShimmerReverb the CPU readout moved to the right side of the title bar (`N% / M%`); with Stock it is back at the 7th item. |
| Shimmer saved: set 70 %, Save, power off, load; then load an old project and a project from v0.2.x | 70 % after loading; a project without `seqfx.bin` or with a v0.2.x one has 25 % (the old sound). |
| ShimmerReverb: diffusion 0 / 68 / 85 | Few distinct echoes / the original sound (68 = default) / smoothest. |
| **CPU reading.** Master > Reverb: the "Rev / All CPU" item (screen button 7 area, next to Type) shows `N% / M%` (v0.2.7, plan 009 P): N = the time the reverb block takes in the audio interrupt as a share of the 2.9 ms block period (average), M = the whole audio interrupt (average since this screen was opened; Config > audio usage has min / max / avg / last of the same). Play the dense pattern below with Stock, note the numbers, switch to ShimmerReverb, note them again | The numbers are readable and stay finite; ShimmerReverb is expected to be a few percent higher than Stock (estimate: 12-16% vs 9-12% on the original). The peak must stay far below 100% together with the rest of the audio load. Write the numbers down; the owner decides. With nothing in the reverb send and a stopped transport the Plinky number drops to almost 0 (silence bypass). |
| ShimmerReverb with 8+ voices and the delay playing at the same time (a dense pattern, ping-pong on, bandpass on, reverb sends up) for 5 minutes | No audio drop-outs, clicks or stutter; the UI stays responsive (device test of the CPU budget). |
| Open a project saved by a newer mod version (its `seqfx.bin` has a newer version) and change the reverb type or the width | The change sounds but is NOT saved (the newer file is left alone, nothing is written). |
| **Saved.** Type ShimmerReverb, Save, power off, load. Then load an old project | ShimmerReverb after loading the project that was saved with it; the old project (no `seqfx.bin`) is Stock and sounds as it did before v0.2.4. |
| Export (stems) with the ShimmerReverb type | The reverb stem contains the ShimmerReverb. |

Pads: the ping-pong and sync pads (PING page, column 10, rows 3-4) are dark when off and dim when on, clearly dimmer than the group / PING / REV selector pads.

Known (v0.2.4): the ping-pong and sync switches are two pads on the PING page now (the jog wheel changed its job); the stock 10 s autosave still writes while playing when the delay center is changed (a stock project value); width, low cut and type are written only after Stop.

## 11f. Synth voice (plan 009 S, Original Tracker)
A polyphonic synth voice (4 voices max) that a MIDI instrument plays. It exists only in the Original Tracker build. Headphones or monitors, level low at first.

**How to make a Plinky instrument**
1. Instrument list: select a MIDI instrument (MIDI 1 .. 16), open the Instrument parameters page (the stock "MIDI Instrument Parameters" page: Velocity, Offset, CC A-F). Hold **Shift** and press screen button 1: this instrument becomes the synth voice (its channel is the voice channel) and the page shows the voice parameters, page 1 of 3, instead of the MIDI settings. The title shows `Plinky 1/3  CPU n% pk m%` (CPU of the voice engine in the audio interrupt, share of the 2.9 ms block; average and slowly decaying peak, refreshed every 50 ms).
2. Page 1: **Channel** (Off, or MIDI 1 .. 16; Off or another channel gives this instrument back to MIDI at once and the MIDI settings return), Voices (1 .. 4), Level, Reverb send, Delay send, Glide, Width (0 = mono, about half the CPU). Select a value with its screen button or Left / Right, change it with the jog wheel or Up / Down, the Default key (wheel press) sets the default. Screen button 8 = **Next**.
3. With MIDI instrument 16 claimed (Shift + button 1 on it) it is the synth voice. With it as the current instrument, play: the pads of a Chord/Arp or Mono track, the keyboard / note pads of the pattern editor and a step with instrument MIDI 16 all sound it. It no longer sends MIDI on channel 16.
4. Page 2: Shape (0 = two detuned saws, 1 .. 100 = pulse wave wide .. narrow), Detune, Interval (-12 .. +12 semitones between the left and the right oscillator pair), Noise, Cutoff, Reso, Drive. Page 3: Attack, Decay, Sustain, Release, LFO rate, LFO depth, LFO to (Pitch / Shape / Cutoff); screen button 8 = **Back** (to the limiter screen).
5. Save the project: the values are in `plinky.bin` next to it (and in the Workspace). A project that never used the voice has no such file and Channel 0.

| Check | Expected |
|---|---|
| Fresh project, MIDI instrument page | The stock MIDI page, unchanged. MIDI instruments send MIDI as before (Channel 0). Shift + screen button 1 on MIDI 16 switches the page to the voice parameters (page 1/3, Channel `MIDI 16`); on another MIDI instrument (say 3) the page stays the MIDI settings. Channel set to Off with the jog wheel: back to the MIDI settings. |
| Page 1, Channel = 16, play a note on the pads (instrument MIDI 16 selected) | A saw-like note with a short attack, held while the pad is held, a release after it; no MIDI note on channel 16 any more (a MIDI monitor on the USB / DIN output sees nothing for that channel). |
| Hold four pads, then a fifth | Four voices sound; the fifth note takes the oldest (a releasing voice first); no click, no silence of the other notes. Voices = 2: only two notes at once. |
| **CPU readout, 1 .. 4 voices.** Default sound (Width 60, Shape 0), page 1 title: hold 1, 2, 3, 4 notes, write down `CPU` and `pk` after 5 s each | Idle `0%`. Estimated from the code: about 4% per voice (4 voices about 15% + 1%), mono (Width 0) about 2.5% per voice. Report the real numbers (device test). |
| Same with Shape 40 (pulse), then Width 0 | Pulse costs about the same; Width 0 clearly less (the right oscillator pair and its filter are skipped). |
| 4 voices together with a dense pattern, delay and reverb on, 5 minutes | No drop-outs, crackle or stutter; the UI stays responsive. Look at the pk value: it should stay well under 100% together with the other load. |
| Cutoff 0 .. 100 while a note is held | Dark to bright, smooth (no zipper noise); Reso up: a resonant whistle, no runaway or silence; Drive up: louder and more saturated, never harsher than the limiter allows. |
| Shape 0, Detune 0 .. 100 | From one steady saw to a wide beating chorus. Interval +7: a fifth in the right oscillator pair (Width above 0). Width 0: mono. |
| Shape 1 .. 100 | Pulse wave from square (1) to thin (100); with LFO to = Shape and some depth the width moves. |
| Attack / Decay / Sustain / Release | The envelope also opens the filter (the louder, the brighter): a short Decay with Sustain 20 gives a pluck. Sustain 0 with a held note: the note rings out and the voice frees itself (CPU back to 0). |
| LFO rate / depth with LFO to = Pitch | A vibrato up to +-2 semitones; Cutoff: a wobble. The Mod wheel (CC 1) of an external keyboard on this channel also adds vibrato. |
| Glide up, play legato (hold a note, press another) | The pitch slides; Glide 0 = no slide. |
| Reverb / Delay send up | The voice appears in the reverb / delay returns (sends are separate from the track faders); both 0 = dry only. |
| Pattern with notes on MIDI 16 and CC automation (Send CC FX on MIDI 16 with CC 74 / 71 / 73 / 72) | CC 74 = Cutoff, 71 = Reso, 73 = Attack, 75 = Decay, 72 = Release, 70 = Shape, 76 = Detune, 77 = Noise, 5 = Glide, 7 = Level, 91 = Reverb, 93 = Delay, 1 = vibrato depth; pitch bend = +-2 semitones. (The CC values are live: they stay in the page until changed and are written at the next save.) |
| Stop during a held note / a chord | All voices release (no hanging note); Stop in Play mode with a latched arp: silent after the release. |
| Change Channel from 16 to 0 while a note sounds | The note releases, MIDI instrument 16 sends MIDI again. |
| **Saved.** Set Cutoff 20, Detune 80, Channel 16, Save, power off, load the project; then load an older project | The values are back in the first project (open the pages); the older project has Channel 0 and default values. |
| Export (stems / mix) of a pattern that plays the voice | The mix contains the voice (it is part of the dry mix); the track stems do not (it is not one of the 8 sample tracks). |
| Stock behaviour (section 12) with Channel 0 | Unchanged; there is no visible difference at all. |

Known (plan 009 S): the sends and the dry level of the voice are not on the Master mixer (use the page); the voice is not on the track faders / mute / solo / stems; crossing Shape 0 to 1 while a note sounds may tick once; one voice engine for all MIDI channels is not possible (exactly one channel).
## 11f. CPU savings (plan 009 P, v0.2.7): compare with the previous build (v0.2.6) on the same project
Use one project with 8 sample voices (some with the filter on), the delay and the stock reverb used, the limiter on, and play it. Open Master > Reverb (it resets the average when it opens) and write down "Rev / All CPU" after 30 s of the busiest part; also read Config > audio usage (min / max / avg / last) and "free heap" / "stack ... free ..." (bottom right of the Config page). Do it on v0.2.6 and on this build; the second number of the Master > Reverb item and the Config numbers are the comparison.
| Press | Expected |
|---|---|
| Same project, same playing, v0.2.6 versus this build: Master > Reverb "Rev / All CPU" and Config avg / max | This build is lower. Estimates (audit, no hardware numbers yet): the reverb number about 55-65k cycles = 10-12 points lower with the Stock type (SRAM buffers); idle voices 1 point each; delay 1.6-4.8 points; limiter about 1 point; output stage about 3 points with 8 voices. Write the numbers down. |
| Stop the transport, remove all sends (reverb, delay at 0) and wait 10 s | "All CPU" drops to a few percent (idle voices send nothing, the reverb skips blocks once its input has been silent for about 0.75 s and the tail is gone, the delay skips when its lines are silent). |
| Config page: "stack N, free M B" and "free heap" | M (the gap between the heap and the deepest stack use so far) stays above about 4000 B after a long session (load a big project, open every page, export stems once). If it is lower or the unit crashes, report it: the stock reverb now keeps 55 KB in internal RAM. |
| **Heap headroom (v0.3, plan 010; device test).** Config page, line "free heap: A, min B B" and "stack N, free M B": B is the lowest free heap since power-on (sampled in the main loop). Read it after: opening a big project, Save and Save As, a sample export (stems), the file browser on a folder with hundreds of files, loading a big sample. | B stays above about 6000 B and M above about 4000 B. The heap and the stack share only about 44 KB since the stock reverb lines live in SRAM (plan 009 P#1); if B or M is lower, report the numbers: the fallback is to move the second reverb buffer (22 KB) back to SDRAM (costs about 7 uncached accesses per sample). |
| Stock reverb: play a note into the reverb send, release it, let the tail die out, play again | The tail is the same as before; no cut-off tail, no click when the reverb starts again after a pause. Very quiet tails (below about -76 dBFS) may end a moment earlier. |
| Delay: play a hit, let the echoes die out completely, play again; then switch ping-pong off and on while the echoes run | Echoes as before. After switching ping-pong or mono the repeats of the other mode start from empty (echoes already running in the other mode are not heard after the switch; the new mode begins clean). |
| A voice with the filter at a low cutoff and high resonance: play a short note, listen to the ring-out, play it again | The ring-out is as before (the filter keeps being fed until its tail is gone); no click on the next note. |
| Volume, pan and track level changes while nothing plays, then play | The note starts at the new level at once (no fade-in over the first 3 ms). |
| Level meters (track bars, delay / reverb return, recording level) on every page that has them | Move as before (a meter shows 0 for the first moment after its page opens). |

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
| Open a project, tap the bottom-right **Perform** key (firmware Pattern; Play mode) and back to the editor without touching any pad; Save | No `seqtracks.bin` is created, the pattern files are unchanged, playback is as before. (Pattern twice no longer enters Session: the launcher is reached with Song on the Song page; Pattern only toggles Play mode.) |
| v0.2.4 (B3): from Pattern, Song, Master, Params, Sample pages, File and Config press plain **Performance** (the firmware key) | The fader page "Perform faders" opens, never the stock Performance page. Shift + Performance from those pages opens the stock page. On the fader page plain Performance does nothing, Shift + Performance goes to the stock page; on the stock page plain flips the tracks, Shift + Performance goes to the fader page. |
| Shift + Performance once, play with the stock Performance page | Exactly the stock page (the stock page is only reached with Shift + Performance; a plain Performance opens the alt fader page). |
| Project with CUT used (rows cut, recorded loop), save and re-open | CUT is not saved: all rows stock after loading; `seqtracks.bin` is only written when a sequencer type is set. |
| Song page: play the song of a known project (v0.2.3 intentional change) | **Not** stock: each slot loops until its scene is flagged play next (section 9e). Flag every pattern of the song and it plays like the stock song (last slot loops instead of wrapping to slot 1). Editing the playlist, Add / Delete slot, copy / paste, Play / Stop, tempo and the stock loop toggle behave as stock. |
| v0.3 (fonts): open **every** page (Pattern, Song, Master, Params, Sample pages, File, Project, Config, Play mode, Session CLIPS, Perform faders, lane view, popups) | All text renders in the original font, nothing blank or garbled. Config > Display font now lists only **Original** (the alternative "New" Liberation Sans set was removed, about 39.7 KB of flash). A config that stored the former "New" font (1) loads and shows the original font; a config that stored an SD .ptfont font (index 2 or higher before) still selects the same font after the first start of this version (stored index is shifted down once, config structure version 11). Custom .ptfont files on the SD card still appear in the list and load. |
