// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT web editor: FM in the browser (0.26), to hear a DX7 voice before it goes to the FM-1. Copyright (C)
// 2026 Jamie (TwistedGenetics).
//
// The FM-1's engine (src/dsp/dn_fm.c, itself ported from Dexed's MSFA, Apache-2.0) once more in JavaScript: the same
// tables made the same way, the same integer steps (BigInt where a product passes 2^53), one voice, blocks of 64.
// tests/host/editor_golden.mjs renders voices here and checks them against the FM-1's own renders.

const N = 64, FS = 44100;
// ---- the tables (tools/gen_dn_tables.py: as MSFA makes them) ----
const i32 = (x) => x | 0;
const SIN = new Int32Array(2048), EXP2 = new Int32Array(2048), FREQ = new Int32Array(1025), FINE = new Int32Array(100);
const DT = new Int32Array(128 * 15), LFO = new Uint32Array(100);
(function tables() {
  const n = 1024, dph = 2 * Math.PI / n, c = BigInt(Math.floor(Math.cos(dph) * (1 << 30) + 0.5)), s = BigInt(Math.floor(Math.sin(dph) * (1 << 30) + 0.5));
  const R = 1n << 29n, toI32 = (b) => Number(BigInt.asIntN(32, b));
  let u = 1n << 30n, v = 0n;
  for (let i = 0; i < n / 2; i++) {
    SIN[(i << 1) + 1] = Number((v + 32n) >> 6n);
    SIN[((i + n / 2) << 1) + 1] = -Number((v + 32n) >> 6n);
    const t = BigInt(toI32((u * s + v * c + R) >> 30n));
    u = BigInt(toI32((u * c - v * s + R) >> 30n));
    v = t;
  }
  for (let i = 0; i < n - 1; i++) SIN[i << 1] = SIN[(i << 1) + 3] - SIN[(i << 1) + 1];
  SIN[(n << 1) - 2] = -SIN[(n << 1) - 1];
  let y = 1 << 30;
  const inc = Math.pow(2, 1 / n);
  for (let i = 0; i < n; i++) { EXP2[(i << 1) + 1] = i32(Math.floor(y + 0.5)); y *= inc; }
  for (let i = 0; i < n - 1; i++) EXP2[i << 1] = i32(EXP2[(i << 1) + 3] - EXP2[(i << 1) + 1]);
  EXP2[(n << 1) - 2] = i32(2147483648 - EXP2[(n << 1) - 1]);
  y = Math.pow(2, 44) / FS;
  for (let i = 0; i <= n; i++) { FREQ[i] = i32(Math.floor(y + 0.5)); y *= inc; }
  for (let f = 0; f < 100; f++) FINE[f] = Math.floor(24204406.323123 * Math.log(1 + 0.01 * f) + 0.5);
  for (let note = 0; note < 128; note++) {
    const lf = 50857777 + Math.floor((1 << 24) / 12) * note, x = Math.fround(Math.fround(lf) / (1 << 24));
    const ratio = 0.0209 * Math.exp(-0.396 * x) / 7;
    for (let det = 0; det < 15; det++) DT[note * 15 + det] = Math.trunc(lf + ratio * lf * (det - 7)) - lf;
  }
  const src = [0.062541, 0.125031, 0.312393, 0.437120, 0.624610, 0.750694, 0.936330, 1.125302, 1.249609, 1.436782, 1.560915,
    1.752081, 1.875117, 2.062494, 2.247191, 2.374451, 2.560492, 2.686728, 2.873976, 2.998950, 3.188013, 3.369840, 3.500175,
    3.682224, 3.812065, 4.000800, 4.186202, 4.310716, 4.501260, 4.623209, 4.814636, 4.930480, 5.121901, 5.315191, 5.434783,
    5.617346, 5.750431, 5.946717, 6.062811, 6.248438, 6.431695, 6.564264, 6.749460, 6.868132, 7.052186, 7.250580, 7.375719,
    7.556294, 7.687577, 7.877738, 7.993605, 8.181967, 8.372405, 8.504848, 8.685079, 8.810573, 8.986341, 9.122423, 9.300595,
    9.500285, 9.607994, 9.798158, 9.950249, 10.117361, 11.251125, 11.384335, 12.562814, 13.676149, 13.904338, 15.092062,
    16.366612, 16.638935, 17.869907, 19.193858, 19.425019, 20.833333, 21.034918, 22.502250, 24.003841, 24.260068, 25.746653,
    27.173913, 27.578599, 29.052876, 30.693677, 31.191516, 32.658393, 34.317090, 34.674064, 36.416606, 38.197097, 38.550501,
    40.387722, 40.749796, 42.625746, 44.326241, 44.883303, 46.772685, 48.590865, 49.261084];
  const ratio = Math.floor(4437500000 * 64 / FS);
  src.forEach((x, i) => { LFO[i] = Math.floor(x * ratio) >>> 0; });
})();
const LFO_UNIT = Math.floor(64 * 25190424 / FS + 0.5), PENV_UNIT = Math.floor(64 * (1 << 24) / (21.3 * FS) + 0.5);

