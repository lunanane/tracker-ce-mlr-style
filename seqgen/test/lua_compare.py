#!/usr/bin/env python
"""Run the original c31 Lua pages (headless, stubbed iii API) and the C++ port on the same gesture
script and compare the MIDI event streams (pulse/tick-stamped).

  python lua_compare.py [--c31 PATH] [--driver PATH] [--fuzz N] [--seed S] [--script FILE ...]

Needs `lupa` (Lua 5.4 inside Python); no Lua interpreter is installed on the machine otherwise.
Script language (one command per line; shared by both sides):
  start | stop | pulse N | tick N | sync
  root X Y d|u        Camelot cell (X 1..4 = row, Y 1..6 = column of c31's chord grid)
  type N              chord type 1..15 (9..15 = c31 two-button extended types)
  set T|S|D|Z N       settings blocks (c31 x5..8, y1..4), queued
  groove N | par Y X | arch A | pon
  sw N | glen N | pmode N | lock X Y | koct N | inv N | tpose N | gtpose N | mute 0|1
  mstep K X DEG | mnp K N | mep K E | mspd K I | mwin K W | mloop K A B | mclear K | mtie K | mmute K v | mtpose K v
"""
import argparse, os, random, subprocess, sys, tempfile

DEFAULT_C31 = r"D:\OneDrive\CodeLibraries\iii\monome_iii_c31.lua"
HERE = os.path.dirname(os.path.abspath(__file__))

PRELUDE = r"""
EV={}; PC=0; TC=0
grid_led=function() end; grid_led_all=function() end; grid_led_get=function() return 0 end
grid_refresh=function() end; grid_intensity=function() end
midi_note_on=function(n,v,c) EV[#EV+1]={PC,TC,1,c,n,v} end
midi_note_off=function(n,v,c) EV[#EV+1]={PC,TC,0,c,n,0} end
midi_cc=function() end
metro={init=function(fn) MFN=fn; return {start=function() end, stop=function() end} end}
pset_init=function() end; pset_write=function() end; pset_read=function() return nil end; pset_delete=function() end
dofile(CODE)
function TICK(n) for _=1,n do TC=TC+1; MFN() end end
function PULSE(n) for _=1,n do PC=PC+1; event_midi(248,0,0) end end
function GR(x,y,z) event_grid(x,y,z) end
function TAP(x,y) event_grid(x,y,1); event_grid(x,y,0) end
for _=1,25 do MFN() end
"""


