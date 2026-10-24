// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT web editor: a project's samples (0.12). Copyright (C) 2026 Jamie (TwistedGenetics).
//
// Samples go on the FM-1 in its three sample slots (X0X's layout, 80 KB each, up to 16 zones a slot), 4-bit IMA ADPCM
// at a rate chosen per sample. Here: the file read in, folded to mono (mix, left, right or side), resampled through a
// proper low-pass (no aliasing), trimmed and faded, then encoded; the cost of each choice in KB and seconds; "fit to
// space" lowering the longest samples first; the slot images built (each zone with its own rate) and sent with
// Felucca's sample-slot commands (11-16), and read back. The slot layout, the encoder and the CRC are the breaks page's
// (X0X's tools/upload_breaks.py), so both pages write the same bytes.

export const SLOT_SIZE = 0x14000, SLOT_DATA_OFF = 512, SLOT_ZONES = 16, NSLOT = 3;
export const SLOT_HDR_LEN = 32 + SLOT_ZONES * 28, SLOT_MAX_DATA = SLOT_SIZE - SLOT_DATA_OFF;
export const CAP = NSLOT * SLOT_MAX_DATA;
export const RATES = [44100, 32000, 26040, 22050, 16000, 11025, 8000];
export const RATE_NAME = { 44100: "44.1 kHz", 32000: "32 kHz", 26040: "26 kHz (SP-1200)", 22050: "22 kHz", 16000: "16 kHz",
  11025: "11 kHz", 8000: "8 kHz" };
export const FOLDS = ["mix", "left", "right", "side"];
const ROOT_BASE = 36;

const IMA_STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88,
  97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
  724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660,
  4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
  18500, 20350, 22385, 24623, 27086, 29794, 32767];
const IMA_IDX = [-1, -1, -1, -1, 2, 4, 6, 8];

// ---- reading -------------------------------------------------------------------------------------------------
// a WAV -> {rate, ch: [Float32Array per channel]} (PCM 8/16/24/32, float); other formats: the browser's decoder
export function readWav(buf) {
  const u = buf instanceof Uint8Array ? buf : new Uint8Array(buf);
  const d = new DataView(u.buffer, u.byteOffset, u.byteLength);
  const tag4 = (o) => String.fromCharCode(d.getUint8(o), d.getUint8(o + 1), d.getUint8(o + 2), d.getUint8(o + 3));
  if (d.byteLength < 12 || tag4(0) !== "RIFF" || tag4(8) !== "WAVE") throw new Error("not a WAV file");
  const chunks = {};
  for (let i = 12; i + 8 <= d.byteLength;) {
    const id = tag4(i), n = d.getUint32(i + 4, true);
    if (!(id in chunks)) chunks[id] = [i + 8, n];
    i += 8 + n + (n & 1);
  }
  if (!chunks["fmt "]) throw new Error("WAV without a fmt chunk");
  const fo = chunks["fmt "][0];
  let tag = d.getUint16(fo, true);
  const nch = d.getUint16(fo + 2, true), sr = d.getUint32(fo + 4, true), bits = d.getUint16(fo + 14, true);
  if (tag === 0xFFFE) tag = d.getUint16(fo + 24, true);
  const bps = bits >> 3;
  if ((tag !== 1 && tag !== 3) || !bps || !nch) throw new Error(`unsupported WAV (format ${tag}, ${bits} bit)`);
  const [o, n] = chunks.data || [0, 0], frame = bps * nch, nfr = Math.floor(Math.min(n, d.byteLength - o) / frame);
  const val = (p) => {
    if (tag === 3) return bps === 4 ? d.getFloat32(p, true) : d.getFloat64(p, true);
    if (bps === 2) return d.getInt16(p, true) / 32768;
    if (bps === 3) return ((d.getUint8(p) << 8 | d.getUint8(p + 1) << 16 | d.getUint8(p + 2) << 24) >> 0) / 2147483648;
    if (bps === 4) return d.getInt32(p, true) / 2147483648;
    return (d.getUint8(p) - 128) / 128;
  };
  const ch = Array.from({ length: Math.min(nch, 2) }, () => new Float32Array(nfr));
  for (let f = 0; f < nfr; f++)
    for (let c = 0; c < ch.length; c++) ch[c][f] = val(o + f * frame + c * bps);
  return { rate: sr, ch };
}