const sinL = (ph) => { const lo = ph & 16383, ix = (ph >> 13) & 2046; return SIN[ix + 1] + Math.floor(SIN[ix] * lo / 16384); };
const exp2L = (x) => { const lo = x & 16383, ix = (x >> 13) & 2046, y = EXP2[ix + 1] + Math.floor(EXP2[ix] * lo / 16384); return y >> (6 - (x >> 24)); };
const freqL = (lf) => { const ix = (lf & 0xffffff) >> 14, y0 = FREQ[ix], y1 = FREQ[ix + 1], y = y0 + Math.floor((y1 - y0) * (lf & 16383) / 16384); return y >> (20 - (lf >> 24)); };
const mulShr = (a, b, s) => Number((BigInt(a) * BigInt(b)) >> BigInt(s));   // (an int64 product, shifted)

// ---- the envelope, the pitch envelope (env.cc, pitchenv.cc) ----
const LEVELLUT = [0, 5, 9, 13, 17, 20, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 42, 43, 45, 46];
const STATICS = [1764000, 1764000, 1411200, 1411200, 1190700, 1014300, 992250, 882000, 705600, 705600, 584325, 507150, 502740,
  441000, 418950, 352800, 308700, 286650, 253575, 220500, 220500, 176400, 145530, 145530, 125685, 110250, 110250, 88200, 88200,
  74970, 61740, 61740, 55125, 48510, 44100, 37485, 31311, 30870, 27562, 27562, 22050, 18522, 17640, 15435, 14112, 13230, 11025,
  9261, 9261, 7717, 6615, 6615, 5512, 5512, 4410, 3969, 3969, 3439, 2866, 2690, 2249, 1984, 1896, 1808, 1411, 1367, 1234, 1146,
  926, 837, 837, 705, 573, 573, 529, 441, 441];
