// SPDX-License-Identifier: GPL-3.0-only
// The editor's samples (0.12): mono folds, resampling without aliasing, the ADPCM codec, costs, fitting to the
// space, the slot images (a rate per zone), and a send / read back against the simulated FM-1.
import test from "node:test";
import assert from "node:assert/strict";
import {
  fold, resample, finish, convert, imaEncode, imaDecode, costAt, fitToSpace, packPlan, slotImage, parseSlot, imagesOf,
  readSlots, writeSlots, slotSamples, readWav, RATES, CAP, SLOT_MAX_DATA, fromDevice, rescaleSlices,
} from "../media.js";
import { connect } from "../protocol.js";
import { MockFM1 } from "../mock-fm1.js";

const sine = (f, sr, sec, a = 0.8) => Float32Array.from({ length: Math.round(sr * sec) }, (_, i) => a * Math.sin((2 * Math.PI * f * i) / sr));
const rms = (x, from = 0) => { let s = 0; for (let i = from; i < x.length; i++) s += x[i] * x[i]; return Math.sqrt(s / Math.max(1, x.length - from)); };

test("mono: mix, left, right, side", () => {
  const L = Float32Array.from([1, 0.5]), R = Float32Array.from([0, 0.5]);
  assert.deepEqual([...fold([L, R], "mix")], [0.5, 0.5]);
  assert.deepEqual([...fold([L, R], "left")], [1, 0.5]);
  assert.deepEqual([...fold([L, R], "right")], [0, 0.5]);
  assert.deepEqual([...fold([L, R], "side")], [0.5, 0]);
  assert.deepEqual([...fold([L], "side")], [0, 0], "a mono file has no side");
});

test("resampling: the length follows the rate, a tone keeps its level, and nothing above the new Nyquist folds back", () => {
  const x = sine(1000, 44100, 0.5), y = resample(x, 44100, 16000);
  assert.ok(Math.abs(y.length - 8000) <= 1, `length ${y.length}`);
  assert.ok(Math.abs(rms(y, 200) / rms(x) - 1) < 0.02, "1 kHz passes at 16 kHz");
  const hi = resample(sine(12000, 44100, 0.5), 44100, 16000);   // 12 kHz into 16 kHz: would alias to 4 kHz
  assert.ok(rms(hi, 200) / rms(sine(12000, 44100, 0.5)) < 0.01, `12 kHz at 16 kHz: ${(20 * Math.log10(rms(hi, 200) / 0.566)).toFixed(1)} dB`);
  const up = resample(sine(500, 22050, 0.2), 22050, 44100);
  assert.ok(Math.abs(rms(up, 100) / 0.566 - 1) < 0.02, "up: the level kept");
});

test("finish: silence trimmed, faded in and out, the peak at 0.95", () => {
  const x = new Float32Array(2000);
  for (let i = 500; i < 1500; i++) x[i] = Math.sin(i / 10) * 0.3;
  const s = finish(x, 44100);
  assert.ok(s.length < 1100 && s.length > 900, `${s.length}`);
  assert.equal(s[0], 0);
  assert.ok(Math.max(...s.map(Math.abs)) >= 31000 && Math.max(...s.map(Math.abs)) <= 31130);
});

test("ADPCM: encode / decode within a few % on a tone", () => {
  const s = Int16Array.from(sine(440, 22050, 0.2), (v) => Math.round(v * 30000)), d = imaDecode(imaEncode(s), s.length);
  let err = 0;
  for (let i = 200; i < s.length; i++) err = Math.max(err, Math.abs(d[i] - s[i]));
  assert.ok(err < 1500, `worst error ${err}`);
});

test("cost: half a byte a sample at the chosen rate", () => {
  const smp = { rate: 44100, ch: [new Float32Array(44100)] };
  assert.equal(costAt(smp, 44100), 22050);
  assert.equal(costAt(smp, 11025), 5513);
  assert.equal(CAP, 2 * SLOT_MAX_DATA + 0x10000 - 512, "(0.29: slot 3 is 64 KB)");
});

test("fit to space: the longest come down first, short hits keep their rate, locked ones are left alone", () => {
  const mk = (sec, rate = 44100, lock = false) => ({ rate: 44100, ch: [new Float32Array(Math.round(44100 * sec))], set: { rate, fold: "mix" }, lock });
  const set = [mk(0.3), mk(0.4), mk(9), mk(6), mk(0.5, 44100, true)];
  const r = fitToSpace(set);
  assert.ok(r, "it fits");
  assert.equal(r[0], 44100, "a short hit keeps 44.1");
  assert.equal(r[4], 44100, "locked");
  assert.ok(r[2] < 44100 && r[3] < 44100, `long ones lowered: ${r[2]} ${r[3]}`);
  assert.ok(RATES.indexOf(r[2]) >= RATES.indexOf(r[3]) - 1, "the longest at least as low (give or take a step)");
  assert.ok(packPlan(set.length, (i) => costAt(set[i], r[i])), "the plan packs");
  assert.equal(fitToSpace([mk(200)]), null, "200 s won't fit even at 8 kHz");
});

