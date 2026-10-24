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

## Web editor

[**twistedgenetics.github.io/DREADNOUGHT/editor**](https://twistedgenetics.github.io/DREADNOUGHT/editor/) (Chrome or
Edge, FM-1 on USB with DREADNOUGHT 0.7 or later; the LFOs need 0.9): all 8 tracks in a mouse-driven piano roll and slice grid, the REESE
and BREAKS controls, play / stop, tempo, save; back up everything to a file and restore it; keep as many REESE sounds
as you like in the browser or as files; the selected note's velocity, condition and retrig, a velocity ramp
(shift-drag the velocity lane) and each instrument's 8 patterns (0.11); from 0.9 the **LFOs** tab, where you draw an LFO's shape with the mouse. It
follows the box live: turn a knob on the FM-1 and the page moves too. **Demo** tries it with a simulated FM-1.

The **Project** tab (0.12): what each track plays and its sends to the effects, the FX rack, how full the FM-1's
memory is and its CPU load; **samples** (drop WAV, MP3, AIFF… files: each goes on in mono — L+R, left, right or side —
at the rate you pick, 44.1 kHz down to 8 kHz with 26 kHz for the SP-1200's sound; **Fit to space** picks rates so they
all fit, the longest lowered first; ▶ plays one as the FM-1 will); and **projects**: Save project to computer writes one
`.dnproj` file with everything (sounds, tempo, layout, all 8 patterns of every track, the samples and their slices, and
the original sample files, so you can convert them again later); Load a project sends one back (only what differs: a
cut-off load carries on when you run it again); Choose a projects folder keeps them in a folder and lists them there
(Chrome / Edge). Stop the FM-1 before saving or loading.

