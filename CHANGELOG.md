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

