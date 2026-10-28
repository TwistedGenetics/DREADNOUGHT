# DREADNOUGHT on the M-VAVE SMK-37 Pro and Elite (beta)

DREADNOUGHT, the FM-1 firmware, on M-VAVE's SMK-37 keyboards. It is built as the "APP" of M-VAVE's own
[SMK37 OPEN kit](https://github.com/m-vave/smk37_open), so it uses M-VAVE's update path, USB and Bluetooth MIDI, and
the keyboard's own panel drivers. Not made or supported by M-VAVE.

**Beta: not yet tried on a keyboard.** Back up before you install.

## Installing

1. Back up what M-VAVE's firmware keeps (projects, DX7 voices, samples) with
   [M-VAVE's workbench](https://open.m-vave.com/smk37-workbench). DREADNOUGHT erases it.
2. Download the flasher page for your model: `DREADNOUGHT-<version>-SMK37-Pro.html` or `...-Elite.html` (from the
   release, or from the `smk37` workflow's run in GitHub Actions: the `smk37-pro` / `smk37-elite` artifact).
   Each page carries the firmware for its model and M-VAVE's V16 to go back.
3. Open the page in desktop Chrome or Edge (it works from the file). Plug the keyboard in by USB, close any other app
   that uses MIDI.
4. The first time (from M-VAVE's firmware): tick "running M-VAVE's own firmware", Connect, then Install. Later
   updates: Connect, Install.
5. To go back: on the same page, **Restore M-VAVE V16**, then turn the keyboard off and on.

## The panel

DREADNOUGHT's panel is the FM-1's (14 buttons, 27 keys, 7 encoders); the SMK's controls stand in for them:

| SMK-37 | DREADNOUGHT (the FM-1's) |
|---|---|
| keys F (the 6th key) to G two octaves up | the FM-1's 27 keys F3..G5 (OCT- / OCT+ move them) |
| pads 1-16 | the FM-1's 16 white keys F3..G5: on SEQ, the 16 steps; lit as those keys light |
| K1 K2 K3 K4 | KNOB 1-4 |
| SELECT encoder | SELECT |
| PAGE encoder | ALGORITHM (the tempo) |
| PRESETS encoder | PRESETS |
| the 8th encoder | the volume |
| HOME, SAVE, OCT-, OCT+, ARP, SELECT (button), ENV, GLO, EDIT, FX, SEQ, PLAY, REC | the FM-1 buttons of the same name |
| SHIFT + ENV | LFO |

(The roles follow how M-VAVE's own firmware maps the SMK's buttons onto the FM-1's; if a label on your keyboard
disagrees, say which.) Faders, wheels, the pedal and pad pressure are not used yet.

## What is different from the FM-1

- **Sample memory: 104 KB** (slots of 40, 40 and 24 KB), against the FM-1's 224 KB. The SMK-37 has 1 MB of flash;
  M-VAVE's library takes about 280 KB and DREADNOUGHT about 430 KB, so the user area is 176 KB here instead of
  M-VAVE's 524 KB. This layout is not the one M-VAVE tested; Restore M-VAVE V16 puts theirs back.
- **Effects: about 84 KB of memory** (176 KB on the FM-1): DELAY, TAPE and BBD echo half as long, the tank reverbs
  (MOD, CELESTIAL, DOLMEN, TUMULUS) are half the size; an effect pair that does not fit turns FX B off.
- **One core** for the sound (the FM-1 can use two): a heavy project may run out of time sooner.
- Samples and loops go on from the web editor (its Samples tab). The FM-1's breaks page does not know the SMK's
  smaller slots.
- Bluetooth: as M-VAVE's library starts it (DREADNOUGHT does not set it up yet). USB MIDI works as on the FM-1.

## Building

GitHub Actions (`.github/workflows/smk37.yml`, Windows): it installs JieLi's toolchain 2.5.3 (the kit checks it file
by file), checks out the M-VAVE kit at a pinned commit, puts DREADNOUGHT in as its APP (`tools/smk_assemble.py`),
builds both models with the kit's build script (as `build-dn.ps1`: its checks of M-VAVE's own instrument relaxed, its
user area 176 KB), and makes the flasher pages (`tools/smk_flasher.py`). On Windows by hand:

```
git clone https://github.com/m-vave/smk37_open kit && git -C kit checkout b2d6bcb
python tools/smk_assemble.py kit
cd kit && .\build-dn.ps1 -Model pro -FirmwareOnly -Toolchain C:\JL\pi32
python ..\tools\smk_flasher.py . pro DREADNOUGHT-SMK37-Pro.html
```

(`tools/smk_assemble.py` needs the font made on a machine with Pillow + libraqm; CI makes it on Linux.)

## Licence

DREADNOUGHT is GPL-3.0; for this build it carries the same linking permission M-VAVE's kit gives its APP
(`smk/LICENSE-EXCEPTION.md`), and every part of it here is DREADNOUGHT's own (`docs/UPSTREAM.md`). The firmware also
contains M-VAVE's closed SMK37 OPEN library, passed on under its terms (`THIRD_PARTY.md` in the kit), which allow it
for the SMK-37 Pro and Elite only. The source of a build: this repository at its commit, and the kit commit the
workflow pins.
