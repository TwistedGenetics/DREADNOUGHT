// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT for iPad: what the web pages get from Chrome that Safari's engine on iPad doesn't have, supplied by the
// app (ios/Sources). Injected before any page script runs. Copyright (C) 2026 Jamie (TwistedGenetics).
//
//  - Web MIDI (navigator.requestMIDIAccess, with SysEx): the ports are CoreMIDI's, so the FM-1 on USB, and anything
//    else the iPad sees, appear as they do in Chrome. Ports come and go live (the flasher needs that: the FM-1
//    restarts into its loader during an install).
//  - Downloads (a link with `download` clicked by a script): saved in the app's folder in Files
//    (On My iPad > DREADNOUGHT), as Chrome saves them to Downloads.
//  - showDirectoryPicker (the editor's projects folder): the app's Projects folder in Files.
//  - navigator.clipboard.writeText (Diagnostics' Copy all).
(() => {
  "use strict";
  const H = window.webkit && window.webkit.messageHandlers;
  if (!H || !H.dnmidi || !H.dnfiles) return;               // (not in the app: leave the page as it is)
  const midiCall = (m) => H.dnmidi.postMessage(m);
  const fileCall = (m) => H.dnfiles.postMessage(m);

  const toB64 = (u8) => {
    let s = "";
    for (let i = 0; i < u8.length; i += 0x8000) s += String.fromCharCode.apply(null, u8.subarray(i, i + 0x8000));
    return btoa(s);
  };
  const fromB64 = (s) => {
    const b = atob(s), u = new Uint8Array(b.length);
    for (let i = 0; i < b.length; i++) u[i] = b.charCodeAt(i);
    return u;
  };
  const bytesOf = async (d) => {
    if (d instanceof Uint8Array) return d;
    if (d instanceof ArrayBuffer) return new Uint8Array(d);
    if (ArrayBuffer.isView(d)) return new Uint8Array(d.buffer, d.byteOffset, d.byteLength);
    if (d instanceof Blob) return new Uint8Array(await d.arrayBuffer());
    if (d && typeof d === "object" && "data" in d) return bytesOf(d.data);   // ({ type: "write", data })
    return new TextEncoder().encode(String(d));
  };

  // ---- Web MIDI ----------------------------------------------------------------------------------------------------
  class Port extends EventTarget {
    constructor(d, type) {
      super();
      this.id = d.id; this.name = d.name; this.manufacturer = d.manufacturer || ""; this.version = "";
      this.type = type; this.state = "connected"; this.connection = "closed"; this.onstatechange = null;
    }
    async open() { this.connection = "open"; return this; }
    async close() { this.connection = "closed"; return this; }
    _changed() {
      const ev = new Event("statechange");
      ev.port = this;
      this.dispatchEvent(ev);
      if (typeof this.onstatechange === "function") this.onstatechange(ev);
    }
  }
  class Input extends Port {
    constructor(d) { super(d, "input"); this._onmsg = null; }
    get onmidimessage() { return this._onmsg; }
    set onmidimessage(f) { this._onmsg = f; if (f) this.connection = "open"; }
    _deliver(data) {
      const ev = new MessageEvent("midimessage", { data });
      this.dispatchEvent(ev);
      if (typeof this._onmsg === "function") this._onmsg(ev);
    }
  }
  class Output extends Port {
    constructor(d) { super(d, "output"); }
    send(data) {
      if (this.state !== "connected") throw new DOMException("The MIDI port is disconnected.", "InvalidStateError");
      const u = data instanceof Uint8Array ? data : Uint8Array.from(data);
      this.connection = "open";
      midiCall({ op: "send", id: this.id, data: toB64(u) }).catch(() => {});
    }
    clear() {}
  }

  const inputs = new Map(), outputs = new Map();
  let access = null, starting = null;
  const sync = (list) => {                       // the ports CoreMIDI has now: new ones added, gone ones disconnected
    const changed = [];
    for (const [map, Kind, now] of [[inputs, Input, list.inputs || []], [outputs, Output, list.outputs || []]]) {
      const ids = new Set(now.map((d) => d.id));
      for (const [id, p] of [...map]) {
        if (!ids.has(id)) { p.state = "disconnected"; p.connection = "closed"; map.delete(id); changed.push(p); }
      }
      for (const d of now) {
        const p = map.get(d.id);
        if (!p) { const q = new Kind(d); map.set(d.id, q); changed.push(q); } else if (p.name !== d.name) p.name = d.name;
      }
    }
    if (access) for (const p of changed) {
      p._changed();
      const ev = new Event("statechange");
      ev.port = p;
      access.dispatchEvent(ev);
      if (typeof access.onstatechange === "function") access.onstatechange(ev);
    }
  };
  class Access extends EventTarget {
    constructor() { super(); this.inputs = inputs; this.outputs = outputs; this.sysexEnabled = true; this.onstatechange = null; }
  }
  const requestMIDIAccess = async () => {
    if (!starting) starting = midiCall({ op: "start" }).then((list) => { sync(list); access = access || new Access(); });
    try { await starting; } catch (e) {
      starting = null;
      throw new DOMException(`MIDI couldn't start: ${e && e.message ? e.message : e}`, "NotAllowedError");
    }
    return access;
  };
  // (the app calls these: a frame from a port, and the port list when a device comes or goes)
  Object.defineProperty(window, "__dnMIDI", { value: Object.freeze({
    recv(id, frames) {
      const p = inputs.get(id);
      if (!p) return;
      for (const f of frames) p._deliver(fromB64(f));
    },
    ports(list) { sync(list); },
  }) });
  Object.defineProperty(Navigator.prototype, "requestMIDIAccess",
    { value: requestMIDIAccess, configurable: true, writable: true });

  // ---- downloads: into the app's folder in Files ---------------------------------------------------------------------
  const nativeClick = HTMLAnchorElement.prototype.click;
  const saveLink = (a) => {
    const name = (a.getAttribute("download") || a.download || "download").split(/[\\/]/).pop() || "download";
    fetch(a.href).then((r) => r.arrayBuffer())                  // (started now: the page revokes the blob URL soon)
      .then((buf) => fileCall({ op: "save", name, data: toB64(new Uint8Array(buf)) }))
      .catch((e) => fileCall({ op: "toast", text: `Couldn't save ${name}: ${e && e.message ? e.message : e}` }));
  };
  HTMLAnchorElement.prototype.click = function () {
    if (this.hasAttribute("download") && /^(blob|data):/i.test(this.href || "")) { saveLink(this); return; }
    return nativeClick.call(this);
  };
  document.addEventListener("click", (ev) => {                 // (a download link the user taps)
    const a = ev.target && ev.target.closest && ev.target.closest("a[download]");
    if (a && /^(blob|data):/i.test(a.href || "")) { ev.preventDefault(); saveLink(a); }
  }, true);

  // ---- a projects folder: Files > On My iPad > DREADNOUGHT > Projects ------------------------------------------------
  const notFound = (name) => new DOMException(`${name} isn't in the folder.`, "NotFoundError");
  class FileHandle {
    constructor(name, info) { this.kind = "file"; this.name = name; this._info = info; }
    async getFile() {
      const r = await fileCall({ op: "read", name: this.name });
      if (!r) throw notFound(this.name);
      return new File([fromB64(r.data)], this.name, { lastModified: r.mtime });
    }
    async createWritable() {
      const parts = [], name = this.name;
      return {
        async write(d) { parts.push(await bytesOf(d)); },
        async close() {
          const n = parts.reduce((a, p) => a + p.length, 0), all = new Uint8Array(n);
          let o = 0;
          for (const p of parts) { all.set(p, o); o += p.length; }
          await fileCall({ op: "write", name, data: toB64(all) });
        },
        async abort() { parts.length = 0; },
      };
    }
    async isSameEntry(h) { return !!h && h.kind === "file" && h.name === this.name; }
  }
  class DirHandle {
    constructor() { this.kind = "directory"; this.name = "Projects"; }
    async getFileHandle(name, opt = {}) {
      const list = await fileCall({ op: "list" });
      const have = list.find((f) => f.name === name);
      if (!have && !opt.create) throw notFound(name);
      return new FileHandle(name, have);
    }
    async removeEntry(name) { await fileCall({ op: "remove", name }); }
    async *entries() { for (const f of await fileCall({ op: "list" })) yield [f.name, new FileHandle(f.name, f)]; }
    async *values() { for await (const [, h] of this.entries()) yield h; }
    async *keys() { for await (const [n] of this.entries()) yield n; }
    [Symbol.asyncIterator]() { return this.entries(); }
    async queryPermission() { return "granted"; }
    async requestPermission() { return "granted"; }
    async isSameEntry(h) { return !!h && h.kind === "directory"; }
  }
  window.showDirectoryPicker = async () => {
    fileCall({ op: "toast", text: "Projects folder: Files › On My iPad › DREADNOUGHT › Projects" }).catch(() => {});
    return new DirHandle();
  };

  // ---- touch: what a mouse and keyboard give the pages, by finger ----------------------------------------------------
  // SHIFT and DELETE toggles (in the editor's top bar, or in a corner): while lit, every click / drag has Shift (accent,
  // hold a panel button down, a velocity ramp, faster knobs) or Alt (delete a note: the editor's right-click / Alt-click)
  // held. A double tap is a double-click (slide, reverse), as iPad's WebKit doesn't always give one.
  const mods = { shift: false, alt: false };
  for (const [key, m] of [["shiftKey", "shift"], ["altKey", "alt"]]) {
    const d = Object.getOwnPropertyDescriptor(MouseEvent.prototype, key);
    if (d && d.get) Object.defineProperty(MouseEvent.prototype, key,
      { configurable: true, enumerable: true, get() { return mods[m] || d.get.call(this); } });
  }
  const touchBar = () => {
    if (document.getElementById("dn-touch")) return;
    const st = document.createElement("style");
    st.textContent = `
      #dn-touch { display: flex; gap: 8px; align-items: center; z-index: 9999; }
      #dn-touch.float { position: fixed; right: 12px; bottom: 12px; background: #fff; padding: 6px; border-radius: 12px;
        border: 2px solid #b9b7ae; }
      #dn-touch button { font: 800 17px/1 "Barlow Semi Condensed", "Arial Narrow", Arial, sans-serif; letter-spacing: .04em;
        min-width: 92px; padding: 10px 14px; border-radius: 8px; background: #fff; color: #101114;
        border: 2px solid #101114; cursor: pointer; -webkit-user-select: none; user-select: none; }
      #dn-touch button.on { background: #ff6a2b; border-color: #101114; color: #101114; }`;
    document.head.appendChild(st);
    const bar = document.createElement("div");
    bar.id = "dn-touch";
    for (const [m, label, title] of [["shift", "SHIFT", "Shift held for every tap: accent, hold a button down, a velocity ramp"],
      ["alt", "DELETE", "Tap a note to delete it (the right-click)"]]) {
      const b = document.createElement("button");
      b.type = "button"; b.textContent = label; b.title = title; b.dataset.mod = m;
      b.setAttribute("aria-pressed", "false");
      b.addEventListener("pointerdown", (ev) => ev.stopPropagation());
      b.addEventListener("click", (ev) => {
        ev.stopPropagation();
        mods[m] = !mods[m];
        if (mods[m]) mods[m === "shift" ? "alt" : "shift"] = false;      // (one at a time)
        for (const x of bar.querySelectorAll("button")) {
          x.classList.toggle("on", mods[x.dataset.mod]);
          x.setAttribute("aria-pressed", String(mods[x.dataset.mod]));
        }
      });
      bar.appendChild(b);
    }
    const top = document.querySelector("header.top");
    if (top) top.appendChild(bar); else { bar.classList.add("float"); document.body.appendChild(bar); }
  };
  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", touchBar); else touchBar();

  let lastTap = null, synthAt = 0;
  document.addEventListener("pointerup", (ev) => {
    if (ev.pointerType !== "touch") return;
    const now = performance.now(), t = ev.target;
    if (lastTap && lastTap.t === t && now - lastTap.time < 350 &&
        Math.hypot(ev.clientX - lastTap.x, ev.clientY - lastTap.y) < 24) {
      lastTap = null;
      synthAt = now;
      const o = { bubbles: true, cancelable: true, view: window, detail: 2, clientX: ev.clientX, clientY: ev.clientY,
        screenX: ev.screenX, screenY: ev.screenY };
      setTimeout(() => t.dispatchEvent(new MouseEvent("dblclick", o)), 0);
    } else lastTap = { t, time: now, x: ev.clientX, y: ev.clientY };
  }, true);
  document.addEventListener("dblclick", (ev) => {           // (WebKit's own, after ours: not twice)
    if (ev.isTrusted && performance.now() - synthAt < 600) ev.stopImmediatePropagation();
  }, true);

  // ---- the clipboard -------------------------------------------------------------------------------------------------
  const clip = { writeText: (text) => fileCall({ op: "clip", text: String(text) }).then(() => undefined) };
  try {
    if (navigator.clipboard) navigator.clipboard.writeText = clip.writeText;
    else Object.defineProperty(Navigator.prototype, "clipboard", { get: () => clip, configurable: true });
  } catch (_) { /* (left as it is) */ }
})();
