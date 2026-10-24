// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT breaks page: slicing a loop by hand. Copyright (C) 2026 Jamie (TwistedGenetics).
//
// A loop's slice points (sample positions, the first always 0) are kept on the FM-1 per loop, keyed by the loop's
// identity (sliceKey: the same as the firmware's dn_brk_key), with the SysEx commands ED_SLICE_GET (50) / SET (51)
// (src/hardware/dn_main_fm1.c). autoSlices is the firmware's onset detector (src/dsp/dn_breaks.c find_hits) in
// JavaScript, so "Auto" here finds what AUTO on the FM-1 finds. editSlices opens the editor.

export const MAX_SLICES = 64;

// FNV-1a over the first 64 bytes of the ADPCM data (fewer if the loop is shorter), xor the length in samples
export function sliceKey(adpcm, n) {
  let h = 2166136261 >>> 0;
  for (let i = 0; i < 64 && i < Math.floor(n / 2); i++) h = Math.imul(h ^ adpcm[i], 16777619) >>> 0;
  return (h ^ n) >>> 0;
}
export const key7 = (k) => [k & 127, (k >>> 7) & 127, (k >>> 14) & 127, (k >>> 21) & 127, (k >>> 28) & 127];
export const pos7 = (v) => [v & 127, (v >> 7) & 127, (v >> 14) & 127];

export function gridSlices(n, bars) {
  const ns = 16 * bars, out = [];
  for (let i = 0; i < ns; i++) out.push(i * Math.floor(n / ns));
  return out;
}

function zeroBefore(s, at, range) {
  let best = at;
  for (let i = Math.max(1, at - range); i <= at && i < s.length; i++)
    if ((s[i - 1] < 0 && s[i] >= 0) || (s[i - 1] > 0 && s[i] <= 0)) best = i;
  return best;
}
export function zeroNear(s, at, range) {
  let best = at, bd = Infinity;
  for (let i = Math.max(1, at - range); i < Math.min(s.length, at + range); i++)
    if (((s[i - 1] < 0 && s[i] >= 0) || (s[i - 1] > 0 && s[i] <= 0)) && Math.abs(i - at) < bd) { best = i; bd = Math.abs(i - at); }
  return best;
}

// the firmware's find_hits, step for step (64-sample frames of |s[n] - s[n-1]|, two together, the rise over frames
// f-2 / f-3, peak-picked against 1.5 x the local mean or 1/20 of the loudest, ~30 ms apart, start refined)
export function autoSlices(s) {
  const H = 64, nf = Math.floor(s.length / H), e = new Array(nf).fill(0), d = new Array(nf).fill(0);
  let last = 0, mx = 0;
  for (let f = 0; f < nf; f++) {
    let acc = 0;
    for (let i = 0; i < H; i++) { const v = s[f * H + i]; acc += Math.abs(v - last); last = v; }
    e[f] = Math.min(32767, Math.floor(acc / H));
  }
  for (let f = nf - 1; f > 0; f--) e[f] = Math.min(65535, e[f] + e[f - 1]);
  for (let f = 0; f < nf; f++) {
    let before = f > 1 ? e[f - 2] : 0;
    if (f > 2 && e[f - 3] > before) before = e[f - 3];
    d[f] = e[f] > before ? e[f] - before : 0;
    if (d[f] > mx) mx = d[f];
  }
  const pos = [], str = [];
  for (let f = 0; f < nf; f++) {
    let local = 0, cnt = 0, pk = true;
    for (let j = Math.max(0, f - 16); j < f + 17 && j < nf; j++) { local += d[j]; cnt++; }
    let thr = Math.floor(local * 3 / (2 * cnt));
    if (thr < Math.floor(mx / 20)) thr = Math.floor(mx / 20);
    for (let j = Math.max(0, f - 2); j <= f + 2 && j < nf; j++)
      if (j !== f && (d[j] > d[f] || (d[j] === d[f] && j < f))) pk = false;
    if (!pk || d[f] <= thr) continue;
    const from = f ? (f - 1) * H : 0, end = Math.min(s.length, (f + 2) * H);
    let pre = 0, top = 0;
    for (let k = from; k < end; k++) {
      const a = Math.abs(s[k]);
      if (k < from + 16 && a > pre) pre = a;
      if (a > top) top = a;
    }
    pre = Math.floor(pre / 2);
    const lvl = 2 * pre + (top > 2 * pre ? Math.floor((top - 2 * pre) / 4) : 0);
    let k = from;
    while (k < end && Math.abs(s[k]) <= lvl) k++;
    k = zeroBefore(s, k, 32);
    if (pos.length && k < pos[pos.length - 1] + 10 * H) {
      if (d[f] > str[str.length - 1]) { pos[pos.length - 1] = k; str[str.length - 1] = d[f]; }
      continue;
    }
    if (pos.length < MAX_SLICES) { pos.push(k); str.push(d[f]); }
    else {
      let w = 0;
      for (let j = 1; j < pos.length; j++) if (str[j] < str[w]) w = j;
      if (str[w] < d[f]) { pos.splice(w, 1); str.splice(w, 1); pos.push(k); str.push(d[f]); }
    }
  }
  if (!pos.length || pos[0] > 10 * H) { if (pos.length === MAX_SLICES) pos.pop(); pos.unshift(0); }
  pos[0] = 0;
  return pos;
}

