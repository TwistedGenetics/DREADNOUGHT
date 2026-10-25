// SPDX-License-Identifier: GPL-3.0-only
// The editor's FM-1 side: the byte codecs, the pattern layout, and a whole session against the simulated FM-1.
import test from "node:test";
import assert from "node:assert/strict";
import {
  pack7, unpack7, digits, num, parsePattern, serializePattern, connect, noteEnd, lenText, ST_ON, overlapSlide, setOverlapCut,
  overlapCut, glideText, lfoPtsArgs, parseLfoPts, lfoShapeAt,
} from "../protocol.js";
import { MockFM1, demoPattern } from "../mock-fm1.js";

test("7-bit packing round trip", () => {
  const b = Array.from({ length: 200 }, (_, i) => (i * 37) & 255);
  const p = pack7(b);
  assert.ok(p.every((x) => x < 128));
  assert.deepEqual(unpack7(p), b);
  assert.equal(num(digits(123456, 3), 0, 3), 123456);
});

test("the pattern's bytes round trip (4136, the firmware's dn_pattern_t of 0.11; 2088 for 0.7-0.10)", () => {
  const p = demoPattern(), b = serializePattern(p);
  assert.equal(b.length, 4136);
  p.tracks[0].steps[3].cond = 11; p.tracks[0].steps[3].rvel = -90; p.tracks[0].steps[3].flags |= 8;
  assert.deepEqual(parsePattern([...serializePattern(p)]), { ...p, pad: [0, 0, 0] });
  const old = serializePattern({ ...p, stepBytes: 4 });
  assert.equal(old.length, 2088);
  assert.equal(parsePattern([...old]).tracks[0].steps[0].note, p.tracks[0].steps[0].note);
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
    assert.equal(st.patSize, 4136);
    const pr = await link.params();
    assert.equal(pr.length, st.nparams + 8, "PARAMS and, from 0.19, the 8 values of PARAMS_HI (the filter envelopes)");
    assert.equal(await link.setParam(127, 99), 0, "a PARAMS_HI value set (REESE F.DEC)");
    assert.equal((await link.params())[127], 99);
    assert.equal(pr[17], -24);
    assert.equal(await link.setParam(1, 90), 0);
    assert.equal((await link.params())[1], 90);
    const pat = await link.readPattern(st.patSize);
    pat.tracks[3].steps[5] = { note: 60, flags: ST_ON, len: 8, vel: 99, cond: 11, rtim: 12, rlen: 0, rvel: -40 };
    await link.writePattern(pat);
    const back = await link.readPattern(st.patSize);
    assert.deepEqual(back.tracks[3].steps[5], { note: 60, flags: ST_ON, len: 8, vel: 99, cond: 11, rtim: 12, rlen: 0, rvel: -40 });
    assert.ok((await link.state()).rev > st.rev);
    assert.equal(await link.transport(true), 1);
    assert.equal(await link.save(), 0);
    assert.deepEqual(await link.names(1), ["ROLLER", "STEPPER"]);
    assert.equal((await link.names(0)).length, 18, "the factory sounds and USER 1-8");
  } finally { m.close(); }
});

test("connect: any port name, and an INFO lost after the port opens is asked again", async () => {
  const m = new MockFM1(), log = [];
  m.input.name = m.output.name = "USB MIDI Device";
  const other = { name: "IAC Bus 1", state: "connected", onmidimessage: null, open: async () => {} };
  m.access.inputs = new Map([["iac", other], ["in", m.input]]);
  m.access.outputs.set("iac", { name: "IAC Bus 1", state: "connected", open: async () => {}, send: () => {} });
  let drop = 1;
  const send = m.output.send;
  m.output.send = (d) => { if (drop-- > 0) return; send(d); };
  const link = await connect(m.access, (s) => log.push(s));
  assert.match(link.name, /^DREADNOUGHT/);
  assert.ok(log.some((s) => /try 1 of 3/.test(s)), log.join(" | "));
  m.close();
});

test("LFO points: the frame's bytes round trip; DRAW's picture holds and slopes as the FM-1's", () => {
  const pts = Array.from({ length: 32 }, (_, i) => (i % 2 ? -100 : 100) - (i === 4 ? 37 : 0));
  const a = lfoPtsArgs(2, 32, pts);
  assert.ok(a.every((x) => x < 128));
  const r = parseLfoPts(a);
  assert.equal(r.lfo, 2); assert.equal(r.npts, 32); assert.deepEqual(r.pts, pts);
  const p = { shape: 7, phase: 0, smooth: 0, npts: 16, pts: Array.from({ length: 32 }, (_, i) => (i % 2 ? -100 : 100)) };
  assert.equal(lfoShapeAt(p, 0.06), 1);
  p.smooth = 127;
  assert.ok(Math.abs(lfoShapeAt(p, 1 / 32)) < 0.01);
});

test("chords (0.28): the 15, matched in any order or inversion as the FM-1 does", async () => {
  const { CHORDS, CHORD_NAMES, chordMatch, chordNotes, ST_ON } = await import("../protocol.js");
  assert.equal(CHORDS.length, 15);
  assert.equal(CHORD_NAMES.length, 16);
  assert.deepEqual(chordMatch([53, 57, 60]), { c: 2, root: 53 });          // F A C: F MAJ
  assert.deepEqual(chordMatch([60, 53, 57]), { c: 2, root: 53 });          // (any order)
  assert.deepEqual(chordMatch([57, 60, 65]), { c: 2, root: 53 });          // A C F: F MAJ, its root under A
  assert.deepEqual(chordMatch([57, 60, 64, 67]), { c: 3, root: 57 });      // A C E G: A MIN7 (not C 6)
  assert.deepEqual(chordMatch([53, 54]), { c: 0, root: 53 });              // none of the 15
  assert.deepEqual(chordMatch([48, 55]), { c: 11, root: 48 });             // C G: a power chord
  CHORDS.forEach((ch, i) => {                                              // each chord's notes match itself
    const st = { note: 50, flags: ST_ON | ((i + 1) << 4) };
    assert.deepEqual(chordMatch(chordNotes(st)), { c: i + 1, root: 50 }, CHORD_NAMES[i + 1]);
  });
});

test("(0.30) the link: a request unanswered is sent again; a closed link lets go of the port", async () => {
  const m = new MockFM1(), link = await connect(m.access);
  let dropped = 0;
  const reply = m.reply.bind(m);
  m.reply = (cmd, bytes) => { if (cmd === 60 && !dropped) { dropped++; return; } reply(cmd, bytes); };
  const st = await link.req(60, [], 300);            // the first answer lost: asked again, answered
  assert.ok(Array.isArray(st) && st.length > 0);
  assert.equal(dropped, 1);
  let played = 0;
  m.reply = (cmd, bytes) => { if (cmd === 86) { played++; return; } reply(cmd, bytes); };
  await assert.rejects(link.req(86, [60, 1], 200));   // a note played: never sent twice
  assert.equal(played, 1);
  m.reply = reply;
  link.close();
  assert.equal(m.input.onmidimessage, null);
  await assert.rejects(link.req(60, [], 200), /newer connection/);
  m.close();
});
