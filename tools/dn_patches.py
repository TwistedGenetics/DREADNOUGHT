# SPDX-License-Identifier: GPL-3.0-only
# DREADNOUGHT: the changes it makes to X0X's sources (the pinned x0x-firmware submodule) as it builds, in one place so
# the host tests build the same text (tests/run_host.sh). Copyright (C) 2026 Jamie (TwistedGenetics).
#
# storage.c (0.29): two more settings objects, DN_OBJ_EXT0 / EXT1 (16 and 17), each an A/B pair of sectors at
# 0xD8000..0xDBFFF: the top 16 KB of sample slot 3, which is 64 KB from 0.29 (slots 1 and 2 stay 80 KB).
import sys
from pathlib import Path

STORAGE = [
    ("static uint32_t st_sector(uint32_t obj, uint32_t copy)  /* flash offset of copy A (0) / B (1) */\n{\n",
     "static uint32_t st_sector(uint32_t obj, uint32_t copy)  /* flash offset of copy A (0) / B (1) */\n{\n"
     "    if (obj >= 16u)                               /* DREADNOUGHT 0.29: EXT0, EXT1 in slot 3's top 16 KB */\n"
     "        return 0xD8000u + (obj - 16u) * 2u * ST_SECTOR + copy * ST_SECTOR;\n"),
]


def patched(text, patches, name):
    for old, new in patches:
        if text.count(old) != 1:
            raise SystemExit(f"dn_patches: X0X's {name} changed, no '{old[:50]}...'")
        text = text.replace(old, new)
    return text


def storage(x0x_src: Path) -> str:
    """X0X's storage.c with DREADNOUGHT's two settings objects"""
    return patched((x0x_src / "storage.c").read_text(), STORAGE, "storage.c")


if __name__ == "__main__":                              # dn_patches.py storage X0X_SRC OUT
    if len(sys.argv) != 4 or sys.argv[1] != "storage":
        raise SystemExit("usage: dn_patches.py storage X0X_SRC OUT")
    Path(sys.argv[3]).write_text(storage(Path(sys.argv[2])))
