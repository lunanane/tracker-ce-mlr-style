#!/usr/bin/env python
# Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
"""Differential test of the ported HYPER_RASP scripts: run the real Lua script (headless, lupa = Lua 5.4, with a stub of the
HYPER_RASP PadScript API: grid_led, midi_note_on/off, set_scale/current_scale, set_root_note/current_root_note, euclidean, ...)
and the C++ port (test/hr_driver) on the same command script, and compare the note event streams (pulse-stamped) and the 8 x 4 LED
levels after every command.

  python hr_compare.py --scripts-dir DIR --driver build/hr_driver [--only NAME] [--fuzz N] [--seed S] [--file FILE]

DIR is HYPER_RASP's scripts directory (also SEQGEN_HR_SCRIPTS). Command language (one per line, shared by both sides):
  grid X Y Z | pulse N | start | stop | tick N | param NAME VALUE | sync
  busscale PC... | busnone | busroot N | busclear        (the scale / root another track published; the Lua side calls scale_changed / root_note_changed)
"""
import argparse, os, random, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))

PRELUDE = r"""
EV={}; PC=0; LED={}; BG={}; for i=1,32 do LED[i]=0; BG[i]=0 end
GRID_WIDTH=8; GRID_HEIGHT=4
function grid_led(x,y,l,role) if x>=1 and x<=8 and y>=1 and y<=4 then LED[(y-1)*8+x]=l end end
function grid_led_bg(x,y,l,role) if x>=1 and x<=8 and y>=1 and y<=4 then BG[(y-1)*8+x]=l end end
function grid_led_all(l) for i=1,32 do LED[i]=l end end
function grid_refresh() end
function midi_note_on(n,v,c) if n>=0 and n<=127 then EV[#EV+1]=string.format("p%d on %d %d",PC,n,v) end end
function midi_note_off(n,v,c) if n>=0 and n<=127 then EV[#EV+1]=string.format("p%d off %d 0",PC,n) end end
function midi_cc() end
function param_changed() end
function pattern_changed() end
BS={3,5,6,8,10,11,1}; BR=nil
function set_scale(t) if t==nil then BS=nil else BS={} for i,v in ipairs(t) do BS[i]=v end end end
function current_scale() if BS==nil then return nil end local c={} for i,v in ipairs(BS) do c[i]=v end return c end
function set_root_note(n) BR=n end
function current_root_note() return BR end
function metro(...) return {start=function() end, stop=function() end} end
function pset_init() end
function pset_read() return nil end
function call_opt(name, ...) local f=_G[name]; if f then return f(...) end end
"""


def bjorklund(pulses, steps):
    """pad_script._bjorklund"""
    if steps <= 0: return []
    if pulses <= 0: return [False] * steps
    if pulses >= steps: return [True] * steps
    counts, rem = {}, {1: pulses}
    divisor, level = steps - pulses, 1
    while rem[level] > 1:
        counts[level] = divisor // rem[level]
        rem[level + 1] = divisor % rem[level]
        divisor = rem[level]
        level += 1
    counts[level] = divisor
    raw = []
    def build(l):
        if l == 0: raw.append(False)
        elif l == -1: raw.append(True)
        else:
            for _ in range(counts[l]): build(l - 1)
            if rem[l] != 0: build(l - 2)
    build(level)
    first = next((i for i, o in enumerate(raw) if o), 0)
    n = len(raw)
    return [raw[(i + first) % n] for i in range(n)]


RENDER_AFTER_PARAM = ("bassline_seq", "euclid_trigger", "grid_seq", "step_follower")


