// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT web editor: the page. Copyright (C) 2026 Jamie (TwistedGenetics).
import {
  connect, REESE, BPM_INDEX, brkParams, brkIndex, KIND, ST_ON, ST_SLIDE, ST_ACC, LEN_STEP, LEN_MAX, noteEnd, lenText,
  noteName, parsePattern, serializePattern, overlapCut, setOverlapCut, overlapSlide, HIDDEN, NLFO, LFO_PTS, lfoParams,
  lfoIndex, NPARAM, lfoShapeAt, COND_NAMES, RTIM_DEN, ST_RTRG, NPAT, PAT_BYTES, ENG_NAMES, setEngines, brkOf, midiNo,
  PF, FX_NAMES, fxParams, E, SUB, subIndex, NPARAM_13, nLfos, FENV, NPARAM_19, feIndex,
} from "./protocol.js";
import {
  readWav, convert, costAt, seconds, fitToSpace, packPlan, imaEncode, imaDecode, imagesOf, readSlots, writeSlots,
  slotSamples, CAP, RATES, RATE_NAME, FOLDS,
} from "./media.js";
import { readProject, writeProject, checkProject, mediaOf, FORMAT } from "./project.js";

const $ = (id) => document.getElementById(id);
const S = {
  link: null, mock: null, st: null, params: [], presets: [], loops: [], pat: null, sel: 0, lo: 36, playStep: -1,
  writing: 0, lastRev: -1, poll: null, dirtyPat: false, drag: null, selNote: -1, zoom: 0, x0: 0,
  lfo: null, lfoNow: null, lfoDraw: null, pats: null,
  proj: null, media: [], dev: null, mediaRead: false, dir: null,
};
const ROWS = 36, ROW_H = 14, GUT = 46;

function say(t, cls = "") { $("status").textContent = t; $("status").className = cls; }
async function guard(fn) {
  try { return await fn(); } catch (e) { say(e.message || String(e), "bad"); return undefined; }
}

// ---- connecting -------------------------------------------------------------------------------------------------
async function start(link, label) {
  S.link = link;
  say("Reading…");
  S.st = await link.state();
  S.presets = await link.names(0);
  S.subPresets = await link.names(5).catch(() => []);
  S.loops = await link.names(1);
  S.params = await link.params();
  S.pat = await link.readPattern(S.st.patSize);
  S.lfo = await readLfos();
  S.proj = await link.proj().catch(() => null);      // (0.12: the project; older firmware has none)
  if (S.proj) setEngines(S.proj.eng);
  S.media = []; S.dev = null; S.mediaRead = false;
  S.lastRev = S.st.rev;
  S.sel = 0;
  fit();
  buildSound();
  buildBreaks();
  buildLfos();
  buildSub();
  buildProject();
  ["play", "bpm", "save"].forEach((id) => { $(id).disabled = false; });
  say(label, "ok");
  render();
  clearInterval(S.poll);
  S.poll = setInterval(poll, 250);
}

// the Diagnostics panel: the ports seen, which one, every frame (hex) and timeout; the picked port remembered
const DIAG = [];
function diag(line) {
  DIAG.push(`${new Date().toISOString().slice(11, 23)} ${line}`);
  if (DIAG.length > 2000) DIAG.splice(0, DIAG.length - 2000);
  if ($("diag").open) { $("diaglog").textContent = DIAG.join("\n"); $("diaglog").scrollTop = 1e9; }
}
const hex = (d) => [...d].map((x) => x.toString(16).padStart(2, "0")).join(" ");
function frameTap(dir, d) {
  if (dir === "!") return diag(`timeout: no answer to command ${d[0]}`);
  if (d[4] === 60 || d[4] === 72 || d[4] === 75) return;   // (the 4 polls a second: not logged)
  if (d[4] === 12 || d[4] === 16 || d[4] === 63 || d[4] === 64) {   // (sample and pattern transfers: a line a 32)
    S.bulk = (S.bulk || 0) + 1;
    if (S.bulk % 32 !== 1) return;
  }
  diag(`${dir === ">" ? "sent" : "got "} ${d.length > 40 ? `${hex(d.slice(0, 40))} … (${d.length} bytes)` : hex(d)}`);
}
const PORT_KEY = "dreadnought-port";
const prefPort = () => { try { return localStorage.getItem(PORT_KEY) || ""; } catch (_) { return ""; } };
$("diag").ontoggle = () => { $("diaglog").textContent = DIAG.join("\n"); };
$("port").onchange = () => { try { localStorage.setItem(PORT_KEY, $("port").value); } catch (_) { /* (private window) */ } };
$("ports").onclick = () => guard(async () => {
  if (!navigator.requestMIDIAccess) throw new Error("This browser can't talk to MIDI devices. Use Chrome or Edge.");
  const midi = await navigator.requestMIDIAccess({ sysex: true }), sel = $("port"), keep = prefPort();
  sel.length = 1;
  for (const i of midi.inputs.values()) {
    const o = document.createElement("option");
    o.value = o.textContent = i.name;
    sel.appendChild(o);
  }
  sel.value = [...sel.options].some((o) => o.value === keep) ? keep : "";
  diag(`ports: in ${[...midi.inputs.values()].map((i) => `"${i.name}" (${i.state})`).join(", ") || "none"}; ` +
    `out ${[...midi.outputs.values()].map((o) => `"${o.name}" (${o.state})`).join(", ") || "none"}`);
});
$("copydiag").onclick = () => {
  const t = [`DREADNOUGHT editor diagnostics, ${new Date().toISOString()}`, navigator.userAgent, ...DIAG].join("\n");
  (navigator.clipboard ? navigator.clipboard.writeText(t) : Promise.reject()).then(() => say("Diagnostics copied.", "ok"),
    () => say("Couldn't copy: select the text in Diagnostics instead.", "bad"));
};
$("connect").onclick = () => guard(async () => {
  if (!navigator.requestMIDIAccess) throw new Error("This browser can't talk to MIDI devices. Use Chrome or Edge.");
  say("Connecting…");
  let midi;
  try { midi = await navigator.requestMIDIAccess({ sysex: true }); } catch (e) {
    diag(`MIDI access refused: ${e.name} ${e.message}`);
    throw new Error("Chrome said no to MIDI with SysEx. Click the icon at the left of the address bar, allow MIDI devices, and reload.");
  }
  S.connLog = [];
  diag(`connect (port picked: ${prefPort() || "none"})`);
  try {
    const link = await connect(midi, (m) => { S.connLog.push(m); diag(m); }, prefPort(), frameTap);
    await start(link, `Connected: ${link.name}`);
  } catch (e) { diag(`failed: ${e.message}`); $("diag").open = true; throw e; }
});
$("demo").onclick = () => guard(async () => {
  const { MockFM1 } = await import("./mock-fm1.js");
  if (S.mock) S.mock.close();
  S.mock = new MockFM1();
  const link = await connect(S.mock.access, diag, "", frameTap);
  await start(link, "Demo: a simulated FM-1 (nothing is sent to a real one)");
});

// the FM-1's state: the playhead, and whatever changed on the box (its knobs, keys, presets)
async function poll() {
  if (!S.link || S.writing || S.drag) return;
  const st = await S.link.state().catch(() => null);
  if (!st) { say("The FM-1 stopped answering. Connect again.", "bad"); clearInterval(S.poll); return; }
  const changed = st.rev !== S.lastRev;
  S.st = st;
  $("play").classList.toggle("on", !!st.playing);
  $("play").textContent = st.playing ? "■" : "▶";
  $("save").classList.toggle("dirty", !!st.dirty);
  if (document.activeElement !== $("bpm")) $("bpm").value = st.bpm;
  if (changed && !S.dirtyPat) {
    S.lastRev = st.rev;
    if (st.loops !== S.loops.length) { S.loops = await S.link.names(1); buildBreaks(); }
    S.params = await S.link.params();
    S.pat = await S.link.readPattern(st.patSize);
    if (!S.lfoDraw) S.lfo = await readLfos();
    if (S.proj) { S.proj = await S.link.proj().catch(() => S.proj); setEngines(S.proj.eng); }
    refreshValues();
    refreshProject();
  } else if (S.proj && $("files").classList.contains("on")) {
    S.proj = await S.link.proj().catch(() => S.proj);   // (the CPU and the memory, live)
    refreshMeters();
  }
  if (S.lfo && $("lfos").classList.contains("on")) S.lfoNow = await S.link.lfoNow().catch(() => null);
  if (S.st.patSize >= PAT_BYTES) S.pats = await S.link.pats().catch(() => null);   // (0.11: 8 patterns a track)
  const tr = S.pat.tracks[S.sel];
  S.playStep = st.playing && st.step ? (st.step - 1) % tr.len : -1;
  render();
}

