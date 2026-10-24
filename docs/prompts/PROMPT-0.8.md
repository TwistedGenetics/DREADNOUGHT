# PROMPT: DREADNOUGHT 0.8 — chopping that works, overlap slides, a roll that follows

You are continuing DREADNOUGHT (https://github.com/TwistedGenetics/DREADNOUGHT), the dark jungle / DnB firmware for the
M-VAVE FM-1, now at 0.7 (own firmware on X0X's platform; REESE; 8-track sequencer with ROLL / DRUM pages; BREAKS with
GRID / AUTO / MANUAL slices and three stretch modes; a web editor and a breaks page with a slice editor). Read
`README.md`, `CHANGELOG.md`, `docs/prompts/PROMPT-0.6.md` and the code first; the code wins where this prompt is out of
date. Keep the house rules: every change gets a host test that measures it (`tests/run_host.sh`, scenarios in
`tests/host/scenarios`), screenshots of every changed page, the web pages light only, CI green before a release.

The user played 0.7 on the FM-1 and reported, in his words:

> 1. when in manual chopping mode i want to be able to start with no slices and move and chop manually.
> 2. i want to audition the chop by hitting the key pad. The manual chopping seems not to work properly.
> 3. when using the sequencer for the synth engines i want to be able to overlap notes which can then cause a slide
>    (the slide amount can be determined somewhere in the settings for that synth).
> 4. when moving a note around on the sequencer page the sequencer screen moves in tandem to show where the note is.

Work in this order: 1 and 2 together (they are one feature: chopping), then 3, then 4. After each: host tests, CI
firmware build, screenshots, a short note to the user. Stop and ask before changing a control he already uses unless
this prompt says how.

---

## 0. First, find out why manual chopping "doesn't work properly"

Reproduce on the host before designing anything (drive the SLICE page from a scenario, as `slice.txt` does, and
record what the user would hear). Reading the 0.7 code (`src/app/dn_app.c` `dn_slice_knob`, `src/dsp/dn_breaks.c`
`dn_brk_slices_apply`) these are likely, each worth a failing test first:

- **MOVE is unusably slow:** 64 samples (~3 ms) a detent, 256 when turned fast — crossing a 3.5 s loop takes over a
  thousand detents. It feels like nothing happens.
- **ADD can only halve the selected slice** — there is no way to put a chop where you hear the hit.
- **The first slice is fixed at sample 0**: the loop's start can't be chopped off.
- **The selected slice can be out of range:** `sl_sel` is clamped only when KNOB 1 turns; after the slice count drops
  (GRID 32 → AUTO 21, or a delete), MOVE / ADD / DEL do nothing at all until KNOB 1 is touched (they test
  `sl_sel < ns`). Silent failure: probably the "not working".
- **MANUAL starts from a copy** of AUTO (or of the table in use) — never empty, which the user asks for.
- **Adding or deleting a chop renumbers the ones after it**, so pattern steps on that track suddenly play different
  audio.
- Manual chops reach flash only on SAVE (the browser's slice editor saves at once): inconsistent.
- The zoom view re-centres on every change, so the chop being moved jumps around the screen.

If the host can't reproduce something the user describes, write down exactly what you tried and ask him for the
steps (which page, which knob, what he heard) rather than guessing.

## 1 + 2. Chopping by hand, heard on the keys

Redesign the SLICE page (BRK, BRK2, SLICE: ARP cycles them) as a sampler's chop mode:

- **Start empty.** MANUAL with no chops yet = one slice, the whole loop. A **CLEAR** action (with a confirmation
  hold, like the track clear) returns any table to that. Switching to MANUAL the first time asks nothing and starts
  empty; keep "copy AUTO" and "copy GRID" as explicit actions (the browser editor's Auto / Grid buttons already do
  this) so nobody loses the quick start.
- **A cursor through the loop**, shown on the zoomed waveform: KNOB 2 moves it fast (an eighth of the visible window
  a detent, accelerated), KNOB 3 fine (a few samples), KNOB 4 zooms (the whole loop .. ~50 ms). The view scrolls to
  keep the cursor in sight instead of re-centring on every change.
- **CHOP at the cursor** (a button: propose which — REC is free on this page) drops a chop there, snapped to the
  nearest zero crossing (an option to snap to the nearest transient AUTO found instead). **Tap-to-chop while it
  plays:** with the loop running (PLAY on the SLICE page plays the whole loop from the start, at its own tempo or the
  song's), each press of CHOP drops a chop at the playing position, minus a short latency compensation — the
  MPC / SP-1200 way of chopping a break by ear.
- **Select, move, delete:** KNOB 1 selects a chop (the cursor jumps to it); moving it is the cursor knobs while it's
  selected; HOME (or the black key 9 hold) deletes the selected chop. Any chop can move, the first one too (the part
  of the loop before it is simply never played).
- **Audition on the keys:** on the SLICE and DRUM pages the 27 keys play the chops in order, chromatically from the
  lowest key (OCT shifts by 27 or by 16 — choose and say why); the key just pressed **selects** that chop for editing,
  and the keys that have a chop are lit. Velocity-free, choked as the track's voice is. Holding a key holds the chop
  (DECAY on release), so long chops can be checked to their end.
- **Chops keep their identity when you add or delete:** a pattern step stores a slice index; when a chop is inserted
  before it, the steps that pointed at later chops must be moved up by one (and down on delete), on both break tracks
  if they play the same loop. Test it: a pattern plays the same audio before and after an insert.
- **Saved as you go:** manual chops are written to flash a moment after the last change (debounced, as the web editor
  writes patterns), like the browser's slice editor does — no SAVE needed for chops. Mind the flash: never more often
  than every few seconds.
- The browser slice editor (`web/breaks/slices.js`) and the web editor's DRUM grid should show the same thing: chop
  numbers, and the same renumbering rule.

Tests: start empty and chop 8 places by cursor → 8 + 1 slices at those samples (± the zero-crossing snap); tap-to-chop
at known times while the loop plays → chops within a few ms of the taps (after the latency compensation); every edit
works whatever was selected before (the out-of-range case); insert / delete keep the pattern's audio; keys select and
play the chop they are mapped to; chops survive a reboot without SAVE.

## 3. Overlapping notes slide

On the synth tracks (REESE now; the engines to come and the MIDI tracks too):

- A note whose length **reaches past the start of the next note** on the same track slides into it: no new attack, the
  pitch glides (the 303 / mono-synth legato way). A note that ends before the next one starts retriggers it as now.
  The explicit SLIDE flag keeps working (a slide even without overlap).
- **The slide amount** is the synth's own setting: REESE's GLIDE (MOD page) is the glide time; rename or document it as
  the slide time if that's clearer, and show it on the web editor's REESE page with the same name. Consider a second
  value, the glide's shape (linear vs exponential) or "legato only / always", only if it earns its place. For the MIDI
  tracks, overlap sends the new note-on before the old note-off (legato for the receiving synth) — say so in the
  README.
- A per-track switch **OVERLAP: SLIDE / CUT** (default SLIDE) for the user who wants overlaps to retrigger.
- Show overlaps: on the FM-1's ROLL and the web editor's piano roll the earlier note draws through to where the next
  starts and the slide is drawn as a ramp between them; the overview's coverage shows them as one held line.
- The sequencer change is in `src/dsp/dn_seq.c` (`fire_step`: today a later note cuts unless it has the SLIDE flag);
  the gate logic that holds a note into a sliding note must treat "overlapped" the same as "flagged".
- Tests (`tests/host/seq_test.c`): an overlapping pair gives one note-on with slide and no note-off between; a
  non-overlapping pair retriggers; OVERLAP CUT retriggers; on REESE the pitch reaches the new note in GLIDE's time
  (measure the glide on a rendered WAV); MIDI tracks send on-before-off.

## 4. The roll follows the note

- **On the FM-1 (ROLL, DRUM, OVERVIEW):** when a held step's note is moved out of view (KNOB 1 changes its pitch, or a
  slice row), the view scrolls with it, by the smallest amount that keeps it in sight (a row or two of margin).
  Moving a note **in time** on the FM-1 doesn't exist yet: add it (propose the control — e.g. hold the step and turn
  SELECT, or hold the step and press the target step key) and make the view move with it across bars (the 16 steps in
  view follow the note, the other bar's steps light as it gets there).
- **In the web editor:** dragging a note past the top or bottom of the piano roll scrolls it (and past the right or left
  edge when the roll is zoomed in on part of a 64-step track — add a horizontal zoom while you're there); keyboard
  arrows on a selected note keep it in view; the DRUM grid scrolls with a dragged hit too.
- Tests: host scenarios hold a step and turn KNOB 1 an octave past the window and expect the view to follow; move a
  note from bar 1 to bar 3 and expect the view on bar 3; the editor's end-to-end test drags a note past the edge and
  expects the scroll.

## 5. Deliverables

- Firmware 0.8 (VERSION, CI release), the web editor and the breaks page updated, all tests green.
- README control tables updated (the SLICE page is new controls; say plainly what moved), CHANGELOG entry.
- Screenshots: the new SLICE page (empty, a cursor, chops), overlaps on the FM-1's ROLL and in the web editor, the
  view following a moved note. WAVs: a break chopped by tap-to-chop and played back; an overlapping REESE line at
  two GLIDE settings.
- Tell the user exactly what to try on the box, and what is untested on the hardware.
