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
| Muted track | A muted track makes no sound from the sequencer either. |
| Two Chord/Arp tracks | The lower-numbered one supplies the scale to Mono tracks (C major if none). |
| Reboot | Track types are not saved yet (step B): all tracks are Off again. |
| Pattern playback timing with some tracks set to Mono/Chord | Pattern timing of the other tracks is steady (no hiccups). |

Known limits of this step: no Rec / punch-in, no saving of sequencer state, no parameter editing on the jog wheel
(only the pads), chord notes are not pattern data.
