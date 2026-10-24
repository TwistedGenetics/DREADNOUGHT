# PROMPT: DREADNOUGHT towards 1.0: projects, six new engines, an FX rack

You are continuing DREADNOUGHT (https://github.com/TwistedGenetics/DREADNOUGHT), the dark jungle / DnB firmware for
the M-VAVE FM-1, now at 0.11:
- REESE with 4 LFOs;
- BREAKS A / B with chop mode;
- an 8-track sequencer: a page per instrument, 8 patterns each, Digitakt-style conditions, retrigs;
- MIDI 1–5;
- a web editor, a breaks page and a web flasher.

Read `README.md`, `CHANGELOG.md`, `docs/PLATFORM.md`, `docs/FEASIBILITY.md`, `docs/prompts/PROMPT-0.11.md` and the
code first. Where this prompt is out of date, the code wins.

House rules:
- Every change gets a host test that measures it (`tests/run_host.sh`, scenarios in `tests/host/scenarios`). Every
  engine also gets a golden render and a CPU figure.
- Screenshots of every changed page.
- Device UI: Massive-style dark. Web pages and documents: light only.
- No 64-bit divides on the device.
- CI green before a release. Push `main:x0x-platform` first, then main.
- Release one part at a time (VERSION bump, CHANGELOG, Release). After each engine, stop and let Jamie try it on the
  hardware before starting the next.
- Don't make manuals unless Jamie asks for one.

The user asked (his words):

> I want the following for this device:
> 1. wav table synth in which i can load serum converted wav tables with mangling ability so I can create complex sounds.
> 2. A synth like vhikk x.
> 3. a sub synth where I can also distort or manipulate to create Jungle bass lines (can you do an SH 101 synth?).
> 4. An fm synth that can take dx7 patches via the editor.
> 5. a sampler which can trigger one shots and also map on keys.
> 6. a juno style synth.
> 7. Effects like a quadraverb, my hollow fx, and fx from Carnac's synth.
>
> maybe the synths can be selected on a project base level, so some way of saving project (on or off the device to save
> space).

## The limits everything is designed around

From `docs/PLATFORM.md`:
- **CPU:** 360 MHz, two cores. One 128-frame block lasts 2.9 ms. Measured on Jamie's FM-1 (0.12): REESE and both
  breaks playing, **18 %** of one core (the second is idle).
- **RAM:** about 512 KB, with no external RAM.
- **Flash:** 1 MB, single bank. The firmware takes ~600 KB, which leaves **~300 KB for everything the user loads**:
  - samples and breaks;
  - wavetables;
  - DX7 banks;
  - patterns and sounds.

What follows from that:
1. **Every engine's code lives in the firmware.** Code is cheap; a synth engine is tens of KB. A project only decides
   which engines the tracks use, and which data is on the device.
2. **The data is what fills the device.** So the user data belongs to the project. Samples, wavetables and DX7 banks go
   on the FM-1 with the project, and come off it when another project is loaded. The computer holds the library of
   projects.
3. **CPU is shared.** All 8 tracks always play, but the polyphonic engines share a pool of voices. Steal voices with
   a short fade, never a click. Show the CPU load on the PROJECT page.
4. **RAM goes mostly to FX.** Reverb and delay buffers are the big users. The FX rack therefore has a fixed number of
   slots per project, with a RAM budget the UI shows. Not every effect can run at once.

## Part 0. Before anything: measure

1. **Firmware headroom.** The `.fwsc` is a fixed 609,680 bytes in both 0.10 and 0.11, so its size tells us nothing.
   - Get the real code and constant size from the linker map / ELF in CI: text, rodata and data, against the image's
     limit and the start of the user store.
   - Add a CI step that prints this, and fails if headroom drops below a set margin.
   - Write the numbers into `docs/PLATFORM.md`.
2. **RAM.** Measure the static use and the pool's high-water mark in the busiest current scenario.
3. **CPU.** Measure per block on both cores: REESE, two breaks and the LFOs at once. If DREADNOUGHT renders on one core
   today, move to the two-core layout Carnac OS uses (odd voices / second engine on core 1) as part of Part 1.

Report these to Jamie in plain words before Part 1, along with the revised per-engine budget below.

## Part 1 (0.12). Projects, an engine per track, the voice pool, the FX bus

**Projects.**
- A **project** holds:
  - a name;
  - the tempo;
  - which engine each of the 8 tracks uses;
  - each track's sound;
  - the LFOs;
  - all patterns;
  - the FX rack;
  - the project's data: samples, chops, wavetables and DX7 banks.
- The FM-1 holds **one working project**. SAVE keeps it, as today.
- **New on the device:** a PROJECT page (from GLO, after MIDI). It shows:
  - the name;
  - each track's engine (KNOB per track; changing one asks for confirmation and clears that track's sound, not its
    notes);
  - flash used by kind (samples, wavetables, DX7, patterns) against what's free;
  - RAM for the FX;
  - the CPU load.