// ---- converting ----------------------------------------------------------------------------------------------
// stereo to mono: both sides (mix), one side, or the difference (side: what only stereo has)
export function fold(ch, how = "mix") {
  const L = ch[0], R = ch[1] || ch[0], n = L.length, out = new Float32Array(n);
  for (let i = 0; i < n; i++)
    out[i] = how === "left" ? L[i] : how === "right" ? R[i] : how === "side" ? (L[i] - R[i]) / 2 : (L[i] + R[i]) / 2;
  return out;
}

// a windowed-sinc low-pass (Blackman), cutoff fc of the sample rate (0..0.5), taps odd
function lowpass(x, fc, taps = 63) {
  const h = new Float32Array(taps), m = (taps - 1) / 2;
  let sum = 0;
  for (let i = 0; i < taps; i++) {
    const k = i - m, w = 0.42 - 0.5 * Math.cos((2 * Math.PI * i) / (taps - 1)) + 0.08 * Math.cos((4 * Math.PI * i) / (taps - 1));
    h[i] = (k === 0 ? 2 * fc : Math.sin(2 * Math.PI * fc * k) / (Math.PI * k)) * w;
    sum += h[i];
  }
  for (let i = 0; i < taps; i++) h[i] /= sum;
  const out = new Float32Array(x.length);
  for (let i = 0; i < x.length; i++) {
    let acc = 0;
    const j0 = Math.max(0, i - m), j1 = Math.min(x.length - 1, i + m);
    for (let j = j0; j <= j1; j++) acc += x[j] * h[j - i + m];
    out[i] = acc;
  }
  return out;
}

// sr -> to: low-passed below the new Nyquist first (when going down), then read at the new rate (cubic)
export function resample(x, sr, to) {
  if (sr === to) return Float32Array.from(x);
  const src = to < sr ? lowpass(x, (0.45 * to) / sr, to < sr / 3 ? 127 : 63) : x;
  const n = Math.max(1, Math.floor(((x.length - 1) * to) / sr) + 1), out = new Float32Array(n), step = sr / to;
  const at = (i) => src[Math.max(0, Math.min(src.length - 1, i))];
  for (let k = 0; k < n; k++) {
    const p = k * step, i = Math.floor(p), f = p - i;
    const y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);   // Catmull-Rom
    out[k] = y1 + 0.5 * f * (y2 - y0 + f * (2 * y0 - 5 * y1 + 4 * y2 - y3 + f * (3 * (y1 - y2) + y3 - y0)));
  }
  return out;
}

// silence off both ends (below -60 dB of the peak), 2 ms fades, peak at 0.95: -> Int16Array
export function finish(x, rate, { trim = true } = {}) {
  let pk = 1e-9;
  for (const v of x) pk = Math.max(pk, Math.abs(v));
  let a = 0, b = x.length;
  if (trim) {
    const th = pk * 0.001;
    while (a < b && Math.abs(x[a]) < th) a++;
    while (b > a && Math.abs(x[b - 1]) < th) b--;
  }
  const n = Math.max(1, b - a), out = new Int16Array(n), f = Math.max(1, Math.round(rate * 0.002)), g = 0.95 / pk;
  for (let i = 0; i < n; i++) {
    const w = Math.min(1, i / f, (n - 1 - i) / f);
    out[i] = Math.max(-32768, Math.min(32767, Math.round(x[a + i] * g * (trim ? Math.max(0, w) : 1) * 32767)));
  }
  return out;
}

// a sample, as it will be on the FM-1: {s: Int16Array, rate}
export function convert(sample, set = sample.set) {
  const mono = fold(sample.ch, set.fold), r = resample(mono, sample.rate, set.rate);
  return { s: finish(r, set.rate, { trim: set.trim !== false }), rate: set.rate };
}

// what a sample costs at a rate (before trimming: an upper bound), ADPCM: half a byte a sample
export const framesAt = (sample, rate) => Math.ceil((sample.ch[0].length * rate) / sample.rate);
export const costAt = (sample, rate) => (framesAt(sample, rate) + 1) >> 1;
export const seconds = (sample) => sample.ch[0].length / sample.rate;

