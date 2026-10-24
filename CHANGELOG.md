# Changelog

## Phase 0 — groundwork (2026-10-23)

- Platform investigation: `docs/PLATFORM.md` (chip, audio, controls, memory, storage, update protocol).
- Recovery guide: `docs/RECOVERY.md`.
- Feasibility report, base recommendation, revised phases and on-device UI mock: `docs/FEASIBILITY.md`, `docs/ui-mock.png`.
- GitHub Actions: firmware build of the pinned base (SLOOP 2.8.1) on Linux, flasher tests, tag releases; Pages deploy.
- Web flasher: device detection, backup gate, release list or local file, SHA-256 and identity checks,
  install with progress, resume after a cut-off, links to M-VAVE's tools for going back.

Tested: flasher unit tests (6 pass) and a Chromium end-to-end run against a simulated FM-1 (full
install, cut-off and resume). Workflows lint clean with actionlint.
The first CI run built and packaged the base firmware on GitHub; the base's own host-test suite
reported a failure there (non-blocking for now, to be read in Phase 1). Not yet tested: the flasher on a real FM-1.

### Phase 0 update — Carnac OS and X0X findings (2026-10-23)

- Clock corrected to 360 MHz (measured by X0X); single-precision float is usable in audio code.
- Measured CPU figures from Carnac OS: 8 wavetable voices ≈ 38 % of both cores.
- Wavetable plan now uses Carnac's proven format (512-sample frames, 7 mip levels, read from flash).
- Recovery: chip-level rescue over plain USB with X0X's `fm1_rescue.py`; OCT− + OCT+ rescue entry and a CI image pre-flight check added to Phase 1.

## Phase 1 — first DREADNOUGHT build (2026-10-23)

- Forked SLOOP 2.8.1 into the repo (credits and what changed: `docs/UPSTREAM.md`); CI now builds this tree.
- Renamed to DREADNOUGHT: boot splash (wordmark over a Reese trace), About page, USB port name, rescue and crash
  screens. Dev builds report `FM-1_900`; tagged releases `FM-1_9XY`.
- Safety: OCT− + OCT+ held 5 s on the rescue screen now enters the chip's own update mode too; every CI build runs
  a start-up pre-flight check (`tools/preflight.py`, from Carnac OS) and fails on a bad image.
- New engine: **REESE** (engine 12). Up to 7 drifting saws, Moog-style ladder with drive, a swept comb for the
  hollow movement, a clean sine on the fundamental. Eight presets at the top of the BASS list: CLASSIC RS,
  HOLLOW RS, STACK RS, NEURO RS, DARK RS, MOVING RS, SUB REESE, and HOOVER RS under LEAD.
- Deferred to Phase 2: Clipper's full HYBRID voice. It's woven into Clipper's own voice, editor and UI layers,
  so it comes in with the Massive-style voice and pages rather than half-ported now.

Tested on the host: every inherited host test, golden renders for the 8 new presets, health (no clipping, DC
or hanging voices), UI fuzz, recovery hold. CI: the firmware builds, packages (610 KB) and passes the
pre-flight check. Not yet tested: anything on a real FM-1.

### Downloads (2026-10-23)

- Every push to main now publishes the Release `v<VERSION>` with `DREADNOUGHT-X.Y.fwsc` and `SHA256SUMS`, and
  redeploys the flasher so it lists the release. First release: **0.1 beta** (package identity `FM-1_901`).

## 0.2 — REESE rebuilt from research (2026-10-23)

Research notes and sources: `docs/REESE.md`.

- **OSC**: 2SAW (the CZ's 1+1' pair), 3SAW, 5SAW, 7SAW, SAWSQ (techstep grit), 2SIN (the soft original).
- **DTN** is now the total spread in cents, 0–100 (64 = 25 cents, 90 = 50, 127 = 100); measured beat rates match
  the maths within 2 %.
- **BEAT**: lock the movement to 1/1 … 1/16 (and triplets) at the song tempo, the same on every note.
- **SUB** now also high-passes the oscillators at ~120 Hz (24 dB), so the low end is a steady, clean mono sine
  instead of pumping with the beating. Drive only works above that split.
- **NTCH**: the swept notch comb (was HOLW).
- Presets: CLASSIC RS, OG REESE, HOLLOW RS, TECHSTEP, NEURO RS, SYNC RS, DARK RS, SUB REESE, HOOVER RS.

## 0.3 — our own firmware (2026-10-23)

