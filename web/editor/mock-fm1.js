// SPDX-License-Identifier: GPL-3.0-only
// A simulated FM-1 running DREADNOUGHT, for the editor's demo mode and its tests: a Web MIDI access object with one
// port that answers the editor's commands (src/app/dn_editor.c) the way the firmware does. Copyright (C) 2026 Jamie
// (TwistedGenetics).
import { parseSlot, SLOT_HDR_LEN, SLOT_DATA_OFF, crc32 } from "./media.js";
import { zoneVoices, unpack as fmUnpack } from "./dx7.js";
import { parseTable, wtMin, wtMax } from "./wavetable.js";
import { juMin, juMax } from "./juno.js";
import { REESE, CHORDS } from "./protocol.js";
import { SMP_PAD, SMP_ZONE, SMP_KEYS, HDR, CMD, digits, pack7, unpack7, parsePattern, serializePattern, ST_ON, ST_SLIDE, ST_ACC, lfoPtsArgs, NLFO_ALL, LFO_PTS, FX_NAMES, FX_KB, FX_ARENA_KB, FX_DEFAULTS, fxParams, newStep, MAGIC } from "./protocol.js";

const PRESETS = ["CLASSIC RS", "OG REESE", "HOLLOW RS", "TECHSTEP", "NEURO RS", "SYNC RS", "DARK RS", "SUB REESE", "HOOVER RS", "INIT"];
const USERS = Array.from({ length: 8 }, (_, i) => `USER ${i + 1} -`);   // (0.15: the user sounds, empty)
const LOOPS = ["ROLLER", "STEPPER"];
const FM_FACTORY = ["DUB FM BS", "FM REESE", "HOLLOW STB", "DARK BELL", "METAL HIT", "FM PLUCK", "DARK PAD", "GROWL BASS",
  "SINE SUB", "INIT VOICE"];
// an INIT VOICE (dn_fm_patch_init), named
function fmInit(name) {
  const v = new Uint8Array(156);
  for (let op = 0; op < 6; op++) {
    const o = op * 21;
    for (let i = 0; i < 4; i++) { v[o + i] = 99; v[o + 4 + i] = i < 3 ? 99 : 0; }
    v[o + 8] = 39; v[o + 16] = op === 5 ? 99 : 0; v[o + 18] = 1; v[o + 20] = 7;
  }
  for (let i = 0; i < 4; i++) { v[126 + i] = 99; v[130 + i] = 50; }
  v[136] = 1; v[137] = 35; v[141] = 1; v[143] = 3; v[144] = 24; v[155] = 0x3f;
  const nm = name.padEnd(10).slice(0, 10);
  for (let i = 0; i < 10; i++) v[145 + i] = nm.charCodeAt(i);
  return v;
}
const CLASSIC = [0, 70, 0, 80, 68, 18, 24, 0, 0, 70, 120, 30, 0, 36, 0, 0, 100, -24];
const LIMITS = [[0, 5], [0, 127], [0, 7], [0, 127], [0, 127], [0, 127], [0, 127], [0, 127], [0, 127], [0, 127],
  [0, 127], [0, 127], [-64, 63], [0, 127], [0, 127], [-64, 63], [0, 127], [-36, 36], [60, 200],
  ...[0, 1].flatMap(() => [[0, 1], [-24, 24], [0, 127], [0, 127], [0, 4], [0, 2], [0, 2], [0, 127]]),
  ...[0, 1, 2, 3].flatMap(() => [[0, 7], [0, 1], [0, 127], [0, 33], [-64, 63], [0, 127], [0, 2], [0, 127]]),
  // SUB (0.13)
  [0, 127], [0, 127], [0, 127], [0, 127], [0, 127], [0, 127], [0, 127], [0, 2], [0, 127], [0, 127], [-64, 63], [0, 127],
  [0, 127], [0, 3], [0, 127], [0, 127], [0, 127], [0, 127], [0, 127], [0, 127], [0, 127], [0, 2], [0, 127], [0, 1],
  [0, 127], [-36, 36], [0, 127],
  // SUB's LFOs (0.15)
  ...[0, 1, 2, 3].flatMap(() => [[0, 7], [0, 1], [0, 127], [0, 33], [-64, 63], [0, 127], [0, 2], [0, 127]])];
