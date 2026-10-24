// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT web editor: the panel (0.26), the FM-1 on the computer's screen, big. Copyright (C) 2026 Jamie
// (TwistedGenetics).
//
// The FM-1 says what its screen shows (UI_STATE: the page, its tabs, the four knobs with their names, values and
// texts, the status line, the lit buttons and keys) and the page presses its buttons, turns its encoders and plays its
// keys (UI_INPUT) as the panel does. So every page and every instrument works here as it does on the FM-1, including
// ones added later, with nothing to change in the editor. Laid out as the FM-1 is: the encoders and OCT on the left,
// the screen, the knobs and the two rows of buttons on the right, the 27 keys below.
import { BTN, ENC, NKEYS, UI_IN } from "./protocol.js";

const $ = (id) => document.getElementById(id);
const ROW1 = ["FX", "SEL", "ENV", "LFO", "EDIT", "GLO"], ROW2 = ["HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"];
const BLABEL = { PLAY: "PLAY/STOP", OCTDN: "OCT −", OCTUP: "OCT +" };
const NN = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const keyNote = (k) => 53 + k;                       // (key 0 is F3)
const isBlack = (k) => [1, 3, 6, 8, 10].includes(keyNote(k) % 12);
// the computer's keys: the bottom row F3..E4, the top row F4..G5 (as a DAW's)
const KB = {};
"zsxdcfvbhnjm".split("").forEach((c, i) => { KB[c] = i; });
"q2w3e4rt6y7ui9o".split("").forEach((c, i) => { KB[c] = 12 + i; });

