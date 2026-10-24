# FM-1 platform notes

What the hardware and the existing firmwares actually give us, read from their source on 23 Oct 2026.
Every number here has a source in the table at the bottom. "Unverified" means nobody has measured it
on a device yet, as far as we know.

## Chip

| | |
|---|---|
| SoC | JieLi **AC791N / WL82** (AC79 family), `pi32v2` CPU. The `JL-BR22` strings in the stock firmware are inherited library names, not the chip. |
| Clock | **360 MHz** measured on a running FM-1 by X0X (M-VAVE's boot sets it; it can't go higher). Static analysis of the stock firmware (FM-1-RE) found a 240 MHz PLL set-up; the running figure is the one to plan with. |
| Cores | **2**. Core 0 runs everything by default; core 1 can be started and handed audio jobs (X0X, SLOOP 2.7+ and Clipper do this). |
| Floating point | **Single-precision float is usable in audio code.** X0X's DSP is floating-point, and Carnac OS measured float filters *faster* on the device than its 64-bit integer versions (~6 % per voice). Doubles go through software helpers, so: `float` yes, `double` never in the audio path, integer where it measures faster (Carnac keeps its block FX in integer). |
| RAM | About **512 KB internal SRAM** (`RAM0_SIZE` in the official linker script is just under 0x80000), **no SDRAM/PSRAM**. SLOOP's map: a 96 KiB RAM region, a ~336 KB pool, ~29 KB of "low RAM". Plus 28 KB of data-cache RAM. |
| Flash | **1 MB in total**, linear-mapped for execute-in-place at `0x02000000`. Single bank: there is no second copy of the firmware to fall back to. Firmware images are ~580–700 KB, so user storage (projects, presets, samples) is what's left. |

## Audio

| | |
|---|---|
| Rate / block | **44.1 kHz**, **128-frame** blocks (2.9 ms) |
| Format | Interleaved **stereo** L/R, signed Q23 at the SDK boundary |
| Stereo | Real stereo out: SLOOP and Clipper already have stereo reverb, chorus and voice SPREAD. The stock DX7 engine is mono, which is where the "mono" reputation comes from. |
| USB audio | The FM-1 is a class-compliant USB audio interface (master out to the computer, 44.1 or 48 kHz) in SLOOP/Felucca. Useful for the resampler and for recording in Ableton. |
| Measured load | SLOOP dual-core build at idle: worst audio block 1,273 µs of 2,902 µs. |

## Controls and screen

| | |
|---|---|
| Screen | **240 × 240 colour LCD, RGB565**, DMA band transfers of 240 × 16 px. Colour, so a proper Massive-style look is possible. |
| Keys | **27 note keys** (F3–G5) with LEDs |
| Buttons | **14 panel buttons** with LEDs |
| Encoders | **7 encoders** (relative detents) plus **1 analogue knob** |
| MIDI | USB-MIDI in/out, BLE MIDI, TRS MIDI in on fitted boards (DIN in the official firmware) |
| Other | Battery level ADC, Bluetooth audio |

## Measured CPU (Carnac OS, on the device)

Carnac's PERF TEST, a wavetable synth with two filters, envelopes, LFOs and a mod matrix per voice:

| Scenario | CPU |
|---|---|
| 8 voices, one core | ~102 % (about 10 % per voice + 19 % idle) — too much for one core |
| 8 voices, both cores (odd voices on core 1) | **~38 %** |
| 8 voices, heavy patch (ring mod, noise, 3 mod slots, delay + reverb) | ~53 % |
| Torture test: two layers, 4-note chords at 1/32 with 4× ratchet at 167 BPM, plus arp | ~72 % average, 74 % peak |

So a rich wavetable voice costs about 5 % of the two-core budget.

## Storage budget (what this means for us)

| Item | Real limit we have to design around |
|---|---|
| User samples | Official firmware: 8 slots sharing **~19.5 s**. SLOOP: 4 user slots, **~15 s** total, ~7 s per slot, ADPCM. |
| Breaks | A 4-bar break at 170 BPM is ~5.6 s, so **2 full breaks** at once, or more if shorter/lower rate. |
| Wavetables | A full Serum table (256 frames × 2048 samples × 16-bit) is **1 MB — the whole flash**. Carnac OS's working format: 512-sample frames stored with 7 band-limited mip levels (2 KB per frame), 8 morph frames per bank = **16 KB per bank**, read straight from flash. 15 banks = ~229 KB. |
| Breaks | X0X fits about **nine one-bar loops** in the three user sample slots, and has a browser page and a CLI for uploading them. |

## DREADNOUGHT measured (0.11, from CI's size report)

CI prints these on every build (`tools/build_fm1.py`, `sizes()`; a notice on the run and `sizes.md` in the
artifact), and fails if the app slot has less than 24 KB free.

| | Used | Of | Free |
|---|---|---|---|
| App slot: code and constants (fixed in the package; the `.fwsc` is always 609,680 B) | 200,292 B | 581,564 B | **381,272 B** |
| RAM `.data` + `.bss` | 49,540 B | 98,304 B | 48,764 B |
| RAM pool | 152,896 B | 344,064 B | **191,168 B** |

The biggest users: the screen's canvas (pool, 69 KB), the two built-in breaks (38 KB each in the image), the
pattern bank (pool, 33 KB), the break voices (pool, 23 KB), the pattern buffer (pool, 15 KB), the fonts (26 KB).

