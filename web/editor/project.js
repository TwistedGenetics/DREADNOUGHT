// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT web editor: a whole project to and from the FM-1, and the .dnproj file (0.12). Copyright (C) 2026
// Jamie (TwistedGenetics).
//
// A project is everything the FM-1 holds for a piece of music: the sound (REESE, the breaks' settings, the LFOs and
// their drawn shapes, SAMPLER's pads and zones (0.23), FM's voice (0.25), WAVE's sound (0.32)), the tempo, each track's instrument, sends and the FX rack, all 8 patterns of all 8 tracks, the
// samples (the three sample slots, as stored) with their hand-set slices, and, for samples added on this computer, the
// original files and how they were converted, so a later save can convert them again from the originals.
//
// The file is JSON (binary parts in base64). The FM-1 must be stopped to read or write one: its patterns are reached by
// selecting each in turn, which while playing would only queue.
import { NLFO_ALL, nLfos, NPAT, TRACKS, parsePattern, serializePattern, PF, setEngines, readSampler, writeSampler, readFm, writeFm, readWave, writeWave, readJuno, writeJuno, readSong, writeSong, ST_CHORD, chordMatch } from "./protocol.js";
import { zoneVoices } from "./dx7.js";
import { parseTable } from "./wavetable.js";
import { readSlots, writeSlots, slotSamples, sameImage, imagesOf, slotMax } from "./media.js";
import { sliceKey, key7, pos7 } from "../breaks/slices.js";

export const FORMAT = "dreadnought-project", VERSION = 1;
export const b64 = (b) => {
  let s = "";
  for (let i = 0; i < b.length; i += 0x8000) s += String.fromCharCode(...b.subarray(i, i + 0x8000));
  return btoa(s);
};
export const unb64 = (s) => Uint8Array.from(atob(s), (c) => c.charCodeAt(0));

// a loop's hand-set slices on the FM-1 (ED_SLICE_GET 50): [starts] or null
async function slicesOf(link, adpcm, n) {
  try {
    const r = await link.req(50, key7(sliceKey(adpcm, n)), 1500), cnt = r[5] || 0;
    return cnt ? Array.from({ length: cnt }, (_, j) => r[6 + 3 * j] | r[7 + 3 * j] << 7 | r[8 + 3 * j] << 14) : null;
  } catch (_) { return null; }
}

// everything from the FM-1 -> a project object. media: the editor's sample list (originals and their settings)
export async function readProject(link, { media = [], onPart = () => {}, say = () => {} } = {}) {
  const st = await link.state();
  if (st.playing) throw new Error("Stop the FM-1 first (a project is read pattern by pattern).");
  say("Reading the sound…");
  const params = await link.params(), proj = await link.proj(), lfo = [];
  for (let k = 0; k < nLfos(params.length); k++) lfo.push(await link.lfoPts(k));   // (0.15: SUB's too)
  const smp = await readSampler(link);                 // (0.23; null before)
  const smp2 = await readSampler(link, 1);             // (0.36: SAMPLER 2; null before)
  const fm = await readFm(link);                       // (0.25; null before)
  const wave = await readWave(link);                   // (0.32; null before)
  const juno = await readJuno(link);                   // (0.36; null before)
  const song = await readSong(link);                   // (0.38; null before)
  say("Reading the patterns…");
  const pats = await link.pats(), cur = pats.tracks.map((t) => t.cur), patterns = [];
  try {
    for (let n = 0; n < NPAT; n++) {
      for (let t = 0; t < TRACKS; t++) await link.patSel(t, n);
      patterns.push(b64(serializePattern(await link.readPattern(st.patSize))));
      onPart(((n + 1) / NPAT) * 0.5);
    }
  } finally {
    for (let t = 0; t < TRACKS; t++) await link.patSel(t, cur[t]);   // (back to what was playing)
  }
  say("Reading the samples…");
  const dev = await readSlots(link, (f) => onPart(0.5 + f * 0.45)), slices = [];
  for (const s of slotSamples(dev)) {
    const sl = await slicesOf(link, s.adpcm, s.n);
    if (sl) slices.push({ key: sliceKey(s.adpcm, s.n), starts: sl });
  }
  onPart(1);
  return {
    format: FORMAT, version: VERSION, saved: new Date().toISOString(), firmware: link.name || "",
    name: proj.name, params, lfo, smp, smp2, fm, wave, juno, song, proj: { eng: proj.eng, send: proj.send, ret: proj.ret, fx: proj.fx },
    patterns, current: cur, patSize: st.patSize,
    slots: dev.map((im) => (im ? { hdr: b64(im.hdr), data: b64(im.data) } : null)), slices,
    media: media.map((m) => ({ name: m.name, set: m.set, lock: !!m.lock, file: m.orig ? b64(m.orig) : undefined })),
  };
}

