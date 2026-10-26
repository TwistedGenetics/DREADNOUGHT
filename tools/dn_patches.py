# SPDX-License-Identifier: GPL-3.0-only
# DREADNOUGHT: the changes it makes to X0X's sources (the pinned x0x-firmware submodule) as it builds, in one place so
# the host tests build the same text (tests/run_host.sh). Copyright (C) 2026 Jamie (TwistedGenetics).
#
# ldr_core.c (0.43): the update loader writes the app's first sector (its header) LAST, and erases it before any other
# sector is written. Felucca's loader wrote from 0x4000 up: an install cut part-way left a valid header over a
# half-written app, which the FM-1's boot stage starts instead of the update loader: a black screen and no USB (a unit
# was lost that way, 2026-10-25). With the header gone, a cut install leaves no app to start: the boot stage runs the
# loader again from its record (or, at worst, the chip's own boot mode answers). After Skiff's loader (illuminus5,
# GPL-3.0), which found the same on the SMK-37 Pro.
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

LDR_CORE = [
    ("#define LDR_REC_HI 0xFC000u                     /* ... and below Felucca's globals */\n",
     "#define LDR_REC_HI 0xFC000u                     /* ... and below Felucca's globals */\n"
     "#define LDR_NSEC ((LDR_APP_HI - LDR_APP_LO) >> 12)   /* (DREADNOUGHT 0.43) */\n"),
    ("    uint32_t i, k, fl_off, fl_size, ota_off, ota_len, key, s, done = 0;\n",
     "    uint32_t i, k, fl_off, fl_size, ota_off, ota_len, key, s, done = 0;\n"
     "    uint32_t n, head_gone = 0;                           /* (DREADNOUGHT 0.43: the app's header erased) */\n"),
    ("    /* 3. app area, sector by sector */\n    for (s = LDR_APP_LO; s < LDR_APP_HI; s += 0x1000u) {\n",
     "    /* 3. app area, sector by sector. DREADNOUGHT 0.43: the app's first sector (its header) is erased before any\n"
     "     * other sector is written, and written last (0x5000 up, then 0x4000): a cut install leaves no app to start */\n"
     "    for (n = 1; n <= LDR_NSEC; n++) {\n"
     "        s = LDR_APP_LO + (n % LDR_NSEC) * 0x1000u;\n"),
    ("        if (!ota_memeq(sec, cur, 0x1000u)) {\n            uint32_t tries;\n",
     "        if (!ota_memeq(sec, cur, 0x1000u)) {\n            uint32_t tries;\n"
     "            if (!head_gone && s != LDR_APP_LO) {         /* (the first sector that changes: the header goes first) */\n"
     "                if (ldr_erase(LDR_APP_LO))\n"
     "                    return -9;\n"
     "                head_gone = 1;\n"
     "            }\n"),
]


def ldr_core(x0x_loader: Path) -> str:
    """X0X's ldr_core.c with the header written last"""
    return patched((x0x_loader / "ldr_core.c").read_text(), LDR_CORE, "ldr_core.c")


def patched(text, patches, name):
    for old, new in patches:
        if text.count(old) != 1:
            raise SystemExit(f"dn_patches: X0X's {name} changed, no '{old[:50]}...'")
        text = text.replace(old, new)
    return text


def storage(x0x_src: Path) -> str:
    """X0X's storage.c with DREADNOUGHT's two settings objects"""
    return patched((x0x_src / "storage.c").read_text(), STORAGE, "storage.c")


if __name__ == "__main__":                              # dn_patches.py storage|ldr_core X0X_DIR OUT
    if len(sys.argv) != 4 or sys.argv[1] not in ("storage", "ldr_core"):
        raise SystemExit("usage: dn_patches.py storage X0X_SRC OUT | ldr_core X0X_LOADER OUT")
    Path(sys.argv[3]).write_text((storage if sys.argv[1] == "storage" else ldr_core)(Path(sys.argv[2])))
