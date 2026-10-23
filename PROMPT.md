# PROMPT: "DREADNOUGHT" — a dark jungle / DnB firmware for the M-VAVE FM-1

You are a senior embedded DSP engineer and a jungle / dark drum & bass producer. We are building a complete alternate firmware for the M-VAVE FM-1 that turns it into a self-contained dark jungle box: Reese and dread basslines, wavetable leads and pads, a breakbeat chopping sampler, and a brutal FX section, all sequenced from the box itself.

Name: **DREADNOUGHT**. Repo: https://github.com/TwistedGenetics/DREADNOUGHT — all code, CI, docs, releases and the web editor (GitHub Pages) live here.

---

## 0. Ground truth first — do NOT skip

Before writing any DSP, investigate and write `docs/PLATFORM.md` covering what you actually find, not what you assume:

1. Clone and read the official source + SDK: `github.com/m-vave/fm1_open` (latest release). Note the licence: app code is GPLv3 with the FM1 SDK linking exception; SDK and third-party parts have their own terms (see THIRD_PARTY.md). Our repo is GPLv3 and must respect that.
2. Also study the community firmwares as reference architectures: `hugelton/Felucca` (src/ app, hal/ hardware layer, loader/), `isod89/sloop-fm1`, `isod89/clipper-fm1`, `charlesvestal/fm1-x0x`. Learn how they handle audio callback, voice allocation, UI, flash storage, sample upload and packaging (`.fwsc`).
3. Also read `AL-255/FM-1-RE` for the reverse-engineered hardware notes.
4. Document precisely:
   - SoC: reported as JieLi AC791N / WL82 with a pi32v2 core. Confirm clock speed, FPU availability (float vs fixed-point), DSP/SIMD instructions, RAM (internal SRAM vs PSRAM), flash size, XIP layout.
   - Audio path: sample rate, block size, bit depth, codec, mono vs stereo (stock engine + FX are mono into a stereo TRS — find out if true stereo is possible).
   - Every physical control: keys, encoders, buttons, screen resolution, LEDs, USB-MIDI, any trigger/sync I/O.
   - How much CPU the stock 12-voice DX7 engine uses per voice — this is our benchmark for what the chip can do.
   - The build toolchain (JieLi toolchain — community uses WSL on Windows). **Build everything in GitHub Actions**, not on my machine: a Linux runner installs the toolchain (cache it), builds on every push and PR, runs the host-side DSP tests, renders the audio previews, and attaches the `.fwsc` package + preview WAVs as workflow artifacts. Tagging `v*` publishes a GitHub Release. I'm on an Apple Silicon Mac, so the CI build is the source of truth; a Docker setup for local builds is a nice-to-have, not a requirement.
   - Flashing and recovery: the official Workbench at open.m-vave.com/fm1-workbench, Felucca's installer, M-VAVE's "return to V15", and the FM-1-transporter recovery dongle. Recovery is NOT guaranteed by the update protocol alone — write `docs/RECOVERY.md` before we ever flash.

**Then stop and give me a feasibility report**: realistic CPU and RAM budget, which of the features below fit, which need cutting down, and your proposed phase plan. Don't start Phase 1 until I say go.

---

## 1. The vision

One box, six tracks, dark as hell. Think Photek, Source Direct, Dillinja, Ed Rush & Optical, Konflict, Bad Company, Current Value, early Metalheadz, mixed with modern dread bass (Kanine, Serum-style donk / dread wobble).

Every engine is voiced for 160–175 BPM, sub-heavy, mono-compatible low end, and aggressive mids.

### Track layout (default project)
| Track | Engine | Role |
|---|---|---|
| 1 | BREAKS | Chopped Amen/Think/Apache-style breaks (user samples) |
| 2 | KIT | One-shot drum kit (kick, snare, hats, ride, rim) |
| 3 | REESE | Main bass |
| 4 | DREAD | Second bass / wobble / donk |
| 5 | WAVE | Wavetable lead / pad / stab |
| 6 | FM | Keep the DX7 engine — dark FM bells, Rhodes, stabs |
| 7 | 808 | Distorted 808 sub bass with slides |
| 8 | PAD / STAB | Dark pads and tech stabs (chord memory) |

Any track can be switched to any engine if the CPU allows. If 8 tracks don't fit the CPU, keep 8 tracks of sequencing but limit how many heavy engines play at once and tell me the limit.

---

## 2. Engines

