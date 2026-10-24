// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT web editor: the FM-1 side, kept apart from the page so it can be tested in Node.
// Copyright (C) 2026 Jamie (TwistedGenetics).
//
// The commands are src/app/dn_editor.c's (60-69) on Felucca's editor frame (F0 7D 46 4C cmd args F7), plus INFO (1).
// Numbers go as 7-bit "digits", low first; signed parameter values + 8192 in two digits; bytes 7 to 8 (a byte of the
// seven top bits, then the seven bytes' low bits).

export const HDR = [0x7D, 0x46, 0x4C];
export const CMD = { INFO: 1, STATE: 60, PARAMS: 61, PARAM_SET: 62, PAT_READ: 63, PAT_WRITE: 64, PAT_COMMIT: 65,
  TRANSPORT: 66, SAVE: 67, PRESET: 68, NAMES: 69 };

export const num = (b, at, digits) => { let v = 0; for (let i = digits - 1; i >= 0; i--) v = v * 128 + b[at + i]; return v; };
export const digits = (v, n) => { const o = []; for (let i = 0; i < n; i++) { o.push(v & 127); v = Math.floor(v / 128); } return o; };

export function pack7(bytes) {
  const out = [];
  for (let i = 0; i < bytes.length; i += 7) {
    let m = 0;
    for (let j = 0; j < 7 && i + j < bytes.length; j++) m |= (bytes[i + j] >> 7) << j;
    out.push(m);
    for (let j = 0; j < 7 && i + j < bytes.length; j++) out.push(bytes[i + j] & 127);
  }
  return out;
}
export function unpack7(a) {
  const out = [];
  for (let i = 0; i < a.length;) {
    const m = a[i++];
    for (let j = 0; j < 7 && i < a.length; j++) out.push(a[i++] | ((m >> j) & 1) << 7);
  }
  return out;
}

// ---- what the FM-1 has -------------------------------------------------------------------------------------------
export function parseState(r) {
  return { rev: num(r, 0, 5), bpm: num(r, 5, 2), playing: r[7], rec: r[8], preset: r[9], trk: r[10], swing: r[11],
    dirty: r[12], slices: [r[13], r[14]], presets: r[15], loops: r[16], patSize: num(r, 17, 2), nparams: r[19],
    step: num(r, 20, 2) };
}

const OSC = ["2SAW", "3SAW", "5SAW", "7SAW", "SAWSQ", "2SIN"];
const BEAT = ["OFF", "1/1", "1/2", "1/4", "1/8", "1/16", "1/4T", "1/8T"];
// the order of PARAMS (dn_editor.c DN_ED_PARAM): REESE (reese.c RP_*), BPM, BREAKS A, BREAKS B
const R = (name, min, max, group, text) => ({ name, min, max, group, text });
export const REESE = [
  R("OSC", 0, 5, "OSC", (v) => OSC[v]), R("DTN", 0, 127, "OSC", (v) => `${Math.floor(v * v * 100 / (127 * 127))}c`),
  R("BEAT", 0, 7, "OSC", (v) => BEAT[v]), R("SUB", 0, 127, "OSC"),
  R("CUT", 0, 127, "FILTER"), R("RES", 0, 127, "FILTER"), R("DRV", 0, 127, "FILTER"), R("NTCH", 0, 127, "FILTER"),
  R("ATK", 0, 127, "AMP"), R("DEC", 0, 127, "AMP"), R("SUS", 0, 127, "AMP"), R("REL", 0, 127, "AMP"),
  R("FENV", -64, 63, "MOD"), R("GLIDE", 0, 127, "MOD"), R("LRATE", 0, 127, "MOD"), R("LFLT", -64, 63, "MOD"),
  R("LEVEL", 0, 127, "OUT"), R("TRANS", -36, 36, "OUT", (v) => (v > 0 ? `+${v}` : `${v}`)),
];
export const BPM_INDEX = REESE.length;
const BRK = (names) => [
  R("LOOP", 0, 127, "", (v) => names[v] || `#${v + 1}`), R("PITCH", -24, 24, "", (v) => (v > 0 ? `+${v}` : `${v}`)),
  R("DECAY", 0, 127, "", (v) => (v >= 127 ? "HOLD" : `${v}`)), R("CRUSH", 0, 127, ""),
  R("CHOKE", 0, 4, "", (v) => (v ? `${v}` : "OFF")), R("SLICES", 0, 2, "", (v) => ["GRID", "AUTO", "MANUAL"][v]),
  R("STRETCH", 0, 2, "", (v) => ["REPITCH", "SLICE", "STRETCH"][v]), R("LEVEL", 0, 127, ""),
];
export const brkParams = (loopNames) => BRK(loopNames);
export const brkIndex = (k, i) => REESE.length + 1 + 8 * k + i;   // BREAKS k (0 A, 1 B), its value i

