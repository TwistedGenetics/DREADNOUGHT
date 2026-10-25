#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The iPad app's page bridge (ios/Resources/bridge.js) end to end in Chromium: the editor connects to the simulated FM-1
(web/editor/mock-fm1.js) through the bridge's Web MIDI, as it does through CoreMIDI in the app, with a stand-in for the
app's native side (the same messages ios/Sources/MIDIBridge.swift and FilesBridge.swift answer). Checks the connection,
ports coming and going, saving a project to the projects folder and listing it, a download, and the clipboard.

    python3 ios/test/bridge_e2e.py [SHOTS_DIR]          (needs playwright and its Chromium)"""
import http.server
import sys
import threading
from functools import partial
from pathlib import Path

from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[2]
shots = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
BRIDGE = (ROOT / "ios/Resources/bridge.js").read_text()


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a):
        pass


srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), partial(Quiet, directory=str(ROOT / "web")))
threading.Thread(target=srv.serve_forever, daemon=True).start()
base = f"http://127.0.0.1:{srv.server_address[1]}"
fails = 0


def check(what, ok, got=""):
    global fails
    print(("ok   " if ok else "FAIL ") + what + (f": {got}" if got != "" else ""))
    fails += not ok


# the app's native side, stood in for: the same ops and replies as MIDIBridge.swift / FilesBridge.swift
NATIVE = r"""
(() => {
  const b64 = (u8) => btoa(String.fromCharCode(...u8));
  const unb64 = (s) => Uint8Array.from(atob(s), (c) => c.charCodeAt(0));
  const fake = window.__fake = { files: {}, saved: {}, toasts: [], clip: null, sent: 0,
    ports: { inputs: [{ id: "i1", name: "DREADNOUGHT FM-1", manufacturer: "M-VAVE" }],
             outputs: [{ id: "o1", name: "DREADNOUGHT FM-1", manufacturer: "M-VAVE" }] } };
  let mock = null;
  const device = async () => {
    if (!mock) {
      const { MockFM1 } = await import("/editor/mock-fm1.js");
      mock = window.__fakeMock = new MockFM1();
      mock.input.onmidimessage = (ev) => setTimeout(() => window.__dnMIDI.recv("i1", [b64(ev.data)]), 1);
    }
    return mock;
  };
  const midi = async (m) => {
    if (m.op === "start") { await device(); return JSON.parse(JSON.stringify(fake.ports)); }
    if (m.op === "send") {
      if (m.id === "o1") { fake.sent++; (await device()).output.send(unb64(m.data)); }
      return true;
    }
    throw new Error("unknown op " + m.op);
  };
  const files = async (m) => {
    switch (m.op) {
      case "save": fake.saved[m.name] = atob(m.data); fake.toasts.push("saved " + m.name); return m.name;
      case "list": return Object.entries(fake.files).map(([name, f]) => ({ name, size: f.data.length, mtime: f.mtime }));
      case "read": return fake.files[m.name] ? { data: btoa(fake.files[m.name].data), mtime: fake.files[m.name].mtime } : null;
      case "write": fake.files[m.name] = { data: atob(m.data), mtime: Date.now() }; return true;
      case "remove": delete fake.files[m.name]; return true;
      case "clip": fake.clip = m.text; return true;
      case "toast": fake.toasts.push(m.text); return true;
    }
    throw new Error("unknown op " + m.op);
  };
  window.webkit = { messageHandlers: { dnmidi: { postMessage: midi }, dnfiles: { postMessage: files } } };
})();
"""

with sync_playwright() as pw:
    br = pw.chromium.launch()
    pg = br.new_page(viewport={"width": 1180, "height": 820})   # (an iPad Air in landscape)
    errors = []
    pg.on("pageerror", lambda e: errors.append(str(e)))
    pg.add_init_script(NATIVE)
    pg.add_init_script(BRIDGE)
    pg.goto(f"{base}/editor/")
    check("the bridge supplies requestMIDIAccess",
          pg.evaluate("navigator.requestMIDIAccess.toString().includes('starting')"))

    pg.click("#connect")
    pg.wait_for_function("document.getElementById('status').textContent.startsWith('Connected')", timeout=20000)
    check("connects to the FM-1 through the bridge", True, pg.inner_text("#status"))
    n0 = pg.evaluate("window.__fake.sent")
    check("frames went out through the bridge", n0 > 5, n0)
    pg.wait_for_timeout(1500)
    check("the editor keeps polling it", pg.evaluate("window.__fake.sent") > n0)

    # a port comes and goes (a keyboard plugged in, then out): the link carries on, the new port is listed then gone
    pg.evaluate("""async () => { const f = window.__fake; f.events = 0;
      const a = await navigator.requestMIDIAccess(); a.onstatechange = () => f.events++;
      window.__dnMIDI.ports({ inputs: [...f.ports.inputs, { id: "i9", name: "KeyStep" }],
                              outputs: [...f.ports.outputs] }); }""")
    pg.wait_for_timeout(300)
    seen = pg.evaluate("navigator.requestMIDIAccess().then((a) => [...a.inputs.values()].map((p) => p.name))")
    check("a new port appears", "KeyStep" in seen, seen)
    pg.evaluate("window.__dnMIDI.ports(window.__fake.ports)")
    pg.wait_for_timeout(300)
    seen = pg.evaluate("navigator.requestMIDIAccess().then((a) => [...a.inputs.values()].map((p) => p.name))")
    check("and goes", seen == ["DREADNOUGHT FM-1"], seen)
    check("statechange fired for both", pg.evaluate("window.__fake.events") == 2, pg.evaluate("window.__fake.events"))
    n1 = pg.evaluate("window.__fake.sent")
    pg.wait_for_timeout(1500)
    check("the link still talks after the change", pg.evaluate("window.__fake.sent") > n1)
    check("still connected", not pg.inner_text("#status").startswith("The FM-1 stopped"), pg.inner_text("#status"))

    # the Project tab: the projects folder is the app's, Save writes there, the list shows it, Load reads it back
    pg.click('.tabs button[data-tab="proj"]') if pg.query_selector('.tabs button[data-tab="proj"]') else None
    pg.evaluate("document.getElementById('pfolder').click()")
    pg.wait_for_timeout(500)
    check("choosing a projects folder works (the app's)", any("Projects folder" in t for t in pg.evaluate("window.__fake.toasts")))
    pg.evaluate("document.getElementById('psave').click()")
    pg.wait_for_function("Object.keys(window.__fake.files).some((n) => n.endsWith('.dnproj'))", timeout=60000)
    names = pg.evaluate("Object.keys(window.__fake.files)")
    check("Save project writes a .dnproj into the folder", True, names)
    size = pg.evaluate("Object.values(window.__fake.files)[0].data.length")
    check("with the project in it", size > 1000, f"{size} bytes")
    pg.wait_for_function("document.querySelectorAll('#pfiles li button').length > 0", timeout=10000)
    check("the folder lists it", True, pg.inner_text("#pfiles").split("\n")[0])
    sent = pg.evaluate("window.__fake.sent")
    pg.evaluate("document.querySelector('#pfiles li button').click()")
    pg.wait_for_function("document.getElementById('msg') ? true : true")
    pg.wait_for_timeout(6000)
    check("Load to FM-1 reads it back and sends it", pg.evaluate("window.__fake.sent") > sent + 10)

    # a download (what Save does without a folder, the backups, the sound files): into the app's folder
    pg.evaluate("""() => { const a = document.createElement('a');
      a.href = URL.createObjectURL(new Blob(['{"hello":1}'], { type: 'application/json' }));
      a.download = 'test.json'; a.click(); setTimeout(() => URL.revokeObjectURL(a.href), 1000); }""")
    pg.wait_for_function("'test.json' in window.__fake.saved", timeout=5000)
    check("a download is saved to the app's folder", pg.evaluate("window.__fake.saved['test.json']") == '{"hello":1}')

    pg.evaluate("navigator.clipboard.writeText('diag text')")
    pg.wait_for_timeout(200)
    check("the clipboard goes to the app", pg.evaluate("window.__fake.clip") == "diag text")

    pg.screenshot(path=str(shots / "ipad-bridge.png"))
    check("no page errors", not errors, errors)
    br.close()

print("all ok" if not fails else f"{fails} failed")
sys.exit(1 if fails else 0)