### 2.1 REESE (the flagship)
- 2–7 detuned saws (supersaw-style), band-limited (PolyBLEP or minBLEP — no aliasing grit unless asked for).
- DETUNE with a nonlinear curve (fine at the bottom, wide at the top), plus "drift" (slow random per-osc pitch wander).
- Phase-locked mode vs free-running, with retrigger option.
- Sub oscillator (sine/triangle) with independent level, octave and a lowpass so the sub stays mono and clean below ~120 Hz; detuned layers are high-passed and can be stereo-spread if the hardware supports stereo.
- Filter: 24 dB ladder (Moog-style, zero-delay-feedback) with drive, plus a notch / comb option for the classic moving-hollow Reese movement.
- "Movement" macro: one knob that sweeps detune + filter + comb together.
- Distortion: tanh, fold, and a "Neuro" multiband-ish mode (split low/high, distort only high).
- Hoover mode: PWM saws + pitch-dive envelope (Alpha Juno "What The" style).

### 2.2 DREAD (dread / donk / wobble bass)
- Morphing oscillator: sine → square → saw → formant ("yoi"/"vowel" talk).
- Tempo-synced wobble LFO (1/1 to 1/32, triplets, dotted) on filter cutoff, formant and wavetable position.
- LFO shape morph and "step LFO" (per-step LFO rate changes from the sequencer = rolling half-time/double-time wobbles).
- FM-into-filter "donk" mode: short pitch envelope + resonant bandpass ping.
- Pitch glide with legato, and per-step slide.
- Built-in "sub follower" so the sine always sits under the growl.

### 2.3 WAVE (wavetable)
- 8–16 built-in wavetables designed for dark music: Reese table, vocal formants, digital "PPG" metallic, Amen-derived tables (single-cycle slices of a break), noise/grit tables, analog-shape morph.
- Smooth position morph (interpolate between frames), band-limited via pre-computed mip-maps.
- Warp modes: sync, bend, mirror, PWM, FM-from-B, bitcrush-in-table.
- Unison 1–4 voices if CPU allows; paraphonic chords for pads.
- Use any sample on the device as a wavetable source ("Resynth" button).

#### Serum wavetable import (must-have)
I need to load wavetables made for / exported from Xfer Serum.
- Parse Serum-format WAVs: concatenated single-cycle frames (normally 2048 samples per frame, up to 256 frames), 16/24/32-bit int and 32-bit float, mono or stereo (fold to mono). Read the `clm ` chunk Serum writes (e.g. `<!>2048 ...`) to get the frame size; if it's missing, fall back to 2048, and let me override the frame size manually (256 / 512 / 1024 / 2048 / 4096).
- Also accept single-cycle folders (AKWF-style), and Vital/other WAV wavetables when the frame size is known.
- Conversion happens **off-device** in two places that share one codebase:
  1. `tools/wt_convert.py` CLI (and a CI job that converts anything dropped in `wavetables/` in the repo).
  2. The web editor: drag a Serum `.wav` in, see all frames in a 3D Massive/Serum-style waterfall, then send it to the FM-1.
- Conversion steps: normalise, remove DC, zero-cross-align each frame, resample frames to the device frame size chosen in Phase 0 (likely 256–1024 samples to save RAM), optionally reduce frame count (keep every Nth frame or smart-pick by spectral difference), pre-compute band-limited mip-maps, then pack into a versioned DREADNOUGHT wavetable format with name and tags.
- Report the memory cost of each table before upload and how many tables fit on the device.
- Ship the factory tables as source WAVs in Serum format in the repo too, so they work in Serum and on the box.

### 2.4 FM
- Keep the existing Dexed/msfa 6-op engine, but add a curated "dark" bank: bells, Rhodes, Metalheadz-style stabs, Reese-ish FM basses, gongs. Add one-knob macros: brightness (overall mod index), metal (ratio detune), decay.

### 2.5 BREAKS (Rex / ReCycle-style chopper — the heart of jungle)
- Load a break (WAV via the uploader). Auto-slice by:
  - transient detection (adjustable sensitivity),
  - grid (8 / 16 / 32 equal slices),
  - manual slice markers set on-device.
- Tempo-follow: slices play at project tempo without pitch change by default (each slice triggered on time, Rex style) — and optionally pitched (old-school timestretch-free speed-up, like a 90s sampler).
- Each sequencer step can choose: slice number, reverse, pitch (±24 st), gate length, roll / ratchet (2, 3, 4, 6, 8), volume, decay, filter.
- "Jungle Mangler" generative tools:
  - RANDOM CHOP — re-sequences slices using musical rules (keep the kick on 1, keep snares on 2 and 4, vary the ghost notes).
  - EDIT FILL — on hold, generates a 1-bar fill with rolls, reverses and pitch-downs.
  - STUTTER — hold a key to repeat the current slice at 1/8 → 1/64.
  - TIMESTRETCH ARTIFACT mode — granular stretch with audible grains (that 1994 Akai "zippy" sound).
  - Half-time / double-time switch for the whole break.
