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

## 0.22 — pick an instrument with HOME + SELECT; name the project and your sounds (2026-10-24)

- **HOME held + SELECT** steps through the 8 tracks, straight to each instrument's pages (the status line names it:
  "T3 BRK B"). On its own HOME does what it did, on letting go: holds a note on a sound page, a rest in ROLL's KEYS,
  FILL while held on a sequencer page (not while picking).
- **Names.** SAVE held + FX on PROJECT names the project (11 letters); on REESE's or SUB's pages it names the user
  sound loaded (8 letters). Storing a sound (SAVE + PRESETS) opens the name editor at once, with the name of the sound
  it came from. KNOB 1 / SELECT the place, KNOB 2 / PRESETS the letter, SAVE keeps (and saves), HOME cancels. The names
  show on the top bar, in PRESETS and in the editor's lists; they're kept with the manual slices (older saves load).

Tested: host scenario `names.txt` (picking tracks and their pages, naming and cancelling, a stored sound's name, all
over a reboot); `patches.txt` and `fenv.txt` updated for the editor that opens after storing.

## 0.23 — SAMPLER: 16 one-shot pads, or a key map with loops (2026-10-24)

- **SAMPLER**, a new instrument (PROJECT: put it on a track, one at most). It plays the same samples BREAKS does: the
  built-in loops and yours from the editor's Project tab.
- **ONE-SHOT** (MODE on OUT): 16 pads, DRUM's rows and the first 16 keys. Each pad: SAMPLE, START, END, REVERSE (PAD) ·
  CUT, RES, DRIVE, CRUSH (FILTER) · ATTACK, HOLD (127: to the end), DECAY, LEVEL (AMP) · PITCH ±24, FINE ±50 cents,
  PAN, CHOKE group 1–4 (MOD). A hit cuts the pad's last hit and its choke group, fading out in about a millisecond, so
  nothing clicks. PRESETS picks the pad the pages show; playing one picks it too. SEQ: its pads on DRUM.
- **KEYS**: a key map of 8 zones (ZONE page): SAMPLE, ROOT, LOW, HIGH; LOOP page: LOOP OFF / LOOP (round while it
  sounds) / SUST (round while the key's held, then on to the end), L.START, L.END (snapped to rising zero crossings: no
  click where it goes round), TUNE; START and LEVEL. One ADSR, filter, drive and crush for the keys; up to 4 voices
  (VOICES 1: mono). PRESETS picks the zone; SEQ: its piano roll. No LFO page (yet).
- **Editor:** a SAMPLER tab: the 16 pads (click one to pick it and hear it), its values; in KEYS the zones drawn over
  a keyboard (click a row to pick a zone, the keyboard to hear a note). Projects saved from the editor carry SAMPLER.
- SAMPLER's settings are kept with the manual slices and the names (that object now holds 20 loops' slices, the
  newest; was 24). Saves from 0.22 and earlier load as they were.

Tested: `smp_test` (pitch across the map and at zone edges, TUNE, START / END, reverse, choke, voice stealing, mono,
no step where a loop goes round, SUST, release); host scenario `sampler.txt` (pads, choke, pages, DRUM, keys, the
editor's frames, kept over a reboot); `upgrade.txt` (a 0.22 save: names and slices come up, SAMPLER at its defaults);
the editor's value names, ranges and texts checked against the firmware's; the editor end to end in Chromium (pads,
zones, mode) and a project carrying SAMPLER from one FM-1 to another.

## 0.24 — ×2: copy a bar or two into the empty bars (2026-10-24)

- **SAVE held + REC** on a sequencer page (ROLL or DRUM): the bars with notes are copied into the empty bars after
  them. A 1-bar track becomes 2 bars, pressed again 4; one bar of notes in a 2- or 4-bar track goes to bar 2, two bars
  to bars 3–4. The status line says what it did ("COPIED: 2 BARS"); "FULL: 64 STEPS" when there's no room. REC isn't
  toggled and SAVE doesn't save when used this way.

Tested: host scenario `double.txt` (empty track, 1 → 2 → 4 bars, full, one bar in a 4-bar track, SAVE still saving).

## 0.25 — FM: a six-operator DX7-style synth that plays DX7 voices (2026-10-25)

- **FM**, a new instrument (PROJECT: put it on a track). Six operators, the DX7's 32 algorithms, feedback, its
  envelopes (4 rates, 4 levels) with their real timings, keyboard level and rate scaling, velocity, the LFO with
  PMD / AMD and its delay, the pitch envelope. The engine is a C port of Dexed's MSFA (Apache-2.0, credited in
  docs/UPSTREAM.md), so voices sound as they do in Dexed. Up to 6 voices (VOICES 1: mono, legato); the keys play chords.
- **Pages:** VOICE (ALGO, FEEDBACK, BRIGHT, TIME), OP / OP+ (the operator, its ratio or fixed Hz, level, detune,
  velocity, mode), EG / its levels, LFO / LFO+, OUT (LEVEL, VOICES, ATTACK, RELEASE). The four macros sit on top of the
  voice without changing it.
- **Voices:** 10 factory voices of DREADNOUGHT's own (DUB FM BS, FM REESE, HOLLOW STB, DARK BELL, METAL HIT, FM PLUCK,
  DARK PAD, GROWL BASS, SINE SUB, INIT VOICE), no Yamaha ROM sounds. Your own DX7 banks go on from the editor: on the
  Project tab, Add files takes .syx (32-voice banks and single voices); tick the voices you want, and they go on with
  the samples (128 bytes a voice) and come after the factory voices in PRESETS. They travel with a project file.
- **Editor:** an FM tab with every value of the voice (the operators as carriers or modulators, their frequencies,
  each one on or off), the name, LEVEL, VOICES and the macros; hear it on the FM-1 from the page.
- FM's voice is kept with the slices, names and SAMPLER (saves from 0.24 and before load as they were).
- Fixed: on a SAMPLER (or FM) page, SELECT round to SETUP switched the track to REESE.

Tested: `fm_test` renders 400 random voices over all 32 algorithms, as notes and chords, through DREADNOUGHT's port and
through Dexed's own MSFA (built for the tests, `tests/host/ref`): 332 identical to the sample, every one without amp
mod identical, and all within 0.15 dB in loudness over time (with AMS, Dexed's level step is a double `exp()`, here a
float one, a few LSB apart). The tables bit for bit as MSFA makes them, packed voices round trip, pitch, velocity,
an attack's timing against Dexed's, voices freed after release, mono, the factory voices (golden). Host scenario
`fm.txt` (pages, PRESETS, project voices from a slot, the keys, macros, the editor's frames, kept over a reboot);
`upgrade.txt` (a 0.24 save); the editor's FM names, ranges and texts against the firmware's; the editor end to end
(FM tab, a .syx bank, voices picked and sent).

## 0.26 — the editor as the FM-1, big; hear DX7 voices before sending (2026-10-25)

- **The editor's FM-1 view.** The editor now opens on the FM-1 itself, laid out as the panel is: the screen (the page,
  its tabs, the sound, the four knobs with their names, values and text, the status line, large), SELECT / PRESETS /
  ALGORITHM and OCT on the left, the twelve buttons on the right with their lights, the 27 keys. Everything works as
  on the FM-1 because it is the FM-1's: the page asks what the screen shows (UI_STATE, 87) and presses, turns and plays
  (UI_INPUT, 88), so every page and every instrument, including later ones, works with nothing added to the editor.
  Shift-click holds a button down for the combinations; a tab clicked goes to that page; the computer's keys play.
  A sequencer page offers the Pattern view for its notes. The in-depth tabs are still there.