// JUNO (0.36): the factory sounds (src/dsp/dn_ju.c JU_PRESET: the host tests compare)
export const JU_PRESETS = [
  ["INIT", [1, 1, 0, 0, 0, 0, 0, 0, 100, 0, 0, 64, 1, 0, 0, 0, 0, 40, 127, 30, 0, 30, 60, 0, 0, 0, 0, 100, 6, 0]],
  ["JUNO PAD", [1, 1, 1, 0, 70, 0, 0, 0, 70, 20, 30, 64, 1, 0, 0, 0, 70, 80, 100, 85, 0, 30, 45, 30, 0, 0, 2, 100, 6, 0]],
  ["BRASS", [1, 1, 0, 0, 0, 0, 0, 0, 45, 15, 75, 80, 1, 0, 0, 0, 25, 60, 80, 40, 0, 30, 60, 0, 0, 0, 1, 100, 6, 0]],
  ["STRINGS", [1, 0, 1, 0, 90, 0, 0, 0, 85, 0, 0, 64, 2, 0, 0, 0, 75, 60, 120, 85, 0, 30, 55, 0, 0, 0, 2, 100, 6, 0]],
  ["106 BASS", [0, 0, 1, 127, 40, 1, 0, 0, 35, 40, 60, 64, 0, 0, 0, 0, 0, 55, 0, 20, 0, 30, 60, 0, 0, 0, 0, 100, 1, 0]],
  ["ORGAN", [1, 0, 1, 100, 0, 0, 0, 0, 110, 0, 0, 64, 1, 0, 0, 0, 0, 40, 127, 10, 1, 30, 60, 0, 0, 0, 1, 75, 6, 0]],
  ["RAVE STAB", [1, 1, 1, 0, 60, 1, 0, 0, 60, 70, 70, 64, 1, 0, 0, 0, 0, 50, 20, 35, 0, 30, 60, 0, 0, 0, 3, 100, 6, 0]],
  ["PLUCK", [1, 1, 0, 0, 0, 0, 0, 0, 40, 30, 80, 64, 1, 0, 0, 40, 0, 45, 0, 45, 0, 30, 60, 0, 0, 0, 2, 100, 6, 0]],
  ["PIANO", [1, 1, 1, 0, 30, 1, 0, 0, 55, 0, 60, 100, 1, 0, 0, 0, 0, 85, 0, 60, 0, 80, 60, 0, 0, 0, 1, 100, 6, 0]],
  ["SUB BASS", [0, 0, 0, 127, 0, 0, 0, 0, 40, 0, 0, 64, 0, 0, 0, 0, 0, 60, 127, 15, 0, 30, 60, 0, 0, 0, 0, 100, 1, 0]],
  ["ACID POLY", [1, 1, 0, 0, 0, 0, 0, 0, 30, 95, 85, 64, 1, 0, 0, 0, 0, 40, 10, 25, 0, 30, 60, 0, 0, 0, 0, 100, 3, 0]],
  ["GLASS", [2, 0, 1, 0, 100, 2, 0, 0, 95, 60, 0, 64, 3, 0, 0, 0, 5, 90, 30, 90, 0, 30, 60, 0, 0, 0, 3, 100, 6, 0]],
  ["DARK PAD", [1, 1, 0, 60, 0, 0, 0, 0, 35, 55, 0, 64, 1, 0, 40, 0, 90, 40, 127, 95, 0, 30, 20, 60, 0, 0, 2, 100, 6, 0]],
  ["NOISE SWEEP", [1, 0, 0, 0, 0, 0, 127, 0, 50, 90, 0, 64, 1, 0, 90, 0, 60, 40, 127, 80, 0, 30, 30, 0, 0, 0, 2, 100, 6, 0]],
  ["WHISTLE", [1, 0, 0, 0, 0, 0, 0, 0, 60, 127, 0, 127, 1, 0, 0, 0, 40, 40, 127, 50, 0, 30, 60, 0, 0, 0, 1, 100, 6, 0]],
  ["HOOVERISH", [0, 1, 1, 0, 110, 0, 0, 30, 80, 30, 20, 64, 1, 0, 0, 0, 0, 40, 127, 30, 0, 30, 80, 40, 0, 0, 3, 100, 2, 0]],
  ["JUNGLE STAB", [1, 1, 1, 80, 50, 1, 0, 0, 70, 35, 55, 64, 2, 0, 0, 0, 0, 45, 0, 30, 0, 30, 60, 0, 0, 0, 1, 100, 6, 0]],
];
// WAVE (0.32): the factory tables, the factory sounds (src/dsp/dn_wt.c WT_PRESET: the host tests compare)
export const WT_TABLES = ["ANALOG", "REESE", "PPG", "VOCAL", "SYNC", "PWM", "GRIT", "HOLLOW"];
export const WT_PRESETS = [
  ["INIT", [0, 0, 0, 0, 1, 20, 64, 0, 100, 20, 0, 0, 2, 60, 110, 40, 2, 60, 0, 40, 0, 0, 40, 0, 100, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0, 1, 30, 0, 0, 4, 70, 0, 0, 2, 60, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]],
  ["SUPERSAW", [0, 127, 0, 0, 4, 45, 110, 0, 112, 10, 0, 0, 2, 60, 115, 55, 2, 60, 0, 40, 0, 0, 40, 0, 90, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 40, 0, 0, 30, 0, 20, 0, 0, 0, 1, 30, 0, 0, 4, 70, 0, 0, 2, 60, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]],
  ["REESE", [1, 40, 0, 0, 2, 25, 60, 60, 62, 30, 18, 40, 0, 60, 127, 22, 2, 70, 0, 40, 0, 30, 18, 0, 100, 1, 18, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0, 1, 30, 0, 0, 4, 70, 0, 0, 2, 60, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]],
  ["PPG BELL", [2, 0, 0, 0, 1, 20, 64, 0, 120, 10, 0, 0, 0, 82, 40, 70, 0, 72, 0, 60, 50, 0, 40, 0, 100, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0, 1, 30, 0, 0, 4, 70, 0, 0, 2, 60, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]],
  ["VOX PAD", [3, 30, 0, 0, 2, 15, 90, 0, 92, 15, 0, 0, 62, 80, 110, 82, 2, 60, 0, 40, 0, 48, 14, 0, 100, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0, 1, 30, 0, 0, 4, 70, 0, 0, 2, 60, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]],
  ["SYNC LEAD", [4, 10, 1, 50, 1, 20, 64, 30, 100, 30, 10, 20, 0, 60, 110, 40, 0, 62, 20, 40, 0, 0, 40, 0, 100, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0, 1, 30, 0, 0, 4, 70, 0, 0, 2, 60, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]],
  ["PWM STRNG", [5, 40, 0, 0, 2, 20, 90, 0, 86, 10, 0, 0, 52, 70, 110, 70, 2, 60, 0, 40, 0, 40, 24, 0, 100, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0, 1, 30, 0, 0, 4, 70, 0, 0, 2, 60, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]],
  ["GRIT BASS", [6, 20, 0, 0, 1, 20, 64, 50, 70, 40, 30, 70, 0, 64, 100, 28, 0, 52, 0, 30, 60, 0, 40, 0, 100, 1, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0, 1, 30, 0, 0, 4, 70, 0, 0, 2, 60, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]],
  ["ORGAN", [7, 60, 0, 0, 1, 20, 64, 20, 104, 10, 0, 10, 2, 60, 110, 40, 2, 60, 0, 40, 0, 0, 40, 0, 100, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0, 1, 30, 0, 0, 4, 70, 0, 0, 2, 60, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]],
  ["FM GROWL", [0, 0, 5, 60, 1, 20, 64, 40, 80, 50, 20, 50, 0, 60, 120, 30, 2, 60, 0, 40, 0, 0, 40, 30, 100, 1, 10, -12, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0, 1, 30, 0, 0, 4, 70, 0, 0, 2, 60, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]],
  ["BENT KEYS", [7, 30, 2, 70, 2, 12, 70, 0, 96, 20, 15, 0, 0, 72, 70, 40, 2, 60, 0, 40, 0, 0, 40, 0, 100, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0, 1, 30, 0, 0, 4, 70, 0, 0, 2, 60, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]],
  ["CRUSHER", [2, 50, 6, 64, 1, 20, 64, 30, 90, 30, 0, 40, 0, 60, 110, 30, 2, 60, 0, 40, 0, 40, 50, 0, 95, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0, 1, 30, 0, 0, 4, 70, 0, 0, 2, 60, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]],
  ["FM BELL", [0, 0, 0, 0, 1, 20, 64, 0, 124, 20, 0, 0, 0, 86, 0, 72, 2, 60, 0, 40, 0, 0, 40, 0, 100, 4, 0, 0, 1, 0, 0, 0, 0, 1, 7, 0, 0, 1, 40, 0, 0, 0, 0, 0, 0, 0, 0, 1, 30, 0, 0, 4, 70, 0, 0, 2, 70, 0, 40, 5, 7, 50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]],
  ["SYNC SWEEP", [0, 127, 0, 0, 1, 20, 64, 0, 100, 25, 0, 25, 2, 60, 110, 40, 0, 75, 20, 40, 0, 0, 40, 0, 100, 1, 16, 0, 1, 0, 127, 0, 0, 0, 0, 0, 0, 4, 10, 0, 0, 0, 0, 0, 0, 0, 0, 1, 30, 0, 0, 4, 70, 0, 0, 2, 60, 0, 40, 4, 7, 50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]],
  ["RING DRONE", [3, 30, 0, 0, 1, 20, 64, 0, 90, 20, 0, 0, 70, 60, 110, 90, 2, 60, 0, 40, 0, 0, 40, 0, 100, 4, 0, 0, 1, 7, 0, 0, 0, -1, 7, 0, 20, 2, 90, 0, 0, 0, 0, 0, 0, 0, 0, 1, 10, 0, 0, 4, 70, 0, 0, 2, 60, 0, 40, 2, 1, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]],
  ["COMB PLUCK", [0, 127, 0, 0, 1, 20, 64, 0, 80, 116, 0, 0, 0, 84, 0, 70, 2, 60, 0, 40, 0, 0, 40, 0, 110, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 40, 50, 5, 127, 0, 0, 0, 0, 0, 1, 30, 0, 0, 4, 70, 0, 0, 2, 60, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]],
  ["FOLD BASS", [0, 0, 0, 0, 1, 20, 64, 60, 74, 30, 0, 70, 0, 60, 120, 40, 0, 60, 0, 40, 0, 0, 40, 0, 100, 1, 8, -12, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 40, 0, 1, 0, 2, 0, 0, 0, 0, 1, 30, 0, 0, 4, 70, 0, 0, 2, 60, 0, 40, 4, 11, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]],
  ["HOOVER", [1, 80, 0, 0, 4, 70, 120, 0, 100, 20, 0, 0, 2, 60, 110, 40, 0, 45, 0, 40, 0, 0, 40, 0, 100, 1, 30, 0, 1, 0, 127, 0, 0, -1, 0, 0, 80, 0, 40, 0, 0, 0, 0, 40, 0, 0, 0, 1, 30, 0, 0, 4, 70, 0, 0, 2, 60, 0, 40, 4, 5, -8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]],
  ["BIT WOBBLE", [5, 60, 0, 0, 1, 20, 64, 0, 60, 60, 0, 70, 2, 60, 110, 40, 2, 60, 0, 40, 0, 0, 40, 0, 100, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 40, 0, 1, 0, 5, 0, 0, 0, 0, 1, 76, 1, 0, 4, 70, 0, 0, 2, 60, 0, 40, 2, 9, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]],
  ["S&H TWIST", [2, 40, 0, 0, 1, 20, 64, 0, 100, 40, 0, 0, 2, 60, 110, 40, 2, 60, 0, 40, 0, 0, 40, 0, 100, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0, 1, 30, 0, 0, 4, 100, 1, 0, 2, 60, 0, 40, 3, 1, 40, 3, 9, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]],
];
const SUB_PRESETS = ["DREAD", "DONK", "ROLLER", "WOBBLE", "ACID 101", "808 SUB", "GROWL", "SQUARE SB", "PUNCH", "TECH SUB", "CRUSHED", "RAVE STAB", "INIT"];
const SUB_DREAD = [40, 90, 110, 0, 30, 0, 0, 0, 48, 30, 20, 40, 70, 0, 0, 90, 0, 70, 110, 30, 40, 1, 60, 0, 100, -24, 70];
const LFO0 = 18 + 1 + 16, SLFO0 = LFO0 + 32 + 27;

