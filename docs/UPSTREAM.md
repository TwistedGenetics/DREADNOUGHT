# Where DREADNOUGHT's code comes from

From 0.3, DREADNOUGHT is its own firmware on a borrowed platform. Everything is GPL-3.0-only unless a file says
otherwise.

**Ours** (Copyright (C) 2026 Jamie / TwistedGenetics): the app and the UI (`src/app/`), the engines and the DSP
building blocks (`src/dsp/`), the device glue's changes, the build script's changes, the host harness and its tests
(`tests/`), the tables generator, the web flasher's page and checks (`web/flasher/` except `vendor/`), the docs.

**The platform** is [X0X](https://github.com/charlesvestal/fm1-x0x) by Charles Vestal, used as a git submodule
(`x0x-firmware/`) pinned to one commit, the way [Carnac OS](https://github.com/OwenKirby/carnac-os) uses it. Nothing in
it is edited: the build takes X0X's `firmware/src/x0x.c` up to the line where its own app starts and puts ours after
it. What we use from X0X (and through it from [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita /
Hügelton Instruments): the HAL, the boot-loop guard and safe mode, the crash screen and watchdog, the panel map and its
calibration, the screen driver, the canvas and fonts, USB MIDI and USB audio, TRS MIDI in, the flash storage, the
M-UPGRADE update entry and the update loader, the package maker and the build checks.

| Part | From | Version / commit | Licence |
|---|---|---|---|
| Platform (`x0x-firmware/`, submodule) | X0X, after Felucca and Melodee | `5048230b86254581d991121916ede3de0d0c91f6` | GPL-3.0-only (fonts and parts as X0X's `LICENSING.md` lists) |
| Device glue (`src/hardware/dn_main_fm1.c`) | Carnac OS `src/hardware/synth_main_fm1.c`, after X0X's `app/main_fm1.c` | 2026-10-23 | GPL-3.0-only |
| Build script (`tools/build_fm1.py`) | Carnac OS `src/hardware/build_fm1.py` | 2026-10-23 | GPL-3.0-only |
| Start-up preflight (`tools/fm1_preflight.py`) | Carnac OS `src/hardware/fm1_preflight.py` (objdump path only changed) | 2026-10-23 | GPL-3.0-only |
| Host harness's screen stubs (`tests/host/dn_host.c`) | X0X `host/x0x_host.c` | via the submodule | GPL-3.0-only |
| REESE's ladder filter and supersaw spacing (`src/dsp/reese.c`) | [Clipper](https://github.com/isod89) `dsp.c`, `eng_hybrid.c` | 2026-10-23 | GPL-3.0-only |
| SVF, polyBLEP, soft clip (`src/dsp/dn_dsp.h`) | written again after Felucca's `dsp.c` | | GPL-3.0-only |
| Sample-slot upload commands (`src/hardware/dn_main_fm1.c`, editor section) | X0X `app/editor.c`, after Felucca's `editor.c` | via the submodule | GPL-3.0-only |
| Breaks upload page (`web/breaks/index.html`, made by `tools/make_breaks_page.py`) | X0X `web/x0x_breaks.html`, `x0x_breaks.js` (encoder and packing unchanged) | via the submodule | GPL-3.0-only |
| IMA ADPCM decode step (`src/dsp/dn_breaks.c`) | Felucca `eng_sample.c` (the standard IMA tables), as X0X uses it | | GPL-3.0-only |
| Web flasher's update-protocol modules (`web/flasher/vendor/`) | SLOOP / Felucca `web/fm1ota.js`, `web/fm1pkg.js` | SLOOP v2.8.1 | GPL-3.0-only |
| FX: TAPE, BBD, SPRING, TOMB, MOD, CELESTIAL, DOLMEN, TUMULUS and the send limiter (`src/dsp/dn_fxc.c`, `dn_fx.c`) | Carnac OS `src/dsp/fx.c` (its ROOM after Felucca's `fx.c` `rev_room`); made per-instance, one core, tempo-synced times, a LOW CUT, float high-passes in the tape (what changed: the file's header) | `6387a21` | GPL-3.0-only |
| FX: MASSIVE (`src/dsp/dn_fxm.c`) | Jamie's own Horizon / Super-Massive (`core/horizon_engine.h`, `horizon_modes.h`, `horizon_sync.h`), ported to C at half rate | TwistedGenetics/Super-Massive | Jamie's (GPL-3.0-only here) |
| FM engine (`src/dsp/dn_fm.c`, tables in `tools/gen_dn_tables.py`) | Dexed's MSFA (`source/msfa`: env, dx7note, fm_core, fm_op_kernel, lfo, pitchenv, sin, exp2, freqlut), Copyright 2012 Google Inc., Pascal Gauthier and the Dexed authors; ported to C, the same integer maths (what changed: the file's header) | Dexed `fea2f0f` | Apache-2.0 (compatible with GPL-3.0; the port is GPL-3.0-only here) |
| FM reference for the tests (`tests/host/ref/msfa/`, host only, not in the firmware) | the same MSFA files, unchanged, with stand-ins for Dexed's tuning, MTS and logger (`tests/host/ref/README.md`) | Dexed `fea2f0f` | Apache-2.0 |

## What the build changes on the way through

- USB names: manufacturer `TwistedGenetics`, product `DREADNOUGHT FM-1` (a copy of X0X's `usb.c` with the two
  strings replaced; "FM-1" stays in the name so every FM-1 flasher still finds the port).
- Package identity: X0X's scheme, `FM-1_9XXYYZZ` from `VERSION` (0.3 → `FM-1_9000300`).
- No package is kept unless the start-up simulation passes and the JieLi indexed 64-bit store miscompile is absent.

## Taking platform fixes

Move the submodule to a newer X0X commit, rebuild, run the host tests and the build checks, and record the new commit
above. `tools/build_fm1.py` refuses to build if the lines it cuts or replaces have changed, rather than guessing.

## 0.1 and 0.2

DREADNOUGHT 0.1 and 0.2 were a fork of [SLOOP](https://github.com/isod89/sloop-fm1) 2.8.1 (isod89) with the REESE
engine added. Their tree is on the `main` branch history up to `93d44c6`.