// (0.29) a project saved from an FM-1 before 0.29: its loop numbers counted the two built-in breaks (ROLLER, STEPPER)
// first. BREAKS' LOOP (value 110) and SAMPLER's SAMPLE (each pad's and zone's first value) move down two; one on a
// built-in loop goes to the first loop (BREAKS) or none (SAMPLER). In place; the project.
export const fwBefore = (o, maj, min) => {
  const m = /(\d+)\.(\d+)/.exec((o && o.firmware) || "");
  return !!m && (+m[1] < maj || (+m[1] === maj && +m[2] < min));
};
export function upLoops(o) {
  if (!fwBefore(o, 0, 29) || o.loopsUp) return o;
  if (Array.isArray(o.params) && o.params.length > 110) o.params[110] = o.params[110] >= 2 ? o.params[110] - 2 : 0;
  if (o.smp) for (const g of [o.smp.pads, o.smp.zones]) for (const v of g || []) if (v && v.length) v[0] = v[0] >= 2 ? v[0] - 2 : -1;
  o.loopsUp = true;
  return o;
}

// (0.28) a 0.27 project's chords were its own shapes (o.chords: distances over the step's note); each step's becomes
// the chord of the 15 it is (none: the note alone), its note the chord's root
export function upChords(pat, shapes) {
  if (!Array.isArray(shapes)) return pat;
  for (const tr of pat.tracks)
    for (const st of tr.steps) {
      const c = (st.flags & ST_CHORD) >> 4;
      if (!c) continue;
      const m = chordMatch([60, ...(shapes[c - 1] || []).filter((d) => d > 0 && d < 60).map((d) => 60 + d)]);
      st.flags = (st.flags & ~ST_CHORD) | (m.c << 4);
      if (m.c) st.note = Math.max(0, Math.min(127, st.note + m.root - 60));
    }
  return pat;
}

export function checkProject(o) {
  if (!o || o.format !== FORMAT) throw new Error("That file is not a DREADNOUGHT project.");
  if (o.version > VERSION) throw new Error("That project was saved by a newer editor: reload the page to update it.");
  if (!Array.isArray(o.patterns) || o.patterns.length !== NPAT) throw new Error("The project's patterns are missing.");
  const eng = o.proj.eng;
  for (const e of [1, 2, 3, 5, 6, 7, 8, 9, 10]) if (eng.filter((x) => x === e).length > 1) throw new Error("The project has an instrument twice.");
  return o;
}