// ---- writing --------------------------------------------------------------------------------------------------
let patTimer = null;
function patternChanged() {
  S.dirtyPat = true;
  render();
  clearTimeout(patTimer);
  patTimer = setTimeout(() => guard(async () => {
    S.writing++;
    try {
      await S.link.writePattern(S.pat);
      S.dirtyPat = false;
      S.lastRev = (await S.link.state()).rev;
    } finally { S.writing--; }
  }), 200);
}
const paramTimers = {};
function setParam(i, v) {
  S.params[i] = v;
  if ((i >= lfoIndex(0, 0) && i < lfoIndex(NLFO, 0) && (i - lfoIndex(0, 0)) % 8 === 1) ||
      (i >= lfoIndex(NLFO, 0) && (i - lfoIndex(NLFO, 0)) % 8 === 1)) {   // SYNC resets RATE (as the FM-1 does)
    S.params[i + 1] = v ? 7 : 64;
    setTimeout(refreshValues, 0);
  }
  clearTimeout(paramTimers[i]);
  paramTimers[i] = setTimeout(() => guard(async () => {
    S.writing++;
    try { await S.link.setParam(i, v); S.lastRev = (await S.link.state()).rev; } finally { S.writing--; }
  }), 40);
}

$("play").onclick = () => guard(async () => { await S.link.transport(!S.st.playing); });
$("bpm").onchange = () => { const v = Math.max(60, Math.min(200, +$("bpm").value || 172)); setParam(BPM_INDEX, v); };
$("save").onclick = () => guard(async () => { await S.link.save(); say("Kept on the FM-1.", "ok"); });

// ---- tabs ----------------------------------------------------------------------------------------------------
document.querySelectorAll(".tabs button").forEach((b) => b.onclick = () => {
  document.querySelectorAll(".tabs button").forEach((x) => x.setAttribute("aria-selected", x === b));
  document.querySelectorAll(".tab").forEach((t) => t.classList.toggle("on", t.id === b.dataset.tab));
  if (b.dataset.tab === "files" && S.link && S.proj && !S.mediaRead) guard(readMedia);
  render();
});

// ---- the pattern: tracks ----------------------------------------------------------------------------------------
function trackLabel(t) {
  const k = KIND(t);
  return k === "REESE" ? ["REESE", "bass"] : k === "SUB" ? ["SUB", "bass"] : k === "BREAKS" ? [brkOf(t) ? "BREAKS B" : "BREAKS A", "slices"]
    : k === "MIDI" ? [`MIDI ${midiNo(t)}`, `channel ${S.pat.tracks[t].chan + 1}`] : ["OFF", "plays nothing"];
}
function renderTracks() {
  const box = $("tracks");
  box.textContent = "";
  S.pat.tracks.forEach((tr, t) => {
    const [k, sub] = trackLabel(t), on = tr.steps.slice(0, tr.len).filter((s) => s.flags & ST_ON).length;
    const b = document.createElement("div");
    b.className = "trk" + (t === S.sel ? " sel" : "");
    b.innerHTML = `<span class="n">${t + 1}</span><span class="k">${k}<small>${sub} · ${on} notes · ${tr.len} steps</small></span>
      <button class="m${tr.mute ? " on" : ""}" title="Mute">M</button>`;
    b.onclick = () => { S.sel = t; S.selNote = -1; S.x0 = 0; fit(); render(); };
    b.querySelector(".m").onclick = (ev) => { ev.stopPropagation(); tr.mute = tr.mute ? 0 : 1; patternChanged(); };
    box.appendChild(b);
  });
}
$("overlap").onchange = () => { setOverlapCut(S.pat, S.sel, $("overlap").value === "cut"); patternChanged(); };
$("trklen").onchange = () => { S.pat.tracks[S.sel].len = Math.max(1, Math.min(64, +$("trklen").value || 16)); patternChanged(); };
$("swing").oninput = () => { S.pat.swing = +$("swing").value; $("swingv").textContent = S.pat.swing; patternChanged(); };
$("zoom").onchange = () => { S.zoom = +$("zoom").value; if (S.selNote >= 0) showNote(S.selNote); render(); };
$("left").onclick = () => { S.x0 = Math.max(0, S.x0 - 16); render(); };
$("right").onclick = () => { S.x0 += 16; render(); };
$("up").onclick = () => { S.lo = Math.min(S.lo + 6, isBrk() ? 0 : 127 - ROWS + 1); render(); };
$("down").onclick = () => { S.lo = Math.max(S.lo - 6, 0); render(); };

const isBrk = () => KIND(S.sel) === "BREAKS";
const slices = () => (S.st ? S.st.slices[brkOf(S.sel)] || 32 : 32);
const trans = () => (KIND(S.sel) === "REESE" ? S.params[17] || 0 : KIND(S.sel) === "SUB" ? S.params[subIndex(25)] || 0 : 0);   // (REESE's TRANS: the roll names the notes it plays)

// the rows in view: pitches for a synth track (high at the top), slices for a break track (slice 1 at the top)
function rows() { return isBrk() ? slices() : ROWS; }
function rowOf(v) { return isBrk() ? v : (S.lo + ROWS - 1) - v; }
function valOf(r) { return isBrk() ? r : (S.lo + ROWS - 1) - r; }
function fit() {                                     // the pitch window: around the track's notes
  if (!S.pat || isBrk()) return;
  const tr = S.pat.tracks[S.sel], ns = tr.steps.slice(0, tr.len).filter((s) => s.flags & ST_ON).map((s) => s.note);
  const mid = ns.length ? Math.round((Math.min(...ns) + Math.max(...ns)) / 2) : tr.lastNote;
  S.lo = Math.max(0, Math.min(127 - ROWS + 1, mid - Math.floor(ROWS / 2)));
}

