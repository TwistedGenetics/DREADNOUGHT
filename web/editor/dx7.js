// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT web editor: DX7 voices for FM (0.25). Copyright (C) 2026 Jamie (TwistedGenetics).
//
// Reads DX7 SysEx: 32-voice banks (F0 43 0n 09 20 00, 4096 bytes of packed voices, checksum, F7: 4104 bytes), single
// voices (F0 43 0n 00 01 1B, 155 bytes, checksum, F7), files with several of them, and bare 4096-byte banks. Packs and
// unpacks voices as the FM-1 does (src/dsp/dn_fm.c: dn_fm_pack / dn_fm_unpack), and names each value as its pages do.
// The project's voices go to the FM-1 in a zone of a sample slot: "DNFMBNK", how many, then each packed voice.

export const PACKED = 128, PATCH = 156, NAME = 145, OPSW = 155;
export const ZONE_MAGIC = [..."DNFMBNK"].map((c) => c.charCodeAt(0));

const OT = [99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 3, 3, 7, 3, 7, 99, 1, 31, 99, 14];
const VT = [99, 99, 99, 99, 99, 99, 99, 99, 31, 7, 1, 99, 99, 99, 99, 1, 5, 7, 48];
export const vmax = (k) => (k < 126 ? OT[k % 21] : k < NAME ? VT[k - 126] : k === OPSW ? 63 : 127);

export function unpack(b) {
  const r = new Uint8Array(PATCH);
  for (let op = 0; op < 6; op++) {
    const s = op * 17, o = op * 21;
    for (let i = 0; i < 11; i++) r[o + i] = b[s + i] & 0x7f;
    r[o + 11] = b[s + 11] & 3;
    r[o + 12] = (b[s + 11] >> 2) & 3;
    r[o + 13] = b[s + 12] & 7;
    r[o + 14] = b[s + 13] & 3;
    r[o + 15] = (b[s + 13] >> 2) & 7;
    r[o + 16] = b[s + 14] & 0x7f;
    r[o + 17] = b[s + 15] & 1;
    r[o + 18] = (b[s + 15] >> 1) & 0x1f;
    r[o + 19] = b[s + 16] & 0x7f;
    r[o + 20] = (b[s + 12] >> 3) & 0x0f;
  }
  for (let i = 0; i < 8; i++) r[126 + i] = b[102 + i] & 0x7f;
  r[134] = b[110] & 0x1f;
  r[135] = b[111] & 7;
  r[136] = (b[111] >> 3) & 1;
  r[137] = b[112] & 0x7f; r[138] = b[113] & 0x7f; r[139] = b[114] & 0x7f; r[140] = b[115] & 0x7f;
  r[141] = b[116] & 1;
  r[142] = (b[116] >> 1) & 7;
  r[143] = (b[116] >> 4) & 7;
  r[144] = b[117] & 0x7f;
  for (let i = 0; i < 10; i++) r[NAME + i] = b[118 + i] & 0x7f;
  r[OPSW] = 0x3f;
  for (let i = 0; i < NAME; i++) if (r[i] > vmax(i)) r[i] = vmax(i);
  return r;
}
export function pack(r) {
  const b = new Uint8Array(PACKED);
  for (let op = 0; op < 6; op++) {
    const s = op * 17, o = op * 21;
    for (let i = 0; i < 11; i++) b[s + i] = r[o + i];
    b[s + 11] = (r[o + 11] & 3) | (r[o + 12] & 3) << 2;
    b[s + 12] = (r[o + 13] & 7) | (r[o + 20] & 15) << 3;
    b[s + 13] = (r[o + 14] & 3) | (r[o + 15] & 7) << 2;
    b[s + 14] = r[o + 16];
    b[s + 15] = (r[o + 17] & 1) | (r[o + 18] & 31) << 1;
    b[s + 16] = r[o + 19];
  }
  for (let i = 0; i < 8; i++) b[102 + i] = r[126 + i];
  b[110] = r[134] & 31;
  b[111] = (r[135] & 7) | (r[136] & 1) << 3;
  b[112] = r[137]; b[113] = r[138]; b[114] = r[139]; b[115] = r[140];
  b[116] = (r[141] & 1) | (r[142] & 7) << 1 | (r[143] & 7) << 4;
  b[117] = r[144];
  for (let i = 0; i < 10; i++) b[118 + i] = r[NAME + i] & 0x7f;
  return b;
}
const nameOf = (bytes, at) => String.fromCharCode(...bytes.slice(at, at + 10).map((c) => (c >= 32 && c < 127 ? c : 32))).replace(/\s+$/, "");
export const packedName = (b) => nameOf([...b], 118);
const checksum = (d) => (128 - (d.reduce((a, x) => a + x, 0) & 127)) & 127;

