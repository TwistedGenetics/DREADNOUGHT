# DREADNOUGHT

A dark jungle / drum & bass firmware for the **M-VAVE FM-1**: Reese and dread basslines, distorted 808s,
wavetables (with Serum import), a Rex-style break chopper, pads, tech stabs and a heavy FX section,
sequenced from the box, with a Massive-style interface.

**Status: 0.4 — the sequencer.** DREADNOUGHT's own app, screen and engines on [X0X](https://github.com/charlesvestal/fm1-x0x)'s
proven FM-1 platform (boot-loop guard, safe mode, USB, storage, updates). First engine: **REESE**, measured against
the maths (see [docs/REESE.md](docs/REESE.md)), and an 8-track step sequencer: track 1 plays REESE, tracks 2–8 play
other gear over MIDI until their engines arrive. Every push builds a `.fwsc` in GitHub Actions and publishes it as a
download (below).

## Download & install

Go to [**Releases**](https://github.com/TwistedGenetics/DREADNOUGHT/releases/latest) and download `DREADNOUGHT-0.4.fwsc`
(the newest version's file). It's a beta that hasn't been played on a real FM-1 yet: read
[docs/RECOVERY.md](docs/RECOVERY.md) first and back up your FM-1.

- **Web flasher:** open [twistedgenetics.github.io/DREADNOUGHT/flasher](https://twistedgenetics.github.io/DREADNOUGHT/flasher/)
  in Chrome or Edge with the FM-1 on USB, back up, then pick the newest release from the list or choose the
  `.fwsc` file you downloaded, and press Install.
- **Command line:** X0X's installer, `python3 x0x-firmware/tools/fm1_install.py DREADNOUGHT-0.4.fwsc` (needs `pip install mido python-rtmidi`).

The FM-1 restarts on the DREADNOUGHT screen. Press **PLAY** to hear the demo reese line.

| Control | Does |
|---|---|
| PLAY | sequencer start / stop (MIDI clock, start and stop go out on USB) |
| REC | record: keys write into the selected track (playing: at the nearest step; stopped: step by step) |
| SEQ | the SEQ page; SEL / EDIT / ENV / LFO / GLO: the sound's pages (OSC, FILTER, AMP, MOD, OUT) |
| Keys (sound pages) | play (mono, last note wins; hold one key and press another to slide) |
| KNOB 1–4 (sound pages) | the four dials on the page |
| White keys (SEQ page) | the 16 steps: tap on / off; hold one and turn KNOB 1 note, 2 length, 3 accent, 4 slide |
| Black keys 1–8 (SEQ page) | mute tracks 1–8; black key 9 held 1 s clears the track; 10 / 11 move through steps 1–64 |
| KNOB 1–4 (SEQ page) | track, track length (1–64), swing, steps in view |
| PRESETS | the factory sounds |
| ALGORITHM | tempo |
| HOME | hold a note, hands free |
| OCT− / OCT+ | octave |
| SAVE | keep the sound and the pattern for next time (lit when changed) |
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
python3 tools/build_fm1.py --release 0.4        # build/fm1/DREADNOUGHT-0.4.fwsc (preflight included)
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
