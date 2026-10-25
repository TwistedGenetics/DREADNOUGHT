// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT web editor: WAVE's wavetables (0.32). Copyright (C) 2026 Jamie (TwistedGenetics).
//
// A Serum wavetable (single cycles one after another in one WAV, 2048 samples a cycle unless its `clm ` chunk says
// otherwise) or a single-cycle WAV, turned into the FM-1's format exactly as tools/wt_convert.py does (the host tests
// check the two agree to the last bit or one): a 16-byte header ("DNWT", version 1, frames, 9, 7, the name in 8
// bytes), then each frame as 7 band-limited versions, 512 samples (harmonics 1-127) down to 8 (1), int16 LE. The
// FM-1 keeps it as a zone of a sample slot (sent with the samples), the editor shows and plays it here first.

export const SIZE = 512, MIPS = 7, MAXF = 64, PEAK = 32000, HDR = 16;
export const HMAX = Array.from({ length: MIPS }, (_, m) => (128 >> m) - 1);
export const MIPOFF = [0, 512, 768, 896, 960, 992, 1008];
export const FRAME_BYTES = 2 * MIPOFF[MIPS - 1] + 2 * 8;   // 2032
export const tableBytes = (nf) => HDR + nf * FRAME_BYTES;

// a WAV -> {x: Float64Array (mono: the channels' mean), clm: the cycle length its clm chunk gives, or null}
export function readTableWav(buf) {
  const u = buf instanceof Uint8Array ? buf : new Uint8Array(buf);
  const d = new DataView(u.buffer, u.byteOffset, u.byteLength);
  const tag4 = (o) => String.fromCharCode(u[o], u[o + 1], u[o + 2], u[o + 3]);
  if (u.length < 12 || tag4(0) !== "RIFF" || tag4(8) !== "WAVE") throw new Error("not a WAV file");
  let fmt = null, pcm = null, clm = null;
  for (let i = 12; i + 8 <= u.length;) {
    const id = tag4(i), n = d.getUint32(i + 4, true), o = i + 8;
    if (id === "fmt ") {
      let tag = d.getUint16(o, true);
      const ch = d.getUint16(o + 2, true), bits = d.getUint16(o + 14, true);
      if (tag === 0xFFFE && n >= 26) tag = d.getUint16(o + 24, true);
      fmt = { tag, ch, bits };
    } else if (id === "data") {
      pcm = [o, Math.min(n, u.length - o)];
    } else if (id === "clm ") {
      const t = String.fromCharCode(...u.subarray(o, Math.min(o + n, u.length)));
      const tok = t.startsWith("<!>") ? t.slice(3).trim().split(/\s+/)[0] || "" : "";
      if (t.startsWith("<!>")) clm = /^[+-]?\d+$/.test(tok) ? parseInt(tok, 10) : null;
    }
    i += 8 + n + (n & 1);
  }
  if (!fmt || !pcm) throw new Error("no audio in that WAV");
  const { tag, ch, bits } = fmt, [o, n] = pcm;
  let bps, val;
  if (tag === 3 && bits === 32) { bps = 4; val = (p) => d.getFloat32(p, true); }
  else if (tag === 1 && bits === 16) { bps = 2; val = (p) => d.getInt16(p, true) / 32768; }
  else if (tag === 1 && bits === 24) {
    bps = 3;
    val = (p) => { const v = u[p] | u[p + 1] << 8 | u[p + 2] << 16; return (v >= 1 << 23 ? v - (1 << 24) : v) / 8388608; };
  } else if (tag === 1 && bits === 32) { bps = 4; val = (p) => d.getInt32(p, true) / 2147483648; }
  else throw new Error(`a WAV of format ${tag}, ${bits} bits: not 16 / 24 / 32-bit or 32-bit float`);
  const total = Math.floor(n / bps), nfr = Math.floor(total / ch), x = new Float64Array(nfr);
  for (let f = 0; f < nfr; f++) {
    let s = 0;
    for (let c = 0; c < ch; c++) s += val(o + (f * ch + c) * bps);
    x[f] = s / ch;
  }
  return { x, clm };
}

const roundHalfEven = (v) => { const r = Math.round(v); return Math.abs(v % 1) === 0.5 ? 2 * Math.round(v / 2) : r; };   // (Python's round)

