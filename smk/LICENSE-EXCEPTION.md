# DREADNOUGHT for the SMK-37: the linking permission

Copyright (C) 2026 Jamie (TwistedGenetics).

DREADNOUGHT is free software under the GNU General Public License, version 3 only (`LICENSE`). For its build on the
M-VAVE SMK-37 Pro and Elite (`smk/`, `tools/smk_assemble.py`), every part of it that goes into that firmware is
Jamie's own work (see `docs/UPSTREAM.md`, "Ours alone"), and Jamie grants this:

## Additional permission under GNU GPL version 3 section 7

If you modify this Program, or any covered work, by linking or combining it with the SMK37 Open binary SDK published
by M-VAVE (`sdk/lib/libfm1_engine.a` and the accompanying binary SDK components listed in that SDK's
`sdk/manifest.json`), or a modified version of that library, containing parts covered by the terms of that SDK's
binary terms (its `THIRD_PARTY.md`), the licensors of this Program grant you additional permission to convey the
resulting work.

This is the same permission M-VAVE gives its own SMK37 OPEN APP ("GPL-3.0-only WITH GPL-3.0-linking-exception"). It
covers DREADNOUGHT's sources that the SMK build takes: `src/app/`, `src/dsp/`, `src/core/`, `smk/`, and the tables,
fonts and headers that `tools/` makes for it. DREADNOUGHT itself, and any modification of it, stays under GPLv3: give
the corresponding source (this repository at the release's tag, plus the M-VAVE kit commit that
`.github/workflows/smk37.yml` pins) with every firmware you pass on. The SDK keeps its own terms; this permission does
not grant rights in it or in anyone else's code.

The FM-1 build (`src/hardware/`, the `x0x-firmware` platform) links nothing closed and is not covered by this
permission; it is plain GPL-3.0-only.

Fonts: Barlow Semi Condensed, SIL Open Font License 1.1 (`assets/fonts/Barlow-OFL.txt`). FM: after Dexed's MSFA,
Apache-2.0 (`docs/UPSTREAM.md`).
