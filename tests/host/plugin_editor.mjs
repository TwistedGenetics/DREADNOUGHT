// SPDX-License-Identifier: GPL-3.0-only
// (0.45) The web editor's own code against the plugin's core (plugin/core/dnc_pipe: the app and the engines, answering
// the editor's frames as the plugin's MIDI port does): it connects, reads a whole project, sends a sample (as the
// Project tab does) and its slices, and writes a project back. Copyright (C) 2026 Jamie (TwistedGenetics).
//   node tests/host/plugin_editor.mjs build/host/dnc_pipe
import { spawn } from "node:child_process";
import assert from "node:assert/strict";
import { connect, PF } from "../../web/editor/protocol.js";
import { readProject, writeProject, checkProject } from "../../web/editor/project.js";
import { imagesOf, writeSlots, readSlots, imaEncode, slotSamples, parseSlot } from "../../web/editor/media.js";
import { sliceKey, key7, pos7 } from "../../web/breaks/slices.js";

function plugin(path) {
  const p = spawn(path, [], { stdio: ["pipe", "pipe", "inherit"] });
  const input = { name: "DREADNOUGHT Plugin", state: "connected", onmidimessage: null, open: async () => {} };
  const output = { name: "DREADNOUGHT Plugin", send: (f) => p.stdin.write(Uint8Array.from(f)), open: async () => {} };
  let buf = [];
  p.stdout.on("data", (d) => {
    for (const b of d) {
      if (b === 0xF0) buf = [];
      buf.push(b);
      if (b === 0xF7 && input.onmidimessage) input.onmidimessage({ data: Uint8Array.from(buf) });
    }
  });
  return { access: { inputs: new Map([["in", input]]), outputs: new Map([["out", output]]) }, close: () => p.stdin.end() };
}

const ok = (what) => console.log(`ok   ${what}`);
const a = plugin(process.argv[2] || "build/host/dnc_pipe");
try {
  const la = await connect(a.access);
  assert.match(la.name, /^DREADNOUGHT [\d.]+ PLUGIN/);
  ok(`the editor finds the plugin: "${la.name}"`);
  const dev = await readSlots(la);
  assert.equal(parseSlot(dev[0].hdr).name, "BREAKS");
  assert.equal(slotSamples(dev).length, 2);
  ok("it reads the slots: ROLLER and STEPPER in slot 1, CRC checked");

  const s = Int16Array.from({ length: 30000 }, (_, i) => Math.round(9000 * Math.sin(i / 7) * Math.exp(-i / 9000)));
  const part = { adpcm: imaEncode(s), n: s.length, rate: 22050 };
  const parts = [...slotSamples(dev).map(({ adpcm, n, rate }) => ({ adpcm, n, rate })), part];
  await writeSlots(la, imagesOf(parts), dev);
  const dev2 = await readSlots(la);
  assert.equal(slotSamples(dev2).length, 3);
  assert.deepEqual([...slotSamples(dev2)[2].adpcm], [...part.adpcm]);
  ok("a new sample sent as the Project tab sends it, read back the same");
  const r = await la.req(51, [...key7(sliceKey(part.adpcm, part.n)), 3, ...[0, 9000, 20000].flatMap(pos7)]);
  assert.equal(r[5], 0);
  ok("its slices set");

  await la.projSet(PF.sendA(3), 77);
  await la.projName("PLUG SESH");
  await la.setParam(4, 33);
  const proj = JSON.parse(JSON.stringify(await readProject(la)));
  checkProject(proj);
  assert.equal(proj.name, "PLUG SESH");
  assert.equal(proj.slots.filter(Boolean).length >= 1, true);
  assert.equal(proj.slices.length, 1);
  ok(`a whole project read: ${proj.patterns.length} patterns, ${proj.slots.filter(Boolean).length} slots, the slices`);

  await la.projName("OTHER");
  await la.setParam(4, 12);
  await writeProject(la, proj);
  const back = JSON.parse(JSON.stringify(await readProject(la)));
  assert.equal(back.name, "PLUG SESH");
  assert.equal(back.params[4], 33);
  assert.deepEqual(back.patterns, proj.patterns);
  assert.deepEqual(back.slots, proj.slots);
  assert.deepEqual(back.slices, proj.slices);
  ok("written back over changes: the same project again");
  console.log("PLUGIN + EDITOR: ok");
} catch (e) {
  console.log(`FAIL ${e.stack || e}`);
  process.exitCode = 1;
} finally {
  a.close();
}
