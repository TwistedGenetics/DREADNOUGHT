# DREADNOUGHT

A dark jungle / drum & bass firmware for the **M-VAVE FM-1**: Reese and dread basslines, distorted 808s,
wavetables (with Serum import), a Rex-style break chopper, pads, tech stabs and a heavy FX section,
sequenced from the box, with a Massive-style interface.

**Status: 0.8 — chop by hand, overlap slides, a roll that follows.** DREADNOUGHT's own app, screen and engines on [X0X](https://github.com/charlesvestal/fm1-x0x)'s
proven FM-1 platform (boot-loop guard, safe mode, USB, storage, updates). First engine: **REESE**, measured against
the maths (see [docs/REESE.md](docs/REESE.md)), an 8-track step sequencer, and BREAKS, a Rex-style break chopper: track 1 plays
REESE, tracks 2 and 3 chop breaks (two built in, or your own from the [breaks page](https://twistedgenetics.github.io/DREADNOUGHT/breaks/)),
tracks 4–8 play other gear over MIDI until their engines arrive. Every push builds a `.fwsc` in GitHub Actions and publishes it as a
download (below).

## Web editor

[**twistedgenetics.github.io/DREADNOUGHT/editor**](https://twistedgenetics.github.io/DREADNOUGHT/editor/) (Chrome or
Edge, FM-1 on USB with DREADNOUGHT 0.7 or later): all 8 tracks in a mouse-driven piano roll and slice grid, the REESE
and BREAKS controls, play / stop, tempo, save; back up everything to a file and restore it; keep as many REESE sounds
as you like in the browser or as files. It follows the box live: turn a knob on the FM-1 and the page moves too.
**Demo** tries it with a simulated FM-1.

## Download & install

Go to [**Releases**](https://github.com/TwistedGenetics/DREADNOUGHT/releases/latest) and download `DREADNOUGHT-0.8.fwsc`
(the newest version's file). It's a beta that hasn't been played on a real FM-1 yet: read
[docs/RECOVERY.md](docs/RECOVERY.md) first and back up your FM-1.

- **Web flasher:** open [twistedgenetics.github.io/DREADNOUGHT/flasher](https://twistedgenetics.github.io/DREADNOUGHT/flasher/)
  in Chrome or Edge with the FM-1 on USB, back up, then pick the newest release from the list or choose the
  `.fwsc` file you downloaded, and press Install.
- **Command line:** X0X's installer, `python3 x0x-firmware/tools/fm1_install.py DREADNOUGHT-0.8.fwsc` (needs `pip install mido python-rtmidi`).

The FM-1 restarts on the DREADNOUGHT screen. Press **PLAY** to hear the demo reese line.

| Control | Does |
|---|---|
| PLAY | sequencer start / stop (MIDI clock, start and stop go out on USB) |
| REC | record: keys write into the selected track (playing: at the nearest step, as long as you hold the key; stopped: step by step) |
| SEQ | the sequencer, pressed again to cycle: **OVERVIEW** (8 tracks) → **ROLL** (piano roll, synth and MIDI tracks) → **DRUM** (break slices) |
| SEL / EDIT / ENV / LFO / GLO | REESE's pages (OSC, FILTER, AMP, MOD, OUT) |
| ARP | BREAKS, pressed again to cycle: **BRK** (loop, pitch, decay, crush) → **BRK2** (choke group, slices GRID / AUTO / MANUAL, stretch REPITCH / SLICE / STRETCH, level, CPU load) → **SLICE** (chop mode, below) |
| Keys (sound pages) | play (mono, last note wins; hold one key and press another to slide); with track 2 or 3 selected: the break's slices |
| White keys (OVERVIEW, ROLL, DRUM) | the 16 steps: tap on / off; hold one and turn KNOB 1 note (break: slice), 2 length (¼ step to 64 steps), 3 accent, 4 slide (break: reverse); **SELECT** moves it in time (over the other notes; the bar in view follows it). The window follows a held note's pitch / slice |
| Black keys 1–8 (OVERVIEW, ROLL, DRUM) | mute tracks 1–8; black key 9 held 1 s clears the track; 10 / 11 move through steps 1–64 |
| KNOB 1–4 (OVERVIEW) | track, track length, swing, steps in view |
| KNOB 1–4 (ROLL) | scroll the pitches, transpose the track, track length, steps in view |
| FX (ROLL) | KEYS mode: the keys write notes at a cursor (KNOB 2 their length, KNOB 4 the cursor, HOME a rest; a key under a held one: a slide) |
| KNOB 1–4 (DRUM) | the slice row (a tap puts it on a step), track length, swing, steps in view |
| SELECT | the page (sound pages), the track (OVERVIEW, ROLL), BREAKS A / B (BRK pages, DRUM); with a step held: moves it |
| KNOB 4 (OUT) | **OVERLAP** (was OCT, which stays on OCT− / OCT+): SLIDE — a REESE note whose length reaches past the next note glides into it, no new attack, in GLIDE's time (MOD page, now shown in ms); CUT — it retriggers |
| PRESETS | the factory sounds; on the BRK pages and DRUM, the loop |
| ALGORITHM | tempo |
| HOME | hold a note, hands free |
| OCT− / OCT+ | octave |
| SAVE (tap) | keep the sound, the pattern and your slices for next time (lit when changed) |
| SAVE held + OCT− / OCT+ | copy / paste the bar in view of the selected track |
| OCT− + OCT+ held 5 s | update mode (recovery) |

**Chop mode (ARP → BRK → BRK2 → SLICE)**, the MPC / SP-1200 way: a break in MANUAL starts as one slice, the whole
loop, and you chop it where you hear the hits. Chops save themselves (3 s after the last edit, no SAVE needed), and
adding or deleting one never changes what your pattern plays (the steps follow their chop).

| Control (SLICE page) | Does |
|---|---|
| KNOB 1 | select a chop (the cursor jumps to it, you hear it) |
| KNOB 2 / KNOB 3 | the cursor through the loop: coarse (an eighth of the screen a detent) / fine (4 samples) |
| KNOB 4 | zoom: the whole loop … 1/64 of it, around the cursor |
| REC | chop at the cursor (snapped to a zero crossing); while PLAY loops it: chop where it's playing (tap along to the beat) |
| PLAY | loop the break from the cursor (again: stop) |
| FX | move the selected chop to the cursor |
| HOME | delete the selected chop; held 1 s: delete them all |
| Keys | play chop 1, 2, 3 … from the lowest key (OCT+ for the 28th and up), and select it; lit: the selected chop, dim: the others |
| SAVE held + OCT− / OCT+ | start from AUTO's hits / from the GRID instead |

On MIDI tracks an overlap sends the new note-on before the old note-off (legato for the synth on the other end); the
web editor's **Overlap** switch sets SLIDE / CUT for every synth and MIDI track. Break tracks never slide.

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
python3 tools/build_fm1.py --release 0.8        # build/fm1/DREADNOUGHT-0.8.fwsc (preflight included)
```

## Tests

```
sh tests/run_host.sh                             # engines measured, the whole app driven from the panel: no toolchain
node --test 'web/flasher/test/*.test.mjs' 'web/editor/test/*.test.mjs'   # flasher and editor checks
python3 tools/make_site.py none site && python3 web/editor/test/e2e_demo.py site   # the editor in Chromium (playwright)
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
