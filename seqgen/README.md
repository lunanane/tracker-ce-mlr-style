# seqgen

Pure C++ sequencer engines, ported 1:1 from the owner's grid-composer script
`monome_iii_c34.lua` (repo `iii-grid-composer`, release "c34: velocity, swing, gate lanes and the arp"; page 3 Chord/Arp without the
arranger, page 4 Mono). The first port (c31) is in git history; c34 supersedes it. No Polyend includes,
no heap, no exceptions/RTTI, no STL, no iostream, integer maths only (no `double`, no `float`).
Compiles with desktop g++ (C++11) and with `arm-none-eabi-g++` 10.3 for Cortex-M4F (`-std=gnu++17`).

License: **PolyForm Noncommercial 1.0.0** (owner 2026-11-04), see `LICENSE.md`.

## Files

| File | What | c31 lines |
|---|---|---|
| `seqgen.h` | `Event`, event sink, `LedRole`, floor div/mod (Lua `//`, `%`), `SeqBase` | (API) |
| `chordarp.h/.cpp` | `ChordArp`: roots, chord types, pool, arp, gstep, queues, live layer, groove, pads, LEDs, params, serialize; `SeqContext`; `vgvCalc` | c34 page 3: 241-274 (tables, state), 275-299 (`pgen`, `gbit`), 300-307 (`gstep`, gate lanes), 308-333 (`scale_set`, `scale_deg`, `rv`, `dlk`, `lkd`), 334-365 (`c_pool`), 366-385 (`c_arp`), 386-388 (`k_l`, `fl`), 398-419 (`tick_live`), 420-455 (`clock_pulse_tick`, quarter queue), 456-475 (`bar_tick` queue part), 476-484 (`update`), 485-576 (`draw`), 577-740 (`event`); `vgv` 87-101 |
| `mono.h/.cpp` | `Mono`: one `tk[k]` | c34 page 4: 746-898 (`SPD`, `fire`, `clock_pulse`, `kill`, menu_event, draw, event) |
| `test/test_main.cpp`, `test/test_cases2.inc`, `test/test_cases3.inc` | 74 unit tests, own tiny runner (cases3 = c34 additions) | - |
| `test/lua_driver.cpp`, `test/lua_compare.py` | differential test against the real Lua pages | - |
| `test/scripts/*.txt` | scripted gesture sequences for the differential test | - |
| `test/shrink.py`, `test/dump.py` | delta-debug a failing script / print both streams side by side | - |
| `playhead.h/.cpp` | `Playhead`: MLR-style per-track playhead for the Session view (plan 002 S1), see the Playhead section | - |
| `test/test_playhead.inc` | 24 Playhead tests (included by `test_main.cpp`) | - |
| `Makefile` | `make test`, `make lua-compare`, `make arm` | - |

## Interface (both sequencers)

```cpp
void pad(int col, int row, bool down);   // 0-based, 12 columns x 4 rows, row 0 = top
void pulse();                            // 24 PPQN clock pulse
void start(); void stop();               // transport
void tick();                             // c31 framework tick, 30 ms (blink, LED pulses, gate timer)
void setSink(EventSink fn, void* user);  // Event {kind on/off, chan, note, vel, gateMs}
void setChannel(uint8_t);                // tag copied into every event (voice/track for the host glue)
void leds(uint8_t out[48], uint8_t* roles = 0);   // level 0..15 + LedRole per pad, index = row*12+col
int  getParam(int id); void setParam(int id, int v);   // screen/encoder items (enum Param in each class)
size_t serialize(uint8_t* buf, size_t cap);   // version byte first; returns 0 if cap too small
bool   deserialize(const uint8_t* buf, size_t len);
```

`SeqContext { chord, gtpose, gb }` is the shared state: Mono reads the scale from the `ChordArp` that registered itself with
`ChordArp::setContext(&ctx)`; without one it plays C major. `ctx.gtpose` is c34 `pages[4].gtpose`; `ctx.gb` is the bar counter
(mod 9) that varies velocity pattern 8 (the ChordArp advances it, a host without one calls `ctx.barTick()`). Velocity is
no longer a Chord/Arp-wide groove: each instance has its own pattern (`P_SEL_VG` / Mono `P_VG`, see c34 below).
Several Mono instances (one per track) share one context.

