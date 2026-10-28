#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# DREADNOUGHT: puts DREADNOUGHT into M-VAVE's SMK37 OPEN kit as its APP, ready for the kit's own build
# (sdk\Build.ps1 -Model pro|elite). Copyright (C) 2026 Jamie (TwistedGenetics).
#   smk_assemble.py KIT [GEN]  KIT: a checkout of m-vave/smk37_open (the commit .github/workflows/smk37.yml pins);
#                              GEN: the tables and the font made already (dn_tables.h dn_wt_tables.h dn_font.h: the
#                              font needs Pillow with libraqm, which Windows lacks), else made here
#
# What it does to KIT/app:
#   - takes out M-VAVE's instrument's APP files (its screen, projects, presets, transfers, samples, MIDI router, DX7
#     library, panel mapping); keeps its board drivers (app/board), its headers and app/synth (the build checks them)
#   - puts in smk/app (DREADNOUGHT's APP: screen.c transfer.c midi_router.c project.c dn_smk.h) and app/version.h
#   - puts DREADNOUGHT's sources in app/dn: the engines (src/dsp/*.c: built one by one), the app (src/app/*.c as .inc:
#     screen.c includes them), src/core, and the generated tables and fonts
#   - changes one thing in M-VAVE's board/audio.c: the audio interrupt renders DREADNOUGHT (dn_smk_render)
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
USER_BYTES = 176 * 1024                              # (the user area: smk/app/transfer.c's layout must agree)