test("slot images: each zone with its own rate, read back the same", () => {
  const a = Int16Array.from(sine(220, 11025, 0.3), (v) => v * 20000), b = Int16Array.from(sine(220, 32000, 0.2), (v) => v * 20000);
  const im = slotImage("SMP1", [{ adpcm: imaEncode(a), n: a.length, rate: 11025 }, { adpcm: imaEncode(b), n: b.length, rate: 32000 }]);
  const h = parseSlot(im.hdr);
  assert.equal(h.nz, 2);
  assert.deepEqual(h.zones.map((z) => z.rate), [11025, 32000]);
  assert.equal(new DataView(im.hdr.buffer).getUint32(32 + 16, true), 16384, "11025 / 44100 in Q16");
  const parts = Array.from({ length: 20 }, () => ({ adpcm: new Uint8Array(1000), n: 2000, rate: 22050 }));
  const ims = imagesOf(parts);
  assert.deepEqual(ims.map((x) => x && parseSlot(x.hdr).nz), [16, 4, null], "16 zones a slot at most");
});

test("a WAV read: 16-bit stereo", () => {
  const n = 100, b = new Uint8Array(44 + n * 4), v = new DataView(b.buffer);
  b.set([..."RIFF"].map((c) => c.charCodeAt(0)), 0); v.setUint32(4, 36 + n * 4, true); b.set([..."WAVEfmt "].map((c) => c.charCodeAt(0)), 8);
  v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 2, true); v.setUint32(24, 48000, true);
  v.setUint32(28, 48000 * 4, true); v.setUint16(32, 4, true); v.setUint16(34, 16, true);
  b.set([..."data"].map((c) => c.charCodeAt(0)), 36); v.setUint32(40, n * 4, true);
  for (let i = 0; i < n; i++) { v.setInt16(44 + 4 * i, 16384, true); v.setInt16(46 + 4 * i, -16384, true); }
  const w = readWav(b);
  assert.equal(w.rate, 48000);
  assert.equal(w.ch.length, 2);
  assert.equal(w.ch[0][5], 0.5);
  assert.equal(w.ch[1][5], -0.5);
});

test("send and read back against the simulated FM-1: only the slots that changed are written", async () => {
  const m = new MockFM1(), link = await connect(m.access);
  const smp = { rate: 44100, ch: [sine(300, 44100, 0.5)], set: { rate: 16000, fold: "mix" } };
  const c = convert(smp), part = { adpcm: imaEncode(c.s), n: c.s.length, rate: c.rate };
  let dev = await readSlots(link);
  assert.deepEqual(dev, [null, null, null]);
  const n1 = await writeSlots(link, imagesOf([part]), dev);
  assert.equal(n1, 1);
  dev = await readSlots(link);
  const back = slotSamples(dev);
  assert.equal(back.length, 1);
  assert.equal(back[0].rate, 16000);
  assert.deepEqual([...back[0].adpcm], [...part.adpcm]);
  assert.equal(await writeSlots(link, imagesOf([part]), dev), 0, "nothing changed: nothing written");
  m.close();
});

test("slot 3 holds 64 KB (0.29): a sample that fits slots 1 and 2 but not 3 goes in them, too much is refused", async () => {
  const { packPlan, slotMax, SLOT_SIZES } = await import("../media.js");
  assert.deepEqual(SLOT_SIZES, [0x14000, 0x14000, 0x10000]);
  assert.equal(slotMax(2), 0x10000 - 512);
  const big = 0x12000;                                 // 72 KB: a slot 1 / 2 sample, too big for slot 3
  assert.deepEqual(packPlan(2, () => big), [0, 1]);
  assert.equal(packPlan(3, () => big), null, "a third 72 KB sample: slot 3 is too small");
  assert.deepEqual(packPlan(3, (i) => (i < 2 ? big : 0x10000 - 512)), [0, 1, 2], "exactly 64 KB less its header fits slot 3");
  assert.equal(packPlan(3, (i) => (i < 2 ? big : 0x10000 - 511)), null);
});