// SysEx bytes -> { voices: [{name, packed}], notes: [warnings] }
export function parseSyx(bytes) {
  const b = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes), voices = [], notes = [];
  const addBank = (data) => { for (let i = 0; i < 32; i++) { const p = data.slice(i * 128, i * 128 + 128); voices.push({ name: packedName(p), packed: p }); } };
  if (b.length === 4096 && b[0] !== 0xf0) { addBank(b); return { voices, notes }; }   // (a bare bank)
  for (let i = 0; i < b.length; i++) {
    if (b[i] !== 0xf0) continue;
    const end = b.indexOf(0xf7, i);
    if (end < 0) break;
    const m = b.subarray(i, end + 1);
    i = end;
    if (m[1] !== 0x43) continue;                     // (not Yamaha's)
    if (m[3] === 9 && m.length >= 4104) {
      const data = m.slice(6, 6 + 4096);
      if (checksum([...data]) !== m[6 + 4096]) notes.push("a bank's checksum is wrong (read anyway)");
      addBank(data);
    } else if (m[3] === 0 && m.length >= 163) {
      const v = new Uint8Array(PATCH);
      v.set(m.slice(6, 6 + 155));
      v[OPSW] = 0x3f;
      for (let k = 0; k < NAME; k++) if (v[k] > vmax(k)) v[k] = vmax(k);
      if (checksum([...m.slice(6, 6 + 155)]) !== m[6 + 155]) notes.push("a voice's checksum is wrong (read anyway)");
      const p = pack(v);
      voices.push({ name: packedName(p), packed: p });
    }
  }
  if (!voices.length) throw new Error("No DX7 voices in that file (a 32-voice bank or a single voice, as SysEx).");
  return { voices, notes };
}
// the voices as a zone's data for a sample slot (the FM-1 finds them by the mark), and back
export function zoneBytes(packedList) {
  const out = new Uint8Array(8 + packedList.length * PACKED);
  out.set(ZONE_MAGIC, 0);
  out[7] = packedList.length;
  packedList.forEach((p, i) => out.set(p, 8 + i * PACKED));
  return out;
}
export function zoneVoices(data) {
  if (!data || data.length < 8 || !ZONE_MAGIC.every((c, i) => data[i] === c)) return null;
  const n = data[7], out = [];
  for (let i = 0; i < n && 8 + (i + 1) * PACKED <= data.length; i++) {
    const p = data.slice(8 + i * PACKED, 8 + (i + 1) * PACKED);
    out.push({ name: packedName(p), packed: p });
  }
  return out;
}

// ---- the values, named as the FM-1's pages name them (dn_fm_vname / dn_fm_vtext) ----------------------------------
const ON = ["R1", "R2", "R3", "R4", "L1", "L2", "L3", "L4", "BREAK", "L.DEPTH", "R.DEPTH", "L.CURVE", "R.CURVE",
  "RATE KS", "AMS", "VEL", "LEVEL", "MODE", "COARSE", "FINE", "DETUNE"];
const VN = ["P.R1", "P.R2", "P.R3", "P.R4", "P.L1", "P.L2", "P.L3", "P.L4", "ALGO", "FEEDBACK", "KEY SYNC", "LFO SPD",
  "LFO DLY", "LFO PMD", "LFO AMD", "LFO SYNC", "LFO WAVE", "PMS", "TRANSP"];
export const vname = (k) => (k < 126 ? ON[k % 21] : k < NAME ? VN[k - 126] : k === OPSW ? "OPS ON" : "NAME");
const NN = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const note = (n) => `${NN[n % 12]}${Math.floor(n / 12) - 2}`;
export function vtext(k, v) {
  if (k < 126) {
    const f = k % 21;
    if (f === 11 || f === 12) return ["-LIN", "-EXP", "+EXP", "+LIN"][v & 3];
    if (f === 17) return v ? "FIXED" : "RATIO";
    if (f === 20) return v > 7 ? `+${v - 7}` : `${v - 7}`;
    if (f === 8) return note(v + 21);
    return `${v}`;
  }
  if (k === 134) return `${v + 1}`;
  if (k === 136 || k === 141) return v ? "ON" : "OFF";
  if (k === 142) return ["TRI", "SAW DN", "SAW UP", "SQUARE", "SINE", "S&H"][Math.min(v, 5)];
  if (k === 144) return note(v + 36);
  return `${v}`;
}
// an operator's frequency, as the OP page shows it: a ratio (COARSE 0 is 0.50), or FIXED, Hz
export function opFreq(voice, op) {                  // op: 0 = OP1
  const o = (5 - op) * 21, c = voice[o + 18], f = voice[o + 19];
  if (voice[o + 17]) { const hz = Math.pow(10, (c & 3) + f / 100); return hz >= 1000 ? `${(hz / 1000).toFixed(1)} kHz` : `${Math.round(hz)} Hz`; }
  return `× ${((c || 0.5) * (1 + f / 100)).toFixed(2)}`;
}
// which operators each algorithm sends out (the rest modulate): fm_core.cc's table, bit 2 of each (OP6 first)
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
export const isCarrier = (alg, op) => (ALG[alg & 31][5 - op] & 4) !== 0;   // op: 0 = OP1
export const hasFeedback = (alg, op) => (ALG[alg & 31][5 - op] & 0xc0) === 0xc0;
