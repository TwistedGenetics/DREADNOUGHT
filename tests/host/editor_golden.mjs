// SPDX-License-Identifier: GPL-3.0-only
// The web editor's decoders (web/editor/protocol.js) on the firmware's own replies (dn_host `golden`): state,
// parameters, the pattern, names.
import { readFileSync } from "node:fs";
import { parseState, parseParams, parsePattern, parseNames, serializePattern, REESE, NPARAM, parseLfoPts, parseLfoNow, COND_NAMES } from "../../web/editor/protocol.js";

const g = JSON.parse(readFileSync(process.argv[2], "utf8"));
let fails = 0;
const check = (what, ok, got, want) => { console.log(`${ok ? "ok  " : "FAIL"} ${what}: ${got} (want ${want})`); fails += !ok; };
const st = parseState(g.state), pr = parseParams(g.params), pat = parsePattern(g.pattern), nm = parseNames(g.names);
check("state: tempo", st.bpm === g.bpm, st.bpm, g.bpm);
check("state: pattern size", st.patSize === g.pattern.length, st.patSize, g.pattern.length);
check("params: as many as the state says", pr.length === st.nparams && st.nparams === NPARAM, pr.length, st.nparams);
check("params: REESE DTN", pr[1] === g.dtn, pr[1], g.dtn);
check("params: the tempo among them", pr[REESE.length] === g.bpm, pr[REESE.length], g.bpm);
check("pattern: the magic (DNQ3)", pat.magic === 0x33514E44, pat.magic.toString(16), "33514e44");
check("pattern: back to the same bytes", serializePattern(pat).every((b, i) => b === g.pattern[i]), "", "");
check("names: the REESE presets", nm.count === 9 && nm.names[0] === "CLASSIC RS", nm.names[0], "CLASSIC RS");
const lp = parseLfoPts(g.lfopts), ln = parseLfoNow(g.lfonow);
check("LFO points: LFO 1, its count", lp.lfo === 0 && lp.npts === g.lfonpts, lp.npts, g.lfonpts);
check("LFO points: point 5", lp.pts[4] === g.lfopt5, lp.pts[4], g.lfopt5);
check("LFOs now: 4, phases 0..1", ln.length === 4 && ln.every((x) => x.phase >= 0 && x.phase < 1), ln.length, 4);
check("trig conditions: the page names them as the FM-1 does (all 103)", g.conds.length === COND_NAMES.length && g.conds.every((c, i) => c === COND_NAMES[i]), g.conds.slice(30, 36).join(" "), COND_NAMES.slice(30, 36).join(" "));
check("the device glue takes every editor frame whole (60-72)", g.long.every((x) => x === 1), g.long.join(""), "1".repeat(13));
console.log(fails ? `EDITOR (page): ${fails} FAILED` : "EDITOR (page): the page reads the firmware's replies");
process.exit(fails ? 1 : 0);
