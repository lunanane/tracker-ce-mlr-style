# seqgen

Pure C++ sequencer engines, ported 1:1 from the owner's grid-composer script
`monome_iii_c31.lua` (page 3 Chord/Arp without the arranger, page 4 Mono). No Polyend includes,
no heap, no exceptions/RTTI, no STL, no iostream, integer maths only (no `double`, no `float`).
Compiles with desktop g++ (C++11) and with `arm-none-eabi-g++` 10.3 for Cortex-M4F (`-std=gnu++17`).

License: **to be chosen by the owner** (placeholder, no license is granted by this directory yet).

## Files

| File | What | c31 lines |
|---|---|---|
| `seqgen.h` | `Event`, event sink, `LedRole`, floor div/mod (Lua `//`, `%`), `SeqBase` | (API) |
| `chordarp.h/.cpp` | `ChordArp`: roots, chord types, pool, arp, gstep, queues, live layer, groove, pads, LEDs, params, serialize; `SeqContext` | page 3: 237-264 (tables, state), 271-296 (`pgen`, `gbit`), 297-302 (`gstep`), 303-327 (`scale_set`, `scale_deg`, `rv`, `dlk`, `lkd`), 329-372 (`c_pool`), 373-380 (`c_arp`), 381-383 (`k_l`, `fl`), 393-414 (`tick_live`), 415-450 (`clock_pulse_tick`, quarter queue), 451-470 (`bar_tick` queue part), 471-479 (`update`), 480-564 (`draw`), 571-708 (`event`) |
| `mono.h/.cpp` | `Mono`: one c31 `tk[k]` | page 4: 711-841 (`SPD`, `fire`, `clock_pulse`, `kill`, menu_event, draw, event) |
| `test/test_main.cpp`, `test/test_cases2.inc` | 57 unit tests, own tiny runner | - |
| `test/lua_driver.cpp`, `test/lua_compare.py` | differential test against the real Lua pages | - |
| `test/scripts/*.txt` | scripted gesture sequences for the differential test | - |
| `test/shrink.py`, `test/dump.py` | delta-debug a failing script / print both streams side by side | - |
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

`SeqContext { chord, gtpose }` is the shared state: Mono reads the scale and the velocity groove from the
`ChordArp` that registered itself with `ChordArp::setContext(&ctx)`; without one it plays C major with flat velocity 100.
`ctx.gtpose` is c31 `pages[4].gtpose`. Several Mono instances (one per track) share one context.

Timing: c31 mixes MIDI-clock events (`pulse`, 24 PPQN) with a 30 ms framework timer (`tick`). `tick()` drives
blink/LED breathing and the live-gate timer (4 ticks normal = 120 ms, 2 ticks short = 60 ms, legato = none).
The host either calls `tick()` every 30 ms (or `advanceMs(dt)`), or calls `setInternalGate(false)` and uses
`Event::gateMs` (120/60/0 = until next note) to schedule its own note-offs (the Tracker's gate/length).

## Pad layouts

Chord/Arp: cols 0-5 = the 24 Camelot roots turned (c31 `cam[x][y]` is pad `(col = y-1, row = x-1)`), cols 6-9 = 4x4 panel
(TYPES 1..15 direct / SETTINGS T,S,D,Z rows queued to the bar / GROOVE 16 `glib` masks / PARAM weights + archetype row),
col 10-11 = TYPES, SETTINGS / GROOVE, PARAM (re-tap PARAM = PON) / INV-, INV+ / OCT-, OCT+ (both held = reset).
Grid play mode (`P_PMODE = 3`): the 24 root pads become the degree grid `(x-1)*6+(6-y)`. LCK is `lockButton(bool)`;
play mode, swing, gate, key lock root etc. are `setParam` items.

Mono: cols 0-7 = 8 steps x 4 degree rows (top = window+3), col 8 = HALF/TIE/WIN+/WIN-, col 9 = PAGE-/SPD-/LOOP (hold),
col 10 = PAGE+/SPD+, col 11 row 0 = CLEAR. LOOP held + two step pads sets the loop (start/end, either order).

## Tests

```
make test                    # 58 unit tests, 2584 checks, host g++ only
make lua-compare PYTHON=<python with lupa>   # differential test, see below
make arm                     # arm-none-eabi-g++ compile + object sizes
```
(`make` is optional; the commands are one-liners, see the Makefile.)

Differential test: `lua_compare.py` loads the real `monome_iii_c31.lua` in Lua 5.4 (through the Python package `lupa`,
already installed for another Python on this machine; nothing was downloaded for this work), stubs the iii API, and drives
it only through `event_grid` / `event_midi` / `framework_tick` (so also the real menu pad to switch pages). The same script
(semantic commands, mapped to c31 grid coordinates on the Lua side and to the 12x4 pads on the C++ side) is run through
`test/lua_driver` and the two MIDI streams, stamped with the pulse and tick counters, are compared. Note-offs released
together are sorted (Lua's `pairs()` order is unspecified). `sync` also compares the blink / LED pulse counters. A random
generator (`--fuzz N --seed S`) produces long scripts mixing every gesture. Result at hand-over: seeds 1-5 x 100 random scripts + 2 hand-written scripts each (510 scripts, about 117k events) and the default `make lua-compare` (42 scripts, 8196 events) all identical, 0 mismatches. The harness drops Lua events with notes outside 0..127 (deviation 6). Like c31, `stop()`/mute leave a pending gate timer armed.
Not covered by the Lua comparison: LED levels (layout differs), serialization, `lockButton` is covered, grid-edge cases listed
below under deviations 12/13.

## Deviations from c31 (and why)

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
5. `pgen()` runs at reset and on every change, so velocity groove "on" before any parameter edit works (c31 would hand `nil` to
   `midi_note_on` in that case; its draw even crashes).
6. The groove pad/PARAM LEDs show the live groove step `l_g_ptr` (c31 showed the arranger pointer `g_ptr`).
7. OCT is kept at c31's three values (-1, 0, +1); INV is -6..+6. Out-of-range MIDI notes (<0 or >127) are not emitted
   (c31 passes them through; only reachable with extreme inversion/transpose).
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
    length directly (c31: hold page pad 1 + pad N). The velocity radio `vg` (menu row 7) was unused by the engine and is dropped.
    TIE, mute, transpose and speed are Mono params (c31: performance page / menu).
15. SPD+ moves to a faster entry of the `SPD` table (index + 1), SPD- slower; c31 picks the entry by pad. (Open question for the owner.)
16. Window clamped to +-40 degrees, degrees stored as `int8`; steps are a fixed array of 128 (8 pages x 16).
17. `CLEAR` resets steps, loop, `np/ep/pg/pos` like c31, but not speed, tie or window.

Test-only restrictions (the comparison generator avoids things c31 cannot do): loop end 16, `mep` beyond `np`, window outside
the 4 visible degrees, parametric groove "on" before any PARAM edit.

## ARM build (Cortex-M4F, `-Os`, `-fno-exceptions -fno-rtti -Wdouble-promotion`, no warnings)

| Object | text (flash) | bss |
|---|---|---|
| `chordarp.o` | 6476 B | 0 |
| `mono.o` | 2540 B | 24 B (shared default context) |

RAM per instance: `sizeof(ChordArp)` = 304 B, `sizeof(Mono)` = 216 B (host build; the ARM layout is the same size class).

## Open questions

- SPD+ direction (item 15); OCT range; whether the Tracker wants `gateMs` or the internal 30 ms timer (item 3).
- LCK gesture on the hardware (button vs. hold-root).
- License (see above).