// ---- the editor -------------------------------------------------------------------------------------------------
let ctx = null;
function play(s, rate, from, to) {                   // hear a slice
  try {
    ctx = ctx || new AudioContext();
    const n = Math.max(1, to - from), buf = ctx.createBuffer(1, n, rate), ch = buf.getChannelData(0);
    for (let i = 0; i < n; i++) ch[i] = s[from + i] / 32768;
    const src = ctx.createBufferSource();
    src.buffer = buf;
    src.connect(ctx.destination);
    src.start();
  } catch (_) { /* no sound: the editor still works */ }
}

const CSS = `
.sl-back { position: fixed; inset: 0; background: rgba(255,255,255,.85); display: flex; align-items: center;
  justify-content: center; z-index: 10; padding: 16px; }
.sl-box { background: #fff; color: #000; border: 1px solid #ccc; box-shadow: 0 8px 32px rgba(0,0,0,.15);
  width: min(960px, 100%); padding: 20px; box-sizing: border-box; }
.sl-box h2 { font-size: 18px; margin: 0 0 4px; }
.sl-box .small { color: #666; font-size: 13px; margin: 0 0 12px; }
.sl-box canvas { width: 100%; height: 220px; display: block; border: 1px solid #ddd; touch-action: none; cursor: crosshair; }
.sl-box .bar { display: flex; flex-wrap: wrap; gap: 8px; margin-top: 12px; align-items: center; }
.sl-box .sp { flex: 1; }
.sl-box button { font: inherit; font-size: 14px; padding: 6px 14px; cursor: pointer; border: 1px solid #ccc; background: #fff; color: #000; }
.sl-box button.go { background: #000; color: #fff; border-color: #000; }
`;