Timing: c31 mixes MIDI-clock events (`pulse`, 24 PPQN) with a 30 ms framework timer (`tick`). `tick()` drives
blink/LED breathing and the live-gate timer (4 ticks normal = 120 ms, 2 ticks short = 60 ms, legato = none).
The host either calls `tick()` every 30 ms (or `advanceMs(dt)`), or calls `setInternalGate(false)` and uses
`Event::gateMs` (120/60/0 = until next note) to schedule its own note-offs (the Tracker's gate/length).

## Pad layouts

Chord/Arp: cols 0-5 = the 24 Camelot roots turned (c31 `cam[x][y]` is pad `(col = y-1, row = x-1)`), cols 6-9 = 4x4 panel
(TYPES 1..15 direct / SETTINGS T,S,D,Z rows queued to the bar / GROOVE 16 `glib` masks / PARAM weights + archetype row),
col 10-11 = TYPES, SETTINGS / GROOVE, PARAM (re-tap PARAM = PON) / INV-, INV+ / OCT-, OCT+ (both held = reset).
Grid play mode (`P_PMODE = 3`): the 24 root pads become the degree grid `(x-1)*6+(6-y)`. LCK is `lockButton(bool)`;
play mode, swing, gate, key lock root etc. are `setParam` items. Speed and direction rows accept two fingers (pair = index 5..10).
Velocity pattern, gate lane and arp octave range (c34) are screen/encoder items, see "c31 -> c34" below.

Mono: cols 0-7 = 8 steps x 4 degree rows (top = window+3), col 8 = HALF/TIE/WIN+/WIN-, col 9 = PAGE-/SPD-/LOOP (hold),
col 10 = PAGE+/SPD+/VEL-, col 11 = CLEAR/-/VEL+. LOOP held + two step pads sets the loop (start/end, either order).

## Tests

```
make test                    # 74 unit tests, 2887 checks, host g++ only
make lua-compare PYTHON=<python with lupa>   # differential test, see below
make arm                     # arm-none-eabi-g++ compile + object sizes
```
(`make` is optional; the commands are one-liners, see the Makefile.)

Differential test: `lua_compare.py` loads the real `monome_iii_c34.lua` from `E:\claude\iii-grid-composer` in Lua 5.4 (through the
Python package `lupa`, already installed for another Python on this machine; nothing was downloaded for this work), stubs the iii
API, and drives it only through `event_grid` / `event_midi` / `framework_tick` (so also the real menu pad to switch pages). The
same script (semantic commands, mapped to c34 grid coordinates on the Lua side and to the 12x4 pads / params on the C++ side) is
run through `test/lua_driver` and the two MIDI streams, stamped with the pulse and tick counters, are compared. Note-offs
released together are sorted (Lua's `pairs()` order is unspecified), Lua notes outside 0..127 are dropped (deviation 6), and
`math.random` is replaced on the Lua side by the same xorshift32 the port uses. `sync` also compares the blink / LED pulse
counters. A random generator (`--fuzz N --seed S`) produces long scripts mixing every gesture. `--lua <file>` selects another
script (c31 is kept in `iii-grid-composer`, but the port follows c34, so it will not match). Results: see the final section.
Not covered by the Lua comparison: LED levels (layout differs), serialization, `lockButton` is covered, grid-edge cases listed
below under deviations 12/13.

## c31 -> c34: what changed and where it lives in 12x4

Diff of `monome_iii_c31.lua` against `monome_iii_c34.lua` (page 3 and 4 only; pages 1-2 and the scene code are out of scope):

| c34 change | Page | In seqgen | On the 12x4 pads |
|---|---|---|---|
| `vgv(g,i,base)`: 8 velocity patterns (flat, metric, backbeat, offbeat push, ghost notes, rise, fall, alive), blend of two (`g = a + 8*b`), `base` is the loudest velocity, pattern 8 varies per bar via `gb`; replaces the parametric `pvt` groove for velocity | 3, 4 | `vgvCalc`, `SeqContext::vgv/gb`, `ChordArp::P_SEL_VG` (1..72), `Mono::P_VG` | Chord/Arp: **screen/encoder** (8 pads + blend gesture do not fit the 4x4 panel). Mono: VEL-/VEL+ pads (col 10/11, row 2) step the base pattern; `P_VG` on the screen for blends |
| Swing 0..3 pulses, capped below the step length (`min(sw, d-1)`) | 3 | `P_SWING` 0..3 | screen/encoder (was already a param) |
| Gate lanes: 4 lane masks (`LM`), `ln` = lane or two lanes (`lo + 8*hi`); a masked step gets a 7-tick gate, others the normal/short/legato gate (`gx`) | 3 | `P_LANE` 0..36, `Event::gateMs` carries the per-step gate | screen/encoder (4 pads would need a 5th panel) |
| Arp octave range 1..4 (`orng`, expands the pool upward, notes >= 128 dropped) | 3 | `P_ORANGE` | screen/encoder (c34 gesture: hold OCT centre + tap up/down; the 12x4 OCT pads have no centre) |
| 6 more speeds (1/4, 1/4T, 1/32, 1/32T, 1/2, 1/2T = `m_div`/`nsb` 5..10), 6 more arp directions (up-down no repeat, down-up, random, converge, inside out, root return) | 3 | `P_SEL_SPEED`, `P_SEL_DIR` 1..10 | **two fingers on the S or D row of the SETTINGS panel** (pair index 5..10), exactly as c34 |
| `c_pool`: transpose T is applied while the chord is built, in the chord's own key (no snap/sort/bump); out-of-range notes and notes beyond the size are dropped | 3 | `ChordArp::pool` | - |
| Parametric groove (PON) shapes only the gate mask now; tiers/archetype occupy the groove block while PON is on | 3 | `pgen` (mask only), PARAM panel | PARAM panel (unchanged); GROOVE panel keeps the 16 `glib` masks |
| `pgen` is built at init (c31 crashed when PON came first) | 3 | already in the c31 port | - |
| Mono velocity: per-sequencer pattern `t.vg` through `vgv`; pad brightness range 45..100 | 4 | `Mono::P_VG`, LED `5 + (vel-45)//6` | VEL-/VEL+ pads |
| Page 1 (five fader banks, eased faders), page 2 chrome, scene format v6 (sel_vg, a_vg, orng, ln, tail defaults) | 1, 2, SC | not ported (outside the sequencer scope; the new fields are in `serialize`) | - |

## Deviations from c34 (and why)

Chord/Arp
1. Arranger/tracker dropped as ordered by the plan: x9-12, RUN, stages `a_*`, `durs`, `c_stage`, `bar_c`, `run`, `h_row`/`fl_row`,
   `live_override`, `live_pending_off`, `f_r`/`f_st`/`k_c`, `gate`/`GT` for stages. Consequences: with no stage the scale
   without a held root is C major (c31 would fall back to the stage root), and the type/speed/dir/groove pads always edit the live
   (queued) values (c31 edited the held stage when one was held).
2. 12x4 pads instead of 16x8; panel pads replace the `x5-8/x13-16` blocks. The 15 chord types are direct pads, so c31's
   "hold pad 1 + pad N" gesture (and its side effect of first selecting type 1) does not exist; the comparison driver emulates the
   side effect so the streams stay identical. PON is a re-tap of PARAM. LCK, play mode, swing, gate are `lockButton`/`setParam`.
3. Gate timer: c31 counts 30 ms framework ticks; here `tick()` is that tick and `Event::gateMs` reports it (see Timing).
4. `g_meas` (position in the bar, 0..95) is per `ChordArp` instance and reset by `start()`; the host can resync with
   `setBarPos()`. c31 had one global counter shared with page 2.
5. `pgen()` runs at reset and on every change (c34 does the same in `init`).
6. The groove pad/PARAM LEDs show the live groove step `l_g_ptr` (c31 showed the arranger pointer `g_ptr`).
7. OCT is kept at c34's three values (-1, 0, +1); INV is -6..+6. Out-of-range MIDI notes (<0 or >127) are not emitted
   (c34 passes inversion results through). c34's `c_arp` raises `n%0` when the octave range pushes every note above 127; the port
   plays nothing instead (unit test `arp_with_every_note_out_of_range_is_silent_not_a_crash`; the comparison reports and stops at it).
7b. `math.random` (arp direction 7) is a fixed xorshift32 seeded at `reset()` so runs are reproducible; the comparison replaces
   `math.random` on the Lua side by the same generator.
7c. The bar counter `gb` that varies velocity pattern 8 is advanced when the next `pulse()` (or `start()`) begins, so Mono
   instances pulsed after the ChordArp still see the old value on the last pulse of a bar, as in c34.
8. `l_n` notes are released in ascending note order (c31 iterates `pairs()`, unspecified).
9. `amute` is `P_MUTE` (kills live notes like page 1 does), `arp_tpose` is `P_TPOSE`, `gtpose` lives in `SeqContext`.
10. Serialized: user settings only (see `serialize`); runtime state (held notes, queues, pointers) restarts clean.
11. LED levels follow c31 `draw()` where a pad has a counterpart; the arranger parts have none. Pulse/blink are integer-math
    equivalents of c31's float formulas (verified equal by `sync`).

Mono
12. **One Mono = one sequencer = one track** (owner decision 2026-11-03): no S1/S2/S3 selection (c31 menu x16 y1-3) and no
    per-sequencer MIDI channel 4-6. An instance emits events with the channel/voice tag the host set (`setChannel`, default 0);
    the firmware glue sends them on the track the instance belongs to. Several Mono tracks are several independent instances
    sharing one `SeqContext`.
13. 8 steps x 4 degrees visible (c31: 16 x 7) with HALF to flip between steps 1-8 and 9-16 of the page. Loop gesture is LOOP held +
    two step pads (c31: two pads on row 8), the end may be step 16 (c31's row 8 pad 16 is the menu button). Press-time step
    numbers are remembered so a HALF flip during the gesture cannot mis-release.
14. Pages: PAGE+ also extends the chain (`np`) when it moves the edit page past it; PAGE- never shrinks it. `P_NP` sets the chain
    length directly (c34: hold page pad 1 + pad N). The velocity pattern (menu row 7, 16 pads = patterns 1..8 plus blend with 1) is
    `P_VG` 1..72 plus VEL-/VEL+ pads. TIE, mute, transpose and speed are Mono params (c34: performance page / menu).
15. SPD+ moves to a faster entry of the `SPD` table (index + 1), SPD- slower; c31 picks the entry by pad. (Open question for the owner.)
16. Window clamped to +-40 degrees, degrees stored as `int8`; steps are a fixed array of 128 (8 pages x 16).
17. `CLEAR` resets steps, loop, `np/ep/pg/pos` like c31, but not speed, tie or window.

Test-only restrictions (the comparison generator avoids things c34 cannot do): loop end 16, `mep` beyond `np`, window outside
the 4 visible degrees, parametric groove "on" before any PARAM edit.

## ARM build (Cortex-M4F, `-Os`, `-fno-exceptions -fno-rtti -Wdouble-promotion`, no warnings)

| Object | text (flash) | bss |
|---|---|---|
| `chordarp.o` | 7305 B | 0 |
| `mono.o` | 2738 B | 24 B (shared default context) |

RAM per instance: `sizeof(ChordArp)` = 296 B, `sizeof(Mono)` = 216 B (host build; the ARM layout is the same size class).

## Playhead (plan 002, S1)

`playhead.h/.cpp`: one `Playhead` per track (16 in the firmware). Pure logic like the rest of seqgen. `sizeof(Playhead)` = 324 B
(host), 16 tracks = 5.2 KB; `playhead.o` on ARM: 1458 B flash, 0 bss.

```cpp
Playhead p;                              // defaults: length 16, 8 columns, quantise 1, normal, forward
p.setLength(1..256); p.setColumns(8|12); p.setQuantise(1..64 steps); p.setRate(RATE_HALF|NORMAL|DOUBLE); p.setReverse(b);
p.press(col); p.release(col);            // pads of this track's row, 0-based
p.recDown(); p.recUp();                  // playhead recording
p.resync(globalStep);                    // align to the global step counter
p.restart(nextStep);                     // continue as if nextStep comes next (no jump flag, keeps gestures + recorded loop)
Playhead::StepOut o = p.step();          // once per sequencer step boundary -> o.pos, o.jumped, o.hold, o.pos2/has2/jumped2
Playhead::noteOffsetFor(pos, hasNote, length, noteStep, stepsSince)   // "replay as if at pos"
p.serialize(buf, cap) / p.deserialize(buf, len)                       // kSerialSize = 296 B, version byte 1
```

Rules (all covered by tests):
- **The Playhead owns the track position.** The glue calls `step()` once per step boundary (instead of advancing the track's
  own position) and plays `o.pos`. Before the first step the next position is 0. Quantise counts these `step()` calls
  (index since `resync(globalStep)`; boundary = index % quantise == 0), so the glue should pass the global step to `resync()`
  when it starts the transport.
- **Columns:** column c starts at position `c * length / columns` (integer). A column's range runs to the next column's start - 1;
  with fewer steps than columns, neighbouring columns share a position.
- **`jumped`** = `pos` is not the engine's natural successor of the previously played position (+1, wrapping at the track end). So
  it is set for pad jumps, loop wraps, every step when reversed, replayed jumps and the first step after `resync()`; it is
  clear for a plain forward run and for a press that happens to hit the natural position. The glue re-seeks the track
  (`actual_pos`) and, on `jumped`, retriggers the last note at or before `pos` with a sample offset from `noteOffsetFor`.
- **One pad:** the jump is queued and applied at the next quantise boundary (the last press before the boundary wins);
  playback then continues forward from there. Releasing it changes nothing.
- **Two or more pads:** the loop is the outer columns (min..max of the held ones, inclusive, so the order of pressing is
  irrelevant). It starts at the range start (the end when reversed) at the next boundary, wraps inside the range
  (flagged `jumped`), and follows a third pad that widens it (it only jumps if the playhead is outside the new range).
  When fewer than two pads are held the loop ends at once and the track runs on freely from the current position.
- **Rate:** half = each position lasts 2 steps (second step: `hold` = true, same `pos`, trigger nothing). Double = `pos` for the
  first half of the step and `pos2` (= next position, loop-aware) for the second half (`has2`, `jumped2`); the glue plays
  `pos2` at the middle of the step (µstep 24 of 48). A jump restarts the half-rate phase. Reverse = positions run backwards;
  wraps at 0 (or the loop start). Rate and reverse apply to free running only; a recorded loop is replayed as recorded.
- **Playhead recording:** `recDown()` starts a take, each `step()` records the position that was played (after jumps, loops,
  rate - one entry per step, incl. holds), max 256 steps; a full take closes itself (the loop then restarts at that moment).
  `recUp()` closes it: the take becomes the replay loop and from the next step on the track plays it, step by step, forever
  (also while the user is on other pages: it only needs `step()`). The previous loop stops while a new take is recorded and is
  replaced by it; a take of 0 steps (tap Rec) clears the loop.
- **Press while a recording replays (decision):** a pad **overrides** the recording while it is down: the jump/loop is queued
  and applied at the boundary as usual, and the track free-runs from there while any pad is held. When the last pad is
  released the track returns to the recorded loop **in phase** - the recorded loop keeps its own clock while overridden, so it
  continues where it would have been. A tap (press and release before the boundary) overrides for exactly one step. The
  recording is **not** cleared by a press or a two-pad loop (this differs from the provisional wording in plan 002); it is
  cleared by `resync()`, `clearRecording()`, an empty take or `reset()`. Holding Rec while jamming records the overriding
  positions (that is how a take is made).
- **`restart(nextStep)`:** the next `step()` lands on `nextStep % length`, **not** flagged `jumped`; held pads, queued gestures, a take in progress and the recorded loop are kept (the loop restarts at index `nextStep % recordedLength`). The glue uses it to take over a running track and for transport (re)start.
- **`resync(globalStep)`:** next position = `globalStep % length`, `jumped` set, loop/queue/held pads/recording cleared (stray
  releases afterwards are ignored). Rate and reverse are kept.
- **`noteOffsetFor`:** scans back from `position` (inclusive), wrapping to the end of the track; returns the step and the
  number of steps since it; false if the track has no note at all.
- **Serialized:** length, columns, quantise, rate, reverse and the recorded loop (positions + hold bits). Position, held pads
  and queue restart clean. Changing the column count clears held pads, loop and queue.
- Not in S1: stop/start of a track, the cut cache (S3), the firmware glue (S2).

## Open questions

- Where velocity pattern / gate lane / octave range should live on the Tracker (screen + encoders proposed; a 5th "VEL" panel needs a spare control pad); SPD+ direction (item 15); OCT range; whether the Tracker wants `gateMs` or the internal 30 ms timer (item 3).
- LCK gesture on the hardware (button vs. hold-root).
- License (see above).

## Results at hand-over (c34)

- `make test`: 75 tests, 0 failures.
- `make lua-compare PYTHON=<python with lupa>` (default: 40 random scripts + 3 hand-written): 43 scripts identical, 9633 events.
- `python -I test/lua_compare.py --fuzz 100 --seed S --script test/scripts/chord_basic.txt test/scripts/mono_basic.txt test/scripts/chord_c34.txt`
  for S = 1..5: 515 scripts, 0 mismatches, 123863 events. One of them (seed 5 #28) hits `n%0` inside the original c34 `c_arp`
  (deviation 7); it is compared up to that moment.
