// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT web editor: the page. Copyright (C) 2026 Jamie (TwistedGenetics).
import {
  connect, REESE, BPM_INDEX, brkParams, brkIndex, KIND, ST_ON, ST_SLIDE, ST_ACC, LEN_STEP, LEN_MAX, noteEnd, lenText,
  noteName, parsePattern, serializePattern, overlapCut, setOverlapCut, overlapSlide, HIDDEN, NLFO, LFO_PTS, lfoParams,
  lfoIndex, NPARAM, lfoShapeAt,
} from "./protocol.js";

const $ = (id) => document.getElementById(id);
const S = {
  link: null, mock: null, st: null, params: [], presets: [], loops: [], pat: null, sel: 0, lo: 36, playStep: -1,
  writing: 0, lastRev: -1, poll: null, dirtyPat: false, drag: null, selNote: -1, zoom: 0, x0: 0,
  lfo: null, lfoNow: null, lfoDraw: null,
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
  S.loops = await link.names(1);
  S.params = await link.params();
  S.pat = await link.readPattern(S.st.patSize);
  S.lfo = await readLfos();
  S.lastRev = S.st.rev;
  S.sel = 0;
  fit();
  buildSound();
  buildBreaks();
  buildLfos();
  ["play", "bpm", "save"].forEach((id) => { $(id).disabled = false; });
  say(label, "ok");
  render();
  clearInterval(S.poll);
  S.poll = setInterval(poll, 250);
}

$("connect").onclick = () => guard(async () => {
  if (!navigator.requestMIDIAccess) throw new Error("This browser can't talk to MIDI devices. Use Chrome or Edge.");
  say("Connecting…");
  const midi = await navigator.requestMIDIAccess({ sysex: true });
  S.connLog = [];
  const link = await connect(midi, (m) => { S.connLog.push(m); console.log("[DREADNOUGHT connect]", m); });
  await start(link, `Connected: ${link.name}`);
});
$("demo").onclick = () => guard(async () => {
  const { MockFM1 } = await import("./mock-fm1.js");
  if (S.mock) S.mock.close();
  S.mock = new MockFM1();
  const link = await connect(S.mock.access);
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
    refreshValues();
  }
  if (S.lfo && $("lfos").classList.contains("on")) S.lfoNow = await S.link.lfoNow().catch(() => null);
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
  if (i >= lfoIndex(0, 0) && (i - lfoIndex(0, 0)) % 8 === 1) {   // SYNC resets RATE (as the FM-1 does)
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
  render();
});