const scaleout = (ol) => (ol >= 20 ? 28 + ol : LEVELLUT[Math.max(0, ol)]);
class Env {
  constructor(r, l, ol, rs) { this.r = r; this.l = l; this.ol = ol; this.rs = rs; this.level = 0; this.down = true; this.advance(0); }
  advance(ix) {
    this.ix = ix;
    if (ix >= 4) return;
    const nl = this.l[ix];
    let act = scaleout(nl) >> 1;
    act = (act << 6) + this.ol - 4256;
    act = act < 16 ? 16 : act;
    this.target = act << 16;
    this.rising = this.target > this.level;
    let q = (this.r[ix] * 41) >> 6;
    q = Math.min(63, q + this.rs);
    if (this.target === this.level || (ix === 0 && nl === 0)) {
      const sr = Math.min(99, this.r[ix] + this.rs);
      this.statics = sr < 77 ? STATICS[sr] : 20 * (99 - sr);
      if (sr < 77 && ix === 0 && nl === 0) this.statics = Math.trunc(this.statics / 20);
    } else this.statics = 0;
    this.inc = (4 + (q & 3)) << (2 + 6 + (q >> 2));
  }
  sample() {
    if (this.statics) {
      this.statics -= N;
      if (this.statics <= 0) { this.statics = 0; this.advance(this.ix + 1); }
    }
    if (this.ix < 3 || (this.ix < 4 && !this.down)) {
      if (this.statics) { /* (holding) */ } else if (this.rising) {
        if (this.level < (1716 << 16)) this.level = 1716 << 16;
        this.level = i32(this.level + (((17 << 24) - this.level) >> 24) * this.inc);
        if (this.level >= this.target) { this.level = this.target; this.advance(this.ix + 1); }
      } else {
        this.level = i32(this.level - this.inc);
        if (this.level <= this.target) { this.level = this.target; this.advance(this.ix + 1); }
      }
    }
    return this.level;
  }
  key(d) { if (this.down !== d) { this.down = d; this.advance(d ? 0 : 3); } }
}
const PENV_RATE = [1, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 16, 16, 17, 18, 18, 19,
  20, 21, 22, 23, 24, 25, 26, 27, 28, 30, 31, 33, 34, 36, 37, 38, 39, 41, 42, 44, 46, 47, 49, 51, 53, 54, 56, 58, 60, 62, 64, 66, 68,
  70, 72, 74, 76, 79, 82, 85, 88, 91, 94, 98, 102, 106, 110, 115, 120, 125, 130, 135, 141, 147, 153, 159, 165, 171, 178, 185, 193,
  202, 211, 232, 243, 254, 255];
const PENV_TAB = [-128, -116, -104, -95, -85, -76, -68, -61, -56, -52, -49, -46, -43, -41, -39, -37, -35, -33, -32, -31, -30, -29,
  -28, -27, -26, -25, -24, -23, -22, -21, -20, -19, -18, -17, -16, -15, -14, -13, -12, -11, -10, -9, -8, -7, -6, -5, -4, -3, -2, -1,
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34,
  35, 38, 40, 43, 46, 49, 53, 58, 65, 73, 82, 92, 103, 115, 127];
class PEnv {
  constructor(r, l) { this.r = r; this.l = l; this.level = PENV_TAB[l[3]] * (1 << 19); this.down = true; this.advance(0); }
  advance(ix) {
    this.ix = ix;
    if (ix >= 4) return;
    this.target = PENV_TAB[this.l[ix]] * (1 << 19);
    this.rising = this.target > this.level;
    this.inc = PENV_RATE[this.r[ix]] * PENV_UNIT;
  }
  sample() {
    if (this.ix < 3 || (this.ix < 4 && !this.down)) {
      if (this.rising) { this.level += this.inc; if (this.level >= this.target) { this.level = this.target; this.advance(this.ix + 1); } }
      else { this.level -= this.inc; if (this.level <= this.target) { this.level = this.target; this.advance(this.ix + 1); } }
    }
    return this.level;
  }
  key(d) { if (this.down !== d) { this.down = d; this.advance(d ? 0 : 3); } }
}

