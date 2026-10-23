// SPDX-License-Identifier: GPL-3.0-only
// A pretend FM-1 behind a fake WebMIDI, for testing the flasher page without hardware.
// It speaks the update protocol the way the device does (as implemented in vendor/fm1ota.js):
// identity handshake, step 1 (the running firmware reads a few blocks, then asks for 0xE0000000),
// re-enumeration as the update loader under a generic port name, step 2 (the loader reads the
// whole image, then asks for 0xF0000000), then a restart that reports the new identity.
// window.__fm1 exposes what happened so a test can check it. Options via window.__fm1opts:
//   { start: "FM-1_900", cutAfter: null }  cutAfter: unplug after N step-2 reads (resume test)
(() => {
  const opts = Object.assign({ start: "FM-1_900", cutAfter: null }, window.__fm1opts || {});
  const pack7 = (data) => { const o = []; let acc = 0, nb = 0; for (const b of data) { acc |= b << nb; nb += 8; while (nb >= 7) { o.push(acc & 0x7f); acc >>>= 7; nb -= 7; } } if (nb) o.push(acc & 0x7f); return o; };
  const unpack7 = (s) => { const o = []; let acc = 0, nb = 0; for (const b of s) { acc |= (b & 0x7f) << nb; nb += 7; while (nb >= 8) { o.push(acc & 0xff); acc >>>= 8; nb -= 8; } } return o; };
  const HS = "F0,0,32,45,0,0,0,40,7F,F7";
  const UPG = "F0,22,24,35,7F,F7";
  const hex = (a) => Array.from(a, (x) => x.toString(16).toUpperCase()).join(",");

  const S = window.__fm1 = { identity: opts.start, mode: "app", received: new Map(), log: [], resumed: false, writes: 0 };
  let access = null, ports = null;

  function identityPkt(text) {
    const d = [0, 0x59, 0x11, 0, 0, 0];
    for (let i = 0; i < 27; i++) d.push(i < text.length ? text.charCodeAt(i) : 0);
    d.push(0);
    return [0xf0, ...pack7(d), 0xf7];
  }
  function request(addr, len, fl = 0) {
    const u = [0, 0x59, 0x30, 0, 0, 0, fl, addr & 0xff, (addr >>> 8) & 0xff, (addr >>> 16) & 0xff, (addr >>> 24) & 0xff, len & 0xff, (len >> 8) & 0xff, (len >> 16) & 0xff];
    let s = 0; for (let i = 6; i < 14; i++) s += u[i];
    u.push(~s & 0xff);
    return [0xf0, ...pack7(u), 0xf7];
  }
  function parseResponse(pkt) {
    const u = unpack7(pkt.slice(1, -1));
    if (u[1] !== 0x59 || u[2] !== 0x30) return null;
    const addr = (u[7] | (u[8] << 8) | (u[9] << 16) | (u[10] << 24)) >>> 0;
    const len = u[11] | (u[12] << 8);
    return { addr, data: u.slice(14, 14 + len) };
  }

  class Port extends EventTarget {
    constructor(name, type) { super(); this.name = name; this.type = type; this.state = "connected"; this.onmidimessage = null; this.id = name + type; }
    async open() { return this; }
    send(bytes) { if (this.state !== "connected") throw new Error("disconnected"); setTimeout(() => device(Array.from(bytes)), 1); }
    emit(bytes) { if (this.state === "connected" && this.onmidimessage) this.onmidimessage({ data: Uint8Array.from(bytes) }); }
  }
  function plug(name) {
    if (ports) { ports.in.state = "disconnected"; ports.out.state = "disconnected"; }
    ports = { in: new Port(name, "input"), out: new Port(name, "output") };
    access.inputs.clear(); access.outputs.clear();
    access.inputs.set(ports.in.id, ports.in); access.outputs.set(ports.out.id, ports.out);
  }
  function unplug() { ports.in.state = "disconnected"; ports.out.state = "disconnected"; access.inputs.clear(); access.outputs.clear(); }

  // the device side: it asks, the host answers
  let queue = [], finish = 0, onDone = null;
  const reply = (bytes) => setTimeout(() => ports && ports.in.emit(bytes), 2);
  function ask() {
    const next = queue.shift();
    if (next === undefined) { reply(request(finish, 0)); return; }
    reply(request(next[0], next[1]));
  }
  function device(bytes) {
    const h = hex(bytes);
    if (h === HS) { reply(identityPkt(S.mode === "ota" ? "ota-" + S.target : S.identity)); return; }
    if (h === UPG) {
      S.log.push("upgrade:" + S.mode);
      if (S.mode === "app") { queue = [[0, 512], [512, 512], [4096, 256]]; finish = 0xE0000000; onDone = toLoader; }
      else { queue = []; for (let a = S.resumeFrom || 0; a < S.imageLen; a += 512) queue.push([a, Math.min(512, S.imageLen - a)]); finish = 0xF0000000; onDone = toApp; }
      ask();
      return;
    }
    if (bytes[0] !== 0xf0) return;
    const r = parseResponse(bytes);
    if (!r) return;
    if (r.addr === 0xE0000000 || r.addr === 0xF0000000) { setTimeout(onDone, 50); return; }
    S.received.set(r.addr, r.data);
    if (S.mode === "ota") {
      S.writes++;
      if (opts.cutAfter && S.writes === opts.cutAfter) { S.resumeFrom = r.addr + r.data.length; S.log.push("cut"); unplug(); setTimeout(() => plug("USB MIDI Device"), 800); return; }
    }
    ask();
  }
  function toLoader() {
    // the running firmware staged the loader: learn the target identity from the image head is not
    // possible in a mock, so the test page tells us (window.__fm1target) what it is installing
    S.target = window.__fm1target || "FM-1_901";
    S.imageLen = window.__fm1imageLen || 0;
    S.mode = "ota"; S.log.push("loader");
    unplug(); setTimeout(() => plug("USB MIDI Device"), 600);   // a generic name, as Windows gives the loader
  }
  function toApp() {
    S.mode = "app"; S.identity = S.target; S.log.push("restart:" + S.identity);
    unplug(); setTimeout(() => plug("DREADNOUGHT"), 600);
  }

  navigator.requestMIDIAccess = async () => {
    if (!access) { access = { inputs: new Map(), outputs: new Map(), sysexEnabled: true }; plug(opts.start === "ota" ? "USB MIDI Device" : "SLOOP"); }
    return access;
  };
})();