def main():
    kit = Path(sys.argv[1]).resolve()
    gen = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None
    app = kit / "app"
    if not (kit / "sdk" / "Build.ps1").exists() or not (app / "board" / "audio.c").exists():
        raise SystemExit(f"smk_assemble: {kit} is not the SMK37 OPEN kit")

    # M-VAVE's instrument's APP out
    for name in ["screen.c", "project.c", "presets.c", "transfer.c", "samples.c", "midi_router.c", "fm_library.c",
                 "fm_factory.c", "smk37_controls.c"]:
        (app / name).unlink(missing_ok=True)

    # DREADNOUGHT's APP in
    for f in (REPO / "smk" / "app").iterdir():
        if f.is_file():
            shutil.copy2(f, app / f.name)

    # the version: VERSION X.Y -> the kit's release "V<Y>" (X = 0), the update identity 8000000 + Y
    ver = (REPO / "VERSION").read_text(encoding="utf-8").strip()
    m = re.fullmatch(r"(\d+)\.(\d+)", ver)
    if not m:
        raise SystemExit(f"smk_assemble: VERSION {ver!r} is not X.Y")
    n = int(m.group(1)) * 100 + int(m.group(2))
    if not 1 <= n <= 999:
        raise SystemExit(f"smk_assemble: VERSION {ver} gives release V{n}, outside V01..V999")
    rel = f"V{n:02d}"
    (app / "version.h").write_text(
        "/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */\n"
        f"/* DREADNOUGHT {ver} (tools/smk_assemble.py) */\n"
        "#ifndef FM1_VERSION_H\n#define FM1_VERSION_H\n"
        f'#define FM1_RELEASE_VERSION "{rel}"\n'
        f'#define FM1_UPDATE_PRODUCT_PRO "SMK-37 Pro_{8000000 + n}"\n'
        f'#define FM1_UPDATE_PRODUCT_ELITE "SMK-37 Elite_{8000000 + n}"\n'
        "#endif\n")

    # the sources, in app/dn
    dn = app / "dn"
    if dn.exists():
        shutil.rmtree(dn)
    dn.mkdir()
    for f in (REPO / "src" / "dsp").iterdir():
        if f.suffix == ".h":
            shutil.copy2(f, dn / f.name)
        elif f.suffix == ".c":                       # (built one by one: each sees the SMK's settings, DN_FX_SMALL ...)
            (dn / f.name).write_text('#include "dn_smk_version.h"                  /* (tools/smk_assemble.py) */\n'
                                     + f.read_text(encoding="utf-8"), encoding="utf-8")
    for f in (REPO / "src" / "core").glob("*.h"):
        shutil.copy2(f, dn / f.name)
    for f in (REPO / "src" / "app").glob("*.h"):
        shutil.copy2(f, dn / f.name)
    for name in ["dn_app", "dn_editor", "dn_splash"]:
        text = (REPO / "src" / "app" / f"{name}.c").read_text(encoding="utf-8")
        if name == "dn_app":
            if text.count('#include "dn_editor.c"') != 1:
                raise SystemExit("smk_assemble: dn_app.c no longer includes dn_editor.c once")
            text = text.replace('#include "dn_editor.c"', '#include "dn_editor.inc"')
        (dn / f"{name}.inc").write_text(text, encoding="utf-8")
    for script, out in [("gen_dn_tables.py", "dn_tables.h"), ("gen_dn_wt.py", "dn_wt_tables.h"),
                        ("gen_dn_font.py", "dn_font.h")]:
        if gen:
            shutil.copy2(gen / out, dn / out)
        else:
            subprocess.run([sys.executable, str(REPO / "tools" / script), str(dn / out)], check=True,
                           stdout=subprocess.DEVNULL)
    (dn / "dn_smk_version.h").write_text(
        "/* made by tools/smk_assemble.py */\n#pragma once\n"
        f'#define DN_VERSION "{ver}"\n'
        f"#define DN_FX_ARENA ({os.environ.get('SMK_FX_KB') or '84'}u * 1024u)  /* (the SMK's RAM: less than the FM-1's 176 KB) */\n"
        "#define DN_GFX_BAND_ROWS 32                         /* (the screen drawn in bands: src/core/dn_gfx.h) */\n"
        "#define DN_GFX_OPS 640u\n#define DN_GFX_CHARS 2048u\n#define DN_GFX_ARGS 512u\n"
        "#define DN_SLOT_BYTES (40u * 1024u)                 /* (the sample slots: transfer.c) */\n"
        "#define DN_SLOT3_BYTES (24u * 1024u)\n"
        "#define DN_POOL_FAST __attribute__((section(\".memp_memory_x\"), aligned(4)))   /* (the D-cache's spare ways) */\n"
        "#define DN_FX_SMALL 1                               /* (the echoes half as long: the FX arena is small) */\n"
        "#define DN_PBUF_SHARED ((uint8_t *)(void *)dg_strip) /* (the patterns' store buffer: the screen band's 15 KB) */\n"
        "#define DN_PAT_TMP_SHARED ((dn_pattern_t *)(void *)dg_ops)   /* (a pattern unpacked: the display list's) */\n")

    # the audio interrupt renders DREADNOUGHT
    audio = app / "board" / "audio.c"
    text = audio.read_text(encoding="utf-8")
    old = """    /* The library renders the instrument; APP mixes external audio. */
    if (fm1_engine_render(rendered, FM1_BLOCK_FRAMES) != 0) {
        ++statistics.overruns;
        return;
    }
"""
    if text.count(old) != 1 or text.count('#include "audio_output.h"\n') != 1:
        raise SystemExit("smk_assemble: board/audio.c has changed; look at it again")
    text = text.replace(old, """    /* DREADNOUGHT renders (app/dn_smk.h, tools/smk_assemble.py); APP mixes external audio. */
    dn_smk_render(rendered, FM1_BLOCK_FRAMES);
""")
    text = text.replace('#include "audio_output.h"\n', '#include "audio_output.h"\n#include "dn_smk.h"\n')
    audio.write_text(text, encoding="utf-8")
    # the kit's build script, as build-dn.ps1 beside it: the same build and checks, but for the parts of M-VAVE's own
    # instrument (DREADNOUGHT does not use it, so the linker leaves it out): absent is fine; present, the same checks
    b = (kit / "sdk" / "Build.ps1").read_text(encoding="utf-8-sig")
    edits = [
        ("""$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$sdk = $PSScriptRoot""", """$root = [IO.Path]::GetFullPath($PSScriptRoot)
$sdk = Join-Path $root 'sdk'"""),
        ("""    foreach ($name in $required) { if (!$info.ContainsKey($name)) { throw "Required runtime symbol absent: $name" } }""",
         """    foreach ($name in $required) { if ($name -like 'groove_*') { continue }; if (!$info.ContainsKey($name)) { throw "Required runtime symbol absent: $name" } }"""),
        ("""    foreach ($name in @('fm1_r_dee1ea411566','fm1_r_d16405f329ed','fm1_r_f03cc16d330d','fm1_r_1993f9b103e3',
            'fm1_io_trs_midi_enabled','fm1_r_05431712fb7b','fm1_r_0aa010fe1de1','fm1_r_33d5e5b63b6b','fm1_app_sysex')) {""",
         """    foreach ($name in @('fm1_io_trs_midi_enabled','fm1_app_sysex')) {"""),
        ("""        if (!$info.ContainsKey($name)) { throw "Audio callback absent: $name" }""",
         """        if (!$info.ContainsKey($name)) {
            if ($name -in @('fm1_audio_irq','fm1_audio_copy','fm1_output_mix','fm1_usb_audio_read','fm1_usb_audio_write','fm1_flash_worker_prepare','cpu_enter_sfc_critical','cpu_exit_sfc_critical')) { throw "Audio callback absent: $name" }
            continue
        }"""),
    ]
    # the spare cache RAM's buffers (DN_POOL_FAST) come out of the compiler as PROGBITS; the startup clears that
    # section (it reads cache_ram_bss_vma / _size), so it is not packaged, as .boot_info is not
    edits += [("elseif ($name -cne '.boot_info')", "elseif ($name -cnotin @('.boot_info','.cache_ram_bss'))")]
    # (0.46) a smaller user area: M-VAVE's layout keeps 524 KB for user data, which leaves the firmware ~460 KB with
    # the SDK's ~280 KB in it; DREADNOUGHT needs ~800 KB. 176 KB of user flash (the store, and 104 KB of samples):
    # smk/app/transfer.c lays it out. The packager (Package.cs, MIT) takes the size as given (Package-dn.cs).
    edits += [
        ("$variant = [pscustomobject]@{user_bytes=536576;", f"$variant = [pscustomobject]@{{user_bytes={USER_BYTES};"),
        ("Add-Type -Path (Join-Path $sdk 'Package.cs')", "Add-Type -Path (Join-Path $root 'Package-dn.cs')"),
    ]
    pk = (kit / "sdk" / "Package.cs").read_text(encoding="utf-8-sig")
    old_pk = "const int FlashLimit = 0xff000, UserSize = 536576, BtSize = 4096, MinimumVm = 32768;"
    if pk.count(old_pk) != 1:
        raise SystemExit("smk_assemble: sdk/Package.cs has changed; look at it again")
    pk = pk.replace(old_pk, f"const int FlashLimit = 0xff000, UserSize = {USER_BYTES}, BtSize = 4096, MinimumVm = 32768;")
    (kit / "Package-dn.cs").write_text("// DREADNOUGHT: the SMK37 OPEN kit's sdk/Package.cs, its user area " + str(USER_BYTES)
                                       + " bytes (tools/smk_assemble.py)\n" + pk, encoding="utf-8")
    for old, new in edits:
        if b.count(old) != 1:
            raise SystemExit("smk_assemble: sdk/Build.ps1 has changed; look at it again: " + old.splitlines()[0][:70])
        b = b.replace(old, new)
    (kit / "build-dn.ps1").write_text("# DREADNOUGHT: the SMK37 OPEN kit's sdk/Build.ps1, its checks of M-VAVE's instrument relaxed "
                                      "(tools/smk_assemble.py)\n" + b, encoding="utf-8")
    print(f"smk_assemble: DREADNOUGHT {ver} ({rel}) in {app}")


if __name__ == "__main__":
    main()