- Per-slice choke groups, and a resampling bus: record the output of the break track back into a new sample.

### 2.7 808 (distorted sub bass)
- Sine (with optional triangle / slight-saw blend) core with a pitch envelope (amount + time) for the punch, and a selectable click/transient layer (synth click or a user sample).
- **Glide and per-step slide**, including slides between overlapping notes in legato mode (the classic 808 slide), with slide time and curve.
- Long decay with key tracking; mono, last-note priority; option to choke from the KIT kick.
- **Distortion section — the point of this engine**:
  - Modes: Tube (asymmetric soft clip), Tape (saturation + slight compression), Hard clip, Foldback, Rectify, Bitcrush, and a "Destroy" mode (fold + clip + rectify stacked).
  - Drive, Tone (post-distortion tilt/LP), and **Mix** for parallel distortion.
  - **Clean sub keep**: split below a set frequency (~80–150 Hz), distort only the top, sum back so the sub stays solid on big systems.
  - Optional 2× oversampling for the distortion if CPU allows; otherwise use ADAA (antiderivative anti-aliasing).
- Post EQ: low-shelf boost and a mid "growl" bell.
- Sidechain duck from the KIT kick (amount + release).
- Massive-style macros: Drive, Tone, Punch, Slide, Decay, Sub, Grit, Mix.