class LuaSide:
    def __init__(self, path, name=""):
        # the HYPER_RASP host side of chord_arp.lua (ScriptedPad + ViewSwitcher): the function column x = 8
        self.name = name
        self.switcher = name == "chord_arp"
        self.view, self.page, self.running, self.held, self.fired = 1, 1, False, set(), False
        from lupa import lua54
        self.rt = lua54.LuaRuntime(unpack_returned_tuples=True)
        self.g = self.rt.globals()
        self.g.euclidean = lambda k, n: self.rt.table_from(bjorklund(int(k), int(n)))
        self.rt.execute(PRELUDE)
        src = open(path, encoding="utf-8").read()
        self.rt.execute(src)
        self.out = []
        self.pulses = 0
        if self.switcher: self.stage_switcher()

    def stage_switcher(self):
        for y in range(1, 5): self.g.LED[(y - 1) * 8 + 8] = 15 if y == self.view else 3

    def switch(self, y, z):
        row = y - 1
        if z:
            self.held.add(row)
            if len(self.held) == 2 and not self.fired:
                self.fired = True
                lo, hi = sorted(self.held)
                if (lo, hi) == (2, 3):
                    self.page = 2 if self.page == 1 else 1
                    self.g.set_page(self.page)
                elif (lo, hi) == (0, 1):
                    self.set_playback(not self.running)
        else:
            lone = len(self.held) == 1 and not self.fired
            self.held.discard(row)
            if not self.held:
                if lone:
                    self.view = row + 1
                    self.g.set_view(self.view)
                    self.stage_switcher()
                self.fired = False

    def set_playback(self, running):
        if running == self.running: return
        self.running = running
        self.rt.execute("call_opt('start_playback')" if running else "call_opt('stop_playback')")

    def flush_events(self):
        ev = self.g.EV
        n = len(ev)
        for i in range(1, n + 1): self.out.append(ev[i])
        self.rt.execute("EV={}")

    def leds(self):
        led = self.g.LED
        bg = self.g.BG
        self.out.append("L " + " ".join(str(int(led[i]) if int(led[i]) > 0 else int(bg[i])) for i in range(1, 33)))

    def run(self, cmd):
        w = cmd.split()
        g = self.g
        c = w[0]
        if c == "grid":
            if self.switcher and int(w[1]) == 8: self.switch(int(w[2]), int(w[3]))
            else: g.event_grid(int(w[1]), int(w[2]), int(w[3]))
        elif c == "pulse":
            for _ in range(int(w[1])):
                self.pulses += 1; g.PC = self.pulses; g.event_midi(248, 0, 0)
        elif c == "start": self.rt.execute("call_opt('start_playback')")
        elif c == "stop": self.rt.execute("call_opt('stop_playback')")
        elif c == "tick":
            for _ in range(int(w[1])): self.rt.execute("call_opt('framework_tick')")
        elif c == "param":
            g.set_param(w[1], int(w[2]))
            if self.name in RENDER_AFTER_PARAM: self.rt.execute("call_opt('render')")  # these scripts repaint on the next event only; the port paints live
        elif c == "busscale":
            self.rt.execute("BS={%s}" % ",".join(w[1:]))
            self.rt.execute("call_opt('scale_changed', current_scale())")
        elif c == "busnone":
            self.rt.execute("BS=nil"); self.rt.execute("call_opt('scale_changed', nil)")
        elif c == "busroot":
            self.rt.execute("BR=%s" % w[1]); self.rt.execute("call_opt('root_note_changed', BR)")
        elif c == "busclear":
            self.rt.execute("BS={3,5,6,8,10,11,1}; BR=nil")
            self.rt.execute("call_opt('scale_changed', current_scale())"); self.rt.execute("call_opt('root_note_changed', nil)")
        elif c == "sync": pass
        else: raise SystemExit("unknown command " + cmd)
        self.flush_events()
        self.leds()


def run_lua(path, cmds, name=""):
    s = LuaSide(path, name)
    for c in cmds: s.run(c)
    return s.out


def run_cpp(driver, name, cmds):
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
        f.write("\n".join(cmds) + "\n")
        fn = f.name
    try:
        r = subprocess.run([os.path.abspath(driver), name, fn], capture_output=True, text=True)
    finally:
        os.unlink(fn)
    if r.returncode != 0: raise SystemExit("driver failed: %s %s" % (r.returncode, r.stderr))
    return r.stdout.splitlines()


def norm(lines):
    """note-offs that belong together are sorted (Lua's table order for a chord that ends is the order it was built in, but
    be tolerant): consecutive 'off' lines of one pulse are sorted"""
    out, run = [], []
    for ln in lines:
        if " off " in ln: run.append(ln); continue
        out += sorted(run); run = []; out.append(ln)
    return out + sorted(run)