- **Hear a DX7 voice before sending it.** ▶ next to a bank's voice (Project tab) and "Hear it here" on the FM tab play
  it in the browser, through the FM-1's own FM engine ported to JavaScript; "FM-1" still plays it on the FM-1.
- The FM tab's view no longer jumps back to an older value right after you change one (the panel's and the tab's
  refreshes wait for your change), and the editor's diagnostics keep their first lines (how it connected).
- **Leftovers looked for:** no unused code in DREADNOUGHT's own sources (every function and constant is used), and
  none of X0X's synths, drum kits or sample sets are in the firmware (the build takes only X0X's hardware layer). What
  stays by choice: the two built-in breaks (76 KB of the 268 KB of firmware space left), the loaders for saves from
  0.4 on (small; they're what keeps a project over updates) and the breaks page (the only place to edit slices by hand).

Tested: host scenario `panel.txt` (the screen read, buttons, an encoder, a key, SEQ, all through UI_STATE / UI_INPUT);
the panel's reading of UI_STATE against the firmware's own; the editor's FM against the FM-1's renders: four voices
(detune, amp mod, feedback), every sample identical; the editor end to end (the panel: a page, a knob dragged, a key,
a held button, the computer's keys, a tab, SEQ; ▶ on a bank's voice plays it with nothing sent).

## 0.27 — chords in the sequencer (2026-10-25)

- **Chords.** On FM, SAMPLER's keys and MIDI tracks a step can be a chord of up to 6 notes. Record one by holding a
  key and pressing others: REC on the sound page (stopped: at the cursor; playing: on the step), or ROLL's KEYS mode
  (a key under a held one, where a synth gets a slide). The step keeps the lowest note; the others are a chord shape,
  distances from it, so moving the step's note (KNOB 1 on a held step, or dragging in the editor) moves the whole
  chord. A project has up to 15 different shapes, each usable on any note as often as you like; a shape no step uses
  any more is freed for a new one ("CHORDS FULL: 15 SHAPES" when none is free). The notes of a chord share its length,
  velocity, condition and retrigs. REESE, SUB and the breaks play a chord step's lowest note.
- On the FM-1's roll a chord's notes all show; a held chord step reads `F3+2`.
- **Editor:** the Pattern view shows chords (their notes joined); on an FM, SAMPLER-keys or MIDI track, click another
  pitch on a step to add it to the chord, right-click one of its notes to take it out. Projects saved from the editor
  keep the chord shapes.
- (The shapes are kept with the patterns; patterns from 0.26 load as they were.)

Tested: host scenario `chords.txt` (a chord recorded stopped, its lowest key second, a single note after, played back
on FM's voices, ROLL's KEYS mode, the editor's CHORDS frame, kept over a reboot); the editor end to end (a chord made
and undone on a MIDI track).

## 0.27.1 — record on the sequencer page: REC makes ROLL's keys play (2026-10-25)

- On ROLL with REC on, the keys play notes and record them (held together on FM, SAMPLER's keys or MIDI: a chord),
  instead of being the steps. Jamie held a chord on ROLL and got one note, the others moving it (holding a step and
  pressing another moves its note, as before). REC off: the keys are the steps again, holding and moving as ever. The
  status line says `REC: KEYS`; the key lights show what's played, not the steps.

Tested: `chords.txt` (a chord held on ROLL with REC on; REC off, a key a step again).

## 0.28 — chords from a list (2026-10-25)

- **Chords are now a fixed set of 15:** MIN, MAJ, MIN7, MAJ7, 7, MIN9, MAJ9, ADD9, SUS2, SUS4, 5, DIM, AUG, MIN6, 6,
  built on the step's note (Jamie's idea, in place of each project recording its own shapes). Hold a step on ROLL
  (FM, SAMPLER's keys, MIDI) and turn **PRESETS** to pick its chord, or press FX twice and turn **KNOB 3 (CHORD)**. The
  held step reads `F3m7` by its note and `MIN7` under CHORD.
- Recording still works: with REC on, keys held together go into one step as the chord they make, in any order or
  inversion (A C F: F MAJ on F). When two fit, the lowest note is the root (A C E G: A MIN7). Notes that aren't one of
  the 15 record the first key alone.
- Gone: the project's 15 recorded shapes and "CHORDS FULL". Patterns get that room back for notes.
- **Editor:** a **Chord** list beside Velocity for the selected note (FM, SAMPLER-keys and MIDI tracks); the step's line
  under the roll names its chord. Clicking another pitch on a step no longer adds it to a chord.
- Patterns and project files from 0.27 keep their chords: each recorded shape becomes the chord it was (an inversion
  moves the step's note to the root), and one that isn't in the list plays its note alone.

Tested: host `chords.txt` (recorded chords, an inversion, A MIN7, two notes that aren't a chord, KNOB 3 and PRESETS on a
held step, OFF and 6 at the ends, ROLL's KEYS mode, REC on ROLL, the editor's frame), `upgrade.txt` (a 0.27 save with
recorded chords), the editor's table against the FM-1's, `chordMatch` and the 0.27 project upgrade in the editor's unit
tests, and the editor end to end (MIN7 picked, the chord dragged, None, no list on REESE).

## 0.29 — HOME, jump to an instrument, the roll remembers, DX7 banks on the FM tab (2026-10-25)

- **HOME** is a page now: the eight tracks in a grid, each with its instrument and sound. A dot shows when it plays a
  note; a muted track is dim. Press a white or black key 1–8 to open that track's instrument, or turn SELECT and press
  HOME again. HOME no longer holds a note. On a sequencer page holding HOME is still **FILL**: FILL now starts after
  150 ms, so a quick tap opens HOME instead.
- **SEQ held + a black key 1–8** (OP1…) opens that track's instrument, its sound pages, from anywhere, including a
  sequencer page. SEQ + a white key still goes to its sequencer page there.
- **The roll remembers:** a step you turn on gets the note, length and chord of the last note you put in, edited or
  touched on that track (the breaks and SAMPLER's pads still take their slice / pad).
- **The built-in breaks are gone** (ROLLER and STEPPER): the breaks play only your loops. With none sent they are
  silent, and LOOP reads NONE. Saves and project files from before keep their loops: the numbers move down past the
  two that went. A break that was on a built-in loop goes to your first loop.
- **Sample slot 3 is 64 KB.** Its top 16 KB holds two more settings objects for what's coming (JUNO next). Slots 1 and
  2 stay 80 KB. The editor and the breaks page pack samples to fit, and a project file saved with a fuller slot 3 is
  packed again on its way. A slot 3 sent before 0.29 that reaches past 64 KB keeps playing, and the new settings
  aren't written over it until it is sent again.
- **Editor, FM tab: DX7 banks.** Load a bank (.syx), play each voice in the browser (▶) or on the FM-1 (**FM-1**),
  tick **Keep** on the ones you want, then **Send the kept voices to the FM-1**. They go into a sample slot with your
  samples (read from the FM-1 first, so nothing is lost) and come up on PRESETS after the factory voices.
- CPU: Jamie measured 50% with samples, a break, FM (6 notes), SUB and REESE all playing, so DREADNOUGHT stays on one
  core for now.

Tested: host scenarios `home.txt` (HOME by a tap, keys, SELECT; FILL by a hold; SEQ + black key; a playing dot),
`roll_memory.txt` (note, length, chord carried over; a step tapped off picked up; REESE without chords),
`no_builtin.txt` (old loop numbers, no loops at all, loops back); `store_test` (X0X's storage as built: the two
settings objects only in 0xD8000–0xDBFFF, slot 3's first 64 KB and the panel untouched); HOME's UI_STATE against the
editor; the editor's unit tests (slot 3's size, packing, old projects' loop numbers, repacking) and end to end (the FM
tab's banks: load, ▶, FM-1, Keep, send).

## 0.30 — the light skin (2026-10-25)

- **A new look for the screen, and the default:** a warm white ground, ink text and rules, and one orange accent for
  the fills (the engine's tag, the values' bars, the notes on the roll, the picked track on HOME). A highlight (the
  page's tab, a pattern playing) is ink with white on it. Jamie picked it from the skin mock-ups.
- On the sound pages the four knobs are now **big values with a bar under each** in place of the drawn knobs. Numbers
  are 32 px tall, words 24 px. A value either side of zero (PITCH, DEPTH) fills out from a tick at zero, and an LFO's
  reach is a red line under the bar.
- HOME's tiles have an ink rule on top, and the picked one is orange.
- **SETUP, FX** switches to **DARK**, the look before, and back. It is kept with SAVE (a save from before 0.30 comes
  up LIGHT).

Tested: host scenario `skin.txt` (LIGHT from the start, DARK by SETUP + FX, kept over a reboot, back to LIGHT, an old
save LIGHT); every page's screenshot looked at in both skins.

## 0.30.1 — the editor: samples read and sent safely (2026-10-25)

Jamie lost his samples from the FM-1, and loading a saved project didn't put them back. His diagnostics showed why:

- Every press of **Connect** opened a new link to the FM-1 without letting go of the old one, so each frame from the
  FM-1 was handled by every link (12 copies in his log). That slowed the page until requests timed out. The project
  load stopped at a SAMPLER value, before it reached the samples.
- A sample slot was read 224 bytes at a time, and each answer was taken as long as it was the right *kind* of answer.
  After a timeout, the late answer to the first try was taken for the next piece, so the read shifted. Sending samples
  after that (the Project tab, or the FM tab's kept DX7 voices, which read the slots first) could write the shifted
  copy back.

Fixed in the editor and on the breaks page:

- A new connection closes the one before.
- A slot read or write only takes the answer for its own slot and place. Late copies of earlier answers are passed
  over.
- Each slot read back is checked against the slot's own CRC (read again once if it doesn't match). If it still
  doesn't match, nothing is written and the editor says so.
- A request that goes unanswered is asked again (up to 3 times) before giving up. Playing a note, a panel press and
  PLAY / STOP are never sent twice.

The FM-1's firmware is unchanged from 0.30.

Tested: editor unit tests (stray copies of earlier answers passed over and the samples read back as sent; a changed
byte in a slot refused by its CRC; a lost answer asked again; a note never sent twice; a closed link lets go of the
port), the editor end to end, the flasher's.

## 0.31 — BLOCKS, the colour-block skin (2026-10-25)

- **A new skin, and the default: BLOCKS** (Idea D from the mock-ups; Jamie found LIGHT hard to see). The ground is
  near-black and the text cream. Each knob's value is a big tile in that knob's colour, wherever it is: K1 orange, K2
  yellow, K3 green, K4 blue. The tile fills from the bottom as far as the value, with the rest pale. A value either
  side of zero fills from ticks at zero, and an LFO's reach is a dark line down the tile's right edge.
- **HOME** in BLOCKS: each track a tile in its instrument's colour (REESE orange, the breaks yellow, SUB green, FM
  blue, SAMPLER lilac, MIDI grey). The picked track is outlined; a muted one is dark with MUTED.
- **SETUP, FX** goes round BLOCKS → LIGHT → DARK, kept with SAVE. 0.30 saves on LIGHT, its default then, come up
  BLOCKS.

Tested: host `skin.txt` (BLOCKS from the start; round the three; each kept over a reboot; an older save BLOCKS); every
page's screenshot looked at in BLOCKS.

## 0.31.1 — big pop-up for a held step; text that fits (2026-10-25)

- **A held step's values pop up big.** On ROLL and DRUM, holding a step shows four large tiles over the roll, one a
  knob as they sit (KNOB 1–4), in place of the small line at the bottom. In BLOCKS they are in the knobs' colours and
  fill as far as the value. On top: STEP n, three dots for which of the three pages (FX: the next), or a message.
  - Page 1: NOTE (a chord's name under it), LENGTH, VEL, COND.
  - Page 2: SLIDE (REVERSE on a break), ACCENT, RETRIG, RATE.
  - Page 3: RT LEN, RT VEL, CHORD.
  - A velocity ramp shows FROM, TO, START, END.
- **Text fits its box.** A value in a BLOCKS tile takes the biggest font that fits (the big digits, then 24, 16, 12
  px), never wider than the tile. A long sound name at the top is cut short before PRESETS rather than running over
  it.
- A host check walks every dial on every page of every instrument (FX: every effect, every view), every value across
  its range, and every page's tag: all fit.

Tested: host scenarios `fit.txt` (the walk: none too wide) and `skin.txt` (the pop-up in all three skins); the trigs,
chords, long-notes and ramp screenshots looked at.

## 0.31.2 — the editor: add to what's on the FM-1, see what's there (2026-10-25)

- **Only what changed is sent.** Adding a DX7 bank's voices or a sample after what's on the FM-1 rewrites just the
  slot's first 4 KB (where its header lives) and the new part, not the whole slot. What's there already is left
  as it is. (It was always kept, but it went across again every time.)
- **The FM tab reads what's on the FM-1** when you open it, as the Project tab does. Banks already on the FM-1 show
  with their kept voices; a newly loaded bank goes on after them.
- **Names come back.** The editor remembers the name each sample and bank was sent with (in this browser, by the
  sound itself), so reading the FM-1 shows `mybank` and `kick`, not `DX7 voices` and `Sample 3`. Project files
  keep their own names as before.

Tested: editor unit tests (a sample added after another: only the new sectors written, the first as it was; the
simulated FM-1 now behaves as flash, erasing by 4 KB sector and checking each slot's CRC at the end) and end to end
(read back by name; one more voice from a new bank sent with a few writes).

## 0.32 — WAVE, the wavetable synth (2026-10-25)

- **New instrument: WAVE.** Put it on a track from PROJECT (or the editor's Project tab). Up to 6 voices, each up to
  4 oscillators in unison (DETUNE, SPREAD across the stereo field) and a sine an octave down (SUB). POS moves through
  the table's frames, morphing smoothly between them; WARP bends the phase before the table is read: SYNC, BEND,
  MIRROR, PWM, FM (by a sine at the note) or CRUSH (fewer samples and bits), with AMOUNT. Then a 12 dB filter with
  DRIVE before it, an amp envelope, a mod envelope (on the cutoff with ENV, through the table with POS ENV), an LFO
  (POS LFO, CUT LFO, its RATE). VOICES 1 is MONO, legato, with GLIDE. The keys and the sequencer play chords.
- **Its pages** in the synths' places: **WAVE** (TABLE, POS, WARP, AMOUNT; SEL again: **OSC**, unison and sub),
  **FILTER**, **AMP** (ENV again: **MOD ENV**), **MOD**, **OUT**. PRESETS: 12 factory sounds (SUPERSAW, REESE,
  PPG BELL, VOX PAD, SYNC LEAD, PWM STRNG, GRIT BASS, ORGAN, FM GROWL, BENT KEYS, CRUSHER, INIT). SAVE keeps the sound
  (in the settings space taken from slot 3 in 0.29). On HOME it's pink.
- **8 factory tables:** ANALOG (sine to triangle to square to saw), REESE (a saw with moving notches), PPG (a band of
  harmonics stepping up), VOCAL (A E I O U), SYNC, PWM (50 % down to 4 %), GRIT (a sine folded harder and harder),
  HOLLOW (odd harmonics, drawbars moving). Also in `wavetables/` as Serum WAVs.
- **No aliasing, no RAM:** each frame is kept as 7 versions, each band-limited to what can't alias an octave up
  (Carnac OS's layout), read straight from flash with 4-point interpolation. Measured: a saw at 2.6 kHz aliases
  55 dB down (12 dB down reading the full frame); at C5, 49.97 dB.
- **CPU:** 8 oscillators in all (voices × unison): UNISON 2 plays 4 voices, 3 or 4 plays 2. At its heaviest
  (2 voices × 4, BEND, SUB, DRIVE) WAVE measures about 2.1× FM's 6 voices on the host (the FM-1 will differ: watch
  PROJECT's CPU figure). The filter runs once a voice when there's no stereo spread.
- **Your own tables (editor, WAVE tab):** load a Serum wavetable or a single-cycle WAV; the editor makes it into the
  FM-1's format (bit for bit what `tools/wt_convert.py` makes), draws its frames, plays it at POS, and sends it into
  a sample slot with the samples (at most 64 frames; you pick 64 / 32 / 16 / 8 / 4). It comes up on TABLE after the
  factory tables, in every project, and isn't taken for a loop. A sound on a table that's later removed falls back to
  ANALOG. Projects keep WAVE's sound and find its table again by name.
- The editor and the breaks page now start every zone of a sample slot on an even byte (WAVE reads its tables in
  place); a slot sent before keeps working.

Tested on the host: tuning (A4 440 Hz), aliasing at 2.6 kHz and C5, POS morphing, envelopes, 6 voices and stealing,
mono legato and glide, every warp on every table, the 12 factory sounds in range and sounding, the cost against FM;
the whole app from the panel (WAVE on track 8: PRESETS, the keys in poly, unison and mono, every page and its second
view, a table sent to a slot, SAVE and a reboot, the slots changing under it, HOME, every dial fits its tile); the
editor's replies decoded as the firmware sends them; the editor's converter against the tool on four WAVs (16-bit
Serum, 24-bit stereo of 80 frames, 32-bit float single cycle, no clm chunk: identical); in Chromium, the WAVE tab
(a sound picked, a value moved, a Serum table loaded, drawn, heard and sent, then picked). Not yet tested: on a
real FM-1 — how it sounds and what it costs there.

### The manual, and the editor in the FM-1's colours (2026-10-25)

- **A manual** (`docs/manual/DREADNOUGHT-manual.pdf`, 27 pages, A4) in the BLOCKS skin's colours on a light page, in the
  FM-1's own font: a quick start first (six walks round the box, each a drawing of the FM-1 with numbered arrows to
  what to press, and the screen you should see), then the panel, the screen, tracks and HOME, every instrument's pages
  knob by knob, LFOs, chop mode, the sequencer, the effects, SETUP and skins, saving, the web editor, memory and CPU,
  recovery, and a one-page quick reference. Made by `tools/manual/make_manual.py` from the host tests' screens.
- **The web editor in BLOCKS:** warm paper, ink type and Barlow Semi Condensed (the FM-1's font, shipped with the
  page); the four knob colours as blocks (the panel's knobs each in its colour, the value big under it); orange
  buttons and tabs with dark type; the piano roll's notes in the skin's blue. Light only, as before.

## 0.32.1 — WAVE without the buzz (2026-10-25)

Jamie: "the wavetable sometimes seems a bit buzzy on all oscs, especially with warp on".

- **The buzz on every table:** the level, the position in the table and the filter moved in steps every 32 samples,
  which buzzes at 1378 Hz around the note (loudest on fast envelopes and moving POS). They now glide a sample at a
  time. A fast decay's buzz went from 26 dB down to more than 40 dB down.
- **The warps' noise:** each warp reads the table faster than the note (SYNC up to 8x, PWM and BEND squeeze it,
  MIRROR doubles it, FM sweeps it), but the version read was picked for the note alone, so high harmonics folded
  back as noise. The version is now picked for the warp's top speed. SYNC's restart and PWM's edges are smoothed
  (PolyBLEP). At C5, what's off the note's harmonics: SYNC -10 → -38 dB, PWM -8 → -36 dB, MIRROR -24 → -46 dB, FM -24
  → -50 dB. CRUSH stays rough on purpose.
- WAVE is a little quieter (about 2 dB), so chords leave the master limiter alone more often.

Tested on the host: every warp's aliasing at C5 and the envelope zipper measured (new checks), all the earlier
checks, the whole app. Not yet tested: on a real FM-1.

## 0.33 — WAVE 2: two oscillators, filters, drives, LFOs and a matrix (2026-10-25)

Jamie: "more lfo's and destinations, more filter types and distortions, saturations etc. anything you suggest to
mangle the sound, maybe two waves so they can be fm'ed".

- **Oscillator B:** OSC B ON gives every voice a second wavetable oscillator with its own table (the factory's or
  yours), POS, WARP and AMOUNT, octave, semitones and cents. B LEVEL is how much of it you hear; **B MODE** is what it
  does to A: **FM** (B moves A's phase, MOD AMT how far), **RING** (A x B), **AM**, or **SYNC** (A starts again with
  each of B's cycles, MOD AMT how far above B it plays: up to 4 octaves). Unison goes with it (2 at most with B on).
- **Filters:** LP12 (as before), LP24, HP, BP, NOTCH, COMB (a delay tuned to the cutoff, RES its feedback). KEY makes
  the cutoff follow the note.
- **Drives:** DIST picks SOFT (as before), HARD, FOLD (a wavefolder), RECT (full-wave: an octave up), TUBE
  (lopsided), BITS (fewer bits) or RATE (fewer samples); DRIVE how hard. SAT saturates after the filter. HARD and
  FOLD are worked out by their integrals and FOLD at twice the rate, so they fold back less noise.
- **Modulation:** a third envelope (ENV 3); three LFOs, each SINE TRI SAW SQUARE S&H RANDOM, free in Hz or synced to
  the tempo (4 bars … 1/32T), FREE (one for all the voices) or RETRIG (each note its own). A **matrix** of 8 routes:
  LFO 1-3, MOD ENV, ENV 3, AMP ENV, VEL, KEY, RANDOM to POS A / B, WARP A / B, PITCH, PITCH B, MOD AMT, B LEVEL,
  CUTOFF, RES, DRIVE, LEVEL, PAN, SUB, NOISE, L1 / L2 RATE, DETUNE, each by -64..+63. And NOISE.
- **Pages:** each button pressed again shows the page's next view (the tab names it): SEL WAVE A, OSC A, WAVE B, OSC B,
  B MIX; EDIT FILTER, DRIVE; ENV AMP, MOD ENV, ENV 3; LFO MOD, LFO 1-3, MATRIX (KNOB 1 the slot).
- **20 factory sounds:** the 12, plus FM BELL, SYNC SWEEP, RING DRONE, COMB PLUCK, FOLD BASS, HOOVER, BIT WOBBLE, S&H TWIST.
- **Within the CPU:** what's switched on decides how many voices WAVE plays, so it never costs more than 0.32's
  heaviest: plain 6, UNISON 2 3, OSC B on 3, B and a drive 2 (VOICES is the most it will use).
- Sounds saved by 0.32 come back as they were, the new values as INIT has them. The editor's WAVE tab has every value,
  B's table and the matrix; project files keep B's table by its name.

Tested on the host: each filter (LP24 darker than LP12, HP brighter), every drive at full (sounding, no DC), each B
MODE (RING takes away A's own pitch, SYNC plays at B's, AM keeps it, FM spreads it), the matrix (VEL to LEVEL), a synced
LFO's speed and its text, the CPU budget over 20000 random sounds, the 20 factory sounds, 0.32's aliasing and zipper
checks, a 0.32 save read back; the whole app (every view, MATRIX, B on a slot's table, SAVE and reboot); the editor in
Chromium (B's table and a matrix route sent). Not yet tested: on a real FM-1 — how it sounds and what it costs there.

### 0.33 editor update — a new rate for a sample already on the FM-1 (2026-10-25)

Jamie: "Is there a way with the web editor that we can change the sample rate of a sample already on the fm1?"

- **Samples:** a sample read back from the FM-1 (no original file on this computer) now has its **Rate** menu open.
  Pick another rate and the editor decodes what's on the FM-1, resamples it (low-passed when lowering, as new samples
  are), and sends it at the new rate. It isn't trimmed or faded again, so it stays the same length in time, and a
  loop's hand-set slices are moved to the same places. Lowering frees space; raising it is allowed but gains no detail
  (the editor says so). **Fit to space** can now lower these samples too.
- No firmware change: the FM-1 stays on 0.33.

Tested: media unit tests (decode → resample → send at 11 kHz: same length in time, same pitch, less space; slices
rescaled) and the editor's end-to-end run (a sample read back, set to 8 kHz, sent: half the samples on the simulated
FM-1, its slices moved with it).

## 0.34 — playing in from the sequencer (2026-10-25)

Jamie: "1. quick way to set patten to 1/2/3/4 bars without using the value knob 2. FX knob to select the fx page.
3. when in sequencer page a way to swap to play/record notes manually but still have sequencer screen up ... 4 record
quantise. 5. the bar that shows the rolls progress should show the bars for the longest sequence".

- **Length in bars:** on ROLL and DRUM, hold black key 10 or 11 (the bar keys) and press white key 1–4: the track is
  1, 2, 3 or 4 bars long. The bar keys now move the bar in view as they come up (so they can be held for this).
- **FX held ½ s: the FX page**, from any page; held again there: back where you were. A tap does FX's usual job on
  the page, now as it comes up (with SAVE or a step held, at once, as before).
- **PLAY mode:** FX on ROLL now goes round STEP → KEYS → **PLAY**: the keys play the sound with the roll in view, and
  with REC lit they record live (as REC did on ROLL before). FX on DRUM switches PLAY mode on and off: the keys play
  the slices or SAMPLER's pads, the row following the key, and record with REC lit. In PLAY mode the bar in view
  follows the one playing. FX is lit in KEYS and PLAY.
- **Record quantise:** hold REC on ROLL or DRUM and turn any knob: live notes go to the nearest 1/16 (a step, as
  before), 1/8, 1/4, 1/2 or bar line. The status line shows it (Q 1/4) while recording or in PLAY mode; SAVE keeps
  it (in the settings, older saves come up 1/16).
- **The roll's ruler:** ROLL's top shows the pattern's bars (the longest track playing, or this one when longer),
  the bar playing lit and numbered, so a 1-bar track recorded against 4-bar ones shows which bar of the four you're
  in. The track's notes repeat, dim, across the bars after its own.

Tested: host scenario `perform.txt` (the bar keys set 3 then 1 bar and still page the bars when tapped; FX held opens
the FX page and goes back, a tap still changes the mode; PLAY mode plays without writing; REC + knob sets the grid
without toggling REC; a note played ~3.4 steps into the bar lands on step 5 at 1/4; DRUM's PLAY mode plays and selects
the row without writing; the ruler with a 1-bar roll against a 4-bar break). The ROLL scenarios step through the
new third mode. Not yet tried on the FM-1.

## 0.35 — instruments chosen on HOME (2026-10-25)

Jamie: "i want to remove the track selection in global menu for the 8 slots and add it to the home page, if a box is
selected that has no instrument in it then a pop up window will appear to allow you to choose a new instrument".

- **HOME's instrument picker:** FX on HOME opens it for the selected track: the nine choices (OFF, REESE, BRK A, BRK B,
  MIDI, SUB, SAMPLER, FM, WAVE) as big tiles in their colours. SELECT or KNOB 1 picks, FX puts it on the track and
  opens it, SEQ (or a page button) calls it off. An instrument that's one-of and on another track shows which (T1):
  choosing it swaps the two tracks, as before. Opening an empty (OFF) track, by its key on HOME, HOME again or SEQ +
  black key, opens the picker by itself.
- **PROJECT** no longer picks instruments: it shows the samples' memory, the effects' memory, the CPU and its peak
  as four tiles. SAVE + FX still names the project.

Tested: host scenarios `project.txt` (track 4 to REESE and BREAKS A to track 8 through the picker, the swaps as
before), `sub.txt` / `patches.txt` (SUB put on track 5 from HOME), `home.txt` (a track set OFF, its key opens the
picker, SEQ calls it off, FM put on it opens FM's pages). Not yet tried on the FM-1.

## 0.36 — JUNO and SAMPLER 2 (2026-10-25)

Jamie: "tests done and seem ok, please build the juno and sampler 2".

- **JUNO**, a Juno-60 / 106-style polysynth, six voices (`src/dsp/dn_ju.c`). The DCO: saw and pulse on or off, the
  pulse's width moved by the LFO, by hand or by the envelope (PWM), a square sub an octave down, noise, RANGE 16' 8' 4',
  the LFO on the pitch. The HPF (BOOST, FLAT, 2, 3) on the voices together, as the 106's. A resonant 4-pole low-pass (a
  zero-delay-feedback ladder, two voices' worth computed at once, that self-oscillates in tune) with the envelope (or
  inverted), the LFO, the keys and the velocity on its cutoff. One ADSR for the filter and the amp, or the amp on the
  gate. One triangle LFO with DELAY (still, then fading in after the first key), FREE or KEY, free or synced to the
  tempo. The CHORUS: I, II and I+II at the Juno-60's rates, two delay lines swept in opposite directions for its
  stereo. 17 factory sounds. Pages: SEL DCO / DCO 2, EDIT VCF / VCF 2, ENV ENV / VCA, LFO, GLO OUT. Plays chords;
  SAVE keeps it. Its cost at its heaviest (everything on, six voices, chorus) is ~2.3 x FM's six voices on the host,
  under WAVE's heaviest.
- **SAMPLER 2**: a second sampler on a track of its own, with its own pads or key map and settings, the same pages as
  SAMPLER (SMPL 2 on the top line). The two share the samples' decoder checkpoints, so a sample on both costs no more.
- HOME's picker shows all eleven instruments; one on another track shows that track's number in a dark badge.
- Memory: SAMPLER 2 in main RAM, JUNO in the pool (which keeps its 8 KB headroom); WAVE's and JUNO's save buffers
  now borrow the sound's save scratch, SAMPLER's sample table is built on the stack, JUNO's chorus line is 16-bit. JUNO
  and SAMPLER 2 are kept in the second settings object (slot 3's top), as WAVE is in the first.
- **Web editor:** a JUNO tab (every value, the factory sounds, keys to hear it on the FM-1); the SAMPLER tab picks
  SAMPLER or SAMPLER 2 when both exist; project files keep JUNO and SAMPLER 2.

Tested: `ju_test` (in tune at each RANGE, the sub an octave down, PWM by hand 50 % to 4 %, the saw's aliasing at C7
-40 dB, the filter closing, ringing on its own and tracking the keys an octave an octave, the HPF, the envelope and the
gate silent after, six voices and stealing, VOICES 1, the LFO's delay, the chorus mono off / stereo on, every factory
sound finite and unclipped with six notes, its cost); host scenarios `juno.txt` (picked on HOME, every page, PRESETS,
chords, the sequencer, kept over a reboot) and `sampler2.txt` (SAMPLER and SAMPLER 2 side by side, each its own mode
and samples, the sequencer playing both, the editor's kinds 4-7, both kept over a reboot); the editor's golden checks
(JUNO's values, names, texts and factory sounds as the firmware's); the editor's end-to-end run (the JUNO tab, SAMPLER
2 on the SAMPLER tab). Not yet tried on the FM-1.

## The editor on iPad (2026-10-25)

- **DREADNOUGHT for iPad** (`ios/`): the web editor, the breaks page and the flasher as an iPad app. Safari on iPad has no
  Web MIDI; the app gives the pages the iPad's MIDI ports through CoreMIDI (SysEx, ports coming and going), saves
  downloads to Files (On My iPad › DREADNOUGHT), makes its Projects folder the editor's projects folder, and adds
  SHIFT / DELETE toggles and double tap for touch. GitHub Actions builds it on every change and publishes the unsigned
  `.ipa` on the rolling **ipad** pre-release (for SideStore / AltStore); `ios/README.md` covers Xcode.

Tested: `ios/test/bridge_e2e.py` (the editor through the bridge to the simulated FM-1: connecting, ports coming and going,
SHIFT / DELETE / double tap on the roll, a project saved to the folder, listed and loaded back, a download, the
clipboard); the app built in Actions and started in an iPad simulator in demo mode (its self-test). Not yet tried on an
iPad with the FM-1.

## 0.37 — quick ones: mutes, colours, shifting, copying, ranges (2026-10-25)

Jamie: "I want the drums to instantly stop when mute them in seq window", "The seq window should match the colour of the
instrument", "shift all the notes in the sequencer so that they wrap round to the start", "holding SAVE, pressing white
key 1, then white key 5" (the copy), and the probabilities to drop notes every 2 / 4 / 8 bars.

- **Mutes cut:** muting a track (black keys 1-8 on ROLL / DRUM) stops what it's playing at once: a break mid-loop, a
  pad, a held note.
- **The sequencer in the instrument's colour** (BLOCKS): the notes on ROLL and DRUM, the overview, the instrument's
  heading and the top line's tag take HOME's colour for it (JUNO cyan, WAVE pink, SUB green ...), darker when softer.
- **Shift the notes round:** a bar key (black 10 or 11) held + SELECT moves every step of the track a step later or
  earlier, + OCT− / OCT+ a bar; what falls off the end comes round to the start.
- **Copy a pattern in one move:** SAVE held, white key A held, white key B: pattern A into pattern B. A tap of a white
  key with SAVE still switches pattern (now as it comes up).
- **A range of steps:** hold the first step, hold REC, press the last (the velocity ramp's move): KNOB 1 now sets the
  CONDITION of every note in the range at once (!4:4: they drop out every 4th time round; 1:2 every other), KNOB 2 their
  LENGTH; KNOB 3 / 4 the ramp as before. The status line names it STEPS 3-9.

Tested: host scenario `quick037.txt` (BREAKS A playing, muted: silent at once; REESE's steps shifted 2 later round the
track and a bar back; pattern 1 copied into 5 without leaving 1; a range's condition on its notes only); every earlier
scenario. Not yet tried on the FM-1.

## 0.38 — SONG (2026-10-25)

- **SONG** (GLO six times): rows, each the pattern every track plays (– : it carries on with its own) for 1-16 bars, up
  to 64 rows. KNOB 1 the row, KNOB 2 the track (or white keys 1-8), KNOB 3 its pattern, KNOB 4 the row's bars. Black
  key 9: a row of the patterns playing now, after this one; 10: a copy of this row; 11: this row deleted; black 1-8
  mute as on the sequencer pages. **FX** turns the song on: PLAY plays it from the row picked, each row's patterns
  switched in at its first bar and started from the top (so 1ST and A:B count from there), round to the first row
  after the last. Turned on while playing, the row picked comes in at the next bar. Kept with SAVE (the second settings
  object; 0.36 / 0.37's JUNO and SAMPLER 2 settings are read as before).
- The sequencer calls the app at every bar (before its notes) and can start a track again from the top as if just
  started: the song's two hooks.
- **Web editor:** a Song card on the Project tab (every row's patterns and bars, add, copy, move up, delete, the song
  on / off); project files keep the song.

Tested: host scenario `song.txt` (a row copied, a track on pattern 2 in it, another carrying on; the song on, PLAY: the
patterns switching at each bar and back round; a row of what's playing now; delete; kept over a reboot); the editor's
golden check (SONG_GET as the firmware sends it) and end-to-end run (a row added and changed: the FM-1 has it). Not
yet tried on the FM-1.

## 0.39 — a hit's own FX (2026-10-25)

- **HIT FX** on breaks and SAMPLER's pads: hold a step, FX twice, KNOB 3 (where a chord goes on the other tracks).
  STR 1/2 and STR 1/4 are the Akai way of stretching (S950 / S1000 cyclic time-stretch): the hit is played in 32nd-note
  cycles, each from where the time has got to, the time moving at half / a quarter speed, so the same pitch lasts 2x /
  4x longer with the cycles' grainy warble. STUT32 / STUT64 repeat its first 32nd / 64th for a beat, then let it play
  on. TAPESTOP slows it to a stop over a beat. OCT− / OCT+ play it an octave down / up. Each cycle's jump is
  declicked. Forward and reversed hits both; a break in STRETCH mode keeps its own grains (no hit FX there).
- **COND ON** (KNOB 4 on the same page): NOTE, as ever, or FX: the hit always plays and its condition decides only
  the effect. Set 1:4 for a stretch every 4th time round. On FM, WAVE, JUNO, SAMPLER's keys and MIDI the same switch
  makes the condition decide the step's chord. The condition's tile reads FX IF / CHORD IF when it's on. Kept in the
  condition's top bit; older patterns load as before.
- SAMPLER: a voice that ends inside an audio block now fades from where it ended, not from the block's start (a
  small click at a pad's END, and at the hit FX's end).
- **Web editor:** the note inspector has a Hit FX list on break and pad tracks, and "Condition: the FX only" (or "the
  chord only"). The FM-1 lists the effects (NAMES kind 10); an older FM-1 doesn't show them.

Tested: host engine tests for each effect on a break and on a pad (lengths, pitch inside a cycle, the stutter's
repeat, the tape stop's half speed half a beat in, octaves, joins declicked, the next hit plain again, reversed); host
scenario `hitfx.txt` (the page, a 1:2 condition on the FX only: stretched, plain, stretched); the editor's end-to-end
run (a break step's STR 1/2 and 1:2 on the FX only reach the FM-1). Not yet tried on the FM-1.

## 0.40 — both cores (2026-10-25)

- **The second core renders.** Up to 0.39 everything ran on one of the FM-1's two cores, and a full project reached
  98 % and froze. Now, inside each audio block, the second core renders some of the instruments while the first
  renders the rest, then runs FX B while the first runs FX A. What each instrument costs is timed (the shared 24 MHz
  timer) and the split is weighed again every 128 frames: the heaviest first, each to the lighter core. All the sums
  are integers, so the mix is the same to the bit on one core or two.
- Started as X0X starts it (at power-on, before the audio), with X0X's safety: a unit whose second core misreads
  memory, or a job that doesn't finish, puts everything back on one core until power-off; not after a failed start.
- PROJECT's CPU tile reads **CPU x2** with both cores working, **CPU x1** on one.

Tested on the host: every scenario now runs with a simulated second core (its jobs run when waited for), and eight
of them (FX, JUNO, FM, WAVE, SAMPLER 2, hit FX, breaks, LFOs) are played again on one core: the 11 recordings match to
the bit. Not yet tried on the FM-1: how much it saves depends on the project (one heavy instrument can't be split).

## 0.41 — light effects (2026-10-25)

- Six new effects for FX A / B, each with next to no memory, so they sit beside any reverb or echo:
  - **FILTER**: a resonant state-variable LP / BP / HP / NOTCH with a drive before it and an LFO on the cutoff, synced
    1/16 to 8 bars, SINE / TRI / SAW / SQUARE / S&H.
  - **PHASER**: 4, 6 or 8 allpass stages swept by a synced LFO, with feedback and STEREO (the sides' sweeps apart).
    Its notches form where the wet meets the dry in the mix, so send plenty.
  - **CHORUS**: CHORUS or FLANGE, a swept delay each side up to 20 ms, FEEDBACK for the flanger. 4 KB.
  - **CRUSH**: bits 16 to 1, the sample rate down to 170 Hz, a drive, a tone after.
  - **RING**: a ring modulator, 20 Hz to 5 kHz, swept by a synced LFO; sine or soft square.
  - **DRIVE**: SOFT, HARD, FOLD, TUBE, RECT, a two-pole LOW CUT before it (the sub stays clean: parallel bass
    distortion) and a TONE after.
- They're send effects like the rest: their wet joins the dry sound at the slot's return.
- **Web editor:** the FX lists and values know them.

Tested: each one's golden render, a sound, silence after, a hot input kept sane; FILTER's LP / HP slopes and its LFO's
sweep; the PHASER's notch at CENTER against the dry; FLANGE's delay to the sample; CRUSH's rate; RING's sum and
difference with the input gone; DRIVE's RECT octave and LOW CUT; their names and texts the same in the editor as on
the FM-1; host scenario `light_fx.txt` (from the panel, sounding, kept over a reboot). Not yet heard on the FM-1.

## 0.42 — ten channels over USB (2026-10-25)

- **USB OUT** (the MIDI page, KNOB 4): STEREO (as ever) or **10 CH**. With 10 CH, after SAVE and a restart, the
  FM-1's USB audio has ten channels: 1-8 the tracks, each mono and dry, and 9-10 the FX A + B returns only (L, R), for
  mixing and adding effects in a DAW. The FM-1's own output still plays the whole mix. Class compliant (USB audio 1,
  16-bit, 44.1 kHz, the same resampling to the computer's clock as the stereo stream); the device reports a new
  version with ten channels so a computer doesn't reuse the stereo description it cached.
- The ten channels' ring (40 KB) and the block the mix fills for it (5 KB) come out of the FX memory, which then has
  131 KB: DELAY with an echo, a small reverb or the light effects fits; two of the big reverbs don't (a saved pair too
  big for it starts with FX B off). STEREO keeps all 176 KB.
- RAM: the breaks' hit finder works in one array instead of two (5 KB back), which pays for the bigger USB packets.

Tested on the host: the setting kept by SAVE and read at start; the arena 131 KB; playing, the blocks the USB takes
carry REESE on channel 1, silence on an empty track's channel, FX A's return on 9 and 10. Not yet tried with a
computer: it needs the FM-1 (and a DAW) to show the ten inputs.

## 0.43 — an install cut off can't leave a dead FM-1 (2026-10-26)

- **The update loader writes the firmware's first sector last.** It used to write from the start up, so an install
  cut part-way (a pulled cable, a flat battery) left a valid-looking firmware header over a half-written firmware. The
  FM-1's boot stage starts any firmware whose header is valid, even with an update still pending, so the unit started
  the broken firmware: a black screen, no USB, no update mode (a unit was lost that way on 2026-10-25). Now the
  header is erased before anything else is written and written back last, so a cut install leaves no firmware to
  start: the FM-1 comes back in its update loader and the flasher finishes the job. After Skiff's loader
  (illuminus5), which found the same on the SMK-37 Pro.
- CI cuts a simulated install at about 400 points (between every few flash operations, some in the middle of a
  page): never a valid header over a half-written firmware, the flash head never touched, and each one finishes when
  run again. The old loader failed 399 of them.
- The loader is checked to fit the 7,936 bytes the stock firmware gives it.
- The running firmware stages the loader that comes in the new package, so installing 0.43 already uses it. Keep
  the battery charged anyway.
- docs/RECOVERY.md: what to do when a unit shows nothing at all (the FM-1 Transporter on an RP2040 board), and how
  to write DREADNOUGHT straight back with it.

## 0.44 — DREADNOUGHT as a plugin (2026-10-27)

- **AU, VST3 and a standalone app** (plugin/): the FM-1's app and engines, unchanged, on a simulated FM-1 inside a
  JUCE plugin. The editor is the FM-1's panel: the screen twice its size (pixel-sharp), SELECT, PRESETS, ALGORITHM and
  OCT on the left, K1–K4 and the two rows of buttons on the right (lit as on the FM-1), the 27 keys below; drag or
  scroll a knob, shift-click a button to hold it, the computer's keys play the keys. The window scales.
- The sound is made at 44.1 kHz, as on the FM-1, and resampled (cubic) to the project's rate; notes and the panel are
  taken every 32 samples (under a millisecond).
- The DAW's MIDI: channels 1–8 play tracks 1–8, 9–16 the track shown (BREAKS and SAMPLER's pads from C1). MIDI tracks
  send their notes to the DAW. FOLLOW THE HOST: the sequencer starts and stops with the DAW, at its tempo.
- The project (sounds, patterns, song, settings: the FM-1's flash objects) goes into the DAW's project; the plugin
  does SAVE when the DAW saves.
- CI builds it for macOS (universal), Windows and Linux, checks it (a note at 48 kHz through the resampler, the state
  saved and loaded, the editor drawn; auval for the AU) and adds the zips to the release.
- Tests: the core alone (plugin/core/dnc_test.c, in tests/run_host.sh): quiet at the start, a host note per channel,
  a break's slice, the host's transport and tempo, PLAY on the panel, the state saved, loaded and the same.
- Firmware: SAVE zeroes WAVE's and JUNO's records before filling them, so an unchanged sound no longer counts as
  changed (their padding held the last record's bytes) and isn't written to the flash again.
- For now: one per project; the two built-in loops only; the sequencer starts from its bar 1 when the DAW plays.

## 0.45 — the plugin's own editor: every instrument with the mouse (2026-10-27)

- **The FM-1's panel is gone from the plugin.** No screen copy, encoders, button rows or keyboard: the plugin is laid
  out as a normal instrument, drawn in vectors (sharp on a Retina screen at any size; the window scales from 60 % to
  200 %), light, with big bold type (Barlow Semi Condensed) and each instrument in its own colour.
  - **The tracks** on the left: the eight, each one's instrument (CHANGE: a menu), its pattern, mute.
  - **SOUND**: every value of the track's instrument, in sections as its pages group them (REESE, SUB, FM, WAVE,
    JUNO, SAMPLER, SAMPLER 2, BREAKS, MIDI): knobs (drag, scroll, shift for fine steps, double-click to type a value)
    and lists for the values that are words. FM's six operators as one table (level, ratio, detune, mode, velocity,
    the EG's rates and levels); the LFOs and WAVE's mod matrix as tables too. BREAKS shows its loop with the slices:
    click to hear one, drag a line to move it, double-click for a new one, right-click to take one out, AUTO SLICES
    to go back. SAMPLER shows its 16 pads (or 8 zones): click one to hear it and edit it.
  - **PATTERN**: the track's 8 patterns, its length and the swing; a piano roll (synths) or a drum grid (BREAKS'
    slices, SAMPLER's pads) with a velocity lane; the clicked step's note, velocity, length, slide, accent,
    condition, chord or hit FX and retrig.
  - **EFFECTS**: every track's sends, the returns, FX A and FX B with every value of the effect picked.
  - **SAMPLES**: drop WAV, AIFF, FLAC or MP3 files anywhere on the plugin (or ADD SAMPLES...): each is converted as the
    web editor converts one (mono fold, resampled through a low-pass to the rate picked, trimmed, faded, 4-bit ADPCM)
    into the FM-1's three sample slots (224 KB, so the project fits on the FM-1); hear one, rename it, remove it.
  - **The top bar**: the instrument's presets (and KEEP AS USER for REESE and SUB), BPM, PLAY, FOLLOW THE DAW, and the
    web editor.
- **Automation**: each instrument's main values are the DAW's parameters (REESE's cutoff, resonance, drive, detune,
  filter envelope, decay, glide, level; SUB's, FM's macros, WAVE's, JUNO's, SAMPLER's filter, BREAKS' pitch, decay,
  crush, level: 54 in all), whichever track the instrument is on; turning them on the plugin moves them in the DAW.
- **The web editor still connects** (macOS, Linux): the plugin opens a MIDI port, "DREADNOUGHT Plugin", and the
  editor finds it as it finds an FM-1, for DX7 banks, Serum wavetables and whole `.dnproj` projects (the same files
  as the FM-1's, so a project moves between the box and the DAW both ways).
- The samples and their names are kept in the DAW's project with the rest (state version 3; 0.44's still loads). A
  new plugin starts with ROLLER and STEPPER in its first slot.
- Under it, a C API for the GUI (plugin/core/dnc_api.c) over the app's own values (names, ranges and texts as the
  FM-1 shows them), presets, patterns, samples and slices.
- Tests: every value of every instrument (332) named, in range, set to its top and read back; every automatable value
  (54); presets and user sounds; steps, lengths, patterns; a sample converted, stored, read back and kept by the
  state with its name; too much for the memory refused with nothing changed; BREAKS' slices set by hand; the web
  editor's own code against the core through a pipe (connect, slots, a sample sent, slices, a whole project read and
  written back). dn_shot (no host): a note at 48 kHz, the state, a WAV dropped on the plugin, the web editor's INFO,
  automation moving the instrument, and every page drawn to a PNG (looked at). CI runs pluginval and auval.