// ---- a note (dx7note.cc) and the operators (fm_core.cc, fm_op_kernel.cc) ----
const ALG = [
  [0xc1, 0x11, 0x11, 0x14, 0x01, 0x14], [0x01, 0x11, 0x11, 0x14, 0xc1, 0x14], [0xc1, 0x11, 0x14, 0x01, 0x11, 0x14],
  [0xc1, 0x11, 0x94, 0x01, 0x11, 0x14], [0xc1, 0x14, 0x01, 0x14, 0x01, 0x14], [0xc1, 0x94, 0x01, 0x14, 0x01, 0x14],
  [0xc1, 0x11, 0x05, 0x14, 0x01, 0x14], [0x01, 0x11, 0xc5, 0x14, 0x01, 0x14], [0x01, 0x11, 0x05, 0x14, 0xc1, 0x14],
  [0x01, 0x05, 0x14, 0xc1, 0x11, 0x14], [0xc1, 0x05, 0x14, 0x01, 0x11, 0x14], [0x01, 0x05, 0x05, 0x14, 0xc1, 0x14],
  [0xc1, 0x05, 0x05, 0x14, 0x01, 0x14], [0xc1, 0x05, 0x11, 0x14, 0x01, 0x14], [0x01, 0x05, 0x11, 0x14, 0xc1, 0x14],
  [0xc1, 0x11, 0x02, 0x25, 0x05, 0x14], [0x01, 0x11, 0x02, 0x25, 0xc5, 0x14], [0x01, 0x11, 0x11, 0xc5, 0x05, 0x14],
  [0xc1, 0x14, 0x14, 0x01, 0x11, 0x14], [0x01, 0x05, 0x14, 0xc1, 0x14, 0x14], [0x01, 0x14, 0x14, 0xc1, 0x14, 0x14],
  [0xc1, 0x14, 0x14, 0x14, 0x01, 0x14], [0xc1, 0x14, 0x14, 0x01, 0x14, 0x04], [0xc1, 0x14, 0x14, 0x14, 0x04, 0x04],
  [0xc1, 0x14, 0x14, 0x04, 0x04, 0x04], [0xc1, 0x05, 0x14, 0x01, 0x14, 0x04], [0x01, 0x05, 0x14, 0xc1, 0x14, 0x04],
  [0x04, 0xc1, 0x11, 0x14, 0x01, 0x14], [0xc1, 0x14, 0x01, 0x14, 0x04, 0x04], [0x04, 0xc1, 0x11, 0x14, 0x04, 0x04],
  [0xc1, 0x14, 0x04, 0x04, 0x04, 0x04], [0xc4, 0x04, 0x04, 0x04, 0x04, 0x04]];
const COARSE = [-16777216, 0, 16777216, 26591258, 33554432, 38955489, 43368474, 47099600, 50331648, 53182516, 55732705, 58039632,
  60145690, 62083076, 63876816, 65546747, 67108864, 68576247, 69959732, 71268397, 72509921, 73690858, 74816848, 75892776, 76922906,
  77910978, 78860292, 79773775, 80654032, 81503396, 82323963, 83117622];
const VEL = [0, 70, 86, 97, 106, 114, 121, 126, 132, 138, 142, 148, 152, 156, 160, 163, 166, 170, 173, 174, 178, 181, 184, 186, 189,
  190, 194, 196, 198, 200, 202, 205, 206, 209, 211, 214, 216, 218, 220, 222, 224, 225, 227, 229, 230, 232, 233, 235, 237, 238, 240,
  241, 242, 243, 244, 246, 246, 248, 249, 250, 251, 252, 253, 254];
const EXPSCALE = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 14, 16, 19, 23, 27, 33, 39, 47, 56, 66, 80, 94, 110, 126, 142, 158, 174, 190,
  206, 222, 238, 250];
const PMS = [0, 10, 20, 33, 55, 92, 153, 255], AMS = [0, 4342338, 7171437, 16777216];
function oscFreq(note, mode, coarse, fine, det) {
  if (!mode) {
    let lf = 50857777 + 1398101 * note + DT[15 * (note & 127) + Math.max(0, Math.min(14, det))] + COARSE[coarse & 31];
    if (fine) lf += FINE[Math.min(99, fine)];
    return lf;
  }
  return ((4458616 * ((coarse & 3) * 100 + fine)) >> 3) + (det > 7 ? 13457 * (det - 7) : 0);
}
const scaleCurve = (g, d, c) => { const sc = c === 0 || c === 3 ? (g * d * 329) >> 12 : (EXPSCALE[Math.min(g, 32)] * d * 329) >> 15; return c < 2 ? -sc : sc; };
function scaleLevel(note, bp, ld, rd, lc, rc) {
  const off = note - bp - 17;
  return off >= 0 ? scaleCurve(Math.trunc((off + 1) / 3), rd, rc) : scaleCurve(Math.trunc(-(off - 1) / 3), ld, lc);
}

