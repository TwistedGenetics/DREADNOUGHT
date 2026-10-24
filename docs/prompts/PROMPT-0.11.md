# PROMPT: DREADNOUGHT 0.11: one sequencer page per instrument, 16 patterns each, Digitakt-style trigs

You are continuing DREADNOUGHT (https://github.com/TwistedGenetics/DREADNOUGHT), the dark jungle / DnB firmware for
the M-VAVE FM-1, now at 0.10:
- REESE;
- BREAKS A / B with chop mode on the keys;
- 4 LFOs;
- an 8-track sequencer (ROLL, DRUM, OVERVIEW; overlap slides);
- a web editor and a breaks page.

Read `README.md`, `CHANGELOG.md`, `docs/prompts/PROMPT-0.9.md` and the code first; where this prompt is out of date,
the code wins.

House rules:
- Every change gets a host test that measures it (`tests/run_host.sh`, scenarios in `tests/host/scenarios`).
- Screenshots of every changed page.
- Web pages light only.
- No 64-bit divides on the device.
- CI green before a release; push `main:x0x-platform` first, then main.

The user asked (his words):

> 1. The sequencer page should only really show the currently selected synths or sliced drums. Any future drum pages
>    like a 808/909 style drums or synth should get its own sequencer page. so if you're on an instrument and press seq
>    it takes you to that instruments page only.
> 2. I would like patten scenes for each instrument so later on when we make a song mode i can chain different pattens
>    into a song.
> 3. I would like to create velocity fades and probability so can i press a note, or a drum trigger and have this.
>    Please look at digitakt manual for how i would like probability.

His answers to the follow-up questions:
- **Remove** the 8-track OVERVIEW.
- **Velocity:** both a Digitakt-style retrig roll with a velocity fade, and a ramp across a run of steps. Also: hold a
  note or trigger on the sequencer page and set its velocity and probability there.
- **Patterns:** 16 patterns per instrument; a change takes effect when the current pattern ends.

Work in this order, each part tested, built in CI and screenshotted before the next:
1. A (the instrument pages).
2. B (the step's new data).
3. C (probability and conditions).
4. D (velocity: retrig fades, ramps).
5. E (patterns).
6. F (the web editor).

---

## A. Every instrument has its own sequencer page; SEQ goes to the one you're on

- **An instrument** is a sound source with a track:
  - REESE (track 1);
  - BREAKS A and B (tracks 2–3);
  - the MIDI tracks 4–8, as "MIDI 1–5".

  Make it a table in `dn_app.c` (`kind`: SYNTH / SLICED / MIDI, later DRUMKIT for an 808/909 engine; its track; its
  name; its sound pages), so a new engine adds a row and gets a page.
- **SEQ** from any of an instrument's pages opens **that instrument's** sequencer page:
  - REESE and MIDI: the piano roll (ROLL);
  - a break: the slice grid (DRUM).

  SEQ again goes back to the sound page you came from (so SEQ toggles sound ↔ sequence). On the sequencer page the
  header shows the instrument, and the tab row shows only its pages (its sound pages and SEQ).
- **The OVERVIEW page is removed.**
  - Track select by SELECT on the ROLL page also goes: an instrument's page is reached from its sound pages. The MIDI
    tracks get a small sound page each, MIDI (channel, and later program / CC), so they too are "an instrument". Reach
    them with a button you propose (e.g. GLO cycles OUT → SETUP → MIDI 1 … 5?). Decide, and say plainly in the README.
  - Mutes (black keys 1–8) stay on every sequencer page.
- **Keep everything the sequencer pages do now:**
  - STEP / KEYS (FX), copy / paste, length, swing;
  - moving notes;
  - the view following;
  - live REC with the count-in.

  Swing and length are now per instrument page (length per track as now; swing stays global, shown on each).
- **Tests:** SEQ from REESE's OSC page lands on REESE's ROLL; from BRK B on B's DRUM; SEQ again returns to the page it
  came from. Screenshots of each instrument's page.

## B. A step grows: velocity, probability / condition, retrig

Today a step is `{note, flags (ON / SLIDE / ACC), len, vel}` (4 bytes). It becomes 8 bytes:

| field | meaning |
|---|---|
| note | as now (a break: the chop) |
| flags | ON, SLIDE, ACC, RTRG (retrig on) |
| len | as now (quarter steps) |
| vel | 1..127 |
| cond | 0 none; 1..100 a % chance (Digitakt's percentages, in its steps: 1 2 4 6 9 13 19 25 33 41 50 59 67 75 81 87 91 94 96 98 99 100); then FILL, !FILL, PRE, !PRE, NEI, !NEI, 1ST, !1ST, LST, !LST, A:B and !A:B for A < B ≤ 8 |
| rtim | the retrig rate: 1/1, 1/2, 1/3, 1/4, 1/5, 1/6, 1/8, 1/10, 1/12, 1/16, 1/20, 1/24, 1/32, 1/40, 1/48, 1/64, 1/80 (of a step… use Digitakt's meaning: of a whole note, so 1/16 = one a step) |
| rlen | how long the retrigs last (quarter steps; default the step's length) |
| rvel | the velocity curve over the retrigs, −127 (fade out to nothing) … 0 (flat) … +127 (fade in from nothing) |

- The pattern format goes up a version, with the migration tested (a 0.10 pattern opens with vel / len as they were,
  cond 0, no retrig). The editor protocol's pattern bytes change with it (see F).
- **Hold a step (ROLL, DRUM) and the knobs edit it.** Today that's KNOB 1 note, 2 length, 3 accent, 4 slide. Make it
  pages of four, cycled by FX while a step is held (or propose better):
  - NOTE / LEN / VEL / PROB (COND);
  - SLIDE / ACC / RTRG / RTIM;
  - RLEN / RVEL.

  The status line names the page; turning a value shows it large. The user's request: "hold a note or a trigger and
  set its velocity and probability there".
- Velocity is shown on ROLL and DRUM: the note's brightness or a small bar under it. A step with a condition shows a
  mark (Elektron: the trig's LED half lit). A retrig shows its repeats as ticks inside the note.

## C. Probability and trig conditions, as the Digitakt does them

Read the Digitakt manual's "Trig conditions and conditional locks" (section 10.10.3 in the OS 1.51 manual) and match
its behaviour:
- **%:** the trig plays with that chance, rolled each time it comes round.
- **A:B:** plays on the A-th pass of every B passes of the track's pattern (1:2 = passes 1, 3, 5…; 2:4 = passes 2, 6,
  10…); **!A:B** the passes it doesn't.
- **1ST** the first pass after the pattern starts (or is switched to); **!1ST** every other. **LST** the last pass
  before a queued pattern change; **!LST** the others.
- **FILL** plays only while FILL is on; **!FILL** only while it's off. FILL is a held button: propose one (HOME held
  on a sequencer page? it holds notes on the sound pages), and a latch.
- **PRE** plays if the most recent evaluated condition on the same track was true (PRE / !PRE themselves are not
  "evaluated" for this). **NEI / !NEI** the same with the neighbouring track's most recent (the track before; false if
  it has none).

Pass counting is per track (polymeter: a 12-step track counts its own passes). A trig that doesn't play doesn't cut or
slide the note before it (it's as if the step were off), but it still counts for PRE.

**Tests** (`tests/host/seq_test.c`):
- 50% over 10 000 passes is 50 ± 2 %, and the rolls come from the app's seeded noise (repeatable in tests).
- 1:2, 2:4, 3:4, !1:2 over 16 passes are exactly the passes the manual says.
- 1ST / !1ST / LST around a pattern change.
- FILL held and not.
- PRE after a 50% trig (the manual's own example: a 50% trig, then PRE, PRE, !PRE).
- NEI across two tracks.
- A 12-step track's A:B counting its own passes.

## D. Velocity: retrig fades and ramps

- **Retrig:** with RTRG on, the step repeats at RTIM for RLEN (cut at the next note or the pattern's end), each repeat
  a new note-on (REESE retriggers its envelopes; a break re-hits its chop; MIDI sends on / off pairs). The velocity
  goes along RVEL's curve from the step's velocity: −127 fades to 0 by the last repeat, +127 rises from 0 to it.
  Sample-accurate like the rest of the sequencer. That gives snare rolls and hat builds.
- **Ramp across steps:** on ROLL / DRUM, hold the first step and press the last step of a run while holding REC (or a
  gesture you propose). KNOB 3 then sets the run's start velocity and KNOB 4 its end velocity, and the steps between
  get the straight line between them. Show it as the velocity bars changing live.
- Velocity reaches the sound:
  - REESE: velocity to level, with a VEL SENS value on OUT or SETUP (0 = as now, ignore it). Don't change how existing
    patterns sound at VEL SENS 0.
  - Breaks: level per hit.
  - MIDI: the note-on's velocity.
- **Tests:**
  - The repeats land at the right samples for 1/32, 1/3 and 1/80 at 172 BPM, with swing 50 and 66.
  - RVEL −127 / 0 / +127 give the velocity sequence expected.
  - A retrig is cut by the next note.
  - A ramp over 8 steps from 20 to 120 sets 20, 34, …, 120.
  - A rendered snare roll WAV.

## E. 16 patterns per instrument, changed at the pattern's end

- **Each instrument has 16 patterns:** its track's steps, length and its own settings (REESE's track: the notes; a
  break's: the chops played, not the chops themselves, which stay per loop as now). Pattern 1 of every instrument is
  today's pattern after migration.
- **Selecting:** on an instrument's sequencer page, hold a button you propose (PRESET's encoder push isn't available,
  so perhaps SAVE held + a white key 1–16, as Elektron uses PTN + TRIG) to queue pattern n.
  - The new pattern starts when the playing one ends (this instrument's own length), which is LST's "last pass".
  - Stopped, the change is immediate.
  - The screen shows the playing and the queued pattern (Elektron: one solid, one flashing).
  - Copy a pattern to another slot: SAVE held + OCT− on the source (copy), then SAVE held + OCT+ on the target, as bar
    copy / paste does now; make it unambiguous with the bar copy.
- **Storage.** The FM-1 has 1 MB of flash; the app sits below 0x93000, X0X's store objects are 2 × 4 KB sectors each
  (≤ 3840 bytes of payload) at 0x97000…, the three user sample slots take 0xA0000–0xDBFFF (80 KiB each), and
  0xE0000–0xFBFFF is the update loader's staging area (anything there is erased by an update). 8 instruments × 16
  patterns × 64 steps × 8 bytes is 64 KiB dense. Options, with the user deciding:
  1. **Sparse in the store objects:** each pattern stores only its steps that are on (plus length and settings). There
     are four spare objects (OBJ_PAT1, OBJ_MOTION, OBJ_SET and OBJ_PAT0 when PAT0 moves), ~15 KB in all: roughly
     1 500 notes across all 128 patterns, a limit to show on screen.
  2. **Take the third sample slot** (80 KiB at 0xC8000) for patterns, dense, no limits; the breaks page then offers
     two slots. Loops already in slot 3 must be moved first: say so in the release notes and on the breaks page.

  Recommend 2 unless the user objects (he chose 16 a slot). Either way: a pattern is written to flash when it's
  changed and the playhead leaves it, or a few seconds after the last edit (as chops are), never during audio-critical
  moments without testing that no buffer underruns (measure the write time and the audio callback's worst case on
  the host's model and say what's left to test on the box).
- RAM: keep the playing and the queued pattern of each track in RAM (2 × 8 × 512 B = 8 KB), read others from flash
  (XIP) on change.
- **Song mode comes later.** Leave a clear place for it: a pattern number per track is the whole state a song step
  needs (plus a repeat count). Say how a song will drive it.

## F. The web editor

- The pattern bytes change (B), and patterns per instrument (E): new commands to read / write one instrument's pattern
  n and to queue / select it. Version the protocol; a 0.10 FM-1 still works with what it has.
- The piano roll and slice grid gain:
  - a per-note inspector (velocity, probability / condition as a select with the Digitakt list, retrig on / rate /
    length / velocity curve) for the selected note;
  - a velocity ramp tool (drag across the velocity lane with Shift to draw a straight line).
  - Notes with a condition get a mark; retrigs show their repeats.
- A pattern strip per instrument (1–16): the playing one, queued, empty / used; click to queue; copy / paste
  patterns.
- The editor's end-to-end test covers each, against the mock (which must refuse 8-bit SysEx bytes, as it now does).

## Deliverables

- Firmware 0.11 (VERSION, CI release), with the web editor updated and all tests green.
- README: the control tables rewritten for the instrument pages (say plainly what moved, and that OVERVIEW is gone), a
  section on conditions and retrigs (the Digitakt list), and patterns.
- A CHANGELOG entry.
- Screenshots:
  - each instrument's sequencer page;
  - the held-step pages;
  - a conditional step and a retrig on ROLL and DRUM;
  - the pattern display (playing / queued);
  - the editor's inspector and pattern strip.
- WAVs: a snare roll with a fade; a hat build; a 50% hat pattern over 4 bars.
- Tell the user what to try, and what is untested on the box.