// opens the editor for a loop {s: samples, n}; done(slices) on Done (not on Cancel)
export function editSlices(loop, bars, rate, done, title = "Slices") {
  if (!document.getElementById("sl-css")) {
    const st = document.createElement("style");
    st.id = "sl-css";
    st.textContent = CSS;
    document.head.appendChild(st);
  }
  const s = loop.s, n = loop.n;
  let sl = (loop.slices && loop.slices.length ? loop.slices : gridSlices(n, bars)).slice();
  const back = document.createElement("div");
  back.className = "sl-back";
  back.innerHTML = `<div class="sl-box" role="dialog" aria-label="${title}">
    <h2>${title}</h2>
    <p class="small">Drag a line to move it. Double-click to add one. Right-click (or Alt-click) a line to remove it.
      Click between two lines to hear that slice. ${bars} bar${bars > 1 ? "s" : ""}; up to ${MAX_SLICES} slices.</p>
    <canvas width="1800" height="440"></canvas>
    <div class="bar"><button data-a="auto">Auto (the hits)</button><button data-a="grid">Grid (16 a bar)</button>
      <button data-a="clear">Clear</button><span class="sp"></span><span class="small cnt"></span>
      <button data-a="cancel">Cancel</button><button class="go" data-a="done">Done</button></div></div>`;
  document.body.appendChild(back);
  const cv = back.querySelector("canvas"), g = cv.getContext("2d"), W = cv.width, Hh = cv.height;
  const peaks = new Float32Array(W);
  for (let x = 0; x < W; x++) {
    let pk = 0;
    for (let i = Math.floor(x * n / W), e = Math.floor((x + 1) * n / W); i < e; i++) pk = Math.max(pk, Math.abs(s[i]));
    peaks[x] = pk / 32768;
  }
  const X = (p) => p * W / n, P = (x) => Math.round(Math.max(0, Math.min(n - 1, x * n / W)));
  let drag = -1, hover = -1;
  function draw() {
    g.clearRect(0, 0, W, Hh);
    for (let i = 0; i < sl.length; i++) {             // slices shaded in turn
      const a = X(sl[i]), b = X(i + 1 < sl.length ? sl[i + 1] : n);
      g.fillStyle = i % 2 ? "#f4f7fa" : "#ffffff";
      g.fillRect(a, 0, b - a, Hh);
    }
    g.fillStyle = "#4a8fc4";
    for (let x = 0; x < W; x++) { const h = Math.max(1, peaks[x] * (Hh / 2 - 20)); g.fillRect(x, Hh / 2 - h, 1, 2 * h); }
    g.font = "22px -apple-system, Helvetica, Arial, sans-serif";
    for (let i = 0; i < sl.length; i++) {
      const x = X(sl[i]);
      g.fillStyle = i === hover || i === drag ? "#d0402a" : "#000";
      g.fillRect(x - (i === hover ? 2 : 1), 0, i === hover ? 4 : 2, Hh);
      g.fillText(String(i + 1), x + 6, 24);
    }
    back.querySelector(".cnt").textContent = `${sl.length} slice${sl.length === 1 ? "" : "s"}`;
  }
  const xOf = (ev) => { const r = cv.getBoundingClientRect(); return (ev.clientX - r.left) * W / r.width; };
  const near = (x) => { let best = -1, bd = 12; sl.forEach((p, i) => { const d = Math.abs(X(p) - x); if (d < bd) { bd = d; best = i; } }); return best; };
  cv.onpointerdown = (ev) => {
    const x = xOf(ev), i = near(x);
    if (i >= 0 && (ev.button === 2 || ev.altKey)) {   // remove (never the first)
      if (i > 0) sl.splice(i, 1);
      draw();
      return;
    }
    if (i > 0) { drag = i; cv.setPointerCapture(ev.pointerId); draw(); return; }
    if (ev.button === 0) {                            // hear the slice under the pointer
      const p = P(x);
      let k = 0;
      while (k + 1 < sl.length && sl[k + 1] <= p) k++;
      play(s, rate, sl[k], k + 1 < sl.length ? sl[k + 1] : n);
    }
  };
  cv.onpointermove = (ev) => {
    const x = xOf(ev);
    if (drag > 0) {
      const lo = sl[drag - 1] + 64, hi = (drag + 1 < sl.length ? sl[drag + 1] : n) - 64;
      sl[drag] = Math.max(lo, Math.min(hi, P(x)));
    } else hover = near(x);
    draw();
  };
  cv.onpointerup = () => {
    if (drag > 0) {
      const lo = sl[drag - 1] + 64, hi = (drag + 1 < sl.length ? sl[drag + 1] : n) - 64;
      sl[drag] = Math.max(lo, Math.min(hi, zeroNear(s, sl[drag], 32)));
    }
    drag = -1;
    draw();
  };
  cv.oncontextmenu = (ev) => ev.preventDefault();
  cv.ondblclick = (ev) => {                          // add a point (to the zero crossing nearby)
    if (sl.length >= MAX_SLICES) return;
    const p = zeroNear(s, P(xOf(ev)), 32);
    if (sl.some((q) => Math.abs(q - p) < 64)) return;
    sl.push(p);
    sl.sort((a, b) => a - b);
    draw();
  };
  const close = () => back.remove();
  back.querySelectorAll("button").forEach((b) => b.onclick = () => {
    const a = b.dataset.a;
    if (a === "auto") sl = autoSlices(s);
    if (a === "grid") sl = gridSlices(n, bars);
    if (a === "clear") sl = [0];
    if (a === "cancel") return close();
    if (a === "done") { close(); return done(sl.slice()); }
    draw();
  });
  back.onkeydown = (ev) => { if (ev.key === "Escape") close(); };
  draw();
  back.querySelector(".go").focus();
}