// ---- ADPCM ---------------------------------------------------------------------------------------------------
export function imaEncode(s) {
  let pred = 0, idx = 0;
  const out = new Uint8Array((s.length + 1) >> 1);
  for (let n = 0; n < s.length; n++) {
    const step = IMA_STEP[idx];
    let diff = s[n] - pred, code = 0;
    if (diff < 0) { code = 8; diff = -diff; }
    let vd = step >> 3;
    if (diff >= step) { code |= 4; diff -= step; vd += step; }
    if (diff >= step >> 1) { code |= 2; diff -= step >> 1; vd += step >> 1; }
    if (diff >= step >> 2) { code |= 1; vd += step >> 2; }
    pred = code & 8 ? pred - vd : pred + vd;
    if (pred > 32767) pred = 32767; else if (pred < -32768) pred = -32768;
    idx += IMA_IDX[code & 7];
    if (idx < 0) idx = 0; else if (idx > 88) idx = 88;
    out[n >> 1] |= n & 1 ? code << 4 : code;
  }
  return out;
}
export function imaDecode(b, n) {
  let pred = 0, idx = 0;
  const out = new Int16Array(n);
  for (let i = 0; i < n; i++) {
    const code = (b[i >> 1] >> (i & 1 ? 4 : 0)) & 15, step = IMA_STEP[idx];
    let vd = step >> 3;
    if (code & 4) vd += step;
    if (code & 2) vd += step >> 1;
    if (code & 1) vd += step >> 2;
    pred = code & 8 ? pred - vd : pred + vd;
    if (pred > 32767) pred = 32767; else if (pred < -32768) pred = -32768;
    idx += IMA_IDX[code & 7];
    if (idx < 0) idx = 0; else if (idx > 88) idx = 88;
    out[i] = pred;
  }
  return out;
}

// ---- fitting ------------------------------------------------------------------------------------------------
// the samples, in order, into the three slots (a slot filled before the next; 16 zones or 80 KB a slot): the slot of
// each sample, or null if they don't fit. bytes(i): sample i's ADPCM bytes
export function packPlan(n, bytes) {
  const slotOf = [];
  let k = 0, used = 0, zones = 0;
  for (let i = 0; i < n; i++) {
    const b = bytes(i);
    if (b > SLOT_MAX_DATA) return null;
    if (used + b > SLOT_MAX_DATA || zones >= SLOT_ZONES) { k++; used = 0; zones = 0; }
    if (k >= NSLOT) return null;
    slotOf.push(k);
    used += b;
    zones++;
  }
  return slotOf;
}
// rates that make the set fit: each sample's own choice where it can, the longest samples lowered first (a long pad
// loses the least by it; a short drum hit keeps its top end), one step at a time. Locked samples keep theirs.
// -> the rates, or null if even 8 kHz all round doesn't fit
export function fitToSpace(samples) {
  const r = samples.map((s) => s.set.rate), idx = (v) => RATES.indexOf(v);
  const fits = () => packPlan(samples.length, (i) => costAt(samples[i], r[i])) !== null;
  for (let guard = 0; guard < 200 && !fits(); guard++) {
    let best = -1;
    samples.forEach((s, i) => {
      if (s.lock || idx(r[i]) >= RATES.length - 1) return;
      if (best < 0 || seconds(s) > seconds(samples[best])) best = i;
    });
    if (best < 0) return null;
    r[best] = RATES[idx(r[best]) + 1];
  }
  return fits() ? r : null;
}

// ---- the slots ----------------------------------------------------------------------------------------------
let CRC_T = null;
export function crc32(b) {
  if (!CRC_T) {
    CRC_T = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
      let c = n;
      for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
      CRC_T[n] = c >>> 0;
    }
  }
  let c = 0xFFFFFFFF;
  for (let i = 0; i < b.length; i++) c = CRC_T[(c ^ b[i]) & 0xFF] ^ (c >>> 8);
  return (c ^ 0xFFFFFFFF) >>> 0;
}
const keySplit = (roots) => roots.map((r, j) => [j === 0 ? 0 : ((roots[j - 1] + r) >> 1) + 1, j === roots.length - 1 ? 127 : (r + roots[j + 1]) >> 1]);

