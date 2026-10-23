# DREADNOUGHT

A dark jungle / drum & bass firmware for the **M-VAVE FM-1**: Reese and dread basslines, distorted 808s,
wavetables (with Serum import), a Rex-style break chopper, pads, tech stabs and a heavy FX section,
sequenced from the box, with a Massive-style interface.

**Status: Phase 1 — first build.** A fork of SLOOP 2.8.1 with the DREADNOUGHT name and the new **REESE**
engine (track engines: REESE is number 12, its presets lead the BASS list). Every push builds a `.fwsc` in
GitHub Actions (Actions → the run → Artifacts). Not yet played on a real FM-1: read
[docs/RECOVERY.md](docs/RECOVERY.md) first.

| | |
|---|---|
| The plan | [PROMPT.md](PROMPT.md) |
| What the hardware can do | [docs/PLATFORM.md](docs/PLATFORM.md) |
| What fits, what gets cut, the base decision | [docs/FEASIBILITY.md](docs/FEASIBILITY.md) |
| Before you flash anything | [docs/RECOVERY.md](docs/RECOVERY.md) |
| Web flasher | `web/flasher/`, published to GitHub Pages by `.github/workflows/pages.yml` |

## Building

Everything builds in GitHub Actions (`.github/workflows/build.yml`) on Linux with JieLi's toolchain;
each run attaches the `.fwsc` package as an artifact. To build on Linux x86-64 yourself:

```
tools/get_deps.sh            # JieLi toolchain + 3 AC79 SDK files, hash-checked
python3 tools/build.py       # build/felucca.fwsc
python3 tools/preflight.py build
```

## Tests

```
tests/run_host_only.sh                           # DSP, sequencer, UI, recovery: no JieLi toolchain needed
sh tests/run_tests.sh                            # the full suite, after a target build
node --test 'web/flasher/test/*.test.mjs'       # flasher checks
python3 web/flasher/test/e2e_mock.py             # flasher in Chromium against a simulated FM-1 (needs playwright)
```

## Credits and licence

GPL-3.0-only (see `LICENSE`). Built on the work of the FM-1 community: [Felucca](https://github.com/hugelton/Felucca)
(Leo Kuroshita / Hügelton Instruments), [SLOOP and Clipper](https://github.com/isod89) (isod89), whose update
protocol code the flasher uses (`web/flasher/vendor/`), [X0X](https://github.com/charlesvestal/fm1-x0x)
(second-core start-up), [FM-1-RE](https://github.com/AL-255/FM-1-RE) and M-VAVE's
[FM1 Open](https://github.com/m-vave/fm1_open). Not affiliated with M-VAVE or Native Instruments.
Installing custom firmware is at your own risk.