// ---- the roll ---------------------------------------------------------------------------------------------------
function geom() {
  const cv = $("roll"), dpr = window.devicePixelRatio || 1, W = cv.clientWidth, nr = rows();
  const rh = isBrk() ? Math.max(8, Math.min(ROW_H, Math.floor(504 / nr))) : ROW_H, H = rh * nr;
  if (cv.width !== Math.round(W * dpr) || cv.height !== Math.round(H * dpr)) {
    cv.width = Math.round(W * dpr); cv.height = Math.round(H * dpr); cv.style.height = `${H}px`;
  }
  const { len, n, x0 } = span(), cw = (W - GUT) / n;
  return { cv, dpr, W, H, rh, len, n, x0, cw, x: (s) => GUT + (s - x0) * cw, step: (x) => x0 + Math.floor((x - GUT) / cw), row: (y) => Math.floor(y / rh) };
}
// the steps in view: all of the track, or 1 / 2 bars of it from S.x0 (the horizontal zoom)
function span() {
  const len = S.pat.tracks[S.sel].len, n = S.zoom && S.zoom < len ? S.zoom : len;
  S.x0 = Math.max(0, Math.min(len - n, S.x0));
  return { len, n, x0: S.x0 };
}
// keep a note in view (a row of margin): the pitch window and the steps follow it
function showNote(i) {
  const st = S.pat.tracks[S.sel].steps[i], { n } = span();
  if (!isBrk()) {
    if (st.note > S.lo + ROWS - 2) S.lo = st.note - ROWS + 2;
    if (st.note < S.lo + 1) S.lo = st.note - 1;
    S.lo = Math.max(0, Math.min(127 - ROWS + 1, S.lo));
  }
  if (i < S.x0) S.x0 = i;
  if (i >= S.x0 + n) S.x0 = i - n + 1;
  span();
}
function drawRoll() {
  const tr = S.pat.tracks[S.sel], g = geom(), c = g.cv.getContext("2d");
  c.setTransform(g.dpr, 0, 0, g.dpr, 0, 0);
  c.clearRect(0, 0, g.W, g.H);
  c.save();
  for (let r = 0; r < rows(); r++) {                 // rows: piano keys (black keys darker), or slices
    const v = valOf(r), y = r * g.rh, pc = (((v + trans()) % 12) + 12) % 12;
    const black = !isBrk() && [1, 3, 6, 8, 10].includes(pc);
    c.fillStyle = black ? "#e6e9ee" : "#fbfcfd";
    c.fillRect(GUT, y, g.W - GUT, g.rh);
    c.fillStyle = black ? "#2b2f36" : "#ffffff";
    c.fillRect(0, y, GUT - 2, g.rh - 1);
    if (isBrk() || pc === 0) {
      c.fillStyle = black ? "#fff" : "#6b7280";
      c.font = "10px -apple-system, Helvetica, Arial";
      c.fillText(isBrk() ? `SL ${v + 1}` : noteName(v + trans()), 4, y + g.rh - 3);
      if (!isBrk()) { c.fillStyle = "#d9dde3"; c.fillRect(GUT, y + g.rh - 1, g.W - GUT, 1); }
    }
  }
  c.beginPath(); c.rect(GUT, 0, g.W - GUT, g.H); c.clip();   // (zoomed in: the notes outside stay outside)
  for (let s = g.x0; s <= g.x0 + g.n; s++) {          // steps; beats stronger, bars strongest
    c.fillStyle = s % 16 === 0 ? "#9aa3ad" : s % 4 === 0 ? "#c5cbd3" : "#e9ecf0";
    c.fillRect(Math.round(g.x(s)), 0, 1, g.H);
  }
  if (S.playStep >= 0) {
    c.fillStyle = "rgba(208,64,42,.10)";
    c.fillRect(g.x(S.playStep), 0, g.cw, g.H);
  }
  let prev = null;
  for (let i = 0; i < g.len; i++) {                  // the notes: as long as they last
    const st = tr.steps[i];
    if (!(st.flags & ST_ON)) continue;
    const end = noteEnd(tr, i), r = rowOf(st.note);
    let x0 = g.x(i) + 1, x1 = g.x(Math.min(end, g.len)) - 1;
    if (st.len < LEN_STEP && end === i + 1) x1 = g.x(i) + g.cw * st.len / LEN_STEP - 1;
    const y = r * g.rh + 1;
    if (r >= 0 && r < rows()) {
      c.fillStyle = st.flags & ST_ACC ? "#1b5c99" : "#2f7fc1";
      c.fillRect(x0, y, Math.max(3, x1 - x0), g.rh - 2);
      c.fillStyle = "rgba(255,255,255,.55)";
      c.fillRect(x1 - 3, y + 2, 2, g.rh - 6);       // the handle that sets the length
      if (st.cond) { c.fillStyle = "#d0402a"; c.beginPath(); c.moveTo(x0, y); c.lineTo(x0 + 7, y); c.lineTo(x0, y + 7); c.fill(); }
      if (st.flags & ST_RTRG) {                      // the retrigs: ticks where they fall
        const iv = (16 * g.cw) / RTIM_DEN[st.rtim] , end = g.x(i) + (g.cw * (st.rlen || st.len)) / LEN_STEP;
        c.fillStyle = "rgba(255,255,255,.8)";
        for (let x = g.x(i) + iv; x < Math.min(end, x1) && iv > 1; x += iv) c.fillRect(Math.round(x), y + 2, 1, g.rh - 6);
      }
      if (i === S.selNote) { c.strokeStyle = "#d0402a"; c.lineWidth = 2; c.strokeRect(x0, y, Math.max(3, x1 - x0), g.rh - 2); }
      if ((st.flags & ST_SLIDE) && isBrk()) {        // a break step's slide is REVERSE: an arrow back
        c.fillStyle = "#fff";
        c.beginPath(); c.moveTo(x0 + 9, y + 2); c.lineTo(x0 + 3, y + g.rh / 2 - 1); c.lineTo(x0 + 9, y + g.rh - 4); c.fill();
      }
    } else {                                         // out of view: a mark at the edge
      c.fillStyle = "#2f7fc1";
      c.fillRect(x0, r < 0 ? 0 : g.H - 3, Math.max(3, x1 - x0), 3);
    }
    if (!isBrk() && prev && prev.end === i && ((st.flags & ST_SLIDE) || overlapSlide(S.pat, S.sel, i))) {
      c.strokeStyle = "#d0402a"; c.lineWidth = 2;      // a slide (flagged, or the note before reaches past): a ramp
      c.beginPath(); c.moveTo(g.x(i) - 6, prev.r * g.rh + g.rh / 2); c.lineTo(g.x(i) + 6, r * g.rh + g.rh / 2); c.stroke();
    }
    prev = { end, r };
  }
  c.restore();
}
function drawVel() {
  const cv = $("vel"), dpr = window.devicePixelRatio || 1, W = cv.clientWidth, H = 72, tr = S.pat.tracks[S.sel];
  if (cv.width !== Math.round(W * dpr)) { cv.width = Math.round(W * dpr); cv.height = Math.round(H * dpr); }
  const c = cv.getContext("2d"), { n, x0 } = span(), cw = (W - GUT) / n;
  c.setTransform(dpr, 0, 0, dpr, 0, 0);
  c.clearRect(0, 0, W, H);
  c.fillStyle = "#fbfcfd";
  c.fillRect(GUT, 0, W - GUT, H);
  for (let i = 0; i < tr.len; i++) {
    const st = tr.steps[i];
    if (!(st.flags & ST_ON)) continue;
    const v = st.flags & ST_ACC ? 127 : st.vel, h = (H - 16) * v / 127;
    c.fillStyle = st.flags & ST_ACC ? "#1b5c99" : "#2f7fc1";
    if (i >= x0 && i < x0 + n) c.fillRect(GUT + (i - x0) * cw + cw * 0.3, H - 4 - h, Math.max(3, cw * 0.4), h);
  }
}

// a note under (step, row): the one starting before and lasting over that step, on that row
function noteAt(step, val) {
  const tr = S.pat.tracks[S.sel];
  for (let i = 0; i < tr.len; i++) {
    const st = tr.steps[i];
    if ((st.flags & ST_ON) && st.note === val && i <= step && step < Math.max(i + 1, noteEnd(tr, i))) return i;
  }
  return -1;
}
function posOf(ev) { const r = $("roll").getBoundingClientRect(); return { x: ev.clientX - r.left, y: ev.clientY - r.top }; }