// a slot image from encoded parts [{adpcm, n, rate}] (each zone at its own rate) -> {hdr, data}
export function slotImage(name, parts) {
  const total = parts.reduce((a, p) => a + p.adpcm.length, 0);
  if (total > SLOT_MAX_DATA) throw new Error(`too long: ${total} B, a slot holds ${SLOT_MAX_DATA} B`);
  if (parts.length < 1 || parts.length > SLOT_ZONES) throw new Error(`1..${SLOT_ZONES} samples a slot`);
  const data = new Uint8Array(total), zs = [];
  let off = 0;
  parts.forEach((p, i) => { data.set(p.adpcm, off); zs.push({ off, n: p.n, rate: p.rate || 22050, root: ROOT_BASE + i }); off += p.adpcm.length; });
  const split = keySplit(zs.map((z) => z.root)), hdr = new Uint8Array(SLOT_HDR_LEN), v = new DataView(hdr.buffer);
  v.setUint32(0, 0x504D5346, true);
  v.setUint16(4, 1, true);
  hdr[6] = zs.length;
  const nm = name.toUpperCase().replace(/[^\x20-\x7E]/g, "").slice(0, 8);
  for (let i = 0; i < nm.length; i++) hdr[8 + i] = nm.charCodeAt(i);
  v.setUint32(16, data.length, true);
  v.setUint32(20, crc32(data), true);
  zs.forEach((z, i) => {
    const o = 32 + i * 28;
    v.setUint32(o, z.off, true);
    v.setUint32(o + 4, z.n, true);
    v.setUint32(o + 8, 0, true);
    v.setUint32(o + 12, z.n - 1, true);
    v.setUint32(o + 16, Math.round((z.rate / 44100) * 65536), true);   // the zone's rate (the FM-1 plays each at its own)
    v.setInt16(o + 20, z.root * 16, true);
    v.setInt16(o + 22, 0, true);
    hdr[o + 24] = 0;
    hdr[o + 25] = split[i][0];
    hdr[o + 26] = split[i][1];
  });
  return { hdr, data };
}
export function parseSlot(hdr) {
  const v = new DataView(hdr.buffer, hdr.byteOffset, hdr.byteLength);
  if (hdr.byteLength < SLOT_HDR_LEN || v.getUint32(0, true) !== 0x504D5346) return null;
  const nz = hdr[6], zones = [];
  let name = "";
  for (let i = 8; i < 16 && hdr[i]; i++) name += String.fromCharCode(hdr[i]);
  for (let i = 0; i < nz && i < SLOT_ZONES; i++) {
    const o = 32 + i * 28;
    zones.push({ off: v.getUint32(o, true), n: v.getUint32(o + 4, true), rate: Math.round((v.getUint32(o + 16, true) * 44100) / 65536) });
  }
  return { nz, name, dataLen: v.getUint32(16, true), zones };
}
export function sameImage(a, b) {
  if (!a || !b) return !a && !b;
  if (a.data.length !== b.data.length || a.hdr.length !== b.hdr.length) return false;
  for (let i = 0; i < a.hdr.length; i++) if (a.hdr[i] !== b.hdr[i]) return false;
  for (let i = 0; i < a.data.length; i++) if (a.data[i] !== b.data[i]) return false;
  return true;
}

