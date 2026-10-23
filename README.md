# DREADNOUGHT

A dark jungle / drum & bass firmware for the **M-VAVE FM-1**: Reese and dread basslines, distorted 808s,
wavetables (with Serum import), a Rex-style break chopper, pads, tech stabs and a heavy FX section,
sequenced from the box, with a Massive-style interface.

**Status: 0.3 — our own firmware.** DREADNOUGHT's own app, screen and engines on [X0X](https://github.com/charlesvestal/fm1-x0x)'s
proven FM-1 platform (boot-loop guard, safe mode, USB, storage, updates). First engine: **REESE**, measured against
the maths (see [docs/REESE.md](docs/REESE.md)). Every push builds a `.fwsc` in GitHub Actions and publishes it as a
download (below).

## Download & install

Go to [**Releases**](https://github.com/TwistedGenetics/DREADNOUGHT/releases/latest) and download `DREADNOUGHT-0.3.fwsc`
(the newest version's file). It's a beta that hasn't been played on a real FM-1 yet: read
[docs/RECOVERY.md](docs/RECOVERY.md) first and back up your FM-1.

- **Web flasher:** open [twistedgenetics.github.io/DREADNOUGHT/flasher](https://twistedgenetics.github.io/DREADNOUGHT/flasher/)
  in Chrome or Edge with the FM-1 on USB, back up, then pick the newest release from the list or choose the
  `.fwsc` file you downloaded, and press Install.
- **Command line:** X0X's installer, `python3 x0x-firmware/tools/fm1_install.py DREADNOUGHT-0.3.fwsc` (needs `pip install mido python-rtmidi`).

The FM-1 restarts on the DREADNOUGHT screen, then the REESE page:

| Control | Does |
|---|---|
| Keys | play (mono, last note wins; hold one key and press another to slide) |
| OCT− / OCT+ | octave |
| SELECT, or SEL / EDIT / ENV / LFO / GLO | page: OSC, FILTER, AMP, MOD, OUT |
| KNOB 1–4 | the four dials on the page |
| PRESETS | the factory sounds |
| ALGORITHM | tempo (for BEAT, which locks the reese's movement to it) |
| PLAY | hold a note, hands free |
| SAVE | keep the sound for next time (lit when changed) |
| OCT− + OCT+ held 5 s | update mode (recovery) |

Releases are automatic: when the version in `VERSION` changes (e.g. `0.2`) and is pushed to main, Actions builds
it and publishes a new Release with `DREADNOUGHT-X.Y.fwsc`. Pushes that keep the same version refresh that
Release's file.

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
git submodule update --init                     # X0X's platform, pinned
tools/get_deps.sh                               # JieLi toolchain + 3 AC79 SDK files, hash-checked
python3 tools/build_fm1.py --release 0.3        # build/fm1/DREADNOUGHT-0.3.fwsc (preflight included)
```

## Tests

```
sh tests/run_host.sh                             # engines measured, the whole app driven from the panel: no toolchain
node --test 'web/flasher/test/*.test.mjs'       # flasher checks
```

`tests/run_host.sh` leaves screenshots (`build/host/ui-*.png`) and recordings (`build/host/*.wav`) to look at and
listen to; CI keeps them as the `host-…` artifact.

## Credits and licence

GPL-3.0-only (see `LICENSE`). Built on the work of the FM-1 community: [Felucca](https://github.com/hugelton/Felucca)
(Leo Kuroshita / Hügelton Instruments), [SLOOP and Clipper](https://github.com/isod89) (isod89), whose update
protocol code the flasher uses (`web/flasher/vendor/`), [X0X](https://github.com/charlesvestal/fm1-x0x)
(second-core start-up), [FM-1-RE](https://github.com/AL-255/FM-1-RE) and M-VAVE's
[FM1 Open](https://github.com/m-vave/fm1_open). Not affiliated with M-VAVE or Native Instruments.
Installing custom firmware is at your own risk.