$("roll").oncontextmenu = (ev) => ev.preventDefault();
$("roll").onpointerdown = (ev) => {
  if (!S.pat) return;
  const g = geom(), { x, y } = posOf(ev), step = g.step(x), val = valOf(g.row(y)), tr = S.pat.tracks[S.sel];
  if (step < g.x0 || step >= Math.min(g.len, g.x0 + g.n) || x < GUT) return;
  const at = noteAt(step, val);
  if (at >= 0 && (ev.button === 2 || ev.altKey)) {   // delete
    tr.steps[at].flags = 0;
    S.selNote = -1;
    return patternChanged();
  }
  if (at >= 0 && ev.shiftKey) { tr.steps[at].flags ^= ST_ACC; return patternChanged(); }
  if (ev.button !== 0) return;
  $("roll").setPointerCapture(ev.pointerId);
  if (at >= 0) {
    const endX = g.x(Math.min(noteEnd(tr, at), g.len));
    S.selNote = at;
    S.drag = Math.abs(x - endX) < 8 || x > endX - 8 ? { mode: "len", i: at } : { mode: "move", i: at, grab: step - at, val };
  } else {                                           // a new note (mono: it takes the step), then its length by dragging
    const st = tr.steps[step];
    Object.assign(st, { note: val, flags: ST_ON, len: +$("newlen").value, vel: 100 });
    if (!isBrk()) tr.lastNote = val;
    S.selNote = step;
    S.drag = { mode: "len", i: step, fresh: true };
  }
  render();
};
$("roll").onpointermove = (ev) => {
  if (!S.drag) return;
  S.drag.last = posOf(ev);
  if (!S.edgeT) S.edgeT = setInterval(edgeScroll, 90);
  dragTo(S.drag.last);
};
// dragging past an edge scrolls: above / below the roll for higher / lower notes, past the right / left end (zoomed
// in) for the next / last steps; the note being dragged comes along
function edgeScroll() {
  const d = S.drag;
  if (!d || !d.last) { clearInterval(S.edgeT); S.edgeT = 0; return; }
  const g = geom(), { x, y } = d.last, lo = S.lo, x0 = S.x0;
  if (!isBrk() && y < g.rh / 2) S.lo = Math.min(127 - ROWS + 1, S.lo + 1);
  if (!isBrk() && y > g.H - g.rh / 2) S.lo = Math.max(0, S.lo - 1);
  if (x > g.W - g.cw / 3) S.x0 += 1;
  if (x < GUT + g.cw / 3 && d.mode === "move") S.x0 = Math.max(0, S.x0 - 1);
  span();
  if (S.lo !== lo || S.x0 !== x0) dragTo(d.last);
}
function dragTo({ x, y }) {
  const g = geom(), tr = S.pat.tracks[S.sel], d = S.drag;
  if (d.mode === "len") {
    const q = g.cw / LEN_STEP, len = Math.max(1, Math.min(LEN_MAX, Math.round((x - g.x(d.i)) / q)));
    if (!d.fresh || len > 1 || Math.abs(x - g.x(d.i)) > 4) tr.steps[d.i].len = len;
  } else {
    const to = Math.max(g.x0, Math.min(Math.min(g.len, g.x0 + g.n) - 1, g.step(x) - d.grab));
    const val = Math.max(0, Math.min(isBrk() ? slices() - 1 : 127, valOf(Math.max(0, Math.min(rows() - 1, g.row(y))))));
    if (to !== d.i) {                                // move along: the note takes the new step
      tr.steps[to] = { ...tr.steps[d.i] };
      tr.steps[d.i] = { ...tr.steps[d.i], flags: 0 };
      d.i = to;
      S.selNote = to;
    }
    tr.steps[d.i].note = val;
  }
  d.moved = true;
  render();
}
$("roll").onpointerup = () => {
  clearInterval(S.edgeT); S.edgeT = 0;
  if (!S.drag) return;
  const d = S.drag;
  S.drag = null;
  if (d.moved || d.fresh) patternChanged();
};
$("roll").ondblclick = (ev) => {
  const g = geom(), { x, y } = posOf(ev), at = noteAt(g.step(x), valOf(g.row(y)));
  if (at >= 0) { S.pat.tracks[S.sel].steps[at].flags ^= ST_SLIDE; patternChanged(); }
};
$("roll").onwheel = (ev) => {
  ev.preventDefault();
  if (ev.shiftKey || Math.abs(ev.deltaX) > Math.abs(ev.deltaY)) {   // sideways: along the steps (zoomed in)
    S.x0 += (ev.deltaX || ev.deltaY) > 0 ? 1 : -1;
    span();
    return render();
  }
  if (isBrk()) return;
  S.lo = Math.max(0, Math.min(127 - ROWS + 1, S.lo + (ev.deltaY < 0 ? 1 : -1)));
  render();
};
let velDrag = false, velFrom = null;
function velPos(ev) {
  const cv = $("vel"), r = cv.getBoundingClientRect(), { n, x0 } = span(), cw = (r.width - GUT) / n;
  return { i: x0 + Math.floor((ev.clientX - r.left - GUT) / cw),
    v: Math.max(1, Math.min(127, Math.round(127 * (1 - (ev.clientY - r.top - 4) / (72 - 16))))) };
}
function velAt(ev) {                                 // drag: each note's velocity; shift-drag: a straight ramp
  const tr = S.pat.tracks[S.sel], p = velPos(ev);
  if (velFrom) {
    const a = velFrom, lo = Math.min(a.i, p.i), hi = Math.max(a.i, p.i);
    for (let i = Math.max(0, lo); i <= hi && i < tr.len; i++) {
      if (!(tr.steps[i].flags & ST_ON)) continue;
      tr.steps[i].vel = hi === lo ? p.v : Math.round(a.v + ((p.v - a.v) * (i - a.i)) / (p.i - a.i));
      tr.steps[i].flags &= ~ST_ACC;
    }
    return render();
  }
  if (p.i < 0 || p.i >= tr.len || !(tr.steps[p.i].flags & ST_ON)) return;
  tr.steps[p.i].vel = p.v;
  tr.steps[p.i].flags &= ~ST_ACC;
  render();
}
$("vel").onpointerdown = (ev) => { velDrag = true; velFrom = ev.shiftKey ? velPos(ev) : null; $("vel").setPointerCapture(ev.pointerId); velAt(ev); };
$("vel").onpointermove = (ev) => { if (velDrag) velAt(ev); };
$("vel").onpointerup = () => { if (velDrag) { velDrag = false; patternChanged(); } };
document.onkeydown = (ev) => {                       // the selected note: Delete, arrows (pitch / slice, in time)
  if (!S.pat || S.selNote < 0 || /INPUT|SELECT/.test(document.activeElement.tagName)) return;
  const tr = S.pat.tracks[S.sel], st = tr.steps[S.selNote];
  if (!(st.flags & ST_ON)) return;
  if (ev.key === "Delete" || ev.key === "Backspace") { st.flags = 0; S.selNote = -1; }
  else if (ev.key === "ArrowUp" || ev.key === "ArrowDown") {   /* up: a higher note (a break: the slice above) */
    const d = (ev.key === "ArrowUp" ? 1 : -1) * (isBrk() ? -1 : 1);
    st.note = Math.max(0, Math.min(isBrk() ? slices() - 1 : 127, st.note + d));
    showNote(S.selNote);
  } else if (ev.key === "ArrowLeft" || ev.key === "ArrowRight") {   /* in time, over the other notes (as the FM-1's
                                                                      * SELECT with a step held) */
    const dir = ev.key === "ArrowRight" ? 1 : -1;
    let j = S.selNote + dir;
    while (j >= 0 && j < tr.len && (tr.steps[j].flags & ST_ON)) j += dir;
    if (j < 0 || j >= tr.len) return;
    tr.steps[j] = { ...st };
    tr.steps[S.selNote] = { ...st, flags: 0 };
    S.selNote = j;
    showNote(j);
  }
  else return;
  ev.preventDefault();
  patternChanged();
};

// ---- sound pages --------------------------------------------------------------------------------------------
function slider(box, i, p) {
  const el = document.createElement("div");
  el.className = "prm";
  if (p.name === "LOOP") {
    el.innerHTML = `<span class="nm">${p.name}</span><select></select>`;
    const sel = el.querySelector("select");
    S.loops.forEach((n, k) => { const o = document.createElement("option"); o.value = k; o.textContent = n; sel.appendChild(o); });
    sel.value = S.params[i];
    sel.onchange = () => setParam(i, +sel.value);
    el.dataset.i = i;
  } else {
    el.innerHTML = `<span class="nm">${p.name}</span><input type="range" min="${p.min}" max="${p.max}" step="1"><span class="v"></span>`;
    const r = el.querySelector("input"), v = el.querySelector(".v");
    const show = () => { v.textContent = p.text ? p.text(+r.value) : r.value; };
    r.value = S.params[i];
    show();
    r.oninput = () => { show(); setParam(i, +r.value); };
    el.dataset.i = i;
  }
  el.p = p;
  box.appendChild(el);
}
function buildSound() {
  const box = $("reese"), groups = {};
  box.textContent = "";
  REESE.forEach((p, i) => {
    if (HIDDEN.has(p.name)) return;                  // (0.8's filter LFO: LFO 1 on the LFOs tab now)
    if (!groups[p.group]) {
      const g = document.createElement("div");
      g.className = "grp";
      g.innerHTML = `<h3>${p.group}</h3>`;
      box.appendChild(g);
      groups[p.group] = g;
    }
    slider(groups[p.group], i, p);
  });
  feGroup(box, 0);
  const ps = $("preset");
  ps.textContent = "";
  S.presets.forEach((n, k) => { const o = document.createElement("option"); o.value = k; o.textContent = n; ps.appendChild(o); });
  ps.value = S.st.preset;
  ps.onchange = () => guard(async () => { await S.link.preset(+ps.value); S.params = await S.link.params(); refreshValues(); });
}
// the filter envelope's own group (0.19: an FM-1 from before has none)
function feGroup(box, set) {
  if (S.params.length < NPARAM_19) return;
  const g = document.createElement("div");
  g.className = "grp";
  g.innerHTML = "<h3>FILT ENV</h3>";
  box.appendChild(g);
  FENV.forEach((p, k) => slider(g, feIndex(set, k), p));
}
function buildSub() {                                // (0.13: SUB's values follow the LFOs)
  const has = !!(S.st && S.st.nparams >= NPARAM_13);
  $("subTabBtn").hidden = !has;
  if (!has) return;
  const box = $("sub"), groups = {};
  box.textContent = "";
  SUB.forEach((p, i) => {
    if (!groups[p.group]) {
      const g = document.createElement("div");
      g.className = "grp";
      g.innerHTML = `<h3>${p.group}</h3>`;
      box.appendChild(g);
      groups[p.group] = g;
    }
    slider(groups[p.group], subIndex(i), p);
  });
  feGroup(box, 1);
  const ps = $("subpreset");
  ps.textContent = "";
  (S.subPresets || []).forEach((n, k) => { const o = document.createElement("option"); o.value = k; o.textContent = n; ps.appendChild(o); });
  ps.onchange = () => guard(async () => { await S.link.subPreset(+ps.value); S.params = await S.link.params(); refreshValues(); });
}
function buildBreaks() {
  [0, 1].forEach((k) => {
    const box = $(k ? "brkB" : "brkA"), g = document.createElement("div");
    box.textContent = "";
    g.className = "grp";
    const t = S.proj ? S.proj.eng.indexOf(k ? E.BRKB : E.BRKA) : k + 1;
    g.innerHTML = `<h3>BREAKS ${k ? "B" : "A"} · ${t >= 0 ? `TRACK ${t + 1}` : "ON NO TRACK"}</h3>`;
    box.appendChild(g);
    brkParams(S.loops).forEach((p, i) => slider(g, brkIndex(k, i), p));
  });
}
// ---- patterns (0.11): 8 an instrument; a click queues one (stopped: at once) -------------------------------------
function renderPats() {
  const box = $("pats");
  box.hidden = !S.pats;
  if (!S.pats) return;
  const P = S.pats.tracks[S.sel];
  if (box.children.length !== NPAT + 2) {
    box.textContent = "";
    const l = document.createElement("span"); l.className = "small"; l.textContent = "Pattern"; box.appendChild(l);
    for (let n = 0; n < NPAT; n++) {
      const b = document.createElement("button");
      b.className = "pat"; b.textContent = n + 1; b.dataset.n = n;
      b.onclick = () => guard(async () => {
        S.writing++;
        try { await S.link.patSel(S.sel, n); S.pats = await S.link.pats(); S.pat = await S.link.readPattern(S.st.patSize); S.lastRev = (await S.link.state()).rev; }
        finally { S.writing--; }
        render();
      });
      box.appendChild(b);
    }
    const m = document.createElement("span"); m.className = "small mem"; box.appendChild(m);
  }
  box.querySelectorAll(".pat").forEach((b) => {
    const n = +b.dataset.n;
    b.classList.toggle("on", P.cur === n);
    b.classList.toggle("queued", P.queued === n);
    b.classList.toggle("used", !!(P.used >> n & 1));
    b.title = P.cur === n ? "Playing" : P.queued === n ? "Comes in when this one ends" : (P.used >> n & 1) ? "Has notes" : "Empty";
  });
  box.querySelector(".mem").textContent = `Pattern memory ${Math.round((100 * S.pats.notes) / S.pats.room)}% (${S.pats.notes} of ${S.pats.room} notes)`;
}

