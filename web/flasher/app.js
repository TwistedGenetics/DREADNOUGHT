// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT flasher page. The update protocol is SLOOP's / Felucca's (vendor/fm1ota.js).
import { Updater } from "./vendor/fm1ota.js";
import { checkPackage, describeDevice, backupAdvice, progressOf, ERRORS } from "./flasher.js";

const $ = (id) => document.getElementById(id);
const state = { midi: null, device: null, pkg: null, writing: false };

const log = (t) => { $("log").textContent += t + "\n"; };
const say = (t, cls = "") => { $("status").textContent = t; $("status").className = cls; };
const setBar = (p) => { if (p != null) $("bar").style.width = p.toFixed(1) + "%"; };

function refresh() {
  $("go").disabled = state.writing || !state.pkg || !$("backed-up").checked || !$("power").checked || !state.midiOk;
  $("detect").disabled = state.writing;
  $("file").disabled = state.writing;
}
["backed-up", "power"].forEach((id) => $(id).addEventListener("change", refresh));

async function midiAccess() {
  if (state.midi) return state.midi;
  state.midi = await navigator.requestMIDIAccess({ sysex: true });
  return state.midi;
}

if (!navigator.requestMIDIAccess) {
  $("support").hidden = false;
  $("detect").disabled = true;
} else {
  state.midiOk = true;
}

// --- 1. connect -------------------------------------------------------------------------------
$("detect").addEventListener("click", async () => {
  $("device").textContent = "Looking…";
  try {
    const up = new Updater(await midiAccess(), log);
    const dev = await up.find(() => true, true);
    if (!dev) { $("device").textContent = "Not found." + up.seen(); return; }
    dev.link.close();
    state.device = describeDevice(dev.id.text);
    $("device").textContent = state.device.label + " on " + dev.name;
    const b = backupAdvice(state.device.kind);
    const box = $("backup-text");
    box.textContent = `Use ${b.tool}. ${b.what} `;
    if (b.url) {
      const a = document.createElement("a");
      a.href = b.url; a.target = "_blank"; a.rel = "noopener"; a.textContent = "Open it";
      box.append(a);
    }
    if (state.device.kind === "loader") box.textContent = "The FM-1 is already in update mode, so a backup isn't possible right now. If this is an install that stopped part-way, press Install to finish it.";
  } catch (e) {
    $("device").textContent = "MIDI access was refused. Allow it in the browser's site settings and try again.";
    log(String(e));
  }
  refresh();
});

// --- 3. firmware ------------------------------------------------------------------------------
async function choose(bytes, label, expected = null) {
  state.pkg = null;
  $("pkg").textContent = "Checking…";
  $("pkg-sha").textContent = "—";
  try {
    const p = await checkPackage(bytes, expected);
    state.pkg = p;
    $("pkg").textContent = `${label} — ${p.product}, ${(p.size / 1024).toFixed(0)} KB`;
    $("pkg-sha").textContent = p.sha256;
  } catch (e) {
    $("pkg").textContent = e.message;
  }
  refresh();
}

async function loadReleases() {
  const box = $("release-list");
  try {
    const r = await fetch("firmware.json", { cache: "no-cache" });
    if (!r.ok) throw new Error("HTTP " + r.status);
    const list = (await r.json()).releases || [];
    box.textContent = "";
    if (!list.length) {
      box.innerHTML = '<p class="small">No releases yet. CI builds are under the repo\'s Actions tab: download one and use the file option below.</p>';
      return;
    }
    for (const rel of list) {
      const row = document.createElement("div");
      row.className = "release";
      const info = document.createElement("div");
      info.innerHTML = `<div class="tag"></div><div class="meta"></div>`;
      info.querySelector(".tag").textContent = rel.tag + (rel.prerelease ? " (beta)" : "");
      info.querySelector(".meta").textContent = `${rel.product} · ${(rel.size / 1024).toFixed(0)} KB · ${rel.published.slice(0, 10)}`;
      const btn = document.createElement("button");
      btn.textContent = "Use this";
      btn.addEventListener("click", async () => {
        document.querySelectorAll(".release").forEach((x) => x.classList.remove("on"));
        row.classList.add("on");
        const res = await fetch(rel.url, { cache: "no-cache" });
        await choose(new Uint8Array(await res.arrayBuffer()), rel.tag, rel);
      });
      row.append(info, btn);
      box.append(row);
    }
  } catch (e) {
    box.innerHTML = '<p class="small">Release list not available here (it appears on the published site). Use a file instead.</p>';
    log("firmware.json: " + e.message);
  }
}
loadReleases();

$("file").addEventListener("change", async (ev) => {
  const f = ev.target.files[0];
  if (!f) return;
  document.querySelectorAll(".release").forEach((x) => x.classList.remove("on"));
  await choose(new Uint8Array(await f.arrayBuffer()), f.name);
});

// --- 4. install -------------------------------------------------------------------------------
window.addEventListener("beforeunload", (e) => {
  if (state.writing) { e.preventDefault(); e.returnValue = "The FM-1 is being written. Leave anyway?"; }
});

$("go").addEventListener("click", async () => {
  if (state.writing || !state.pkg) return;
  if (!confirm(`Install ${state.pkg.product} on the FM-1? Keep it plugged in until it says Done.`)) return;
  state.writing = true; refresh();
  setBar(0);
  const { image, product } = state.pkg;
  const step = (k, a) => {
    log(`${k} ${a ?? ""}`);
    setBar(progressOf(k, a, image.length));
    const words = { start: "Starting…", verify: "The FM-1 is checking the package…", loader: "Restarting into update mode…",
      write: "Writing… keep it plugged in", reboot: "Restarting…", done: "Done. Unplug and replug the FM-1 once." };
    if (words[k]) say(words[k], k === "done" ? "good" : "");
  };
  try {
    const up = new Updater(await midiAccess(), log);
    const loader = await up.find((id) => /^ota-/i.test(id.model), true);
    if (loader) {
      loader.link.close();
      say("Resuming an install that stopped part-way…");
      const ok = await up.resume(image, step);
      say(ok ? "Done. Switch the FM-1 off and on." : ERRORS.stopped, ok ? "good" : "bad");
      if (ok) setBar(100);
    } else {
      await up.install(image, product, step);
    }
  } catch (e) {
    say((ERRORS[e.code] || "Error: " + e.message) + (e.detail ? ` (${e.detail})` : ""), "bad");
    log(String(e && e.stack || e));
  }
  state.writing = false; refresh();
});
refresh();
