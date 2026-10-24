// SPDX-License-Identifier: GPL-3.0-only
// The web editor's decoders (web/editor/protocol.js) on the firmware's own replies (dn_host `golden`): state,
// parameters, the pattern, names.
import { readFileSync } from "node:fs";
import { parseState, parseParams, parsePattern, parseNames, serializePattern, REESE } from "../../web/editor/protocol.js";

const g = JSON.parse(readFileSync(process.argv[2], "utf8"));
let fails = 0;
const check = (what, ok, got, want) => { console.log(`${ok ? "ok  " : "FAIL"} ${what}: ${got} (want ${want})`); fails += !ok; };
const st = parseState(g.state), pr = parseParams(g.params), pat = parsePattern(g.pattern), nm = parseNames(g.names);
check("state: tempo", st.bpm === g.bpm, st.bpm, g.bpm);
check("state: pattern size", st.patSize === g.pattern.length, st.patSize, g.pattern.length);
check("params: as many as the state says", pr.length === st.nparams && st.nparams === REESE.length + 1 + 16, pr.length, st.nparams);
check("params: REESE DTN", pr[1] === g.dtn, pr[1], g.dtn);
check("params: the tempo among them", pr[REESE.length] === g.bpm, pr[REESE.length], g.bpm);
check("pattern: the magic (DNQ2)", pat.magic === 0x32514E44, pat.magic.toString(16), "32514e44");
check("pattern: back to the same bytes", serializePattern(pat).every((b, i) => b === g.pattern[i]), "", "");
check("names: the REESE presets", nm.count === 9 && nm.names[0] === "CLASSIC RS", nm.names[0], "CLASSIC RS");
console.log(fails ? `EDITOR (page): ${fails} FAILED` : "EDITOR (page): the page reads the firmware's replies");
process.exit(fails ? 1 : 0);
