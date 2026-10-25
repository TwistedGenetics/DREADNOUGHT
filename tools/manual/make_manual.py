#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""DREADNOUGHT's manual: HTML in the BLOCKS skin's colours (on a light page), printed to an A4 PDF.
Copyright (C) 2026 Jamie (TwistedGenetics).

    python3 tools/manual/make_manual.py OUT_DIR       (after sh tests/run_host.sh: the screens come from build/host)

Writes OUT_DIR/DREADNOUGHT-manual.html and OUT_DIR/DREADNOUGHT-manual.pdf. Needs playwright (Chromium), pypdf and
reportlab (the page colour behind every page)."""
import base64
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from fm1_svg import COL, TINT, INK, PAPER, fm1, screen, png_uri  # noqa: E402

REPO = Path(__file__).resolve().parents[2]
SHOTS = REPO / "build" / "host"
FONTS = REPO / "x0x-firmware" / "assets" / "fonts"
VERSION = (REPO / "VERSION").read_text().strip()


def font_face():
    out = []
    for w, f in ((500, "Medium"), (600, "SemiBold"), (700, "Bold")):
        b = base64.b64encode((FONTS / f"BarlowSemiCondensed-{f}.ttf").read_bytes()).decode()
        out.append(f'@font-face{{font-family:"Barlow Semi Condensed";font-weight:{w};src:url(data:font/ttf;base64,{b}) format("truetype")}}')
    return "\n".join(out)


def k(*names):                                       # keycaps
    return " ".join(f'<span class="k">{n}</span>' for n in names)


def shot(f, cap="", size="s"):
    return (f'<figure class="shot {size}"><img src="{png_uri(SHOTS / f)}" alt="">'
            + (f"<figcaption>{cap}</figcaption>" if cap else "") + "</figure>")


def shots(*items):
    return '<div class="shots">' + "".join(shot(f, c) for f, c in items) + "</div>"


def knobs(rows, note=""):
    """rows: (page, how, k1, k2, k3, k4)"""
    h = ('<table class="knobs"><thead><tr><th>PAGE</th><th>GET THERE</th>' +
         "".join(f'<th style="background:{TINT[i]}"><i style="background:{COL[i]}"></i>KNOB {i + 1}</th>' for i in range(4)) +
         "</tr></thead><tbody>")
    for r in rows:
        h += f"<tr><td><b>{r[0]}</b></td><td>{r[1]}</td>" + "".join(f"<td>{x}</td>" for x in r[2:]) + "</tr>"
    h += "</tbody></table>"
    if note:
        h += f'<p class="note">{note}</p>'
    return h


def table(rows, head=None, cls="tbl"):
    h = f'<table class="{cls}">'
    if head:
        h += "<thead><tr>" + "".join(f"<th>{x}</th>" for x in head) + "</tr></thead>"
    h += "<tbody>" + "".join("<tr>" + "".join(f"<td>{x}</td>" for x in r) + "</tr>" for r in rows) + "</tbody></table>"
    return h


def tip(text, col=1, title="TIP"):
    return f'<div class="tip" style="background:{TINT[col]};border-left-color:{COL[col]}"><b>{title}</b> {text}</div>'


CH = [0]


def chapter(title, col, sub="", brk=True):
    CH[0] += 1
    return (f'<section class="chapter{"" if brk else " flow"}"><div class="chead" style="background:{COL[col]}"><span class="cn">{CH[0]}</span>'
            f'<span class="ct">{title}</span></div>' + (f'<p class="lead">{sub}</p>' if sub else ""))


def fig(svg, cap=""):
    return f'<figure class="fig">{svg}' + (f"<figcaption>{cap}</figcaption>" if cap else "") + "</figure>"


def qs(n, title, col, svg, body, see=None, cap=""):
    side = (f'<div class="see"><b>ON THE SCREEN</b>{shot(see, cap)}</div>' if see else "")
    return (f'<section class="qs"><div class="qhead"><span class="qn" style="background:{COL[col]}">{n}</span>'
            f'<span class="qt">{title}</span></div>{fig(svg)}<div class="qbody"><div>{body}</div>{side}</div></section>')


def steps(*items):
    return '<ol class="steps">' + "".join(f"<li>{x}</li>" for x in items) + "</ol>"


# ---- the figures ------------------------------------------------------------------------------------------------
def overview():
    c = [
        dict(t="MASTER", title="MASTER", text="Volume", side="left", at=60, col=4),
        dict(t="PRESETS", title="PRESETS", text="Sounds, loops", side="left", at=200, col=0),
        dict(t="ALGORITHM", title="ALGORITHM", text="Tempo", side="left", at=340, col=1, edge="bottom"),
        dict(t=["OCT-", "OCT+"], title="OCT- OCT+", text="Octave", side="left", at=470, col=2, edge="bottom"),
        dict(t="W1", title="WHITE KEYS", text="Notes · steps · tracks", side="left", at=620, col=3, edge="left"),
        dict(t="SELECT", title="SELECT", text="The page", side="top", at=-90, col=3, w=250),
        dict(t="SCREEN", title="SCREEN", text="The 4 tiles: the knobs", side="top", at=330, col=5, w=330),
        dict(t=["K1", "K2", "K3", "K4"], title="KNOB 1-4", text="The four tiles' values", side="top", at=850, col=0, w=360),
        dict(t=["SEL", "EDIT", "ENV", "LFO", "GLO"], title="SEL…GLO", text="The sound's pages", side="right", at=60, col=1, edge="top"),
        dict(t="FX", title="FX", text="More values", side="right", at=200, col=2, edge="bottom"),
        dict(t=["HOME", "SAVE", "ARP", "SEQ"], title="HOME SAVE ARP SEQ", text="Tracks · keep · breaks · sequencer", side="right", at=345, col=3, edge="bottom"),
        dict(t=["PLAY", "REC"], title="PLAY  REC", text="Play · record", side="right", at=490, col=4, edge="bottom"),
        dict(t="B11", title="BLACK KEYS", text="Notes · mutes · bars", side="right", at=625, col=5, edge="right"),
    ]
    return fm1(c, screen=SHOTS / "ui-01-boot.png", margin=(340, 340, 150, 20), ft=30, fx=25)


def qs_figs():
    F = {}
    M = dict(ft=38)
    F[1] = fm1([
        dict(t="PLAY", n=1, title="PLAY", side="right", at=300, edge="bottom", col=0, w=250),
        dict(t="MASTER", n=2, title="VOLUME", side="left", at=92, col=1, w=270),
        dict(t="PRESETS", n=3, title="SOUNDS", side="left", at=230, col=2, w=270),
        dict(t=["K1", "K2", "K3", "K4"], n=4, title="KNOBS 1-4", side="top", at=766, w=330, col=3),
        dict(t="KEYS", n=5, title="PLAY THE KEYS", side="bottom", at=500, w=390, col=4),
    ], screen=SHOTS / "ui-01-boot.png", margin=(300, 300, 90, 90), **M)
    F[2] = fm1([
        dict(t="HOME", n=1, title="HOME", side="right", at=290, edge="bottom", col=0, w=250),
        dict(t=[f"W{i}" for i in range(1, 9)], n=2, title="WHITE KEY 1-8", side="bottom", at=290, w=400, col=1),
        dict(t=["SEL", "EDIT", "ENV", "LFO", "GLO"], n=3, title="PAGES", side="right", at=110, edge="top", col=2, w=250),
        dict(t="SELECT", n=4, title="OR SELECT", side="left", at=92, col=3, edge="left", w=270),
    ], screen=SHOTS / "home-01.png", lit={"HOME"}, margin=(300, 300, 20, 90), **M)
    F[3] = fm1([
        dict(t="SEQ", n=1, title="SEQ", side="right", at=240, edge="bottom", col=0, w=250),
        dict(t=[f"W{i}" for i in range(1, 17)], n=2, title="TAP STEPS", side="bottom", at=500, w=360, col=1),
        dict(t=["K1", "K2", "K3", "K4"], n=3, title="HOLD A STEP + KNOBS", side="top", at=766, w=520, col=2),
        dict(t="PLAY", n=4, title="PLAY", side="right", at=400, edge="bottom", col=3, w=250),
        dict(t="SAVE", n=5, title="SAVE", side="left", at=420, edge="bottom", col=4, w=250),
    ], screen=SHOTS / "seq-02-step-held.png", margin=(300, 300, 90, 90), **M)
    F[4] = fm1([
        dict(t="REC", n=1, title="REC", side="right", at=150, edge="top", col=0, w=250),
        dict(t="PLAY", n=2, title="PLAY", side="right", at=330, edge="bottom", col=1, w=250),
        dict(t="KEYS", n=3, title="PLAY ALONG", side="bottom", at=500, w=360, col=2),
        dict(t="REC", n=4, title="REC: OFF", side="right", at=480, edge="right", col=3, w=250),
        dict(t="SAVE", n=5, title="SAVE", side="left", at=420, edge="bottom", col=4, w=250),
    ], screen=SHOTS / "progress-04-countin.png", lit={"REC"}, margin=(300, 300, 20, 90), **M)
    F[5] = fm1([
        dict(t="GLO", n=1, title="GLO ×5", side="right", at=110, edge="top", col=0, w=250),
        dict(t="K1", n=2, title="TRACK", side="top", at=440, w=250, col=1),
        dict(t="K2", n=3, title="INSTRUMENT", side="top", at=790, w=330, col=2),
        dict(t="FX", n=4, title="FX: PUT IT ON", side="right", at=270, edge="bottom", col=3, w=290),
        dict(t="SAVE", n=5, title="SAVE", side="left", at=420, edge="bottom", col=4, w=250),
    ], screen=SHOTS / "project-05-chosen.png", margin=(300, 300, 90, 20), **M)
    F[6] = fm1([
        dict(t="ARP", n=1, title="ARP", side="right", at=240, edge="bottom", col=0, w=250),
        dict(t="PRESETS", n=2, title="THE LOOP", side="left", at=210, col=1, w=270),
        dict(t="SEQ", n=3, title="SEQ", side="right", at=400, edge="bottom", col=2, w=250),
        dict(t="K1", n=4, title="SLICE ROW", side="top", at=610, w=320, col=3),
        dict(t=[f"W{i}" for i in range(1, 17)], n=5, title="TAP STEPS", side="bottom", at=500, w=360, col=4),
    ], screen=SHOTS / "drum-01.png", margin=(300, 300, 90, 90), **M)
    return F


def screen_fig():
    return screen(SHOTS / "ui-01-boot.png", [
        dict(r=(0, 0, 240, 20), title="TOP BAR", text="DREADNOUGHT, the tempo; playing: the bar and beat.", side="left", at=75, col=4),
        dict(r=(0, 23, 240, 21), title="SOUND BAR", text="The instrument (REESE) and its sound. * changed since SAVE.", side="right", at=100, col=0),
        dict(r=(0, 46, 240, 47), title="BARS AND SCOPE", text="The pattern's bars, where it is, the beat; the sound's wave.", side="left", at=260, col=1),
        dict(r=(0, 96, 240, 18), title="TABS", text="The sound's pages; lit: this one.", side="right", at=320, col=2),
        dict(r=(1, 115, 238, 88), title="THE FOUR TILES", text="KNOB 1-4: name, value, filled as far as the value goes.", side="left", at=470, col=3),
        dict(r=(0, 213, 240, 27), title="STATUS LINE", text="Octave, track, SAVED; messages.", side="right", at=690, col=5),
    ])


# ---- the text ---------------------------------------------------------------------------------------------------
def body():
    F = qs_figs()
    o = []
    o.append(f'''
<section class="cover">
  <div class="brand">DREADNOUGHT</div>
  <div class="tagline">Dark jungle &amp; drum and bass firmware for the M-VAVE FM-1</div>
  <div class="tiles">
    <div style="background:{COL[0]}"><small>FIRMWARE</small><b>{VERSION}</b></div>
    <div style="background:{COL[1]}"><small>TRACKS</small><b>8</b></div>
    <div style="background:{COL[2]}"><small>INSTRUMENTS</small><b>8</b></div>
    <div style="background:{COL[3]}"><small>PATTERNS</small><b>64</b></div>
  </div>
  {fig(overview())}
  <div class="covernote">Manual &amp; quick start · github.com/TwistedGenetics/DREADNOUGHT · a beta: back up your FM-1 first (see INSTALLING, UPDATING, RECOVERY)</div>
</section>''')

    # ---- quick start
    o.append(f'<section class="qsintro"><div class="chead" style="background:{COL[0]}"><span class="ct">QUICK START</span></div>'
             f'<p class="lead">Six walks round the box, each a few presses. Every one starts from the screen DREADNOUGHT starts '
             f'on: REESE\'s OSC page. The numbers on the pictures are the order to press things in.</p></section>')
    o.append(qs(1, "HEAR IT", 0, F[1], steps(
        f"Press {k('PLAY')}. The demo pattern plays: REESE, the bass on track 1. {k('PLAY')} again stops it.",
        f"Turn {k('MASTER')} for the volume.",
        f"Turn {k('PRESETS')}: REESE's factory sounds, one a detent (CLASSIC RS, OG REESE, HOLLOW RS, TECHSTEP …). The sound bar names it.",
        f"Turn {k('KNOB 1')} – {k('KNOB 4')}. Each changes the tile in its colour: orange, yellow, green, blue.",
        "Play the keys: notes from F3 up to G5. Hold one and press another: it slides (REESE is mono)."),
        "ui-01-boot.png", "REESE's OSC page: OSC, DTN, BEAT, SUB."))
    o.append(qs(2, "FIND YOUR WAY", 1, F[2], steps(
        f"Tap {k('HOME')}. The eight tracks, each its number, instrument and sound, in its instrument's colour.",
        "Press a white key 1-8 (the first eight from the left): that track's instrument opens on its first page.",
        f"{k('SEL')} {k('EDIT')} {k('ENV')} {k('LFO')} {k('GLO')} are its pages: OSC, FILTER, AMP, MOD, OUT. Press one again for its second page (REESE: ENV again is the filter envelope).",
        f"Or turn {k('SELECT')} to go through every page in turn. The tabs show where you are.") +
        tip(f"From anywhere: hold {k('SEQ')} and press a white key 1-8 to jump to that track's sequencer, or a black key 1-8 to its sound pages.", 2),
        "home-01.png", "HOME: the eight tracks."))
    o.append(qs(3, "MAKE A PATTERN", 2, F[3], steps(
        f"On the track you want, press {k('SEQ')}: its piano roll. (On a break: the slice grid, DRUM.)",
        "Tap white keys: steps 1-16 on and off. A new step gets the note, length and chord you last put in.",
        f"Hold a step: its four values pop up big. Turn {k('KNOB 1')} NOTE, {k('KNOB 2')} LENGTH, {k('KNOB 3')} VEL, {k('KNOB 4')} COND (a chance, FILL, every 2nd time…). {k('FX')} while holding: SLIDE, ACCENT, RETRIG, RATE; again: the retrig's fade and CHORD.",
        f"{k('PLAY')} to hear it. Black keys 10 and 11 show bars 2-4 (64 steps).",
        f"Tap {k('SAVE')} (lit when something has changed) to keep it."),
        "seq-02-step-held.png", "A step held: its values, big."))
    o.append(qs(4, "RECORD LIVE", 3, F[4], steps(
        f"Press {k('REC')} (lit).",
        f"Press {k('PLAY')}: a bar of clicks to count you in, then the pattern runs with a click on the beat.",
        "Play: each note lands on its nearest step, as long as you held it. Keys held together on FM, WAVE, SAMPLER's keys or MIDI make a chord step.",
        f"Press {k('REC')} again to stop recording; {k('PLAY')} to stop.",
        f"Tap {k('SAVE')}.") + tip(f"Stopped, REC records step by step: each key writes the next step. The click and the count-in are on SETUP ({k('GLO')} three times).", 3),
        "progress-04-countin.png", "The count-in: the bar counts down."))
    o.append(qs(5, "CHOOSE THE INSTRUMENTS", 4, F[5], steps(
        f"Press {k('GLO')} five times: OUT, FX, SETUP, MIDI, PROJECT.",
        f"{k('KNOB 1')}: the track.",
        f"{k('KNOB 2')}: an instrument for it: OFF, REESE, BRK A, BRK B, MIDI, SUB, SAMPLER, FM or WAVE.",
        f"{k('FX')} puts it on the track. REESE, SUB, SAMPLER, FM, WAVE and each break are on one track at a time: given to another, the two tracks swap. The patterns stay with their track.",
        f"{k('SAVE')} keeps the layout."),
        "project-05-chosen.png", "PROJECT: a track and its instrument."))
    o.append(qs(6, "PLAY A BREAK", 5, F[6], steps(
        "First send some loops: the breaks page or the web editor's Project tab (see THE WEB EDITOR). Until then the breaks are silent.",
        f"Open a break track (HOME, key 2) and press {k('ARP')}: BRK, the loop's page. {k('ARP')} again: BRK2, then SLICE.",
        f"Turn {k('PRESETS')} to pick the loop.",
        f"{k('SEQ')}: the slice grid. {k('KNOB 1')} picks the slice row; tap white keys to put that slice on those steps.",
        f"{k('PLAY')}. The loop follows the tempo ({k('ALGORITHM')})."),
        "drum-01.png", "DRUM: slices down, steps across."))

    # ---- 1 the panel
    o.append(chapter("THE PANEL", 0, "Everything on the FM-1, and what it does in DREADNOUGHT. The screen tells you what "
                     "the four knobs do on each page; the buttons do the same thing everywhere."))
    o.append(table([
        (k("MASTER"), "Volume."),
        (k("SELECT"), "The page (on the sound pages). On the breaks' pages: BREAKS A / B. On MIDI: which MIDI instrument. With a step held: moves it."),
        (k("PRESETS"), "The track instrument's sounds. On a break: the loop. With SAVE held: the user slot to store into. On ROLL with a step held: its chord."),
        (k("ALGORITHM"), "The tempo, 60-200 BPM."),
        (k("KNOB 1") + " – " + k("KNOB 4"), "The four tiles on the screen, in their colours: orange, yellow, green, blue."),
        (k("OCT-") + " " + k("OCT+"), "Octave, -3 to +3. With a step held: moves its note an octave."),
        (k("SEL") + " " + k("EDIT") + " " + k("ENV") + " " + k("LFO") + " " + k("GLO"), "The sound's five pages (OSC, FILTER, AMP, MOD, OUT on the synths). Pressed again: the page's second view. GLO goes on: FX, SETUP, MIDI, PROJECT."),
        (k("FX"), "More: the next four values of an effect, a held step, an LFO; on PROJECT, puts the instrument on the track; on SETUP, the skin."),
        (k("HOME"), "Tap: the HOME page (the eight tracks). On a sequencer page, held: FILL."),
        (k("SAVE"), "Tap: keep the sound, patterns, slices and layout (lit when something changed). Held with other controls: store, name, copy (see SAVING)."),
        (k("ARP"), "BREAKS: BRK, again BRK2, again SLICE (chop mode)."),
        (k("SEQ"), "This instrument's sequencer page; again: back. Held + a key 1-8: jump to a track."),
        (k("PLAY"), "Start / stop the sequencer (MIDI clock, start and stop go out on USB)."),
        (k("REC"), "Record into the selected track: playing, live; stopped, step by step."),
        ("White keys", "Notes. On ROLL and DRUM: the 16 steps in view. On HOME: tracks 1-8. On SLICE with REC lit: chops."),
        ("Black keys", "Notes. On ROLL and DRUM: 1-8 mute tracks 1-8, 9 held 1 s clears the track, 10 / 11 the bar in view."),
        (k("OCT-") + " + " + k("OCT+") + " held 5 s", "Update mode, for the flasher (recovery)."),
    ], head=["CONTROL", "DOES"]))

    # ---- 2 the screen
    o.append(chapter("THE SCREEN", 1, "DREADNOUGHT's screen is built to be read from arm's length: big values, each knob "
                     "its own coloured tile. This is the default skin, BLOCKS; SETUP has two more (see SETUP AND SKINS)."))
    o.append(fig(screen_fig()))
    o.append("<p>The tile fills from the bottom as far as the value goes; a value that can go either way (a pitch, an "
             "envelope amount) fills from the middle. When something happens (a refusal, a pattern queued, \"STORE TO USER 3\") the "
             "status line says so for a moment.</p>")
    o.append(shots(("home-01.png", "HOME: the tracks in their instrument's colour."),
                   ("seq-02-step-held.png", "A step held on ROLL: its values big."),
                   ("project-04-project.png", "PROJECT: the tracks and their instruments.")))

    # ---- 3 tracks
    o.append(chapter("TRACKS, HOME AND PROJECTS", 2, "A project is eight tracks. Each plays one instrument and has "
                     "8 patterns of up to 64 steps."))
    o.append("<p>Out of the box: track 1 REESE, tracks 2 and 3 BREAKS A and B, tracks 4-8 MIDI (your other gear, "
             "channels 4-8). PROJECT changes that (quick start 5); the instruments you can put on a track:</p>")
    o.append(table([
        (f'<span class="sw" style="background:#FF6A2B"></span><b>REESE</b>', "Detuned saws through a ladder filter: the jungle bass. Mono, slides.", ""),
        (f'<span class="sw" style="background:#3DDC97"></span><b>SUB</b>', "A 101-style mono synth with a dirt stage: dread bass, acid, 808 subs.", ""),
        (f'<span class="sw" style="background:#FFC21A"></span><b>BRK A, BRK B</b>', "Two break players: loops in time with the tempo, sliced and re-sequenced, chopped by hand.", ""),
        (f'<span class="sw" style="background:#C3A2FF"></span><b>SAMPLER</b>', "16 one-shot pads, or a key map of 8 zones (a multisample).", ""),
        (f'<span class="sw" style="background:#5B9BFF"></span><b>FM</b>', "A six-operator DX7-style synth (Dexed's engine): plays DX7 voices. 6 voices.", ""),
        (f'<span class="sw" style="background:#FF5CA8"></span><b>WAVE</b>', "A wavetable synth: 8 tables, your own Serum tables, unison, warps. 6 voices.", ""),
        (f'<span class="sw" style="background:#D6D4CC"></span><b>MIDI</b>', "Plays other gear on its channel over USB. Any number of tracks.", ""),
        (f'<span class="sw" style="background:#3A3D45"></span><b>OFF</b>', "Plays nothing.", ""),
    ], head=["INSTRUMENT", "WHAT IT IS", ""]))
    o.append("<h2>HOME</h2><p>Tap " + k("HOME") + " from any sound page. Each track shows its number, instrument and "
             "sound; a dot lights when it plays a note, a muted track is dim. A white or black key 1-8 opens that track. Or "
             "turn " + k("SELECT") + " to pick one and tap " + k("HOME") + " again. Hold " + k("HOME") + " and turn " + k("SELECT") +
             " to step through the instruments without leaving the page you're on.</p>")
    o.append("<h2>Jumping</h2>" + table([
        (k("SEQ") + " held + white key 1-8", "That track's instrument (on a sequencer page: its sequencer page)."),
        (k("SEQ") + " held + black key 1-8", "That track's sound pages, from anywhere."),
        (k("HOME") + " held + " + k("SELECT"), "The next / previous instrument's pages."),
    ]))
    o.append("<h2>Saving</h2><p>" + k("SAVE") + " keeps everything: each instrument's sound, all 64 patterns, the layout, the "
             "effects, your slices. It comes back at power-on. The project has a name: on PROJECT, hold " + k("SAVE") +
             " and press " + k("FX") + " to name it (see SAVING). The web editor saves whole projects to your computer.</p>")

    # ---- 4 the synths
    o.append(chapter("REESE", 0, "Up to seven drifting saws, a Moog-style ladder with drive, a swept comb for the "
                     "hollow movement and a clean sine on the fundamental. Mono: a key held under another slides."))
    o.append(knobs([
        ("OSC", k("SEL"), "OSC: 2SAW … 7SAW, SAWSQ, 2SIN", "DTN: detune", "BEAT: the movement locked to the tempo", "SUB: the sine"),
        ("FILTER", k("EDIT"), "CUT", "RES", "DRV: drive", "NTCH: the comb"),
        ("AMP", k("ENV"), "ATK", "DEC", "SUS", "REL"),
        ("F.ENV", k("ENV") + " again", "F.ATK", "F.DEC", "F.SUS", "F.REL"),
        ("MOD", k("LFO"), "FENV: filter env amount", "GLIDE (ms)", "L1 RATE", "L1 DEPTH"),
        ("OUT", k("GLO"), "LEVEL", "TRANS", "BPM", "OVERLAP: SLIDE / CUT"),
    ], "PRESETS: CLASSIC RS, OG REESE, HOLLOW RS, TECHSTEP, NEURO RS, SYNC RS, DARK RS, SUB REESE, HOOVER RS, INIT, then "
       "USER 1-8. FX on AMP: ENV SYNC, the envelope's times as note lengths at the tempo."))
    o.append(shots(("ui-01-boot.png", "OSC."), ("ui-02-filter.png", "FILTER."), ("fenv-01-reese.png", "F.ENV (ENV again).")))
    o.append(tip("OVERLAP: SLIDE glides a note into the next one when its length reaches past it (no new attack): the classic rolling reese. CUT retriggers every note.", 0))

    o.append(chapter("SUB", 2, "A 101-style mono synth: saw, pulse with PWM, a sub-oscillator and noise into a "
                     "resonant filter that tracks the keys, then the dirt."))
    o.append(knobs([
        ("OSC", k("SEL"), "SAW", "PULSE", "SUB", "NOISE"),
        ("OSC2", k("SEL") + " again", "PW", "PWM", "PWM RATE", "SUB MODE: -1 OCT / -2 OCT / -2 25%"),
        ("FILTER", k("EDIT"), "CUT", "RES (self-oscillates)", "ENV", "KEY: tracking"),
        ("DIRT", k("EDIT") + " again", "DRIVE", "TYPE: SOFT / HARD / FOLD / RECT", "CRUSH", "TONE"),
        ("AMP", k("ENV"), "ATK", "DEC", "SUS", "REL"),
        ("F.ENV", k("ENV") + " again", "F.ATK", "F.DEC", "F.SUS", "F.REL"),
        ("MOD", k("LFO"), "GLIDE", "PORTA: OFF / AUTO / ON", "ACCENT", "VCA: ENV / GATE"),
        ("OUT", k("GLO"), "LEVEL", "TRANS", "SPLIT: below it, no dirt", "OVERLAP"),
    ], "PRESETS: DREAD, DONK, ROLLER, WOBBLE, ACID 101, 808 SUB, GROWL, SQUARE SB, PUNCH, TECH SUB, CRUSHED, RAVE STAB, "
       "INIT, then USER 1-8. ACCENT plays the sequencer's ACCENT steps louder and brighter, as the 303 does."))
    o.append(shots(("sub-01-osc.png", "OSC."), ("sub-04-dirt.png", "DIRT (EDIT again)."), ("sub-07-mod.png", "MOD.")))

    o.append(chapter("LFOS", 3, "REESE and SUB each have four LFOs, part of their sound: a preset brings its own."))
    o.append("<p>Press " + k("LFO") + " on REESE or SUB: MOD. Again: LFO 1, then 2, 3, 4, then back to MOD. "
             + k("FX") + " shows the LFO's MORE values, and on a DRAW LFO, the drawing.</p>")
    o.append(knobs([
        ("LFO 1-4", k("LFO") + " again", "SHAPE: SINE TRI SAW UP SAW DN SQUARE S&amp;H SMOOTH DRAW", "RATE: Hz, or a note length", "DEST: where it goes", "DEPTH: + or -"),
        ("MORE", k("FX"), "SYNC: FREE / SYNC", "MODE: FREE / RETRIG / 1-SHOT", "PHASE", "SMOOTH"),
        ("DRAW", k("FX") + " again", "the pen (a point's value)", "SMOOTH", "POINTS: 16 / 32", "points in view"),
    ], "DEST: the synth's own values (CUTOFF, RES, DRIVE, NOTCH, DETUNE, PITCH, SUB, LEVEL, F.ENV; SUB's S CUT, S RES …), "
       "the breaks' (A PITCH, A DECAY …) or another LFO's depth. DRAW: the white keys are the points; press one to set it to the pen."))
    o.append(shots(("lfo-01-main.png", "An LFO."), ("lfo-03-more.png", "MORE (FX)."), ("lfo-05-drawn.png", "DRAW: a shape by hand.")))

    o.append(chapter("BREAKS", 1, "Two break players, A and B, on tracks 2 and 3 unless you move them. They play the "
                     "loops you send from the editor or the breaks page, always in time."))
    o.append(knobs([
        ("BRK", k("ARP"), "LOOP (PRESETS too)", "PITCH", "DECAY (127: hold)", "CRUSH"),
        ("BRK2", k("ARP") + " again", "CHOKE: group OFF, 1-4", "SLICES: GRID / AUTO / MANUAL", "STRETCH: REPITCH / SLICE / STRETCH", "LEVEL"),
        ("SLICE", k("ARP") + " again", "CHOP: select", "CURSOR (coarse)", "FINE", "ZOOM"),
    ], f"{k('SELECT')} on these pages: BREAKS A or B. A and B choke each other (group 1) like one sampler voice."))
    o.append(shots(("brk-02-page.png", "BRK."), ("drum-02-playing.png", "DRUM, playing."), ("chop-04-playing.png", "SLICE: chops.")))
    o.append("<h2>The slice grid (DRUM)</h2><p>" + k("SEQ") + " from a break's page. Each row is a slice, each column a step. "
             + k("KNOB 1") + " picks the row; tap white keys to put it on steps. " + k("KNOB 2") + " the track's length, "
             + k("KNOB 3") + " swing, " + k("KNOB 4") + " the steps in view. Hold a step: SLIDE plays it reversed. "
             + k("SAVE") + " + " + k("FX") + ": the break laid out straight, each step its own slice.</p>")
    o.append("<h2>Chop mode (SLICE)</h2><p>Set SLICES to MANUAL on BRK2, then " + k("ARP") + " again. The MPC way: press "
             + k("PLAY") + ", press " + k("REC") + " (lit: the keys cut) and hit the lowest key on the first hit, the next key on "
             "the next, and so on. Each chop lands where you hit it; hit a key again to retake it. " + k("REC") + " off: the keys "
             "play the chops. Chops save themselves.</p>")
    o.append(table([
        (k("KNOB 1"), "Select a chop (the cursor jumps there, you hear it)."),
        (k("KNOB 2") + " / " + k("KNOB 3"), "The cursor: coarse / fine."),
        (k("KNOB 4"), "Zoom: the whole loop … 1/64 of it."),
        (k("FX"), "Move the selected chop to the cursor."),
        (k("HOME"), "Delete the selected chop; held 1 s: delete them all."),
        (k("SAVE") + " + " + k("REC"), "A chop between the others, at the cursor."),
        (k("SAVE") + " + " + k("OCT-") + " / " + k("OCT+"), "Start again from AUTO's hits / from the GRID."),
    ], head=["ON SLICE", "DOES"]))

    o.append(chapter("SAMPLER", 4, "Your samples (sent with the editor) on 16 pads, or mapped across the keys."))
    o.append(knobs([
        ("PAD", k("SEL"), "SAMPLE", "START", "END", "REVERSE"),
        ("FILTER", k("EDIT"), "CUT", "RES", "DRIVE", "CRUSH"),
        ("AMP", k("ENV"), "ATTACK", "HOLD (127: to the end)", "DECAY", "LEVEL"),
        ("MOD", k("LFO"), "PITCH", "FINE", "PAN", "CHOKE"),
        ("OUT", k("GLO"), "MODE: ONE-SHOT / KEYS", "PAD", "BPM", ""),
    ], "ONE-SHOT: 16 pads, played by the first 16 keys and sequenced on DRUM's rows. PRESETS: the pad shown."))
    o.append(knobs([
        ("ZONE", k("SEL"), "SAMPLE", "ROOT", "LOW", "HIGH"),
        ("FILTER", k("EDIT"), "CUT", "RES", "DRIVE", "CRUSH"),
        ("AMP", k("ENV"), "ATTACK", "DECAY", "SUSTAIN", "RELEASE"),
        ("LOOP", k("LFO"), "LOOP: OFF / LOOP / SUST", "L.START", "L.END", "TUNE"),
        ("OUT", k("GLO"), "MODE", "ZONE", "LEVEL", "VOICES (1: mono)"),
    ], "KEYS: 8 zones, each a sample over a range of keys. Sequenced on ROLL; plays chords."))
    o.append(shots(("smp-01-pad.png", "ONE-SHOT: a pad."), ("smp-03-drum.png", "The pads on DRUM."), ("smp-04-keys.png", "KEYS: a zone.")))

    o.append(chapter("FM", 3, "A six-operator FM synth that plays DX7 voices: ten factory voices, and any you keep from "
                     "a DX7 bank in the editor."))
    o.append(knobs([
        ("VOICE", k("SEL"), "ALGO 1-32", "FEEDBACK", "BRIGHT: the modulators", "TIME: every envelope"),
        ("OP", k("EDIT"), "OP: which of the six", "COARSE (ratio / Hz)", "FINE", "LEVEL"),
        ("OP+", k("EDIT") + " again", "OP", "DETUNE", "VEL", "MODE: RATIO / FIXED"),
        ("EG", k("ENV"), "R1", "R2", "R3", "R4"),
        ("EG LVL", k("ENV") + " again", "L1", "L2", "L3", "L4"),
        ("LFO", k("LFO"), "SPEED", "DELAY", "PMD", "AMD"),
        ("LFO+", k("LFO") + " again", "WAVE", "SYNC", "PMS", "KEY SYNC"),
        ("OUT", k("GLO"), "LEVEL", "VOICES 1-6 (1: mono)", "ATTACK", "RELEASE"),
    ], "PRESETS: DUB FM BS, FM REESE, HOLLOW STB, DARK BELL, METAL HIT, FM PLUCK, DARK PAD, GROWL BASS, SINE SUB, INIT VOICE, "
       "then your DX7 voices. BRIGHT, TIME, ATTACK and RELEASE sit on top of the voice without changing it."))
    o.append(shots(("fm-01-voice.png", "VOICE."), ("fm-02-op.png", "OP: the operator's frequency."), ("fm-04-eg.png", "EG: OP3's rates.")))

    o.append(chapter("WAVE", 4, "A wavetable synth (new in 0.32): 8 factory tables and your own Serum tables, morphing "
                     "through the frames, the phase warped, unison, a sub, a filter with drive."))
    o.append(knobs([
        ("WAVE", k("SEL"), "TABLE", "POS: through the frames", "WARP: OFF SYNC BEND MIRROR PWM FM CRUSH", "AMOUNT"),
        ("OSC", k("SEL") + " again", "UNISON 1-4", "DETUNE", "SPREAD: stereo", "SUB: a sine an octave down"),
        ("FILTER", k("EDIT"), "CUT", "RES", "ENV: mod env on the cutoff", "DRIVE"),
        ("AMP", k("ENV"), "ATK", "DEC", "SUS", "REL"),
        ("MOD ENV", k("ENV") + " again", "M.ATK", "M.DEC", "M.SUS", "M.REL"),
        ("MOD", k("LFO"), "POS ENV", "POS LFO", "LFO RATE", "CUT LFO"),
        ("OUT", k("GLO"), "LEVEL", "VOICES 1-6 (1: MONO)", "GLIDE", "TRANS"),
    ], "PRESETS: INIT, SUPERSAW, REESE, PPG BELL, VOX PAD, SYNC LEAD, PWM STRNG, GRIT BASS, ORGAN, FM GROWL, BENT KEYS, CRUSHER."))
    o.append(shots(("wave-01-wave.png", "WAVE."), ("wave-02-osc.png", "OSC (SEL again)."), ("wave-06-mod.png", "MOD.")))
    o.append(table([
        ("ANALOG", "Sine, to triangle, to square, to saw."), ("REESE", "A saw with notches that move: two detuned saws, frozen."),
        ("PPG", "A band of harmonics stepping up, on a thin saw."), ("VOCAL", "The vowels A E I O U, one into the next."),
        ("SYNC", "Hard sync: a saw restarting 1 to 8 times a cycle."), ("PWM", "A pulse from 50 % down to 4 %."),
        ("GRIT", "A sine folded harder and harder."), ("HOLLOW", "Odd harmonics, drawbars moving: an organ going hollow."),
    ], head=["TABLE", "POS 0 → 127"]))
    o.append(tip("WAVE plays 8 oscillators at most: UNISON 1 gives 6 voices, UNISON 2 gives 4, UNISON 3 or 4 gives 2. "
                 "Watch CPU on PROJECT when everything plays at once.", 4, "NOTE"))

    o.append(chapter("MIDI TRACKS", 5, "Tracks set to MIDI play your other gear over USB, each on its channel.", brk=False))
    o.append(knobs([("MIDI", k("GLO") + " ×4", "MIDI: which (1-5…)", "CHANNEL 1-16", "OVERLAP: SLIDE / CUT", "")],
                   f"{k('SELECT')} on MIDI: the next MIDI instrument. With a MIDI track selected the keys play out on its channel. "
                   "SLIDE sends the new note before the old one's note-off: legato on the other synth."))

    # ---- sequencer
    o.append(chapter("THE SEQUENCER", 0, "Every instrument has its own sequencer page: ROLL (a piano roll) for the synths, "
                     "SAMPLER's keys and MIDI; DRUM (a slice grid) for the breaks and SAMPLER's pads."))
    o.append(shots(("roll-01-demo.png", "ROLL: REESE's pattern."), ("trigs-04-roll-marks.png", "Marks: conditions, retrigs, accents."), ("drum-01.png", "DRUM: a break.")))
    o.append(table([
        ("White keys", "The 16 steps in view: tap on / off."),
        ("Black keys 1-8", "Mute / unmute tracks 1-8 (lit: playing)."),
        ("Black key 9, held 1 s", "Clear the track."),
        ("Black keys 10 / 11", "The bar in view: steps 1-16, 17-32, 33-48, 49-64."),
        (k("KNOB 1") + " – " + k("KNOB 4") + " (ROLL)", "Scroll the pitches, transpose the track, its length (1-64), the steps in view."),
        (k("KNOB 1") + " – " + k("KNOB 4") + " (DRUM)", "The slice row, length, swing, the steps in view."),
        (k("FX") + " (ROLL)", "KEYS mode: the keys write notes at a cursor. KNOB 2 their length, KNOB 4 the cursor, HOME a rest."),
        (k("HOME") + " held", "FILL, for FILL / !FILL steps."),
    ], head=["ON ROLL / DRUM", "DOES"]))
    o.append("<h2>A step held</h2><p>Hold a step's key: its values pop up big, four tiles, one a knob. " + k("FX") +
             " (still holding) shows the next four.</p>")
    o.append(knobs([
        ("1", "hold a step", "NOTE (break: slice)", "LENGTH", "VEL", "COND"),
        ("2", k("FX"), "SLIDE (break: reverse)", "ACCENT", "RETRIG", "RATE"),
        ("3", k("FX") + " again", "RT LEN", "RT VEL: fade in / out", "CHORD", ""),
    ]))
    o.append(table([
        ("Move a note", "Hold its step and press the step it should go to (black keys 10 / 11: another bar; onto a note: they swap). Or turn SELECT. OCT- / OCT+: an octave."),
        ("Copy a note", "As moving, with SAVE held."),
        ("Velocity ramp", "Hold the first step, hold REC, press the last; KNOB 3 / 4 the ramp's start and end."),
        ("Retrigs", "RETRIG on: the step repeats at RATE (1/16 a step, 1/32 two …) for RT LEN, its velocity fading (RT VEL): rolls and builds."),
        ("Chords", "FM, WAVE, SAMPLER's keys and MIDI: 15 chords (MIN, MAJ, MIN7, MAJ7, 7, MIN9, MAJ9, ADD9, SUS2, SUS4, 5, DIM, AUG, MIN6, 6). Hold a step and turn PRESETS, or CHORD on page 3, or record keys held together."),
        ("Conditions", "COND: a % chance; FILL / !FILL; PRE / !PRE (the track's last condition); NEI / !NEI (the track before's); 1ST / !1ST; LST / !LST; A:B (the A-th of every B passes)."),
        ("Overlaps", "A note whose length reaches the next slides into it (OVERLAP SLIDE on OUT) or retriggers it (CUT)."),
    ], head=["DOING", "HOW"]))
    o.append("<h2>Patterns</h2><p>Each instrument has 8 patterns, along the top of its sequencer page: the playing one lit, a "
             "queued one flashing, ones with notes marked, and how full pattern memory is.</p>")
    o.append(table([
        (k("SAVE") + " + white key 1-8", "This instrument's pattern 1-8: stopped, at once; playing, when the current one ends."),
        (k("SAVE") + " + black key 10 / 11", "Copy this pattern / paste into this one."),
        (k("SAVE") + " + " + k("OCT-") + " / " + k("OCT+"), "Copy / paste the bar in view."),
        (k("SAVE") + " + " + k("REC"), "×2: the bars with notes copied into the empty bars after them; a full track doubles (16 → 32 → 64 steps)."),
    ], head=["COMBINATION", "DOES"]))
    o.append("<h2>Recording</h2><p>" + k("REC") + " lit, playing: each key writes a note at the nearest step, as long as you "
             "hold it. Stopped: step by step from the cursor; a key held under another writes a slide. Keys held together make a "
             "chord step on the instruments that play chords. A new step you tap in takes the last note, length and chord you "
             "used on that track.</p>")

    # ---- FX
    o.append(chapter("EFFECTS", 2, "Two effect slots, FX A and FX B, fed by each track's sends. A master limiter keeps "
                     "the mix from clipping."))
    o.append(knobs([
        ("MIX", k("GLO") + " ×2", "SEND A (the track)", "SEND B", "RETURN A", "RETURN B"),
        ("FX A", k("FX"), "the effect", "its 1st value", "2nd", "3rd"),
        ("FX A 2", k("FX") + " again", "its 4th", "5th", "6th", "7th"),
        ("FX B, FX B 2", k("FX") + " again", "as FX A", "", "", ""),
    ], "176 KB of effect memory: an effect that won't fit beside the other is refused. DELAY fits beside anything; two of the big reverbs fit; two DELAYs don't."))
    o.append(shots(("project-01-fx-mix.png", "MIX: the sends and returns."), ("fx-01-massive.png", "FX A: MASSIVE."), ("fx-05-quadra-flange.png", "QUADRA, FLANGE.")))
    o.append(table([
        ("DELAY", "The dark half-rate ping-pong.", "TIME (synced), FEEDBACK, TONE"),
        ("TAPE", "Tape echo: wow, flutter, head loss, saturation.", "TIME, FEEDBACK, DEGRADE, PINGPONG"),
        ("BBD", "Bucket brigade: crumbly, dark.", "as TAPE"),
        ("SPRING", "Two springs: the dub drip.", "TIME, DAMP, PREDELAY, LOW CUT"),
        ("TOMB", "A small stone room.", "as SPRING"),
        ("MOD", "A big moving hall.", "as SPRING"),
        ("CELESTIAL", "MOD with an octave-up shimmer.", "as SPRING"),
        ("DOLMEN", "A small dense chamber, bright.", "as SPRING"),
        ("TUMULUS", "A cavern: long and dark.", "as SPRING"),
        ("QUADRA", "Quadraverb-style chains: CHORUS, REVERB, 5EQ>REV, FLANGE, DETUNE, DLY>REV.", "CONFIG, DECAY, TIME, FEEDBACK, MOD, TONE, 16-BIT"),
        ("MASSIVE", "Super-Massive: ROOM, HALL, HALO, SWELL, ABYSS, ECHOVRB, ORBIT, PRISM, PONG, DUB.", "MODE, TIME, FEEDBACK, WARP, DENSITY, MOD, TONE"),
    ], head=["EFFECT", "WHAT IT IS", "VALUES"]))

    # ---- setup
    o.append(chapter("SETUP AND SKINS", 3, f"{k('GLO')} three times."))
    o.append(knobs([("SETUP", k("GLO") + " ×3", "CLICK: OFF / REC / ON", "CLK LVL", "COUNT-IN: off / 1 / 2 bars", "VEL SENS (REESE)")],
                   f"{k('FX')} on SETUP changes the skin: BLOCKS (the default: big coloured tiles), LIGHT (warm white, ink, orange), "
                   "DARK (the first look). SAVE keeps it."))
    o.append(shots(("skin-00-blocks-osc.png", "BLOCKS."), ("skin-01-light-osc.png", "LIGHT."), ("skin-03-dark-osc.png", "DARK.")))

    # ---- saving
    o.append(chapter("SAVING, USER SOUNDS, NAMES", 4, brk=False))
    o.append(table([
        (k("SAVE") + " (tap)", "Keep everything for next time. Lit when something changed."),
        (k("SAVE") + " held + " + k("PRESETS"), "Store REESE's or SUB's sound (with its LFOs) in USER 1-8: turn to pick, let go of SAVE to store. The name editor opens."),
        (k("SAVE") + " held + " + k("FX"), "Name it: on PROJECT the project, on a synth the user sound loaded."),
        ("The name editor", f"{k('KNOB 1')} or {k('SELECT')} the place, {k('KNOB 2')} or {k('PRESETS')} the letter; {k('SAVE')} keeps it, {k('HOME')} leaves it as it was."),
    ], head=["DOING", "HOW"]))
    o.append(tip("FM and WAVE keep their sound with SAVE too; FM's extra voices and WAVE's extra tables live in the sample slots, sent from the editor.", 4))

    # ---- editor
    o.append(chapter("THE WEB EDITOR AND THE BREAKS PAGE", 5, "In Chrome or Edge, with the FM-1 on USB: "
                     "twistedgenetics.github.io/DREADNOUGHT/editor"))
    o.append("<p>The editor opens on a big copy of the FM-1: its screen, knobs, buttons and keys. Everything there does what it "
             "does on the FM-1, and turning a knob on the FM-1 moves the page. The tabs after it edit in depth:</p>")
    o.append(table([
        ("Pattern", "Every track in a mouse-driven piano roll or slice grid; velocity, conditions, retrigs, chords, the 8 patterns."),
        ("REESE, SUB, LFOs", "Every value; LFO shapes drawn with the mouse."),
        ("SAMPLER", "The 16 pads or the key map."),
        ("FM", "Every value of the voice. DX7 banks: load a .syx, hear each voice here or on the FM-1, keep some, send them."),
        ("WAVE", "Every value. Wavetables: load a Serum wavetable or single-cycle WAV, see its frames, hear it, send it: it comes up on TABLE."),
        ("Breaks", "The breaks' settings and slices."),
        ("Project", "Each track's instrument and sends, the FX, memory and CPU; samples (WAV, MP3, AIFF … converted and sent); save and load whole projects."),
    ], head=["TAB", "WHAT'S THERE"]))
    o.append("<p><b>Sample memory:</b> three slots, 80 + 80 + 64 KB. Samples, loops, DX7 voices and wavetables share it. A "
             "wavetable is about 2 KB a frame. The editor sends only what changed.</p>")
    o.append("<p><b>The breaks page</b> (twistedgenetics.github.io/DREADNOUGHT/breaks) sends loops straight to the slots and "
             "sets their slices.</p>")
    o.append(tip("If the editor won't connect: close everything else using the FM-1's MIDI port (a DAW, the flasher or the breaks page in another tab), reload, Connect again. Diagnostics at the bottom of the page lists the ports.", 3))

    # ---- limits
    o.append(chapter("MEMORY AND CPU", 0, brk=False))
    o.append(table([
        ("Patterns", "8 per instrument, 64 steps each; notes of all 64 patterns share memory (about 1 680). The sequencer page shows how full it is."),
        ("Samples", "224 KB in three slots, shared by loops, samples, DX7 voices and wavetables."),
        ("Effects", "176 KB between FX A and FX B."),
        ("CPU", f"PROJECT shows the load and its peak (CPU 40 PK 62%). Too much at once: the status line says AUDIO LATE: CPU FULL. Fewer voices, less unison or a smaller reverb bring it back."),
    ]))

    # ---- recovery
    o.append(chapter("INSTALLING, UPDATING, RECOVERY", 1, brk=False))
    o.append(steps(
        "Open twistedgenetics.github.io/DREADNOUGHT/flasher in Chrome or Edge, the FM-1 on USB.",
        "Back up first: the flasher saves your FM-1's own firmware.",
        "Pick the newest release (or a .fwsc file from GitHub Releases) and press Install. The FM-1 restarts on DREADNOUGHT's screen.",
    ))
    o.append(tip(f"If something goes wrong: hold {k('OCT-')} and {k('OCT+')} together for 5 seconds. The FM-1 goes into "
                 "update mode and the flasher can install again, or put the stock firmware back with M-VAVE's tools. "
                 "docs/RECOVERY.md on GitHub has every way back. Updates keep your sounds, patterns and samples.", 0, "RECOVERY"))

    # ---- reference card
    o.append(chapter("QUICK REFERENCE", 2, "The combinations, on one page."))
    o.append(table([
        (k("HOME"), "the eight tracks; key 1-8 opens one"),
        (k("SEQ"), "this instrument's sequencer; again: back"),
        (k("SEQ") + " + white 1-8", "that track's sequencer / instrument"),
        (k("SEQ") + " + black 1-8", "that track's sound pages"),
        (k("HOME") + " + " + k("SELECT"), "next / previous instrument"),
        (k("SEL") + " … " + k("GLO") + " again", "the page's second view"),
        (k("GLO") + " ×2 / ×3 / ×4 / ×5", "FX · SETUP · MIDI · PROJECT"),
        (k("ARP") + " ×1 / ×2 / ×3", "BRK · BRK2 · SLICE"),
        (k("LFO") + " again (REESE, SUB)", "LFO 1 … 4"),
        ("step held + knobs", "NOTE LENGTH VEL COND; FX: SLIDE ACCENT RETRIG RATE; FX: RT LEN RT VEL CHORD"),
        ("step held + another step", "move it there (SAVE held: copy)"),
        ("step held + " + k("REC") + " + last step", "velocity ramp"),
        (k("SAVE") + " + " + k("PRESETS"), "store to USER 1-8"),
        (k("SAVE") + " + " + k("FX"), "name the project / sound"),
        (k("SAVE") + " + white 1-8", "pattern 1-8"),
        (k("SAVE") + " + black 10 / 11", "copy / paste the pattern"),
        (k("SAVE") + " + " + k("OCT-") + " / " + k("OCT+"), "copy / paste the bar"),
        (k("SAVE") + " + " + k("REC"), "×2: double the pattern"),
        (k("HOME") + " held (sequencer)", "FILL"),
        ("black 9 held 1 s", "clear the track"),
        (k("OCT-") + " + " + k("OCT+") + " 5 s", "update mode"),
    ], head=["PRESS", "FOR"], cls="tbl ref"))
    return "\n".join(o)


CSS = """
@page { size: A4; margin: 14mm 13mm 16mm 13mm; }
* { box-sizing: border-box; }
html { -webkit-print-color-adjust: exact; print-color-adjust: exact; }
body { margin: 0; font-family: 'Barlow Semi Condensed', 'Arial Narrow', Arial, sans-serif; font-weight: 500; font-size: 12.5pt;
       line-height: 1.38; color: #101114; background: #F4F3EE; }
@media screen { body { max-width: 210mm; margin: 0 auto; padding: 14mm 13mm; } }
b, strong { font-weight: 700; }
h2 { font-weight: 700; font-size: 16pt; letter-spacing: .03em; margin: 12pt 0 4pt; text-transform: uppercase; break-after: avoid; }
h2::before { content: ''; display: inline-block; width: 11pt; height: 11pt; background: #FF6A2B; margin-right: 7pt; border-radius: 2pt; }
p { margin: 4pt 0 6pt; }
.k { display: inline-block; font-weight: 700; font-size: .82em; letter-spacing: .04em; padding: 0 6pt; border: 1.6pt solid #101114;
     border-radius: 5pt; background: #FFFFFF; line-height: 1.35; white-space: nowrap; vertical-align: 1pt; }
.cover { break-after: page; }
.brand { font-weight: 700; font-size: 58pt; letter-spacing: .02em; color: #E8471C; line-height: 1; margin-top: 4mm; }
.tagline { font-size: 17pt; font-weight: 600; margin: 3pt 0 10pt; }
.tiles { display: grid; grid-template-columns: repeat(4, 1fr); gap: 5pt; margin: 6pt 0 8pt; }
.tiles div { border-radius: 6pt; padding: 6pt 9pt 4pt; }
.tiles small { display: block; font-weight: 700; font-size: 10pt; letter-spacing: .06em; }
.tiles b { font-size: 30pt; line-height: 1.1; }
.covernote { font-size: 11pt; margin-top: 4pt; }
.fig { margin: 4pt 0; break-inside: avoid; }
.fig svg { width: 100%; height: auto; display: block; }
.fig figcaption { font-size: 10.5pt; }
.chead { display: flex; align-items: center; gap: 10pt; border-radius: 7pt; padding: 7pt 12pt; margin: 0 0 6pt; break-after: avoid; }
.cn { font-weight: 700; font-size: 26pt; line-height: 1; background: #FFFFFF; color: #101114; border: 2.5pt solid #101114; border-radius: 5pt; padding: 1pt 8pt; }
.ct { font-weight: 700; font-size: 26pt; letter-spacing: .03em; line-height: 1.1; }
.chapter { break-before: page; } .chapter.flow { break-before: auto; margin-top: 14pt; }
.lead { font-size: 13.5pt; font-weight: 600; margin: 4pt 0 8pt; }
.qsintro { break-after: avoid; }
.qs { break-inside: avoid; break-before: page; }
.qsintro + .qs { break-before: avoid; }
.qbody { display: grid; grid-template-columns: 1fr 52mm; gap: 12pt; align-items: start; font-size: 13.5pt; }
.qbody .see b { display: block; font-size: 10.5pt; letter-spacing: .08em; margin-bottom: 3pt; }
.qhead { display: flex; align-items: center; gap: 10pt; margin: 6pt 0 0; }
.qn { font-weight: 700; font-size: 28pt; line-height: 1; border-radius: 7pt; padding: 3pt 12pt; }
.qt { font-weight: 700; font-size: 24pt; letter-spacing: .03em; }
.steps { margin: 4pt 0 6pt; padding: 0; counter-reset: s; list-style: none; }
.steps li { counter-increment: s; position: relative; padding: 2pt 0 4pt 30pt; }
.steps li::before { content: counter(s); position: absolute; left: 0; top: 2pt; width: 21pt; height: 21pt; border-radius: 50%;
                    background: #FFFFFF; border: 2pt solid #101114; color: #101114; font-weight: 700; text-align: center; line-height: 17pt; font-size: 12pt; }
.tip { border-left: 7pt solid; border-radius: 6pt; padding: 6pt 10pt; margin: 7pt 0; break-inside: avoid; }
.tip b:first-child { letter-spacing: .06em; margin-right: 4pt; }
table { width: 100%; border-collapse: separate; border-spacing: 0 3pt; margin: 4pt 0 6pt; }
table.tbl th { text-align: left; font-weight: 700; font-size: 10.5pt; letter-spacing: .06em; padding: 3pt 8pt; background: #E4E2DA; }
table.tbl th:first-child { border-radius: 5pt 0 0 5pt; } table.tbl th:last-child { border-radius: 0 5pt 5pt 0; }
table.tbl td { background: #FFFFFF; padding: 3pt 8pt; vertical-align: top; line-height: 1.28; }
table.tbl td:first-child { border-radius: 5pt 0 0 5pt; font-weight: 600; width: 30%; }
table.tbl td:last-child { border-radius: 0 5pt 5pt 0; }
table.tbl tr { break-inside: avoid; }
table.ref td { padding: 3.5pt 8pt; font-size: 12pt; } table.ref td:first-child { width: 42%; }
table.knobs { border-spacing: 3pt 3pt; margin-left: -3pt; width: calc(100% + 6pt); table-layout: fixed; }
table.knobs th { font-weight: 700; font-size: 10.5pt; letter-spacing: .05em; padding: 3pt 6pt; text-align: left; border-radius: 4pt; background: #E4E2DA; }
table.knobs th i { display: inline-block; width: 8pt; height: 8pt; border-radius: 2pt; margin-right: 4pt; }
table.knobs th:nth-child(1) { width: 12%; } table.knobs th:nth-child(2) { width: 13%; }
table.knobs td { background: #FFFFFF; padding: 4pt 6pt; vertical-align: top; border-radius: 4pt; font-size: 11.5pt; line-height: 1.25; }
table.knobs td:first-child { font-size: 12.5pt; }
table.knobs tr { break-inside: avoid; } table.knobs { break-inside: avoid; }
.note { font-size: 11.5pt; margin: 0 0 6pt; }
.sw { display: inline-block; width: 11pt; height: 11pt; border-radius: 2pt; margin-right: 5pt; vertical-align: -1pt; }
.shots { display: grid; grid-template-columns: repeat(4, 1fr); gap: 7pt; margin: 6pt 0; break-inside: avoid; }
.shot { margin: 0; }
.shot img { width: 100%; image-rendering: pixelated; border-radius: 6pt; border: 4pt solid #1A1B1F; display: block; }
.shot figcaption { font-size: 10.5pt; margin-top: 2pt; }
"""


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "build/manual")
    out.mkdir(parents=True, exist_ok=True)
    html = (f'<!doctype html><html lang="en"><head><meta charset="utf-8"><title>DREADNOUGHT Manual</title>'
            f'<meta name="viewport" content="width=device-width, initial-scale=1"><style>{font_face()}{CSS}</style></head>'
            f'<body>{body()}</body></html>')
    hp = out / "DREADNOUGHT-manual.html"
    hp.write_text(html)
    from playwright.sync_api import sync_playwright
    raw = out / "_raw.pdf"
    foot = ('<div style="width:100%;font-family:\'Barlow Semi Condensed\';font-weight:600;font-size:9pt;color:#101114;'
            'padding:0 13mm;display:flex;justify-content:space-between"><span>DREADNOUGHT ' + VERSION +
            ' · MANUAL</span><span><span class="pageNumber"></span></span></div>')
    with sync_playwright() as p:
        b = p.chromium.launch()
        pg = b.new_page()
        pg.goto(hp.resolve().as_uri())
        pg.wait_for_timeout(500)
        pg.pdf(path=str(raw), format="A4", print_background=True, display_header_footer=True,
               header_template="<span></span>", footer_template=foot,
               margin={"top": "14mm", "bottom": "16mm", "left": "13mm", "right": "13mm"})
        b.close()
    # the page colour behind every page (Chromium leaves the margins white)
    from io import BytesIO
    from pypdf import PdfReader, PdfWriter
    from reportlab.lib.pagesizes import A4
    from reportlab.pdfgen import canvas
    bb = BytesIO()
    cv = canvas.Canvas(bb, pagesize=A4)
    cv.setFillColorRGB(0xF4 / 255, 0xF3 / 255, 0xEE / 255)
    cv.rect(0, 0, A4[0], A4[1], stroke=0, fill=1)
    cv.showPage()
    cv.save()
    w = PdfWriter()
    for page in PdfReader(str(raw)).pages:
        bg = PdfReader(BytesIO(bb.getvalue())).pages[0]
        bg.merge_page(page)
        w.add_page(bg)
    w.add_metadata({"/Title": f"DREADNOUGHT {VERSION} Manual", "/Author": "Jamie (TwistedGenetics)"})
    pdf = out / "DREADNOUGHT-manual.pdf"
    with open(pdf, "wb") as f:
        w.write(f)
    raw.unlink()
    print(f"{pdf}: {len(PdfReader(str(pdf)).pages)} pages")


if __name__ == "__main__":
    main()