// ---- the selected note's inspector (0.11): velocity, condition, retrig ----------------------------------------
function renderInsp() {
  const box = $("insp"), tr = S.pat && S.pat.tracks[S.sel], st = tr && S.selNote >= 0 ? tr.steps[S.selNote] : null;
  const has = !!(st && (st.flags & ST_ON) && S.pat.stepBytes !== 4);
  box.hidden = !has;
  if (!has) return;
  ["i-rtim", "i-rlen", "i-rvel"].forEach((id) => { $(id).disabled = !(st.flags & ST_RTRG); });
  if (box.contains(document.activeElement)) return;  // (not the control in your hand)
  $("i-vel").value = st.flags & ST_ACC ? 127 : st.vel; $("i-velv").textContent = st.flags & ST_ACC ? "accent" : st.vel;
  $("i-cond").value = st.cond;
  $("i-rtrg").checked = !!(st.flags & ST_RTRG);
  $("i-rtim").value = st.rtim;
  $("i-rlen").value = st.rlen;
  $("i-rvel").value = st.rvel; $("i-rvelv").textContent = st.rvel ? (st.rvel > 0 ? `fade in ${st.rvel}` : `fade out ${-st.rvel}`) : "flat";
}
function buildInsp() {
  const c = $("i-cond"), r = $("i-rtim"), l = $("i-rlen");
  COND_NAMES.forEach((n, i) => { const o = document.createElement("option"); o.value = i; o.textContent = n === "ALWAYS" ? "Always" : n; c.appendChild(o); });
  RTIM_DEN.forEach((d, i) => { const o = document.createElement("option"); o.value = i; o.textContent = `1/${d}`; r.appendChild(o); });
  [[0, "the note's"], [1, "¼ step"], [2, "½ step"], [4, "1 step"], [8, "2 steps"], [16, "4 steps"], [32, "8 steps"], [64, "16 steps"]]
    .forEach(([v, t]) => { const o = document.createElement("option"); o.value = v; o.textContent = t; l.appendChild(o); });
  const step = () => S.pat.tracks[S.sel].steps[S.selNote];
  $("i-vel").oninput = () => { const st = step(); st.vel = +$("i-vel").value; st.flags &= ~ST_ACC; $("i-velv").textContent = st.vel; patternChanged(); };
  c.onchange = () => { step().cond = +c.value; patternChanged(); };
  $("i-rtrg").onchange = () => { const st = step(); st.flags = $("i-rtrg").checked ? st.flags | ST_RTRG : st.flags & ~ST_RTRG; patternChanged(); renderInsp(); };
  r.onchange = () => { step().rtim = +r.value; patternChanged(); };
  l.onchange = () => { step().rlen = +l.value; patternChanged(); };
  $("i-rvel").oninput = () => {
    const v = +$("i-rvel").value;
    step().rvel = v;
    $("i-rvelv").textContent = v ? (v > 0 ? `fade in ${v}` : `fade out ${-v}`) : "flat";
    patternChanged();
  };
}
buildInsp();

// ---- the LFOs (0.9) ------------------------------------------------------------------------------------------
async function readLfos() {                          // the DRAW points (an FM-1 before 0.9 has no LFOs)
  if (!S.st || S.st.nparams < NPARAM) return null;
  const out = [];
  for (let k = 0; k < nLfos(S.st.nparams); k++) out.push(await S.link.lfoPts(k));
  return out;
}
const LFO_PRESETS = {                                // a few shapes to start from (points, and a rate if it matters)
  Ramp: (i, n) => 100 - (i * 200) / (n - 1),
  Steps: (i) => [100, 40, -20, -80][i % 4],
  "Trance gate": (i) => ([1, 1, 0, 1, 0, 1, 1, 0][i % 8] ? 100 : -100),
  "Wobble 1/8": (i, n) => Math.round(100 * Math.cos((2 * Math.PI * i * 2) / n)),
  "Wobble 1/4T": (i, n) => Math.round(100 * Math.cos((2 * Math.PI * i * 3) / n)),
};
const ptsTimers = {};
function sendPts(k) {
  clearTimeout(ptsTimers[k]);
  ptsTimers[k] = setTimeout(() => guard(async () => {
    S.writing++;
    try { await S.link.setLfoPts(k, S.lfo[k].npts, S.lfo[k].pts); S.lastRev = (await S.link.state()).rev; } finally { S.writing--; }
  }), 120);
}
function buildLfos() {
  const box = $("lfoBox");
  box.textContent = "";
  $("lfoOld").hidden = !!S.lfo;
  if (!S.lfo) return;
  for (let k = 0; k < S.lfo.length; k++) {
    const g = document.createElement("div"), ps = lfoParams();
    g.className = "grp lfo";
    g.innerHTML = `<h3>${S.lfo.length > NLFO ? (k < NLFO ? "REESE " : "SUB ") : ""}LFO ${(k % NLFO) + 1}</h3><div class="lfo-prm"></div>
      <div class="lfo-draw"><canvas data-k="${k}" title="Draw the shape: click and drag (shift: a straight line). Drawing makes the shape DRAW."></canvas></div>
      <div class="row small lfo-tools"><label>Points <select class="npts"><option>16</option><option>32</option></select></label>
      <label>Snap <select class="snap"><option value="0">off</option><option value="50">5 levels</option><option value="25">9 levels</option></select></label>
      <span class="pre"></span></div>`;
    box.appendChild(g);
    const prm = g.querySelector(".lfo-prm");
    ps.forEach((p, i) => {
      const q = { ...p };
      if (p.name === "RATE") { q.text = (v) => p.text(v, S.params[lfoIndex(k, 1)]); q.max = S.params[lfoIndex(k, 1)] ? 15 : 127; }
      slider(prm, lfoIndex(k, i), q);
    });
    const np = g.querySelector(".npts");
    np.value = S.lfo[k].npts;
    np.onchange = () => { S.lfo[k].npts = +np.value; sendPts(k); drawLfos(); };
    const pre = g.querySelector(".pre");
    Object.entries(LFO_PRESETS).forEach(([name, f]) => {
      const b = document.createElement("button");
      b.className = "quiet small";
      b.textContent = name;
      b.onclick = () => {
        const n = S.lfo[k].npts;
        S.lfo[k].pts = Array.from({ length: LFO_PTS }, (_, i) => Math.round(f(i % n, n)));
        toDraw(k);
        sendPts(k);
        drawLfos();
      };
      pre.appendChild(b);
    });
    const cv = g.querySelector("canvas");
    cv.onpointerdown = (ev) => { cv.setPointerCapture(ev.pointerId); S.lfoDraw = { k, from: lfoPoint(cv, ev, k), shift: ev.shiftKey }; paint(cv, ev, k); };
    cv.onpointermove = (ev) => { if (S.lfoDraw && S.lfoDraw.k === k) paint(cv, ev, k); };
    cv.onpointerup = () => { S.lfoDraw = null; sendPts(k); };
  }
  drawLfos();
}
function toDraw(k) {                                 // drawing or a preset: the shape becomes DRAW
  const i = lfoIndex(k, 0);
  if (S.params[i] !== 7) { setParam(i, 7); refreshValues(); }
}
function lfoPoint(cv, ev, k) {
  const r = cv.getBoundingClientRect(), n = S.lfo[k].npts, snap = +cv.closest(".lfo").querySelector(".snap").value;
  const i = Math.max(0, Math.min(n - 1, Math.floor(((ev.clientX - r.left) / r.width) * n)));
  let v = Math.max(-100, Math.min(100, Math.round(100 - (200 * (ev.clientY - r.top)) / r.height)));
  if (snap) v = Math.round(v / snap) * snap;
  return { i, v };
}
function paint(cv, ev, k) {
  const p = lfoPoint(cv, ev, k), L = S.lfo[k], d = S.lfoDraw;
  if (d.shift) {                                     // a straight line from where it started
    const a = d.from, lo = Math.min(a.i, p.i), hi = Math.max(a.i, p.i);
    for (let i = lo; i <= hi; i++) L.pts[i] = hi === lo ? p.v : Math.round(a.v + ((p.v - a.v) * (i - a.i)) / (p.i - a.i));
  } else L.pts[p.i] = p.v;
  if (L.npts === 16) for (let i = 0; i < 16; i++) L.pts[i + 16] = L.pts[i];
  toDraw(k);
  sendPts(k);
  drawLfos();
}
function drawLfos() {
  if (!S.lfo) return;
  document.querySelectorAll(".lfo canvas").forEach((cv) => {
    const k = +cv.dataset.k, dpr = window.devicePixelRatio || 1, W = cv.clientWidth, H = 150;
    if (cv.width !== Math.round(W * dpr)) { cv.width = Math.round(W * dpr); cv.height = Math.round(H * dpr); cv.style.height = `${H}px`; }
    const c = cv.getContext("2d"), L = S.lfo[k], n = L.npts;
    const p = { shape: S.params[lfoIndex(k, 0)], phase: S.params[lfoIndex(k, 5)], smooth: S.params[lfoIndex(k, 7)], npts: n, pts: L.pts };
    c.setTransform(dpr, 0, 0, dpr, 0, 0);
    c.fillStyle = "#fbfcfd"; c.fillRect(0, 0, W, H);
    c.fillStyle = "#e9ecf0";
    for (let i = 0; i <= n; i++) c.fillRect(Math.round((i * W) / n), 0, 1, H);
    c.fillStyle = "#c5cbd3"; c.fillRect(0, H / 2, W, 1);
    if (p.shape === 7) {                             // DRAW: the points as bars
      c.fillStyle = "#cfe3f5";
      for (let i = 0; i < n; i++) {
        const y = H / 2 - (L.pts[i] / 100) * (H / 2 - 4);
        c.fillRect((i * W) / n + 2, Math.min(y, H / 2), W / n - 4, Math.abs(H / 2 - y));
      }
    }
    c.strokeStyle = "#2f7fc1"; c.lineWidth = 2; c.beginPath();
    for (let x = 0; x <= W; x++) {
      const y = H / 2 - lfoShapeAt(p, x / W) * (H / 2 - 4);
      if (x) c.lineTo(x, y); else c.moveTo(x, y);
    }
    c.stroke();
    const now = S.lfoNow && S.lfoNow[k];
    if (now) { c.fillStyle = "#d0402a"; c.fillRect(Math.round(now.phase * W), 0, 2, H); }
  });
}

