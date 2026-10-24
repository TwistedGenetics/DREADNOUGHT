// SPDX-License-Identifier: GPL-3.0-only
// The web editor's decoders (web/editor/protocol.js) on the firmware's own replies (dn_host `golden`): state,
// parameters, the pattern, names.
import { readFileSync } from "node:fs";
import { unpack as fmUnpack, pack as fmPack, vname as fmName, vmax as fmMax, vtext as fmText, isCarrier } from "../../web/editor/dx7.js";
import { parseSmp, SMP_PAD, SMP_ZONE, SMP_KEYS, parseState, parseParams, parsePattern, parseNames, serializePattern, REESE, NPARAM_15, parseLfoPts, parseLfoNow, COND_NAMES,
  parseProj, fxParams, FX_DEFAULTS, FX_NAMES } from "../../web/editor/protocol.js";

const g = JSON.parse(readFileSync(process.argv[2], "utf8"));
let fails = 0;
const check = (what, ok, got, want) => { console.log(`${ok ? "ok  " : "FAIL"} ${what}: ${got} (want ${want})`); fails += !ok; };
const st = parseState(g.state), pr = parseParams(g.params), pat = parsePattern(g.pattern), nm = parseNames(g.names);
check("state: tempo", st.bpm === g.bpm, st.bpm, g.bpm);
check("state: pattern size", st.patSize === g.pattern.length, st.patSize, g.pattern.length);
check("params: as many as the state says", pr.length === st.nparams && st.nparams === NPARAM_15, pr.length, st.nparams);
check("params: REESE DTN", pr[1] === g.dtn, pr[1], g.dtn);
check("params: the tempo among them", pr[REESE.length] === g.bpm, pr[REESE.length], g.bpm);
check("pattern: the magic (DNQ3)", pat.magic === 0x33514E44, pat.magic.toString(16), "33514e44");
check("pattern: back to the same bytes", serializePattern(pat).every((b, i) => b === g.pattern[i]), "", "");
check("names: the REESE presets", nm.count === 18 && nm.names[0] === "CLASSIC RS", nm.names[0], "CLASSIC RS");
const lp = parseLfoPts(g.lfopts), ln = parseLfoNow(g.lfonow);
check("LFO points: LFO 1, its count", lp.lfo === 0 && lp.npts === g.lfonpts, lp.npts, g.lfonpts);
check("LFO points: point 5", lp.pts[4] === g.lfopt5, lp.pts[4], g.lfopt5);
check("LFOs now: 8 (REESE's, SUB's), phases 0..1", ln.length === 8 && ln.every((x) => x.phase >= 0 && x.phase < 1), ln.length, 8);
check("trig conditions: the page names them as the FM-1 does (all 103)", g.conds.length === COND_NAMES.length && g.conds.every((c, i) => c === COND_NAMES[i]), g.conds.slice(30, 36).join(" "), COND_NAMES.slice(30, 36).join(" "));
check("the device glue takes every editor frame whole (60-72)", g.long.every((x) => x === 1), g.long.join(""), "1".repeat(13));
// 0.16: the project's effects, seven values each; every effect's values named, topped and read as the FM-1 does
const pj = parseProj(g.proj);
check("PROJ: FX A DELAY, FX B OFF, seven values each", pj.fx[0].type === 1 && pj.fx[1].type === 0 && pj.fx[0].p.length === 7,
  `${pj.fx[0].type} ${pj.fx[1].type} ${pj.fx[0].p.length}`, "1 0 7");
check("PROJ: as many effects as the page knows", pj.nFx === FX_NAMES.length, pj.nFx, FX_NAMES.length);
let bad = [];
for (const [t, i, name, max, text] of g.fxp) {
  const d = FX_DEFAULTS[t], ps = fxParams(t, d), x = ps.find((q) => q.i === i);
  if (name === "-") { if (x) bad.push(`${FX_NAMES[t]} ${i}: the page shows ${x.name}`); continue; }
  if (!x || x.name !== name || x.max !== max || x.text(d[i]) !== text)
    bad.push(`${FX_NAMES[t]} ${i}: ${x ? `${x.name} ${x.max} ${x.text(d[i])}` : "none"} vs ${name} ${max} ${text}`);
}
check("every effect's values: names, tops and texts as the FM-1's", !bad.length, bad.slice(0, 3).join("; ") || "all", "all");
const hi = parseParams(g.hi);
check("PARAMS_HI: 8 values, REESE's filter envelope first", hi.length === 8 && hi.slice(0, 4).join() === g.rfe.join(),
  hi.slice(0, 4).join(), g.rfe.join());
// 0.23: SAMPLER: pad 1 as the firmware sends it; every value named, ranged and read as the FM-1 does
const sg = parseSmp(g.smpget);
check("SMP_GET: pad 1, 16 values, the defaults", sg.kind === 0 && sg.idx === 0 && sg.vals.join() === "-1,0,1000,0,0,0,100,0,0,127,64,0,127,0,0,0",
  sg.vals.join(), "-1,0,1000,0,0,0,100,0,0,127,64,0,127,0,0,0");
bad = [];
for (const [kind, k, name, min, max, texts] of g.smpdefs) {
  const x = [SMP_PAD, SMP_ZONE, SMP_KEYS][kind][k];
  if (!x || x.name !== name || x.min !== min || x.max !== max) { bad.push(`${kind}.${k}: ${x && x.name} ${x && x.min}..${x && x.max} vs ${name} ${min}..${max}`); continue; }
  if (name === "SAMPLE") continue;                   // (the page names the loop)
  for (const [v, t] of texts) if ((x.text ? x.text(v) : `${v}`) !== t) { bad.push(`${name} ${v}: ${x.text ? x.text(v) : v} vs ${t}`); break; }
}
check("SAMPLER's values: names, ranges and texts as the FM-1's", !bad.length, bad.slice(0, 3).join("; ") || "all", "all");
// 0.25: FM: every value named, topped and read as the FM-1 does; a voice unpacked and packed the same; the carriers
bad = [];
for (const [k, name, max, texts] of g.fmdefs) {
  if (fmName(k) !== name || fmMax(k) !== max) { bad.push(`${k}: ${fmName(k)} ${fmMax(k)} vs ${name} ${max}`); continue; }
  texts.forEach((t, v) => { if (fmText(k, v) !== t && bad.length < 5) bad.push(`${name} ${v}: ${fmText(k, v)} vs ${t}`); });
}
check("FM's values: names, tops and texts as the FM-1's (145)", !bad.length, bad.slice(0, 3).join("; ") || "all", "all");
const fu = fmUnpack(Uint8Array.from(g.fmpacked)), fp = fmPack(Uint8Array.from(g.fmvoice));
check("FM: the FM-1's packed voice unpacks to its voice here, and packs back", fu.every((x, i) => x === g.fmvoice[i]) && fp.every((x, i) => x === g.fmpacked[i]), "", "");
check("FM: the carriers of all 32 algorithms as the FM-1's", g.fmcarriers.every((c, i) => (isCarrier(Math.floor(i / 6), 5 - (i % 6)) ? 1 : 0) === c), "", "");
console.log(fails ? `EDITOR (page): ${fails} FAILED` : "EDITOR (page): the page reads the firmware's replies");
process.exit(fails ? 1 : 0);