- DREADNOUGHT is no longer a SLOOP fork: its own app, screen and engines, on X0X's platform pinned as a submodule
  (boot-loop guard, safe mode, crash screen, USB MIDI and audio, TRS MIDI in, storage, the update entry and loader),
  built the way Carnac OS builds (`tools/build_fm1.py`, with the start-up preflight and the miscompile check).
- REESE as a standalone engine (`src/dsp/reese.c`): mono, legato with glide, ADSR, filter envelope and LFO, per-oscillator
  drift. BEAT now locks exactly to the tempo on every note (one shared drift).
- Massive-style screen: title bar, preset strip, a live scope, five pages of four dials, status line; the page's button
  lights. SAVE keeps the sound over a power cycle.
- Host harness (`tests/host/dn_host.c`): the whole app run from a script, with screenshots and recordings; the engine
  measured: detune beat rates within 6 % (the drift is deliberate), tempo-locked beats exact at three pitches, the low
  end steady with SUB.
- Package identity follows X0X's scheme (FM-1_9000300); the web flasher accepts it.

Not yet tested on a real FM-1.

## 0.4 — the 8-track sequencer (2026-10-24)

- Eight tracks of up to 64 steps, each its own length (polymeter); per step a note, gate length, accent and slide
  (a tie with glide, the 303's way); swing; mutes. Sample-accurate in the audio interrupt; steps add up exactly to the
  tempo (no drift). MIDI clock, start and stop out on USB.
- Track 1 plays REESE; tracks 2–8 play MIDI channels 2–8 (keys play the selected track too).
- SEQ page: eight rows of the 16 steps in view, the playhead, the held step's values. White keys are the steps, black
  keys mute tracks, clear a track (held 1 s) and move through the 64 steps.
- REC: live recording to the nearest step while playing, step recording when stopped; a key held under another
  records a slide.
- PLAY is the transport now; holding a note moved to HOME. SAVE keeps the pattern as well as the sound.
- A demo reese line on first boot. Host tests: the sequencer's timing against the tempo, swing, slides, polymeter and
  mutes; the SEQ page driven from the panel, including record and save over a restart.

Not yet tested on a real FM-1.

## 0.5 — BREAKS (2026-10-24)

- BREAKS, a Rex-style break chopper, on tracks 2 and 3 (A and B). A loop is cut into 16 slices a bar; each step plays
  one; the loop's speed follows the tempo (a 136 BPM break at 172 is sped up, pitch and all). Per step: slice, gate,
  accent, reverse. Per track: loop, pitch (±24 semitones), decay after the gate (or HOLD: the break plays on), crush.
  A new slice cuts the last one without a click. Stopping the transport fades the breaks out.
- Two built-in loops, ROLLER and STEPPER, synthesised by DREADNOUGHT's own code at build time (no recordings).
- Your own loops: the new [breaks page](https://twistedgenetics.github.io/DREADNOUGHT/breaks/) (X0X's, adapted) uploads
  WAVs into the FM-1's three sample slots (~20 s in all); they appear after the built-in ones as BR1, BR2 …
  The firmware now answers the sample-slot commands (X0X's).
- BRK page (ARP): the loop's picture with its slices and the slice playing; LOOP, PITCH, DECAY, CRUSH.
- Tracks 4–8 play MIDI (channels 4–8). The keys play slices with a break track selected; REC records them.
- Host tests: a slice is the loop's own samples (correlation 1.00), reversed is the slice backwards, all slices in order
  at another tempo are the loop sped up, the release reaches silence, cuts don't click; the BRK page driven from the panel.

Not yet tested on a real FM-1.

## 0.6 — the mop-up (2026-10-24)

From `docs/prompts/PROMPT-0.6.md`.

- **Long notes.** A step's length runs from a quarter step to 64 steps and holds across the steps after it and the
  pattern's end, until it runs out or a later note cuts it (or slides from it). 0.4 / 0.5 patterns are converted.
  The overview shows how far each note reaches.
- **ROLL**, a piano roll for REESE and the MIDI tracks: notes as long as they last, slides as ramps, accents bright,
  an overview of the whole track above. STEP mode (white keys are steps) or KEYS mode (FX: the keys write notes at a
  cursor). Live recording keeps how long each key was held. Copy / paste a bar (SAVE + OCT− / OCT+); transpose.
- **DRUM**, a grid of a break track's slices against its steps.
- **Choke groups** for BREAKS A and B (both in group 1 at first: a hit on one stops the other).
- **Slicing.** GRID (16 a bar), AUTO (the loop's hits, found by an onset detector: every kick and snare of the
  built-in loops within 3.3 ms), or MANUAL: set on the FM-1's SLICE page or, easier, on the breaks page in the browser
  (drag, add, remove, hear each slice; Auto / Grid). Kept per loop, outside the sample slots.
- **Stretch.** REPITCH (the jungle way, as before), SLICE (each slice at its own speed, so PITCH is free of the tempo)
  and STRETCH (granular: tempo and pitch apart, the attack kept). Measured: pitch within 1 cent at 136 and 172 BPM;
  STRETCH ~1.2× REPITCH's cost. BRK2 shows the FM-1's CPU load.
- SAVE now acts when it comes up (so it can be held for copy / paste).

Not yet tested on a real FM-1.

## 0.7 — the web editor (2026-10-24)

- **Web editor** at `/editor` on the site (Chrome or Edge, over USB): every track in a piano roll (REESE and the MIDI
  tracks) or a slice grid (BREAKS A / B): click to add, drag to move, drag the end to lengthen, shift-click accent,
  double-click slide (breaks: reverse), right-click delete, a velocity lane; track length, mutes, swing. The REESE
  sound (factory sounds and every control), both BREAKS (loop, pitch, decay, crush, choke, slices, stretch, level),
  tempo, play / stop, save on the FM-1. Back up and restore everything as a file; a library of REESE sounds in the
  browser and as files. It follows the box live (a change on the FM-1 shows on the page). A Demo mode with a
  simulated FM-1.
- Firmware: the editor's commands (60–69: state, parameters, pattern read / write / commit, transport, save, factory
  sounds, names), tested on the host; the page's decoders checked against the firmware's own replies; the page tested
  end to end in Chromium against the simulated FM-1.

Not yet tested on a real FM-1.

## 0.8 — chop by hand, overlap slides, a roll that follows (2026-10-24)

- **Chop mode** (the SLICE page, rebuilt): MANUAL starts empty (one slice, the whole loop); a cursor (KNOB 2 coarse,
  KNOB 3 fine, KNOB 4 zoom 1x–64x, the view scrolling only to keep it in sight); REC chops at the cursor, snapped to a
  zero crossing, or — with PLAY looping the break on this page — where it's playing, 12 ms early for your reaction:
  tap-to-chop; FX moves the selected chop to the cursor (any chop, the first one too); HOME deletes it, held 1 s clears
  them all; SAVE + OCT− / OCT+ start from AUTO's hits or the GRID. The keys play the chops in order and select the one
  you play (lit). What was wrong in 0.7: moving was 3 ms a detent, adding could only halve a slice, slice 1 couldn't
  move, the selection could point past the last slice after a change (then every edit silently did nothing), and
  MANUAL began as a copy of AUTO.
- **Chops keep their sound:** adding or deleting a chop moves the pattern's steps with it (on both break tracks, and
  from the breaks page too), so the pattern plays the same audio. Chops save themselves 3 s after the last change.
- **Overlapping notes slide:** a REESE or MIDI note whose length reaches past the next note slides into it (no new
  attack; MIDI: note-on before note-off). GLIDE is the slide time and now reads in ms / s on the FM-1 and in the web
  editor (measured, an octave: GLIDE 64 ≈ 0.1 s, 90 ≈ 0.6 s; 0 = OFF, notes retrigger). **OVERLAP** SLIDE / CUT per
  track (REESE on OUT's KNOB 4, which was a second OCT control; every synth and MIDI track in the web editor). Slides
  are drawn as ramps on ROLL and in the piano roll.
- **The view follows the note:** on ROLL / DRUM the window follows a held note's pitch or slice; SELECT with a step
  held moves it in time, over the other notes, and the bar in view follows it. In the web editor: a horizontal zoom
  (1 bar / 2 bars / all), dragging past an edge scrolls with the note, arrow keys move the selected note (up / down,
  along the steps) and keep it in view.
- Fixed: a 1 s hold (black key 9 track clear, HOME on SLICE) could fire at once (a timer wrap on even milliseconds).

Tested on the host: new scenarios `chop.txt` (empty → chops by cursor and by tapping while it plays, insert / delete
keep the pattern's audio, keys select chops, chops survive a restart without SAVE), `overlap.txt` (mid-slide pitch,
CUT), `follow.txt`; overlap cases in `seq_test`; GLIDE timing and its text; the web editor end to end. Not yet tested
on a real FM-1.

## 0.9 — LFOs you can draw, a progress bar, moving notes, chop mode fixed (2026-10-24)

- **Chop mode never stuck** (the user: "the chop knob won't move off chop 1, so I can't set new chops"): an empty
  loop is one chop with the cursor on it, KNOB 1 had nowhere to go and REC on a chop silently did nothing. Now REC on
  a chop splits its slice in half, KNOB 1 moves the cursor while there's one chop, every refusal says why on the
  status line, and the status line shows the chop and the cursor's time.
- **The editor connects more surely:** it tries the likely port names, then every port, asks three times, and names
  the ports it saw; a **Diagnostics** panel (the ports, a port picker, every frame in hex, copy all); the breaks page
  finds the port by "FM-1" too. The device takes every editor frame whole by one rule shared with the tests.
- **Tabs:** each page group shows its own pages (sound pages, OVERVIEW / ROLL / DRUM, BRK / BRK2 / SLICE).
- **Progress bar** on the sound pages; a **count-in** (REC armed + PLAY: 1 or 2 bars, the keys only play meanwhile,
  PLAY blinks) and a **click** (OFF / REC / ON, level) on a new **SETUP** page (GLO again), with the OCT dial.
- **Moving notes:** hold a step and press another (another bar: black keys 10 / 11): moved; onto a note: swapped;
  SAVE held: copied; OCT− / OCT+ move a held note an octave; a word on the status line says which.
- **LFOs:** four, synced (an exact phase counter: within a sample of the sequencer over 64 bars) or free; eight
  shapes with a drawable one (16 / 32 points, SMOOTH); FREE / RETRIG / 1-SHOT; to REESE (cutoff, res, drive, notch,
  detune, pitch, sub, level, filter env), the breaks (pitch, decay, crush, level) or another LFO's depth. The LFO page
  (shape, where it is, its value), DRAW on the keys, the reach on modulated knobs; saved with the sound; the web
  editor's LFOs tab draws them with the mouse. A 0.8 sound's filter LFO becomes LFO 1 and sounds exactly the same
  (tested to the sample); MOD's KNOB 3 / 4 are LFO 1's rate and depth.
- Fixed: a 1 s hold (black key 9, HOME on SLICE) could fire at once (0.8's last-minute fix; tested).

Tested on the host: `lfo_test` (shapes, DRAW, sync against the sequencer, 1-SHOT, the 0.8 LFO, every destination
moving), scenarios `chop_stuck`, `progress` (click timing to the sample, count-in), `move`, `lfo`, `lfo_wav`, the
editor's LFO commands and golden replies, the web editor end to end (drawing, presets, Diagnostics). Not yet tested
on a real FM-1.

## 0.10 — chop on the keys (2026-10-24)

- **Chop mode, the MPC / SP-404 way** (the user's idea): REC on the SLICE page makes the keys cut. Key n puts chop n
  where the loop is playing (PLAY) or, stopped, at the cursor; a key that has a chop moves it (a retake), the next key
  adds one, and the chops stay in order (a key that would land out of order says so and cuts nothing). REC off: the
  keys play the chops, as before. SAVE + REC puts a chop between others (the ones after move up a key, the pattern's
  steps with them). REC no longer chops at the cursor by itself.
- **The web editor reads the pattern again on a real FM-1:** it asked for 200 bytes in one SysEx byte (over 127:
  Chrome refused the frame; found with the new Diagnostics). The simulated FM-1 now refuses 8-bit bytes as Web MIDI
  does.

Tested on the host (`chop_stuck`: keys cutting at the cursor and by ear, order refused, a retake, REC off playing;
`chop`, `slice`, `chop_bugs_07` updated). Not yet tested on a real FM-1.

## 0.11 — a sequencer page per instrument, 8 patterns each, conditions, retrigs (2026-10-24)

- **One sequencer page per instrument:** SEQ opens the instrument you're on (REESE's roll, a break's grid, a MIDI
  instrument's roll from the new MIDI page: GLO for OUT, SETUP, MIDI) and SEQ again goes back. The 8-track OVERVIEW is
  gone, with its track knob (an instrument is chosen on its own pages). The tab row shows the instrument, its 8
  patterns and pattern memory.
- **8 patterns per instrument,** changed at the playing one's end (stopped: at once): SAVE + white key 1–8; copy /
  paste a pattern: SAVE + black key 10 / 11. Kept compact (only the notes that are on, ~1 680 across all 64) in spare
  settings storage, so the three sample slots stay yours. A 0.10 pattern opens as each instrument's pattern 1.
- **Steps grow:** velocity, a Digitakt trig condition (%, FILL, PRE, NEI, 1ST, LST, A:B and their opposites; HOME
  held = FILL), and retrigs (1/1 … 1/80, a length, a velocity fade in or out). Hold a step: NOTE LEN VEL COND; FX:
  SLIDE ACC RETRIG RATE; FX: RT LEN RT VEL. A velocity ramp: hold a step, hold REC, press another; KNOB 3 / 4.
  Notes show their velocity (brighter: louder), a condition (red corner) and retrigs (ticks).
- **VEL SENS** (SETUP, where the OCT dial was; OCT− / OCT+ as ever): velocity to REESE's level (0, the default, as
  before). Breaks and MIDI take each note's velocity.
- **SAVE + FX on a break's grid** lays the break straight (each step its own slice).
- The web editor: the selected note's velocity, condition, retrig (rate, length, fade); shift-drag the velocity lane for
  a ramp; each instrument's 8 patterns (click to queue); conditions named exactly as on the FM-1 (tested); still works
  with an FM-1 on 0.7–0.10.

Tested on the host: conditions against the Digitakt manual's rules (50 % over 10 000 passes, A:B lists, 1ST, LST,
FILL, PRE after a 50 % trig, NEI, a 12-step track's own passes), retrig timing (1/32, 1/3, 1/80) and fades, cut by the
next note, 0.4–0.10 patterns opening; scenarios `trigs` (held-step pages, ramp, FILL, patterns queued and switched,
copy / paste, kept over a restart, the MIDI page) and `trig_wav` (a snare roll fading in, 50 % hats over 4 bars); every
scenario moved to the instrument pages; the web editor end to end. Not yet tested on a real FM-1.


## 0.12 — projects: any instrument on any track, the FX rack, samples from the editor (2026-10-24)

- **Measured first:** CI now reports the firmware's real size on every build (the `.fwsc` is padded, so its size said
  nothing): 200 KB of code and constants in the 568 KB app slot (381 KB free), RAM 49 KB of 96 KB, the pool 153 KB of
  336 KB. The build fails if the app slot has less than 24 KB free. The flash map is in `docs/PLATFORM.md`: 240 KB
  for samples, untouched by updates.
- **Any instrument on any track:** the new **PROJECT** page (GLO ×5) shows the 8 tracks and what each plays. KNOB 1
  the track, KNOB 2 an instrument (OFF, REESE, BRK A, BRK B, MIDI; red until it's in), FX puts it on. REESE and each
  break are one each: given to another track they swap. A track's patterns stay with it; overlaps slide on the synths
  and MIDI, never on the breaks, whichever tracks they're on. Saved with the sound.
- **SEQ held + white key 1–8** picks track 1–8 and opens its instrument's page (on a sequencer page, its sequencer
  page). SEQ now acts as it comes up.
- **The FX rack:** every track has SEND A and SEND B (the FX page, GLO ×2: MIX), into two effect slots with their
  RETURNs. The first effect is a tempo-synced ping-pong **DELAY** (1/32 … 1/2, dotted and triplets) run at half rate
  for a darker, dub-like repeat and 1.1 s of memory, FEEDBACK past 100 building into a howl that never clips, TONE in
  the loop, and a new TIME gliding there like tape. The two slots share one 144 KB memory arena; an effect that won't
  fit is refused with a message (two delays don't). Reverbs come next.
- **The voice pool** for the coming polyphonic engines (FM, JUNO, WAVE): one pool, a voice limit per track, a new note
  taking the oldest released voice, then the oldest held (`src/dsp/dn_voice.c`, tested).
- **The web editor's Project tab:** each track's instrument and sends, the FX rack, memory and CPU meters; **samples**
  dropped in (WAV, MP3, AIFF…) go on the FM-1 in mono (L+R, left, right or side) at a rate you pick (44.1 kHz down to
  8 kHz, 26 kHz for the SP-1200's sound) through a proper anti-aliasing filter, with each one's size, a ▶ to hear it as
  the FM-1 will, and **Fit to space** lowering the longest first; and **projects**: one `.dnproj` file with
  everything (the sounds, tempo, layout, all 8 patterns of every track, the samples, their slices, and the original
  files so they can be converted again), saved to the computer or a projects folder, and loaded back (only what
  differs is sent: a cut-off load carries on when run again). Editor commands 75–77 (PROJ, PROJ_SET, PROJ_NAME).
- **CI:** a published release is never rebuilt from later code on main (a work-in-progress build had replaced 0.11's
  file; it has been rebuilt from 0.11's own commit). Bump VERSION for a new release.

Tested on the host: `project` (instruments swapped and kept, SEQ + key, the delay heard after a send, a second delay
refused, the editor commands), `fx_test` (the delay's timing to the sample, feedback ratio, decay to silence, never
wrapping at full feedback, no click on a time change; the voice pool's stealing rules), all earlier scenarios; the
editor: `media` (folds, aliasing below −40 dB, ADPCM, fitting, slot images with a rate per zone, send / read back),
`project` (a whole project from one simulated FM-1 to another), and the end-to-end run in Chromium. Not yet tested on
a real FM-1.

## 0.13 — SUB: an SH-101-style bass with dirt (2026-10-24)

- **SUB**, a new instrument: the SH-101's voice as its panel lays it out — one oscillator with a saw, a pulse (its
  width by hand and moved by its own triangle: PWM) and a square sub an octave or two down (or the 25 % pulse two
  down), noise, each on a mixer level; a resonant 4-pole low-pass that self-oscillates in tune (within half a cent an
  octave over three octaves, KEY at full), with envelope amount and keyboard tracking; one ADSR for the filter and the
  amp, or the amp on the gate alone (VCA GATE); portamento OFF / AUTO (legato only) / ON; ACCENT from the sequencer's
  accented steps opening the filter and lifting the level.
- **The dirt, for jungle:** after the amp, DRIVE (SOFT, HARD, FOLD, RECT), CRUSH (rate and bits down), TONE (a low-pass to
  tame it), and **SPLIT**: below its frequency the signal goes round the dirt, so the sub stays whole under the
  heaviest drive (measured: the fundamental within 0.1 dB of clean under HARD at full drive; 8 dB off without it).
- 12 factory sounds: DREAD, DONK, ROLLER, WOBBLE, ACID 101, 808 SUB, GROWL, SQUARE SB, PUNCH, TECH SUB, CRUSHED,
  RAVE STAB, levelled against each other. LEVEL is the output after the dirt (so it changes the volume, not the
  drive).
- Put it on a track on the PROJECT page (or the editor's Project tab); SEQ + its track's white key opens its pages:
  the same buttons as REESE's (OSC, FILTER, AMP, MOD, OUT), SEL again for OSC2, EDIT again for DIRT. The sound pages
  follow the synth on the selected track (REESE or SUB); from a break or MIDI track they go to the synth shown last.
  PRESETS loads SUB's sounds with its track selected. VEL SENS (SETUP) counts for both synths. SUB has its own LFO
  destinations (S CUT, S RES, S DRIVE, S PITCH, S LEVEL, S PW, after L4 DEPTH): the first 0.13 build sent REESE's
  CUTOFF, LEVEL... to SUB as well, so an LFO set up for REESE made a held SUB note pulse; fixed the same evening.
  On SUB's pages the LFO page's DEST goes through SUB's own destinations — S CUT, S RES, S DRIVE, S PITCH, S LEVEL,
  S PW, S ENV, S SUB, S CRUSH, S TONE, S PULSE, S NOISE — then the breaks', then the LFO depths (on REESE's pages,
  REESE's), the knob's pointer moving along that list from start to end; each is tested to change the sound. Fixed on the way: SUB's ENV (and REESE's FENV, since 0.3) moved the
  cutoff ~64 times less than meant (63 now sweeps 63 steps, ~4.5 octaves); SUB's factory sounds re-voiced for it;
  TONE now sweeps 120 Hz .. open; NOISE louder.
  Its notes start without a click: the attack is never quicker than ~3 ms and the saw starts at its zero crossing
  (the click at the start of a note measured 3-10x lower on the snappy sounds).
- Saved with the sound; the web editor has a **SUB** tab (every value, the factory sounds) and SUB in the Project tab's
  instruments. Editor: PARAMS ends with SUB's 27 values; NAMES kind 5; command 78 SUB_PRESET.

Tested on the host: `sub_test` (the oscillator and the sub at pitch to 0.3 Hz, the filter ringing in tune and
tracking the keys, SPLIT under drive, accent, the gate VCA, AUTO portamento, all 12 sounds in range, and
`build/host/sub_presets.wav`, each sound on a jungle bass line to listen to), the `sub` scenario (on a track, its
pages, PRESETS, the keys and the sequencer playing it, kept by SAVE), every earlier test, and the editor's end-to-end
run. Not yet tested on a real FM-1.

## 0.14 — envelopes locked to the tempo; SUB's notes retrigger the LFOs (2026-10-24)

- **ENV SYNC:** on a synth's AMP page (REESE or SUB), FX locks its envelope to the tempo: ATK, DEC and REL become
  note lengths (1/64, 1/32T, 1/32, 1/16T, 1/16, 1/8T, 1/8, 1/4T, 1/4, 1/2T, 1/2, 1 BAR, 2, 4, 8 BARS; up to 10 s)
  and follow the BPM; FX lit while it's on, FX again frees them (ms). Each synth keeps its own; saved with the sound.
  REESE's filter envelope follows its amp's times, synced too.
- **The LFOs were already tempo-synced** (the LFO page, FX: MORE, SYNC; 8 bars … 1/32 with dotted and triplets, the
  default) and could already restart on a note (MODE: RETRIG, or 1-SHOT): now **SUB's notes restart them too**, not
  only REESE's (a note sliding in with a key held doesn't).

Tested on the host: `sub_test` (ATK on 1/4 takes twice as long at 60 BPM as at 120, and as long as the same time free),
the `sub` scenario (FX on AMP, the knobs reading note lengths, kept by SAVE; a SUB note restarting an LFO on RETRIG),
every earlier test. Not yet tested on a real FM-1.

## 0.15 — sounds keep their LFOs; INIT; 8 user sounds a synth (2026-10-24)

- **Each synth has its own 4 LFOs**, part of its sound: REESE's and SUB's no longer share them. Picking a factory sound
  sets that synth's LFOs (off, but WOBBLE's: 1/8 on S CUT, retriggered); the LFO page shows the selected track's synth.
- **INIT**: the last factory sound on REESE and SUB, a plain starting point.
- **USER 1–8 for each synth**, after the factory sounds on PRESETS. **SAVE held + PRESETS** picks the slot, letting go of
  SAVE stores the sound there (its values, LFOs with their drawn shapes, ENV SYNC) and saves at once. Kept over a power-off.
- Editor: SUB's LFOs on the LFOs tab, the user sounds in the preset lists, projects carry all 8 LFOs.
- Older saves load as before (SUB's LFOs start off, the user slots empty).

Tested: host scenario `patches.txt` (a preset change resets that synth's LFOs, store / recall / empty slot, over a
reboot, REESE's and SUB's slots apart), the editor's tests and its end-to-end run.

## 0.16 — the effects: Carnac's echoes and reverbs, QUADRA, MASSIVE (2026-10-24)

- **Eleven effects for the two FX slots.** From Carnac OS: **TAPE** and **BBD** (wow, flutter, DEGRADE, ping-pong, a
  runaway past FEEDBACK 100), **SPRING**, **TOMB**, **MOD**, **CELESTIAL** (shimmer), **DOLMEN**, **TUMULUS**. New:
  **QUADRA**, Quadraverb-style chains (CHORUS, REVERB, 5EQ>REV, FLANGE, DETUNE, DLY>REV) with a 16-BIT grain switch.
  **MASSIVE**: Jamie's Super-Massive (Horizon) with its ten modes, at half rate. DELAY stays.
- **Seven values an effect**, on two views of the FX page (FX: MIX, FX A, FX A 2, FX B, FX B 2). TIMEs synced to the
  tempo; the reverbs get PREDELAY and a LOW CUT for the sub.
- **FX memory 176 KB** (was 144): DELAY beside any reverb, two big reverbs together. MASSIVE takes what the other slot
  leaves; its TIME shows in ms when the lines can't hold the division asked.
- Every effect's send is limited (no clipping inside a reverb on a loud chord); DELAY's tail now ends in exact silence
  (it left ±1–3 LSB).
- Editor: every effect and value on the Project tab; projects keep all seven values. Saves from 0.15 load with their
  effects as they were.

Tested: `fx2_test` (each effect's memory, host cost, a golden render, its tail dying to silence, a hot 8-voice chord
no harsher than a quiet one, TAPE / BBD / QUADRA echoes and MASSIVE's ORBIT on the beat, MASSIVE fitting TIME to its
memory and changing mode without a click, ABYSS held at FEEDBACK 127, CELESTIAL's octaves climbing), host scenario
`fx.txt` (the panel, memory refusals, kept over a reboot), the editor against the firmware's names and texts for every
value. Not yet measured: the CPU load on the FM-1 (the PROJECT page shows it).

## 0.17 — the buzz: QUADRA's levels, a master limiter, the load on show (2026-10-24)

- **QUADRA was far too loud** (Jamie: "a buzz sometimes, like it's distorting"): its chain summed to 2–4x full scale
  on a busy mix (FLANGE worst, its regeneration clamping), and the master's soft knee clipped it. Now it takes its send
  at half, the chorus / flange / detune sit under the dry, the plate's level suits each configuration, the delay
  saturates softly instead of clamping, and a new CONFIG starts from its own values (5EQ>REV flat).
- **A master limiter** before the soft knee: loud peaks are turned down (Carnac's law: instant on a peak, back in
  ~0.15 s), not clipped.
- **AUDIO LATE: CPU FULL** on the status line whenever the audio misses its time (a buzz of its own); the PROJECT page
  shows the CPU load's peak beside it.

Tested: every QUADRA configuration's peak on a loud mix (the 0.4 demo +3.5 dB, full return: 95 000 → under 30 000 at
the defaults; no clamps), host scenario `loud.txt` (REESE and both breaks sent full to CELESTIAL and MASSIVE at full
return: peaks held at the limiter's ceiling, the knee untouched), QUADRA's golden render renewed.

## 0.18 — the FX list no longer sticks on "NO MEMORY" (2026-10-24)

- Turning KNOB 1 through the effects now skips any effect that won't fit beside the other slot's, instead of stopping
  on it. With DELAY on FX A, FX B's list went OFF → DELAY (no room for two) and stuck there, so TAPE, the reverbs,
  QUADRA and MASSIVE couldn't be reached (Jamie: "a 'no memory, FX off first' warning"). The warning now only comes
  when nothing further along fits. The editor's effect menu still refuses one that doesn't fit, as before.

Tested: host scenario `project.txt` (FX B up from OFF beside DELAY lands on TAPE, down again on OFF), the editor's
refusal unchanged.

## 0.18 — the FX list no longer sticks on "NO MEMORY" (2026-10-24)

- Turning KNOB 1 through the effects now skips any effect that won't fit beside the other slot's, instead of stopping
  on it. With DELAY on FX A, FX B's list went OFF → DELAY (no room for two) and stuck there, so TAPE, the reverbs,
  QUADRA and MASSIVE couldn't be reached (Jamie: "a 'no memory, FX off first' warning"). The warning now only comes
  when nothing further along fits. The editor's effect menu still refuses one that doesn't fit, as before.

Tested: host scenario `project.txt` (FX B up from OFF beside DELAY lands on TAPE, down again on OFF), the editor's
refusal unchanged.

## 0.19 — a filter envelope for REESE and SUB (2026-10-24)

- **F.ENV**: press ENV on the AMP page again for the filter envelope, F.ATK, F.DEC, F.SUS, F.REL, its own from the
  amp's (ENV again: back to AMP). Its amount stays where it was: FENV on REESE's MOD page, ENV on SUB's FILTER page.
  A plucky filter on a held note, or a slow filter swell under a fast amp. ENV SYNC (FX) syncs its times too.
- A factory sound, an older save and the user sounds from 0.15–0.18 start it as they always sounded: REESE's as the
  amp's attack and decay with no sustain and the amp's release, SUB's as the amp's (the 101's one envelope). The
  factory sounds render bit for bit as before.
- Kept by SAVE and in the user sounds; on the editor's REESE and SUB tabs (FILT ENV), through a new command (79) for
  the values past the old 7-bit list.

Tested: SUB with the amp held and F.SUS 0 (the 8th harmonic falls 36 dB, the fundamental within 2.3 dB) and F.SUS 127
(it stays); every REESE and SUB factory render identical to 0.18's; host scenario `fenv.txt` (the page, a factory
sound's values, USER 1 and a reboot keeping them, SUB's apart); the editor against the firmware's PARAMS_HI.

## 0.20 — no AUDIO LATE warning on SAVE (2026-10-24)

- Writing to the flash (SAVE, a user sound, chops and slices kept, samples from the editor) pauses everything for a
  moment, the audio too: it always has. Since 0.17 that pause showed as "AUDIO LATE: CPU FULL" (Jamie: on every save).
  The warning now leaves out the moments around a flash write; it still comes when the CPU really runs out.

## 0.21 — updates keep your project (2026-10-24)

- **An update no longer clears the project** (Jamie: "every time I update the FW it clears my project"). The saved
  sound and project grow at the end when a version adds something; the loader took only an exact list of the older
  lengths, and the list was 2 bytes short of each (a save is padded to 4 bytes). So a save from 0.14, 0.15 or
  0.16–0.18 was turned away after updating and the FM-1 started from its factory project. Now any save from 0.4 on
  loads, each part read if the save reaches past it.
- The host tests keep a save made by each release since 0.14 (`tests/host/saves/`) and load them all on every build
  (`upgrade.txt`), so this can't come back unseen.

Saves made on 0.19 and 0.20 were never affected. A project lost to an earlier update can't be brought back by this
(it was replaced at the next SAVE); a project saved to the computer from the editor's Project tab can be loaded again.
