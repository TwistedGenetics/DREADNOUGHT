# PROMPT: DREADNOUGHT 0.9: fixes from the box, moving notes, a progress bar, LFOs you can draw

You are continuing DREADNOUGHT (https://github.com/TwistedGenetics/DREADNOUGHT), the dark jungle / DnB firmware for the
M-VAVE FM-1. It is now at 0.8:
- its own firmware on X0X's platform;
- REESE;
- an 8-track sequencer with ROLL and DRUM pages and overlap slides;
- BREAKS, with the SLICE page's chop mode;
- a web editor and a breaks page.

Read `README.md`, `CHANGELOG.md`, `docs/prompts/PROMPT-0.8.md` and the code first. Where this prompt is out of date,
the code wins.

House rules:
- Every change gets a host test that measures it (`tests/run_host.sh`, with scenarios in `tests/host/scenarios`).
- Take screenshots of every changed page.
- Web pages are light only: never light text on a dark background (the FM-1's own screen stays Massive-style dark).
- CI must be green before a release.
- Push `main:x0x-platform` first, then main.

The user flashed 0.8 and reported, in his own words (his item numbers):

> Chop knob wont move of chop 1 so i cant then set new chops. also i have some new stuff ass follows: move notes on
> synth sequencer 2. progress bar in the main screen so i can time my playing 3. drawable synced lfo modulation for
> various parameters, 4. LFO's 5. remove seq and brk from sel page as they no longer do anything 6. I can connect
> using the editor.

Item 6 almost certainly means "I **can't** connect", so treat it as a bug. Work in this order:
1. The two bugs (A, B).
2. The quick fix (C).
3. The progress bar (D).
4. Moving notes (E).
5. The LFOs (F).

After each part:
- host tests;
- a CI firmware build;
- screenshots;
- a short note to the user.

Ask before changing a control he already uses, unless this prompt says how to change it.

---

## A. Chop mode: "won't move off chop 1, so I can't set new chops"

Some background from reading the 0.8 code (`src/app/dn_app.c`: `dn_slice_knob`, `dn_chop_at` and the SLICE block at
the top of `dn_app_input`). An empty MANUAL loop has exactly one chop, so KNOB 1's range is 1..1 and it can't move;
that part is by design. New chops come from REC, which chops at the **cursor** (KNOB 2 / KNOB 3).

- **Likely cause:** after a clear the cursor sits at sample 0, right on chop 1. `dn_chop_at` silently refuses any
  chop within 64 samples of an existing one, so REC does nothing until the cursor has moved. To the user that looks
  like "nothing works".
- **Other causes the host can't rule out on the real box:** REC is masked or eaten by the platform on this page; KNOB
  2's step is `width / 8` while `dn_sl_width` is wrong for some loops; the page uses BREAKS B while A is shown
  (`dn_bsel` against `dn.trk`, for example arriving from a REESE page).

**First, reproduce exactly what he did.**
- Script it on the host: boot; go to the SLICE page from a sound page with track 1 (REESE) selected; clear with HOME
  held; turn KNOB 1; press REC; turn KNOB 2; press REC.
- Run the same steps with `dn.trk` = 0, 1 and 2, and from the 0.7 save format (an existing MANUAL table carried over
  from 0.7).
- If the host can't reproduce his fault, add a tiny on-screen debug line behind a hidden combo (sl_sel, ns, cursor,
  the last REC result) so the next hardware test tells you exactly where it stops. Ask him for the steps he used
  (page, knob, button, what the screen showed).

**Then make chop mode impossible to get stuck in:**
- **REC always does something.** With the cursor on an existing chop, it either:
  - nudges the new chop to the middle of the slice it's in (the "split this slice" the user expects when there's only
    one); or
  - flashes `MOVE THE CURSOR FIRST` in the status line.

  Choose one and say why. Every refusal (full, too close) shows on the screen; none are silent.
- **KNOB 1 is still useful with one chop.** With a single chop, KNOB 1 moves the cursor (the user turns the first
  knob first). With several, it selects a chop as now.
- **Show where the cursor is.** The status line reads `CHOP 1/1 · CURSOR 0.00 s` and names the next action when
  empty: `TURN CURSOR, REC TO CHOP`. The cursor line on the waveform must be visible at every zoom.
- **Tests:** the host scenario above (every `dn.trk`, every route into the page), the REC-on-a-chop case, KNOB 1 with
  one chop, and a screenshot of each message.

## B. The web editor can't connect

`web/editor/protocol.js` `connect()` does three things:
- takes the first MIDI input whose name matches `/dreadnought|fm-1|fm1/i`;
- pairs it with an output;
- sends INFO and expects a reply starting "DREADNOUGHT".

Find out which step fails on a real FM-1 running 0.8:
- **The port name.** What does X0X's USB descriptor call the port on macOS, Windows and Linux? Look at the submodule's
  USB descriptor strings. It may be "X0X", "FM-1 MIDI", "USB MIDI Device" or "M-VAVE …". Match whatever it really is.
  Also offer a port picker (a select with every MIDI port, remembered per browser) for when the name match fails, and
  when two ports match.
