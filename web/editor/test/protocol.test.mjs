// SPDX-License-Identifier: GPL-3.0-only
// The editor's FM-1 side: the byte codecs, the pattern layout, and a whole session against the simulated FM-1.
import test from "node:test";
import assert from "node:assert/strict";
import {
  pack7, unpack7, digits, num, parsePattern, serializePattern, connect, noteEnd, lenText, ST_ON, overlapSlide, setOverlapCut,
  overlapCut, glideText,
} from "../protocol.js";
import { MockFM1, demoPattern } from "../mock-fm1.js";

test("7-bit packing round trip", () => {
  const b = Array.from({ length: 200 }, (_, i) => (i * 37) & 255);
  const p = pack7(b);
  assert.ok(p.every((x) => x < 128));
  assert.deepEqual(unpack7(p), b);
  assert.equal(num(digits(123456, 3), 0, 3), 123456);
});

test("the pattern's bytes round trip (2088, the firmware's dn_pattern_t)", () => {
  const p = demoPattern(), b = serializePattern(p);
  assert.equal(b.length, 2088);
  assert.deepEqual(parsePattern([...b]), { ...p, pad: [0, 0, 0] });
  setOverlapCut(p, 3, true);
  assert.equal(parsePattern([...serializePattern(p)]).pad[0], 8, "OVERLAP CUT kept in pad[0]");
});

test("overlaps: a note reaching past the next one slides into it (not on the breaks, not on CUT)", () => {
  const p = demoPattern();
  for (const t of [0, 1, 4]) {
    const st = p.tracks[t].steps;
    st.forEach((s) => { s.flags = 0; });
    Object.assign(st[0], { flags: ST_ON, len: 9 });
    Object.assign(st[2], { flags: ST_ON, len: 4 });
    Object.assign(st[4], { flags: ST_ON, len: 4 });
  }
  assert.equal(overlapSlide(p, 0, 2), true, "REESE: 2 1/4 steps over a note 2 steps on");
  assert.equal(overlapSlide(p, 0, 4), false, "a note that ends where the next starts");
  assert.equal(overlapSlide(p, 4, 2), true, "a MIDI track");
  assert.equal(overlapSlide(p, 1, 2), false, "a break track never");
  p.tracks[0].steps[0].len = 8;
  assert.equal(overlapSlide(p, 0, 2), false, "exactly two steps: no overlap");
  p.tracks[0].steps[0].len = 9;
  setOverlapCut(p, 0, true);
  assert.equal(overlapCut(p, 0), true);
  assert.equal(overlapSlide(p, 0, 2), false, "OVERLAP CUT");
});

test("GLIDE reads as the slide time, as the FM-1 shows it", () => {
  assert.equal(glideText(0), "OFF");
  assert.equal(glideText(64), "104ms");
  assert.equal(glideText(90), "685ms");
  assert.equal(glideText(127), "10.0s");
});

test("a note's reach: its length, cut by the next note", () => {
  const p = demoPattern(), tr = p.tracks[0];
  assert.equal(noteEnd(tr, 0), 3);                   // 11 quarter steps, cut at step 3 by the next note
  assert.equal(lenText(11), "2¾");
  assert.equal(lenText(4), "1");
});

test("a session with the simulated FM-1: state, params, pattern write and commit, transport, save", async () => {
  const m = new MockFM1(), link = await connect(m.access);
  try {
    const st = await link.state();
    assert.equal(st.bpm, 172);
    assert.equal(st.patSize, 2088);
    const pr = await link.params();
    assert.equal(pr.length, st.nparams);
    assert.equal(pr[17], -24);
    assert.equal(await link.setParam(1, 90), 0);
    assert.equal((await link.params())[1], 90);
    const pat = await link.readPattern(st.patSize);
    pat.tracks[3].steps[5] = { note: 60, flags: ST_ON, len: 8, vel: 99 };
    await link.writePattern(pat);
    const back = await link.readPattern(st.patSize);
    assert.deepEqual(back.tracks[3].steps[5], { note: 60, flags: ST_ON, len: 8, vel: 99 });
    assert.ok((await link.state()).rev > st.rev);
    assert.equal(await link.transport(true), 1);
    assert.equal(await link.save(), 0);
    assert.deepEqual(await link.names(1), ["ROLLER", "STEPPER"]);
    assert.equal((await link.names(0)).length, 9);
  } finally { m.close(); }
});