- **In the editor**, a Projects tab:
  - **Save to computer**: a single `.dnproj` file (chunked binary: header, engine map, sounds, patterns, FX, then each
    data blob). Use the File System Access API to a chosen folder where available, otherwise a download.
  - **Load to FM-1**: only while stopped. Writing flash silences audio. Show progress, and make it resumable like the
    flasher.
  - **New project**: from a template.
  - **Library**: the projects in the folder, with their sizes and engines.
- **A MEDIA tab per project.** Everything the project loads goes on the FM-1 from here, never from separate pages:
  - samples (for SAMPLER and BREAKS);
  - DX7 .syx files;
  - wavetables;
  - SysEx banks.

  The tab shows:
  - a list of the project's media, each with its size and which tracks use it;
  - one bar for the project's flash, filling as items are added, red when it won't fit;
  - nothing written until **Send to FM-1**.

  The breaks page's upload code moves here; the breaks page stays as a shortcut for loops.
- **Sample conversion in the editor.** Jamie asked (his words):

  > maybe we could mono and downsample the samples to fit more on?

  Yes. Every sample is converted in the browser before it's sent:
  - **Mono.** Stereo is folded to mono by default: sum, left, right or mid. The FM-1 plays mono samples anyway; stereo
    width comes back from the FX.
  - **Rate:** 44.1, 32, 26.04 (the SP-1200's), 22.05, 16, 11.025 or 8 kHz. Resample with a proper anti-aliasing
    filter, and **preview each choice in the browser before sending**.
  - **Format:**
    - 16-bit;
    - **12-bit** (the SP-1200 / MPC60 grit);
    - 8-bit;
    - 4-bit ADPCM (what the breaks use today: a quarter of 16-bit).

    The engines decode each format, and the formats are tagged per sample.
  - **Trim and fades.** Trim silence, normalise, and a 2 ms fade at the ends.
  - **Fit to space.** One button picks the best rate and format per sample so the set fits the space left. Drums lose
    the least and long pads the most; Jamie can override any of it.
  - **Cost and duration.** Each sample's cost in KB and seconds, live as the settings change.

  For scale, with about 250 KB free for samples:

  | Format | Seconds that fit |
  |---|---|
  | 44.1 kHz 16-bit stereo | 1.4 s |
  | 44.1 kHz 16-bit mono | 2.9 s |
  | 22 kHz 16-bit mono | 5.8 s |
  | 26 kHz 12-bit mono (SP-1200) | 6.6 s |
  | 22 kHz ADPCM mono (today's breaks) | 23 s |
  | 16 kHz ADPCM mono | 32 s |
  | 11 kHz ADPCM mono | 46 s |

  Recompute the table from Part 0's real free space and put it in the README.
- **The originals stay on the computer.** The `.dnproj` file keeps the original WAVs alongside the converted ones, so
  conversion choices can be changed later without losing quality. The editor checks the size and offers to leave the
  originals out.
- **DX7 files.** Drag .syx files onto MEDIA:
  - a 32-voice bank (4,104 bytes);
  - a single voice (163 bytes);
  - a folder of them.

  The editor:
  - lists the voice names, with an audition from a JavaScript port of the FM engine (render in the browser, so Jamie
    hears a voice before sending it);
  - lets Jamie tick which voices go into the project (each costs 128 bytes);
  - rejects files that aren't DX7 voice data, with the reason.
- Today's state migrates into "PROJECT 1" with REESE / BRK A / BRK B / MIDI 1–5.
- If the measurements leave room, offer a second on-device slot for a quick A/B switch. Don't promise it before
  measuring.

**Engines per track.**
- Extend the instrument table from 0.11 so every row is `{engine, track}`. A new engine is one table row plus its
  pages.
- Engines:
  - REESE;
  - BREAKS (sliced loop);
  - SUB (the 101, Part 2);
  - SAMPLER;
  - FM;
  - JUNO;
  - WAVE;
  - TEXTURE (the Vhikk-style voice);
  - MIDI.
- Any engine may go on any track. Mono engines may appear more than once if the CPU allows it; the PROJECT page says
  when it doesn't.
- The sequencer's page follows the engine:
  - ROLL for the pitched engines;
  - DRUM for BREAKS;
  - for the SAMPLER, DRUM in one-shot mode and ROLL in keys mode.

**Voice pool.**
- 8–10 poly voices shared by FM, JUNO and WAVE. Exact numbers come from Part 0's measurements.
- Each poly track sets a voice limit.
- Steal the oldest released voice first, then the oldest held, with a 2–5 ms fade.

**FX bus.**
- Each track has two sends (A, B) and a dry level.
- The FX rack has 3 slots per project: send A, send B, and a master insert (drive / EQ / limiter).
- The slots' RAM comes from one arena sized at project load. A slot whose effect doesn't fit is refused, with the
  reason on screen.
- Part 3 fills the rack. In 0.12 ship one cheap delay so the bus is tested end to end.

## Part 2 (0.13). SUB: an SH-101-style mono bass for jungle

- **Oscillator.** One VCO with saw, pulse (with PWM from the LFO or the envelope) and square-sub (−1 oct, −2 oct, −2 oct
  25 %), plus noise, all on a 101-style mixer. Use band-limited oscillators (polyBLEP is fine).
- **Filter.** 24 dB IR3109-style lowpass with resonance up to self-oscillation, keyboard tracking, envelope amount and
  LFO amount.
- **Envelope and amp.** One ADSR (the 101 shares it: VCA on ENV or GATE), plus 101 portamento: OFF / AUTO (legato
  only) / ON.
- **The jungle part, after the VCA, in order:**
  1. DRIVE with 4 types: soft, hard, fold, rectify.
  2. CRUSH (bits and rate).
  3. A post-filter for taming.
  4. A clean-sub split, so the low end stays clean under the distortion.
- **Controls.**
  - Velocity and accent from the sequencer: accent opens the filter and lifts the level, as the 101's accent does.
  - Every knob can be a destination of the 4 LFOs.
- **Factory sounds.** About 12: dread, donk, wobble, rolling sub, hoover-ish, acid, 808-style sine sub with drive, and
  others.
- **Tests.**
  - Spectral checks of the waves and the sub.
  - Self-oscillation pitch tracking within a few cents over 3 octaves.
  - The clean-sub split keeps the fundamental intact under hard drive.

## Part 3 (0.14). Effects

1. **Carnac OS's FX** (Carnac OS is GPL-3.0, from the same family; port the code and credit it in `docs/UPSTREAM.md`):
   - reverbs: SPRING, TOMB, MOD, CELESTIAL, DOLMEN, TUMULUS;
   - delays: TAPE and BBD, with DEGRADE (wow, flutter, filtering) and PING PONG.

   Check every reverb against its RAM figure here and run its internal rate lower if needed.
2. **Quadraverb-style.** An Alesis Quadraverb-flavoured multi-effect with the same configurations idea:
   - chains of EQ → pitch (detune / chorus / flange) → delay → reverb;
   - a few configurations (EQ-CHORUS-DELAY-REVERB, EQ-REVERB, 5-BAND EQ-REVERB, and others);
   - the 1989 character: band-limited around 20 kHz and slightly grainy. Offer a "16-BIT" switch that lowers the rate
     and word length on purpose.

   It is one slot's worth of RAM, so its reverb and delay are smaller than Carnac's.
3. **Jamie's "hollow" FX = his Super-Massive** (his Valhalla Supermassive-style reverb / delay for the microKORG 2,
   logue SDK, repo TwistedGenetics/Super-Massive). Port its algorithms from his code (his own, so no licence
   question), fitted to the FX arena; measure its RAM and CPU like the others.

Each FX gets:
- a golden render;
- a RAM figure;
- a CPU figure;
- a test that it decays to silence (no endless whine, no buzz with several voices in, as Carnac found);
- send limiting before the reverbs, as Carnac does.

## Part 4 (0.15). SAMPLER: one-shots and key-mapped samples

- **Shared sample store.** BREAKS and SAMPLER use the same project sample store, so the PROJECT page shows one sample
  budget. Breaks stay as they are.
- **One-shot mode.** Up to 16 samples on the 16 steps / white keys, played in DRUM. Each sample has:
  - start and end;
  - pitch and fine;
  - level and pan;
  - AHD envelope;
  - reverse;
  - choke group;
  - filter;
  - drive / crush (the same blocks as SUB);
  - the FX sends.
- **Keys mode.** A key map: a sample per zone with its root note and range (several zones make a multisample). Played
  chromatically in ROLL, with up to 4 voices from the pool. Loop points and an optional sustain loop.
- **Editor.**
  - Samples arrive through the MEDIA tab with the conversions from Part 1.
  - Also: set the root from the file name or by pitch detection; build a key map by dragging zones onto a keyboard; and
    set one-shot pads by dropping samples on 16 squares.
- **Tests.** The pitch across the map, zone edges, loop clicks (none), and choke.

## Part 5 (0.16). FM with DX7 patches

- **Engine.** 6 operators, the 32 DX7 algorithms, operator feedback, and DX7-style envelopes (4 rates, 4 levels),
  keyboard level / rate scaling, velocity sensitivity, LFO (with PMD / AMD and delay), pitch EG and transpose.
  - Dexed's engine core (MSFA, Apache-2.0, which is compatible with GPL-3.0) is a good reference for the envelope and
    the scaling curves. Credit it in `docs/UPSTREAM.md`.
  - Run 4–6 voices from the pool. SLOOP caps its FM6 at 6 when alone; measure ours.
- **Patches.**
  - The editor reads DX7 SysEx through the MEDIA tab (Part 1): 32-voice banks (`F0 43 00 09 20 00`, 4,104 bytes,
    packed 128 bytes a voice) and single voices (155-byte unpacked).
  - It shows the bank's names, auditions them in the browser, lets Jamie pick which voices go to the project, and sends
    them.
  - Store the packed voice (128 bytes), so a whole bank costs 4 KB.
- **On the device.**
  - PRESETS browses the project's voices.
  - Four macro pages: ALGORITHM / FEEDBACK / a modulator-level macro ("BRIGHTNESS") / envelope-time macro; then OP 1–6
    pages for ratio, level and envelope.
  - The FX sends and the LFOs work as on the other engines.
- **No ROM banks.** Don't ship Yamaha's factory banks in the firmware. Write a few original FM sounds (bells, stabs, FM
  bass, a "Reese-ish" detuned FM) as factory sounds instead; Jamie loads his own banks.
- **Tests.**
  - Load a known bank and compare a few voices' renders (pitch, envelope timing, spectral centroid) with Dexed's output,
    rendered offline in CI if practical.
  - The parser must round-trip every field of the 128-byte voice.

## Part 6 (0.17). JUNO

- **Voice.** A Juno-106-style voice: DCO with saw and pulse (PWM manual or from the LFO), a sub (square −1 oct) and
  noise; a non-resonant HPF (4 steps); the IR3109 lowpass (shared with SUB); one ADSR to the VCF and the VCA (or GATE).
- **Chorus.** The **stereo chorus I / II / I+II**, BBD-style, which is the sound. It is a per-track insert, cheap in RAM.
- **Polyphony.** Up to 6 voices from the pool. Optional unison with detune, so it can do the Hoover lead too.
- **Factory sounds.** About 12: pads, stabs, the rave hoover, strings, organ-ish, bass.
- **Tests.** The chorus's modulation depth and rates against the published Juno figures; and PWM, HPF steps and
  polyphony stealing.

## Part 7 (0.18). WAVE: wavetables from Serum, with mangling

- **The format problem.** A Serum table can be up to 256 frames × 2048 samples, 1 MB, the whole flash.
- **The editor converts.**
  - Read the WAV and its `clm ` chunk (Serum's frame size, usually 2048).
  - Let Jamie pick how many frames to keep (8, 16, 32 or 64), with resampled morphing between kept frames.
  - Resample each frame to 512 samples.
  - Build the band-limited mip levels (Carnac OS's format: `docs/wavetables.md`, `gen_wavetables.py`).
  - Show the size before sending. About 2 KB a frame with mips, so a 64-frame table is about 128 KB. Say so plainly.
  - A project can hold a few tables; each one's cost appears on the PROJECT page.
  - Also ship a handful of factory tables in the firmware if Part 0 says there's room.
- **Engine.**
  - 2 wavetable oscillators plus a sub and noise, each with POSITION (modulatable, smooth between frames), unison up to
    7 with detune and spread, and phase randomisation.
  - Filters from the existing engines (ladder, IR3109, a notch / comb).
  - 2 envelopes plus the 4 LFOs, with POSITION as a destination.
- **Mangling.** Serum-style warp per oscillator, two at once:
  - SYNC;
  - BEND + / −;
  - PWM;
  - ASYM;
  - MIRROR;
  - FLIP;
  - QUANTISE;
  - FM from B;
  - AM / RM from B;
  - WAVEFOLD;
  - REMAP (a curve drawn in the editor, stored with the sound).
- **Editor mangles.** In the editor, offline mangles create new tables: spectral morph between two tables, formant
  shift, harmonic filter, "crush frames", and normalise and phase-align all frames.
- **Tests.** Aliasing at high notes (the mip choice), smooth POSITION sweeps (no zipper), each warp's shape against its
  maths, and the converter's output against a Serum table's frames.

## Part 8 (0.19). TEXTURE: a Vhikk X-style voice

The Forge TME VHIKK X is a stereo Eurorack voice with 36 algorithms in 4 banks. They cover oscillator banks,
atonal / textural synthesis and effects processing, and are aimed at drones, basses, textures and sound effects. Don't
clone it; make an engine in its spirit.
- Stereo throughout.
- One ALGORITHM knob over a set of about 8 algorithms:
  - detuned oscillator bank / cluster;
  - feedback FM pair;
  - resonator bank excited by noise;
  - Karplus string;
  - grain cloud from a project sample;
  - wavefold drone;
  - ring-mod clang;
  - formant voice.
- Every algorithm shares the same 4 macro knobs (TIMBRE, COLOUR, MOTION, SPACE), as a module would expose them, plus a
  built-in stereo effect per algorithm (shimmer, smear, freeze).
- Jamie's answer (2026-10-24): "all of them, if you can make them i want something that creates dark sounds like it
  does". So: as many of its algorithm families as fit (research the module's manual and demos first), each tuned
  for dark drones, basses and textures. It can't be a copy of a module's firmware: an engine of our own in its spirit.

## Open questions: ask Jamie before the part that needs them

1. ~~"My hollow FX"~~: answered, his Super-Massive (Part 3).
2. ~~Vhikk X~~: answered, all of its kinds of sound, dark (Part 8).
3. **Engine order.** The order above puts jungle bass first. He may want to reorder.
4. **Second on-device project slot vs more sample space.** Decide after Part 0's numbers.

## Deliverables per part

- Code with host tests, golden renders, CPU and RAM figures.
- Screenshots of every new page.
- README control tables and CHANGELOG entry.
- Editor support with its tests (`node --test`, the e2e demo).
- A Release.
- A short note to Jamie: what to try on the hardware, and what the numbers came out at.