export function initPanel(S, { guard, say, onSeqPage }) {
  const latched = new Set();                         // (buttons held down by shift-click: SAVE + FX, HOME + SELECT ...)
  let ui = null, busy = false, timer = 0, keysDown = new Set();
  const send = (kind, id, v) => { if (!S.link) return; guard(async () => { await S.link.uiInput(kind, id, v); refresh(); }); };
  const turn = (enc, d) => { if (d) send(UI_IN.TURN, enc, Math.max(1, Math.min(127, 64 + d))); };

  // ---- the controls, built once ----
  function button(name) {
    const b = document.createElement("button");
    b.className = "pbtn";
    b.dataset.b = name;
    b.innerHTML = `<i></i><span>${BLABEL[name] || name}</span>`;
    b.title = "Click: press. Shift-click: hold it down (click again to let go), for SAVE + FX, HOME + SELECT, SEQ + a key…";
    b.onpointerdown = (ev) => {
      ev.preventDefault();
      if (ev.shiftKey || ev.button === 2) {
        if (latched.has(name)) { latched.delete(name); send(UI_IN.UP, BTN[name]); } else { latched.add(name); send(UI_IN.DOWN, BTN[name]); }
        paint();
        return;
      }
      if (latched.has(name)) { latched.delete(name); send(UI_IN.UP, BTN[name]); paint(); return; }
      send(UI_IN.DOWN, BTN[name]);
      b.classList.add("down");
      const up = () => { window.removeEventListener("pointerup", up); b.classList.remove("down"); send(UI_IN.UP, BTN[name]); };
      window.addEventListener("pointerup", up);
    };
    b.oncontextmenu = (ev) => ev.preventDefault();
    return b;
  }
  // an encoder: drag up / down, the wheel, or its arrows
  function encoder(el, enc, label) {
    el.classList.add("penc");
    el.innerHTML = `<button class="quiet small" aria-label="${label} left">◀</button><div class="dial"><span></span></div><button class="quiet small" aria-label="${label} right">▶</button><b>${label}</b>`;
    const [l, r] = el.querySelectorAll("button"), d = el.querySelector(".dial");
    l.onclick = () => turn(enc, -1);
    r.onclick = () => turn(enc, 1);
    dragTurn(d, (n) => turn(enc, n));
  }
  function dragTurn(el, fn) {                        // (8 px a detent; shift: 4 at a time)
    el.onwheel = (ev) => { ev.preventDefault(); fn((ev.deltaY < 0 ? 1 : -1) * (ev.shiftKey ? 4 : 1)); };
    el.onpointerdown = (ev) => {
      ev.preventDefault();
      el.setPointerCapture(ev.pointerId);
      let y0 = ev.clientY, acc = 0;
      el.onpointermove = (e) => {
        acc += (y0 - e.clientY) / 8;
        y0 = e.clientY;
        const n = Math.trunc(acc);
        if (n) { acc -= n; fn(n * (e.shiftKey ? 4 : 1)); }
      };
      el.onpointerup = () => { el.onpointermove = null; };
    };
  }
  function build() {
    encoder($("p-enc-sel"), ENC.SELECT, "SELECT");
    encoder($("p-enc-pre"), ENC.PRESET, "PRESETS");
    encoder($("p-enc-alg"), ENC.ALGO, "ALGORITHM");
    $("p-oct").append(button("OCTDN"), button("OCTUP"));
    ROW1.forEach((n) => $("p-row1").appendChild(button(n)));
    ROW2.forEach((n) => $("p-row2").appendChild(button(n)));
    const kn = $("s-knobs");
    for (let k = 0; k < 4; k++) {
      const d = document.createElement("div");
      d.className = "skn";
      d.innerHTML = `<div class="nm"></div><svg viewBox="0 0 100 100"><circle class="ring" cx="50" cy="50" r="38"/><path class="arc"/><line class="ptr" x1="50" y1="50" x2="50" y2="18"/></svg><div class="tx"></div>`;
      d.tabIndex = 0;
      d.title = `KNOB ${k + 1}: drag up / down, the wheel, or the arrow keys (shift: faster)`;
      dragTurn(d, (n) => turn(ENC.K1 + k, n));
      d.onkeydown = (ev) => {
        const n = ev.key === "ArrowUp" || ev.key === "ArrowRight" ? 1 : ev.key === "ArrowDown" || ev.key === "ArrowLeft" ? -1 : 0;
        if (n) { ev.preventDefault(); turn(ENC.K1 + k, n * (ev.shiftKey ? 4 : 1)); }
      };
      kn.appendChild(d);
    }
    const kb = $("p-keys");
    let wx = 0;
    for (let k = 0; k < NKEYS; k++) {                 // white keys in a row, black ones over the gaps
      const e = document.createElement("div");
      e.className = isBlack(k) ? "pk b" : "pk w";
      e.dataset.k = k;
      if (!isBlack(k)) { e.style.left = `calc(${wx} * var(--kw))`; wx++; } else e.style.left = `calc(${wx} * var(--kw) - var(--kw) * 0.32)`;
      e.title = `${NN[keyNote(k) % 12]}${Math.floor(keyNote(k) / 12) - 2}`;
      e.onpointerdown = (ev) => {
        ev.preventDefault();
        send(UI_IN.KEYDOWN, k);
        e.classList.add("down");
        const up = () => { window.removeEventListener("pointerup", up); e.classList.remove("down"); send(UI_IN.KEYUP, k); };
        window.addEventListener("pointerup", up);
      };
      kb.appendChild(e);
    }
    kb.style.setProperty("--nw", wx);
    $("s-tabs").onclick = (ev) => {                  // a tab: SELECT round to it (the sound pages cycle that way)
      const t = ev.target.closest(".stab");
      if (!t || !ui || ui.tabSel === 127) return;
      turn(ENC.SELECT, +t.dataset.i - ui.tabSel);
    };
    $("p-seqopen").onclick = () => onSeqPage(ui);
    window.addEventListener("keydown", (ev) => {     // the computer's keyboard plays the keys (not while typing)
      if (!visible() || ev.repeat || ev.metaKey || ev.ctrlKey || ev.altKey || /INPUT|SELECT|TEXTAREA/.test(document.activeElement.tagName)) return;
      const k = KB[ev.key.toLowerCase()];
      if (k === undefined || keysDown.has(k)) return;
      keysDown.add(k);
      send(UI_IN.KEYDOWN, k);
      ev.preventDefault();
    });
    window.addEventListener("keyup", (ev) => {
      const k = KB[ev.key.toLowerCase()];
      if (k === undefined || !keysDown.has(k)) return;
      keysDown.delete(k);
      send(UI_IN.KEYUP, k);
    });
    window.addEventListener("blur", () => { if (S.link && (latched.size || keysDown.size)) { latched.clear(); keysDown.clear(); send(UI_IN.ALLUP, 0); paint(); } });
  }

  // ---- what the FM-1 shows, drawn ----
  const ARC0 = -135, ARC1 = 135;
  const pt = (deg, r) => { const a = (deg - 90) * Math.PI / 180; return [50 + r * Math.cos(a), 50 + r * Math.sin(a)]; };
  function arc(from, to) {
    const [x0, y0] = pt(from, 38), [x1, y1] = pt(to, 38);
    return `M${x0.toFixed(2)} ${y0.toFixed(2)} A38 38 0 ${to - from > 180 ? 1 : 0} 1 ${x1.toFixed(2)} ${y1.toFixed(2)}`;
  }
  function paint() {
    document.querySelectorAll(".pbtn").forEach((b) => {
      const id = BTN[b.dataset.b];
      b.classList.toggle("lit", !!(ui && (ui.litBtn >> id) & 1));
      b.classList.toggle("held", latched.has(b.dataset.b));
    });
    if (!ui) return;
    $("s-bpm").textContent = `${ui.bpm} BPM`;
    $("s-pos").textContent = ui.playing ? `▶ ${Math.floor(Math.max(0, ui.pos - 1) / 16) % 100 + 1}.${Math.floor(Math.max(0, ui.pos - 1) / 4) % 4 + 1}` : "";
    $("s-rec").hidden = !ui.rec;
    $("s-tag").textContent = ui.tag;
    $("s-name").textContent = ui.name + (ui.dirty ? " *" : "");
    const tabs = $("s-tabs");
    tabs.textContent = "";
    ui.tabs.forEach((t, i) => { const e = document.createElement("span"); e.className = `stab${i === ui.tabSel ? " on" : ""}`; e.dataset.i = i; e.textContent = t; tabs.appendChild(e); });
    tabs.hidden = !ui.tabs.length;
    const seqView = ui.seq && !ui.naming;
    $("s-seq").hidden = !seqView;
    $("s-naming").hidden = !ui.naming;
    if (ui.naming) {
      const nm = $("s-nmbox");
      nm.textContent = "";
      [...ui.name.padEnd(Math.max(ui.name.length, 1))].forEach((c, i) => {
        const e = document.createElement("span");
        e.className = i === ui.nameCur ? "on" : "";
        e.textContent = c === " " ? "_" : c;
        nm.appendChild(e);
      });
    }
    document.querySelectorAll(".skn").forEach((d, k) => {
      const kn = ui.knobs[k];
      d.classList.toggle("off", !kn);
      d.querySelector(".nm").textContent = kn ? kn.name : seqView ? `K${k + 1}` : "";
      d.querySelector(".tx").textContent = kn ? kn.text : "";
      const f = kn && kn.max > kn.min ? (kn.value - kn.min) / (kn.max - kn.min) : 0, deg = ARC0 + f * (ARC1 - ARC0);
      d.querySelector(".arc").setAttribute("d", kn ? (kn.min < 0 && kn.max > 0 ? arc(Math.min(0, deg), Math.max(0, deg) + 0.01) : arc(ARC0, deg + 0.01)) : "");
      const [x, y] = pt(deg, 30);
      const p = d.querySelector(".ptr");
      p.setAttribute("x2", x.toFixed(2)); p.setAttribute("y2", y.toFixed(2));
      p.style.visibility = kn ? "visible" : "hidden";
    });
    $("s-left").textContent = `OCT ${ui.oct > 0 ? "+" : ""}${ui.oct}${ui.hold ? "   HOLD" : ""}   T${ui.trk + 1}${ui.status ? `   ${ui.status}` : ""}`;
    $("s-msg").textContent = ui.msg || (ui.dirty ? "SAVE?" : "SAVED");
    $("s-msg").className = ui.msg ? "msg" : ui.dirty ? "dirty" : "";
    document.querySelectorAll(".pk").forEach((e) => e.classList.toggle("lit", !!((ui.litKeys >> +e.dataset.k) & 1)));
  }
  const visible = () => $("panel").classList.contains("on");
  async function refresh() {
    if (!S.link || busy || S.writing) return;
    busy = true;
    try { ui = await S.link.ui(); paint(); } catch (_) { /* (an FM-1 before 0.26 has no panel) */ } finally { busy = false; }
  }
  build();
  return {
    start() { clearInterval(timer); timer = setInterval(() => { if (visible()) refresh(); }, 150); refresh(); },
    refresh,
    get state() { return ui; },
  };
}