function refreshValues() {                           // the FM-1 changed: the controls follow (not the one in your hand)
  document.querySelectorAll(".prm").forEach((el) => {
    if (!el.p) return;                               // (the project's FX rows: refreshProject keeps those)
    const i = +el.dataset.i, inp = el.querySelector("input,select");
    if (inp === document.activeElement) return;
    inp.value = S.params[i];
    const v = el.querySelector(".v");
    if (v) v.textContent = el.p.text ? el.p.text(S.params[i]) : S.params[i];
  });
  if (S.st) $("preset").value = S.st.preset;
  document.querySelectorAll(".lfo .prm").forEach((el) => {   // RATE: note lengths synced, Hz free
    if (el.p && el.p.name === "RATE") el.querySelector("input").max = S.params[+el.dataset.i - 1] ? 15 : 127;
  });
  drawLfos();
}

// ---- files ------------------------------------------------------------------------------------------------------
function download(name, obj) {
  const a = document.createElement("a");
  a.href = URL.createObjectURL(new Blob([JSON.stringify(obj, null, 1)], { type: "application/json" }));
  a.download = name;
  a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
}
const b64 = (b) => btoa(String.fromCharCode(...b));
const unb64 = (s) => Uint8Array.from(atob(s), (c) => c.charCodeAt(0));
let fileWant = null;
$("file").onchange = async (ev) => {
  const f = ev.target.files[0];
  ev.target.value = "";
  if (!f || !fileWant) return;
  const want = fileWant;
  fileWant = null;
  await guard(async () => want(JSON.parse(await f.text())));
};
// a backup from the 0.7-0.11 editor (the sound and the one pattern): still loads
async function restoreBackup(o) {
  {
    S.writing++;
    try {
      for (let i = 0; i < o.params.length && i < S.params.length; i++) await S.link.setParam(i, o.params[i]);
      S.pat = parsePattern([...unb64(o.pattern)]);
      await S.link.writePattern(S.pat);
      if (o.lfo && S.lfo) for (let k = 0; k < NLFO; k++) await S.link.setLfoPts(k, o.lfo[k].npts, o.lfo[k].pts);
      S.lfo = await readLfos();
      S.params = await S.link.params();
      S.lastRev = (await S.link.state()).rev;
    } finally { S.writing--; }
    refreshValues();
    render();
    say("Restored. Press Save on FM-1 to keep it there.", "ok");
  }
}
const LIB = "dreadnought-sounds";
const lib = () => { try { return JSON.parse(localStorage.getItem(LIB)) || []; } catch (_) { return []; } };
const saveLib = (l) => { try { localStorage.setItem(LIB, JSON.stringify(l)); } catch (_) { /* (private window) */ } };
const soundNow = (name) => ({ format: "dreadnought-sound", version: 2, name, reese: S.params.slice(0, REESE.length),
  lfos: S.lfo ? S.params.slice(lfoIndex(0, 0), lfoIndex(NLFO, 0)) : undefined, lfo: S.lfo || undefined });
async function applySound(o) {
  if (o.format !== "dreadnought-sound") throw new Error("That file is not a DREADNOUGHT sound.");
  S.writing++;
  try {
    for (let i = 0; i < REESE.length; i++) await S.link.setParam(i, o.reese[i]);
    if (o.lfos && S.lfo) {                           // (a sound kept with 0.9: its LFOs too)
      for (let i = 0; i < o.lfos.length; i++) await S.link.setParam(lfoIndex(0, 0) + i, o.lfos[i]);
      for (let k = 0; k < NLFO; k++) await S.link.setLfoPts(k, o.lfo[k].npts, o.lfo[k].pts);
      S.lfo = await readLfos();
    }
    S.params = await S.link.params();
    S.lastRev = (await S.link.state()).rev;
  } finally { S.writing--; }
  refreshValues();
  say(`Sound "${o.name}" sent.`, "ok");
}
function renderLib() {
  const ul = $("library");
  ul.textContent = "";
  lib().forEach((o, k) => {
    const li = document.createElement("li");
    li.innerHTML = `<span></span><button class="quiet small">Load</button><button class="quiet small">Delete</button>`;
    li.querySelector("span").textContent = o.name;
    const [ld, del] = li.querySelectorAll("button");
    ld.onclick = () => (S.link ? guard(() => applySound(o)) : say("Connect first.", "bad"));
    del.onclick = () => { const l = lib(); l.splice(k, 1); saveLib(l); renderLib(); };
    ul.appendChild(li);
  });
}
$("keep").onclick = () => {
  if (!S.pat) return say("Connect first.", "bad");
  const name = $("sndname").value.trim() || `Sound ${lib().length + 1}`, l = lib();
  l.push(soundNow(name));
  saveLib(l);
  $("sndname").value = "";
  renderLib();
};
$("export").onclick = () => {
  if (!S.pat) return say("Connect first.", "bad");
  const name = $("sndname").value.trim() || "reese";
  download(`${name.replace(/[^\w-]+/g, "_")}.dnsound.json`, soundNow(name));
};
$("import").onclick = () => {
  if (!S.pat) return say("Connect first.", "bad");
  fileWant = applySound;
  $("file").click();
};

