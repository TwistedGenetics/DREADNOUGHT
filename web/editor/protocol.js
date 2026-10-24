// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT web editor: the FM-1 side, kept apart from the page so it can be tested in Node.
// Copyright (C) 2026 Jamie (TwistedGenetics).
//
// The commands are src/app/dn_editor.c's (60-69) on Felucca's editor frame (F0 7D 46 4C cmd args F7), plus INFO (1).
// Numbers go as 7-bit "digits", low first; signed parameter values + 8192 in two digits; bytes 7 to 8 (a byte of the
// seven top bits, then the seven bytes' low bits).

export const HDR = [0x7D, 0x46, 0x4C];
export const CMD = { INFO: 1, LFO_PTS_GET: 70, LFO_PTS_SET: 71, LFO_NOW: 72, PATS: 73, PAT_SEL: 74, STATE: 60, PARAMS: 61, PARAM_SET: 62, PAT_READ: 63, PAT_WRITE: 64, PAT_COMMIT: 65,
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
// GLIDE is the slide time (a SLIDE step, or a note that reaches past the next one): as the FM-1 shows it
export function glideText(v) {
  if (v <= 0) return "OFF";
  let ms = 1;
  for (let i = 0; i < v && i < 127; i++) ms *= 1.075254;
  return ms < 1000 ? `${Math.floor(ms + 0.5)}ms` : `${(Math.floor(ms / 100 + 0.5) / 10).toFixed(1)}s`;
}
export const REESE = [
  R("OSC", 0, 5, "OSC", (v) => OSC[v]), R("DTN", 0, 127, "OSC", (v) => `${Math.floor(v * v * 100 / (127 * 127))}c`),
  R("BEAT", 0, 7, "OSC", (v) => BEAT[v]), R("SUB", 0, 127, "OSC"),
  R("CUT", 0, 127, "FILTER"), R("RES", 0, 127, "FILTER"), R("DRV", 0, 127, "FILTER"), R("NTCH", 0, 127, "FILTER"),
  R("ATK", 0, 127, "AMP"), R("DEC", 0, 127, "AMP"), R("SUS", 0, 127, "AMP"), R("REL", 0, 127, "AMP"),
  R("FENV", -64, 63, "MOD"), R("GLIDE", 0, 127, "MOD", glideText), R("LRATE", 0, 127, "MOD"), R("LFLT", -64, 63, "MOD"),
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
// REESE's own filter LFO of 0.8 (LRATE, LFLT): the FM-1 moves it to LFO 1, so the page doesn't show them
export const HIDDEN = new Set(["LRATE", "LFLT"]);

// ---- the LFOs (0.9): 8 values each after BREAKS B; the DRAW points and where they are by their own commands -----
export const NLFO = 4, LFO_PTS = 32;
export const LFO_SHAPES = ["SINE", "TRI", "SAW UP", "SAW DN", "SQUARE", "S&H", "SMOOTH", "DRAW"];
export const LFO_DIVS = ["8 BARS", "4 BARS", "2 BARS", "1 BAR", "1/2", "1/4D", "1/2T", "1/4", "1/8D", "1/4T", "1/8",
  "1/16D", "1/8T", "1/16", "1/16T", "1/32"];
export const LFO_DESTS = ["OFF", "CUTOFF", "RES", "DRIVE", "NOTCH", "DETUNE", "PITCH", "SUB", "LEVEL", "F.ENV",
  "A PITCH", "A DECAY", "A CRUSH", "A LEVEL", "B PITCH", "B DECAY", "B CRUSH", "B LEVEL",
  "L1 DEPTH", "L2 DEPTH", "L3 DEPTH", "L4 DEPTH"];
export const LFO_MODES = ["FREE", "RETRIG", "1-SHOT"];
export function lfoHz(v) { return 0.05 * Math.pow(400, v / 127); }
// as the FM-1's text (dn_app.c dn_lfo_text); RATE reads by SYNC, given as `sync`
export const lfoParams = () => [
  R("SHAPE", 0, 7, "", (v) => LFO_SHAPES[v]), R("SYNC", 0, 1, "", (v) => (v ? "SYNC" : "FREE")),
  R("RATE", 0, 127, "", (v, sync) => (sync ? LFO_DIVS[Math.min(15, v)] : `${lfoHz(v) < 10 ? lfoHz(v).toFixed(2) : lfoHz(v).toFixed(1)}Hz`)),
  R("DEST", 0, LFO_DESTS.length - 1, "", (v) => LFO_DESTS[v]), R("DEPTH", -64, 63, "", (v) => (v >= 0 ? `+${v}` : `${v}`)),
  R("PHASE", 0, 127, "", (v) => `${Math.floor(v * 360 / 128)}°`), R("MODE", 0, 2, "", (v) => LFO_MODES[v]),
  R("SMOOTH", 0, 127, ""),
];
export const lfoIndex = (k, i) => REESE.length + 1 + 16 + 8 * k + i;   // LFO k, its value i (lfoParams' order)
export const NPARAM_08 = REESE.length + 1 + 16, NPARAM = NPARAM_08 + NLFO * 8;
export function parseLfoPts(r) {
  const pts = [];
  for (let i = 0; i < LFO_PTS; i++) pts.push(num(r, 2 + 2 * i, 2) - 100);
  return { lfo: r[0], npts: r[1], pts };
}
export const lfoPtsArgs = (l, npts, pts) => [l, npts, ...pts.flatMap((v) => digits(Math.max(-100, Math.min(100, Math.round(v))) + 100, 2))];
export function parseLfoNow(r) {
  const out = [];
  for (let k = 0; k < NLFO; k++) out.push({ phase: num(r, 4 * k, 2) / 16384, value: (num(r, 4 * k + 2, 2) - 8192) / 8191 });
  return out;
}
// the shape at a phase (0..1), as dn_lfo.c draws it (the page's picture of it)
export function lfoShapeAt(p, x) {
  x = ((x + p.phase / 128) % 1 + 1) % 1;
  switch (p.shape) {
    case 0: return Math.sin(2 * Math.PI * x);
    case 1: return x < 0.5 ? -1 + 4 * x : 3 - 4 * x;
    case 2: return 2 * x - 1;
    case 3: return 1 - 2 * x;
    case 4: return x < 0.5 ? 1 : -1;
    case 5: case 6: return 0;
    default: {
      const n = p.npts === 32 ? 32 : 16, pos = x * n, i = Math.floor(pos) % n, f = pos - Math.floor(pos);
      const a = p.pts[i] / 100, b = p.pts[(i + 1) % n] / 100, w = p.smooth / 127, edge = 1 - w;
      return !w || f <= edge ? a : a + (b - a) * (f - edge) / w;
    }
  }
}

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
export const KIND = (t) => (t === 0 ? "REESE" : t === 1 || t === 2 ? "BREAKS" : "MIDI");
export const ST_RTRG = 8;
// trig conditions, as the FM-1 numbers them (dn_seq.h DN_CO_*: Digitakt's list)
export const CO_PCT = [1, 2, 4, 6, 9, 13, 19, 25, 33, 41, 50, 59, 67, 75, 81, 87, 91, 94, 96, 98, 99, 100];
export const COND_NAMES = (() => {
  const n = ["ALWAYS", ...CO_PCT.map((p) => `${p}%`), "FILL", "!FILL", "PRE", "!PRE", "NEI", "!NEI", "1ST", "!1ST", "LST", "!LST"];
  const ab = [];
  for (let b = 2; b <= 8; b++) for (let a = 1; a <= b; a++) ab.push(`${a}:${b}`);
  return [...n, ...ab, ...ab.map((x) => `!${x}`)];
})();
export const RTIM_DEN = [1, 2, 3, 4, 5, 6, 8, 10, 12, 16, 20, 24, 32, 40, 48, 64, 80];
export const NPAT = 8;
export function parsePats(r) {
  const tracks = [];
  for (let t = 0; t < TRACKS; t++) {
    const o = 4 * t;
    tracks.push({ cur: r[o], queued: r[o + 1] === 127 ? -1 : r[o + 1], used: r[o + 2] | r[o + 3] << 7 });
  }
  return { tracks, notes: num(r, 32, 2), room: num(r, 34, 2) };
}
// 0.11's steps are 8 bytes {note, flags, len, vel, cond, rtim, rlen, rvel}; 0.7-0.10's were 4 (the FM-1 says which by
// the pattern's size in STATE: 4136 or 2088)
const SB = (n) => (n >= 8 + TRACKS * (STEPS * 8 + 4) ? 8 : 4);
export const PAT_BYTES = 8 + TRACKS * (STEPS * 8 + 4), PAT_BYTES_OLD = 8 + TRACKS * (STEPS * 4 + 4);
export const MAGIC = 0x33514E44, MAGIC_OLD = 0x32514E44;
export const newStep = () => ({ note: 53, flags: 0, len: 3, vel: 100, cond: 0, rtim: 12, rlen: 0, rvel: 0 });

export function parsePattern(b) {
  if (b.length < PAT_BYTES_OLD) throw new Error("short pattern");
  const sb = SB(b.length), TB = STEPS * sb + 4;
  const p = { magic: (b[0] | b[1] << 8 | b[2] << 16 | b[3] << 24) >>> 0, swing: b[4], pad: [b[5], b[6], b[7]], tracks: [], stepBytes: sb };
  for (let t = 0; t < TRACKS; t++) {
    const o = 8 + t * TB, steps = [];
    for (let i = 0; i < STEPS; i++) {
      const q = o + sb * i;
      steps.push(sb === 8
        ? { note: b[q], flags: b[q + 1], len: b[q + 2], vel: b[q + 3], cond: b[q + 4], rtim: b[q + 5], rlen: b[q + 6], rvel: (b[q + 7] << 24) >> 24 }
        : { ...newStep(), note: b[q], flags: b[q + 1], len: b[q + 2], vel: b[q + 3] });
    }
    const q = o + STEPS * sb;
    p.tracks.push({ steps, len: b[q], mute: b[q + 1], chan: b[q + 2], lastNote: b[q + 3] });
  }
  return p;
}
export function serializePattern(p) {
  const sb = p.stepBytes || 8, TB = STEPS * sb + 4, b = new Uint8Array(8 + TRACKS * TB);
  b[0] = p.magic & 255; b[1] = (p.magic >>> 8) & 255; b[2] = (p.magic >>> 16) & 255; b[3] = (p.magic >>> 24) & 255;
  b[4] = p.swing;
  if (p.pad) b.set(p.pad.slice(0, 3).map((x) => x & 255), 5);   // pad[0]: OVERLAP CUT, a bit a track (0.8)
  for (let t = 0; t < TRACKS; t++) {
    const o = 8 + t * TB, tr = p.tracks[t];
    tr.steps.forEach((s, i) => {
      b.set(sb === 8 ? [s.note, s.flags & 15, s.len, s.vel, s.cond || 0, s.rtim ?? 12, s.rlen || 0, (s.rvel || 0) & 255]
        : [s.note, s.flags & 7, s.len, s.vel], o + sb * i);
    });
    b.set([tr.len, tr.mute, tr.chan, tr.lastNote], o + STEPS * sb);
  }
  return b;
}
// where a note on step i stops, in steps (exclusive): its length, cut by the next note on the track (dn_note_end)
// OVERLAP (pattern pad[0], a bit a track): set = CUT. Overlaps slide on REESE and the MIDI tracks, never on the breaks
export const overlapCut = (pat, t) => ((pat.pad[0] >> t) & 1) === 1;
export function setOverlapCut(pat, t, cut) { pat.pad[0] = cut ? pat.pad[0] | (1 << t) : pat.pad[0] & ~(1 << t); }
// does the note on step i slide in because the one before it reaches past its start (dn_seq.c fire_step)?
export function overlapSlide(pat, t, i) {
  const tr = pat.tracks[t];
  if (KIND(t) === "BREAKS" || overlapCut(pat, t)) return false;
  for (let j = i - 1; j >= 0; j--) {
    const st = tr.steps[j];
    if (st.flags & ST_ON) return st.len > (i - j) * LEN_STEP;
  }
  return false;
}
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
    this.tap = null;                                  // (the Diagnostics panel: every frame both ways)
    input.onmidimessage = (ev) => {
      const d = ev.data;
      if (this.tap && d[0] === 0xF0) this.tap("<", d);
      if (d[0] !== 0xF0 || d[1] !== HDR[0] || d[2] !== HDR[1] || d[3] !== HDR[2] || !this.wait || d[4] !== this.wait.cmd) return;
      const w = this.wait;
      this.wait = null;
      clearTimeout(w.timer);
      w.resolve([...d.slice(5, d.length - 1)]);
    };
  }
  req(cmd, args = [], ms = 2000) {
    const run = () => new Promise((resolve, reject) => {
      this.wait = { cmd, resolve, timer: setTimeout(() => { this.wait = null; if (this.tap) this.tap("!", [cmd]); reject(new Error("The FM-1 stopped answering.")); }, ms) };
      const f = [0xF0, ...HDR, cmd, ...args, 0xF7];
      if (this.tap) this.tap(">", f);
      this.output.send(f);
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
      const off = b.length, r = await this.req(CMD.PAT_READ, [...digits(off, 2), 112]);   // (a count: 7 bits, <= 127)
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
  async lfoPts(l) { return parseLfoPts(await this.req(CMD.LFO_PTS_GET, [l])); }
  async setLfoPts(l, npts, pts) { return (await this.req(CMD.LFO_PTS_SET, lfoPtsArgs(l, npts, pts)))[0]; }
  async lfoNow() { return parseLfoNow(await this.req(CMD.LFO_NOW)); }
  async pats() { return parsePats(await this.req(CMD.PATS)); }
  async patSel(t, n) { return (await this.req(CMD.PAT_SEL, [t, n]))[0]; }
  async save() { return (await this.req(CMD.SAVE, [], 8000))[0]; }
  async preset(i) { return (await this.req(CMD.PRESET, [i]))[0]; }
  async info() { return String.fromCharCode(...(await this.req(CMD.INFO, [], 1500)).filter((c, i, a) => i < a.indexOf(0))); }
}

// find the FM-1 running DREADNOUGHT 0.7+ among the MIDI ports (it answers INFO with "DREADNOUGHT ..."). The port's
// name differs by OS and by what the OS remembered from earlier firmware (DREADNOUGHT FM-1, X0X FM-1, FM-1, ...), so
// the likely names are asked first, then every other port (INFO uses the non-commercial ID 7D: harmless elsewhere).
// INFO is asked three times: the first frame after a port opens can be lost.
const LIKELY = /dreadnought|fm-1|fm1|x0x|felucca|m-vave/i;
export async function connect(midi, log = () => {}, prefer = "", tap = null) {
  const outs = [...midi.outputs.values()], ins = [...midi.inputs.values()].filter((i) => i.state !== "disconnected");
  const order = [...ins.filter((i) => i.name === prefer), ...ins.filter((i) => i.name !== prefer && LIKELY.test(i.name || "")),
    ...ins.filter((i) => i.name !== prefer && !LIKELY.test(i.name || ""))];   // (the port picked on the page first)
  log(`MIDI in: ${ins.map((i) => i.name).join(", ") || "none"}; out: ${outs.map((o) => o.name).join(", ") || "none"}`);
  for (const input of order) {
    const output = outs.find((o) => o.name === input.name) ||
      (LIKELY.test(input.name || "") && outs.find((o) => LIKELY.test(o.name || "")));
    if (!output) { log(`${input.name}: no output port with that name`); continue; }
    try {
      if (input.open) await input.open();
      if (output.open) await output.open();
    } catch (e) { log(`${input.name}: can't open (${e.message}): another app may have it`); continue; }
    const link = new Link(input, output);
    link.tap = tap;
    log(`trying ${input.name} -> ${output.name}`);
    for (let k = 0; k < 3; k++) {
      try {
        const info = await link.info();
        log(`${input.name}: answered "${info}"`);
        if (/^DREADNOUGHT/i.test(info)) { link.name = info; return link; }
        break;
      } catch (_) { log(`${input.name}: no answer to INFO (try ${k + 1} of 3)`); }
    }
    input.onmidimessage = null;
  }
  const names = ins.map((i) => i.name).join(", ");
  throw new Error(`No FM-1 running DREADNOUGHT 0.7 or later answered. MIDI ports seen: ${names || "none"}. ` +
    "Connect it by USB, close other apps that use its MIDI port (Ableton, the breaks page or the flasher in another tab), and try again.");
}
