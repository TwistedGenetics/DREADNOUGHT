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
    ver = (REPO / "VERSION").read_text().strip()
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
        if f.suffix in (".c", ".h"):
            shutil.copy2(f, dn / f.name)
    for f in (REPO / "src" / "core").glob("*.h"):
        shutil.copy2(f, dn / f.name)
    for f in (REPO / "src" / "app").glob("*.h"):
        shutil.copy2(f, dn / f.name)
    for name in ["dn_app", "dn_editor", "dn_splash"]:
        text = (REPO / "src" / "app" / f"{name}.c").read_text()
        if name == "dn_app":
            if text.count('#include "dn_editor.c"') != 1:
                raise SystemExit("smk_assemble: dn_app.c no longer includes dn_editor.c once")
            text = text.replace('#include "dn_editor.c"', '#include "dn_editor.inc"')
        (dn / f"{name}.inc").write_text(text)
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
        f"#define DN_FX_ARENA ({os.environ.get('SMK_FX_KB', '112')}u * 1024u)  /* (the SMK's RAM: less than the FM-1's 176 KB) */\n"
        "#define DN_GFX_BAND_ROWS 24                         /* (the screen drawn in bands: src/core/dn_gfx.h) */\n"
        "#define DN_GFX_OPS 640u\n#define DN_GFX_CHARS 2048u\n#define DN_GFX_ARGS 512u\n")

    # the audio interrupt renders DREADNOUGHT
    audio = app / "board" / "audio.c"
    text = audio.read_text()
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
    audio.write_text(text)
    print(f"smk_assemble: DREADNOUGHT {ver} ({rel}) in {app}")


if __name__ == "__main__":
    main()
