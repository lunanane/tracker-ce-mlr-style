# Plan 011 — harmony across sequencer tracks (DEFERRED by the owner, 2026-11-05)

Status: parked — "keep that topic for later and just finish the current fixes". Design agreed so far:

Problem: many sequencer types now, some are followers (scale/chord), no fixed lead as in grid composer / HYPER_RASP.
Today the shared HrBus is last-publisher-wins (timing dependent with two chord tracks).

Proposal (owner reviewed the first two decisions):
- **Project key**: one root + scale per project, shown in the Play-mode title, Shift + jog in Play mode, saved with
  the project; default for every sequencer that follows.
- **Lead**: at most one track, a chord-type sequencer (Chord/Arp, HR Chord/Arp, Chord Stabs) publishing its live chord
  and key; first chord-type track becomes lead automatically; making another track lead demotes the previous to Follow.
  Strip mark **L**; set with the track's screen button held + Shift (gesture TBD).
- **Role per track**: **Follow** (lead's live chord, else project key) or **Free** (own key). Arps, bass, step,
  Strings follow by default; drum types and plain pattern notes are unaffected. Changing the lead's key updates the
  project key.
- Owner decision 1: followers follow the **live chord + key** (like grid composer / HYPER_RASP).
- Owner decision 2: the project key does **NOT** drive the stock Tracker pad scale — the Tracker stays chromatic
  (outboard gear has its own scale quantisation and the live set relies on stock chromatic mode). Only the new
  sequencer parts receive quantisation.

## Owner idea 2026-11-06 (not decided — owner is thinking it over)
"If we could set a lead sequencer in a scene, and even if we record into the tracker with it, could that clip overwrite
the global scale whenever it's playing?" — Feasible: record the lead's harmony WITH the clip (per pattern×track
metadata, like the Play 1x clip modes in seqclips.bin): level 1 = root + scale per clip; level 2 = per-step chord
(a recorded harmony lane, a few bytes per step). When the clip plays (launch, scene, song) it publishes that key /
chord to the global harmony bus even without a running sequencer; followers quantise to it; stock tracks stay
chromatic. Open rules: conflict between two playing lead clips (last launched vs scene), clips without harmony keep or
reset the key, manual edit/clear of a clip's key.
