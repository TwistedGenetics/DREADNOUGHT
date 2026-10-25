// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT web editor: the FM-1 side, kept apart from the page so it can be tested in Node.
// Copyright (C) 2026 Jamie (TwistedGenetics).
//
// The commands are src/app/dn_editor.c's (60-69) on Felucca's editor frame (F0 7D 46 4C cmd args F7), plus INFO (1).
// Numbers go as 7-bit "digits", low first; signed parameter values + 8192 in two digits; bytes 7 to 8 (a byte of the
// seven top bits, then the seven bytes' low bits).

export const HDR = [0x7D, 0x46, 0x4C];
export const CMD = { INFO: 1, LFO_PTS_GET: 70, LFO_PTS_SET: 71, LFO_NOW: 72, PATS: 73, PAT_SEL: 74, STATE: 60, PARAMS: 61, PARAM_SET: 62, PAT_READ: 63, PAT_WRITE: 64, PAT_COMMIT: 65,
  TRANSPORT: 66, SAVE: 67, PRESET: 68, NAMES: 69, PROJ: 75, PROJ_SET: 76, PROJ_NAME: 77, SUB_PRESET: 78, PARAMS_HI: 79,
  SMP_GET: 80, SMP_SET: 81, SMP_PLAY: 82, FM_GET: 83, FM_SET: 84, FM_LOAD: 85, FM_PLAY: 86,
  UI_STATE: 87, UI_INPUT: 88, CHORDS: 89 };

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
export const NLFO = 4, LFO_PTS = 32;                // (a synth's: REESE's 4, then from 0.15 SUB's own 4)
export const NLFO_ALL = 8;
export const LFO_SHAPES = ["SINE", "TRI", "SAW UP", "SAW DN", "SQUARE", "S&H", "SMOOTH", "DRAW"];
export const LFO_DIVS = ["8 BARS", "4 BARS", "2 BARS", "1 BAR", "1/2", "1/4D", "1/2T", "1/4", "1/8D", "1/4T", "1/8",
  "1/16D", "1/8T", "1/16", "1/16T", "1/32"];
export const LFO_DESTS = ["OFF", "CUTOFF", "RES", "DRIVE", "NOTCH", "DETUNE", "PITCH", "SUB", "LEVEL", "F.ENV",
  "A PITCH", "A DECAY", "A CRUSH", "A LEVEL", "B PITCH", "B DECAY", "B CRUSH", "B LEVEL",
  "L1 DEPTH", "L2 DEPTH", "L3 DEPTH", "L4 DEPTH", "SUB CUT", "SUB RES", "SUB DRIVE", "SUB PITCH", "SUB LEVEL", "SUB PW",
  "SUB ENV", "SUB SUB", "SUB CRUSH", "SUB TONE", "SUB PULSE", "SUB NOISE"];
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
// LFO k (0-3 REESE's, 4-7 SUB's, 0.15), its value i (lfoParams' order). SUB's come after SUB's values.
export const lfoIndex = (k, i) => (k < NLFO ? REESE.length + 1 + 16 + 8 * k + i : REESE.length + 1 + 16 + NLFO * 8 + SUB.length + 8 * (k - NLFO) + i);
export const NPARAM_08 = REESE.length + 1 + 16, NPARAM = NPARAM_08 + NLFO * 8;

