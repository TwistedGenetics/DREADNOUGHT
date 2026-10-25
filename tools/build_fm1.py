#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build DREADNOUGHT for the FM-1 on X0X's platform: app, update loader and an installable .fwsc.

  python3 tools/build_fm1.py [--release X.Y] [--out DIR]

Adapted from Carnac OS's src/hardware/build_fm1.py (owenkirby/carnac-os, GPL-3.0-only). Reuses X0X's tools/build.py
(the x0x-firmware submodule, pinned) whole: the update loader, the image layout, the package and its checks (entry
stub, no soft doubles, RAM / pool sizes, register access only in hal/, which here covers our sources too). Only the
app's units differ:

  dreadnought_fm1.c  -Os  X0X's firmware/src/x0x.c up to its app (slots.c), then src/hardware/dn_main_fm1.c (which
                          brings in src/app/dn_app.c and dn_splash.c); USB names DREADNOUGHT
  dreadnought_dsp.c  -O2  every src/dsp/*.c (the engines: the audio path)

Then, before any package is kept: the start-up simulated on the host (tools/fm1_preflight.py) and the check for the
JieLi compiler's indexed 64-bit store miscompile (Carnac, 2026-10-16).

Needs the JieLi toolchain and the AC79 SDK files (tools/get_deps.sh): JIELI_TOOLCHAIN and AC79_SDK.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
X0X = REPO / "x0x-firmware"


def u16(s):
    """a USB string descriptor's bytes, as C"""
    b = [2 + 2 * len(s), 3]
    for ch in s:
        b += [f"'{ch}'", 0]
    return "{" + ", ".join(str(x) for x in b) + "}"


USB = [  # X0X's names -> ours (the flashers find the port by "FM-1" in the name)
    ("static const uint8_t STR1[] = {8, 3, 'X', 0, '0', 0, 'X', 0};",
     f"static const uint8_t STR1[] = {u16('TwistedGenetics')};"),
    ("static const uint8_t STR2[] = {18, 3, 'X', 0, '0', 0, 'X', 0, ' ', 0, 'F', 0, 'M', 0, '-', 0, '1', 0};",
     f"static const uint8_t STR2[] = {u16('DREADNOUGHT FM-1')};"),
]


def overlay(b, gen):
    """the two unity files in the build dir; returns them"""
    cut = []
    for ln in (b.FW / "src" / "x0x.c").read_text().splitlines(keepends=True):
        if ln.startswith('#include "app/slots.c"'):
            break
        cut.append(ln)
    else:
        raise SystemExit("build_fm1: x0x.c has no '#include \"app/slots.c\"' to cut at")
    inc = lambda p: f'#include "{p.as_posix()}"\n'
    # usb.c with our names: a copy next to the unity file, included in place of X0X's
    usb = (b.FW / "src" / "usb.c").read_text()
    for old, new in USB:
        if usb.count(old) != 1:
            raise SystemExit(f"build_fm1: X0X's usb.c changed, no '{old[:40]}...'")
        usb = usb.replace(old, new)
    # (0.42) USB audio's ten channels (uac_multi, uac_tap_mc: tools/usb_mc.json, X0X's text -> ours, in order)
    import json
    for old, new in json.loads((REPO / "tools" / "usb_mc.json").read_text()):
        if usb.count(old) != 1:
            raise SystemExit(f"build_fm1: X0X's usb.c changed, no '{old.strip()[:40]}...' (tools/usb_mc.json)")
        usb = usb.replace(old, new)
    (gen / "usb_dn.c").write_text(usb)
    text = "".join(cut)
    if text.count('#include "usb.c"') != 1:
        raise SystemExit("build_fm1: x0x.c no longer includes usb.c once")
    text = text.replace('#include "usb.c"', inc(gen / "usb_dn.c").rstrip())
    # storage.c with DREADNOUGHT's settings objects in slot 3's top 16 KB (0.29; tools/dn_patches.py)
    sys.path.insert(0, str(REPO / "tools"))
    import dn_patches
    (gen / "storage_dn.c").write_text(dn_patches.storage(b.FW / "src"))
    if text.count('#include "storage.c"') != 1:
        raise SystemExit("build_fm1: x0x.c no longer includes storage.c once")
    text = text.replace('#include "storage.c"', inc(gen / "storage_dn.c").rstrip())
    main = gen / "dreadnought_fm1.c"
    main.write_text(text + "/* ---- DREADNOUGHT (X0X's app cut away; tools/build_fm1.py) ---- */\n" +
                    inc(REPO / "src" / "hardware" / "dn_main_fm1.c"))
    dsp = gen / "dreadnought_dsp.c"
    dsp.write_text("/* DREADNOUGHT's engines, one -O2 unit (tools/build_fm1.py) */\n" +
                   "".join(inc(p) for p in sorted((REPO / "src" / "dsp").glob("*.c"))))
    return main, dsp


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--release", metavar="X.Y", help="the version: the package's identity FM-1_9XXYY00 and the screen")
    ap.add_argument("--out", type=Path, default=REPO / "build" / "fm1", help="where the package goes")
    a = ap.parse_args()
    os.environ.setdefault("JIELI_TOOLCHAIN", "/opt/jieli/toolchain")
    os.environ.setdefault("AC79_SDK", "/opt/fw-AC79_AIoT_SDK")
    if not (X0X / "tools" / "build.py").exists():
        raise SystemExit("build_fm1: no X0X tree in x0x-firmware/ (git submodule update --init)")
    if b"\r\n" in (X0X / "tools" / "gen_builtin_break.sh").read_bytes():
        raise SystemExit("build_fm1: x0x-firmware has CRLF line endings; clone with core.autocrlf=false")
    ver = a.release or "DEV"
    sys.path.insert(0, str(X0X / "tools"))
    import build as b                                   # X0X's tools/build.py

    dngen = a.out / "gen"

    def generate():                                     # the cut x0x.c needs only the font; we add our tables
        b.GEN.mkdir(parents=True, exist_ok=True)
        dngen.mkdir(parents=True, exist_ok=True)
        for cmd in ([sys.executable, str(b.SRC / "tools" / "gen_font.py"), str(b.GEN / "felucca_font.h")],
                    [sys.executable, str(REPO / "tools" / "gen_dn_tables.py"), str(dngen / "dn_tables.h")],
                    [sys.executable, str(REPO / "tools" / "gen_dn_wt.py"), str(dngen / "dn_wt_tables.h")]):
            # (0.29: no built-in breaks in the image; tools/gen_dn_breaks.py makes them for the host tests only)
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode:
                sys.stderr.write(r.stdout + r.stderr)
                raise SystemExit(f"build_fm1: {Path(cmd[1]).name} failed")

    def build_app():
        gen = b.OUT / "dreadnought"
        gen.mkdir(parents=True, exist_ok=True)
        main_c, dsp_c = overlay(b, gen)
        inc = [f"-I{(REPO / d).as_posix()}" for d in ("src", "src/app", "src/dsp", "src/hardware")]
        flags = [*b.CFLAGS, *b.FPU, "-Ifirmware/hal", "-Ifirmware/src", "-Ibuild/gen", f"-I{dngen.as_posix()}", *inc,
                 f'-DFELUCCA_ID="{b.PRODUCT}"', f'-DDN_VERSION="{ver} BETA"' if a.release else '-DDN_VERSION="DEV"']
        o2 = [f if f != "-Os" else "-O2" for f in flags]
        objs = [b.OUT / "crt0.o", b.OUT / "fm1_vec.o", b.OUT / "fm1_isr.o", b.OUT / "fm1_cpu1.o",
                gen / "dreadnought_fm1.o", gen / "dreadnought_dsp.o"]
        b.tc_all(("cc", "-c", b.FW / "crt0.S", "-o", objs[0]),
                 ("cc", "-c", b.FW / "hal" / "fm1_vec.S", "-o", objs[1]),
                 ("cc", "-c", b.FW / "hal" / "fm1_isr.S", "-o", objs[2]),
                 ("cc", "-c", b.FW / "hal" / "fm1_cpu1.S", "-o", objs[3]),
                 ("cc", *flags, "-c", main_c, "-o", objs[4]),
                 ("cc", *o2, "-c", dsp_c, "-o", objs[5]))
        return link(objs)

    def link(objs):
        # X0X's build_app from the link on, unchanged: borrowed with its compile step stubbed out (Carnac's way)
        real_all, real_tc, real_o2 = b.tc_all, b.tc, b.O2_UNITS
        first = [True]

        def tc_all(*cmds):
            if first[0]:                                # its compile batch: ours already ran
                first[0] = False
                return []
            return real_all(*cmds)

        def tc(tool, *args):
            if tool == "pi32v2/bin/ld":                 # X0X's objects out, ours in
                args = [x for x in args if not (isinstance(x, Path) and x.suffix == ".o")]
                i = args.index("-o")
                args = [*args[:i], *objs, *args[i:]]
            return real_tc(tool, *args)
        b.tc_all, b.tc, b.O2_UNITS = tc_all, tc, []
        try:
            return real_build_app()
        finally:
            b.tc_all, b.tc, b.O2_UNITS = real_all, real_tc, real_o2

    real_mmio = b.mmio_check

    def mmio_check():                                   # X0X's rule (registers only in hal/) over our sources too
        fw = b.FW
        errors = real_mmio()
        shadow = b.OUT / "dreadnought" / "mmio"
        shutil.rmtree(shadow, ignore_errors=True)
        (shadow / "src").mkdir(parents=True)
        shutil.copytree(fw / "hal", shadow / "hal")
        for p in (REPO / "src").rglob("*.[ch]"):
            d = shadow / "src" / p.relative_to(REPO / "src")
            d.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy(p, d)
        b.FW = shadow
        try:
            return errors + real_mmio()
        finally:
            b.FW = fw

    real_build_app = b.build_app
    b.generate, b.build_app, b.mmio_check = generate, build_app, mmio_check
    a.out.mkdir(parents=True, exist_ok=True)
    for old in a.out.glob("*.fwsc"):                    # never leave an older package to be installed by mistake
        old.unlink()
    for old in b.OUT.glob("x0x*.fwsc"):
        old.unlink()
    sys.argv = [sys.argv[0]] + (["--release", a.release] if a.release else [])   # the identity: FM-1_9XXYY00
    b.main()
    built = sorted(b.OUT.glob("x0x*.fwsc"))
    if len(built) != 1:
        raise SystemExit(f"build_fm1: expected one package from X0X's build, found {[p.name for p in built]}")
    pkg = built[0]
    # the start-up, simulated on the host: no package unless RAM comes out as the program expects
    r = subprocess.run([sys.executable, str(REPO / "tools" / "fm1_preflight.py"), str(b.OUT / "x0x.elf"),
                        str(b.OUT / "x0x.bin"), str(REPO / "src" / "hardware" / "dn_main_fm1.c")])
    if r.returncode:
        pkg.unlink(missing_ok=True)
        raise SystemExit("build_fm1: preflight FAILED: no package written (do not install this build)")
    bad = [ln.strip() for ln in (b.OUT / "x0x.dis").read_text(errors="replace").splitlines()
           if re.search(r"\bd\[r\d+\+r\d+<<\d\] = ", ln)]
    if bad:
        pkg.unlink(missing_ok=True)
        raise SystemExit("build_fm1: %d indexed 64-bit store(s) (the JieLi miscompile Carnac found), e.g.\n  %s\n"
                         "no package written: check that loop" % (len(bad), bad[0]))
    print("  ok    no indexed 64-bit stores (the JieLi miscompile, Carnac 2026-10-16)")
    name = f"DREADNOUGHT-{a.release}" if a.release else "dreadnought-dev"
    shutil.copy(pkg, a.out / f"{name}.fwsc")
    for src, dst in ((b.OUT / "x0x.bin", "dreadnought.bin"), (b.OUT / "x0x.elf", "dreadnought.elf"),
                     (b.OUT / "x0x.dis", "dreadnought.dis"), (b.OUT / "x0x.map", "dreadnought.map")):
        if src.exists():
            shutil.copy(src, a.out / dst)
    sizes(b, a.out)
    print(f"build_fm1: {a.out / (name + '.fwsc')}  identity {b.PRODUCT}")
    return 0


