#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Make web/breaks/index.html from X0X's break-loop page (x0x-firmware/web/x0x_breaks.html and x0x_breaks.js, by
Charles Vestal, GPL-3.0-only): its encoder and slot packing inlined unchanged, plus DREADNOUGHT's slicing by hand
(web/breaks/slices.js) (byte for byte what X0X's
tools/upload_breaks.py writes), the words, the port names and the look changed for DREADNOUGHT (light only).

    python3 tools/make_breaks_page.py           (run after moving the x0x-firmware submodule; commit the result)
"""
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
SRC = REPO / "x0x-firmware" / "web"
OUT = REPO / "web" / "breaks" / "index.html"

EDITS = [
    ('''  @media (prefers-color-scheme: dark) {
    :root:not([data-theme="light"]) { --bg: #000; --fg: #fff; --dim: #888; --rule: #333; --wave: #666; --accent: #f0a043; color-scheme: dark; }
  }
  :root[data-theme="dark"] { --bg: #000; --fg: #fff; --dim: #888; --rule: #333; --wave: #666; --accent: #f0a043; color-scheme: dark; }
''', ""),
    ("--accent: #c8700a;", "--accent: #1f7fbf;"),
    ("<title>X0X break loops</title>", "<title>DREADNOUGHT breaks</title>"),
    ('<a class="home" href="../">X0X</a>', '<a class="home" href="../flasher/">DREADNOUGHT</a>'),
    ("<h1>Break loops</h1>", "<h1>Your breaks</h1>"),
    ('''  <p>Your own loops for X0X's BREAK part. They appear on the FM-1 as BR1, BR2 and so on, after X0X's own
    loops, when you turn LOOP A or LOOP B (BREAK, then EDIT).</p>
  <p class="small">Connect the FM-1 by USB with X0X running, and use Chrome or Edge.</p>''',
     '''  <p>Your own break loops for DREADNOUGHT's BREAKS tracks (tracks 2 and 3). On the FM-1 they come after the two
    built-in loops (ROLLER, STEPPER) as BR1, BR2 and so on: press ARP for the BRK page, then turn PRESETS (or
    KNOB 1, LOOP) to pick one.</p>
  <p class="small">Connect the FM-1 by USB with DREADNOUGHT 0.5 or later running, and use Chrome or Edge. Whole
    loops of 1, 2 or 4 bars work best: DREADNOUGHT works out the bars from the length and cuts 16 slices a bar.
    The three slots hold about 20 seconds of audio in all.</p>'''),
    ('''<input type="checkbox" id="bars" checked> Detect first bar</label>''',
     '''<input type="checkbox" id="bars"> First bar only</label>'''),
    ('''  <p class="small" style="margin-top:32px">Loops follow the tempo by playing faster or slower, so their pitch
    follows it too; the break's PITCH knob shifts it. Removing a loop renumbers the ones after it, so patterns
    that used them play the next loop instead.
    <a href="../manual/#s8" style="color:inherit">The manual's BREAK section</a> says how X0X plays a loop.</p>
  <p class="small">X0X is GPL-3.0, built on Felucca by Hügelton Instruments; the break generator is BB Gen's.
    <a href="https://github.com/charlesvestal/fm1-x0x" style="color:inherit">github.com/charlesvestal/fm1-x0x</a></p>''',
     '''  <p class="small" style="margin-top:32px">Loops follow the tempo by playing faster or slower, so their pitch
    follows it too, the jungle way; the BRK page's PITCH knob shifts it. Removing a loop renumbers the ones after
    it, so a BREAKS track that used one plays the next loop instead.</p>
  <p class="small">This page is <a href="https://github.com/charlesvestal/fm1-x0x" style="color:inherit">X0X</a>'s
    break-loop page by Charles Vestal (GPL-3.0), built on Felucca by Hügelton Instruments, adapted for
    <a href="https://github.com/TwistedGenetics/DREADNOUGHT" style="color:inherit">DREADNOUGHT</a>. Only upload
    loops you have the right to use.</p>'''),
    # slicing by hand (web/breaks/slices.js): a Slices button on every loop, the slices sent with Send and read back
    ('''<div class="acts"><button class="link rep">Replace</button>''',
     '''<div class="acts"><button class="link sl">Slices</button><button class="link rep">Replace</button>'''),
    ('''${l.isNew ? " · new" : ""}</small>''',
     '''${l.isNew ? " · new" : ""}${l.slices ? ` · ${l.slices.length} slices` : ""}${l.slicesDirty ? " (not sent)" : ""}</small>'''),
    ('''    r.querySelector(".del").onclick = () => { loops.splice(i, 1); render(); };''',
     '''    r.querySelector(".del").onclick = () => { loops.splice(i, 1); render(); };
    r.querySelector(".sl").onclick = () => editSlices(l, barsOf(l.n / SLOT_RATE)[0], SLOT_RATE,
      (sl) => { l.slices = sl; l.slicesDirty = true; render(); }, `Slices of BR${i + 1}`);
    r.querySelector(".sl").disabled = busy;'''),
    ('''  const changed = readOk && plan && changedSlots(plan).length > 0;''',
     '''  const changed = readOk && plan && (changedSlots(plan).length > 0 || loops.some((l) => l.slicesDirty));'''),
    ('''  bar.hidden = true;
  say("Reading back…");''',
     '''  for (const l of loops) {                           // the slices set by hand (DREADNOUGHT's ED_SLICE_SET, 51)
    if (!l.slicesDirty) continue;
    if (!l.adpcm) l.adpcm = imaEncode(l.s);
    say("Sending the slices…");
    const r = await link.req(51, [...key7(sliceKey(l.adpcm, l.n)), l.slices.length, ...l.slices.flatMap(pos7)], 5000);
    if (r[5]) throw new Error("The FM-1 didn't keep the slices.");
    l.slicesDirty = false;
  }
  bar.hidden = true;
  say("Reading back…");'''),
    ('''  bar.hidden = true;
  readOk = true;
}''',
     '''  bar.hidden = true;
  for (const l of loops) {                           // each loop's slices set by hand, if any (ED_SLICE_GET, 50)
    try {
      const r = await link.req(50, key7(sliceKey(l.adpcm, l.n)), 1500), cnt = r[5] || 0;
      if (cnt) l.slices = Array.from({ length: cnt }, (_, j) => r[6 + 3 * j] | r[7 + 3 * j] << 7 | r[8 + 3 * j] << 14);
    } catch (_) { break; }                           // (a firmware without slices: don't ask again)
  }
  readOk = true;
}'''),
    ('''!/x0x|felucca/i.test(input.name || "")''', '''!/dreadnought|x0x|felucca|fm-1|fm1|m-vave/i.test(input.name || "")'''),
    ('''"No FM-1 running X0X found. Connect it by USB, and close other apps that use its MIDI port."''',
     '''"No FM-1 running DREADNOUGHT found. Connect it by USB, and close other apps that use its MIDI port."'''),
    ('''"This X0X can't send its loops back to the page. Update X0X, then connect again."''',
     '''"This firmware can't send its loops back to the page. Install DREADNOUGHT 0.5 or later, then connect again."'''),
    ('''say("Done. On the FM-1: BREAK, then EDIT, then LOOP A or LOOP B.");''',
     '''say("Done. On the FM-1: ARP (the BRK page), then PRESETS to pick the loop.");'''),
]


def main():
    html = (SRC / "x0x_breaks.html").read_text()
    lib = (SRC / "x0x_breaks.js").read_text().replace("export const ", "const ").replace("export function ", "function ")
    for old, new in EDITS:
        if html.count(old) != 1:
            raise SystemExit(f"make_breaks_page: X0X's page changed, no '{old[:60]}'")
        html = html.replace(old, new)
    if html.count("/*LIB*/") != 1:
        raise SystemExit("make_breaks_page: no /*LIB*/ in X0X's page")
    html = html.replace("/*LIB*/", 'import { sliceKey, key7, pos7, editSlices } from "./slices.js";\n' + lib)
    html = html.replace("<!doctype html>\n", "<!doctype html>\n<!-- Made by tools/make_breaks_page.py from X0X's "
                        "web/x0x_breaks.html and x0x_breaks.js: edit those or the script, not this file. -->\n", 1)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(html)
    print(f"breaks page: {OUT}")


if __name__ == "__main__":
    main()
