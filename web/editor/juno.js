// SPDX-License-Identifier: GPL-3.0-only
// JUNO (0.36): the Juno-60 / 106-style polysynth's values as the FM-1 numbers, ranges and shows them (src/dsp/dn_ju.h
// JP_*, dn_ju.c's names and texts; tests/host/editor_golden.mjs checks them against the firmware's).
export const JU = ["RANGE", "SAW", "PULSE", "SUB", "PWM", "PWM SRC", "NOISE", "LFO",
  "CUT", "RES", "ENV", "KEY", "HPF", "ENV POL", "LFO", "VEL", "ATK", "DEC", "SUS", "REL", "VCA", "VEL",
  "RATE", "DELAY", "SYNC", "MODE", "CHORUS", "LEVEL", "VOICES", "TRANS"];
export const JP = Object.freeze({ RANGE: 0, SAW: 1, PULSE: 2, SUB: 3, PWM: 4, PWMSRC: 5, NOISE: 6, DLFO: 7, CUT: 8, RES: 9,
  FENV: 10, FKEY: 11, HPF: 12, FPOL: 13, FLFO: 14, FVEL: 15, ATK: 16, DEC: 17, SUS: 18, REL: 19, VCA: 20, VVEL: 21,
  LRATE: 22, LDELAY: 23, LSYNC: 24, LMODE: 25, CHORUS: 26, LEVEL: 27, VOICES: 28, TRANS: 29 });
const MIN = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -24];
const MAX = [2, 1, 1, 127, 127, 2, 127, 127, 127, 127, 127, 127, 3, 1, 127, 127, 127, 127, 127, 127, 1, 127, 127, 127, 12, 1,
  3, 127, 6, 24];
export const juMin = (k) => MIN[k];
export const juMax = (k) => MAX[k];
const DIVN = ["1/16", "1/8", "1/8D", "1/4", "1/4D", "1/2", "1/2D", "1 BAR", "1.5 BR", "2 BAR", "3 BAR", "4 BAR"];
export function juText(k, v) {
  v = Math.max(MIN[k], Math.min(MAX[k], v));
  switch (k) {
    case JP.RANGE: return ["16'", "8'", "4'"][v];
    case JP.SAW: case JP.PULSE: return v ? "ON" : "OFF";
    case JP.PWMSRC: return ["LFO", "MAN", "ENV"][v];
    case JP.HPF: return ["BOOST", "FLAT", "2", "3"][v];
    case JP.FPOL: return v ? "INV" : "NORM";
    case JP.VCA: return v ? "GATE" : "ENV";
    case JP.LSYNC: return v ? DIVN[v - 1] : "OFF";
    case JP.LMODE: return v ? "KEY" : "FREE";
    case JP.CHORUS: return ["OFF", "I", "II", "I+II"][v];
    case JP.TRANS: return v > 0 ? `+${v}` : `${v}`;
    default: return `${v}`;
  }
}
// the page's groups, as the FM-1's pages lay them out
export const JU_GROUPS = [
  ["DCO", [JP.RANGE, JP.SAW, JP.PULSE, JP.SUB, JP.PWM, JP.PWMSRC, JP.NOISE, JP.DLFO]],
  ["VCF", [JP.CUT, JP.RES, JP.FENV, JP.FKEY, JP.HPF, JP.FPOL, JP.FLFO, JP.FVEL]],
  ["ENV / VCA", [JP.ATK, JP.DEC, JP.SUS, JP.REL, JP.VCA, JP.VVEL]],
  ["LFO", [JP.LRATE, JP.LDELAY, JP.LSYNC, JP.LMODE]],
  ["OUT", [JP.CHORUS, JP.LEVEL, JP.VOICES, JP.TRANS]],
];