### 2.8 PAD (stubs to build out)
Dark, wide, slow-moving beds for intros and rollers:
- **Atmos pad** — 2–3 detuned wavetable osc + slow filter drift + long envelope. Paraphonic/poly as CPU allows.
- **String machine** — divide-down style saw/square ensemble with a triple-chorus "ensemble" effect (Solina-style, original implementation).
- **Choir / vocal pad** — formant filter bank sweeping vowels (shares DSP with DREAD's formant mode).
- **Granular pad** — freeze-and-scatter grains from any sample or resampled audio (shares DSP with the timestretch-artifact mode in BREAKS).
- **Reese pad** — slow, wide, low-cut REESE voice for tension under breaks.
- **FM glass pad** — soft FM bell layer from the FM engine.
- All pads get: long attack/release, pad-friendly LFOs, built-in ensemble/chorus, send to the big reverb by default.

### 2.9 STAB (tech stabs — stubs to build out)
For techstep / Metalheadz / rave stabs:
- **Chord memory**: hold a chord, save it to the track, then play it from one key, transposed. Ship dark chord sets (minor 9, minor 11, sus, stacked fifths, Detroit-style voicings).
- **Dub chord stab** — filtered saw chord, short decay, straight into the dub delay (dub techno / Basic Channel-inspired).
- **Techstep stab** — distorted, detuned, bandpassed bass/chord stab with a fast filter env.
- **Hoover stab** — short pitch-dive hoover.
- **Rave organ stab** — organ-style stacked sines/square, short decay.
- **Rave piano / FM stab** — from the FM engine.
- **Metallic FM stab** — inharmonic ratios, short decay.
- **Orch hit** — sample slot for the user's own hit (we ship none).
- Per-stab: filter env, accent, per-step chord inversion, and a "spread" that strums the chord.

### 2.10 Other stubs I suggest
- **FX generator** — risers, downlifters, white-noise sweeps, sub drops and impacts, all tempo-synced and generated (no samples needed).
- **Vocal / one-shot track** — MC/vox chops with slice + pitch, shares the BREAKS slicer.
- **Bell / pluck** — quick Karplus-Strong pluck for dark melodic hooks.

### 2.11 KIT
- 8 one-shot slots, sample-based, with pitch, decay, filter, drive. Ships with CC0/self-made dark jungle kit (no copyrighted breaks or samples bundled — user loads their own).

---

## 3. FX — "crazy FX sampler"

### Per-track insert (choose one)
- Drive / bitcrush / sample-rate reduction (SP-1200 12-bit, MPC60, S950, Amiga 8-bit — reuse ideas from my EraCrush microKORG 2 unit if helpful).
- Comb / resonator (tuned to note).
- Ring mod, frequency shifter.
- Formant filter.

### Send FX
- **Dub delay**: tempo-synced, tape wobble, filter in the feedback loop, freeze, ping-pong if stereo.
- **Big dark reverb**: lush, long-tail, modulated FDN (think Valhalla Supermassive / my Super-Massive unit), with "shimmer down" (octave-down pitch in the tail) and freeze.
- **Space echo / spring**.

### Master / performance FX (Kaoss-style, played live on the keys)
- Beat repeat / stutter (synced), tape stop, tape start, reverse buffer, vinyl brake, scratch, filter sweep (DJ-style LP↔HP one knob), gater/trance-gate, pitch-down dive, "Bass drop" sub-boom.
- Holds while key pressed, releases on lift.
- Master bus: glue compressor, sub-mono below 120 Hz, soft clipper / limiter so it can be loud without ugly digital clipping.

### Resampler
- Record any track, any bus or the master into a new sample, then chop it in BREAKS or use as a wavetable. This is the "crazy FX sampler" loop: mangle → resample → chop → mangle again.

---

## 4. Sequencer & performance
- 6–8 tracks, up to 64 steps per pattern, 16 patterns, song chaining.
- Per-step parameter locks (Elektron style) for every engine parameter.
- Probability, conditional trigs (1:2, 1:4, fill, not-fill), micro-timing (swing per track), ratchets.
- Per-track length (polymeters) and per-track speed (half-time bass under a full-time break).
- Scale lock with dark defaults: natural minor, Phrygian, harmonic minor, plus "tritone" spice.
- Live record, step record, and an arpeggiator for stabs.
- Mute / solo / scene snapshot (one press to drop the break and leave the Reese).
- MIDI clock in/out, MIDI note/CC in for every track, so it can be played from Ableton too.

---

## 5. UI — styled after NI Massive

The whole interface should feel like Native Instruments Massive: the same workflow, layout logic and look, adapted to the FM-1. Make it an original design *inspired by* Massive — no NI logos, names, fonts or copied artwork — but anyone who knows Massive should feel at home instantly.

### 5.1 Synth architecture to mirror Massive's workflow
Restructure the synth engines (REESE, DREAD, WAVE) around a shared Massive-style voice so they all share one familiar layout:
- **3 wavetable oscillators** (OSC 1–3), each with Pitch, Wt-position, Intensity, Amp, and a warp mode selector (Spectrum, Bend, Formant, PWM, Sync, Bitcrush…).
- **Modulation oscillator** (ring mod / phase / position / filter FM onto any osc).
- **Noise** and **Feedback** sources.
- **2 filters** with a Serial ↔ Parallel blend (F1/F2 mix), each with Cutoff, Resonance, type (Lowpass 4, Bandpass, Comb, Daft, Scream, Acid…-style originals).
- **4 envelopes** + **4 modulators** that can each be an LFO or a **Stepper/Performer** (tempo-synced step shapes — this is where the wobbles live).
- **8 Macro knobs** per preset — the main performance controls.
- **Insert FX 1 & 2** in the voice, **Master FX 1–3**, and an EQ page.
- **Routing page** to re-order osc → insert → filter → insert flow.
- REESE, DREAD, WAVE then become *preset families* and specialised oscillator/warp modes on this shared engine, not separate UIs. If CPU forces it, cut the voice count before cutting this structure.

### 5.2 On-device screen
- Find the real screen size/colour depth in Phase 0 and design for it.
- **Tabbed sections** across the top like Massive: OSC | MOD | FILT | ENV | LFO | FX | EQ | ROUTE | MACRO. A button cycles tabs; encoders control the 4 params on the current tab.
- **Massive-style knobs**: round knob with a value arc, and a second **modulation ring** around it showing how much and which source modulates it (source number shown in a small slot under the knob, as in Massive).
- **Drag-to-modulate equivalent**: hold a source (Env 1, LFO 2, Macro 3…) + turn any knob = assign and set depth. Shift + turn = edit depth of existing mod.
- Live mini-displays: wavetable shape with position marker, filter curve, envelope shape, stepper pattern — redraw at UI rate, never in the audio thread.
- Colour language (if colour screen): dark charcoal panels, light grey labels, cool blue accents for modulation, one highlight colour per section. On a monochrome screen use inverted headers and outlined knobs to get the same feel.
- BREAKS and KIT keep their own pages but use the same tab/knob style; BREAKS shows a waveform with slice markers.
- Preset browser styled like Massive's: filter by tags (Bass / Reese / Dread / 808 / Hoover / Lead / Pad / Stab / Break / FX) and characteristics (Dark, Dirty, Wide, Moving, Sub).

### 5.3 Companion web editor (big-screen Massive)
The FM-1 screen is tiny, so also build a **browser editor** (WebMIDI/SysEx, Chrome/Edge, hosted on GitHub Pages from the same repo, deployed by the CI):
- Full one-window Massive-style panel: three osc strips across the top, filters and FX in the middle, envelopes/LFO tabs and the macro row at the bottom, exactly the dense, everything-visible layout Massive is loved for.
- Real drag-and-drop modulation: drag a source's cross-hair onto any knob to assign it, with blue mod rings.
- Live two-way sync with the hardware, preset load/save, sample and wavetable upload, and a WebAudio preview that runs the same DSP compiled to WebAssembly so I can design sounds without the box plugged in.

### 5.4 Web firmware flasher (must-have)
A one-click browser installer like Felucca, SLOOP and Clipper have, hosted on GitHub Pages next to the editor:
- Works in Chrome/Edge with the FM-1 on USB. Find out in Phase 0 exactly which transport the existing installers and M-VAVE's Workbench use (WebMIDI SysEx, WebUSB or WebHID) and the update protocol, by reading their source. Reuse proven code where the licence allows, with credit.
- Flow: detect the FM-1 and show its current firmware → **force a backup step first** (projects, voices, samples, wavetables) → pick a release (latest stable, beta, or a CI build artifact) → verify checksum/CRC → flash with a progress bar → verify → reboot.
- Release list pulled automatically from GitHub Releases, so tagging a version in CI makes it installable.
- A "Return to official firmware" button pointing to M-VAVE's V15 / FM1 Open recovery route, plus clear links to `docs/RECOVERY.md`.
- Refuse to flash if the battery/USB looks unstable, the wrong device is connected or the file fails verification. Never let a browser tab close mid-flash without warning.
- Also allow dropping a local `.fwsc` file for testing.

### 5.5 Content
- Ship at least 64 presets and 8 demo patterns of dark jungle and DnB, each with 8 musically assigned macros (e.g. Reese: Movement, Detune, Drive, Cutoff, Comb, Sub, Space, Width).

---

## 6. Engineering rules
1. **CPU budget is law.** Profile every engine on hardware early. Target max 75% CPU at worst case so the UI never stutters. If something doesn't fit, degrade gracefully (fewer unison voices, lower-rate modulation, voice stealing), never click or drop out.
2. Pick float vs fixed-point based on what Phase 0 finds about the FPU. Write hot loops for that reality.
3. Pre-compute: wavetable mip-maps, tanh/fold tables, filter coefficient tables. Block-rate modulation where audio-rate isn't audible.
4. No allocations, no locks in the audio callback.
5. Every DSP module gets a host-side unit test (compile the same C/C++ on macOS, render WAVs, check for NaN, DC, clipping, aliasing). Add a `tools/render.py` that renders presets to WAV so I can audition on my Mac before flashing.
6. Build a desktop simulator (or use an existing FM-1 browser emulator if one fits) so I can play the UI without the hardware.
7. Keep the HAL separate from the DSP so engines can be ported to my other logue SDK work (microKORG 2, NTS-1 mkII) later.
8. Keep flash/storage formats versioned with migration code; never corrupt user projects.
9. Credit and licence everything properly (Felucca, SLOOP, msfa/Dexed, M-VAVE SDK).
10. Build a real `.fwsc` package and installer flow as the other firmwares do. Always tell me to back up projects, voices and samples first.

---

## 7. Phases (adjust after Phase 0 report)
- **Phase 0** — Platform investigation, GitHub Actions build of an unmodified fm1_open producing a downloadable `.fwsc`, first version of the web flasher on GitHub Pages, flash it to prove the loop. Recovery doc. Feasibility report, including screen specs and a UI mock of the Massive-style pages.
- **Phase 1** — Audio skeleton: one REESE voice + sub + ladder filter, playable from keys. Measure CPU.
- **Phase 2** — REESE complete + DREAD + master limiter. Basic 4-track sequencer.
- **Phase 3** — BREAKS engine: load, slice, sequence, rolls, reverse, pitch. Jungle Mangler.
- **Phase 4** — WAVE engine + Serum wavetable import (CLI, CI and web editor) + factory tables + FM dark bank + 808 engine.
- **Phase 4b** — PAD and STAB engines (chord memory first), then the extra stubs.
- **Phase 5** — FX: inserts, sends, performance FX, resampler.
- **Phase 6** — Massive-style UI polish on device, web editor, presets, demo patterns, docs, release via GitHub Releases + Pages.

At the end of each phase: commit, tag, write a short CHANGELOG entry, list what you tested on hardware vs only in simulation, and tell me exactly what to listen for.

---

## 8. How to work with me
- Explain decisions briefly; I'm a producer who codes, not a full-time DSP engineer.
- If a feature won't fit the chip, say so straight and propose the best cut-down version. Don't fake it.
- Ask before anything that risks bricking the device.
- Push for sound quality: it has to sound like a record, not a toy.