export function parseParams(r) {
  const n = r[0], v = [];
  for (let i = 0; i < n; i++) v.push(num(r, 1 + 2 * i, 2) - 8192);
  return v;
}
export function parseNames(r) {
  const count = r[0], from = r[1], names = [];
  let cur = [];
  for (let i = 2; i < r.length; i++) {
    if (r[i] === 0) { names.push(String.fromCharCode(...cur)); cur = []; } else cur.push(r[i]);
  }
  return { count, from, names };
}

// ---- the pattern (dn_seq.h dn_pattern_t, little-endian) ------------------------------------------------------------
export const TRACKS = 8, STEPS = 64, LEN_STEP = 4, LEN_MAX = 255;
export const ST_ON = 1, ST_SLIDE = 2, ST_ACC = 4;
const TR_BYTES = STEPS * 4 + 4, PAT_BYTES = 8 + TRACKS * TR_BYTES;
export const KIND = (t) => (t === 0 ? "REESE" : t === 1 || t === 2 ? "BREAKS" : "MIDI");

export function parsePattern(b) {
  if (b.length < PAT_BYTES) throw new Error("short pattern");
  const p = { magic: (b[0] | b[1] << 8 | b[2] << 16 | b[3] << 24) >>> 0, swing: b[4], pad: [b[5], b[6], b[7]], tracks: [] };
  for (let t = 0; t < TRACKS; t++) {
    const o = 8 + t * TR_BYTES, steps = [];
    for (let i = 0; i < STEPS; i++)
      steps.push({ note: b[o + 4 * i], flags: b[o + 4 * i + 1], len: b[o + 4 * i + 2], vel: b[o + 4 * i + 3] });
    const q = o + STEPS * 4;
    p.tracks.push({ steps, len: b[q], mute: b[q + 1], chan: b[q + 2], lastNote: b[q + 3] });
  }
  return p;
}
export function serializePattern(p) {
  const b = new Uint8Array(PAT_BYTES);
  b[0] = p.magic & 255; b[1] = (p.magic >>> 8) & 255; b[2] = (p.magic >>> 16) & 255; b[3] = (p.magic >>> 24) & 255;
  b[4] = p.swing;
  for (let t = 0; t < TRACKS; t++) {
    const o = 8 + t * TR_BYTES, tr = p.tracks[t];
    tr.steps.forEach((s, i) => { b.set([s.note, s.flags, s.len, s.vel], o + 4 * i); });
    b.set([tr.len, tr.mute, tr.chan, tr.lastNote], o + STEPS * 4);
  }
  return b;
}
// where a note on step i stops, in steps (exclusive): its length, cut by the next note on the track (dn_note_end)
export function noteEnd(tr, i) {
  const span = Math.ceil(tr.steps[i].len / LEN_STEP);
  for (let j = 1; j < span; j++) if (tr.steps[(i + j) % tr.len].flags & ST_ON) return i + j;
  return i + span;
}
export function lenText(t) {
  const w = Math.floor(t / 4), f = ["", "¼", "½", "¾"][t % 4];
  return w ? `${w}${f}` : f;
}
export function noteName(n) {
  return ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"][((n % 12) + 12) % 12] + (Math.floor(n / 12) - 1);
}