- **Permission.** Is `requestMIDIAccess({ sysex: true })` asked for, and is a denied prompt told to the user (Chrome
  asks once per site; a denial persists)? Check what happens on GitHub Pages over https.
- **The reply.** Does the firmware answer the editor's INFO command at all through the X0X glue? Check
  `src/hardware/dn_main_fm1.c`: the command number, the reply framing and the manufacturer ID (`F0 7D 46 4C …`).
  Compare it byte for byte with what `protocol.js` sends and expects. The mock (`web/editor/mock-fm1.js`) and the host
  harness both agree with the page, so a mismatch with the *real* glue would pass every test we have. Add a test that
  runs the editor's actual frames through the device glue's dispatcher, not just `dn_editor.c`.
- **Another app holding the port** (Ableton, the breaks page or the flasher in another tab). Say so in the error
  text.

**Already done (after 0.8, on the site only, no firmware change).**
- The user's report: Chrome didn't ask for MIDI permission (it was granted earlier on the site), nothing else had the
  port, and the page said "No FM-1 running DREADNOUGHT 0.7 or later found".
- So the access worked, and either no port matched the name or INFO wasn't answered in time. The editor asked INFO
  once with an 800 ms timeout. The breaks page retries, and also matches `x0x|felucca`.
- `connect()` now tries the likely names first, then every port; asks INFO three times with 1.5 s each; logs every
  step (`S.connLog` and the console); and names the ports it saw in the error.
- Start B from the user's next report with that list. If the port is there and INFO still goes unanswered, the fault
  is on the firmware side (`ed_service` / `ota_frame_get`).

**Also:**
- Add a **Diagnostics** panel to the editor (light, collapsible): the MIDI ports it sees, which one it chose, the
  frames sent and received in hex, and timeouts.
- Ask the user, through the report back, for its contents if connecting still fails.
- Check the breaks page's and the flasher's connect code for the same fault.

## C. The tabs on the sound pages: drop SEQ and BRK

The sound pages' tab row shows OSC, FILTER, AMP, MOD, OUT, SEQ and BRK. SELECT only cycles the first five, and SEQ /
BRK have their own buttons, so the last two tabs only confuse.
- Show only the pages SELECT reaches. Once F adds them, the LFO pages also appear as tabs, where SELECT can reach them.
- Check the other pages' tab rows (SEQ, BRK) for the same fault.
- Screenshot every page.

## D. A progress bar on the main screen, to time playing against

On the sound pages (OSC … OUT, and the LFO pages to come), wherever the scope is now, show **where the pattern is**:
- A thin bar across the screen for the pattern's length: the longest playing track, so a 4-bar REESE line over a
  1-bar break shows 4 bars. Bar lines and beat ticks are marked, and the playhead fills it as it goes.
- `BAR.BEAT` (already in the header while playing) gets bigger.
- Each beat flashes, the first beat of the bar more strongly.
- While stopped, show the length and where PLAY will start.

**A count-in and a click for recording:**
- REC armed + PLAY counts in one bar on the bar (and on the PLAY LED) before recording starts.
- An optional **click** (off / REC only / always) that sends a short tick into the audio out, louder on the bar.
- Put both in a settings place you propose: the OUT page has no free knob after OVERLAP, so consider a hold-SAVE
  menu, or a second OUT page.

Keep it cheap. The screen redraws already cost frames, so measure that the redraw stays inside the budget with
STRETCH breaks playing (the BRK2 CPU readout).