// one note of voice p (156 values, as dx7.js unpacks it): held for `hold` samples of `n` -> Int16Array, as the FM-1
// plays one voice at LEVEL 128 (Dexed's level)
export function renderNote(p, note, vel = 100, hold = FS, n = 2 * FS) {
  const pitch = Math.max(0, Math.min(127, note + p[144] - 24)), env = [], op = [], base = [], amsens = [], mode = [];
  for (let k = 0; k < 6; k++) {
    const o = 21 * k;
    let ol = scaleout(p[o + 16]) + scaleLevel(pitch, p[o + 8], p[o + 9], p[o + 10], p[o + 11], p[o + 12]);
    ol = Math.min(127, ol) << 5;
    const cv = Math.max(0, Math.min(127, vel));
    ol += ((p[o + 15] * (VEL[cv >> 1] - 239) + 7) >> 3) << 4;
    ol = Math.max(0, ol);
    const x = Math.max(0, Math.min(31, Math.trunc(pitch / 3) - 7));
    env.push(new Env([p[o], p[o + 1], p[o + 2], p[o + 3]], [p[o + 4], p[o + 5], p[o + 6], p[o + 7]], ol, (p[o + 13] * x) >> 3));
    base.push(oscFreq(pitch, p[o + 17], p[o + 18], p[o + 19], p[o + 20]));
    mode.push(p[o + 17]);
    amsens.push(AMS[p[o + 14] & 3]);
    op.push({ levelIn: 0, gainOut: 0, freq: 0, phase: 0 });
  }
  const penv = new PEnv([p[126], p[127], p[128], p[129]], [p[130], p[131], p[132], p[133]]);
  const alg = ALG[p[134] & 31], fbshift = p[135] ? 8 - p[135] : 16, pmd = (p[139] * 165) >> 6, pms = PMS[p[143] & 7], amd = (p[140] * 165) >> 6;
  const opsw = p[155] === undefined ? 0x3f : p[155];
  // the LFO (lfo.cc), triggered by the note
  let lph = 0, ldst = 0, lrand = 0, ldinc, ldinc2;
  const ldelta = LFO[Math.min(99, p[137])], lwave = p[142];
  { let a = 99 - p[138]; if (a === 99) { ldinc = ldinc2 = 0xffffffff; } else { a = (16 + (a & 15)) << (1 + (a >> 4)); ldinc = (LFO_UNIT * a) >>> 0; a &= 0xff80; a = Math.max(0x80, a); ldinc2 = (LFO_UNIT * a) >>> 0; } }
  if (p[141]) lph = 0x7fffffff;
  const out = new Int16Array(n), bus = [new Int32Array(N), new Int32Array(N)], buf = new Int32Array(N), fb = [0, 0];
  for (let b = 0; b * N < n; b++) {
    if (b * N >= hold && env[0].down) { env.forEach((e) => e.key(false)); penv.key(false); }
    lph = (lph + ldelta) >>> 0;
    let lv;
    switch (lwave) {
      case 0: lv = ((lph >>> 7) ^ -(lph >>> 31)) & 0xffffff; break;
      case 1: lv = ((~lph ^ 0x80000000) >>> 0) >>> 8; break;
      case 2: lv = ((lph ^ 0x80000000) >>> 0) >>> 8; break;
      case 3: lv = ((~lph) >>> 7) & (1 << 24); break;
      case 4: lv = (1 << 23) + (sinL(lph >>> 8) >> 1); break;
      case 5: if (lph < ldelta) lrand = (lrand * 179 + 17) & 0xff; lv = ((lrand ^ 0x80) + 1) << 16; break;
      default: lv = 1 << 23;
    }
    let ld;
    { const dl = ldst < 0x80000000 ? ldinc : ldinc2, d = ldst + dl; if (d > 0xffffffff) ld = 1 << 24; else { ldst = d; ld = d < 0x80000000 ? 0 : (Math.floor(d / 128) & 0xffffff); } }
    // pitch and amp mod (dx7note.cc compute)
    const pmdq = (pmd * ld) >>> 0, sens = pms * (lv - (1 << 23));
    let pmod = Math.abs(mulShr(pmdq, sens, 39));
    pmod = penv.sample() + (sens < 0 ? -pmod : pmod);
    const lv2 = (1 << 24) - lv;
    let amod = Number((BigInt(amd) * BigInt(ld)) >> 8n);
    amod = Number((BigInt(amod) * BigInt(lv2)) >> 24n);
    for (let k = 0; k < 6; k++) {
      if (!(opsw & (1 << k))) { env[k].sample(); op[k].levelIn = 0; continue; }
      op[k].freq = freqL(mode[k] ? base[k] : base[k] + pmod);
      let level = env[k].sample();
      if (amsens[k]) {
        const sa = Number((BigInt(amod) * BigInt(amsens[k])) >> 24n), pt = Math.trunc(Math.exp(Math.fround(sa / 262144) * 0.07 + 12.2));
        level -= Number((BigInt(level >>> 0) * (BigInt(pt) << 4n)) >> 28n);
      }
      op[k].levelIn = level;
    }
    // the operators
    buf.fill(0);
    const has = [true, false, false];
    for (let k = 0; k < 6; k++) {
      const fl = alg[k], o = op[k], inbus = (fl >> 4) & 3, outbus = fl & 3, outp = outbus === 0 ? buf : bus[outbus - 1];
      let add = (fl & 4) !== 0;
      const g1 = o.gainOut, g2 = exp2L(o.levelIn - (14 << 24));
      o.gainOut = g2;
      if (g1 >= 1120 || g2 >= 1120) {
        if (!has[outbus]) add = false;
        const dg = (g2 - g1 + 32) >> 6;
        let g = g1, ph = o.phase;
        if (inbus === 0 || !has[inbus]) {
          if ((fl & 0xc0) === 0xc0 && fbshift < 16) {
            let y0 = fb[0], y = fb[1];
            for (let i = 0; i < N; i++) {
              g += dg;
              const sfb = (y0 + y) >> (fbshift + 1);
              y0 = y;
              y = Math.floor(sinL(i32(ph + sfb)) * g / 16777216);
              outp[i] = add ? outp[i] + y : y;
              ph = i32(ph + o.freq);
            }
            fb[0] = y0; fb[1] = y;
          } else {
            for (let i = 0; i < N; i++) { g += dg; const y = Math.floor(sinL(ph) * g / 16777216); outp[i] = add ? outp[i] + y : y; ph = i32(ph + o.freq); }
          }
        } else {
          const inp = bus[inbus - 1];
          for (let i = 0; i < N; i++) { g += dg; const y = Math.floor(sinL(i32(ph + inp[i])) * g / 16777216); outp[i] = add ? outp[i] + y : y; ph = i32(ph + o.freq); }
        }
        has[outbus] = true;
      } else if (!add) has[outbus] = false;
      o.phase = i32(o.phase + (o.freq << 6));
    }
    for (let i = 0; i < N && b * N + i < n; i++) {
      const x = buf[i] >> 4;
      out[b * N + i] = x < -(1 << 24) ? -32768 : x >= (1 << 24) ? 32767 : x >> 9;
    }
  }
  return out;
}

// play it now (Web Audio): a note held `holdS` seconds, at -6 dB
let ctx = null, cur = null;
export function audition(p, note = 60, holdS = 1.2, totalS = 2.6) {
  const s = renderNote(p, note, 100, Math.round(holdS * FS), Math.round(totalS * FS));
  ctx = ctx || new AudioContext();
  if (cur) { try { cur.stop(); } catch (_) { /* (ended) */ } }
  const ab = ctx.createBuffer(1, s.length, FS), d = ab.getChannelData(0);
  for (let i = 0; i < s.length; i++) d[i] = s[i] / 65536;
  const src = ctx.createBufferSource();
  src.buffer = ab;
  src.connect(ctx.destination);
  src.start();
  cur = src;
  return s;
}
