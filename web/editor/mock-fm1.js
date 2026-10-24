// SPDX-License-Identifier: GPL-3.0-only
// A simulated FM-1 running DREADNOUGHT, for the editor's demo mode and its tests: a Web MIDI access object with one
// port that answers the editor's commands (src/app/dn_editor.c) the way the firmware does. Copyright (C) 2026 Jamie
// (TwistedGenetics).
import { HDR, CMD, digits, pack7, unpack7, parsePattern, serializePattern, ST_ON, ST_SLIDE, ST_ACC, lfoPtsArgs, NLFO, LFO_PTS, newStep, MAGIC } from "./protocol.js";

const PRESETS = ["CLASSIC RS", "OG REESE", "HOLLOW RS", "TECHSTEP", "NEURO RS", "SYNC RS", "DARK RS", "SUB REESE", "HOOVER RS"];
const LOOPS = ["ROLLER", "STEPPER"];
const CLASSIC = [0, 70, 0, 80, 68, 18, 24, 0, 0, 70, 120, 30, 0, 36, 0, 0, 100, -24];
const LIMITS = [[0, 5], [0, 127], [0, 7], [0, 127], [0, 127], [0, 127], [0, 127], [0, 127], [0, 127], [0, 127],
  [0, 127], [0, 127], [-64, 63], [0, 127], [0, 127], [-64, 63], [0, 127], [-36, 36], [60, 200],
  ...[0, 1].flatMap(() => [[0, 1], [-24, 24], [0, 127], [0, 127], [0, 4], [0, 2], [0, 2], [0, 127]]),
  ...[0, 1, 2, 3].flatMap(() => [[0, 7], [0, 1], [0, 127], [0, 33], [-64, 63], [0, 127], [0, 2], [0, 127]]),
  // SUB (0.13)
  [0, 127], [0, 127], [0, 127], [0, 127], [0, 127], [0, 127], [0, 127], [0, 2], [0, 127], [0, 127], [-64, 63], [0, 127],
  [0, 127], [0, 3], [0, 127], [0, 127], [0, 127], [0, 127], [0, 127], [0, 127], [0, 127], [0, 2], [0, 127], [0, 1],
  [0, 127], [-36, 36], [0, 127]];
const SUB_PRESETS = ["DREAD", "DONK", "ROLLER", "WOBBLE", "ACID 101", "808 SUB", "GROWL", "SQUARE SB", "PUNCH", "TECH SUB", "CRUSHED", "RAVE STAB"];
const SUB_DREAD = [40, 90, 110, 0, 30, 0, 0, 0, 48, 30, 20, 40, 70, 0, 0, 90, 0, 70, 110, 30, 40, 1, 60, 0, 100, -24, 70];
const LFO0 = 18 + 1 + 16;

export function demoPattern() {
  const steps = () => Array.from({ length: 64 }, () => newStep());
  const p = { magic: MAGIC, swing: 54, pad: [0, 0, 0], tracks: [], stepBytes: 8 };
  for (let t = 0; t < 8; t++) p.tracks.push({ steps: steps(), len: 16, mute: 0, chan: t, lastNote: 53 });
  const S = ST_SLIDE, A = ST_ACC;
  [[0, 53, 11, 0], [3, 53, 2, A], [6, 56, 4, 0], [7, 58, 7, S], [10, 53, 7, 0], [12, 51, 7, 0], [14, 41, 7, A]]
    .forEach(([i, n, l, f]) => Object.assign(p.tracks[0].steps[i], { note: n, len: l, flags: ST_ON | f }));
  [0, 2, 4, 6, 8, 10, 12, 14].forEach((i) => Object.assign(p.tracks[1].steps[i], { note: i * 2, len: 4, flags: ST_ON }));
  return p;
}

