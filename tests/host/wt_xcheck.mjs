// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT (0.32): the editor's wavetable converter (web/editor/wavetable.js) against tools/wt_convert.py, on the
// WAVs tests/host/wt_xcheck.py wrote: the same header, every sample within 1 of the tool's.
import { readFileSync } from "node:fs";
import { fromWav, parseTable } from "../../web/editor/wavetable.js";

const dir = process.argv[2];
let fails = 0;
for (const c of JSON.parse(readFileSync(`${dir}/cases.json`, "utf8"))) {
  const want = new Uint8Array(readFileSync(`${dir}/${c.name}.dnwt`));
  const got = fromWav(new Uint8Array(readFileSync(`${dir}/${c.name}.wav`)), { frames: c.frames, name: c.name });
  const hdr = got.length === want.length && got.subarray(0, 16).every((b, i) => b === want[i]);
  let worst = 0, off = 0;
  if (hdr) {
    const a = new DataView(got.buffer), b = new DataView(want.buffer);
    for (let i = 16; i < got.length; i += 2) {
      const d = Math.abs(a.getInt16(i, true) - b.getInt16(i, true));
      worst = Math.max(worst, d);
      off += d > 0;
    }
  }
  const ok = hdr && worst <= 1 && !!parseTable(got);
  console.log(`${ok ? "ok  " : "FAIL"} ${c.name}: ${parseTable(got) ? parseTable(got).frames : "?"} frames, header ${hdr ? "same" : "differs"}, worst ${worst} (${off} samples off by one)`);
  fails += !ok;
}
console.log(fails ? "WAVETABLES: FAILED" : "WAVETABLES: the editor's tables are the tool's");
process.exit(fails ? 1 : 0);