// one cycle's harmonics 1..127 (re, im) by a DFT over its own length (wt_convert.harmonics)
function harmonics(c) {
  const n = c.length, H = HMAX[0], re = new Float64Array(H), im = new Float64Array(H);
  const cs = new Float64Array(n), sn = new Float64Array(n);
  for (let t = 0; t < n; t++) { cs[t] = Math.cos((2 * Math.PI * t) / n); sn[t] = Math.sin((2 * Math.PI * t) / n); }
  for (let k = 1; k <= H; k++) {
    let a = 0, b = 0;
    for (let t = 0, j = 0; t < n; t++, j = (j + k) % n) { a += c[t] * cs[j]; b += c[t] * sn[j]; }
    re[k - 1] = a; im[k - 1] = -b;
  }
  return { re, im };
}
// version m: 512 >> m samples from harmonics 1 .. HMAX[m] (wt_convert.mip)
function mip(h, m, nIn) {
  const size = SIZE >> m, out = new Float64Array(size), cs = new Float64Array(size), sn = new Float64Array(size);
  for (let t = 0; t < size; t++) { cs[t] = Math.cos((2 * Math.PI * t) / size); sn[t] = Math.sin((2 * Math.PI * t) / size); }
  for (let t = 0; t < size; t++) {
    let a = 0, b = 0;
    for (let k = 1, j = t % size; k <= HMAX[m]; k++, j = (j + t) % size) { a += cs[j] * h.re[k - 1]; b += sn[j] * h.im[k - 1]; }
    out[t] = (2 / nIn) * (a - b);
  }
  return out;
}

// samples (mono float) -> the FM-1's table (Uint8Array); opts: {frame, clm, frames, name}
export function convert(x, { frame = null, clm = null, frames = null, name = "TABLE" } = {}) {
  const cyc = frame || clm || (x.length <= 4096 ? x.length : 2048), nf = Math.floor(x.length / cyc);
  if (nf < 1) throw new Error(`shorter than one cycle of ${cyc} samples`);
  const want = Math.min(frames || MAXF, MAXF, nf);
  let pick = Array.from({ length: nf }, (_, i) => i);
  if (want < nf) pick = Array.from({ length: want }, (_, i) => (want > 1 ? roundHalfEven((i * (nf - 1)) / (want - 1)) : 0));
  const out = pick.map((f) => {
    const c = Float64Array.from(x.subarray(f * cyc, (f + 1) * cyc));
    let mean = 0;
    for (let i = 0; i < cyc; i++) mean += c[i];
    mean /= cyc;
    for (let i = 0; i < cyc; i++) c[i] -= mean;
    const h = harmonics(c);
    return Array.from({ length: MIPS }, (_, m) => mip(h, m, cyc));
  });
  let peak = 0;
  for (const fr of out) for (const v of fr[0]) peak = Math.max(peak, Math.abs(v));
  const scale = PEAK / (peak || 1), b = new Uint8Array(tableBytes(out.length)), dv = new DataView(b.buffer);
  b.set([0x44, 0x4E, 0x57, 0x54, 1, out.length, 9, MIPS]);
  const nm = name.toUpperCase().replace(/[^\x00-\x7F]/g, "?").slice(0, 8);
  for (let i = 0; i < nm.length; i++) b[8 + i] = nm.charCodeAt(i);
  let o = HDR;
  for (const fr of out)
    for (const m of fr)
      for (const v of m) { dv.setInt16(o, Math.max(-32768, Math.min(32767, Math.floor(v * scale + 0.5))), true); o += 2; }
  return b;
}
// a WAV file's bytes -> the table (the cycle length: the file's clm chunk, else frame, else 2048 / the whole file)
export function fromWav(bytes, opts = {}) {
  const { x, clm } = readTableWav(bytes);
  return convert(x, { ...opts, clm });
}

// a table read: {frames, name, frame(f, m = 0) -> Int16Array}; null if it isn't one
export function parseTable(t) {
  if (!t || t.length < HDR || t[0] !== 0x44 || t[1] !== 0x4E || t[2] !== 0x57 || t[3] !== 0x54 || t[4] !== 1 || !t[5]
    || t[5] > MAXF || t[6] !== 9 || t[7] !== MIPS || t.length < tableBytes(t[5])) return null;
  let name = "";
  for (let i = 8; i < 16 && t[i] >= 32 && t[i] < 127; i++) name += String.fromCharCode(t[i]);
  const dv = new DataView(t.buffer, t.byteOffset, t.byteLength);
  const frame = (f, m = 0) => {
    const n = SIZE >> m, a = new Int16Array(n), o = HDR + f * FRAME_BYTES + 2 * MIPOFF[m];
    for (let i = 0; i < n; i++) a[i] = dv.getInt16(o + 2 * i, true);
    return a;
  };
  return { frames: t[5], name, bytes: tableBytes(t[5]), frame };
}
export const isTable = (b) => !!parseTable(b);