// ---- the project (0.12): each track's instrument and sends, the FX rack, memory and load --------------------------
const projTimers = {};
function projSet(f, v, after) {
  clearTimeout(projTimers[f]);
  projTimers[f] = setTimeout(() => guard(async () => {
    S.writing++;
    try {
      const rc = await S.link.projSet(f, v);
      S.proj = await S.link.proj();
      setEngines(S.proj.eng);
      S.lastRev = (await S.link.state()).rev;
      if (rc) say(f >= PF.fx(0, 0) ? "No memory for that effect: turn the other one off first." : "The FM-1 didn't take that.", "bad");
    } finally { S.writing--; }
    refreshProject();
    if (after) after();
    render();
  }), 60);
}
function rangeCell(val, onv) {
  const td = document.createElement("td");
  td.innerHTML = `<input type="range" min="0" max="127"><span class="v"></span>`;
  const r = td.querySelector("input"), v = td.querySelector(".v");
  r.value = val; v.textContent = val;
  r.oninput = () => { v.textContent = r.value; onv(+r.value); };
  return td;
}
function buildProject() {
  $("projOld").hidden = !!S.proj;
  document.querySelectorAll("#files .pcols").forEach((x) => { x.hidden = !S.proj; });
  if (!S.proj) return;
  refreshProject();
}
function refreshProject() {
  if (!S.proj || !S.pat) return;
  const P = S.proj, tb = $("ptracks");
  if (document.activeElement !== $("pname")) $("pname").value = P.name;
  if (!tb.contains(document.activeElement)) {
    tb.textContent = "";
    for (let t = 0; t < 8; t++) {
      const tr = document.createElement("tr"), td = document.createElement("td"), te = document.createElement("td"), sel = document.createElement("select");
      td.textContent = t + 1;
      ENG_NAMES.forEach((n, e) => { const o = document.createElement("option"); o.value = e; o.textContent = n === "MIDI" ? "MIDI out" : n; sel.appendChild(o); });
      sel.value = P.eng[t];
      sel.onchange = () => projSet(PF.eng(t), +sel.value, buildBreaks);
      te.appendChild(sel);
      tr.append(td, te, rangeCell(P.send[0][t], (v) => projSet(PF.sendA(t), v)), rangeCell(P.send[1][t], (v) => projSet(PF.sendB(t), v)));
      tb.appendChild(tr);
    }
  }
  const fb = $("fxbox");
  if (!fb.contains(document.activeElement)) {
    fb.textContent = "";
    [0, 1].forEach((k) => {
      const f = P.fx[k], g = document.createElement("div");
      g.className = "grp";
      g.innerHTML = `<h3>FX ${k ? "B" : "A"}</h3>`;
      const row = (name, el) => { const d = document.createElement("div"); d.className = "prm"; d.innerHTML = `<span class="nm">${name}</span>`; d.appendChild(el); g.appendChild(d); return d; };
      const ty = document.createElement("select");
      FX_NAMES.forEach((n, i) => { const o = document.createElement("option"); o.value = i; o.textContent = n; ty.appendChild(o); });
      ty.value = f.type;
      ty.onchange = () => projSet(PF.fx(k, 0), +ty.value);
      row("TYPE", ty);
      fxParams(f.type, f.p).forEach(({ i, name, max, text }) => {
        const d = row(name, document.createElement("span"));
        d.lastChild.remove();
        d.insertAdjacentHTML("beforeend", `<input type="range" min="0" max="${max}"><span class="v"></span>`);
        const r = d.querySelector("input"), v = d.querySelector(".v"), show = () => { v.textContent = text(+r.value); };
        r.value = f.p[i]; show();
        r.oninput = () => { show(); projSet(PF.fx(k, i + 1), +r.value); };
      });
      const rt = row("RETURN", document.createElement("span"));
      rt.lastChild.remove();
      rt.insertAdjacentHTML("beforeend", `<input type="range" min="0" max="127"><span class="v"></span>`);
      const r = rt.querySelector("input"), v = rt.querySelector(".v");
      r.value = P.ret[k]; v.textContent = P.ret[k];
      r.oninput = () => { v.textContent = r.value; projSet(PF.ret(k), +r.value); };
      fb.appendChild(g);
    });
  }
  refreshMeters();
}
function meter(id, used, cap, text) {
  const f = cap ? Math.min(1, used / cap) : 0;
  $(id).style.width = `${(100 * f).toFixed(1)}%`;
  $(id).classList.toggle("full", f > 0.95);
  $(`${id}v`).textContent = text;
}
function refreshMeters() {
  if (!S.proj) return;
  const P = S.proj;
  meter("m-smp", P.smpUsed, P.smpCap, `${Math.round(P.smpUsed / 1024)} of ${Math.round(P.smpCap / 1024)} KB`);
  meter("m-fx", P.fxUsed, P.fxCap, `${P.fxUsed} of ${P.fxCap} KB`);
  meter("m-cpu", P.cpu, 100, S.mock ? "— (the demo)" : `${P.cpu}%`);
}
$("pname").onchange = () => guard(async () => {
  S.writing++;
  try { await S.link.projName($("pname").value.trim() || "PROJECT"); S.proj = await S.link.proj(); } finally { S.writing--; }
});

// ---- samples ------------------------------------------------------------------------------------------------
// S.media: [{name, set: {rate, fold}, lock, ch / rate (decoded from orig: changeable), adpcm / n (as on the FM-1),
//            orig (the file's bytes), stale (changed, not sent)}]
const isSrc = (m) => !!m.ch;                         // (converted here: its settings can change)
const sizeOf = (m) => (isSrc(m) && (m.stale || !m.adpcm) ? costAt(m, m.set.rate) : m.adpcm.length);
const proxy = (m) => (isSrc(m) ? m : { rate: m.rate, ch: [{ length: m.n }], set: { rate: m.rate }, lock: true });
async function decodeFile(bytes) {
  try { return readWav(bytes); } catch (_) { /* not a WAV the reader takes: the browser's decoder */ }
  const ctx = new OfflineAudioContext(1, 1, 44100), ab = await ctx.decodeAudioData(bytes.slice().buffer);
  const ch = [];
  for (let c = 0; c < Math.min(2, ab.numberOfChannels); c++) ch.push(Float32Array.from(ab.getChannelData(c)));
  return { rate: ab.sampleRate, ch };
}
async function readMedia() {
  S.mediaRead = true;
  say("Reading the samples on the FM-1…");
  S.writing++;
  try { S.dev = await readSlots(S.link); } finally { S.writing--; }
  const kept = S.media.filter((m) => !m.sent);       // (added here, not sent yet: they stay)
  S.media = slotSamples(S.dev).map((x, i) => ({ name: `Sample ${i + 1}`, adpcm: x.adpcm, n: x.n, rate: x.rate, set: { rate: x.rate, fold: "mix" }, sent: true }));
  S.media.push(...kept);
  say("Samples read.", "ok");
  renderMedia();
}
async function addMedia(files) {
  for (const f of files) {
    try {
      const orig = new Uint8Array(await f.arrayBuffer()), d = await decodeFile(orig);
      if (!d.ch[0].length) throw new Error("no audio");
      S.media.push({ name: f.name.replace(/\.[^.]+$/, ""), ch: d.ch, rate: d.rate, orig, set: { rate: 22050, fold: "mix" }, stale: true });
    } catch (e) { say(`Couldn't read ${f.name}: ${e.message}`, "bad"); }
  }
  renderMedia();
}
let playing = null;
function preview(m) {
  if (playing) { try { playing.stop(); } catch (_) { /* (ended) */ } playing = null; }
  const c = isSrc(m) && (m.stale || !m.adpcm) ? convert(m) : { s: imaDecode(m.adpcm, m.n), rate: isSrc(m) ? m.set.rate : m.rate };
  const ctx = S.actx || (S.actx = new AudioContext()), buf = ctx.createBuffer(1, c.s.length, c.rate), d = buf.getChannelData(0);
  for (let i = 0; i < c.s.length; i++) d[i] = c.s[i] / 32768;
  const src = ctx.createBufferSource();
  src.buffer = buf; src.connect(ctx.destination); src.start();
  playing = src;
}
function renderMedia() {
  const tb = $("mlist");
  tb.textContent = "";
  const plan = packPlan(S.media.length, (i) => sizeOf(S.media[i])), total = S.media.reduce((a, m) => a + sizeOf(m), 0);
  S.media.forEach((m, i) => {
    const tr = document.createElement("tr");
    tr.className = (m.stale ? "stale " : "") + (!plan ? "over" : "");
    const len = isSrc(m) ? seconds(m) : m.n / m.rate;
    tr.innerHTML = `<td class="name"></td><td>${len.toFixed(2)} s</td><td></td><td></td><td class="v">${(sizeOf(m) / 1024).toFixed(1)} KB</td>
      <td><button class="quiet small" title="Listen (as it will be on the FM-1)">▶</button><button class="quiet small" title="Remove">✕</button></td>`;
    tr.querySelector(".name").textContent = m.name;
    tr.querySelector(".name").title = isSrc(m) ? `${m.name} (${m.rate} Hz, ${m.ch.length > 1 ? "stereo" : "mono"})` : "On the FM-1 as it is (load the project file it came from to convert it again)";
    const fs = document.createElement("select"), rs = document.createElement("select");
    FOLDS.forEach((f) => { const o = document.createElement("option"); o.value = f; o.textContent = f === "mix" ? "L+R" : f; fs.appendChild(o); });
    RATES.forEach((r) => { const o = document.createElement("option"); o.value = r; o.textContent = RATE_NAME[r]; rs.appendChild(o); });
    fs.value = m.set.fold; rs.value = m.set.rate;
    if (!rs.value) { const o = document.createElement("option"); o.value = m.set.rate; o.textContent = `${m.set.rate} Hz`; rs.appendChild(o); rs.value = m.set.rate; }
    fs.disabled = rs.disabled = !isSrc(m) || (m.ch.length < 2 && false);
    if (isSrc(m) && m.ch.length < 2) fs.disabled = true;
    fs.onchange = () => { m.set = { ...m.set, fold: fs.value }; m.stale = true; renderMedia(); };
    rs.onchange = () => { m.set = { ...m.set, rate: +rs.value }; m.stale = true; m.lock = true; renderMedia(); };
    tr.children[2].appendChild(fs);
    tr.children[3].appendChild(rs);
    const [pl, rm] = tr.querySelectorAll("button");
    pl.onclick = () => preview(m);
    rm.onclick = () => { S.media.splice(i, 1); renderMedia(); };
    tb.appendChild(tr);
  });
  $("m-media").style.width = `${Math.min(100, (100 * total) / CAP).toFixed(1)}%`;
  $("m-media").classList.toggle("full", !plan);
  $("mtotal").textContent = `${(total / 1024).toFixed(0)} of ${(CAP / 1024).toFixed(0)} KB${plan ? "" : " — too much: lower some rates, or Fit to space"}`;
  $("msend").disabled = !plan || !S.link || !S.media.some((m) => m.stale) && S.media.length === slotSamples(S.dev || [null, null, null]).length;
}
$("madd").onclick = () => $("mfiles").click();
$("mfiles").onchange = (ev) => { addMedia([...ev.target.files]); ev.target.value = ""; };
$("mediaCard").ondragover = (ev) => { ev.preventDefault(); $("mediaCard").classList.add("over"); };
$("mediaCard").ondragleave = () => $("mediaCard").classList.remove("over");
$("mediaCard").ondrop = (ev) => { ev.preventDefault(); $("mediaCard").classList.remove("over"); addMedia([...ev.dataTransfer.files]); };
$("mfit").onclick = () => {
  const px = S.media.map(proxy), r = fitToSpace(px);
  if (!r) return say("Even at 8 kHz these don't all fit: remove some.", "bad");
  S.media.forEach((m, i) => { if (isSrc(m) && m.set.rate !== r[i]) { m.set = { ...m.set, rate: r[i] }; m.stale = true; } });
  say("Rates picked to fit. Listen, then send.", "ok");
  renderMedia();
};
$("msend").onclick = () => guard(async () => {
  if (S.st.playing) throw new Error("Stop the FM-1 first: writing its memory interrupts the sound.");
  const parts = S.media.map((m) => {
    if (isSrc(m) && (m.stale || !m.adpcm)) { const c = convert(m); m.adpcm = imaEncode(c.s); m.n = c.s.length; }
    return { adpcm: m.adpcm, n: m.n, rate: isSrc(m) ? m.set.rate : m.rate };   // (a source's m.rate is its file's)
  });
  const ims = imagesOf(parts);
  $("pprog").hidden = false;
  S.writing++;
  try {
    if (!S.dev) S.dev = await readSlots(S.link);
    await writeSlots(S.link, ims, S.dev, (f) => { $("pprog").value = f; }, (t) => say(t));
    S.media.forEach((m) => { m.stale = false; m.sent = true; });
    S.loops = await S.link.names(1);
    S.proj = await S.link.proj();
  } finally { S.writing--; $("pprog").hidden = true; }
  buildBreaks(); renderMedia(); refreshMeters();
  say("Samples on the FM-1. The breaks play them: ARP, then PRESETS for the loop.", "ok");
});