// ---- the link --------------------------------------------------------------------------------------------------
// one request at a time; a reply is the next editor frame with the same command
export class Link {
  constructor(input, output) {
    this.input = input;
    this.output = output;
    this.wait = null;
    this.queue = Promise.resolve();
    input.onmidimessage = (ev) => {
      const d = ev.data;
      if (d[0] !== 0xF0 || d[1] !== HDR[0] || d[2] !== HDR[1] || d[3] !== HDR[2] || !this.wait || d[4] !== this.wait.cmd) return;
      const w = this.wait;
      this.wait = null;
      clearTimeout(w.timer);
      w.resolve([...d.slice(5, d.length - 1)]);
    };
  }
  req(cmd, args = [], ms = 2000) {
    const run = () => new Promise((resolve, reject) => {
      this.wait = { cmd, resolve, timer: setTimeout(() => { this.wait = null; reject(new Error("The FM-1 stopped answering.")); }, ms) };
      this.output.send([0xF0, ...HDR, cmd, ...args, 0xF7]);
    });
    const p = this.queue.then(run, run);
    this.queue = p.catch(() => {});
    return p;
  }
  async state() { return parseState(await this.req(CMD.STATE)); }
  async params() { return parseParams(await this.req(CMD.PARAMS)); }
  async setParam(i, v) { return (await this.req(CMD.PARAM_SET, [i, ...digits(v + 8192, 2)]))[0]; }
  async names(kind) {
    let out = [], from = 0, count = 1;
    while (from < count) {
      const r = parseNames(await this.req(CMD.NAMES, [kind, from]));
      count = r.count;
      if (!r.names.length) break;
      out = out.concat(r.names);
      from += r.names.length;
    }
    return out;
  }
  async readPattern(size) {
    const b = [];
    while (b.length < size) {
      const off = b.length, r = await this.req(CMD.PAT_READ, [...digits(off, 2), 200]);
      const got = unpack7(r.slice(2));
      if (!got.length) throw new Error("no pattern data");
      b.push(...got.slice(0, size - off));
    }
    return parsePattern(b);
  }
  async writePattern(p) {
    const b = serializePattern(p);
    for (let off = 0; off < b.length; off += 200) {
      const r = await this.req(CMD.PAT_WRITE, [...digits(off, 2), ...pack7(b.subarray(off, off + 200))]);
      if (r[0]) throw new Error("The FM-1 didn't take the pattern.");
    }
    if ((await this.req(CMD.PAT_COMMIT))[0]) throw new Error("The FM-1 refused the pattern.");
  }
  async transport(on) { return (await this.req(CMD.TRANSPORT, [on ? 1 : 0]))[0]; }
  async save() { return (await this.req(CMD.SAVE, [], 8000))[0]; }
  async preset(i) { return (await this.req(CMD.PRESET, [i]))[0]; }
  async info() { return String.fromCharCode(...(await this.req(CMD.INFO, [], 800)).filter((c, i, a) => i < a.indexOf(0))); }
}

// find the FM-1 running DREADNOUGHT 0.7+ among the MIDI ports (it answers INFO with "DREADNOUGHT ...")
export async function connect(midi) {
  const outs = [...midi.outputs.values()];
  for (const input of midi.inputs.values()) {
    if (input.state === "disconnected" || !/dreadnought|fm-1|fm1/i.test(input.name || "")) continue;
    const output = outs.find((o) => o.name === input.name) || outs.find((o) => /dreadnought|fm-1|fm1/i.test(o.name || ""));
    if (!output) continue;
    if (input.open) await input.open();
    if (output.open) await output.open();
    const link = new Link(input, output);
    try {
      const info = await link.info();
      if (/^DREADNOUGHT/i.test(info)) { link.name = info; return link; }
    } catch (_) { /* not answering: the next */ }
  }
  throw new Error("No FM-1 running DREADNOUGHT 0.7 or later found. Connect it by USB and close other apps that use its MIDI port.");
}