// ---- the FM-1 (Felucca's sample-slot commands on the editor link) --------------------------------------------
const p7 = (b) => {
  const out = [];
  for (let i = 0; i < b.length; i += 7) {
    let m = 0;
    for (let j = 0; j < 7 && i + j < b.length; j++) m |= ((b[i + j] >> 7) & 1) << j;
    out.push(m);
    for (let j = 0; j < 7 && i + j < b.length; j++) out.push(b[i + j] & 0x7F);
  }
  return out;
};
const u7 = (a) => {
  const out = [];
  for (let i = 0; i < a.length;) {
    const m = a[i++];
    for (let j = 0; j < 7 && i < a.length; j++) out.push(a[i++] | ((m >> j) & 1) << 7);
  }
  return new Uint8Array(out);
};
async function retry(link, cmd, args, ms, tries = 3) {
  for (let k = 1; ; k++) { try { return await link.req(cmd, args, ms); } catch (e) { if (k >= tries) throw e; } }
}
async function readBytes(link, k, off, n, onPart) {
  const out = new Uint8Array(n);
  for (let at = 0; at < n;) {
    const want = Math.min(224, n - at), adr = off + at;
    const r = await retry(link, 16, [k, adr & 0x7F, (adr >> 7) & 0x7F, (adr >> 14) & 0x7F, (want + 1) >> 1], 3000);
    const b = u7(r.slice(4));
    if (!b.length) throw new Error("no sample data");
    out.set(b.subarray(0, Math.min(b.length, n - at)), at);
    at += Math.min(b.length, n - at);
    if (onPart) onPart(at / n);
  }
  return out;
}
// the three slots as they are on the FM-1: [{hdr, data} | null]
export async function readSlots(link, onPart = () => {}) {
  const info = await link.req(15, []), nz = [];
  for (let k = 0, i = 2; k < info[0]; k++) { nz.push(info[i]); i = info.indexOf(0, i + 1) + 2; }
  const dev = [null, null, null];
  for (let k = 0; k < NSLOT; k++) {
    if (!nz[k]) continue;
    const hdr = await readBytes(link, k, 0, SLOT_HDR_LEN), h = parseSlot(hdr);
    if (!h) continue;
    dev[k] = { hdr, data: await readBytes(link, k, SLOT_DATA_OFF, h.dataLen, (f) => onPart((k + f) / NSLOT)) };
  }
  return dev;
}
// the samples in the slots read back: [{adpcm, n, rate, slot}]
export function slotSamples(dev) {
  const out = [];
  dev.forEach((im, k) => {
    const h = im && parseSlot(im.hdr);
    if (h) for (const z of h.zones) out.push({ adpcm: im.data.slice(z.off, z.off + ((z.n + 1) >> 1)), n: z.n, rate: z.rate, slot: k });
  });
  return out;
}
// write the slots that differ from what's there (the others untouched: run it again after a cut-off and it carries
// on from where it stopped). images: [{hdr, data} | null] x 3
export async function writeSlots(link, images, dev, onPart = () => {}, say = () => {}) {
  const todo = [];
  images.forEach((im, k) => { if (!sameImage(im, dev[k])) todo.push({ k, im }); });
  const total = todo.reduce((a, t) => a + (t.im ? t.im.data.length : 1), 0) || 1;
  let sent = 0;
  for (const { k, im } of todo) {
    if (!im) {
      say(`Clearing slot ${k + 1}…`);
      const r = await link.req(14, [k], 30000);
      if (r[1]) throw new Error("The FM-1 couldn't clear its sample memory.");
      sent += 1;
      continue;
    }
    say(`Erasing slot ${k + 1}…`);
    const b = await link.req(11, [k], 10000);
    if (b[1]) throw new Error("The FM-1 couldn't erase its sample memory.");
    say(`Sending slot ${k + 1}…`);
    for (let off = 0; off < im.data.length; off += 256) {
      const adr = SLOT_DATA_OFF + off;
      const r = await retry(link, 12, [k, adr & 0x7F, (adr >> 7) & 0x7F, (adr >> 14) & 0x7F, ...p7(im.data.subarray(off, off + 256))], 3000);
      if (r[4]) throw new Error("Writing to the FM-1 failed.");
      sent += Math.min(256, im.data.length - off);
      onPart(sent / total);
    }
    const e = await link.req(13, [k, ...p7(im.hdr)], 5000);
    if (e[1]) throw new Error("The FM-1 didn't accept the samples.");
    dev[k] = im;
  }
  return todo.length;
}
// a project's samples ({adpcm, n, rate}, in order) -> the three slot images (null: empty)
export function imagesOf(parts) {
  const plan = packPlan(parts.length, (i) => parts[i].adpcm.length);
  if (!plan) throw new Error("The samples don't fit in the FM-1's sample memory.");
  const groups = [[], [], []];
  parts.forEach((p, i) => groups[plan[i]].push(p));
  return groups.map((g, k) => (g.length ? slotImage(`SMP${k + 1}`, g) : null));
}
