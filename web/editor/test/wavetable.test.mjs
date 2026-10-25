// SPDX-License-Identifier: GPL-3.0-only
// WAVE's wavetables in the editor (0.32): a Serum-style WAV made into the FM-1's format, read back, played; a table
// among samples keeps its zone 2-byte aligned (the FM-1 reads it in place). (The host tests check the converter
// against tools/wt_convert.py bit for bit: tests/host/wt_xcheck.mjs.)
import { test } from "node:test";
import assert from "node:assert/strict";
import { convert, fromWav, parseTable, render, tableBytes, FRAME_BYTES, MAXF, wtText, wtMax } from "../wavetable.js";
import { imagesOf, parseSlot, slotSamples, imaEncode } from "../media.js";

const saw = (n, f) => Float64Array.from({ length: n }, (_, i) => 2 * (((i * f) / n) % 1) - 1);
function serum(frames, cyc = 2048) {                   // 16-bit mono with Serum's clm chunk
  const x = new Int16Array(frames.length * cyc);
  frames.forEach((fr, k) => fr.forEach((v, i) => { x[k * cyc + i] = Math.round(v * 30000); }));
  const clm = new TextEncoder().encode(`<!>${cyc} 10000000 wavetable`), pad = clm.length & 1;
  const n = 4 + 8 + 16 + 8 + clm.length + pad + 8 + x.byteLength, b = new Uint8Array(8 + n), d = new DataView(b.buffer);
  let o = 0;
  const tag = (t) => { for (const c of t) b[o++] = c.charCodeAt(0); }, u32 = (v) => { d.setUint32(o, v, true); o += 4; };
  tag("RIFF"); u32(n); tag("WAVE"); tag("fmt "); u32(16);
  d.setUint16(o, 1, true); d.setUint16(o + 2, 1, true); d.setUint32(o + 4, 44100, true); d.setUint32(o + 8, 88200, true);
  d.setUint16(o + 12, 2, true); d.setUint16(o + 14, 16, true); o += 16;
  tag("clm "); u32(clm.length); b.set(clm, o); o += clm.length + pad;
  tag("data"); u32(x.byteLength); b.set(new Uint8Array(x.buffer), o);
  return b;
}

test("a Serum table of 4 frames of 1024: the FM-1's format, named, its frames saws of more and more harmonics", () => {
  const w = serum([1, 2, 3, 4].map((f) => saw(1024, f)), 1024), t = fromWav(w, { name: "my saws!" });
  assert.equal(t.length, tableBytes(4));
  assert.equal(t.length, 16 + 4 * FRAME_BYTES);
  const p = parseTable(t);
  assert.equal(p.frames, 4);
  assert.equal(p.name, "MY SAWS!");
  const f0 = p.frame(0), peak = Math.max(...f0.map(Math.abs));
  assert.ok(peak > 20000 && peak <= 32767, `peak ${peak}`);
  assert.equal(p.frame(0, 6).length, 8);              // (the last version: 8 samples, the fundamental)
});
test("more than 64 frames: thinned to 64, or to what's asked", () => {
  const x = Float64Array.from({ length: 2048 * 100 }, (_, i) => Math.sin((2 * Math.PI * i) / 2048));
  assert.equal(parseTable(convert(x)).frames, MAXF);
  assert.equal(parseTable(convert(x, { frames: 16 })).frames, 16);
});
test("a single cycle (no clm, 600 samples): one frame", () => {
  assert.equal(parseTable(convert(saw(600, 1))).frames, 1);
});
test("not a table: refused; a table cut short: refused", () => {
  assert.equal(parseTable(new Uint8Array(100)), null);
  const t = convert(saw(2048, 1));
  assert.equal(parseTable(t.subarray(0, t.length - 2)), null);
});
test("played here: a note at its pitch, not silent", () => {
  const s = render(convert(saw(2048, 1)), 0, 69, 0.5, 44100);
  assert.equal(s.length, 22050);
  let cross = 0;
  for (let i = 1000; i < 21000; i++) cross += s[i - 1] < 0 && s[i] >= 0;
  assert.ok(Math.abs(cross / (20000 / 44100) - 440) < 5, `${cross / (20000 / 44100)} Hz`);
});
test("WAVE's values as text: TABLE by name, WARP, MONO, signed", () => {
  assert.equal(wtText(0, 1, ["ANALOG", "REESE"]), "REESE");
  assert.equal(wtText(2, 4), "PWM");
  assert.equal(wtText(25, 1), "MONO");
  assert.equal(wtText(10, 5), "+5");
  assert.equal(wtMax(0, 9), 8);
});
test("a table after an odd-length sample: its zone starts 2-byte aligned, and reads back as a table", () => {
  const a = imaEncode(Int16Array.from({ length: 1001 }, (_, i) => (i % 50) * 100)), t = convert(saw(2048, 1));
  assert.equal(a.length % 2, 1);
  const ims = imagesOf([{ adpcm: a, n: 1001, rate: 22050 }, { adpcm: t, n: 2 * t.length, rate: 22050 }]);
  const z = parseSlot(ims[0].hdr).zones;
  assert.equal(z[1].off % 2, 0);
  assert.equal(z[1].off, a.length + 1);
  const back = slotSamples(ims);
  assert.ok(parseTable(back[1].adpcm));
  assert.equal(back[0].n, 1001);
});
