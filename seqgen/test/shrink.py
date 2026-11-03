#!/usr/bin/env python
"""Delta-debug a failing lua_compare script: python shrink.py script.txt"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lua_compare as lc
c31, drv = lc.DEFAULT_LUA, os.path.join(lc.HERE, "..", "build", "lua_driver.exe")
def bad(lines):
    lua = lc.LuaSide(c31)
    try: lua.run(lines)
    except Exception: return False
    return lc.normalise(lua.events()) != lc.normalise(lc.run_cpp(drv, lines))
lines = [l.rstrip("\n") for l in open(sys.argv[1]) if l.strip()]
assert bad(lines)
changed = True
while changed:
    changed = False
    i = 0
    while i < len(lines):
        t = lines[:i] + lines[i+1:]
        if bad(t): lines = t; changed = True
        else: i += 1
print("\n".join(lines))
