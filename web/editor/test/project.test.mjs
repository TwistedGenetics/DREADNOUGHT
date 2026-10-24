// SPDX-License-Identifier: GPL-3.0-only
// A whole project (0.12) read from one simulated FM-1, saved as a file, and written to another: the layout, sends,
// FX, sound, all 8 patterns of all 8 tracks, the samples and their slices come across.
import test from "node:test";
import assert from "node:assert/strict";
import { connect, parsePattern, ST_ON, PF } from "../protocol.js";
import { MockFM1 } from "../mock-fm1.js";
import { readProject, writeProject, checkProject, mediaOf } from "../project.js";
import { imagesOf, writeSlots, readSlots, imaEncode } from "../media.js";
import { sliceKey, key7, pos7 } from "../../breaks/slices.js";

test("a project goes from one FM-1 to another, all of it", async () => {
  const a = new MockFM1(), la = await connect(a.access);
  await la.projSet(PF.eng(3), 1);                     // REESE to track 4 (it swaps with track 1)
  await la.projSet(PF.sendA(3), 77);
  await la.projSet(PF.fx(0, 2), 99);
  await la.projName("DUB SESH");
  await la.setParam(4, 33);
  await la.patSel(5, 6);                              // a note in track 6's pattern 7
  const p = await la.readPattern(4136);
  Object.assign(p.tracks[5].steps[9], { flags: ST_ON, note: 70, len: 4, vel: 90 });
  await la.writePattern(p);
  await la.patSel(5, 0);
  const s = Int16Array.from({ length: 4000 }, (_, i) => Math.round(9000 * Math.sin(i / 7)));
  const part = { adpcm: imaEncode(s), n: s.length, rate: 16000 };
  await writeSlots(la, imagesOf([part]), await readSlots(la));
  await la.req(51, [...key7(sliceKey(part.adpcm, part.n)), 3, ...[0, 1000, 2500].flatMap(pos7)]);
  const proj = JSON.parse(JSON.stringify(await readProject(la, { media: [{ name: "hit", set: { rate: 16000, fold: "left" } }] })));
  assert.deepEqual(checkProject(proj).proj.eng, [4, 2, 3, 1, 4, 4, 4, 4]);
  assert.equal(proj.name, "DUB SESH");
  assert.deepEqual((await la.pats()).tracks.map((t) => t.cur), [0, 0, 0, 0, 0, 0, 0, 0], "the patterns playing are as they were");

  const b = new MockFM1(), lb = await connect(b.access);
  await writeProject(lb, proj);
  const pj = await lb.proj();
  assert.deepEqual(pj.eng, [4, 2, 3, 1, 4, 4, 4, 4]);
  assert.equal(pj.send[0][3], 77);
  assert.equal(pj.fx[0].p[1], 99);
  assert.equal(pj.name, "DUB SESH");
  assert.equal((await lb.params())[4], 33);
  await lb.patSel(5, 6);
  const q = parsePattern([...b.pattern]);
  assert.equal(q.tracks[5].steps[9].note, 70, "pattern 7 of track 6 came across");
  const back = mediaOf(proj);
  assert.equal(back.length, 1);
  assert.equal(back[0].rate, 16000);
  assert.equal(back[0].set.fold, "left");
  assert.deepEqual(b.slices[key7(sliceKey(part.adpcm, part.n)).join(",")], [0, 1000, 2500], "the slices too");
  a.close(); b.close();
});

test("a project file is checked before it's sent", () => {
  assert.throws(() => checkProject({ format: "x" }), /not a DREADNOUGHT project/);
  assert.throws(() => checkProject({ format: "dreadnought-project", version: 1, patterns: Array(8).fill(""), proj: { eng: [1, 1, 2, 3, 4, 4, 4, 4] } }), /twice/);
});