**If it won't connect:** close everything else that uses the FM-1's MIDI port (Ableton, the breaks page or the flasher
in another tab), reload the page, Connect again. If Chrome once refused MIDI for the site, click the icon left of the
address bar and allow MIDI devices. Still nothing: open **Diagnostics** at the bottom of the page, press **Look for
ports**, pick the FM-1's port if it's listed under another name (macOS, Windows and Linux name it differently, and
remember the name from earlier firmware), Connect, and send the text from **Copy all**.

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
| SEL / EDIT / ENV / LFO / GLO | REESE's pages (OSC, FILTER, AMP, MOD, OUT); **LFO** again: LFO 1, 2, 3, 4 (below), back to MOD; **GLO** again: **FX** (below), again: **SETUP** (CLICK OFF / REC / ON, its level, COUNT-IN off / 1 / 2 bars, **VEL SENS**: how much velocity changes REESE's level, 0 = not at all), again: **MIDI** (the MIDI instruments, the tracks playing MIDI in order: which, its channel, its OVERLAP; SELECT: the next), again: **PROJECT** (below) |
| SEQ held + white key 1–8 | **track 1–8**: its instrument's page (on a sequencer page: its sequencer page) |
| SUB's pages (its track selected; SEL / EDIT / ENV / LFO / GLO as REESE's) | **OSC** SAW, PULSE, SUB, NOISE (the 101's mixer; **SEL again: OSC2** PW, PWM, PWM RATE, SUB MODE −1 OCT / −2 OCT / −2 25%) · **FILTER** CUT, RES (self-oscillates in tune near the top), ENV, KEY (**EDIT again: DIRT** DRIVE, TYPE SOFT / HARD / FOLD / RECT, CRUSH, TONE) · **AMP** ATK, DEC, SUS, REL (one envelope for filter and amp, the 101's) · **MOD** GLIDE, PORTA OFF / AUTO / ON, ACCENT (the sequencer's ACC steps), VCA ENV / GATE · **OUT** LEVEL, TRANS, SPLIT (below it the signal goes round the dirt: the low end stays clean), OVERLAP. PRESETS: its 12 factory sounds, INIT, then USER 1–8. **FX on AMP** (REESE's too): **ENV SYNC**, ATK DEC REL as note lengths at the tempo (1/64 … 8 bars; FX lit), FX again: free, in ms. Its LFO destinations: S CUT, S RES, S DRIVE, S PITCH, S LEVEL, S PW (the others are REESE's and the breaks') |
| PROJECT (GLO ×5) | the project's 8 tracks and what each plays. KNOB 1 the track, KNOB 2 an instrument for it (OFF, REESE, BRK A, BRK B, MIDI, SUB), **FX** puts it on the track. REESE, SUB and each break are on one track at a time: given to another track, they swap. The patterns stay with their track. The status line: sample memory used, FX memory, CPU load |
| FX (GLO ×2) | the FX rack. **MIX:** the selected track's SEND A and SEND B, and FX A / B's RETURN; **FX** button: **FX A** (KNOB 1 its effect, KNOBS 2–4 its first values), again **FX A 2** (its next four, when it has them), again **FX B**, **FX B 2**, back to MIX. The effects are listed below. An effect that won't fit in the FX memory (176 KB) is refused: DELAY sits beside any other, two of the big reverbs fit, two DELAYs don't |
| ARP | BREAKS, pressed again to cycle: **BRK** (loop, pitch, decay, crush) → **BRK2** (choke group, slices GRID / AUTO / MANUAL, stretch REPITCH / SLICE / STRETCH, level, CPU load) → **SLICE** (chop mode, below) |
| Keys (sound pages) | play (mono, last note wins; hold one key and press another to slide); with track 2 or 3 selected: the break's slices |
| White keys (ROLL, DRUM) | the 16 steps: tap on / off. **Hold one** and the knobs edit it: NOTE (break: slice), LENGTH, VELOCITY, CONDITION; **FX** (still holding): SLIDE (break: reverse), ACCENT, RETRIG, its RATE; FX again: the retrigs' LENGTH and VELOCITY fade. **Move a note:** hold its step and press the step it should go to (black keys 10 / 11 for another bar; onto a note: they swap; SAVE held: a copy), or turn **SELECT**; OCT− / OCT+ move it an octave. **A velocity ramp:** hold the first step, hold REC, press the last step; KNOB 3 / 4 set the ramp's start and end |
| Black keys (ROLL, DRUM) | 1–8 mute tracks 1–8; 9 held 1 s clears the track; 10 / 11 move through steps 1–64 |
| KNOB 1–4 (ROLL) | scroll the pitches, transpose the track, track length, steps in view |
| FX (ROLL) | KEYS mode: the keys write notes at a cursor (KNOB 2 their length, KNOB 4 the cursor, HOME a rest; a key under a held one: a slide) |
| KNOB 1–4 (DRUM) | the slice row (a tap puts it on a step), track length, swing, steps in view. **SAVE + FX:** the break laid straight, each step its own slice |
| SELECT | the page (sound pages), BREAKS A / B (BRK pages, DRUM), the MIDI instrument (MIDI page); with a step held: moves it |
| KNOB 3 / 4 (MOD) | LFO 1's RATE and DEPTH (where LRATE and LFLT were: a 0.8 sound's filter LFO is LFO 1 now, sounding the same) |
| KNOB 4 (OUT) | **OVERLAP** (OCT's dial is on SETUP; OCT− / OCT+ as ever): SLIDE — a REESE note whose length reaches past the next note glides into it, no new attack, in GLIDE's time (MOD page, now shown in ms); CUT — it retriggers |
| PRESETS | the synth's sounds (REESE's, or SUB's with its track selected): the factory sounds, the last of them **INIT** (a plain starting point), then **USER 1–8** (an empty one reads `USER n -` and leaves the sound alone). A sound carries its synth's 4 LFOs and ENV SYNC. On the BRK pages and DRUM, the loop |
| ALGORITHM | tempo |
| HOME | hold a note, hands free; on a sequencer page, held: **FILL** (for FILL / !FILL steps) |
| OCT− / OCT+ | octave |
| SAVE (tap) | keep the sound, the patterns and your slices for next time (lit when changed) |
| SAVE held + PRESETS | store the synth's sound (with its LFOs) in a user slot: turn to pick USER 1–8 (shown on the status line), let go of SAVE to store it; it is kept at once |
| SAVE held + OCT− / OCT+ | copy / paste the bar in view of the selected track |
| SAVE held + white key 1–8 (ROLL, DRUM) | this instrument's **pattern** 1–8: stopped, at once; playing, when the playing one ends (it flashes until then) |
| SAVE held + black key 10 / 11 (ROLL, DRUM) | copy this pattern / paste into this one |
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

LOW CUT keeps a reverb's tail off the sub (off, 30 Hz … 800 Hz). MASSIVE runs at half rate and takes the FX memory the
other slot leaves: beside DELAY it has 80 KB (HALL holds ~320 ms, the echo modes ~450 ms), alone 176 KB (about twice
that); TIME shows the time it plays in ms when it's shorter than asked. Long tails come from FEEDBACK, as in Horizon.
Every effect's send is limited first, so a loud chord into a reverb doesn't clip inside it, and every tail dies away to
silence.

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
