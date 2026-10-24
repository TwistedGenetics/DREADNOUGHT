// SPDX-License-Identifier: GPL-3.0-only
// The web editor's decoders (web/editor/protocol.js) on the firmware's own replies (dn_host `golden`): state,
// parameters, the pattern, names.
import { readFileSync } from "node:fs";
import { parseState, parseParams, parsePattern, parseNames, serializePattern, REESE, NPARAM_15, parseLfoPts, parseLfoNow, COND_NAMES,
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
console.log(fails ? `EDITOR (page): ${fails} FAILED` : "EDITOR (page): the page reads the firmware's replies");
process.exit(fails ? 1 : 0);
