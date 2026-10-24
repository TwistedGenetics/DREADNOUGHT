// SPDX-License-Identifier: GPL-3.0-only
// The breaks page's slicing (web/breaks/slices.js) against the firmware (tests/host/slices_xcheck.c's JSON on stdin):
// the same loop key, the same AUTO slices, and the slice table's 7-bit packing round trip.
import { readFileSync } from "node:fs";
import { sliceKey, autoSlices, gridSlices, key7, pos7 } from "../../web/breaks/slices.js";
import { imaDecode } from "../../x0x-firmware/web/x0x_breaks.js";

const loops = JSON.parse(readFileSync(0, "utf8"));
let fails = 0;
const check = (what, ok, got, want) => { console.log(`${ok ? "ok  " : "FAIL"} ${what}: ${got} (want ${want})`); fails += !ok; };
for (const L of loops) {
  const adpcm = Uint8Array.from(L.adpcm.match(/../g).map((h) => parseInt(h, 16)));
  check("page key = firmware key", sliceKey(adpcm, L.n) === L.key, sliceKey(adpcm, L.n), L.key);
  const s = imaDecode(adpcm, L.n), a = autoSlices(s);
  check("page Auto = firmware AUTO", JSON.stringify(a) === JSON.stringify(L.auto), a.length, L.auto.length);
  const k7 = key7(L.key), back = (k7[0] | k7[1] << 7 | k7[2] << 14 | k7[3] << 21 | k7[4] * 2 ** 28) >>> 0;
  check("key in 7-bit bytes and back", back === L.key && k7.every((b) => b < 128), back, L.key);
  const g = gridSlices(L.n, 2), p = g.flatMap(pos7), un = g.map((_, j) => p[3 * j] | p[3 * j + 1] << 7 | p[3 * j + 2] << 14);
  check("slice places in 7-bit bytes and back", JSON.stringify(un) === JSON.stringify(g), un.length, g.length);
}
console.log(fails ? `SLICES (page): ${fails} FAILED` : "SLICES (page): the page slices as the firmware does");
process.exit(fails ? 1 : 0);
