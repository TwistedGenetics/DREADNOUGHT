// SPDX-License-Identifier: GPL-3.0-only
// A simulated FM-1 running DREADNOUGHT, for the editor's demo mode and its tests: a Web MIDI access object with one
// port that answers the editor's commands (src/app/dn_editor.c) the way the firmware does. Copyright (C) 2026 Jamie
// (TwistedGenetics).
import { HDR, CMD, digits, pack7, unpack7, parsePattern, serializePattern, ST_ON, ST_SLIDE, ST_ACC, lfoPtsArgs, NLFO, LFO_PTS } from "./protocol.js";

const PRESETS = ["CLASSIC RS", "OG REESE", "HOLLOW RS", "TECHSTEP", "NEURO RS", "SYNC RS", "DARK RS", "SUB REESE", "HOOVER RS"];
const LOOPS = ["ROLLER", "STEPPER"];
const CLASSIC = [0, 70, 0, 80, 68, 18, 24, 0, 0, 70, 120, 30, 0, 36, 0, 0, 100, -24];
const LIMITS = [[0, 5], [0, 127], [0, 7], [0, 127], [0, 127], [0, 127], [0, 127], [0, 127], [0, 127], [0, 127],
  [0, 127], [0, 127], [-64, 63], [0, 127], [0, 127], [-64, 63], [0, 127], [-36, 36], [60, 200],
  ...[0, 1].flatMap(() => [[0, 1], [-24, 24], [0, 127], [0, 127], [0, 4], [0, 2], [0, 2], [0, 127]]),
  ...[0, 1, 2, 3].flatMap(() => [[0, 7], [0, 1], [0, 127], [0, 21], [-64, 63], [0, 127], [0, 2], [0, 127]])];
const LFO0 = 18 + 1 + 16;

export function demoPattern() {
  const steps = () => Array.from({ length: 64 }, () => ({ note: 53, flags: 0, len: 3, vel: 100 }));
  const p = { magic: 0x32514E44, swing: 54, pad: [0, 0, 0], tracks: [] };
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
      ...[0, 1, 2, 3].flatMap(() => [0, 1, 7, 0, 0, 0, 0, 0])];
    this.lfo = Array.from({ length: NLFO }, () => ({ npts: 16, pts: Array.from({ length: LFO_PTS }, (_, i) => Math.round(100 - (i % 16) * 200 / 15)) }));
    this.pattern = serializePattern(demoPattern());
    this.staging = new Uint8Array(this.pattern);
    this.rev = 1; this.playing = 0; this.preset = 0; this.dirty = 0; this.step = 0; this.saved = 0;
    this.log = [];
    const self = this;
    this.input = { name: "DREADNOUGHT FM-1", state: "connected", onmidimessage: null, open: async () => {} };
    this.output = { name: "DREADNOUGHT FM-1", state: "connected", open: async () => {}, send: (d) => setTimeout(() => self.recv(d), 2) };
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
          if (p.magic !== 0x32514E44) return [1];
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
      case CMD.TRANSPORT: this.playing = a[0] ? 1 : 0; if (this.playing) this.step = 0; return [this.playing];
      case CMD.SAVE: this.dirty = 0; this.saved++; return [0];
      case CMD.PRESET: this.preset = a[0]; this.rev++; return [0];
      case CMD.NAMES: {
        const list = a[0] ? LOOPS : PRESETS, out = [list.length, a[1]];
        for (let i = a[1]; i < list.length; i++) out.push(...list[i].split("").map((c) => c.charCodeAt(0)), 0);
        return out;
      }
      default: return null;
    }
  }
  close() { clearInterval(this.timer); }
}