def compare(lua_path, driver, name, cmds, verbose=True):
    a = norm(run_lua(lua_path, cmds, name)); b = norm(run_cpp(driver, name, cmds))
    if a == b: return True, len(a)
    if verbose:
        for i in range(max(len(a), len(b))):
            x = a[i] if i < len(a) else "<end>"; y = b[i] if i < len(b) else "<end>"
            if x != y:
                print("  MISMATCH at output line %d\n    lua: %s\n    c++: %s" % (i, x, y))
                # which command produced it
                break
    return False, 0


# ------------------------------------------------------------------ generators
def tap(x, y): return ["grid %d %d 1" % (x, y), "grid %d %d 0" % (x, y)]


def gen_stab(rng, n, follower):
    cmds = []
    rates = [24, 12, 8, 6, 4, 3, 2, 1]
    for _ in range(n):
        r = rng.random()
        if r < 0.28:
            x, y = rng.randint(1, 8), rng.randint(1, 4)
            cmds += tap(x, y) if rng.random() < 0.9 else ["grid %d %d 1" % (x, y)]
        elif r < 0.60: cmds.append("pulse %d" % rng.choice([1, 1, 2, 3, 6, 6, 12, 24, 40, 100]))
        elif r < 0.66: cmds.append("param octave %d" % rng.randint(-2, 2))
        elif r < 0.72: cmds.append("param rate %d" % (rng.choice(rates) if rng.random() < 0.8 else rng.randint(1, 24)))
        elif r < 0.78: cmds.append("param vel %d" % rng.randint(1, 127))
        elif r < 0.84: cmds.append("param transp %d" % rng.randint(-12, 12))
        elif r < 0.88 and follower: cmds.append("param role %d" % rng.choice([1, 2]))
        elif r < 0.91: cmds.append(rng.choice(["start", "stop"]))
        elif r < 0.94 and follower:
            k = rng.random()
            if k < 0.4: cmds.append("busscale " + " ".join(str(v) for v in rng.choice([[0, 2, 4, 5, 7, 9, 11], [9, 11, 0, 2, 4, 5, 7], [2, 4, 5, 7, 9, 10, 0], [0, 2, 3, 5, 7, 8, 10], [0, 2, 4, 7, 9], [0, 3, 5, 6, 7, 10]])))
            elif k < 0.55: cmds.append("busnone")
            elif k < 0.85: cmds.append("busroot %d" % rng.randint(30, 100))
            else: cmds.append("busclear")
        else: cmds.append("sync")
    cmds.append("sync")
    return cmds


def gen_arpseq(rng, n):
    cmds = []
    for _ in range(n):
        r = rng.random()
        if r < 0.22:
            x, y = rng.randint(1, 6), rng.randint(1, 2)  # roots
            cmds += tap(x, y)
        elif r < 0.30:  # a chord gesture: overlapping holds of the chord keys, released in any order
            keys = rng.sample([1, 2, 3], rng.randint(1, 3))
            cmds += ["grid 7 %d 1" % k for k in keys]
            rel = keys[:]; rng.shuffle(rel)
            cmds += ["grid 7 %d 0" % k for k in rel]
        elif r < 0.37: cmds += tap(rng.choice([1, 2, 3, 4, 5, 6]), 3)  # quality + the five cycle buttons
        elif r < 0.45:  # a step: tap = store, hold two = loop range
            a, b = rng.randint(1, 8), rng.randint(1, 8)
            if rng.random() < 0.55: cmds += tap(a, 4)
            else: cmds += ["grid %d 4 1" % a, "grid %d 4 1" % b, "grid %d 4 0" % a, "grid %d 4 0" % b]
        elif r < 0.50: cmds += tap(8, 3)  # PLAY
        elif r < 0.53: cmds += tap(rng.choice([7, 8]), rng.randint(1, 4))  # strays: dark keys, the 4th row of the chord column
        elif r < 0.75: cmds.append("pulse %d" % rng.choice([1, 1, 2, 3, 4, 6, 8, 12, 24, 50, 100]))
        elif r < 0.80: cmds.append("pulse %d" % rng.choice([384, 385, 400, 800]))
        elif r < 0.87: cmds.append("tick %d" % rng.choice([1, 3, 8, 16, 20]))
        elif r < 0.90: cmds.append("stop")
        else: cmds.append("sync")
    cmds.append("sync")
    return cmds