// the FX arena as the FM-1 lays it out: MASSIVE takes what the other leaves
const fxMem = (a, b) => (a === 11 || b === 11 ? FX_ARENA_KB : FX_KB[a] + FX_KB[b]);

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
      ...[0, 1, 2, 3].flatMap(() => [0, 1, 7, 0, 0, 0, 0, 0]), ...SUB_DREAD,
      ...[0, 1, 2, 3].flatMap(() => [0, 1, 7, 0, 0, 0, 0, 0])];
    this.patCur = Array(8).fill(0); this.patQ = Array(8).fill(-1);
    this.bank = Array.from({ length: 8 }, (_, t) => Array.from({ length: 8 }, (_, n) => (n === 0 ? null : { steps: Array.from({ length: 64 }, () => newStep()), len: 16, lastNote: 53 })));
    this.lfo = Array.from({ length: NLFO_ALL }, () => ({ npts: 16, pts: Array.from({ length: LFO_PTS }, (_, i) => Math.round(100 - (i % 16) * 200 / 15)) }));
    this.pattern = serializePattern(demoPattern());
    this.staging = new Uint8Array(this.pattern);
    this.rev = 1; this.playing = 0; this.preset = 0; this.dirty = 0; this.step = 0; this.saved = 0;
    // the project (0.12): each track's instrument, sends, returns, the two FX slots; the sample slots (X0X's layout)
    this.proj = { name: "PROJECT 1", eng: [1, 2, 3, 4, 4, 4, 4, 4], send: [Array(8).fill(0), Array(8).fill(0)], ret: [100, 100],
      fx: [[1, 7, 60, 70, 0, 0, 0, 0], [0, 0, 0, 0, 0, 0, 0, 0]] };
    // SAMPLER (0.23): its defaults, as dn_smp.c sets them
    this.smp = { mode: 0, pads: Array.from({ length: 16 }, () => [-1, 0, 1000, 0, 0, 0, 100, 0, 0, 127, 64, 0, 127, 0, 0, 0]),
      zones: Array.from({ length: 8 }, () => [-1, 60, 0, 127, 0, 100, 0, 0, 0, 1000]), keys: [0, 64, 127, 30, 127, 0, 0, 0, 4] };
    // FM (0.25): its voice, LEVEL, VOICES, the macros, where PRESETS is
    this.smp2 = JSON.parse(JSON.stringify(this.smp));   // (0.36: SAMPLER 2, the same defaults)
    this.fm = { voice: fmInit("DUB FM BS"), level: 100, voices: 6, macros: [0, 0, 0, 0], preset: 0 };
    this.fmPlayed = 0;
    this.wave = { p: WT_PRESETS[1][1].slice(), preset: 1 };   // (0.32: SUPERSAW, as the FM-1 starts)
    this.wtPlayed = 0;
    this.juno = { p: JU_PRESETS[1][1].slice(), preset: 1 };   // (0.36: JUNO PAD, as the FM-1 starts)
    this.juPlayed = 0;
    this.song = { n: 1, on: 0, rows: Array.from({ length: 64 }, (_, i) => ({ pat: Array(8).fill(i ? 127 : 0), bars: 4 })) };   // (0.38)
    // the panel (0.26): a small FM-1 behind it: REESE's five sound pages and a sequencer page
    this.ui = { page: 0, back: 0, keys: 0, btn: 0, msg: "" };
    this.slots = [0x14000, 0x14000, 0x10000].map((n) => ({ buf: new Uint8Array(n).fill(0xFF), ok: false }));   // (0.29: slot 3 64 KB)
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
        this.pattern[4], this.dirty, 32, 32, PRESETS.length + USERS.length, LOOPS.length, ...digits(this.pattern.length, 2),
        this.params.length, ...digits(this.playing ? this.step : 0, 2)];
      case CMD.PARAMS: return [this.params.length, ...this.params.flatMap((v) => digits(v + 8192, 2))];
      case CMD.PARAM_SET: {
        const i = a[0], v = (a[1] | a[2] << 7) - 8192;
        if (i >= this.params.length) return [1];
        const [lo, hi] = LIMITS[i];
        this.params[i] = Math.max(lo, Math.min(hi, v));
        const lo8 = i >= SLFO0 ? SLFO0 : i < LFO0 + 32 ? LFO0 : -1;   // (REESE's LFOs, or SUB's)
        if (lo8 >= 0 && i >= LFO0 && (i - lo8) % 8 === 1) this.params[i + 1] = v ? 7 : 64;   // SYNC resets RATE (as the FM-1)
        if (lo8 >= 0 && i >= LFO0 && (i - lo8) % 8 === 2) this.params[i] = Math.min(this.params[i], this.params[i - 1] ? 15 : 127);
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
        return [0, 1, 2, 3, 4, 5, 6, 7].flatMap((k) => { const ph = (t * (k + 1) / 2) % 1; return [...digits(Math.floor(ph * 16384), 2), ...digits(8192 + Math.round(8191 * Math.sin(2 * Math.PI * ph)), 2)]; });
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
      case CMD.PARAMS_HI: {                         // (0.19: the filter envelopes)
        this.hi = this.hi || [0, 70, 0, 30, 0, 70, 110, 30];
        if (!a.length) return [8, ...this.hi.flatMap((v) => digits(v + 8192, 2))];
        if (a[0] >= 8) return null;
        this.hi[a[0]] = Math.max(0, Math.min(127, (a[1] | a[2] << 7) - 8192)); this.rev++; this.dirty = 1;
        return [0];
      }
      case CMD.SMP_GET: case CMD.SMP_SET: {
        if (a[0] > 7) return null;
        const kind = a[0] & 3, i = a[1], m = a[0] >= 4 ? this.smp2 : this.smp;   // (0.36: kinds 4-7 SAMPLER 2's)
        if ( (kind === 0 && i >= 16) || (kind === 1 && i >= 8)) return null;
        const vals = kind === 0 ? m.pads[i] : kind === 1 ? m.zones[i] : kind === 2 ? m.keys : null;
        const defs = kind === 0 ? SMP_PAD : kind === 1 ? SMP_ZONE : SMP_KEYS;
        if (cmd === CMD.SMP_GET) {
          const out = kind === 3 ? [m.mode] : vals;
          return [a[0], i, out.length, ...out.flatMap((v) => digits(v + 8192, 2))];
        }
        const v = (a[3] | a[4] << 7) - 8192;
        if (kind === 3) { m.mode = v ? 1 : 0; } else {
          if (a[2] >= vals.length) return [1];
          const d = defs[a[2]];
          vals[a[2]] = Math.max(d.min, Math.min(d.name === "SAMPLE" ? LOOPS.length - 1 : d.max, v));
        }
        this.rev++; this.dirty = 1;
        return [0];
      }
      case CMD.FM_GET: {
        const f = this.fm, n = FM_FACTORY.length + this.fmVoices().length;
        return [...f.voice, f.level, f.voices, ...f.macros.map((m) => m + 64), ...digits(f.preset, 2), ...digits(n, 2)];
      }
      case CMD.FM_SET: {
        const k = a[0] | a[1] << 7, v = (a[2] | a[3] << 7) - 8192, f = this.fm;
        if (k < 156) f.voice[k] = Math.max(0, Math.min(127, v));
        else if (k === 156) f.level = Math.max(0, Math.min(127, v));
        else if (k === 157) f.voices = Math.max(1, Math.min(6, v));
        else if (k <= 161) f.macros[k - 158] = Math.max(-50, Math.min(50, v));
        else return [1];
        this.rev++; this.dirty = 1;
        return [0];
      }
      case CMD.FM_LOAD: {
        const f = this.fm, pv = this.fmVoices();
        if (a.length >= 156) { f.voice = Uint8Array.from(a.slice(0, 156)); }
        else {
          const i = a[0] | a[1] << 7;
          if (i >= FM_FACTORY.length + pv.length) return [1];
          f.voice = i < FM_FACTORY.length ? fmInit(FM_FACTORY[i]) : fmUnpack(pv[i - FM_FACTORY.length].packed);
          f.preset = i;
        }
        this.rev++; this.dirty = 1;
        return [0];
      }
      case CMD.CHORDS: return CHORDS.flat().map((v) => v + 64);   // (0.28) the 15 chords, fixed
      case CMD.UI_STATE: return this.uiState();
      case CMD.UI_INPUT: return this.uiInput(a);
      case CMD.FM_PLAY: this.fmPlayed += a[1] ? 1 : 0; return [0];
      case CMD.WT_GET: {
        const w = this.wave;
        return [w.p.length, ...w.p.flatMap((v) => digits(v + 8192, 2)), ...digits(w.preset, 2), ...digits(WT_PRESETS.length, 2),
          ...digits(this.wtNames().length, 2), ...digits(WT_TABLES.length, 2)];
      }
      case CMD.WT_SET: {
        const k = a[0], v = (a[1] | a[2] << 7) - 8192;
        if (k >= WT_PRESETS[0][1].length) return [1];
        this.wave.p[k] = Math.max(wtMin(k), Math.min(wtMax(k, this.wtNames().length), v));
        this.rev++; this.dirty = 1;
        return [0];
      }
      case CMD.WT_LOAD: {
        const i = a[0] | a[1] << 7;
        if (i >= WT_PRESETS.length) return [1];
        this.wave = { p: WT_PRESETS[i][1].slice(), preset: i };
        this.rev++; this.dirty = 1;
        return [0];
      }
      case CMD.WT_PLAY: this.wtPlayed += a[1] ? 1 : 0; return [0];
      case CMD.JU_GET: {
        const j = this.juno;
        return [j.p.length, ...j.p.flatMap((v) => digits(v + 8192, 2)), ...digits(j.preset, 2), ...digits(JU_PRESETS.length, 2)];
      }
      case CMD.JU_SET: {
        const k = a[0], v = (a[1] | a[2] << 7) - 8192;
        if (k >= JU_PRESETS[0][1].length) return [1];
        this.juno.p[k] = Math.max(juMin(k), Math.min(juMax(k), v));
        this.rev++; this.dirty = 1;
        return [this.juno.p[k] === v ? 0 : 1];
      }
      case CMD.JU_LOAD: {
        const i = a[0] | a[1] << 7;
        if (i >= JU_PRESETS.length) return [1];
        this.juno = { p: JU_PRESETS[i][1].slice(), preset: i };
        this.rev++; this.dirty = 1;
        return [0];
      }
      case CMD.JU_PLAY: this.juPlayed += a[1] ? 1 : 0; return [0];
      case CMD.SONG_GET: {
        const from = a[0] || 0, cnt = Math.min(16, 64 - from), out = [this.song.n, this.song.on, from, cnt];
        for (let r = from; r < from + cnt; r++) out.push(...this.song.rows[r].pat, this.song.rows[r].bars);
        return out;
      }
      case CMD.SONG_SET: {
        const [n, on, from, cnt] = a;
        if (n < 1 || n > 64 || from + cnt > 64 || a.length < 4 + 9 * cnt) return [1];
        this.song.n = n; this.song.on = on ? 1 : 0;
        for (let r = 0; r < cnt; r++) {
          const q = a.slice(4 + 9 * r, 13 + 9 * r);
          this.song.rows[from + r] = { pat: q.slice(0, 8).map((p) => (p < 8 ? p : 127)), bars: Math.max(1, Math.min(16, q[8])) };
        }
        this.rev++; this.dirty = 1;
        return [0];
      }
      case CMD.SMP_PLAY:
        if (a[2]) this.smp2Played = (this.smp2Played || 0) + (a[1] ? 1 : 0);
        else this.smpPlayed = (this.smpPlayed || 0) + (a[1] ? 1 : 0);
        return [0];
      case CMD.SUB_PRESET: this.subPreset = a[0]; this.rev++; return [0];
      case CMD.PROJ: {
        const P = this.proj, nm = P.name.padEnd(12, "\0").slice(0, 12).split("").map((c) => c.charCodeAt(0) & 127);
        const used = this.slots.reduce((acc, sl) => acc + (sl.ok ? 512 + new DataView(sl.buf.buffer).getUint32(16, true) : 0), 0);
        const fxk = fxMem(P.fx[0][0], P.fx[1][0]);
        return [...nm, ...P.eng, ...P.send[0], ...P.send[1], ...P.ret, ...P.fx[0].slice(0, 4), ...P.fx[1].slice(0, 4), ...digits(used, 3),
          ...digits(2 * (0x14000 - 512) + 0x10000 - 512, 3), ...digits(fxk, 2), ...digits(FX_ARENA_KB, 2), 12, 8, FX_NAMES.length,
          ...P.fx[0].slice(4, 8), ...P.fx[1].slice(4, 8)];
      }
      case CMD.PROJ_SET: {
        const f = a[0], v = a[1] | a[2] << 7, P = this.proj;
        this.rev++; this.dirty = 1;
        if (f < 8) {
          if (v > 10) return [1];
          if (v !== 0 && v !== 4) { const u = P.eng.indexOf(v); if (u >= 0 && u !== f) P.eng[u] = P.eng[f]; }
          P.eng[f] = v;
          return [0];
        }
        if (f < 16) { P.send[0][f - 8] = Math.min(127, v); return [0]; }
        if (f < 24) { P.send[1][f - 16] = Math.min(127, v); return [0]; }
        if (f < 26) { P.ret[f - 24] = Math.min(127, v); return [0]; }
        if (f < 42) {
          const k = f < 34 ? (f < 30 ? 0 : 1) : (f < 38 ? 0 : 1), i = f < 34 ? (f - 26) % 4 : 4 + ((f - 34) % 4);
          if (i === 0) {
            if (v >= FX_NAMES.length) return [1];
            if (FX_KB[v] + FX_KB[P.fx[1 - k][0]] > FX_ARENA_KB) return [1];   // no memory for it beside the other
            P.fx[k] = [v, ...FX_DEFAULTS[v]];
          } else {
            const pr = fxParams(P.fx[k][0], P.fx[k].slice(1)).find((x) => x.i === i - 1);
            if (P.fx[k][0] === 10 && i === 1 && v % 6 !== P.fx[k][1] % 6)   // QUADRA: a new CONFIG, its own values
              P.fx[k].splice(3, 5, ...[[7, 40, 40, 64, 0], [20, 0, 0, 64, 0], [64, 64, 64, 64, 64], [7, 40, 60, 64, 0],
                [7, 30, 50, 64, 0], [7, 50, 64, 64, 0]][v % 6]);
            P.fx[k][i] = Math.min(pr ? pr.max : 0, v);
          }
          return [0];
        }
        return [1];
      }
      case CMD.PROJ_NAME: this.proj.name = String.fromCharCode(...a.slice(0, 11)) || "P1"; this.rev++; this.dirty = 1; return [0];
      // the slots as flash (as the FM-1's): BEGIN erases the first 4 KB sector, a write at a sector's start erases that
      // sector, a write only clears bits, END checks the data's CRC (0.31.2: so a send of only what changed is tested)
      case 11: { const sl = this.slots[a[0]]; if (!sl) return [a[0], 1]; sl.ok = false; sl.buf.fill(0xFF, 0, 4096); this.erases = (this.erases || 0) + 1; return [a[0], 0]; }
      case 12: {
        const sl = this.slots[a[0]], off = a[1] | a[2] << 7 | a[3] << 14, b = unpack7(a.slice(4));
        if (!sl || off < 512 || (off & 0xFF) || off + b.length > sl.buf.length) return [a[0], a[1], a[2], a[3], 1];
        if (sl.ok) return [a[0], a[1], a[2], a[3], 4];
        if (!(off & 0xFFF)) { sl.buf.fill(0xFF, off, off + 4096); this.erases = (this.erases || 0) + 1; }
        for (let i = 0; i < b.length; i++) sl.buf[off + i] &= b[i];
        this.writes = (this.writes || 0) + 1;
        return [a[0], a[1], a[2], a[3], 0];
      }
      case 13: {
        const sl = this.slots[a[0]], h = unpack7(a.slice(1)).slice(0, 480);
        if (!sl || h.length < 480) return [a[0], 1];
        const v = new DataView(Uint8Array.from(h).buffer), len = v.getUint32(16, true);
        if (512 + len > sl.buf.length) return [a[0], 2];
        if (crc32(sl.buf.subarray(512, 512 + len)) !== v.getUint32(20, true)) return [a[0], 3];
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
        const list = [[...PRESETS, ...USERS], LOOPS, ["OFF", "REESE", "BRK A", "BRK B", "MIDI", "SUB", "SAMPLER", "FM", "WAVE", "JUNO", "SAMPLER 2"], FX_NAMES,
          ["1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4", "1/2T", "1/4D", "1/2"], [...SUB_PRESETS, ...USERS],
          [...FM_FACTORY, ...this.fmVoices().map((v) => v.name)], this.wtNames(), WT_PRESETS.map((x) => x[0]), JU_PRESETS.map((x) => x[0])][a[0]] || [];
        const out = [list.length, a[1]];
        for (let i = a[1]; i < list.length; i++) out.push(...list[i].split("").map((c) => c.charCodeAt(0)), 0);
        return out;
      }
      default: return null;
    }
  }
  // the panel: the screen as UI_STATE sends it, the controls as UI_INPUT presses them
  uiState() {
    const U = this.ui, KN = [[0, 1, 2, 3], [4, 5, 6, 7], [8, 9, 10, 11], [12, 13, 14, 15], [16, 17, 18, -1]];
    const str = (t) => [...t.split("").map((c) => c.charCodeAt(0) & 127), 0];
    const seq = U.page === 7, out = [U.page, 0, 1, (this.playing ? 1 : 0) | (this.dirty ? 4 : 0) | (seq ? 32 : 0), 8,
      ...digits(this.params[18], 2), ...digits(this.playing ? this.step + 1 : 0, 2), ...str("REESE"), ...str(PRESETS[this.preset] || "CLASSIC RS")];
    const tabs = seq ? [] : ["OSC", "FILTER", "AMP", "MOD", "LFO", "OUT", "SETUP"];
    out.push(tabs.length, seq ? 127 : U.page < 4 ? U.page : 5);
    tabs.forEach((t) => out.push(...str(t)));
    for (let k = 0; k < 4; k++) {
      const i = seq ? -1 : KN[U.page][k];
      if (i < 0) { out.push(0); continue; }
      const p = i === 18 ? { name: "BPM", min: 60, max: 200 } : REESE[i], v = this.params[i];
      out.push(1, ...digits(100 + i, 2), ...str(p.name), ...digits(v + 8192, 2), ...digits(p.min + 8192, 2), ...digits(p.max + 8192, 2),
        ...str(p.text ? p.text(v) : `${v}`));
    }
    out.push(...str(U.msg), ...str(""), ...digits((1 << [1, 4, 2, 3, 5][Math.min(U.page, 4)]) | (seq ? 1 << 9 : 0), 2), ...digits(U.keys, 4), 0);
    return out;
  }
  uiInput(a) {
    const U = this.ui, [kind, id] = a, PAGE_OF = { 1: 0, 4: 1, 2: 2, 3: 3, 5: 4 };
    if (kind === 0) {
      U.btn |= 1 << id;
      if (id in PAGE_OF) U.page = PAGE_OF[id];
      else if (id === 9) { if (U.page === 7) U.page = U.back; else { U.back = U.page; U.page = 7; } }
      else if (id === 10) { this.playing = this.playing ? 0 : 1; this.step = 0; }
      else if (id === 7) { this.dirty = 0; this.saved++; }
    } else if (kind === 1) U.btn &= ~(1 << id);
    else if (kind === 2) U.keys |= 1 << id;
    else if (kind === 3) U.keys &= ~(1 << id);
    else if (kind === 4) {
      const d = a[2] - 64, KN = [[0, 1, 2, 3], [4, 5, 6, 7], [8, 9, 10, 11], [12, 13, 14, 15], [16, 17, 18, -1]];
      if (id === 0 && U.page !== 7) U.page = Math.max(0, Math.min(4, U.page + d));
      else if (id >= 3 && U.page !== 7) {
        const i = KN[U.page][id - 3];
        if (i >= 0) {
          const p = i === 18 ? { min: 60, max: 200 } : REESE[i];
          this.params[i] = Math.max(p.min, Math.min(p.max, this.params[i] + d));
          this.rev++; this.dirty = 1;
        }
      } else if (id === 2) { this.preset = (this.preset + d + PRESETS.length) % PRESETS.length; this.rev++; }
    } else if (kind === 5) { U.btn = 0; U.keys = 0; }
    else return [1];
    return [0];
  }
  // the project's DX7 voices: the zones of the slots marked DNFMBNK
  fmVoices() {
    const out = [];
    for (const sl of this.slots) {
      if (!sl.ok) continue;
      const h = parseSlot(sl.buf.subarray(0, SLOT_HDR_LEN));
      if (h) for (const z of h.zones) { const v = zoneVoices(sl.buf.subarray(SLOT_DATA_OFF + z.off, SLOT_DATA_OFF + z.off + ((z.n + 1) >> 1))); if (v) out.push(...v); }
    }
    return out;
  }
  // (0.32) WAVE's tables: the factory's, then the slots' zones that hold one (the FM-1 takes only those 2-byte aligned)
  wtNames() {
    const out = WT_TABLES.slice();
    this.wtOdd = 0;
    for (const sl of this.slots) {
      if (!sl.ok) continue;
      const h = parseSlot(sl.buf.subarray(0, SLOT_HDR_LEN));
      if (h) for (const z of h.zones) {
        const t = parseTable(sl.buf.subarray(SLOT_DATA_OFF + z.off, SLOT_DATA_OFF + z.off + ((z.n + 1) >> 1)));
        if (t && z.off % 2) this.wtOdd++;
        else if (t) out.push(t.name);
      }
    }
    return out;
  }
  close() { clearInterval(this.timer); }
}
