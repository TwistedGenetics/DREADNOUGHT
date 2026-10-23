# Where DREADNOUGHT's code comes from

DREADNOUGHT is a fork. Everything is GPL-3.0-only unless a file says otherwise; see `LICENSING.md`
(SLOOP's, kept as it is) and `LICENSES/`.

| Part | From | Version / commit | Licence |
|---|---|---|---|
| Firmware, HAL, update loader, build and package tools, host tests, web editor (`firmware/`, `tools/`, `tests/`, `web/` except `web/flasher/`, `assets/`) | [SLOOP](https://github.com/isod89/sloop-fm1) by isod89, itself after [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita / Hügelton Instruments | v2.8.1, `d89364253b79a5c6002c0d90c5f16b3c3c16ea9d`, imported 2026-10-23 | GPL-3.0-only (parts MIT / Apache-2.0 / CC0 / OFL as listed in `LICENSING.md`) |
| Second-core start-up (inside SLOOP's HAL) | [X0X](https://github.com/charlesvestal/fm1-x0x) by Charles Vestal, after Melodee | via SLOOP | GPL-3.0-only |
| Web flasher's update-protocol modules (`web/flasher/vendor/`) | SLOOP / Felucca `web/fm1ota.js`, `web/fm1pkg.js` | SLOOP v2.8.1 | GPL-3.0-only |
| Start-up pre-flight check (`tools/preflight.py`) | [Carnac OS](https://github.com/OwenKirby/carnac-os) by OrbVorgis, `src/hardware/fm1_preflight.py` | 2026-10-23 | GPL-3.0-only |
| SLOOP's own documentation | kept for reference in `docs/upstream/` | v2.8.1 | GPL-3.0-only |

## What DREADNOUGHT changed in the imported code

Each change is marked `DREADNOUGHT` in a comment where it is made.

- Name: version string, USB manufacturer and port name (`DREADNOUGHT`), boot splash, About page, rescue and crash screens.
- The installers' MIDI port filters also accept `dreadnought` (`web/fm1ota.js`, `web/editor.html`, `tools/fm1_install.py`).
- The editor INFO reply still reports `SLOOP 2.8.1` as its protocol version, so the inherited editor keeps its version-gated behaviour.
- Rescue screen: OCT− + OCT+ held 5 s enters the chip's own update mode (UBOOT), as the main loop already did.
- `tests/run_tests.sh` reports failing tests as GitHub annotations; `tests/run_host_only.sh` runs the toolchain-free tests locally.

## Taking upstream fixes

SLOOP moves fast. To bring in a later SLOOP fix, diff that SLOOP release against v2.8.1 and apply the
change by hand, keeping the `DREADNOUGHT` changes above. Record the new base here.