def gen_chordarp(rng, n):
    cmds = []
    pr = {"type": (1, 10), "size": (2, 5), "octave": (-1, 1), "inv": (-8, 8), "mode": (1, 2), "speed": (1, 4), "dir": (1, 4), "groove": (1, 12),
          "vel": (1, 127), "gate": (1, 16), "transp": (-12, 12), "qstart": (1, 2), "keylock": (1, 2), "key": (0, 11), "scale": (1, 2), "latch": (1, 2)}
    for _ in range(n):
        r = rng.random()
        if r < 0.30:
            x, y = rng.randint(1, 7), rng.randint(1, 4)
            cmds += tap(x, y) if rng.random() < 0.85 else ["grid %d %d 1" % (x, y)]
        elif r < 0.37:  # mod keys: two held (a combo type) or three
            ys = rng.sample([1, 2, 3, 4], rng.randint(1, 3))
            cmds += ["grid 7 %d 1" % y for y in ys]
            rel = ys[:]; rng.shuffle(rel)
            cmds += ["grid 7 %d 0" % y for y in rel]
        elif r < 0.45: cmds += tap(8, rng.randint(1, 4))  # a view
        elif r < 0.49:  # a function-column combo
            ys = rng.sample([1, 2, 3, 4], 2)
            cmds += ["grid 8 %d 1" % ys[0], "grid 8 %d 1" % ys[1], "grid 8 %d 0" % ys[0], "grid 8 %d 0" % ys[1]]
        elif r < 0.52: cmds += rng.choice([["grid 6 1 1"], ["grid 6 1 0"], ["grid 6 1 1", "grid 6 1 0"]])
        elif r < 0.78: cmds.append("pulse %d" % rng.choice([1, 1, 2, 3, 6, 6, 12, 24, 24, 48, 100]))
        elif r < 0.92:
            k = rng.choice(list(pr))
            lo, hi = pr[k]
            cmds.append("param %s %d" % (k, rng.randint(lo, hi)))
        elif r < 0.95: cmds.append(rng.choice(["start", "stop"]))
        else: cmds.append("sync")
    cmds.append("sync")
    return cmds


def bus_cmd(rng):
    k = rng.random()
    if k < 0.4: return "busscale " + " ".join(str(v) for v in rng.choice([[0, 2, 4, 5, 7, 9, 11], [9, 11, 0, 2, 4, 5, 7], [2, 4, 5, 7, 9, 10, 0], [0, 2, 3, 5, 7, 8, 10], [0, 2, 4, 7, 9], [0, 3, 5, 6, 7, 10], [5, 7, 9, 10, 0, 2, 4]]))
    if k < 0.55: return "busnone"
    if k < 0.85: return "busroot %d" % rng.randint(0, 127)
    return "busclear"


def gen_bass(rng, n):
    cmds, held = [], []
    for _ in range(n):
        r = rng.random()
        if r < 0.22:  # press keys (several: the press order is the sequence), release some
            x, y = rng.randint(1, 8), rng.randint(1, 3)
            cmds.append("grid %d %d 1" % (x, y)); held.append((x, y))
        elif r < 0.34 and held:
            k = held.pop(rng.randrange(len(held))); cmds.append("grid %d %d 0" % k)
        elif r < 0.42: cmds += tap(rng.randint(1, 7), 4)  # E(k,8) preset
        elif r < 0.47: cmds += ["grid 8 4 1"] + ["pulse %d" % rng.choice([6, 12, 48, 100, 400])] + ["grid 8 4 0"]  # record
        elif r < 0.50: cmds += tap(rng.randint(1, 8), 4)
        elif r < 0.70: cmds.append("pulse %d" % rng.choice([1, 1, 2, 3, 5, 6, 6, 12, 24, 40, 100]))
        elif r < 0.75: cmds.append("param octave %d" % rng.randint(-2, 2))
        elif r < 0.80: cmds.append("param direction %d" % rng.randint(1, 4))
        elif r < 0.85: cmds.append("param groove %d" % rng.randint(1, 12))
        elif r < 0.90: cmds.append(rng.choice(["start", "start", "stop"]))
        elif r < 0.95: cmds.append(bus_cmd(rng))
        else: cmds.append("sync")
    cmds.append("sync")
    return cmds


