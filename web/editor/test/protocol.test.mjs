// SPDX-License-Identifier: GPL-3.0-only
// The editor's FM-1 side: the byte codecs, the pattern layout, and a whole session against the simulated FM-1.
import test from "node:test";
import assert from "node:assert/strict";
import { pack7, unpack7, digits, num, parsePattern, serializePattern, connect, noteEnd, lenText, ST_ON } from "../protocol.js";
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
