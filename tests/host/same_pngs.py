#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# DREADNOUGHT host tests: the PNGs in B are pixel for pixel the ones of the same name in A. Copyright (C) 2026 Jamie
# (TwistedGenetics).
import sys
from pathlib import Path
from PIL import Image, ImageChops

a, b = Path(sys.argv[1]), Path(sys.argv[2])
n, bad = 0, []
for f in sorted(b.glob("*.png")):
    g = a / f.name
    if not g.exists():
        continue
    n += 1
    if ImageChops.difference(Image.open(g).convert("RGB"), Image.open(f).convert("RGB")).getbbox():
        bad.append(f.name)
if bad or not n:
    print(f"FAIL {b}: {len(bad)} of {n} screens differ: {', '.join(bad[:8])}")
    sys.exit(1)
print(f"ok   {b}: {n} screens the same as {a}'s")