SCRIPTS = {
    "arp_lines": ("arp_lines.lua", lambda rng, n: gen_stab(rng, n, True)),
    "chord_stabs": ("chord_stabs.lua", lambda rng, n: gen_stab(rng, n, False)),
    "arp_seq": ("arp_seq.lua", gen_arpseq),
    "chord_arp": ("chord_arp.lua", gen_chordarp),
    "bassline_seq": ("bassline_seq.lua", gen_bass),
}


def load_gens():
    # more generators live in hr_compare_gens.py (one per script), added as the scripts are ported
    try:
        sys.path.insert(0, HERE)
        import hr_compare_gens
        SCRIPTS.update(hr_compare_gens.GENS)
        FIXED.update(hr_compare_gens.FIXED)
    except ImportError:
        pass


def sweep_stab(follow):
    """every velocity 1..127 against every accent level, every chord x quality x pattern once (exhaustive tables)"""
    c = ["grid 1 1 1", "grid 1 1 0"]
    for v in range(1, 128):
        c += ["param vel %d" % v, "pulse 6"]
    c += ["param vel 100"]
    for pat in range(10):
        x, y = 2 + pat % 5, 3 + pat // 5
        for q in (3, 4):
            for ch in range(1, 9):
                cx, cy = 7 + (ch - 1) // 4, (ch - 1) % 4 + 1
                c += tap(1, q) + tap(x, y) + tap(cx, cy) + ["pulse 100"]
        if follow: c += ["param role 2"] + ["grid 3 1 1", "grid 3 1 0", "pulse 100"] * 2 + ["param role 1"]
    for r in range(1, 13):
        x, y = (r - 1) % 6 + 1, (r - 1) // 6 + 1
        c += tap(x, y) + tap(x, y) + ["pulse 3"]
    return c


def sweep_chordarp():
    """the key lock snap (note -> degree -> note, collisions) for every key x scale x chord type x root pad x a few transposes"""
    c = ["param keylock 2"]
    for key in range(12):
        for scale in (1, 2):
            c += ["param key %d" % key, "param scale %d" % scale]
            for tr in (-5, 0, 3):
                c.append("param transp %d" % tr)
                for t in range(1, 11):
                    c.append("param type %d" % t)
                    for y in range(1, 5):
                        for x in range(1, 7):
                            c += tap(x, y)
    c += ["param keylock 1", "sync"]
    return c


FIXED = {"arp_lines": [sweep_stab(True)], "chord_stabs": [sweep_stab(False)], "chord_arp": [sweep_chordarp()]}  # name -> list of hand-written command scripts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scripts-dir", default=os.environ.get("SEQGEN_HR_SCRIPTS"))
    ap.add_argument("--driver", default=os.path.join(HERE, "..", "build", "hr_driver"))
    ap.add_argument("--only", action="append")
    ap.add_argument("--fuzz", type=int, default=40)
    ap.add_argument("--len", type=int, default=120)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--file")
    a = ap.parse_args()
    if not a.scripts_dir: raise SystemExit("set --scripts-dir or SEQGEN_HR_SCRIPTS")
    load_gens()
    bad = total = events = 0
    for name, (lua, gen) in SCRIPTS.items():
        if a.only and name not in a.only: continue
        path = os.path.join(a.scripts_dir, lua)
        runs = list(FIXED.get(name, []))
        if a.file: runs = [open(a.file).read().split("\n")]
        else:
            for i in range(a.fuzz):
                rng = random.Random(a.seed * 100003 + i)
                runs.append(gen(rng, a.len))
        okc = 0
        for i, cmds in enumerate(runs):
            ok, n = compare(path, a.driver, name, [c for c in cmds if c.strip()])
            total += 1
            if ok: okc += 1; events += n
            else:
                bad += 1
                fn = os.path.join(tempfile.gettempdir(), "hr_fail_%s_%d.txt" % (name, i))
                open(fn, "w").write("\n".join(cmds) + "\n")
                print("%s: run %d FAILED (script saved to %s)" % (name, i, fn))
        print("%s: %d/%d identical" % (name, okc, len(runs)))
    print("total: %d scripts compared, %d mismatches, %d output lines" % (total, bad, events))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