test("(0.30) read back right though stray copies of earlier replies arrive; a slot read wrong is refused", async () => {
  const m = new MockFM1(), link = await connect(m.access);
  const smp = { rate: 44100, ch: [sine(300, 44100, 0.5)], set: { rate: 16000, fold: "mix" } };
  const c = convert(smp), part = { adpcm: imaEncode(c.s), n: c.s.length, rate: c.rate };
  await writeSlots(link, imagesOf([part]), [null, null, null]);
  let last = null;                                   // every slot read answered first by a copy of the one before
  const reply = m.reply.bind(m);
  m.reply = (cmd, bytes) => { if (cmd === 16 && last) reply(16, last); if (cmd === 16) last = bytes; reply(cmd, bytes); };
  const dev = await readSlots(link);
  assert.deepEqual([...slotSamples(dev)[0].adpcm], [...part.adpcm], "the samples as sent, the copies passed over");
  m.reply = reply;
  m.slots[0].buf[512 + 100] ^= 0x55;                 // a byte of the samples changed: the slot's CRC no longer fits
  await assert.rejects(readSlots(link), /read back wrong/);
  m.close();
});

test("(0.31.2) adding a sample sends only what changed: the slot's first sector and the new sectors", async () => {
  const m = new MockFM1(), link = await connect(m.access);
  const mk = (len, seed) => ({ adpcm: Uint8Array.from({ length: len }, (_, i) => (i * seed + (i >> 5)) & 255), n: 2 * len, rate: 22050 });
  const a = mk(40000, 7), b = mk(5500, 13);           // 40 KB, then 5.5 KB more
  try {
  let dev = await readSlots(link);
  await writeSlots(link, imagesOf([a]), dev);
  const full = m.writes;
  dev = await readSlots(link);
  m.writes = 0;
  await writeSlots(link, imagesOf([a, b]), dev);
  const back = slotSamples(await readSlots(link));
  assert.equal(back.length, 2);
  assert.deepEqual([...back[0].adpcm], [...a.adpcm], "the first sample as it was");
  assert.deepEqual([...back[1].adpcm], [...b.adpcm], "the new one after it");
  assert.ok(m.writes < full / 2, `only the new part sent: ${m.writes} writes against ${full} for the first`);
  } finally { m.close(); }
});

test("(0.33, editor) a sample on the FM-1 given a new rate: decoded, resampled, sent; same length in time, same pitch, slices moved", async () => {
  const m = new MockFM1(), link = await connect(m.access);
  const smp = { rate: 44100, ch: [sine(440, 44100, 0.5)], set: { rate: 32000, fold: "mix" } };
  const c = convert(smp);
  await writeSlots(link, imagesOf([{ adpcm: imaEncode(c.s), n: c.s.length, rate: 32000 }]), await readSlots(link));
  let dev = await readSlots(link);
  const old = slotSamples(dev)[0];
  const src = fromDevice(old.adpcm, old.n, old.rate, 11025);
  assert.equal(src.rate, 32000);
  assert.equal(src.set.trim, false);
  const c2 = convert(src);
  assert.ok(Math.abs(c2.s.length - Math.round((old.n * 11025) / 32000)) <= 1, "not trimmed: the same length in time");
  let zc = 0;                                        // (the pitch: zero crossings a second)
  for (let i = 1; i < c2.s.length; i++) if ((c2.s[i - 1] < 0) !== (c2.s[i] < 0)) zc++;
  assert.ok(Math.abs((zc / 2) / (c2.s.length / 11025) - 440) < 5, `still 440 Hz (${(zc / 2) / (c2.s.length / 11025)})`);
  const back = imaDecode(imaEncode(c2.s), c2.s.length);
  assert.ok(rms(Float32Array.from(back, (v) => v / 32768)) > 0.5, "the level kept");
  await writeSlots(link, imagesOf([{ adpcm: imaEncode(c2.s), n: c2.s.length, rate: 11025 }]), dev);
  dev = await readSlots(link);
  const now = slotSamples(dev)[0];
  assert.equal(now.rate, 11025);
  assert.ok(now.adpcm.length < old.adpcm.length / 2.5, "less space");
  m.close();
});

test("(0.33, editor) slices rescaled to a new length: same places in time, in order, inside the sample", () => {
  assert.deepEqual(rescaleSlices([0, 8000, 16000, 24000], 32000, 11025), [0, 2756, 5513, 8269]);
  assert.deepEqual(rescaleSlices([0, 1, 2, 30000], 32000, 8), [0, 7]);   // (crowded ones merge)
  assert.deepEqual(rescaleSlices([0, 100], 200, 400), [0, 200]);
});