class LuaSide:
    def __init__(self, c31):
        from lupa import lua54
        self.rt = lua54.LuaRuntime(unpack_returned_tuples=True)
        g = self.rt.globals()
        g.CODE = c31.replace("\\", "/")
        self.rt.execute(PRELUDE)
        self.g = g
        self.page = 1
        self.menu = False
        self.sel = None
        self.sync_lines = []
        self.eval = self.rt.eval

    # --- page / menu navigation through the real (16,8) menu pad ---
    def tap(self, x, y): self.g.TAP(x, y)

    def ensure_page(self, p):
        if self.page == p and not self.menu: return
        if not self.menu: self.tap(16, 8); self.menu = True
        self.tap(p, 8); self.page = p; self.menu = False

    def ensure_menu(self, p):
        self.ensure_page(p)
        if not self.menu: self.tap(16, 8); self.menu = True

    def ensure_nomenu(self, p):
        self.ensure_page(p)
        if self.menu: self.tap(16, 8); self.menu = False

    def msel(self, k):
        self.ensure_menu(4); self.tap(16, k)

    def run(self, lines):
        g = self.g
        for line in lines:
            t = line.split()
            if not t: continue
            c, a = t[0], t[1:]
            ai = lambda i: int(a[i])
            if c == "start":
                g.event_midi(250, 0, 0)
            elif c == "stop":
                n0 = len(self.rt.eval("EV"))
                g.event_midi(252, 0, 0)
                self.rt.execute("for i=#EV,#EV-895,-1 do EV[i]=nil end")  # drop the 128x7 note-off flood
            elif c == "pulse": g.PULSE(ai(0))
            elif c == "tick": g.TICK(ai(0))
            elif c == "sync":
                s = self.eval("{pages[3].pulse,pages[3].pn,pages[3].blink and 1 or 0}")
                self.sync_lines.append("S %d %d %d" % (s[1], s[2], s[3]))
                self.rt.execute("EV[#EV+1]={PC,TC,'S',pages[3].pulse,pages[3].pn,pages[3].blink and 1 or 0}")
            elif c == "root":
                self.ensure_nomenu(3); g.GR(ai(0), ai(1), 1 if a[2] == "d" else 0)
            elif c == "type":
                self.ensure_nomenu(3); n = ai(0)
                if n <= 8: self.tap((n - 1) % 4 + 1, 7 + (n - 1) // 4)
                else:
                    m = n - 7; g.GR(1, 7, 1); g.GR((m - 1) % 4 + 1, 7 + (m - 1) // 4, 1)
                    g.GR((m - 1) % 4 + 1, 7 + (m - 1) // 4, 0); g.GR(1, 7, 0)
            elif c == "set":
                self.ensure_nomenu(3); y = "TSDZ".index(a[0]) + 1; self.tap(4 + int(a[1]), y)
            elif c == "groove":
                self.ensure_nomenu(3); n = ai(0); self.tap(5 + (n - 1) % 4, 5 + (n - 1) // 4)
            elif c == "par": self.ensure_nomenu(3); self.tap(13 + ai(1), ai(0))
            elif c == "arch": self.ensure_nomenu(3); self.tap(12 + ai(0), 4)
            elif c == "pon": self.ensure_nomenu(3); self.tap(15, 8)
            elif c == "sw": self.ensure_nomenu(3); self.tap(13, 5 + ai(0))
            elif c == "glen":
                self.ensure_nomenu(3)
                cur = int(self.eval("pages[3].glen"))
                for _ in range((ai(0) - cur) % 3): self.tap(14, 8)
            elif c == "pmode": self.ensure_nomenu(3); self.tap(14, 4 + ai(0))
            elif c == "lock":
                self.ensure_nomenu(3); g.GR(14, 6, 1); g.GR(ai(0), ai(1), 1); g.GR(ai(0), ai(1), 0); g.GR(14, 6, 0)
            elif c == "koct": self.ensure_nomenu(3); self.tap(16, 6 - ai(0))
            elif c == "inv":
                self.ensure_nomenu(3); self.tap(15, 6)
                for _ in range(abs(ai(0))): self.tap(15, 5 if ai(0) > 0 else 7)
            elif c == "tpose": self.rt.execute("pages[3].arp_tpose=%d" % ai(0))
            elif c == "gtpose": self.rt.execute("pages[4].gtpose=%d" % ai(0))
            elif c == "mute":
                self.rt.execute("local p=pages[3]; p.amute=%s; if p.amute then p:k_c();p:k_l() end" % ("true" if ai(0) else "false"))
            elif c == "mstep":
                k, x, deg = ai(0), ai(1), ai(2)
                self.msel(k); self.tap(16, 8); self.menu = False  # close menu, step edit works in non-menu mode
                win = int(self.eval("pages[4].tk[%d].win" % k))
                if 0 <= deg - win <= 3: self.tap(x, 7 - (deg - win))  # C++ window is 4 degrees tall
            elif c == "mnp":
                k, n = ai(0), ai(1); self.msel(k)
                g.GR(1, 6, 1); g.GR(n, 6, 1); g.GR(n, 6, 0); g.GR(1, 6, 0)
            elif c == "mep":
                self.msel(ai(0))
                if ai(1) <= int(self.eval("pages[4].tk[%d].np" % ai(0))): self.tap(ai(1), 6)  # C++ PAGE+ would extend np
            elif c == "mspd": self.msel(ai(0)); self.tap(ai(1), 1)
            elif c == "mwin":
                k = ai(0); self.msel(k); cur = int(self.eval("pages[4].tk[%d].win" % k))
                while cur < ai(1): self.tap(15, 5); cur += 1
                while cur > ai(1): self.tap(15, 6); cur -= 1
            elif c == "mloop":
                k = ai(0); self.msel(k); self.tap(16, 8); self.menu = False
                g.GR(ai(1), 8, 1); g.GR(ai(2), 8, 1); g.GR(ai(2), 8, 0); g.GR(ai(1), 8, 0)
            elif c == "mclear": self.msel(ai(0)); self.tap(14, 6)
            elif c == "mtie": self.msel(ai(0)); self.tap(9, 6)
            elif c == "mmute": self.rt.execute("pages[4].tk[%d].mute=%s" % (ai(0), "true" if ai(1) else "false"))
            elif c == "mtpose": self.rt.execute("pages[4].tk[%d].tpose=%d" % (ai(0), ai(1)))
            else: raise SystemExit("unknown command: " + line)

    def events(self):
        out = []
        n = int(self.eval("#EV"))
        for i in range(1, n + 1):
            e = self.eval("EV[%d]" % i)
            if e[3] == "S":
                out.append("S %d %d %d" % (e[4], e[5], e[6])); continue
            kind, ch, note, vel = e[3], e[4], e[5], e[6]
            if ch is None or ch < 3 or ch > 6: continue
            out.append("p%d t%d %s %d %d %d" % (e[1], e[2], "on" if kind == 1 else "off", ch, note, vel if vel is not None else -1))
        return out


def run_cpp(driver, lines):
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
        f.write("\n".join(lines) + "\n"); name = f.name
    try:
        r = subprocess.run([driver, name], capture_output=True, text=True, check=True)
    finally:
        os.unlink(name)
    return r.stdout.splitlines()


def normalise(ev):
    """Note-offs released together come out of Lua's pairs() in unspecified order: sort each run of offs."""
    out, run = [], []
    def flush():
        run.sort(); out.extend(run); run.clear()
    for e in ev:
        if e.split()[2:3] == ["off"] and not e.startswith("S"):
            run.append(e)
        else:
            flush(); out.append(e)
    flush()
    return out


def compare(lua, cpp, lines, label):
    # the C++ driver prints 'S' lines only for sync; the Lua side records them in the same stream
    a, b = normalise(lua.events()), normalise(cpp)
    if a == b: return True, len(a)
    print("MISMATCH in", label)
    for i, (x, y) in enumerate(zip(a + [None] * len(b), b + [None] * len(a))):
        if x != y:
            print("  first difference at event %d: lua=%r cpp=%r" % (i, x, y)); break
    print("  lua events: %d, cpp events: %d" % (len(a), len(b)))
    return False, len(a)


# ---------------- random script generator ----------------
def gen_script(rng, nlines=260):
    L = ["par 1 2"]  # triggers pgen on the Lua side (c31 never calls it before a PARAM edit, vgv would be nil)
    held = []
    win = {1: 0, 2: 0, 3: 0}; np_ = {1: 1, 2: 1, 3: 1}
    r = rng.random
    L.append("start")
    for _ in range(nlines):
        x = r()
        if x < 0.22: L.append("pulse %d" % rng.choice([1, 1, 2, 3, 6, 12, 24, 30, 96]))
        elif x < 0.30: L.append("tick %d" % rng.choice([1, 2, 3, 5, 8]))
        elif x < 0.40:
            if held and r() < 0.55:
                h = held.pop(rng.randrange(len(held))); L.append("root %d %d u" % h)
            else:
                h = (rng.randint(1, 4), rng.randint(1, 6))
                if h not in held: held.append(h); L.append("root %d %d d" % h)
        elif x < 0.46: L.append("type %d" % rng.randint(1, 15))
        elif x < 0.54: L.append("set %s %d" % (rng.choice("TSDZ"), rng.randint(1, 4)))
        elif x < 0.58: L.append("groove %d" % rng.randint(1, 16))
        elif x < 0.61: L.append("par %d %d" % (rng.randint(1, 3), rng.randint(0, 3)))
        elif x < 0.63: L.append("arch %d" % rng.randint(1, 4))
        elif x < 0.645: L.append("pon")
        elif x < 0.66: L.append("sw %d" % rng.randint(0, 2))
        elif x < 0.675: L.append("glen %d" % rng.randint(1, 3))
        elif x < 0.69:
            held = []; L.append("pmode %d" % rng.randint(1, 3))
        elif x < 0.70: held = []; L.append("lock %d %d" % (rng.randint(1, 4), rng.randint(1, 6)))
        elif x < 0.715: L.append("koct %d" % rng.randint(-1, 1))
        elif x < 0.73: L.append("inv %d" % rng.randint(-4, 4))
        elif x < 0.74: L.append("tpose %d" % rng.randint(-3, 3))
        elif x < 0.745: L.append("gtpose %d" % rng.randint(-2, 2))
        elif x < 0.75: L.append("mute %d" % rng.randint(0, 1))
        elif x < 0.85:
            k = rng.randint(1, 3)
            deg = win[k] + rng.randint(0, 3)
            L.append("mstep %d %d %d" % (k, rng.randint(1, 16), deg))
        elif x < 0.88:
            k = rng.randint(1, 3); win[k] = rng.randint(-3, 7); L.append("mwin %d %d" % (k, win[k]))
        elif x < 0.90:
            k = rng.randint(1, 3); np_[k] = rng.randint(2, 4); L.append("mnp %d %d" % (k, np_[k]))
        elif x < 0.92:
            k = rng.randint(1, 3); L.append("mep %d %d" % (k, rng.randint(1, np_[k])))
        elif x < 0.94: L.append("mspd %d %d" % (rng.randint(1, 3), rng.choice([4, 5, 6, 7, 8, 8, 9, 10, 11])))
        elif x < 0.96:
            a, b = rng.randint(1, 15), rng.randint(1, 15); L.append("mloop %d %d %d" % (rng.randint(1, 3), a, b))
        elif x < 0.975: L.append("mtie %d" % rng.randint(1, 3))
        elif x < 0.98:
            k = rng.randint(1, 3); np_[k] = 1; L.append("mclear %d" % k)
        elif x < 0.985: L.append("mmute %d %d" % (rng.randint(1, 3), rng.randint(0, 1)))
        elif x < 0.99: L.append("mtpose %d %d" % (rng.randint(1, 3), rng.randint(-3, 3)))
        elif x < 0.995: L.append("sync")
        else: L.append("stop"); L.append("start"); held = []
    return L


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--c31", default=DEFAULT_C31)
    ap.add_argument("--driver", default=os.path.join(HERE, "..", "build", "lua_driver.exe"))
    ap.add_argument("--fuzz", type=int, default=0)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--script", nargs="*", default=[])
    a = ap.parse_args()
    a.driver = os.path.abspath(a.driver)
    total, bad, events = 0, 0, 0
    jobs = []
    for s in a.script:
        jobs.append((s, [l.rstrip("\n") for l in open(s) if l.strip() and not l.startswith("#")]))
    rng = random.Random(a.seed)
    for i in range(a.fuzz):
        jobs.append(("fuzz seed=%d #%d" % (a.seed, i), gen_script(rng)))
    for label, lines in jobs:
        lua = LuaSide(a.c31)
        lua.run(lines)
        cpp = run_cpp(a.driver, lines)
        ok, n = compare(lua, cpp, lines, label)
        total += 1; events += n; bad += (not ok)
        if not ok:
            p = os.path.join(tempfile.gettempdir(), "seqgen_fail_%d.txt" % total)
            open(p, "w").write("\n".join(lines) + "\n"); print("  script saved to", p)
    print("lua_compare: %d scripts, %d identical, %d mismatching, %d events compared" % (total, total - bad, bad, events))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