// ---- SUB (0.13): the SH-101-style bass, its values after the LFOs (src/dsp/dn_sub.h SP_*) -----------------------
const subCut = (v) => { const hz = 30 * Math.pow(533.33, v / 127); return hz >= 1000 ? `${(hz / 1000).toFixed(1)}k` : `${Math.round(hz)}Hz`; };
const sgn = (v) => (v > 0 ? `+${v}` : `${v}`);
export const SUB = [
  R("SAW", 0, 127, "OSC"), R("PULSE", 0, 127, "OSC"), R("SUB", 0, 127, "OSC"), R("NOISE", 0, 127, "OSC"),
  R("PW", 0, 127, "OSC", (v) => `${50 - Math.floor((v * 45) / 127)}%`), R("PWM", 0, 127, "OSC"), R("PWM RATE", 0, 127, "OSC"),
  R("SUB MODE", 0, 2, "OSC", (v) => ["-1 OCT", "-2 OCT", "-2 25%"][v]),
  R("CUT", 0, 127, "FILTER", subCut), R("RES", 0, 127, "FILTER"), R("ENV", -64, 63, "FILTER", sgn), R("KEY", 0, 127, "FILTER"),
  R("DRIVE", 0, 127, "DIRT"), R("TYPE", 0, 3, "DIRT", (v) => ["SOFT", "HARD", "FOLD", "RECT"][v]),
  R("CRUSH", 0, 127, "DIRT", (v) => (v ? `${v}` : "OFF")), R("TONE", 0, 127, "DIRT"),
  R("ATK", 0, 127, "AMP"), R("DEC", 0, 127, "AMP"), R("SUS", 0, 127, "AMP"), R("REL", 0, 127, "AMP"),
  R("GLIDE", 0, 127, "MOD", (v) => (v ? glideText(v) : "OFF")), R("PORTA", 0, 2, "MOD", (v) => ["OFF", "AUTO", "ON"][v]),
  R("ACCENT", 0, 127, "MOD"), R("VCA", 0, 1, "MOD", (v) => (v ? "GATE" : "ENV")),
  R("LEVEL", 0, 127, "OUT"), R("TRANS", -36, 36, "OUT", sgn), R("SPLIT", 0, 127, "OUT", (v) => (v ? `${40 + Math.floor((v * 260) / 127)}Hz` : "OFF")),
];
export const subIndex = (i) => NPARAM + i;
export const NPARAM_13 = NPARAM + SUB.length;
export const NPARAM_15 = NPARAM_13 + NLFO * 8;     // (0.15: SUB's LFOs)
// 0.19: the values past PARAMS' 7-bit index (PARAMS_HI, 79), after them in the page's list: the filter envelopes,
// REESE's then SUB's (ATK DEC SUS REL)
export const FENV = ["F.ATK", "F.DEC", "F.SUS", "F.REL"].map((n) => R(n, 0, 127, "FILT ENV"));
export const NPARAM_19 = NPARAM_15 + 8;
export const feIndex = (set, k) => NPARAM_15 + 4 * set + k;
export const nLfos = (nparams) => (nparams >= NPARAM_15 ? NLFO_ALL : nparams >= NPARAM ? NLFO : 0);
export function parseLfoPts(r) {
  const pts = [];
  for (let i = 0; i < LFO_PTS; i++) pts.push(num(r, 2 + 2 * i, 2) - 100);
  return { lfo: r[0], npts: r[1], pts };
}
export const lfoPtsArgs = (l, npts, pts) => [l, npts, ...pts.flatMap((v) => digits(Math.max(-100, Math.min(100, Math.round(v))) + 100, 2))];
export function parseLfoNow(r) {
  const out = [];
  for (let k = 0; k < Math.min(NLFO_ALL, r.length >> 2); k++) out.push({ phase: num(r, 4 * k, 2) / 16384, value: (num(r, 4 * k + 2, 2) - 8192) / 8191 });
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
// what each track plays (0.12: the project says; before it, REESE / BREAKS A / B / MIDI x5)
export const ENG_NAMES = ["OFF", "REESE", "BRK A", "BRK B", "MIDI", "SUB", "SAMPLER", "FM"];
export const E = { OFF: 0, REESE: 1, BRKA: 2, BRKB: 3, MIDI: 4, SUB: 5, SMP: 6, FM: 7 };
let ENG = [1, 2, 3, 4, 4, 4, 4, 4];
export function setEngines(e) { ENG = e.slice(0, 8); }
export const engines = () => ENG.slice();
export const KIND = (t) => (ENG[t] === E.REESE ? "REESE" : ENG[t] === E.BRKA || ENG[t] === E.BRKB ? "BREAKS" : ENG[t] === E.MIDI ? "MIDI"
  : ENG[t] === E.SUB ? "SUB" : ENG[t] === E.SMP ? "SAMPLER" : ENG[t] === E.FM ? "FM" : "OFF");
export const brkOf = (t) => (ENG[t] === E.BRKB ? 1 : 0);   // a break track's voice: 0 A, 1 B
export const midiNo = (t) => ENG.slice(0, t).filter((e) => e === E.MIDI).length + 1;   // "MIDI n"
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
      b.set(sb === 8 ? [s.note, s.flags & 255, s.len, s.vel, s.cond || 0, s.rtim ?? 12, s.rlen || 0, (s.rvel || 0) & 255]
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

// ---- the project (0.12): PROJ ---------------------------------------------------------------------------------
// the FX rack's effects (src/dsp/dn_fx.h; 0.16 adds Carnac OS's echoes and reverbs, QUADRA and MASSIVE)
export const FX_NAMES = ["OFF", "DELAY", "TAPE", "BBD", "SPRING", "TOMB", "MOD", "CELESTIAL", "DOLMEN", "TUMULUS", "QUADRA",
  "MASSIVE"];
export const FX_KB = [0, 96, 32, 32, 17, 20, 73, 77, 73, 73, 52, 64];   // memory (MASSIVE: at least; it takes the rest)
export const FX_ARENA_KB = 176;
export const FX_DEFAULTS = [[0, 0, 0, 0, 0, 0, 0], [7, 60, 70, 0, 0, 0, 0], [7, 70, 36, 1, 0, 0, 0], [7, 70, 36, 1, 0, 0, 0],
  [60, 40, 0, 30, 0, 0, 0], [70, 50, 10, 30, 0, 0, 0], [80, 50, 10, 30, 0, 0, 0], [90, 40, 20, 40, 0, 0, 0],
  [64, 30, 0, 30, 0, 0, 0], [90, 70, 20, 30, 0, 0, 0], [0, 70, 7, 40, 40, 64, 0], [1, 89, 96, 108, 120, 50, 54]];
export const FX_DIVS = ["1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4", "1/2T", "1/4D", "1/2"];
export const QUADRA_CFG = ["CHORUS", "REVERB", "5EQ>REV", "FLANGE", "DETUNE", "DLY>REV"];
export const MASSIVE_MODES = ["ROOM", "HALL", "HALO", "SWELL", "ABYSS", "ECHOVRB", "ORBIT", "PRISM", "PONG", "DUB"];
const MDIVS = [...FX_DIVS, "1/2D"];
const sgn64 = (v) => `${v - 64}`;
const onoff = (v) => (v ? "ON" : "OFF");
const div = (v) => FX_DIVS[Math.min(11, v)];
const QN = [["DECAY", "TIME", "FEEDBACK", "MOD", "TONE", "16-BIT"], ["DECAY", "PREDELAY", "-", "-", "TONE", "16-BIT"],
  ["DECAY", "LOW", "LO MID", "MID", "HI MID", "HIGH"], ["DECAY", "TIME", "FEEDBACK", "MOD", "TONE", "16-BIT"],
  ["DECAY", "TIME", "FEEDBACK", "DETUNE", "TONE", "16-BIT"], ["DECAY", "TIME", "FEEDBACK", "REV LVL", "TONE", "16-BIT"]];
// an effect's values as the FM-1 names them (dn_fx_pname / pmax / ptext): [{i (its place), name, max, text}]
export function fxParams(type, p = []) {
  const P = (name, max, text) => ({ name, max, text: text || ((v) => `${v}`) });
  let l = [];
  if (type === 1) l = [P("TIME", 11, div), P("FEEDBACK", 127), P("TONE", 127)];
  else if (type === 2 || type === 3) l = [P("TIME", 11, div), P("FEEDBACK", 127), P("DEGRADE", 127), P("PINGPONG", 1, onoff)];
  else if (type >= 4 && type <= 9)
    l = [P("TIME", 127), P("DAMP", 127), P("PREDELAY", 127, (v) => `${Math.floor((v * 350) / 441)}ms`),
      P("LOW CUT", 127, (v) => (v ? `${30 + Math.floor((v * v * 770) / 16129)}Hz` : "OFF"))];
  else if (type === 10) {
    const c = (p[0] || 0) % 6;
    l = [P("CONFIG", 5, (v) => QUADRA_CFG[v % 6]), ...QN[c].map((n) => (n === "TIME" ? P(n, 11, div) : n === "16-BIT" ? P(n, 1, onoff)
      : /^(TONE|LOW|LO MID|MID|HI MID|HIGH)$/.test(n) ? P(n, 127, sgn64) : n === "-" ? P(n, 0) : P(n, 127)))];
  } else if (type === 11)
    l = [P("MODE", 9, (v) => MASSIVE_MODES[v % 10]),
      P("TIME", 127, (v) => (v < 13 ? MDIVS[v] : `${Math.round(10 * Math.pow(125, (v - 13) / 114))}ms`)),
      P("FEEDBACK", 127), P("WARP", 127), P("DENSITY", 127), P("MOD", 127), P("TONE", 127, sgn64)];
  return l.map((x, i) => ({ i, ...x })).filter((x) => x.name !== "-");
}
export function parseProj(r) {
  let name = "";
  for (let i = 0; i < 12 && r[i]; i++) name += String.fromCharCode(r[i]);
  const eng = r.slice(12, 20), send = [r.slice(20, 28), r.slice(28, 36)], ret = [r[36], r[37]];
  const more = r.length >= 67 ? [r.slice(59, 63), r.slice(63, 67)] : [[0, 0, 0, 0], [0, 0, 0, 0]];   // (0.16)
  const fx = [r.slice(38, 42), r.slice(42, 46)].map((f, k) => ({ type: f[0], p: [...f.slice(1), ...more[k]] }));
  return { name, eng, send, ret, fx, smpUsed: num(r, 46, 3), smpCap: num(r, 49, 3), fxUsed: num(r, 52, 2), fxCap: num(r, 54, 2),
    cpu: r[56], nEng: r[57], nFx: r[58] };
}
// PROJ_SET's fields: an effect's type (i 0) and its values (i 1..7: 1-3 then, from 0.16, 4-7)
export const PF = { eng: (t) => t, sendA: (t) => 8 + t, sendB: (t) => 16 + t, ret: (k) => 24 + k,
  fx: (k, i) => (i < 4 ? 26 + 4 * k + i : 34 + 4 * k + (i - 4)) };

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
  async params() {
    const p = parseParams(await this.req(CMD.PARAMS));
    if (p.length === NPARAM_15 && this.hi !== false) {   // (0.19: the rest, if the FM-1 has them)
      try { p.push(...parseParams(await this.req(CMD.PARAMS_HI, [], 800))); this.hi = true; } catch (_) { this.hi = false; }
    }
    return p;
  }
  async setParam(i, v) {
    if (i >= NPARAM_15) return (await this.req(CMD.PARAMS_HI, [i - NPARAM_15, ...digits(v + 8192, 2)]))[0];
    return (await this.req(CMD.PARAM_SET, [i, ...digits(v + 8192, 2)]))[0];
  }
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
  async smpGet(kind, i) { return parseSmp(await this.req(CMD.SMP_GET, [kind, i])); }
  async smpSet(kind, i, k, v) { return (await this.req(CMD.SMP_SET, [kind, i, k, ...digits(v + 8192, 2)]))[0]; }
  async smpPlay(note, on) { return (await this.req(CMD.SMP_PLAY, [note & 127, on ? 1 : 0]))[0]; }
  async chords() { return parseChords(await this.req(CMD.CHORDS, [])); }   // (0.27+: the FM-1 plays chords)
  async ui() { return parseUi(await this.req(CMD.UI_STATE, [], 800)); }
  async uiInput(kind, id, v = 64) { return (await this.req(CMD.UI_INPUT, [kind, id, v]))[0]; }
  async fmGet() { return parseFm(await this.req(CMD.FM_GET)); }
  async fmSet(k, v) { return (await this.req(CMD.FM_SET, [...digits(k, 2), ...digits(v + 8192, 2)]))[0]; }
  async fmLoad(x) { return (await this.req(CMD.FM_LOAD, typeof x === "number" ? digits(x, 2) : [...x].map((b) => b & 127)))[0]; }
  async fmPlay(note, on) { return (await this.req(CMD.FM_PLAY, [note & 127, on ? 1 : 0]))[0]; }
  async subPreset(i) { return (await this.req(CMD.SUB_PRESET, [i]))[0]; }
  async preset(i) { return (await this.req(CMD.PRESET, [i]))[0]; }
  async proj() { return parseProj(await this.req(CMD.PROJ)); }
  async projSet(f, v) { return (await this.req(CMD.PROJ_SET, [f, ...digits(v, 2)]))[0]; }
  async projName(n) { return (await this.req(CMD.PROJ_NAME, n.split("").map((c) => c.charCodeAt(0) & 127).filter((c) => c >= 32).slice(0, 11)))[0]; }
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

// ---- SAMPLER (0.23): 16 one-shot pads, or a key map of 8 zones (src/dsp/dn_smp.h PD_* / ZN_* / KY_*) ---------------
// SMP_GET kind i: kind 0 pad i, 1 zone i, 2 the keys' values, 3 the mode (0 ONE-SHOT, 1 KEYS). A SAMPLE is a loop's
// place in NAMES kind 1 (the loops BREAKS plays: the slots' loops, in order; 0.29: no built-in ones), -1 none.
const pct = (v) => `${(v / 10).toFixed(1)}%`;
const NN = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
export const smpNote = (v) => `${NN[v % 12]}${Math.floor(v / 12) - 2}`;   // (C3 = 60, as the FM-1 names it)
const open = (v) => (v >= 127 ? "OPEN" : `${v}`);
export const SMP_PAD = [
  R("SAMPLE", -1, 126, "PAD"), R("START", 0, 1000, "PAD", pct), R("END", 0, 1000, "PAD", pct), R("REVERSE", 0, 1, "PAD", (v) => (v ? "ON" : "OFF")),
  R("PITCH", -24, 24, "PITCH", sgn), R("FINE", -50, 50, "PITCH", (v) => `${sgn(v)}c`), R("LEVEL", 0, 127, "OUT"),
  R("PAN", -64, 63, "OUT", (v) => (v ? `${v < 0 ? "L" : "R"}${Math.abs(v)}` : "C")),
  R("ATTACK", 0, 127, "AMP"), R("HOLD", 0, 127, "AMP", (v) => (v >= 127 ? "END" : `${v}`)), R("DECAY", 0, 127, "AMP"),
  R("CHOKE", 0, 4, "OUT", (v) => (v ? `${v}` : "OFF")),
  R("CUT", 0, 127, "FILTER", open), R("RES", 0, 127, "FILTER"), R("DRIVE", 0, 127, "FILTER"), R("CRUSH", 0, 127, "FILTER"),
];
export const SMP_ZONE = [
  R("SAMPLE", -1, 126, "ZONE"), R("ROOT", 0, 127, "ZONE", smpNote), R("LOW", 0, 127, "ZONE", smpNote), R("HIGH", 0, 127, "ZONE", smpNote),
  R("TUNE", -50, 50, "ZONE", (v) => `${sgn(v)}c`), R("LEVEL", 0, 127, "ZONE"), R("START", 0, 1000, "ZONE", pct),
  R("LOOP", 0, 2, "LOOP", (v) => ["OFF", "LOOP", "SUST"][v]), R("L.START", 0, 1000, "LOOP", pct), R("L.END", 0, 1000, "LOOP", pct),
];
export const SMP_KEYS = [
  R("ATTACK", 0, 127, "AMP"), R("DECAY", 0, 127, "AMP"), R("SUSTAIN", 0, 127, "AMP"), R("RELEASE", 0, 127, "AMP"),
  R("CUT", 0, 127, "FILTER", open), R("RES", 0, 127, "FILTER"), R("DRIVE", 0, 127, "FILTER"), R("CRUSH", 0, 127, "FILTER"),
  R("VOICES", 1, 4, "AMP", (v) => (v === 1 ? "MONO" : `${v}`)),
];
export const SMP_NPADS = 16, SMP_NZONES = 8;
export function parseSmp(r) {
  const n = r[2], vals = [];
  for (let k = 0; k < n; k++) vals.push(num(r, 3 + 2 * k, 2) - 8192);
  return { kind: r[0], idx: r[1], vals };
}
// the whole SAMPLER: { mode, pads: 16 x 16, zones: 8 x 10, keys: 9 }; null from firmware before 0.23
export async function readSampler(link) {
  let mode;
  try { mode = (await link.smpGet(3, 0)).vals[0]; } catch (_) { return null; }
  const pads = [], zones = [];
  for (let i = 0; i < SMP_NPADS; i++) pads.push((await link.smpGet(0, i)).vals);
  for (let j = 0; j < SMP_NZONES; j++) zones.push((await link.smpGet(1, j)).vals);
  return { mode, pads, zones, keys: (await link.smpGet(2, 0)).vals };
}
// a sampler object -> the FM-1 (each value; the mode last, so its voices start clean)
export async function writeSampler(link, o) {
  if (!o) return;
  for (let i = 0; i < SMP_NPADS && o.pads && o.pads[i]; i++) for (let k = 0; k < o.pads[i].length; k++) await link.smpSet(0, i, k, o.pads[i][k]);
  for (let j = 0; j < SMP_NZONES && o.zones && o.zones[j]; j++) for (let k = 0; k < o.zones[j].length; k++) await link.smpSet(1, j, k, o.zones[j][k]);
  for (let k = 0; o.keys && k < o.keys.length; k++) await link.smpSet(2, 0, k, o.keys[k]);
  await link.smpSet(3, 0, 0, o.mode || 0);
}

// ---- FM (0.25): the voice (dx7.js: 156 values, OP6 first), LEVEL, VOICES, the macros, where PRESETS is -----------
export const FM_MACROS = ["BRIGHT", "TIME", "ATTACK", "RELEASE"];
export function parseFm(r) {
  return { voice: Uint8Array.from(r.slice(0, 156)), level: r[156], voices: r[157], macros: r.slice(158, 162).map((v) => v - 64),
    preset: num(r, 162, 2), presets: num(r, 164, 2) };
}
// FM as a project keeps it: the voice and its settings (null: an FM-1 before 0.25)
export async function readFm(link) {
  try { const f = await link.fmGet(); return { voice: [...f.voice], level: f.level, voices: f.voices, macros: f.macros, preset: f.preset }; } catch (_) { return null; }
}
export async function writeFm(link, o) {
  if (!o) return;
  await link.fmLoad(Uint8Array.from(o.voice));
  await link.fmSet(156, o.level);
  await link.fmSet(157, o.voices);
  for (let i = 0; i < 4; i++) await link.fmSet(158 + i, o.macros[i] || 0);
}

// ---- the panel (0.26): what the FM-1's screen shows, and its controls pressed from the page ------------------------
// the FM-1's buttons, encoders (plat.h B_* / EN_*) and the flags of UI_STATE
export const BTN = { FX: 0, SEL: 1, ENV: 2, LFO: 3, EDIT: 4, GLO: 5, HOME: 6, SAVE: 7, ARP: 8, SEQ: 9, PLAY: 10, REC: 11, OCTDN: 12, OCTUP: 13 };
export const ENC = { SELECT: 0, ALGO: 1, PRESET: 2, K1: 3, K2: 4, K3: 5, K4: 6 };
export const NKEYS = 27;
export const UI_IN = { DOWN: 0, UP: 1, KEYDOWN: 2, KEYUP: 3, TURN: 4, ALLUP: 5 };
export function parseUi(r) {
  let i = 0;
  const str = () => { let t = ""; while (i < r.length && r[i]) t += String.fromCharCode(r[i++]); i++; return t; };
  const n2 = () => { const v = num(r, i, 2); i += 2; return v; };
  const o = { page: r[i++], trk: r[i++], eng: r[i++] };
  const f = r[i++];
  Object.assign(o, { playing: !!(f & 1), rec: !!(f & 2), dirty: !!(f & 4), naming: !!(f & 8), hold: !!(f & 16), seq: !!(f & 32), stepHeld: !!(f & 64) });
  o.oct = r[i++] - 8;
  o.bpm = n2(); o.pos = n2();
  o.tag = str(); o.name = str();
  const nt = r[i++];
  o.tabSel = r[i++];
  o.tabs = [];
  for (let k = 0; k < nt; k++) o.tabs.push(str());
  o.knobs = [];
  for (let k = 0; k < 4; k++) {
    if (!r[i++]) { o.knobs.push(null); continue; }
    const id = n2(), name = str(), value = n2() - 8192, min = n2() - 8192, max = n2() - 8192, text = str();
    o.knobs.push({ id, name, value, min, max, text });
  }
  o.msg = str(); o.status = str();
  o.litBtn = n2(); o.litKeys = num(r, i, 4); i += 4;
  o.nameCur = r[i++];
  return o;
}

// ---- chords (0.28): a step's flags' top 4 bits pick one of 15 chords (0: none), its notes above the step's; FM,
// SAMPLER's keys and MIDI play them (the same table as the FM-1's: tests/host/editor_golden.mjs) ----------------------
export const ST_CHORD = 0xf0, NCHORDS = 15, CHORD_N = 5;
export const CHORD_NAMES = ["OFF", "MIN", "MAJ", "MIN7", "MAJ7", "7", "MIN9", "MAJ9", "ADD9", "SUS2", "SUS4", "5", "DIM", "AUG", "MIN6", "6"];
export const CHORDS = [[3, 7], [4, 7], [3, 7, 10], [4, 7, 11], [4, 7, 10], [3, 7, 10, 14], [4, 7, 11, 14], [4, 7, 14], [2, 7], [5, 7],
  [7, 12], [3, 6], [4, 8], [3, 7, 9], [4, 7, 9]].map((c) => [...c, 0, 0, 0, 0, 0].slice(0, CHORD_N));
export const chordOf = (st) => (st.flags & ST_CHORD) >> 4;
export function parseChords(r) {
  const out = [];
  for (let c = 0; c < NCHORDS; c++) out.push(r.slice(c * CHORD_N, c * CHORD_N + CHORD_N).map((v) => v - 64));
  return out;
}
// (0.28) the chord these notes make, in any order or inversion: {c (1..15; 0: none of them), root (at or below the
// lowest note; none: the lowest)}; the lowest note is the root when two fit, as on the FM-1
export function chordMatch(notes) {
  const s = [...notes].sort((a, b) => a - b), lo = s[0];
  for (const r of s) {
    let have = 0;
    for (const n of s) have |= 1 << (((n - r) % 12) + 12) % 12;
    for (let c = 0; c < NCHORDS; c++) {
      let want = 1;
      for (const d of CHORDS[c]) if (d) want |= 1 << (d % 12);
      if (have === want) { let root = r; while (root > lo) root -= 12; return { c: c + 1, root: root < 0 ? root + 12 : root }; }
    }
  }
  return { c: 0, root: lo };
}
// a step's notes, its own first (the chord's when it has one)
export function chordNotes(st, chords = CHORDS) {
  const c = chordOf(st), out = [st.note];
  if (c && chords && chords[c - 1]) for (const d of chords[c - 1]) if (d && st.note + d >= 0 && st.note + d <= 127) out.push(st.note + d);
  return out;
}