// a note of the table at position pos (0..1 across the frames), as the FM-1 reads it (the version that can't alias,
// the two frames either side mixed) -> Float32Array, -1..1, with a short fade in and out
export function render(t, pos, note = 48, secs = 1, rate = 44100) {
  const p = parseTable(t);
  if (!p) return new Float32Array(0);
  const hz = 440 * 2 ** ((note - 69) / 12), inc = hz / rate;
  let m = 0;
  while (m < MIPS - 1 && HMAX[m] * hz > rate / 2) m++;
  const fp = pos * (p.frames - 1), fa = Math.floor(fp), fb = Math.min(p.frames - 1, fa + 1), tf = fp - fa;
  const A = p.frame(fa, m), B = p.frame(fb, m), n = Math.round(secs * rate), out = new Float32Array(n), sz = A.length;
  const fade = Math.min(441, n >> 2);
  let ph = 0;
  for (let i = 0; i < n; i++) {
    const q = ph * sz, j = Math.floor(q), f = q - j, j1 = (j + 1) % sz;
    const a = A[j] + (A[j1] - A[j]) * f, b = B[j] + (B[j1] - B[j]) * f;
    const g = i < fade ? i / fade : i > n - fade ? (n - i) / fade : 1;
    out[i] = ((a + (b - a) * tf) / 32768) * 0.6 * g;
    ph += inc;
    ph -= Math.floor(ph);
  }
  return out;
}

// ---- WAVE's values (src/dsp/dn_wt.h WP_*: names, ranges and texts as the FM-1's; the host tests compare) --------
export const WARPS = ["OFF", "SYNC", "BEND", "MIRROR", "PWM", "FM", "CRUSH"];
export const BMODES = ["MIX", "FM", "RING", "AM", "SYNC"];
export const FTYPES = ["LP12", "LP24", "HP", "BP", "NOTCH", "COMB"];
export const DTYPES = ["SOFT", "HARD", "FOLD", "RECT", "TUBE", "BITS", "RATE"];
export const LSHAPES = ["SINE", "TRI", "SAW", "SQUARE", "S&H", "RANDOM"];
export const SOURCES = ["OFF", "LFO 1", "LFO 2", "LFO 3", "MOD ENV", "ENV 3", "AMP ENV", "VEL", "KEY", "RANDOM"];
export const DESTS = ["OFF", "POS A", "POS B", "WARP A", "WARP B", "PITCH", "PITCH B", "MOD AMT", "B LEVEL", "CUTOFF", "RES",
  "DRIVE", "LEVEL", "PAN", "SUB", "NOISE", "L1 RATE", "L2 RATE", "DETUNE"];
export const DIVS = ["4 BARS", "2 BARS", "1 BAR", "1/2", "1/4D", "1/2T", "1/4", "1/8D", "1/4T", "1/8", "1/16D", "1/8T", "1/16",
  "1/16T", "1/32", "1/32T"];
export const WT = ["TABLE","POS","WARP","AMOUNT","UNISON","DETUNE","SPREAD","SUB","CUT","RES","ENV","DRIVE","ATK","DEC","SUS","REL","M.ATK","M.DEC","M.SUS","M.REL","POS ENV","POS LFO","L1 RATE","CUT LFO","LEVEL","VOICES","GLIDE","TRANS","OSC B","B TABLE","B POS","B WARP","B AMT","B OCT","B SEMI","B FINE","B LEVEL","B MODE","MOD AMT","NOISE","FILTER","KEY","DIST","SAT","L1 SHAPE","L1 SYNC","L1 MODE","L2 SHAPE","L2 RATE","L2 SYNC","L2 MODE","L3 SHAPE","L3 RATE","L3 SYNC","L3 MODE","E3 ATK","E3 DEC","E3 SUS","E3 REL","SOURCE","DEST","DEPTH","SOURCE","DEST","DEPTH","SOURCE","DEST","DEPTH","SOURCE","DEST","DEPTH","SOURCE","DEST","DEPTH","SOURCE","DEST","DEPTH","SOURCE","DEST","DEPTH","SOURCE","DEST","DEPTH"];
export const WT_M0 = 59, WT_NMOD = 8;
const I = Object.fromEntries(["TABLE", "POS", "WARP", "AMOUNT", "UNISON", "DETUNE", "SPREAD", "SUB", "CUT", "RES", "FENV", "DRIVE",
  "ATK", "DEC", "SUS", "REL", "MATK", "MDEC", "MSUS", "MREL", "PENV", "PLFO", "LRATE", "CLFO", "LEVEL", "VOICES", "GLIDE", "TRANS",
  "OSCB", "BTAB", "BPOS", "BWARP", "BWAMT", "BOCT", "BSEMI", "BFINE", "BLEVEL", "BMODE", "BAMT", "NOISE", "FTYPE", "FKEY", "DTYPE",
  "SAT", "L1SHAPE", "L1SYNC", "L1MODE", "L2SHAPE", "L2RATE", "L2SYNC", "L2MODE", "L3SHAPE", "L3RATE", "L3SYNC", "L3MODE",
  "E3ATK", "E3DEC", "E3SUS", "E3REL"].map((n, i) => [n, i]));
