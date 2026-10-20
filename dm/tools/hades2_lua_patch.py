#!/usr/bin/env python
"""Make Hades II's Lua UI layout use the real screen size (needed for native wide rendering).
Reads Scripts/RunLogic.lua from YOUR romfs tree, writes a patched copy to <out>/Scripts/RunLogic.lua.
Push <out> to <app files>/d2r-mobile/hades-overlay (shadows the romfs file). No game data is stored in this repo.
usage: hades2_lua_patch.py <romfs tree> <out dir> <width> [height=720]"""
import re, sys, pathlib
src, out, w = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), int(sys.argv[3])
h = int(sys.argv[4]) if len(sys.argv) > 4 else 720
t = (src / "Scripts/RunLogic.lua").read_text(encoding="utf-8")
t, n1 = re.subn(r'(local actualWidth = )GetConfigOptionValue\(\{ Name = "X" \}\)', rf"\g<1>{w}", t)
t, n2 = re.subn(r'(local actualHeight = )GetConfigOptionValue\(\{ Name = "Y" \}\)', rf"\g<1>{h}", t)
assert n1 == 1 and n2 == 1, "RunLogic.lua layout differs from the tested build"
(out / "Scripts").mkdir(parents=True, exist_ok=True)
(out / "Scripts/RunLogic.lua").write_text(t, encoding="utf-8", newline="")
print(f"patched: {w}x{h}")
