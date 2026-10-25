#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The DREADNOUGHT editor end to end in Chromium, in demo mode (the simulated FM-1, web/editor/mock-fm1.js): draw,
lengthen, move, accent, slide and delete notes on the piano roll, place and reverse slices on a break track, turn a
REESE control, play, back up, keep a sound; overlaps (0.8), the view following a dragged or moved note. Checks what reached the simulated FM-1, not just the screen.

    python3 web/editor/test/e2e_demo.py SITE_DIR [SHOTS_DIR]     (SITE_DIR: tools/make_site.py's output)
Needs playwright (pip install playwright)."""
import http.server
import json
import sys
import threading
from functools import partial
from pathlib import Path

from playwright.sync_api import sync_playwright

site, shots = Path(sys.argv[1]), Path(sys.argv[2] if len(sys.argv) > 2 else ".")
ROOT = Path(__file__).resolve().parents[3]
class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a):
        pass


srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), partial(Quiet, directory=str(site)))
threading.Thread(target=srv.serve_forever, daemon=True).start()
url = f"http://127.0.0.1:{srv.server_address[1]}/editor/?demo"
fails = 0


def check(what, ok, got=""):
    global fails
    print(("ok   " if ok else "FAIL ") + what + (f": {got}" if got != "" else ""))
    fails += not ok


# the simulated FM-1's pattern, parsed in the page
DEV = """([t, i]) => { const m = window.__dn.mock, b = m.pattern, o = 8 + t * 516 + 8 * i;
  return {note: b[o], flags: b[o + 1], len: b[o + 2], vel: b[o + 3]}; }"""

with sync_playwright() as pw:
    b = pw.chromium.launch()
    p = b.new_page(viewport={"width": 1280, "height": 900}, accept_downloads=True)
    errs = []
    p.on("pageerror", lambda e: errs.append(str(e)))
    p.goto(url)
    p.wait_for_function("window.__dn && window.__dn.pat")
    p.wait_for_timeout(300)
    # the panel (0.26): the editor opens on the FM-1 itself, its screen as the FM-1's says
    p.wait_for_function("window.__panel.state && window.__panel.state.knobs[0]")
    check("panel: the screen shows REESE's OSC page, its four knobs named", p.locator(".stab.on").inner_text() == "OSC" and
          p.locator(".skn .nm").first.inner_text() == "OSC", p.locator(".stab.on").inner_text())
    p.screenshot(path=str(shots / "editor-00-panel.png"), full_page=True)
    p.click('.pbtn[data-b="EDIT"]')
    p.wait_for_function("window.__panel.state.page === 1")
    cut0 = p.evaluate("() => window.__dn.mock.params[4]")
    kb = p.locator(".skn").first.bounding_box()
    p.mouse.move(kb["x"] + kb["width"] / 2, kb["y"] + 90)
    p.mouse.down()
    p.mouse.move(kb["x"] + kb["width"] / 2, kb["y"] + 50, steps=10)
    p.mouse.up()
    p.wait_for_timeout(400)
    cut1 = p.evaluate("() => window.__dn.mock.params[4]")
    check("panel: EDIT, then KNOB 1 dragged up 40 px: CUT up 5 on the FM-1, the screen shows it", cut1 == cut0 + 5 and
          p.locator(".skn .tx").first.inner_text() == str(cut1), (cut0, cut1, p.locator(".skn .tx").first.inner_text()))
    p.locator('.pk[data-k="7"]').dispatch_event("pointerdown")
    p.wait_for_timeout(250)
    check("panel: a key held on the page is down on the FM-1 (and lit)", p.evaluate("() => window.__dn.mock.ui.keys") == 1 << 7 and
          "lit" in p.locator('.pk[data-k="7"]').get_attribute("class"))
    p.mouse.up()
    p.wait_for_timeout(250)
    check("panel: let go", p.evaluate("() => window.__dn.mock.ui.keys") == 0)
    p.click('.pbtn[data-b="SAVE"]', modifiers=["Shift"])
    p.wait_for_timeout(200)
    check("panel: shift-click holds SAVE down", p.evaluate("() => window.__dn.mock.ui.btn") & (1 << 7) != 0)
    p.click('.pbtn[data-b="SAVE"]')
    p.wait_for_timeout(200)
    check("panel: a click lets it go", p.evaluate("() => window.__dn.mock.ui.btn") & (1 << 7) == 0)
    p.keyboard.down("z")
    p.wait_for_timeout(200)
    check("panel: the computer's Z plays key 1 (F3)", p.evaluate("() => window.__dn.mock.ui.keys") == 1)
    p.keyboard.up("z")
    p.wait_for_timeout(200)
    p.click(".stab >> text=AMP")
    p.wait_for_function("window.__panel.state.page === 2")
    check("panel: a tab clicked goes to that page (AMP)", p.locator(".stab.on").inner_text() == "AMP")
    p.click('.pbtn[data-b="SEQ"]')
    p.wait_for_function("window.__panel.state.seq")
    check("panel: SEQ, a sequencer page: the Pattern view offered", p.locator("#s-seq").is_visible())
    p.click("#p-seqopen")
    p.wait_for_timeout(300)
    check("panel: it opens the Pattern view", p.locator("#pattern").is_visible())
    box = p.locator("#roll").bounding_box()
    geo = p.evaluate("""() => { const S = window.__dn, cv = document.getElementById('roll');
      return {cw: (cv.clientWidth - 46) / S.pat.tracks[S.sel].len, rh: 14, lo: S.lo}; }""")
    X = lambda step, frac=0.5: box["x"] + 46 + (step + frac) * geo["cw"]
    Y = lambda note: box["y"] + ((geo["lo"] + 35) - note) * 14 + 7
    p.screenshot(path=str(shots / "editor-01-pattern.png"), full_page=True)

    # a new note on track 1, step 9 (index 8), pitch lo+20, dragged out to 3 steps
    pitch = geo["lo"] + 20
    p.mouse.move(X(8, 0.2), Y(pitch))
    p.mouse.down()
    p.mouse.move(X(10, 0.95), Y(pitch), steps=6)
    p.mouse.up()
    p.wait_for_timeout(600)
    d = p.evaluate(DEV, [0, 8])
    check("a drawn note reaches the FM-1 (note, on)", d["note"] == pitch and d["flags"] & 1, d)
    check("dragged out to 3 steps (12 quarter steps)", d["len"] == 12, d["len"])
    # moved one step later and two notes up
    p.mouse.move(X(8, 0.3), Y(pitch))
    p.mouse.down()
    p.mouse.move(X(9, 0.3), Y(pitch + 2), steps=5)
    p.mouse.up()
    p.wait_for_timeout(600)
    d9, d8 = p.evaluate(DEV, [0, 9]), p.evaluate(DEV, [0, 8])
    check("moved: the note is on step 10, two notes up", d9["note"] == pitch + 2 and d9["flags"] & 1 and not d8["flags"] & 1, d9)
    # accent (shift-click), slide (double-click), delete (right-click)
    p.keyboard.down("Shift")
    p.mouse.click(X(9, 0.3), Y(pitch + 2))
    p.keyboard.up("Shift")
    p.wait_for_timeout(500)
    check("shift-click: accent", p.evaluate(DEV, [0, 9])["flags"] & 4)
    p.mouse.dblclick(X(9, 0.3), Y(pitch + 2))
    p.wait_for_timeout(500)
    check("double-click: slide", p.evaluate(DEV, [0, 9])["flags"] & 2)
    p.screenshot(path=str(shots / "editor-02-edited.png"), full_page=True)
    p.mouse.click(X(9, 0.3), Y(pitch + 2), button="right")
    p.wait_for_timeout(600)
    check("right-click: deleted", not p.evaluate(DEV, [0, 9])["flags"] & 1)

    # 0.8: overlaps slide (a ramp, and Overlap: Cut per track), the view follows a dragged / moved note
    p.mouse.move(X(14, 0.3), Y(41))                     # the demo's last note (step 15, F2 with an accent)
    p.evaluate("""() => { const S = window.__dn, st = S.pat.tracks[0].steps; st[12].len = 12; }""")
    p.select_option("#overlap", "cut")
    p.wait_for_timeout(600)
    check("Overlap: Cut reaches the FM-1 (pattern pad[0])", p.evaluate("() => window.__dn.mock.pattern[5]") & 1 == 1)
    p.select_option("#overlap", "slide")
    p.wait_for_timeout(600)
    check("Overlap: Slide again", p.evaluate("() => window.__dn.mock.pattern[5]") & 1 == 0)
    p.screenshot(path=str(shots / "editor-07-overlap.png"), full_page=True)
    # 2 bars of track, 1 bar in view
    p.fill("#trklen", "32")
    p.dispatch_event("#trklen", "change")
    p.select_option("#zoom", "16")
    p.wait_for_timeout(300)
    z = p.evaluate("() => { const S = window.__dn; return {x0: S.x0, zoom: S.zoom, lo: S.lo}; }")
    check("1 bar in view of 2", z["zoom"] == 16 and z["x0"] == 0, z)
    box = p.locator("#roll").bounding_box()            # (again: the tool row above may have wrapped since)
    cw = (box["width"] - 46) / 16
    XZ = lambda step, frac=0.5: box["x"] + 46 + (step - p.evaluate("() => window.__dn.x0") + frac) * cw
    YZ = lambda note: box["y"] + ((p.evaluate("() => window.__dn.lo") + 35) - note) * 14 + 7
    # drag the first note (step 1) up past the top of the roll and hold there: the roll scrolls, the note comes along
    lo0 = z["lo"]
    n0 = p.evaluate(DEV, [0, 0])["note"]
    p.mouse.move(XZ(0, 0.3), YZ(n0))
    p.mouse.down()
    p.mouse.move(XZ(0, 0.3), box["y"] - 30, steps=8)
    p.wait_for_timeout(1200)
    p.mouse.up()
    p.wait_for_timeout(600)
    lo1, n1 = p.evaluate("() => window.__dn.lo"), p.evaluate(DEV, [0, 0])["note"]
    check("dragged past the top: the roll scrolled up and the note came along", lo1 > lo0 + 4 and n1 > lo1 + 30 and n1 > n0,
          (lo0, lo1, n0, n1))
    # arrows: the selected note along the steps, past the bar in view: the view follows it into bar 2
    for _ in range(12):
        p.keyboard.press("ArrowRight")
    p.wait_for_timeout(600)
    f = p.evaluate("() => { const S = window.__dn; return {sel: S.selNote, x0: S.x0}; }")
    check("ArrowRight x12 (over the other notes): into bar 2, the view with it", f["sel"] >= 16 and f["x0"] > 0 and
          f["sel"] < f["x0"] + 16, f)
    check("... and on the FM-1", p.evaluate(DEV, [0, f["sel"]])["note"] == n1 and not p.evaluate(DEV, [0, 0])["flags"] & 1)
    # a drag past the right end scrolls on too
    s0 = f["sel"]
    p.mouse.move(XZ(s0, 0.3), YZ(n1))
    p.mouse.down()
    p.mouse.move(box["x"] + box["width"] + 10, YZ(n1), steps=6)
    p.wait_for_timeout(2500)
    p.mouse.up()
    p.wait_for_timeout(300)
    f2 = p.evaluate("() => { const S = window.__dn; return {sel: S.selNote, x0: S.x0}; }")
    check("dragged past the right end: the steps scroll on (to the last bar's end)", f2["sel"] == 31 and f2["x0"] == 16, f2)
    p.screenshot(path=str(shots / "editor-08-follow.png"), full_page=True)
    p.select_option("#zoom", "0")

    # 0.11: the selected note's inspector: a condition (50%) and a retrig with a fade; a velocity ramp; patterns
    p.select_option("#zoom", "0")
    p.wait_for_timeout(200)
    rb = p.locator("#roll").bounding_box()          # click the demo's step 4 (an accented F) to select it
    gg = p.evaluate("""() => { const S = window.__dn, t = S.pat.tracks[0];
      return {cw: (document.getElementById('roll').clientWidth - 46) / t.len, lo: S.lo, note: t.steps[3].note}; }""")
    p.mouse.click(rb["x"] + 46 + 3.4 * gg["cw"], rb["y"] + ((gg["lo"] + 35) - gg["note"]) * 14 + 7)
    p.wait_for_timeout(300)
    p.select_option("#i-cond", label="50%")
    p.check("#i-rtrg")
    p.select_option("#i-rtim", label="1/32")
    p.evaluate("""() => { const r = document.getElementById('i-rvel'); r.value = -127; r.dispatchEvent(new Event('input')); }""")
    p.wait_for_timeout(700)
    d4 = p.evaluate("""() => { const m = window.__dn.mock, b = m.pattern, o = 8 + 8 * 3;
      return {cond: b[o + 4], flags: b[o + 1], rtim: b[o + 5], rvel: (b[o + 7] << 24) >> 24}; }""")
    check("inspector: step 4's condition 50% and a 1/32 retrig fading out reach the FM-1",
          d4["cond"] == 11 and d4["flags"] & 8 and d4["rtim"] == 12 and d4["rvel"] == -127, d4)
    p.screenshot(path=str(shots / "editor-10-inspector.png"), full_page=True)
    vb = p.locator("#vel").bounding_box()
    cwv = (vb["width"] - 46) / p.evaluate("() => window.__dn.pat.tracks[0].len")
    p.keyboard.down("Shift")
    p.mouse.move(vb["x"] + 46 + 0.5 * cwv, vb["y"] + vb["height"] - 8)
    p.mouse.down()
    p.mouse.move(vb["x"] + 46 + 14.5 * cwv, vb["y"] + 6, steps=8)
    p.mouse.up()
    p.keyboard.up("Shift")
    p.wait_for_timeout(700)
    v = p.evaluate("""() => { const m = window.__dn.mock, b = m.pattern;
      return [3, 6, 7, 10, 12, 14].map((i) => b[8 + 8 * i + 3]); }""")   # (the notes on steps 4..15)
    check("shift-drag in the velocity lane: a ramp from soft to loud", all(v[k] < v[k + 1] for k in range(5)) and v[0] < 40 and v[5] > 110, v)
    p.wait_for_timeout(400)
    p.locator("#pats .pat").nth(1).click()
    p.wait_for_timeout(500)
    pc = p.evaluate("() => ({cur: window.__dn.mock.patCur[0], on: window.__dn.pat.tracks[0].steps.filter((s) => s.flags & 1).length})")
    check("pattern 2: the FM-1 switches (stopped: at once) and the roll shows its (empty) notes", pc["cur"] == 1 and pc["on"] == 0, pc)
    p.screenshot(path=str(shots / "editor-11-pattern2.png"), full_page=True)
    p.locator("#pats .pat").nth(0).click()
    p.wait_for_timeout(500)
    check("pattern 1 again: its notes back", p.evaluate("() => window.__dn.pat.tracks[0].steps.filter((s) => s.flags & 1).length") == 7)

    # chords (0.28): on a MIDI track, a note, then MIN7 from the Chord list under the roll: the step plays it
    p.locator(".trk").nth(4).click()
    p.wait_for_timeout(200)
    g5 = p.evaluate("""() => { const S = window.__dn, cv = document.getElementById('roll');
      return {cw: (cv.clientWidth - 46) / S.pat.tracks[S.sel].len, lo: S.lo}; }""")
    rb = p.locator("#roll").bounding_box()
    X5 = lambda step: rb["x"] + 46 + (step + 0.5) * g5["cw"]
    Y5 = lambda note: rb["y"] + ((g5["lo"] + 35) - note) * 14 + 7
    p.mouse.click(X5(2), Y5(g5["lo"] + 10))
    p.wait_for_timeout(400)
    check("chords: the Chord list shows for a MIDI track's note", p.locator("#i-chordl").is_visible())
    p.select_option("#i-chord", label="MIN7")
    p.wait_for_timeout(900)
    st5 = p.evaluate(DEV, [4, 2])
    check("chords: MIN7 picked: the FM-1's step has chord 3 on its note", st5["note"] == g5["lo"] + 10 and (st5["flags"] >> 4) == 3, st5)
    p.screenshot(path=str(shots / "editor-chord.png"), full_page=False)
    # a chord's upper note dragged up 2: the whole chord moves (its root with it)
    p.mouse.move(X5(2), Y5(g5["lo"] + 17))
    p.mouse.down()
    p.mouse.move(X5(2), Y5(g5["lo"] + 19), steps=4)
    p.mouse.up()
    p.wait_for_timeout(900)
    st5 = p.evaluate(DEV, [4, 2])
    check("chords: dragging one of its notes moves the chord", st5["note"] == g5["lo"] + 12 and (st5["flags"] >> 4) == 3, st5)
    p.select_option("#i-chord", label="None")
    p.wait_for_timeout(900)
    st5 = p.evaluate(DEV, [4, 2])
    check("chords: None: a single note again", st5["flags"] >> 4 == 0 and st5["flags"] & 1 == 1, st5)
    p.locator(".trk").nth(0).click()
    p.wait_for_timeout(200)
    p.mouse.click(X5(0), Y5(g5["lo"] + 2))
    p.wait_for_timeout(300)
    check("chords: no Chord list on a track that plays one note (REESE)", not p.locator("#i-chordl").is_visible())

    # BREAKS A: a slice on a step, reversed
    p.locator(".trk").nth(1).click()
    p.wait_for_timeout(200)
    bbox = p.locator("#roll").bounding_box()
    rh = p.evaluate("() => { const S = window.__dn, cv = document.getElementById('roll'); return cv.clientHeight / 32; }")
    p.mouse.click(bbox["x"] + 46 + 3.5 * geo["cw"], bbox["y"] + 5.5 * rh)
    p.wait_for_timeout(600)
    d = p.evaluate(DEV, [1, 3])
    check("BREAKS: slice 6 on step 4", d["note"] == 5 and d["flags"] & 1, d)
    p.mouse.dblclick(bbox["x"] + 46 + 3.5 * geo["cw"], bbox["y"] + 5.5 * rh)
    p.wait_for_timeout(600)
    check("BREAKS: double-click reverses it", p.evaluate(DEV, [1, 3])["flags"] & 2)
    p.screenshot(path=str(shots / "editor-03-breaks.png"), full_page=True)

    # 0.9: the LFOs: LFO 1 to CUTOFF, then a shape drawn on its canvas (it becomes DRAW), a preset on LFO 2
    p.click("[data-tab=lfos]")
    p.wait_for_timeout(300)
    p.evaluate("""() => { const el = document.querySelector('.prm[data-i="38"] input'); el.value = 1;
      el.dispatchEvent(new Event('input')); }""")
    p.wait_for_timeout(300)
    check("LFO 1 DEST reaches the FM-1 (CUTOFF)", p.evaluate("() => window.__dn.mock.params[38]") == 1)
    cvb = p.locator(".lfo canvas").first.bounding_box()
    p.mouse.move(cvb["x"] + 3, cvb["y"] + 5)
    p.mouse.down()
    p.mouse.move(cvb["x"] + cvb["width"] - 3, cvb["y"] + cvb["height"] - 5, steps=30)
    p.mouse.up()
    p.wait_for_timeout(500)
    m = p.evaluate("() => ({shape: window.__dn.mock.params[35], pts: window.__dn.mock.lfo[0].pts})")
    check("drawn: LFO 1 is DRAW on the FM-1", m["shape"] == 7, m["shape"])
    check("drawn: the points fall from top left to bottom right", m["pts"][0] > 80 and m["pts"][15] < -80, m["pts"][:16])
    p.locator(".lfo").nth(1).get_by_text("Trance gate").click()
    p.wait_for_timeout(500)
    m2 = p.evaluate("() => window.__dn.mock.lfo[1].pts")
    check("preset: LFO 2 a trance gate", m2[:8] == [100, 100, -100, 100, -100, 100, 100, -100], m2[:8])
    p.screenshot(path=str(shots / "editor-09-lfos.png"), full_page=True)

    # a REESE control and the transport
    p.click("[data-tab=sound]")
    p.wait_for_timeout(200)
    p.screenshot(path=str(shots / "editor-04-reese.png"), full_page=True)
    p.evaluate("""() => { const r = document.querySelector('.prm[data-i="4"] input'); r.value = 90;
      r.dispatchEvent(new Event('input')); }""")
    p.wait_for_timeout(400)
    check("REESE CUT reaches the FM-1", p.evaluate("() => window.__dn.mock.params[4]") == 90)
    # SUB (0.13): its tab, a value reaching the FM-1, a factory sound
    p.click("[data-tab=subtab]")
    p.wait_for_timeout(200)
    p.evaluate("""() => { const r = document.querySelector('.prm[data-i="75"] input'); r.value = 100;
      r.dispatchEvent(new Event('input')); }""")
    p.wait_for_timeout(400)
    check("SUB CUT (value 75) reaches the FM-1", p.evaluate("() => window.__dn.mock.params[75]") == 100)
    p.select_option("#subpreset", "4")
    p.wait_for_timeout(300)
    check("SUB: a factory sound (ACID 101) asked for", p.evaluate("() => window.__dn.mock.subPreset") == 4)
    p.screenshot(path=str(shots / "editor-13-sub.png"), full_page=True)
    p.click("#play")
    p.wait_for_timeout(500)
    check("PLAY starts the FM-1", p.evaluate("() => window.__dn.mock.playing") == 1)
    p.click("[data-tab=breaks]")
    p.wait_for_timeout(200)
    p.screenshot(path=str(shots / "editor-05-breaks-tab.png"), full_page=True)

    # the project (0.12): a track's instrument and send, a sample in (mono, 16 kHz) and on the FM-1, the whole project
    # saved to a file, changed on the FM-1, loaded back
    p.click("#play")                                 # (stopped: a project is read and written stopped)
    p.wait_for_timeout(300)
    p.click("[data-tab=files]")
    p.wait_for_function("window.__dn.mediaRead")
    p.wait_for_timeout(300)
    check("project: 8 tracks listed", p.locator("#ptracks tr").count() == 8)
    p.locator("#ptracks tr").nth(3).locator("select").select_option("1")
    p.wait_for_timeout(400)
    eng = p.evaluate("() => window.__dn.mock.proj.eng")
    check("track 4 to REESE: it swaps with track 1 on the FM-1", eng[:4] == [4, 2, 3, 1], eng)
    p.locator("#ptracks tr").nth(3).locator("input[type=range]").first.fill("90")
    p.wait_for_timeout(400)
    check("track 4's send A reaches the FM-1", p.evaluate("() => window.__dn.mock.proj.send[0][3]") == 90)
    import io, math, struct, wave
    wav = io.BytesIO()
    with wave.open(wav, "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(44100)
        w.writeframes(b"".join(struct.pack("<hh", int(12000 * math.sin(i / 9)), int(-6000 * math.sin(i / 9))) for i in range(44100)))
    p.set_input_files("#mfiles", files=[{"name": "kick.wav", "mimeType": "audio/wav", "buffer": wav.getvalue()}])
    p.wait_for_function("window.__dn.media.length === 1")
    p.locator("#mlist tr").first.locator("select").nth(1).select_option("16000")
    p.wait_for_timeout(100)
    check("a 1 s stereo WAV at 16 kHz mono: 8 KB", "7.8 KB" in p.locator("#mlist tr").first.inner_text(), p.locator("#mlist tr").first.inner_text())
    p.screenshot(path=str(shots / "editor-12-project.png"), full_page=True)
    p.click("#msend")
    p.wait_for_function("window.__dn.mock.slots[0].ok", timeout=20000)
    z = p.evaluate("() => { const b = window.__dn.mock.slots[0].buf, v = new DataView(b.buffer); return {nz: b[6], n: v.getUint32(36, true), rate: v.getUint32(48, true)}; }")
    check("the sample is on the FM-1: one zone, ~16000 samples, at 16 kHz", z["nz"] == 1 and 15000 < z["n"] <= 16000 and z["rate"] == round(16000 / 44100 * 65536), z)
    with p.expect_download(timeout=60000) as dl:
        p.click("#psave")
    data = json.loads(Path(dl.value.path()).read_text())
    check("project file: the layout, 8 patterns, the samples and the original", data["format"] == "dreadnought-project" and
          data["proj"]["eng"][:4] == [4, 2, 3, 1] and len(data["patterns"]) == 8 and data["slots"][0] and data["media"][0]["file"], data["proj"]["eng"])
    p.evaluate("() => { const P = window.__dn.mock.proj; P.eng = [1, 2, 3, 4, 4, 4, 4, 4]; P.send[0][3] = 0; window.__dn.mock.rev++; }")
    with p.expect_file_chooser() as fc:
        p.click("#pload")
    fc.value.set_files(dl.value.path())
    p.wait_for_function("window.__dn.mock.proj.eng[3] === 1", timeout=60000)
    p.wait_for_function("document.getElementById('status').textContent.includes('is on the FM-1')", timeout=60000)
    p.wait_for_timeout(300)
    check("loaded back: the layout and the send", p.evaluate("() => window.__dn.mock.proj.send[0][3]") == 90)
    check("loaded back: the sample can be converted again (its original came with it)",
          p.evaluate("() => !!(window.__dn.media[0] && window.__dn.media[0].ch)"))
    p.fill("#sndname", "Dark one")
    p.click("#keep")
    check("library: kept", p.locator("#library li").count() == 1)
    p.screenshot(path=str(shots / "editor-06-files.png"), full_page=True)
    saved0 = p.evaluate("() => window.__dn.mock.saved")
    p.click("#save")
    p.wait_for_timeout(300)
    check("Save on FM-1", p.evaluate("() => window.__dn.mock.saved") == saved0 + 1)
    p.click("#diag summary")
    p.wait_for_timeout(200)
    dl = p.locator("#diaglog").inner_text()
    check("Diagnostics: the ports, the INFO frame sent and its answer", "f0 7d 46 4c 01 f7" in dl and "answered" in dl, dl[:200])
    # SAMPLER (0.23): its tab, pad 3 given the second loop and reversed, a hit; KEYS: zone 2's root by the map
    p.click("#smpTabBtn")
    p.wait_for_timeout(200)
    check("SAMPLER: 16 pads on its tab", p.locator(".pad").count() == 16, p.locator(".pad").count())
    p.locator(".pad").nth(2).click()
    p.wait_for_timeout(150)
    check("SAMPLER: a pad clicked plays (SMP_PLAY)", p.evaluate("() => window.__dn.mock.smpPlayed || 0") >= 1)
    p.locator("#padEd select").first.select_option("1")
    rev = p.locator("#padEd .prm").filter(has_text="REVERSE").locator("input")
    rev.evaluate("(r) => { r.value = 1; r.dispatchEvent(new Event('input')); }")
    p.wait_for_timeout(200)
    got = p.evaluate("() => window.__dn.mock.smp.pads[2].slice(0, 4)")
    check("SAMPLER: pad 3 -> loop 2, reversed, on the FM-1", got == [1, 0, 1000, 1], got)
    check("SAMPLER: the pad shows its sample", "STEPPER" in p.locator(".pad").nth(2).inner_text(), p.locator(".pad").nth(2).inner_text())
    p.screenshot(path=str(shots / "editor-sampler-pads.png"), full_page=True)
    p.select_option("#smpmode", "1")
    p.wait_for_timeout(200)
    check("SAMPLER: KEYS mode on the FM-1", p.evaluate("() => window.__dn.mock.smp.mode") == 1)
    zb = p.locator("#zmap").bounding_box()
    p.mouse.click(zb["x"] + 30, zb["y"] + 6 + 18 * 1 + 9)   # zone 2's row
    p.wait_for_timeout(100)
    check("SAMPLER: zone 2 picked on the map", p.evaluate("() => window.__dn.zone") == 1)
    p.locator("#zoneEd select").first.select_option("0")
    root = p.locator("#zoneEd .prm").filter(has_text="ROOT").locator("input")
    root.evaluate("(r) => { r.value = 48; r.dispatchEvent(new Event('input')); }")
    p.wait_for_timeout(200)
    got = p.evaluate("() => window.__dn.mock.smp.zones[1].slice(0, 2)")
    check("SAMPLER: zone 2 -> loop 1, root C2", got == [0, 48], got)
    p.screenshot(path=str(shots / "editor-sampler-keys.png"), full_page=True)
    # FM (0.25): its tab (the factory voices, the values), a voice picked, ALGO changed; a DX7 bank on the Project tab,
    # 5 of its voices picked and sent: they're FM's voices on the FM-1 too
    p.click("#fmTabBtn")
    p.wait_for_timeout(200)
    check("FM: the factory's 10 voices", p.locator("#fmpreset option").count() == 10, p.locator("#fmpreset option").count())
    check("FM: OP1..OP6 and the voice's values on the tab", p.locator("#fmEd .opgrp").count() == 6 and p.locator("#fmEd .prm").count() > 150,
          p.locator("#fmEd .prm").count())
    p.select_option("#fmpreset", "3")
    p.wait_for_timeout(300)
    check("FM: voice 4 (DARK BELL) on the FM-1", p.evaluate("() => String.fromCharCode(...window.__dn.mock.fm.voice.slice(145, 154))") == "DARK BELL",
          p.evaluate("() => String.fromCharCode(...window.__dn.mock.fm.voice.slice(145, 155))"))
    algo = p.locator("#fmEd .prm").filter(has_text="ALGO").locator("input")
    algo.evaluate("(r) => { r.value = 21; r.dispatchEvent(new Event('input')); }")
    p.wait_for_timeout(250)
    check("FM: ALGO 22 on the FM-1", p.evaluate("() => window.__dn.mock.fm.voice[134]") == 21)
    check("FM: the tab names the carriers of algorithm 22 (OP1, OP3, OP4, OP5)", "OP1, OP3, OP4, OP5 sound" in p.locator("#fmalg").inner_text(),
          p.locator("#fmalg").inner_text())
    p.screenshot(path=str(shots / "editor-fm.png"), full_page=True)
    bank = bytearray([0xF0, 0x43, 0x00, 0x09, 0x20, 0x00])
    data = bytearray()
    for v in range(32):
        vb = bytearray(128)
        for op in range(6):
            o = op * 17
            vb[o:o + 4] = bytes([99, 99, 99, 99]); vb[o + 4:o + 8] = bytes([99, 99, 99, 0]); vb[o + 8] = 39
            vb[o + 12] = 7 << 3; vb[o + 14] = 99 if op == 5 else 0; vb[o + 15] = 1 << 1
        vb[110] = v % 32; vb[111] = 8; vb[112] = 35; vb[116] = 1 | 3 << 4; vb[117] = 24
        vb[118:128] = f"BANK {v + 1:02d}".ljust(10).encode()
        data += vb
    bank += data + bytes([(128 - (sum(data) & 127)) & 127, 0xF7])
    p.click("[data-tab=files]")
    p.wait_for_timeout(200)
    n0 = p.evaluate("() => window.__dn.media.length")
    p.set_input_files("#mfiles", files=[{"name": "mybank.syx", "mimeType": "application/octet-stream", "buffer": bytes(bank)}])
    p.wait_for_function(f"window.__dn.media.length === {n0 + 1}")
    p.wait_for_timeout(100)
    check("DX7: the bank's 32 voices listed", p.locator(".dx7pick label").count() == 32, p.locator(".dx7pick label").count())
    p.evaluate("() => { window.__played = 0; const C = window.AudioBufferSourceNode.prototype.start; window.AudioBufferSourceNode.prototype.start = function (...a) { window.__played++; return C.apply(this, a); }; }")
    p.locator(".dx7pick label").first.locator("button").first.click()
    p.wait_for_timeout(300)
    check("DX7: ▶ plays the voice here, before it's sent (nothing sent to the FM-1)", p.evaluate("() => window.__played") == 1 and
          p.evaluate("() => window.__dn.mock.fmPlayed") == 0)
    boxes = p.locator(".dx7pick input[type=checkbox]")
    for i in range(5, 32):
        boxes.nth(i).uncheck()
    p.wait_for_timeout(100)
    row = p.locator("#mlist tr").filter(has_text="DX7")
    check("DX7: 5 of 32 picked, 0.6 KB", "5 of 32 voices" in row.inner_text() and "0.6 KB" in row.inner_text(), row.inner_text())
    p.screenshot(path=str(shots / "editor-dx7-bank.png"), full_page=True)
    p.click("#msend")
    p.wait_for_timeout(3000)
    names = p.evaluate("() => window.__dn.fmNames")
    check("DX7: sent, FM's voices now 10 + 5, BANK 01 first of them", len(names) == 15 and names[10] == "BANK 01", names[9:12])
    check("DX7: not taken for a loop (the breaks' list as it was)", p.evaluate("() => window.__dn.loops.length") == 2)
    # (0.29) the FM tab's bank browser: a second bank loaded there, a voice heard here and on the FM-1, two kept, sent
    bank2 = bytearray(bank)
    for v in range(32):
        bank2[6 + v * 128 + 118:6 + v * 128 + 128] = f"TWO {v + 1:02d}".ljust(10).encode()
    d2 = bank2[6:6 + 4096]
    bank2[6 + 4096] = (128 - (sum(d2) & 127)) & 127
    p.click("#fmTabBtn")
    p.wait_for_timeout(200)
    check("FM banks: the voices kept before shown on the FM tab (5 of 32, on the FM-1)",
          p.locator("#fmBankList .fmbank").count() >= 1 and "5 of 32 kept" in p.locator("#fmBankList .fmbank").first.inner_text(),
          p.locator("#fmBankList").inner_text()[:120])
    p.set_input_files("#fmBankFile", files=[{"name": "second.syx", "mimeType": "application/octet-stream", "buffer": bytes(bank2)}])
    p.wait_for_timeout(300)
    last = p.locator("#fmBankList .fmbank").last
    check("FM banks: the loaded bank's 32 voices, none kept yet", last.locator(".fmtile").count() == 32 and "0 of 32 kept" in last.inner_text(),
          last.locator(".fmtile").count())
    p.evaluate("() => { window.__played = 0; }")
    f0 = p.evaluate("() => window.__dn.mock.fmPlayed")
    last.locator(".fmtile").nth(2).locator("button").first.click()
    p.wait_for_timeout(300)
    check("FM banks: ▶ plays voice 3 here (nothing sent)", p.evaluate("() => window.__played") == 1 and p.evaluate("() => window.__dn.mock.fmPlayed") == f0)
    last.locator(".fmtile").nth(2).locator("button").nth(1).click()
    p.wait_for_timeout(600)
    check("FM banks: FM-1 makes voice 3 FM's voice there and plays it",
          p.evaluate("() => String.fromCharCode(...window.__dn.mock.fm.voice.slice(145, 151))") == "TWO 03" and p.evaluate("() => window.__dn.mock.fmPlayed") > f0,
          p.evaluate("() => String.fromCharCode(...window.__dn.mock.fm.voice.slice(145, 155))"))
    last = p.locator("#fmBankList .fmbank").last
    last.locator(".fmtile").nth(0).locator("input").check()
    p.wait_for_timeout(100)
    p.locator("#fmBankList .fmbank").last.locator(".fmtile").nth(1).locator("input").check()
    p.wait_for_timeout(100)
    check("FM banks: 7 voices kept, not sent yet", "7 voices kept" in p.locator("#fmKept").inner_text() and "not sent" in p.locator("#fmKept").inner_text(),
          p.locator("#fmKept").inner_text())
    p.screenshot(path=str(shots / "editor-fm-banks.png"), full_page=True)
    p.click("#fmSendKept")
    p.wait_for_timeout(3500)
    names = p.evaluate("() => window.__dn.fmNames")
    check("FM banks: sent: FM's voices 10 + 7, the two new ones after the five", len(names) == 17 and names[15] == "TWO 01" and names[16] == "TWO 02", names[9:])
    check("FM banks: the samples kept (the breaks' list as it was)", p.evaluate("() => window.__dn.loops.length") == 2)
    check("FM banks: nothing left to send", "not sent" not in p.locator("#fmKept").inner_text(), p.locator("#fmKept").inner_text())
    # (0.31.2) read back from the FM-1 (as on the next connection): the banks and the sample by the names they were sent with;
    # adding one more voice sends only what changed
    p.evaluate("() => { const S = window.__dn; S.media = []; S.dev = null; S.mediaRead = false; }")
    p.click("[data-tab=files]")
    p.wait_for_timeout(2500)
    names_now = p.evaluate("() => window.__dn.media.map((m) => m.name)")
    check("read back: the banks by the names they were sent with", "mybank" in names_now and "second" in names_now, names_now)
    p.click("#fmTabBtn")
    p.wait_for_timeout(300)
    w0 = p.evaluate("() => window.__dn.mock.writes || 0")
    p.set_input_files("#fmBankFile", files=[{"name": "third.syx", "mimeType": "application/octet-stream", "buffer": bytes(bank2)}])
    p.wait_for_timeout(300)
    p.locator("#fmBankList .fmbank").last.locator(".fmtile").nth(5).locator("input").check()
    p.wait_for_timeout(100)
    p.click("#fmSendKept")
    p.wait_for_timeout(3000)
    w1 = p.evaluate("() => window.__dn.mock.writes || 0")
    check("one more voice kept: sent with only what changed (a few writes, not the whole slot)", 0 < w1 - w0 < 30 and len(p.evaluate("() => window.__dn.fmNames")) == 18,
          (w1 - w0, len(p.evaluate("() => window.__dn.fmNames"))))
    # (0.32) WAVE: its tab (12 factory sounds, 8 tables), a sound picked, POS moved; a Serum wavetable loaded, seen and
    # heard here (nothing sent), sent: a table of the FM-1's after the factory's, its zone 2-byte aligned, then picked
    p.click("#waveTabBtn")
    p.wait_for_timeout(300)
    check("WAVE: the 20 factory sounds and 8 tables", p.locator("#wvpreset option").count() == 20 and p.locator("#wvtable option").count() == 8,
          (p.locator("#wvpreset option").count(), p.locator("#wvtable option").count()))
    p.select_option("#wvpreset", "3")
    p.wait_for_timeout(400)
    check("WAVE: PPG BELL on the FM-1 (its table PPG)", p.evaluate("() => window.__dn.mock.wave.preset") == 3 and p.evaluate("() => window.__dn.mock.wave.p[0]") == 2,
          p.evaluate("() => window.__dn.mock.wave"))
    pos = p.locator("#wvEd .prm").filter(has_text="POS ENV").locator("input")
    pos.evaluate("(e) => { e.value = 20; e.dispatchEvent(new Event('input')); }")
    p.wait_for_timeout(300)
    check("WAVE: POS ENV +20 on the FM-1", p.evaluate("() => window.__dn.mock.wave.p[20]") == 20, p.evaluate("() => window.__dn.mock.wave.p[20]"))
    wav = (ROOT / "wavetables" / "reese.wav").read_bytes()
    p.evaluate("() => { window.__played = 0; }")
    p.set_input_files("#wvFile", files=[{"name": "myreese.wav", "mimeType": "audio/wav", "buffer": wav}])
    p.wait_for_function("window.__dn.media.some((m) => m.wt)", timeout=20000)
    p.wait_for_timeout(200)
    check("WAVE: the table converted: 8 frames, its frames drawn", p.locator("#wvList .wvtab").count() == 1 and "8 frames" in p.locator("#wvList .wvtab").inner_text(),
          p.locator("#wvList").inner_text()[:100])
    p.locator("#wvList .wvtab input[type=range]").evaluate("(e) => { e.value = 700; e.dispatchEvent(new Event('input')); }")
    p.locator("#wvList .wvtab button[data-n='48']").click()
    p.wait_for_timeout(300)
    check("WAVE: C2 plays it here (nothing sent)", p.evaluate("() => window.__played") == 1 and p.evaluate("() => window.__dn.mock.wtPlayed") == 0)
    p.screenshot(path=str(shots / "editor-wave.png"), full_page=True)
    p.click("#wvSend")
    p.wait_for_timeout(4000)
    tabs = p.evaluate("() => window.__dn.wvTables")
    check("WAVE: sent: the tables 8 + 1, MYREESE after the factory's", len(tabs) == 9 and tabs[8] == "MYREESE", tabs)
    check("WAVE: its zone 2-byte aligned (the FM-1 reads it in place)", p.evaluate("() => window.__dn.mock.wtOdd") == 0)
    check("WAVE: not taken for a loop (the breaks' list as it was)", p.evaluate("() => window.__dn.loops.length") == 2)
    p.select_option("#wvtable", "8")
    p.wait_for_timeout(400)
    check("WAVE: TABLE MYREESE on the FM-1", p.evaluate("() => window.__dn.mock.wave.p[0]") == 8, p.evaluate("() => window.__dn.mock.wave.p[0]"))
    # (0.33) WAVE 2 on the tab: B's table, the matrix's first route
    p.select_option("#wvtableb", "3")
    p.wait_for_timeout(300)
    row = p.locator(".mxrow").first
    row.locator("select").nth(0).select_option("2")
    row.locator("select").nth(1).select_option("9")
    row.locator("input").evaluate("(e) => { e.value = 30; e.dispatchEvent(new Event('input')); }")
    p.wait_for_timeout(400)
    check("WAVE 2: B's table and the matrix's first route (LFO 2 to CUTOFF +30) on the FM-1",
          p.evaluate("() => window.__dn.mock.wave.p.slice(59, 62).join() + ' ' + window.__dn.mock.wave.p[29]") == "2,9,30 3",
          p.evaluate("() => window.__dn.mock.wave.p.slice(59, 62).join() + ' ' + window.__dn.mock.wave.p[29]"))
    p.screenshot(path=str(shots / "editor-wave-sent.png"), full_page=True)
    check("no page errors", not errs, errs)
    b.close()
srv.shutdown()
print("EDITOR e2e: " + ("all ok" if not fails else f"{fails} FAILED"))
sys.exit(1 if fails else 0)
