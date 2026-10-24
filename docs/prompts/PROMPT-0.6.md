# PROMPT: DREADNOUGHT 0.6 — the mop-up: slicing, choke, stretch, long notes, piano roll

You are continuing DREADNOUGHT (https://github.com/TwistedGenetics/DREADNOUGHT), the dark jungle / DnB firmware for the
M-VAVE FM-1. Read `README.md`, `CHANGELOG.md`, `docs/UPSTREAM.md` and the code before you change anything; the facts
below are from 0.5 and the code wins where they differ.

Where things stand (0.5):
- Our own app on X0X's platform (submodule, pinned), built by `tools/build_fm1.py`, released by CI on every push to
  main. Host harness `tests/host/dn_host.c` drives the whole app from scripts (`tests/host/scenarios/*.txt`) with
  screenshots and WAVs; `tests/run_host.sh` runs everything. Keep that discipline: every feature below gets a host
  test that measures it, and screenshots of every new page.
- Track 1 REESE (`src/dsp/reese.c`), tracks 2–3 BREAKS A / B (`src/dsp/dn_breaks.c`), tracks 4–8 MIDI out.
- Sequencer `src/dsp/dn_seq.c`: 8 tracks × 64 steps, a step = note, flags (on / slide / accent), gate `len` in
  sixteenths **of one step** (1..16), vel. Pattern saved in one storage object (`OBJ_PAT0`, 3840 bytes max).
- BREAKS cuts a loop into 16 equal slices a bar, speed locked to the tempo (pitch follows), reverse from a 4096-sample
  RAM buffer, ADPCM seek via a decoder-state table every 256 samples. Loops: 2 built-in + user loops in X0X's three
  80 KiB sample slots (IMA ADPCM, 22050 Hz), uploaded from `web/breaks` (X0X's page, adapted).
- Hardware: JieLi pi32v2, two cores at 360 MHz, single-precision FPU, ~512 KB SRAM (X0X's `.pool` holds the big
  buffers), 1 MB flash, 240×240 screen, 27 keys (16 white = steps on SEQ), 7 encoders, 14 buttons, 44.1 kHz.
  The second core is idle in DREADNOUGHT so far (`plat_cpu2_*`, as Carnac OS uses it).

The user's own words for this round: "I would like to manually slice the break if possible. I would like mute groups
for the two breaks. Time stretching for the break, pitch at the very least but if processing power (with everything
else we want to do synth wise) will allow. Also the reese engine I noticed that the notes don't hold when the length
is set, I would also like the extended notes to show on the sequencer. Maybe have separate sequencer pages for the
synths and drums. The sequencer pages could then resemble and work like a traditional piano roll (still with options
to slide etc)."

Work in this order. After each part: host tests green, CI firmware build green, screenshots, a short note to the user.
Stop and ask before anything that breaks saved patterns or changes a control the user already knows, unless this
prompt already says how.

---

## 1. Long notes (the REESE bug) — fix first

**What's wrong:** a step's gate is `len` sixteenths of *one* step, so the longest note is one step; a note can only
last longer by tying (slide) into the next step. The user sets a length and expects the note to hold.

**Do:**
- Note length in steps: 1/16 step up to 64 steps (a whole pattern), in a unit fine enough for short stabs and long
  holds. Suggested: `len` in 1/4-step ticks, 1..255 (a quarter step to ~64 steps), or a separate `steps` + `frac`.
  Pick one, justify it in the code comment, keep the struct 4 bytes a step if you can.
- The sequencer holds the gate across following steps (and across the pattern's wrap) until the length runs out;
  a later step on the same track cuts it (mono) — or, with SLIDE on that later step, glides into it as now.
- KNOB 2 on a held step: length, accelerated (fine near short values, coarse above a step). Show it as steps + beats
  ("1/4", "1", "2", "4 1/2"…).
- **Migrate** 0.5 patterns: the pattern magic goes to "DNQ2"; a "DNQ1" pattern loads with each `len` converted
  (old len 16 = one step). Never drop a saved pattern on the floor.
- Tests: a 4-step note on REESE sounds for exactly 4 steps (gate measured in samples against the tempo); a note
  across the wrap; a later step cutting a long note; a slide into a long note; the migration of a 0.5 pattern.

## 2. Separate sequencer pages: a piano roll for the synths, a grid for the drums

Keep the current SEQ grid (it is the overview: 8 tracks, mutes, the playhead) and add two editors:

### 2a. ROLL — the synth tracks (REESE now; DREAD, PADS, STABS, 808 later; MIDI tracks too)
A traditional piano roll on 240 × 240:
- Pitch up the screen (an octave and a half in view, scrolling; C rows marked), time across (16 steps in view,
  scrolling by bar), notes drawn as bars **as long as they last** — the long notes from part 1 are visible at a glance.
  Slides drawn as a ramp joining the two notes; accents brighter; the playhead moving; the selected note outlined.
- Below the roll: a velocity / accent lane, and the selected note's values (pitch name, start, length, vel, SLIDE).
- **Controls (propose the final map, keep it learnable):**
  - white keys = the 16 steps in view (as on SEQ); **hold a step key and press a black-or-white note key?** — no: the
    FM-1 has one keyboard, so pick between two clean schemes and say which you chose and why:
    (A) *step mode*: white keys are steps; hold a step and turn KNOB 1 pitch, KNOB 2 length, KNOB 3 accent / vel,
        KNOB 4 slide; a tap toggles the step; or
    (B) *keyboard mode*: the keys are notes; SELECT moves a time cursor; pressing a key writes that note at the
        cursor with the current length and moves on (step entry); holding one key while pressing another writes a
        slide; KNOB 2 sets the length of the note under the cursor.
    Offer both if it stays simple (a button toggles them, the screen says which).
  - REC + PLAY: live record into the roll with the lengths actually held (quantised to the grid, length to the
    nearest tick) — today live recording writes a fixed length.
  - Copy / paste a bar; transpose the track (KNOB with a held modifier); clear.
- Polyphony: REESE is mono, so the roll is mono for it (a new note cuts the old). Design the data so a later
  poly engine (PADS, STABS) can put chords in the roll: say how many notes a step can hold and what it costs in
  pattern storage (3840 bytes an object — use `OBJ_PAT1` too, or `OBJ_SONG` / `OBJ_MOTION`, which DREADNOUGHT does not
  use yet; check `plat.h` for their flash places).

### 2b. DRUMS — the break tracks (and the KIT / 808 tracks to come)
- Rows are what plays (for a break track: its slices, 16 / 32 / 64 rows, scrolling; later for KIT: the drum
  voices), columns are steps. A hit is a cell; its length a bar if it is longer than a step.
- Per hit: slice, length, accent, reverse, and (from part 4) pitch per hit. Show the slice's waveform for the
  selected row in a strip so the user sees what they are placing.
- Both break tracks on one page (A over B) with the choke group (part 3) shown.

Navigation: SEQ cycles OVERVIEW → ROLL → DRUMS (or SEQ = overview, SEQ held + another button = the editors); show the
page name in the tab row. Write the control map into the README table and the app's header comment.

## 3. Mute (choke) groups for the breaks

- Each break track (and later each drum voice) gets a CHOKE group: OFF, 1, 2, 3, 4. A hit in a group cuts every
  other voice sounding in that group (with the existing declick), the way an open hat is cut by a closed one.
- Default: BREAKS A and B both in group 1, so layering two breaks behaves like one sampler voice; the user can
  set OFF to layer them freely.
- Where: on the BRK page (a fifth value — the page has four dials, so propose where it goes: a second BRK page,
  or a SHIFT layer) and shown on the DRUMS page.
- Saved with the sound (the save struct grows: keep accepting the 0.4 and 0.5 sizes).
- Test: A and B in group 1 → B's hit silences A within the declick time; group OFF → both sound.

## 4. Slicing by hand

- **Auto first, then by hand.** On loading a loop, find its transients (onset detection in the main loop: energy
  or spectral-flux over ~5 ms frames of the decoded ADPCM, peak-picked, snapped to the nearest zero crossing) and
  offer them as slice points; the 16-a-bar grid stays as the other choice. A setting per loop: GRID / AUTO / MANUAL.
- **On the FM-1:** a SLICE edit mode on the BRK page: a zoomed waveform (one slice and its neighbours), the slice
  start as a line; KNOB 1 picks the slice, KNOB 2 moves its start (coarse), KNOB 3 fine (zero-crossing snap
  on/off), KNOB 4 adds / removes a slice point; a key auditions the slice while editing. Up to 64 slices a loop.
- **In the browser (better for this):** the breaks page draws the waveform it already has, with slice markers
  the user can drag, add (double-click) and delete, plus "Auto" and "Grid" buttons; Send writes the slice table
  with the loops. Keep the slot audio format X0X's exactly (other X0X-family firmwares must still read the slots):
  store the slice tables **outside** the slot images — in a DREADNOUGHT storage object, or in the slot's data area
  after the audio with its own magic and CRC, beyond `data_len` so X0X's CRC check is untouched. Say which, and why.
- Slice points key to the loop by its length and a hash of its first bytes, so re-ordering loops on the page keeps
  each loop's slices.
- With uneven slices, "sync" means: each slice starts on its step and plays at the speed the tempo asks; the next
  hit cuts it (as now). The sequencer's default for a new step stays "the slice that belongs here".
- Tests: auto-slicing the built-in loops finds their hits (the generator knows exactly where it put every drum:
  have `tools/gen_dn_breaks.py` emit the true hit positions into the header and assert ≥ 95 % found within 5 ms);
  a moved slice point plays from the new place; a 0.5 loop with no table falls back to the grid.

## 5. Time-stretch: pitch and tempo apart

The user wants, at least, the break's pitch independent of the tempo; full time-stretch if the CPU allows next to
everything else planned (REESE, DREAD, wavetable PADS, STABS, KIT, 808, the FX section — `PROMPT.md`).

**First, measure.** Before choosing, profile on the device the way Carnac OS does (cycles per sample per voice, both
cores) and on the host: today's BREAKS voice, REESE, and a prototype of each mode below. Report a budget table:
what a voice costs, what is left for the engines still to come, and what the second core can take. Then choose.

Modes to offer, as a per-track STRETCH setting:
1. **REPITCH** (today): speed and pitch locked to the tempo. Cheapest. The classic jungle sound — keep it the default.
2. **SLICE** (Rex / ReCycle style): each slice plays at its *original* speed (PITCH knob transposes it freely,
   independent of tempo); the sequencer places slices in time. At a faster tempo a slice is cut by the next hit; at a
   slower tempo the gap after a slice ends is filled (a short loop of the slice's tail with a crossfade, or a fade to
   silence — a setting). Costs almost nothing more than REPITCH. With manual / auto slices (part 4) this is what most
   people mean by "time-stretching a break" on a sampler, and it may be all that's needed.
3. **STRETCH** (granular / WSOLA, per slice): the slice decoded into RAM (as reverse does), read by two overlapping
   windowed grains (~20–40 ms, Hann), grain positions advancing at the tempo's rate while the grains themselves play
   at the pitch's rate, a WSOLA-style search (a short cross-correlation) to line the next grain up with the last to
   avoid phasing on drums; transient-preserving: restart the grain stream on each detected onset. Budget it: two
   grain reads + window + interpolation per output sample per voice (~40–60 cycles?) plus the search once a grain.
   Run it on the second core if the measurement says so (X0X's `plat_cpu2_run` / `plat_cpu2_wait`, half the work a
   block, as Carnac splits its voices).

RAM: a decoded loop is 2 bytes a sample (a 2-bar 136 BPM loop at 22 kHz is ~155 KB) — too much for two tracks
whole. Stay per slice: a decoded-slice buffer per voice (today 4096 samples; size it from the longest slice the
manual slicing allows, and say what happens to a longer one).

UI: STRETCH mode and PITCH on the BRK page; the screen shows the effective tempo ratio and the pitch in semitones.

Tests: SLICE and STRETCH keep pitch when the tempo changes (measure the spectrum's peaks of a pitched test loop at
136 and at 172: within ±10 cents); STRETCH keeps the loop's length in bars at the new tempo (within 1 %); transients
stay sharp (the attack envelope of a kick within a few ms of REPITCH's); the cost per voice reported by the host
profile and checked on the device; no clicks at grain joins (the largest sample-to-sample step no worse than the
source's own).

## 6. Deliverables and checks
- Code, host tests and scenarios for every part; screenshots of ROLL, DRUMS, the BRK slice editor and the choke
  setting; WAVs: a long REESE line from the roll, a break in each STRETCH mode at 136 and 172, two breaks choking.
- README control table, CHANGELOG entry, the app's header comment, `docs/UPSTREAM.md` if anything borrowed.
- Pattern and sound migrations from 0.4 / 0.5 tested on the host (`reboot` with old images in the simulated flash).
- The breaks page's slicing tested in Node (marker maths, table packing) and end to end against the mock device.
- VERSION 0.6, CI release. Tell the user what is untested on the hardware.
- Keep the user's preferences: web pages light (never light text on dark), the device UI Massive-style as before.