// ---- the pattern: tracks ----------------------------------------------------------------------------------------
function trackLabel(t) {
  const k = KIND(t);
  return k === "REESE" ? ["REESE", "bass"] : k === "BREAKS" ? [t === 1 ? "BREAKS A" : "BREAKS B", "slices"]
    : ["MIDI", `channel ${S.pat.tracks[t].chan + 1}`];
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
const slices = () => (S.st ? S.st.slices[S.sel - 1] || 32 : 32);

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
    const v = valOf(r), y = r * g.rh, pc = (((v + (S.sel === 0 ? S.params[17] || 0 : 0)) % 12) + 12) % 12;
    const black = !isBrk() && [1, 3, 6, 8, 10].includes(pc);
    c.fillStyle = black ? "#e6e9ee" : "#fbfcfd";
    c.fillRect(GUT, y, g.W - GUT, g.rh);
    c.fillStyle = black ? "#2b2f36" : "#ffffff";
    c.fillRect(0, y, GUT - 2, g.rh - 1);
    if (isBrk() || pc === 0) {
      c.fillStyle = black ? "#fff" : "#6b7280";
      c.font = "10px -apple-system, Helvetica, Arial";
      c.fillText(isBrk() ? `SL ${v + 1}` : noteName(v + (S.sel === 0 ? S.params[17] || 0 : 0)), 4, y + g.rh - 3);
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
let velDrag = false;
function velAt(ev) {
  const cv = $("vel"), r = cv.getBoundingClientRect(), tr = S.pat.tracks[S.sel], { n, x0 } = span(), cw = (r.width - GUT) / n;
  const i = x0 + Math.floor((ev.clientX - r.left - GUT) / cw), v = Math.round(127 * (1 - (ev.clientY - r.top - 4) / (72 - 16)));
  if (i < 0 || i >= tr.len || !(tr.steps[i].flags & ST_ON)) return;
  tr.steps[i].vel = Math.max(1, Math.min(127, v));
  tr.steps[i].flags &= ~ST_ACC;
  render();
}
$("vel").onpointerdown = (ev) => { velDrag = true; $("vel").setPointerCapture(ev.pointerId); velAt(ev); };
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
  const ps = $("preset");
  ps.textContent = "";
  S.presets.forEach((n, k) => { const o = document.createElement("option"); o.value = k; o.textContent = n; ps.appendChild(o); });
  ps.value = S.st.preset;
  ps.onchange = () => guard(async () => { await S.link.preset(+ps.value); S.params = await S.link.params(); refreshValues(); });
}
function buildBreaks() {
  [0, 1].forEach((k) => {
    const box = $(k ? "brkB" : "brkA"), g = document.createElement("div");
    box.textContent = "";
    g.className = "grp";
    g.innerHTML = `<h3>BREAKS ${k ? "B · TRACK 3" : "A · TRACK 2"}</h3>`;
    box.appendChild(g);
    brkParams(S.loops).forEach((p, i) => slider(g, brkIndex(k, i), p));
  });
}
// ---- the LFOs (0.9) ------------------------------------------------------------------------------------------
async function readLfos() {                          // the DRAW points (an FM-1 before 0.9 has no LFOs)
  if (!S.st || S.st.nparams < NPARAM) return null;
  const out = [];
  for (let k = 0; k < NLFO; k++) out.push(await S.link.lfoPts(k));
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
  for (let k = 0; k < NLFO; k++) {
    const g = document.createElement("div"), ps = lfoParams();
    g.className = "grp lfo";
    g.innerHTML = `<h3>LFO ${k + 1}</h3><div class="lfo-prm"></div>
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
    const i = +el.dataset.i, inp = el.querySelector("input,select");
    if (inp === document.activeElement) return;
    inp.value = S.params[i];
    const v = el.querySelector(".v");
    if (v) v.textContent = el.p.text ? el.p.text(S.params[i]) : S.params[i];
  });
  if (S.st) $("preset").value = S.st.preset;
  document.querySelectorAll(".lfo .prm").forEach((el) => {   // RATE: note lengths synced, Hz free
    if (el.p.name === "RATE") el.querySelector("input").max = S.params[+el.dataset.i - 1] ? 15 : 127;
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
$("backup").onclick = () => {
  if (!S.pat) return say("Connect first.", "bad");
  download(`dreadnought-${new Date().toISOString().slice(0, 10)}.json`,
    { format: "dreadnought-backup", version: 2, params: S.params, pattern: b64(serializePattern(S.pat)), lfo: S.lfo });
};
$("restore").onclick = () => {
  if (!S.pat) return say("Connect first.", "bad");
  fileWant = async (o) => {
    if (o.format !== "dreadnought-backup") throw new Error("That file is not a DREADNOUGHT backup.");
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
  };
  $("file").click();
};
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
    : "Click to add a note, drag to move it, drag its right end to make it longer. Shift-click: accent. Double-click: slide. A note that reaches past the next one slides into it (Overlap: Slide). Right-click or Alt-click: delete. Arrow keys move the selected note (up / down, along the steps). Scroll for higher or lower notes; drag past an edge and the roll follows.";
  if ($("pattern").classList.contains("on")) { drawRoll(); drawVel(); }
  if ($("lfos").classList.contains("on")) drawLfos();
  if (S.selNote >= 0 && tr.steps[S.selNote] && (tr.steps[S.selNote].flags & ST_ON)) {
    const st = tr.steps[S.selNote];
    $("hint").textContent = `Step ${S.selNote + 1}: ${isBrk() ? `slice ${st.note + 1}` : noteName(st.note + (S.sel === 0 ? S.params[17] : 0))}, `
      + `${lenText(st.len)} step${st.len === 4 ? "" : "s"}, velocity ${st.flags & ST_ACC ? "accent" : st.vel}`
      + `${st.flags & ST_SLIDE ? (isBrk() ? ", reversed" : ", slides in") : !isBrk() && overlapSlide(S.pat, S.sel, S.selNote) ? ", slides in (the note before overlaps it)" : ""}.`;
  }
}
window.addEventListener("resize", render);
renderLib();
if (new URLSearchParams(location.search).has("demo")) $("demo").click();
window.__dn = S;                                     // (the tests look in)