**Tests:** host scenarios with screenshots at known steps (bar 1 beat 1, bar 3 beat 2, a 3-bar track); the count-in
(the first recorded note can't land before bar 2); the click's level and timing in a WAV (the ticks on the beats to
the sample).

## E. Moving notes on the synth sequencer

0.8 added one way to move a note in time: hold its step key and turn **SELECT**. On the box that's evidently either
not found or not working.
- **Check it works on the hardware.** Is SELECT the encoder the platform reports as `EN_SELECT`? Does a push-turn
  report differently?
- **Add the step sequencer's usual way: pick up and drop.** Hold the note's step key and press the step key where it
  should go; the note (pitch, length, accent, slide, velocity) moves there. Black keys 10 / 11 still change the bar
  while you hold it, so a note can move to another bar. If the target step already has a note, propose: swap, replace
  or refuse.
- **Moving in pitch is already** KNOB 1 with the step held (the view follows). Make it a semitone per detent with no
  acceleration (it must never jump an octave by accident), and an octave per detent with OCT held.
- **ROLL KEYS mode (FX):** a held note there moves with KNOB 4 (the cursor) too.
- **Copy instead of move:** hold SAVE while dropping (the same modifier as copy / paste).
- **Tests:** a host scenario for each (pick-up-drop in the bar, across bars, onto an occupied step, copy, pitch per
  detent); README control table updated. Say plainly what's new.

## F. LFOs, synced, drawable, on many destinations

REESE has one LFO today: LRATE and LFLT on the MOD page (rate in Hz, to the filter only). Replace it with a real
modulation section.

**Four LFOs (decide the count from the CPU budget).** Each LFO has:
- **SHAPE:** sine, triangle, saw up, saw down, square, S&H, smooth random, and **DRAW**.
- **RATE:** synced to the tempo by default (8 bars … 1/32, with triplet and dotted); SYNC off gives Hz (0.05–20 Hz).
- **DEPTH:** bipolar.
- **DEST:** one destination each.
- **PHASE.**
- **MODE:** FREE (runs on), RETRIG (restarts on every note that isn't a slide) or ONE-SHOT (an envelope).

**Destinations:**
- REESE: CUT, RES, DRV, NTCH, DTN, pitch (± semitones), SUB, LEVEL, BEAT depth.
- Another LFO's depth or rate.
- BREAKS A / B: PITCH, DECAY, CRUSH, LEVEL.

The engine reads modulated values at control rate (every 32 samples), smoothed so a square or a drawn step doesn't
click. Measure the extra CPU with every LFO on a destination and STRETCH breaks running.

**Drawable (the user's "drawable synced lfo modulation"):**
- **DRAW** is a shape of 16 or 32 points per cycle, each −100…+100, with **SMOOTH** (steps … a curve).
- **On the FM-1:** a DRAW page shows the shape. The 16 white keys are its points (hold one and turn KNOB 1 to set it,
  or turn KNOB 1 and press keys to paint), black keys 10 / 11 reach points 17–32, KNOB 2 is SMOOTH, and quick shapes
  sit on the black keys (copy a built-in shape into DRAW, invert, clear).
- **In the web editor:** a new **LFO** tab. Each LFO's settings sit beside a large canvas where the user draws the
  shape with the mouse (click-drag paints, shift-drag draws a straight line, a snap-to-grid option), with presets
  (ramp, steps, trance gate, wobble 1/8, 1/4T). Show the playing LFO's phase live when connected.
- Sync is what makes this musical for jungle: a drawn 1-bar shape on the filter is a rhythmic wobble locked to the
  break.

**Device UI:**
- The LFO button cycles MOD → LFO 1 → LFO 2 → … (the tab row shows them; see C).
- Each LFO page: KNOB 1 SHAPE, 2 RATE, 3 DEST, 4 DEPTH.
- PHASE / MODE / SYNC on the same page with a modifier (propose one; hold FX?) or a second page.
- MOD keeps FENV and GLIDE. Propose what goes in the two free knobs (perhaps the filter envelope's decay and the LFO
  1 depth shortcut), or leave them empty.
- Show each LFO's live value as a small meter, and mark a modulated parameter's knob ring with the modulation range
  (as Massive does).

**Saving, migrating and the protocol:**
- Saved sounds and the factory presets get the LFO block. Migrate: a 0.8 sound's LRATE / LFLT becomes LFO 1 (free,
  the same Hz, sine, to CUT with the same depth), so old sounds sound the same. Test that against a WAV rendered by
  0.8.
- The sound save format gets a version bump with the migration tested both ways (0.8 save into 0.9, garbage refused).
- The editor protocol's parameter list grows. Keep the count and order in one table that the firmware
  (`dn_editor.c`), `protocol.js` and the mock share, or at least test it (the golden replies). The drawn points travel
  as their own command (read / write per LFO), not as 32 parameters. Backups from the 0.8 editor still restore.
- Per-pattern modulation (an LFO per track, or parameter locks per step) is **not** in this release. Note it in the
  README as a possible next step, and don't build it.

**Tests:**
- Each shape's output over a cycle against the maths.
- Sync accuracy: a 1/4 LFO at 172 BPM drifts less than a sample per bar against the sequencer's step clock, measured.
- RETRIG resets on a non-slide note and not on a slide.
- DRAW with SMOOTH 0 and max.
- Each destination actually moves (render and measure CUT, pitch, LEVEL and a break's PITCH).
- Migration of a 0.8 sound.
- The editor's draw-and-send round trip end to end against the mock and the firmware's replies.

---

## Deliverables

- Firmware 0.9 (VERSION, CI release), with the web editor and the breaks page updated and all tests green.
- README updated:
  - the control tables, with a "what moved" list;
  - the LFO section;
  - a troubleshooting section for connecting the editor (port names per OS, the sysex permission, other apps
    holding the port).
- A CHANGELOG entry.
- Screenshots: SLICE with its new messages, the sound pages' tabs, the progress bar (stopped, playing, count-in),
  moving a note across bars, each LFO page, the DRAW page, and the editor's LFO tab while drawing.
- WAVs:
  - a drawn 1/8 filter wobble on a REESE line over a break;
  - a 1/4T pitch LFO;
  - the click with the count-in.
- Tell the user:
  - exactly what to try on the box, step by step for chopping and connecting;
  - what to send back if connecting still fails (the Diagnostics panel);
  - what is untested on the hardware.