SLOT_MARGIN = 24 * 1024                                 # CI fails below this much free in the app slot


def sizes(b, out):
    """What the firmware really takes (the .fwsc is padded to the slot, so its size says nothing): the image
    against the app slot, RAM (.data + .bss, the pool), the biggest symbols. Printed, written to sizes.md, and on
    GitHub as notices and the job summary."""
    import json
    syms = b.tc("common/bin/objdump", "-t", b.OUT / "x0x.elf")
    img = (b.OUT / "x0x.bin").stat().st_size
    slot = b.APP_SLOT
    sect, rows = {}, []
    for ln in syms.splitlines():
        m = re.match(r"^([0-9a-f]{8})\s.{7}\s(\S+)\s+([0-9a-f]{8})\s+(\S+)$", ln)
        if not m:
            continue
        sz = int(m[3], 16)
        if not sz:
            continue
        sect[m[2]] = sect.get(m[2], 0) + sz
        rows.append((sz, m[2], m[4]))
    rows.sort(reverse=True)

    def sym(name):
        mm = re.search(r"^([0-9a-f]+) .*\s" + name + r"$", syms, re.M)
        return int(mm.group(1), 16) if mm else 0
    ram = sym("_bss_end") - 0x01C08000
    pool = sym("_pool_end") - sym("_pool_start")
    ours = sum(sz for sz, s, n in rows if s in (".text", ".rodata", ".data") and re.match(r"(dn_|reese|DN_|RS_|ed_)", n))
    rep = {"image": img, "slot": slot, "slot_free": slot - img, "ram_data_bss": ram, "ram_region": 98304,
           "pool": pool, "pool_region": 0x54000, "sections": sect, "dreadnought_symbols": ours,
           "top": [{"size": sz, "section": s, "name": n} for sz, s, n in rows[:40]]}
    md = [f"## Firmware size ({b.PRODUCT})", "",
          "| | used | of | free |", "|---|---|---|---|",
          f"| App slot (code + constants) | {img:,} B | {slot:,} B | **{slot - img:,} B** |",
          f"| RAM .data + .bss | {ram:,} B | 98,304 B | {98304 - ram:,} B |",
          f"| RAM pool | {pool:,} B | {0x54000:,} B | {0x54000 - pool:,} B |", "",
          "Sections: " + ", ".join(f"{k} {v:,} B" for k, v in sorted(sect.items(), key=lambda kv: -kv[1])[:8]), "",
          "| size | section | symbol |", "|---|---|---|"] + [f"| {sz:,} | {s} | `{n}` |" for sz, s, n in rows[:40]]
    (out / "sizes.md").write_text("\n".join(md) + "\n")
    (out / "sizes.json").write_text(json.dumps(rep, indent=1))
    print("\n".join(md[:8]))
    if os.environ.get("GITHUB_ACTIONS"):
        print(f"::notice title=App slot::{img} B of {slot} B used, {slot - img} B free")
        print(f"::notice title=RAM::.data+.bss {ram} B of 98304; pool {pool} B of {0x54000} ({0x54000 - pool} free)")
        print("::notice title=Biggest::" + "; ".join(f"{n} {s} {sz}" for sz, s, n in rows[:12]))
        rr = [(sz, s, n) for sz, s, n in rows if s in (".data", ".bss", "COMMON")]
        print("::notice title=Biggest in RAM::" + "; ".join(f"{n} {sz}" for sz, s, n in rr[:40]))
        if os.environ.get("GITHUB_STEP_SUMMARY"):
            with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as f:
                f.write("\n".join(md) + "\n")
    if slot - img < SLOT_MARGIN:
        raise SystemExit(f"build_fm1: only {slot - img} B free in the app slot (< {SLOT_MARGIN})")


if __name__ == "__main__":
    sys.exit(main())
