#!/usr/bin/env python
"""Print both event streams for a script: python dump.py script.txt"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lua_compare as lc
lines = [l.strip() for l in open(sys.argv[1]) if l.strip()]
lua = lc.LuaSide(lc.DEFAULT_LUA); lua.run(lines)
a = lc.normalise(lua.events()); b = lc.normalise(lc.run_cpp(os.path.join(lc.HERE, "..", "build", "lua_driver.exe"), lines))
for i in range(max(len(a), len(b))):
    x = a[i] if i < len(a) else ""; y = b[i] if i < len(b) else ""
    print("%-28s %-28s %s" % (x, y, "" if x == y else "<<<"))
