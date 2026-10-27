# DREADNOUGHT

A dark jungle / drum & bass firmware for the **M-VAVE FM-1**: Reese and dread basslines, distorted 808s,
wavetables (with Serum import), a Rex-style break chopper, pads, tech stabs and a heavy FX section,
sequenced from the box, with a Massive-style interface.

**Status: 0.11 — a sequencer page per instrument, 8 patterns each, Digitakt-style conditions, retrig rolls.** DREADNOUGHT's own app, screen and engines on [X0X](https://github.com/charlesvestal/fm1-x0x)'s
proven FM-1 platform (boot-loop guard, safe mode, USB, storage, updates). First engine: **REESE**, measured against
the maths (see [docs/REESE.md](docs/REESE.md)), an 8-track step sequencer, and BREAKS, a Rex-style break chopper: track 1 plays
REESE, tracks 2 and 3 chop breaks (two built in, or your own from the [breaks page](https://twistedgenetics.github.io/DREADNOUGHT/breaks/)),
tracks 4–8 play other gear over MIDI until their engines arrive. Every push builds a `.fwsc` in GitHub Actions and publishes it as a
download (below).

**Manual:** [docs/manual/DREADNOUGHT-manual.pdf](docs/manual/DREADNOUGHT-manual.pdf) — a quick start, then everything.

## Web editor

**The FM-1 on your computer's screen (0.26).** The editor opens on a big copy of the FM-1: the screen (what the FM-1's
shows, large: the page and its tabs, the sound's name, the four knobs with their names and values, the status line),
the encoders and OCT on the left, the two rows of buttons on the right (lit as on the FM-1), the 27 keys below. Turn a
knob (drag, or the wheel), press a button, play a key, and the FM-1 does what it would; turn a knob on the FM-1 and the
page follows. Shift-click a button to hold it down for the combinations (SAVE + FX, HOME + SELECT, SEQ + a key);
click a tab to go to that page; your computer's keys play the keys (Z S X D C… and Q 2 W 3 E…). It's the FM-1's own
pages, so every instrument works there, including ones added later. The tabs after it edit in depth.

[**twistedgenetics.github.io/DREADNOUGHT/editor**](https://twistedgenetics.github.io/DREADNOUGHT/editor/) (Chrome or
Edge, FM-1 on USB with DREADNOUGHT 0.7 or later; the LFOs need 0.9): all 8 tracks in a mouse-driven piano roll and slice grid, the REESE
and BREAKS controls, play / stop, tempo, save; back up everything to a file and restore it; keep as many REESE sounds
as you like in the browser or as files; the selected note's velocity, condition and retrig, a velocity ramp
(shift-drag the velocity lane) and each instrument's 8 patterns (0.11); from 0.9 the **LFOs** tab, where you draw an LFO's shape with the mouse. From 0.23 the **SAMPLER** tab: its 16 pads, or its zones over a keyboard. From 0.25 the **FM** tab, every value of the voice; from 0.29 its **DX7 banks** card: load a bank (.syx), play each voice in the browser (▶, the FM-1's own engine) or on the FM-1 (**FM-1**), tick **Keep** on the ones you want and send them: they go into a sample slot with the samples and come up on the FM-1's PRESETS after the factory voices. (DX7 banks dropped on the Project tab's samples work as before.) From 0.32 the **WAVE** tab: every value, and its **Wavetables** card: load a Serum wavetable (.wav, as Serum, Vital and Ableton's Wavetable save them) or a single-cycle WAV, see its frames, move POS through them and hear it in the browser, then send it: it goes into a sample slot with the samples and comes up on WAVE's TABLE after the factory tables. It
follows the box live: turn a knob on the FM-1 and the page moves too. **Demo** tries it with a simulated FM-1.

The **Project** tab (0.12): what each track plays and its sends to the effects, the FX rack, how full the FM-1's
memory is and its CPU load; **samples** (three slots: 80 KB, 80 KB and, from 0.29, 64 KB, whose top 16 KB holds settings; the breaks play only your loops, the built-in ROLLER and STEPPER are gone from 0.29; drop WAV, MP3, AIFF… files: each goes on in mono — L+R, left, right or side —
at the rate you pick, 44.1 kHz down to 8 kHz with 26 kHz for the SP-1200's sound; **Fit to space** picks rates so they
all fit, the longest lowered first; a sample already on the FM-1 can be given a new rate too, its slices kept; ▶ plays one as the FM-1 will); and **projects**: Save project to computer writes one
`.dnproj` file with everything (sounds, tempo, layout, all 8 patterns of every track, the samples and their slices, and
the original sample files, so you can convert them again later); Load a project sends one back (only what differs: a
cut-off load carries on when you run it again); Choose a projects folder keeps them in a folder and lists them there
(Chrome / Edge). Stop the FM-1 before saving or loading.

**On an iPad:** Safari can't talk to MIDI, so the editor comes as an app there: the same pages, with the FM-1 on USB
through CoreMIDI, and SHIFT / DELETE toggles for what a keyboard and mouse do. Download the `.ipa` from the
[**ipad** release](https://github.com/TwistedGenetics/DREADNOUGHT/releases/tag/ipad) and add it in SideStore or AltStore, or
build it with Xcode: [ios/README.md](ios/README.md).

**If it won't connect:** close everything else that uses the FM-1's MIDI port (Ableton, the breaks page or the flasher
in another tab), reload the page, Connect again. If Chrome once refused MIDI for the site, click the icon left of the
address bar and allow MIDI devices. Still nothing: open **Diagnostics** at the bottom of the page, press **Look for
ports**, pick the FM-1's port if it's listed under another name (macOS, Windows and Linux name it differently, and
remember the name from earlier firmware), Connect, and send the text from **Copy all**.

## Plugin: AU / VST3 / standalone (0.44, beta)

**DREADNOUGHT in your DAW.** The FM-1's own app and engines, built for the computer: the same screen, pages, sounds,
sequencer and effects, with the FM-1's panel to click on (the encoders, the knobs, both rows of buttons, the 27 keys;
your computer's keys play them too). AU, VST3 and a standalone app for macOS (Apple silicon and Intel), VST3 and the
app for Windows and Linux: `DREADNOUGHT-plugin-X.Y-macOS.zip` (or `-Windows`, `-Linux`) on each
[release](https://github.com/TwistedGenetics/DREADNOUGHT/releases/latest), with an install note inside
([docs/plugin/README.txt](docs/plugin/README.txt)).

- MIDI channels 1–8 play tracks 1–8; channels 9–16 play the track the screen shows. BREAKS (and SAMPLER's pads)
  take C1 as the first slice.
- **FOLLOW THE HOST** (on by default): the sequencer starts and stops with the DAW, at its tempo.
- MIDI tracks send their notes out to the DAW. The project is saved with the DAW's project.
- **The web editor connects to the plugin (0.45, macOS and Linux):** the plugin opens a MIDI port, "DREADNOUGHT
  Plugin", and the editor finds it as it finds an FM-1. Your own samples, slices, DX7 banks, wavetables, every sound,
  pattern and the song, and whole `.dnproj` projects: the same files as the FM-1's, so a project moves between the
  box and the DAW both ways. Samples are kept in the DAW's project, within the FM-1's 224 KB.
- For now: one per project, no editor port on Windows, and the sequencer starts from its bar 1 when the DAW plays.

Build it: `cmake -S plugin -B build/plugin -DCMAKE_BUILD_TYPE=Release && cmake --build build/plugin --config Release`
(JUCE is fetched; Python 3 with numpy and Pillow makes the engines' tables).

## Download & install

Go to [**Releases**](https://github.com/TwistedGenetics/DREADNOUGHT/releases/latest) and download `DREADNOUGHT-0.11.fwsc`
(the newest version's file). It's a beta that hasn't been played on a real FM-1 yet: read
[docs/RECOVERY.md](docs/RECOVERY.md) first and back up your FM-1.

- **Web flasher:** open [twistedgenetics.github.io/DREADNOUGHT/flasher](https://twistedgenetics.github.io/DREADNOUGHT/flasher/)
  in Chrome or Edge with the FM-1 on USB, back up, then pick the newest release from the list or choose the
  `.fwsc` file you downloaded, and press Install.
- **Command line:** X0X's installer, `python3 x0x-firmware/tools/fm1_install.py DREADNOUGHT-0.14.fwsc` (needs `pip install mido python-rtmidi`).

The FM-1 restarts on the DREADNOUGHT screen. Press **PLAY** to hear the demo reese line.

| Control | Does |
|---|---|
| PLAY | sequencer start / stop (MIDI clock, start and stop go out on USB); with REC armed, a bar's count-in first (SETUP) |
| REC | record: keys write into the selected track (playing: at the nearest step, as long as you hold the key; stopped: step by step). On a sequencer page REC acts as it comes up (so it can be held for a ramp) |
| SEQ | **this instrument's sequencer page**: from REESE's pages its piano roll (ROLL), from a break's BRK pages its slice grid (DRUM), from the MIDI page that MIDI instrument's roll; SEQ again goes back. (The 8-track OVERVIEW is gone: each instrument has its own page) |
| SEL / EDIT / ENV / LFO / GLO | REESE's pages (OSC, FILTER, AMP, MOD, OUT); **ENV** again: **F.ENV**, the filter envelope (F.ATK, F.DEC, F.SUS, F.REL; its amount is FENV on MOD), ENV again back to AMP; **LFO** again: LFO 1, 2, 3, 4 (below), back to MOD; **GLO** again: **FX** (below), again: **SETUP** (CLICK OFF / REC / ON, its level, COUNT-IN off / 1 / 2 bars, **VEL SENS**: how much velocity changes REESE's level, 0 = not at all); on SETUP, **FX** switches the screen's **skin**: **BLOCKS** (the default from 0.31: a near-black ground, each knob's value a big tile in that knob's colour, K1 orange, K2 yellow, K3 green, K4 blue, filled from the bottom as far as the value; HOME's tracks in their instrument's colour), **LIGHT** (warm white, ink, orange; 0.30) or **DARK** (the look before), round; kept with SAVE, again: **MIDI** (the MIDI instruments, the tracks playing MIDI in order: which, its channel, its OVERLAP; SELECT: the next), again: **PROJECT** (below) |
| SEQ held + white key 1–8 | **track 1–8**: its instrument's page (on a sequencer page: its sequencer page) |
| SEQ held + black key 1–8 (OP1…) | **track 1–8's instrument**: its sound pages, from anywhere, the sequencer pages too (0.29) |
| SUB's pages (its track selected; SEL / EDIT / ENV / LFO / GLO as REESE's) | **OSC** SAW, PULSE, SUB, NOISE (the 101's mixer; **SEL again: OSC2** PW, PWM, PWM RATE, SUB MODE −1 OCT / −2 OCT / −2 25%) · **FILTER** CUT, RES (self-oscillates in tune near the top), ENV, KEY (**EDIT again: DIRT** DRIVE, TYPE SOFT / HARD / FOLD / RECT, CRUSH, TONE) · **AMP** ATK, DEC, SUS, REL (**ENV again: F.ENV**, the filter's own F.ATK, F.DEC, F.SUS, F.REL; ENV on FILTER its amount. A factory sound starts it the same as the amp, the 101's one envelope) · **MOD** GLIDE, PORTA OFF / AUTO / ON, ACCENT (the sequencer's ACC steps), VCA ENV / GATE · **OUT** LEVEL, TRANS, SPLIT (below it the signal goes round the dirt: the low end stays clean), OVERLAP. PRESETS: its 12 factory sounds, INIT, then USER 1–8. **FX on AMP** (REESE's too): **ENV SYNC**, ATK DEC REL as note lengths at the tempo (1/64 … 8 bars; FX lit), FX again: free, in ms. Its LFO destinations: S CUT, S RES, S DRIVE, S PITCH, S LEVEL, S PW (the others are REESE's and the breaks') |
| SAMPLER's pages (its track selected; SEL / EDIT / ENV / LFO / GLO) | **ONE-SHOT** (16 pads: DRUM's rows, the first 16 keys): **PAD** SAMPLE, START, END, REVERSE · **FILTER** CUT, RES, DRIVE, CRUSH · **AMP** ATTACK, HOLD (127: to the end), DECAY, LEVEL · **MOD** PITCH, FINE, PAN, CHOKE (a hit cuts its group) · **OUT** MODE, PAD. **KEYS** (a key map of 8 zones): **ZONE** SAMPLE, ROOT, LOW, HIGH · **FILTER** CUT, RES, DRIVE, CRUSH · **AMP** ATTACK, DECAY, SUSTAIN, RELEASE · **LOOP** LOOP OFF / LOOP / SUST, L.START, L.END, TUNE · **OUT** MODE, ZONE, LEVEL, VOICES (1: mono). PRESETS: the pad / zone shown. SEQ: DRUM (pads) or ROLL (keys) |
| FM's pages (its track selected; SEL / EDIT / ENV / LFO / GLO) | A six-operator DX7-style synth (Dexed's engine). **VOICE** ALGO (1–32), FEEDBACK, BRIGHT (the modulators' levels), TIME (every envelope faster / slower) · **OP** OP (KNOB 1: which of the six), COARSE and FINE (shown as the ratio, or Hz when FIXED), LEVEL; **EDIT again: OP+** DETUNE, VEL, MODE RATIO / FIXED · **EG** the operator's R1–R4; **ENV again:** its L1–L4 · **LFO** the voice's own: SPEED, DELAY, PMD, AMD; **LFO again:** WAVE, SYNC, PMS, KEY SYNC · **OUT** LEVEL, VOICES (1–6; 1: mono, legato), ATTACK and RELEASE (the carriers'). BRIGHT, TIME, ATTACK and RELEASE sit on top of the voice without changing it. PRESETS: the factory voices, then the project's DX7 voices (from the editor). The keys play chords. The rest of a voice (scaling, curves, pitch envelope, AMS, the name): the editor's FM tab |
| WAVE's pages (its track selected; SEL / EDIT / ENV / LFO / GLO) (0.32; 0.33: WAVE 2) | A wavetable synth with two oscillators. Each page button again: the page's next view. **SEL:** WAVE A (TABLE, POS, WARP OFF / SYNC / BEND / MIRROR / PWM / FM / CRUSH, AMOUNT) · OSC A (UNISON, DETUNE, SPREAD, SUB) · WAVE B (B TABLE, B POS, B WARP, B AMT) · OSC B (OSC B ON, B OCT, B SEMI, B FINE) · B MIX (B LEVEL, B MODE MIX / FM / RING / AM / SYNC, MOD AMT, NOISE). **EDIT:** FILTER (LP12 LP24 HP BP NOTCH COMB, CUT, RES, ENV) · DRIVE (DIST SOFT HARD FOLD RECT TUBE BITS RATE, DRIVE, SAT, KEY). **ENV:** AMP · MOD ENV · ENV 3. **LFO:** MOD (POS ENV, POS LFO, L1 RATE, CUT LFO) · LFO 1-3 (SHAPE SINE TRI SAW SQUARE S&H RANDOM, RATE, SYNC to the tempo, MODE FREE / RETRIG) · MATRIX (SLOT 1-8, SOURCE, DEST, DEPTH: LFOs, envelopes, velocity, key, random to positions, warps, pitches, MOD AMT, B LEVEL, cutoff, res, drive, level, pan, sub, noise, LFO rates, detune). **GLO:** OUT (LEVEL, VOICES, GLIDE, TRANS). The voices it plays depend on what's on (unison, B, a drive, LP24, SAT, stereo), within the FM-1's CPU. PRESETS: 20 factory sounds. The keys play chords. SAVE keeps it |
| JUNO's pages (its track selected; SEL / EDIT / ENV / LFO / GLO) (0.36) | A Juno-60 / 106-style polysynth, six voices. SEL, EDIT and ENV again: the page's second view. **SEL:** DCO (RANGE 16' 8' 4', SAW, PULSE, SUB) · DCO 2 (PWM, PWM SRC LFO / MAN / ENV, NOISE, LFO on the pitch). **EDIT:** VCF (CUT, RES: rings on its own near the top, ENV, KEY) · VCF 2 (HPF BOOST / FLAT / 2 / 3, ENV POL, LFO, VEL). **ENV:** ENV (ATK DEC SUS REL: the filter's and the amp's) · VCA (ENV / GATE, VEL). **LFO:** RATE, DELAY (fades in after the first key), SYNC (OFF or a length), MODE FREE / KEY. **GLO:** OUT (CHORUS OFF / I / II / I+II, LEVEL, VOICES 1-6, TRANS). PRESETS: 17 factory sounds (JUNO PAD, BRASS, STRINGS, 106 BASS, ORGAN, RAVE STAB …). Plays chords. SAVE keeps it |
| SAMPLER 2 (0.36) | a second sampler on a track of its own: its own pads or key map and settings, the same pages as SAMPLER (the top line says SMPL 2). The web editor's SAMPLER tab picks which one it shows |
| PROJECT (GLO ×5) | (0.35) the samples' memory, the effects' memory, the CPU load and its peak, as four tiles (0.40: **CPU x2** when both cores are working, x1 on one). SAVE + FX names the project. (The tracks' instruments are chosen on HOME now) |
| SONG (GLO ×6) | (0.38) **the song**: rows, each the pattern every track plays (– carries on) for 1-16 bars, up to 64. KNOB 1 the row, KNOB 2 the track (or white keys 1-8), KNOB 3 its pattern, KNOB 4 the bars; black 9 a row of the patterns playing now, 10 a copy, 11 delete; black 1-8 mutes. **FX** turns the song on: PLAY then plays it from the row picked, each row's patterns switched in at its first bar, round to the first row. The web editor's Project tab edits it too |
| FX (GLO ×2) | the FX rack. **MIX:** the selected track's SEND A and SEND B, and FX A / B's RETURN; **FX** button: **FX A** (KNOB 1 its effect, KNOBS 2–4 its first values), again **FX A 2** (its next four, when it has them), again **FX B**, **FX B 2**, back to MIX. The effects are listed below. An effect that won't fit in the FX memory (176 KB) is refused: DELAY sits beside any other, two of the big reverbs fit, two DELAYs don't |
| ARP | BREAKS, pressed again to cycle: **BRK** (loop, pitch, decay, crush) → **BRK2** (choke group, slices GRID / AUTO / MANUAL, stretch REPITCH / SLICE / STRETCH, level, CPU load) → **SLICE** (chop mode, below) |
| Keys (sound pages) | play (mono, last note wins; hold one key and press another to slide); with track 2 or 3 selected: the break's slices |
| White keys (ROLL, DRUM) | the 16 steps: tap on / off. **Holding one pops up its values big** (0.31.1): four tiles, one a knob, over the roll; FX the next four (the dots show which of three). A step turned on gets **the last note's** note, length and chord (the last one you put in, edited or touched on that track; 0.29). **Hold one** and the knobs edit it: NOTE (break: slice), LENGTH, VELOCITY, CONDITION; **FX** (still holding): SLIDE (break: reverse), ACCENT, RETRIG, its RATE; FX again: the retrigs' LENGTH and VELOCITY fade. **Move a note:** hold its step and press the step it should go to (black keys 10 / 11 for another bar; onto a note: they swap; SAVE held: a copy), or turn **SELECT**; OCT− / OCT+ move it an octave. **A range** (0.37): hold the first step, hold REC, press the last step; KNOB 1 sets the CONDITION of every note in it (e.g. !4:4: they drop out every 4th time round), KNOB 2 their LENGTH, KNOB 3 / 4 a velocity ramp's start and end |
| Black keys (ROLL, DRUM) | 1–8 mute tracks 1–8 (0.37: a muted track stops at once, a ringing break or pad too); 9 held 1 s clears the track; 10 / 11 move through steps 1–64 (as they come up) |
| Black key 10 or 11 held + white key 1–4 (ROLL, DRUM) | **the track 1, 2, 3 or 4 bars long** (0.34) |
| KNOB 1–4 (ROLL) | scroll the pitches, transpose the track, track length, steps in view |
| FX (ROLL) | the keys' mode, round: **STEP** (white keys are the steps), **KEYS** (the keys write notes at a cursor: KNOB 2 their length, KNOB 4 the cursor, HOME a rest; a key under a held one: a slide), **PLAY** (0.34: the keys play the sound with the roll in view; REC lit: they record live; the bar in view follows the one playing). FX lit: KEYS or PLAY |
| FX (DRUM) | (0.34) **PLAY** mode on / off: the keys play the slices (or SAMPLER's pads), the row following the key; REC lit: they record live |
| FX held ½ s | (0.34) **the FX page**, from any page; held again there: back to the page you came from. A tap does FX's job on the page (as it comes up) |
| REC held + any knob (ROLL, DRUM) | (0.34) **REC's grid** (record quantise): live notes go to the nearest 1/16 (a step, as before), 1/8, 1/4, 1/2 or BAR line. Shown on the status line while REC is lit or in PLAY mode; kept with SAVE |
| ROLL's top | the whole track, and over it (0.34) **a ruler of the pattern's bars**: the longest track playing (or this one, when longer), the bar playing lit; a shorter track's notes repeat dim across it, so you can see where your bar 1 falls against the longer tracks |
| KNOB 1–4 (DRUM) | the slice row (a tap puts it on a step), track length, swing, steps in view. **SAVE + FX:** the break laid straight, each step its own slice |
| SELECT | the page (sound pages), BREAKS A / B (BRK pages, DRUM), the MIDI instrument (MIDI page); with a step held: moves it |
| MIDI page, KNOB 4: USB OUT (0.42) | **STEREO**: the computer records the mix over the cable, as ever. **10 CH**: ten channels — 1-8 the tracks (each mono, dry), 9-10 the FX returns only — to mix and add effects in the DAW. Turn it, SAVE, then power off and on. Ten channels' ring (40 KB) comes out of the FX memory: 131 KB left (DELAY plus an echo or a small reverb fit; two big reverbs don't, FX B goes off if a saved pair is too big). The FM-1's own output always plays the whole mix |
| KNOB 3 / 4 (MOD) | LFO 1's RATE and DEPTH (where LRATE and LFLT were: a 0.8 sound's filter LFO is LFO 1 now, sounding the same) |
| KNOB 4 (OUT) | **OVERLAP** (OCT's dial is on SETUP; OCT− / OCT+ as ever): SLIDE — a REESE note whose length reaches past the next note glides into it, no new attack, in GLIDE's time (MOD page, now shown in ms); CUT — it retriggers |
| PRESETS | the synth's sounds (REESE's, or SUB's with its track selected): the factory sounds, the last of them **INIT** (a plain starting point), then **USER 1–8** (an empty one reads `USER n -` and leaves the sound alone). A sound carries its synth's 4 LFOs and ENV SYNC. On the BRK pages and DRUM, the loop |
| ALGORITHM | tempo |
| HOME | **the HOME page** (0.29): the eight tracks in a grid, each its instrument and sound (a dot when it plays a note, MUTED dim). A white or black key 1–8 opens that track's instrument; or SELECT to pick one and HOME again to open it. On a sequencer page, held: **FILL** (for FILL / !FILL steps; a tap still opens HOME) |
| FX (HOME) | (0.35) **the instrument picker** for the selected track: SELECT or KNOB 1 picks OFF, REESE, BRK A, BRK B, MIDI, SUB, SAMPLER, FM, WAVE, JUNO or SAMPLER 2 (one on another track shows that track's number: choosing it swaps the two), FX puts it in and opens it, SEQ calls it off. An empty (OFF) track opens the picker when you open it |
| OCT− / OCT+ | octave |
| SAVE (tap) | keep the sound, the patterns and your slices for next time (lit when changed) |
| SAVE held + PRESETS | store the synth's sound (with its LFOs) in a user slot: turn to pick USER 1–8 (shown on the status line), let go of SAVE to store it; it is kept at once. The name editor opens straight after (below) |
| SAVE held + FX | **name it**: on PROJECT the project, on a synth's pages the user sound loaded (a factory sound: store it first). The name editor: KNOB 1 or SELECT the place, KNOB 2 or PRESETS the letter, **SAVE** keeps it, **HOME** leaves it as it was |
| HOME held + SELECT | **pick an instrument**: the next / last track, straight to its instrument's pages (the status line names it) |
| SAVE held + OCT− / OCT+ | copy / paste the bar in view of the selected track |
| SAVE held + white key 1–8 (ROLL, DRUM) | this instrument's **pattern** 1–8: stopped, at once; playing, when the playing one ends (it flashes until then) |
| SAVE held + white key A held + white key B (ROLL, DRUM) | (0.37) **pattern A copied into pattern B**, in one move; you stay on the one playing |
| Black key 10 / 11 held + SELECT, or + OCT− / OCT+ (ROLL, DRUM) | (0.37) **every step shifted** a step (SELECT) or a bar (OCT) later / earlier, wrapping round the track: a part recorded onto the wrong bar put right |
| SAVE held + black key 10 / 11 (ROLL, DRUM) | copy this pattern / paste into this one |
| Hit FX (breaks, SAMPLER's pads) | (0.39) a step's own effect on its hit: hold the step, FX twice, KNOB 3 **HIT FX**: STR 1/2 and STR 1/4 (the Akai S950 / S1000's cyclic time-stretch: the hit at its own pitch, 2x / 4x longer, in 32nd-note cycles, the warble and all), STUT32 / STUT64 (its first 32nd / 64th over and over for a beat, then it plays on), TAPESTOP (slowing to a stop over a beat), OCT− / OCT+. KNOB 4 **COND ON**: NOTE (as ever) or FX: the hit always plays, and its condition decides only the effect, e.g. 1:4 for a stretch every 4th time round, 1:2 for every other bar. On a chord's track the same switch makes the condition decide the chord. The web editor's note inspector has both |
| Chords (FM, WAVE, SAMPLER's keys, MIDI tracks) | 15 chords: MIN, MAJ, MIN7, MAJ7, 7, MIN9, MAJ9, ADD9, SUS2, SUS4, 5, DIM, AUG, MIN6, 6, built on the step's note. Hold a step on ROLL and turn PRESETS, or press FX twice and turn KNOB 3 (CHORD). Or record: with REC on (the sound page, ROLL, or ROLL's KEYS mode) hold keys together and the step gets the chord they make, in any order or inversion (none of the 15: just the first key). A chord step reads `F3m7`; its note moves the whole chord. Other instruments play the root |
| SAVE held + REC (ROLL, DRUM) | **×2**: the bars with notes copied into the empty bars after them (1 bar → bar 2, 2 bars → bars 3–4); when the notes fill the track it doubles (16 → 32 → 64 steps) with them copied into the new half. Press again for 4 bars |
| OCT− + OCT+ held 5 s | update mode (recovery) |

**Chop mode (ARP → BRK → BRK2 → SLICE)**, the MPC / SP-404 way: a break in MANUAL starts as one slice, the whole
loop, and you put each chop on its own key: press PLAY (the loop runs), press REC (lit: the keys cut), and hit the
lowest key on the first hit, the next key on the next, and so on. Each key's chop lands where you hit it; hit a key
again to retake it. REC off: the keys play the chops. Chops save themselves (3 s after the last edit, no SAVE needed), and
adding or deleting one never changes what your pattern plays (the steps follow their chop).

| Control (SLICE page) | Does |
|---|---|
| REC | the keys cut (lit) or play (off) |
| Keys, REC lit | key n puts chop n where the loop is playing (PLAY) or, stopped, at the cursor; a key with a chop moves it (a retake), the next key adds one; the chops stay in order, so a key that would land before the chop below it or after the one above says so and cuts nothing; lit: the next key to cut |
| Keys, REC off | play chop 1, 2, 3 … from the lowest key (OCT+ for the 28th and up), and select it; lit: the selected chop, dim: the others |
| SAVE held + REC | a chop between the others, at the cursor (or where the loop plays): the ones after it move up a key (and the pattern's steps with them) |
| KNOB 1 | select a chop (the cursor jumps to it, you hear it); with only one chop: moves the cursor |
| KNOB 2 / KNOB 3 | the cursor through the loop: coarse (an eighth of the screen a detent) / fine (4 samples) |
| KNOB 4 | zoom: the whole loop … 1/64 of it, around the cursor |
| PLAY | loop the break from the cursor (again: stop) |
| FX | move the selected chop to the cursor |
| HOME | delete the selected chop; held 1 s: delete them all |
| SAVE held + OCT− / OCT+ | start from AUTO's hits / from the GRID instead |

On MIDI tracks an overlap sends the new note-on before the old note-off (legato for the synth on the other end); the
web editor's **Overlap** switch sets SLIDE / CUT for every synth and MIDI track. Break tracks never slide.

**Each instrument has 8 patterns** (REESE, BREAKS A and B, MIDI 1–5), shown along the top of its sequencer page: the
playing one lit, a queued one flashing, the ones with notes marked, and how full pattern memory is (the notes of all
64 patterns share it: about 1 680). They're kept with SAVE. A song mode will chain them later.

**Conditions (as the Digitakt):** hold a step, KNOB 4: **%** (a chance, rolled each time round), **FILL / !FILL** (HOME
held or not), **PRE / !PRE** (the last condition on this track came out true / false), **NEI / !NEI** (the track
before's), **1ST / !1ST** (the pattern's first pass), **LST / !LST** (its last pass before a queued pattern takes
over), **A:B / !A:B** (the A-th of every B passes: 1:2 every other, 4:4 every fourth…). A step whose condition says no
is as if it were off. A red corner marks a step with a condition.

**The effects (0.16)** — each FX slot holds one, fed by the tracks' sends, its RETURN mixed in. TIMEs marked *synced*
follow the tempo (1/32 … 1/2, dotted and triplets); too long for the memory at a slow tempo they halve, still on the
beat.

| Effect | What it is | Values |
|---|---|---|
| DELAY | the dark half-rate ping-pong of 0.12 | TIME (synced), FEEDBACK (over 100 it howls), TONE |
| TAPE | Carnac OS's tape echo: wow, flutter, head loss, saturation; long times slow the tape (darker) | TIME (synced), FEEDBACK (over 100 it runs away into a held howl), DEGRADE (new … worn out), PINGPONG |
| BBD | Carnac OS's bucket brigade: crumbly, dark, the compander's breath | as TAPE |
| SPRING | Carnac OS's two springs: every echo a falling chirp (the dub "drip") | TIME, DAMP, PREDELAY, LOW CUT |
| TOMB | a small stone room (Freeverb-style) | as SPRING |
| MOD | a big moving hall, its tail chorused (Lexicon-style) | as SPRING |
| CELESTIAL | MOD with an octave-up shimmer climbing in its tail | as SPRING |
| DOLMEN | a small dense chamber, bright | as SPRING |
| TUMULUS | a cavern: long and dark | as SPRING |
| QUADRA | Quadraverb-style chains, CONFIG: CHORUS (EQ › chorus › delay › plate), REVERB, 5EQ>REV, FLANGE, DETUNE, DLY>REV | CONFIG, DECAY, TIME (synced) / PREDELAY, FEEDBACK, MOD / DETUNE / REV LVL, TONE, 16-BIT (the 1989 grain); 5EQ>REV: LOW, LO MID, MID, HI MID, HIGH |
| MASSIVE | your Super-Massive (Horizon): the 8-line network and its ten modes ROOM, HALL, HALO, SWELL, ABYSS, ECHOVRB, ORBIT, PRISM, PONG, DUB | MODE, TIME (0–12 synced 1/32 … 1/2D, then 10 ms … 1250 ms), FEEDBACK, WARP, DENSITY, MOD, TONE |
| FILTER (0.41) | resonant LP / BP / HP / NOTCH with a drive before it and a synced LFO on the cutoff (1/16 … 8 bars; SINE, TRI, SAW, SQUARE, S&H). No memory | MODE, CUTOFF, RES, LFO RATE, LFO AMT, DRIVE, LFO SHP |
| PHASER (0.41) | 4 / 6 / 8 allpass stages swept by a synced LFO, feedback, the sides apart by STEREO. The notches form where its wet meets the dry: send plenty. No memory | RATE, DEPTH, FEEDBACK, STAGES, CENTER, STEREO |
| CHORUS (0.41) | CHORUS or FLANGE: a swept delay each side (up to 20 ms), FEEDBACK for the flanger's ring. 4 KB | MODE, RATE, DEPTH, FEEDBACK, DELAY, STEREO |
| CRUSH (0.41) | bits (16 … 1) and the sample rate down (44.1 kHz … 170 Hz), a drive before, a tone after: parallel crunch. No memory | BITS, RATE, DRIVE, TONE |
| RING (0.41) | ring modulator, 20 Hz … 5 kHz, its pitch swept by a synced LFO; sine or soft square. No memory | FREQ, LFO RATE, LFO AMT, SHAPE |
| DRIVE (0.41) | SOFT, HARD, FOLD, TUBE or RECT (an octave up); LOW CUT (two poles) before it keeps the sub out of the dirt; a TONE after. No memory | TYPE, DRIVE, TONE, LOW CUT |

LOW CUT keeps a reverb's tail off the sub (off, 30 Hz … 800 Hz). MASSIVE runs at half rate and takes the FX memory the
other slot leaves: beside DELAY it has 80 KB (HALL holds ~320 ms, the echo modes ~450 ms), alone 176 KB (about twice
that); TIME shows the time it plays in ms when it's shorter than asked. Long tails come from FEEDBACK, as in Horizon.
Every effect's send is limited first, so a loud chord into a reverb doesn't clip inside it, and every tail dies away to
silence. A master limiter (0.17) turns the whole mix down on loud peaks rather than clipping it. If the FM-1 ever runs
out of time (too much at once), the status line says **AUDIO LATE: CPU FULL**; the PROJECT page shows the load and its
peak (CPU 40 PK 62%). From 0.40 both of the FM-1's cores render: the instruments are shared out between them by what
each costs (weighed again every 3 ms), and FX B runs on the second core beside FX A. If the second core won't start,
or gives up (X0X's checks for units where it misreads memory), everything goes back on one core, until power-off.

**Retrigs:** FX with a step held, RETRIG on: the step repeats at RATE (1/1 … 1/80 of a bar; 1/16 = a step, 1/32 two)
for LENGTH (FX again; the note's own by default), its VELOCITY fading out (−) or in (+): snare rolls, hat builds. White
ticks show the repeats. A later note cuts them.

**The sound pages** show a **progress bar** over the scope: the bars of the longest playing track, the beats, where it
is, BAR.BEAT, the beat flashing (the bar's first brighter); counting in, the beats of the count-in in red.

**LFOs (LFO from MOD).** Four, each to one place. KNOB 1–4: **SHAPE** (sine, triangle, saw up / down, square, S&H,
smooth random, DRAW), **RATE** (synced: 8 bars … 1/32, dotted D, triplet T; free: 0.05–20 Hz), **DEST** (REESE's
CUTOFF, RES, DRIVE, NOTCH, DETUNE, PITCH, SUB, LEVEL, F.ENV; BREAKS A / B PITCH, DECAY, CRUSH, LEVEL; another LFO's
depth), **DEPTH** (−64…+63; full: the whole cutoff, ±12 semitones, ±64 steps). **FX:** MORE (SYNC / FREE, MODE: FREE,
RETRIG on every REESE note that isn't a slide, 1-SHOT; PHASE; SMOOTH) and, with SHAPE on DRAW, **DRAW**: the white keys
are the points (16 or 32 a cycle; black keys 10 / 11 the second 16), a key sets its point to the pen (KNOB 1; hold a key
and turn KNOB 1 to move that point), KNOB 2 SMOOTH (steps … slopes), KNOB 3 16 / 32 points; black keys 1–8: sine,
triangle, saw, square, random, invert, reverse, clear. Synced LFOs start with the pattern and stay locked to it. The
screen shows the shape, where it is and its value; a modulated knob shows its reach in red. Saved with the sound.
Per-pattern modulation (an LFO per track, or values per step) may come later.

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

## Wavetables

WAVE (0.32) reads its tables straight from flash, each frame kept as 7 band-limited versions (512 samples down to 8:
Carnac OS's layout), so a high note never aliases and a table needs no RAM. The 8 factory tables are made by
`tools/gen_dn_wt.py` and kept in `wavetables/` as Serum WAVs (they open in Serum or Vital too). The editor converts
your own; the same conversion on the command line:

```
python3 tools/wt_convert.py MyTable.wav [MyTable.dnwt] [--frames 32] [--name MYTABLE]
```

A table is about 2 KB a frame (64 frames: 127 KB of the 224 KB the sample slots hold), so fewer frames leave more
room for samples.

## Building

Everything builds in GitHub Actions (`.github/workflows/build.yml`) on Linux with JieLi's toolchain;
each run attaches the `.fwsc` package as an artifact. To build on Linux x86-64 yourself:

```
git submodule update --init                     # X0X's platform, pinned
tools/get_deps.sh                               # JieLi toolchain + 3 AC79 SDK files, hash-checked
python3 tools/build_fm1.py --release 0.8        # build/fm1/DREADNOUGHT-0.11.fwsc (preflight included)
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
