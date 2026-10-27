#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# CI (smk37.yml): what fills the SMK's RAM, from the linker's map: the biggest input sections placed in internal RAM
# (0x1c00000..0x1c80000) and the totals by file. Copyright (C) 2026 Jamie (TwistedGenetics).
#   smk_mapsum.py sdk.map
import re
import sys
from collections import defaultdict

LO, HI = 0x1C00000, 0x1C80000
ROM_LO, ROM_HI = 0x2000000, 0x2200000
rows, by_file, rom_file = [], defaultdict(int), defaultdict(int)
pending = None
for line in open(sys.argv[1], errors="replace"):
    m = re.match(r"^ (\.\S+)\s*$", line)
    if m:
        pending = m.group(1)
        continue
    m = re.match(r"^ (\.\S+)?\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(\S.*)$", line)
    if m:
        name = m.group(1) or pending
        pending = None
        addr, size, src = int(m.group(2), 16), int(m.group(3), 16), m.group(4).strip()
        base = re.sub(r".*[\\/]", "", src)
        if LO <= addr < HI and size and name:
            rows.append((size, name, src))
            by_file[base] += size
        elif ROM_LO <= addr < ROM_HI and size and name:
            rom_file[base] += size
total = sum(s for s, _, _ in rows)
print(f"internal RAM taken by input sections: {total} bytes")
for s, n, src in sorted(rows, reverse=True)[:45]:
    base = re.sub(r".*[\\/]", "", src)
    print(f"{s:7d} {n[:48]:48s} {base[:40]}")
print("by file:")
for f, s in sorted(by_file.items(), key=lambda x: -x[1])[:15]:
    print(f"{s:7d} {f}")
print(f"flash (code and constants): {sum(rom_file.values())} bytes; by file:")
for f, s in sorted(rom_file.items(), key=lambda x: -x[1])[:15]:
    print(f"{s:7d} {f}")