**Flash map** (X0X's layout; `x0x-firmware/tools/fm1pkg_make.py`, `firmware/src/storage.c`):

| Range | What | Size |
|---|---|---|
| 0x00120 – 0x8E0DC | the app slot (rewritten by every update) | 568 KB |
| 0x97000 – 0x9F000 | store objects: sound, patterns, song / manifest, settings | 32 KB |
| 0xA0000 – 0xDC000 | the user sample slots | **240 KB** |
| 0xDC000 – 0xE0000 | calibration, motion (the 4th pattern object) | 16 KB |
| 0xE0000 – 0xFC000 | the update loader's staging area: never ours | 112 KB |
| 0xFC000 – | settings | |

What this means:
- **Code is not the limit.** 381 KB is free in the app slot: room for every planned engine and factory content
  (wavetables, factory samples, factory FM sounds) that ships with the firmware.
- **Project media gets 240 KB** (the sample slots), unchanged by updates.
- **RAM has room for the FX**: ~190 KB of pool free before any FX buffers.
- **CPU**: one core renders today (in the audio interrupt); the second core is idle. The on-device load is on the BRK2
  page; it has not been reported from hardware yet.

## Software bases

There are two kinds of firmware for this box, and they build differently.

### 1. M-VAVE FM1 Open (official, `m-vave/fm1_open`)

- App source is GPLv3 with an SDK linking exception, but the **audio engine is a closed binary**
  (`libfm1_engine.a`) behind a "sealed" audio API.
- Builds **only on Windows** with JieLi's Windows toolchain 2.5.3 (an `.exe` installer), with every
  toolchain file pinned by SHA-256 in `sdk/manifest.json`.
- Useful to us for: the clean SDK headers (`fm1.h`) as hardware documentation, the stock V15 recovery
  image (`sdk/firmware/FM-1_015.fwsc`, Apache-2.0, SHA-256 `db1642b2…edb8a`), and the Workbench's return-to-V15 path.

### 2. The Felucca family (Felucca → SLOOP → Clipper, all GPL-3.0-only)

- **Full source, no closed engine.** Own hardware layer, own update loader, own packager.
- Builds on **Linux x86-64** with JieLi's Linux toolchain (clang 4.0.1 for pi32v2) plus 3 files from
  the Apache-2.0 AC79 SDK. Perfect for GitHub Actions on `ubuntu-latest`.
- Has host-side tests, golden audio renders, a browser emulator (WebAssembly), a WebMIDI installer and
  a SysEx editor protocol.
- **SLOOP** (isod89, v2.8.1): a 4-track groovebox — 3 synth parts + drums, 64-step sequencer with
  p-locks, micro-timing, fills, chords, slide, ratchets; user samples; 27 punch-in FX; dual-core.
- **Clipper** (isod89, v1.1): one synth, but its **HYBRID** voice is already 3 oscillators (incl.
  7-saw supersaw, sync, fold, FM), 2 filters in series/parallel/split (incl. Moog ladder), 2 envelopes,
  tempo-synced LFOs, a 16-slot mod matrix and 4 macros — very close to the Massive voice we want.

## Update and flashing

- The update protocol is SysEx over USB-MIDI, so a **browser installer uses WebMIDI with SysEx**
  (Chrome/Edge). Step 1: the running firmware checks the package and stages its update loader.
  Step 2: the loader reads the whole image and writes it.
- An install that is cut off in step 2 **resumes**: the loader stays in update mode until it finishes.
- Felucca-family packages carry the identity `FM-1_9XY`; stock is `FM-1_0XX`.
- The installers refuse to write through another firmware's loader, because loaders may lay out the
  image differently.

## Build toolchain decision

CI builds on **`ubuntu-latest`** with JieLi's **Linux** toolchain, following the Felucca/SLOOP build.
Building FM1 Open itself would need a Windows runner plus a silent install of a vendor `.exe` with
pinned hashes; that is possible later but not needed for our chosen base (see FEASIBILITY.md).

## Sources

| What | Where |
|---|---|
| Screen, keys, buttons, encoders, MIDI, flash API, audio block | `m-vave/fm1_open` → `sdk/include/fm1.h`, `fm1_block.h`, `fm1_audio_io.h`, `fm1_device.h` |
| RAM/flash layout | `m-vave/fm1_open` → `sdk/config/linker.ld.in`; `isod89/sloop-fm1` → `RAM.md` |
| Toolchain, closed engine, Windows-only build | `m-vave/fm1_open` → `README.md`, `sdk/manifest.json`, `THIRD_PARTY.md` |
| SoC identity, PLL set-up, soft-float doubles, 1 MB single-bank flash | `AL-255/FM-1-RE` → `README.md`, `docs/architecture.md`, `TODO_aug2.md` |
| Second core, measured load | `isod89/sloop-fm1` → `DUALCORE.md`; `isod89/clipper-fm1` → `README.md` |
| Sample limits | `m-vave/fm1_open` → `README.md`; `isod89/sloop-fm1` → `README.md`; `hugelton/Felucca` → `firmware/src/slice_store.c` |
| Linux build, packaging, identities | `hugelton/Felucca` → `BUILDING.md`, `tools/build.py`; `isod89/sloop-fm1` → `BUILDING.md` |
| Update protocol | `hugelton/Felucca` and `isod89/sloop-fm1` → `web/fm1ota.js`, `web/fm1pkg.js` |
| 360 MHz clock, float vs integer, measured CPU, wavetable format | `OwenKirby/carnac-os` → `docs/dual-core-plan.md`, `docs/fm1-measure-checklist.md`, `src/dsp/wavetables.h`, `src/dsp/gen_wavetables.py` |
| Chip-level rescue, break upload | `charlesvestal/fm1-x0x` → `tools/fm1_rescue.py`, `README.md`; `OwenKirby/carnac-os` → `docs/fm1-recovery.md` |