// ---- projects: save, load, a folder of them ----------------------------------------------------------------
const projFileName = (o) => `${(o.name || "project").replace(/[^\w-]+/g, "_")}.dnproj`;
async function saveProject() {
  if (!S.link || !S.proj) return say("Connect first.", "bad");
  $("pprog").hidden = false;
  let o;
  S.writing++;
  try { o = await readProject(S.link, { media: S.media, onPart: (f) => { $("pprog").value = f; }, say }); } finally { S.writing--; $("pprog").hidden = true; }
  const text = JSON.stringify(o);
  if (S.dir) {
    const fh = await S.dir.getFileHandle(projFileName(o), { create: true }), w = await fh.createWritable();
    await w.write(text);
    await w.close();
    await listFolder();
    say(`Saved ${projFileName(o)} in your projects folder.`, "ok");
  } else {
    const a = document.createElement("a");
    a.href = URL.createObjectURL(new Blob([text], { type: "application/json" }));
    a.download = projFileName(o);
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 1000);
    say(`Saved ${projFileName(o)} to your downloads.`, "ok");
  }
}
async function loadProject(o) {
  if (o.format === "dreadnought-backup") return restoreBackup(o);
  checkProject(o);
  if (!S.link || !S.proj) throw new Error("Connect to an FM-1 running DREADNOUGHT 0.12 or later first.");
  $("pprog").hidden = false;
  S.writing++;
  try {
    await writeProject(S.link, o, { onPart: (f) => { $("pprog").value = f; }, say });
    S.params = await S.link.params();
    S.proj = await S.link.proj();
    setEngines(S.proj.eng);
    S.lfo = await readLfos();
    S.pat = await S.link.readPattern(S.st.patSize);
    S.loops = await S.link.names(1);
    S.dev = null;
    S.media = mediaOf(o);
    for (const m of S.media) if (m.orig) {          // (the originals back: they can be converted again)
      try { const d = await decodeFile(m.orig); m.ch = d.ch; m.rate = d.rate; } catch (_) { /* kept as stored */ }
    }
    S.lastRev = (await S.link.state()).rev;
  } finally { S.writing--; $("pprog").hidden = true; }
  buildBreaks(); refreshValues(); refreshProject(); renderMedia(); render();
  say(`"${o.name}" is on the FM-1. Press Save on FM-1 to keep it there.`, "ok");
}
async function listFolder() {
  const ul = $("pfiles");
  ul.textContent = "";
  if (!S.dir) return;
  const files = [];
  for await (const [name, h] of S.dir.entries()) if (h.kind === "file" && /\.dnproj$/i.test(name)) files.push(h);
  files.sort((a, b) => a.name.localeCompare(b.name));
  for (const h of files) {
    const f = await h.getFile(), li = document.createElement("li");
    li.innerHTML = `<span></span><small class="small">${(f.size / 1024).toFixed(0)} KB · ${new Date(f.lastModified).toLocaleDateString()}</small><button class="quiet small">Load to FM-1</button>`;
    li.querySelector("span").textContent = h.name.replace(/\.dnproj$/i, "");
    li.querySelector("button").onclick = () => guard(async () => loadProject(JSON.parse(await (await h.getFile()).text())));
    ul.appendChild(li);
  }
  if (!files.length) ul.innerHTML = `<li><span class="small">No projects in this folder yet: Save puts them here.</span></li>`;
}
$("psave").onclick = () => guard(saveProject);
$("pload").onclick = () => { fileWant = loadProject; $("file").click(); };
$("pfolder").onclick = () => guard(async () => {
  if (!window.showDirectoryPicker) throw new Error("This browser can't open a folder: Save downloads the file instead (Chrome and Edge can).");
  S.dir = await window.showDirectoryPicker({ id: "dreadnought-projects", mode: "readwrite" });
  await listFolder();
});

// ---- drawing it all -------------------------------------------------------------------------------------------
function render() {
  if (!S.pat) return;
  const tr = S.pat.tracks[S.sel];
  renderTracks();
  const [k] = trackLabel(S.sel);
  $("trkname").textContent = `Track ${S.sel + 1} · ${k}`;
  if (document.activeElement !== $("trklen")) $("trklen").value = tr.len;
  $("swing").value = S.pat.swing;
  $("swingv").textContent = S.pat.swing;
  $("up").hidden = $("down").hidden = isBrk();
  { const { len, n, x0 } = span(); $("left").disabled = x0 === 0; $("right").disabled = x0 + n >= len;
    $("left").hidden = $("right").hidden = n >= len; }
  $("ovlbox").hidden = isBrk();
  $("overlap").value = overlapCut(S.pat, S.sel) ? "cut" : "slide";
  $("hint").textContent = isBrk()
    ? "Click a slice's row to play it on that step, drag to move it, drag its right end to hold it longer. Shift-click: accent. Double-click: reverse. Right-click or Alt-click: delete."
    : "Click to add a note, drag to move it, drag its right end to make it longer. Shift-click: accent. Double-click: slide. A note that reaches past the next one slides into it (Overlap: Slide). Right-click or Alt-click: delete. Arrow keys move the selected note (up / down, along the steps). Below: the selected note's velocity, condition and retrig; shift-drag the velocity lane for a ramp. Scroll for higher or lower notes; drag past an edge and the roll follows.";
  if ($("pattern").classList.contains("on")) { drawRoll(); drawVel(); }
  if ($("lfos").classList.contains("on")) drawLfos();
  renderPats();
  renderInsp();
  if (S.selNote >= 0 && tr.steps[S.selNote] && (tr.steps[S.selNote].flags & ST_ON)) {
    const st = tr.steps[S.selNote];
    $("hint").textContent = `Step ${S.selNote + 1}: ${isBrk() ? `slice ${st.note + 1}` : noteName(st.note + trans())}, `
      + `${lenText(st.len)} step${st.len === 4 ? "" : "s"}, velocity ${st.flags & ST_ACC ? "accent" : st.vel}`
      + `${st.flags & ST_SLIDE ? (isBrk() ? ", reversed" : ", slides in") : !isBrk() && overlapSlide(S.pat, S.sel, S.selNote) ? ", slides in (the note before overlaps it)" : ""}.`;
  }
}
window.addEventListener("resize", render);
renderLib();
if (new URLSearchParams(location.search).has("demo")) $("demo").click();
window.__dn = S;                                     // (the tests look in)