export const WP = I;
const WMIN = [0,0,0,0,1,0,0,0,0,0,-64,0,0,0,0,0,0,0,0,0,-64,0,0,0,0,1,0,-24,0,0,0,0,0,-3,-12,-50,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,-64,0,0,-64,0,0,-64,0,0,-64,0,0,-64,0,0,-64,0,0,-64,0,0,-64];
const WMAX = [0,127,6,127,4,127,127,127,127,127,63,127,127,127,127,127,127,127,127,127,63,127,127,127,127,6,127,24,1,0,127,6,127,3,12,50,127,4,127,127,5,127,6,127,5,1,1,5,127,1,1,5,127,1,1,127,127,127,127,9,18,63,9,18,63,9,18,63,9,18,63,9,18,63,9,18,63,9,18,63,9,18,63];
export const wtMin = (k) => WMIN[k];
export const wtMax = (k, ntab = 8) => (k === I.TABLE || k === I.BTAB ? ntab - 1 : WMAX[k]);
const RATE_OF = { [I.LRATE]: I.L1SYNC, [I.L2RATE]: I.L2SYNC, [I.L3RATE]: I.L3SYNC };
const f = Math.fround;
// a value as the FM-1 shows it (tables: the names of the tables; p: the sound, for an LFO's RATE: synced or not)
export function wtText(k, v, tables = [], p = null) {
  const cl = (a, n) => a[Math.max(0, Math.min(n.length - 1, v))];
  if (k === I.TABLE || k === I.BTAB) return (tables[v] || "-").slice(0, 7);
  if (k === I.WARP || k === I.BWARP) return cl(WARPS, WARPS);
  if (k === I.BMODE) return cl(BMODES, BMODES);
  if (k === I.FTYPE) return cl(FTYPES, FTYPES);
  if (k === I.DTYPE) return cl(DTYPES, DTYPES);
  if (k === I.L1SHAPE || k === I.L2SHAPE || k === I.L3SHAPE) return cl(LSHAPES, LSHAPES);
  if (k === I.OSCB) return v ? "ON" : "OFF";
  if (k === I.L1SYNC || k === I.L2SYNC || k === I.L3SYNC) return v ? "SYNC" : "FREE";
  if (k === I.L1MODE || k === I.L2MODE || k === I.L3MODE) return v ? "RETRIG" : "FREE";
  if (k >= WT_M0 && (k - WT_M0) % 3 === 0) return cl(SOURCES, SOURCES);
  if (k >= WT_M0 && (k - WT_M0) % 3 === 1) return cl(DESTS, DESTS);
  if (k in RATE_OF) {
    if (p && p[RATE_OF[k]]) return DIVS[Math.floor((Math.max(0, Math.min(127, v)) * 16) / 128)];
    let hz = f(0.05);
    for (let i = 0; i < v; i++) hz = f(hz * f(1.048309));
    let c = Math.trunc(f(f(hz * 100) + 0.5));
    if (c < 1000) return `${Math.floor(c / 100)}.${Math.floor(c / 10) % 10}${c % 10}Hz`;
    c = Math.floor((c + 5) / 10);
    return `${Math.floor(c / 10)}.${c % 10}Hz`;
  }
  if (k === I.VOICES && v === 1) return "MONO";
  if ((k === I.FENV || k === I.PENV || k === I.TRANS || k === I.BOCT || k === I.BSEMI || k === I.BFINE ||
       (k >= WT_M0 && (k - WT_M0) % 3 === 2)) && v > 0) return `+${v}`;
  return `${v}`;
}
// the pages, as the FM-1 groups them (each button again: its next view)
export const WT_GROUPS = [["WAVE A", [0, 1, 2, 3]], ["OSC A", [4, 5, 6, 7]], ["WAVE B", [29, 30, 31, 32]], ["OSC B", [28, 33, 34, 35]],
  ["B MIX", [36, 37, 38, 39]], ["FILTER", [40, 8, 9, 10]], ["DRIVE", [42, 11, 43, 41]], ["AMP", [12, 13, 14, 15]],
  ["MOD ENV", [16, 17, 18, 19]], ["ENV 3", [55, 56, 57, 58]], ["MOD", [20, 21, 22, 23]], ["LFO 1", [44, 22, 45, 46]],
  ["LFO 2", [47, 48, 49, 50]], ["LFO 3", [51, 52, 53, 54]], ["OUT", [24, 25, 26, 27]]];