export class MockFM1 {
  constructor() {
    this.params = [...CLASSIC, 172, 0, 0, 127, 0, 1, 0, 0, 100, 1, 0, 127, 0, 1, 0, 0, 100,
      ...[0, 1, 2, 3].flatMap(() => [0, 1, 7, 0, 0, 0, 0, 0]), ...SUB_DREAD];
    this.patCur = Array(8).fill(0); this.patQ = Array(8).fill(-1);
    this.bank = Array.from({ length: 8 }, (_, t) => Array.from({ length: 8 }, (_, n) => (n === 0 ? null : { steps: Array.from({ length: 64 }, () => newStep()), len: 16, lastNote: 53 })));
    this.lfo = Array.from({ length: NLFO }, () => ({ npts: 16, pts: Array.from({ length: LFO_PTS }, (_, i) => Math.round(100 - (i % 16) * 200 / 15)) }));
    this.pattern = serializePattern(demoPattern());
    this.staging = new Uint8Array(this.pattern);
    this.rev = 1; this.playing = 0; this.preset = 0; this.dirty = 0; this.step = 0; this.saved = 0;
    // the project (0.12): each track's instrument, sends, returns, the two FX slots; the sample slots (X0X's layout)
    this.proj = { name: "PROJECT 1", eng: [1, 2, 3, 4, 4, 4, 4, 4], send: [Array(8).fill(0), Array(8).fill(0)], ret: [100, 100],
      fx: [[1, 7, 60, 70], [0, 7, 60, 70]] };
    this.slots = [0, 1, 2].map(() => ({ buf: new Uint8Array(0x14000).fill(0xFF), ok: false }));
    this.log = [];
    const self = this;
    this.input = { name: "DREADNOUGHT FM-1", state: "connected", onmidimessage: null, open: async () => {} };
    this.output = { name: "DREADNOUGHT FM-1", state: "connected", open: async () => {}, send: (d) => {
      // as Web MIDI does: a SysEx frame may carry only 7-bit bytes between F0 and F7 (Chrome throws otherwise)
      for (let i = 1; i < d.length - 1; i++)
        if (d[i] > 0x7F) throw new TypeError(`Failed to execute 'send' on 'MIDIOutput': System exclusive message contains a status byte at index ${i} (${d[i]}).`);
      setTimeout(() => self.recv(d), 2);
    } };
    this.access = { inputs: new Map([["in", this.input]]), outputs: new Map([["out", this.output]]) };
    this.timer = setInterval(() => { if (this.playing) this.step++; }, 87);
  }
  reply(cmd, bytes) { if (this.input.onmidimessage) this.input.onmidimessage({ data: new Uint8Array([0xF0, ...HDR, cmd, ...bytes, 0xF7]) }); }
  recv(d) {
    if (d[0] !== 0xF0 || d[1] !== HDR[0]) return;
    const cmd = d[4], a = [...d.slice(5, d.length - 1)];
    this.log.push(cmd);
    const r = this.answer(cmd, a);
    if (r) this.reply(cmd, r);
  }
  answer(cmd, a) {
    switch (cmd) {
      case CMD.INFO: return [..."DREADNOUGHT DEMO".split("").map((c) => c.charCodeAt(0)), 0, 0, 0, 0, 0, 0, 0];
      case CMD.STATE: return [...digits(this.rev, 5), ...digits(this.params[18], 2), this.playing, 0, this.preset, 0,
        this.pattern[4], this.dirty, 32, 32, PRESETS.length, LOOPS.length, ...digits(this.pattern.length, 2),
        this.params.length, ...digits(this.playing ? this.step : 0, 2)];
      case CMD.PARAMS: return [this.params.length, ...this.params.flatMap((v) => digits(v + 8192, 2))];
      case CMD.PARAM_SET: {
        const i = a[0], v = (a[1] | a[2] << 7) - 8192;
        if (i >= this.params.length) return [1];
        const [lo, hi] = LIMITS[i];
        this.params[i] = Math.max(lo, Math.min(hi, v));
        if (i >= LFO0 && (i - LFO0) % 8 === 1) this.params[i + 1] = v ? 7 : 64;   // SYNC resets RATE (as the FM-1)
        if (i >= LFO0 && (i - LFO0) % 8 === 2) this.params[i] = Math.min(this.params[i], this.params[i - 1] ? 15 : 127);
        this.rev++; this.dirty = 1;
        return [0];
      }
      case CMD.PAT_READ: {
        const off = a[0] | a[1] << 7, n = Math.min(a[2], 200, this.pattern.length - off);
        return [...digits(off, 2), ...pack7(this.pattern.subarray(off, off + n))];
      }
      case CMD.PAT_WRITE: {
        const off = a[0] | a[1] << 7, b = unpack7(a.slice(2));
        this.staging.set(b.slice(0, this.staging.length - off), off);
        return [off + b.length <= this.staging.length ? 0 : 1];
      }
      case CMD.PAT_COMMIT: {
        try {
          const p = parsePattern([...this.staging]);
          if (p.magic !== MAGIC) return [1];
        } catch (_) { return [1]; }
        this.pattern = new Uint8Array(this.staging);
        this.rev++; this.dirty = 1;
        return [0];
      }
      case CMD.LFO_PTS_GET: {
        const l = this.lfo[a[0]];
        if (!l) return null;
        return lfoPtsArgs(a[0], l.npts, l.pts);
      }
      case CMD.LFO_PTS_SET: {
        const l = this.lfo[a[0]];
        if (!l || a.length < 2 + 2 * LFO_PTS) return [1];
        l.npts = a[1] === 32 ? 32 : 16;
        l.pts = Array.from({ length: LFO_PTS }, (_, i) => (a[2 + 2 * i] | a[3 + 2 * i] << 7) - 100);
        this.rev++; this.dirty = 1;
        return [0];
      }
      case CMD.LFO_NOW: {
        const t = Date.now() / 1000;
        return [0, 1, 2, 3].flatMap((k) => { const ph = (t * (k + 1) / 2) % 1; return [...digits(Math.floor(ph * 16384), 2), ...digits(8192 + Math.round(8191 * Math.sin(2 * Math.PI * ph)), 2)]; });
      }
      case CMD.PATS: {
        const p = parsePattern([...this.pattern]), out = [];
        let notes = 0;
        for (let t = 0; t < 8; t++) {
          let used = 0;
          for (let n = 0; n < 8; n++) {
            const tr = n === this.patCur[t] ? p.tracks[t] : this.bank[t][n];
            const c = tr ? tr.steps.filter((x) => x.flags & ST_ON).length : 0;
            if (c) used |= 1 << n;
            notes += c;
          }
          out.push(this.patCur[t], this.patQ[t] < 0 ? 127 : this.patQ[t], used & 127, used >> 7);
        }
        return [...out, ...digits(notes, 2), ...digits(1680, 2)];
      }
      case CMD.PAT_SEL: {
        const [t, n] = a;
        if (t > 7 || n > 7) return null;
        const go = () => {
          const p = parsePattern([...this.pattern]), live = p.tracks[t];
          this.bank[t][this.patCur[t]] = { steps: live.steps, len: live.len, lastNote: live.lastNote };
          const nx = this.bank[t][n] || { steps: Array.from({ length: 64 }, () => newStep()), len: 16, lastNote: 53 };
          Object.assign(live, { steps: nx.steps, len: nx.len, lastNote: nx.lastNote });
          this.pattern = serializePattern(p);
          this.patCur[t] = n; this.patQ[t] = -1; this.rev++;
        };
        if (this.playing) { this.patQ[t] = n; setTimeout(go, 600); } else go();
        this.rev++;
        return [0];
      }
      case CMD.TRANSPORT: this.playing = a[0] ? 1 : 0; if (this.playing) this.step = 0; return [this.playing];
      case CMD.SAVE: this.dirty = 0; this.saved++; return [0];
      case CMD.PRESET: this.preset = a[0]; this.rev++; return [0];
      case CMD.SUB_PRESET: this.subPreset = a[0]; this.rev++; return [0];
      case CMD.PROJ: {
        const P = this.proj, nm = P.name.padEnd(12, "\0").slice(0, 12).split("").map((c) => c.charCodeAt(0) & 127);
        const used = this.slots.reduce((acc, sl) => acc + (sl.ok ? 512 + new DataView(sl.buf.buffer).getUint32(16, true) : 0), 0);
        const fxk = P.fx.reduce((acc, f) => acc + (f[0] === 1 ? 96 : 0), 0);
        return [...nm, ...P.eng, ...P.send[0], ...P.send[1], ...P.ret, ...P.fx[0], ...P.fx[1], ...digits(used, 3),
          ...digits(3 * (0x14000 - 512), 3), ...digits(fxk, 2), ...digits(144, 2), 12, 6, 2];
      }
      case CMD.PROJ_SET: {
        const f = a[0], v = a[1] | a[2] << 7, P = this.proj;
        this.rev++; this.dirty = 1;
        if (f < 8) {
          if (v > 5) return [1];
          if (v !== 0 && v !== 4) { const u = P.eng.indexOf(v); if (u >= 0 && u !== f) P.eng[u] = P.eng[f]; }
          P.eng[f] = v;
          return [0];
        }
        if (f < 16) { P.send[0][f - 8] = Math.min(127, v); return [0]; }
        if (f < 24) { P.send[1][f - 16] = Math.min(127, v); return [0]; }
        if (f < 26) { P.ret[f - 24] = Math.min(127, v); return [0]; }
        if (f < 34) {
          const k = f < 30 ? 0 : 1, i = (f - 26) % 4;
          if (i === 0) {
            if (v === 1 && P.fx[1 - k][0] === 1) return [1];   // two delays: no memory for the second
            P.fx[k] = [v, 7, 60, 70];
          } else P.fx[k][i] = Math.min(i === 1 ? 11 : 127, v);
          return [0];
        }
        return [1];
      }
      case CMD.PROJ_NAME: this.proj.name = String.fromCharCode(...a.slice(0, 11)) || "P1"; this.rev++; this.dirty = 1; return [0];
      case 11: { const sl = this.slots[a[0]]; if (!sl) return [a[0], 1]; sl.ok = false; sl.buf.fill(0xFF, 0, 512); return [a[0], 0]; }
      case 12: {
        const sl = this.slots[a[0]], off = a[1] | a[2] << 7 | a[3] << 14, b = unpack7(a.slice(4));
        if (!sl || off < 512 || off + b.length > sl.buf.length) return [a[0], a[1], a[2], a[3], 1];
        sl.buf.set(b, off);
        return [a[0], a[1], a[2], a[3], 0];
      }
      case 13: {
        const sl = this.slots[a[0]], h = unpack7(a.slice(1)).slice(0, 480);
        if (!sl || h.length < 480) return [a[0], 1];
        sl.buf.set(h, 0); sl.ok = true; this.rev++;
        return [a[0], 0];
      }
      case 14: { const sl = this.slots[a[0]]; if (!sl) return [a[0], 1]; sl.buf.fill(0xFF); sl.ok = false; return [a[0], 0]; }
      case 15: {
        const out = [3, 80];
        for (const sl of this.slots) {
          const nz = sl.ok ? sl.buf[6] : 0, nm = sl.ok ? String.fromCharCode(...sl.buf.slice(8, 16)).replace(/\0.*$/, "") : "";
          out.push(nz, ...nm.split("").map((c) => c.charCodeAt(0)), 0, sl.ok ? Math.ceil(new DataView(sl.buf.buffer).getUint32(16, true) / 1024) : 0);
        }
        return out;
      }
      case 16: {
        const sl = this.slots[a[0]], off = a[1] | a[2] << 7 | a[3] << 14, n = Math.min(224, a[4] * 2);
        if (!sl) return null;
        return [a[0], a[1], a[2], a[3], ...pack7(sl.buf.subarray(off, Math.min(sl.buf.length, off + n)))];
      }
      case 50: { const sl = this.slices && this.slices[a.slice(0, 5).join(",")]; return [...a.slice(0, 5), sl ? sl.length : 0, ...(sl || []).flatMap((v) => [v & 127, (v >> 7) & 127, (v >> 14) & 127])]; }
      case 51: {
        const cnt = a[5], st = [];
        for (let j = 0; j < cnt; j++) st.push(a[6 + 3 * j] | a[7 + 3 * j] << 7 | a[8 + 3 * j] << 14);
        (this.slices = this.slices || {})[a.slice(0, 5).join(",")] = st;
        return [...a.slice(0, 5), 0];
      }
      case CMD.NAMES: {
        const list = [PRESETS, LOOPS, ["OFF", "REESE", "BRK A", "BRK B", "MIDI", "SUB"], ["OFF", "DELAY"],
          ["1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4", "1/2T", "1/4D", "1/2"], SUB_PRESETS][a[0]] || [];
        const out = [list.length, a[1]];
        for (let i = a[1]; i < list.length; i++) out.push(...list[i].split("").map((c) => c.charCodeAt(0)), 0);
        return out;
      }
      default: return null;
    }
  }
  close() { clearInterval(this.timer); }
}