// a project object -> the FM-1. Sends only what differs for the samples (a cut-off load can simply be run again).
export async function writeProject(link, o, { onPart = () => {}, say = () => {} } = {}) {
  checkProject(o);
  if (!fwBefore({ firmware: link.name || "" }, 0, 29)) upLoops(o);   // (an FM-1 without the built-in breaks)
  const st = await link.state();
  if (st.playing) throw new Error("Stop the FM-1 first (a project is written pattern by pattern).");
  say("Sending the project's layout…");
  for (let t = 0; t < TRACKS; t++) await link.projSet(PF.eng(t), o.proj.eng[t]);   // (one REESE at most: they swap into place)
  setEngines(o.proj.eng);
  await link.projName(o.name || "PROJECT");
  for (let k = 0; k < 2; k++) {
    await link.projSet(PF.fx(k, 0), 0);                // (both off first: the memory is free for either)
  }
  for (let k = 0; k < 2; k++) {
    await link.projSet(PF.fx(k, 0), o.proj.fx[k].type);
    for (let i = 0; i < Math.min(7, o.proj.fx[k].p.length); i++) await link.projSet(PF.fx(k, i + 1), o.proj.fx[k].p[i]);
    await link.projSet(PF.ret(k), o.proj.ret[k]);
  }
  for (let t = 0; t < TRACKS; t++) {
    await link.projSet(PF.sendA(t), o.proj.send[0][t]);
    await link.projSet(PF.sendB(t), o.proj.send[1][t]);
  }
  say("Sending the sound…");
  const n = Math.min(o.params.length, (await link.params()).length);
  for (let i = 0; i < n; i++) await link.setParam(i, o.params[i]);
  const nl = nLfos((await link.params()).length);
  for (let k = 0; k < Math.min(NLFO_ALL, nl) && o.lfo && o.lfo[k]; k++) await link.setLfoPts(k, o.lfo[k].npts, o.lfo[k].pts);
  if (o.smp && (await readSampler(link))) await writeSampler(link, o.smp);   // (an FM-1 before 0.23: no SAMPLER)
  if (o.smp2 && (await readSampler(link, 1))) await writeSampler(link, o.smp2, 1);   // (0.36)
  if (o.fm && (await readFm(link))) await writeFm(link, o.fm);   // (before 0.25: no FM)
  onPart(0.1);
  say("Sending the patterns…");
  for (let p = 0; p < NPAT; p++) {
    for (let t = 0; t < TRACKS; t++) await link.patSel(t, p);
    const pat = upChords(parsePattern([...unb64(o.patterns[p])]), o.chords);
    if (st.patSize && serializePattern(pat).length !== st.patSize) pat.stepBytes = st.patSize > 3000 ? 8 : 4;
    await link.writePattern(pat);
    onPart(0.1 + ((p + 1) / NPAT) * 0.3);
  }
  for (let t = 0; t < TRACKS; t++) await link.patSel(t, (o.current && o.current[t]) || 0);
  say("Sending the samples…");
  const dev = await readSlots(link);
  let want = (o.slots || []).map((s) => (s ? { hdr: unb64(s.hdr), data: unb64(s.data) } : null));
  while (want.length < 3) want.push(null);
  if (want.some((w, k) => w && w.data.length > slotMax(k))) {   // (0.29: slot 3 is 64 KB: the samples packed again)
    want = imagesOf(slotSamples(want).map(({ adpcm, n, rate }) => ({ adpcm, n, rate })));
    say("Slot 3 holds 64 KB now: the samples were packed again to fit.");
  }
  if (!want.every((w, k) => sameImage(w, dev[k]))) await writeSlots(link, want, dev, (f) => onPart(0.4 + f * 0.55), say);
  if (o.wave && (await readWave(link))) await writeWave(link, o.wave);   // (0.32: after the slots: its table may be one of theirs)
  if (o.juno && (await readJuno(link))) await writeJuno(link, o.juno);   // (0.36)
  if (o.song && (await readSong(link))) await writeSong(link, o.song);   // (0.38)
  for (const sl of o.slices || []) {
    const r = await link.req(51, [...key7(sl.key), sl.starts.length, ...sl.starts.flatMap(pos7)], 5000).catch(() => null);
    if (!r) break;
  }
  onPart(1);
  say("Sent.");
}

// the samples a project file carries, for the media list: from the slots (as stored), with the originals and their
// settings where the file has them (matched in order: the slots hold the samples in the media list's order)
export function mediaOf(o) {
  const dev = (o.slots || []).map((s) => (s ? { hdr: unb64(s.hdr), data: unb64(s.data) } : null));
  return slotSamples(dev).map((s, i) => {
    const m = (o.media || [])[i], dx = zoneVoices(s.adpcm), wt = parseTable(s.adpcm);
    if (wt) return { name: (m && m.name) || wt.name, wt: s.adpcm, adpcm: s.adpcm, n: s.n, rate: s.rate, sent: true };   // (0.32: a wavetable)
    if (dx) return { name: (m && m.name) || "DX7 voices", dx7: dx.map((v) => ({ ...v, on: true })), adpcm: s.adpcm, n: s.n, rate: s.rate, sent: true };
    return { name: (m && m.name) || `Sample ${i + 1}`, adpcm: s.adpcm, n: s.n, rate: s.rate, set: (m && m.set) || { rate: s.rate, fold: "mix" },
      lock: !!(m && m.lock), orig: m && m.file ? unb64(m.file) : null, sent: true };
  });
}
