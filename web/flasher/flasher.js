// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT flasher: the checks, kept apart from the page so they can be tested in Node.
import { productOf, logicalImage, sha256hex } from "./vendor/fm1pkg.js";

// Packages we will write: Felucca-family identities, written through a Felucca-family loader. FM-1_9XY (Felucca,
// SLOOP, DREADNOUGHT 0.1-0.2) or X0X's FM-1_9XXYYZZ (DREADNOUGHT from 0.3, which builds on X0X's platform).
export const OUR_PRODUCT = /^FM-1_9(\d\d|\d{6})$/;

// What the identity the device reports tells us about the firmware on it.
export function describeDevice(text) {
  if (/^ota-/i.test(text)) return { kind: "loader", label: "Update mode (" + text.slice(4) + ")" };
  const m = /^FM-1_(\d+)$/.exec(text);
  if (!m) return { kind: "unknown", label: text };
  const n = parseInt(m[1], 10);
  if (n >= 900) return { kind: "felucca", label: "Felucca-family firmware (" + text + ")" };
  return { kind: "official", label: "M-VAVE firmware (" + text + ")" };
}

// Where to back up, for the firmware the device reports.
export function backupAdvice(kind) {
  if (kind === "official") return {
    tool: "M-VAVE Workbench",
    url: "https://open.m-vave.com/fm1-workbench",
    what: "Export your projects, DX7 voice groups A–D (one at a time) and the sample bank. Project files do not contain sample audio.",
  };
  if (kind === "felucca") return {
    tool: "the web editor of the firmware on your FM-1 (SLOOP, Felucca, Clipper or DREADNOUGHT)",
    url: null,
    what: "Projects → Backup, and keep the file somewhere safe.",
  };
  return {
    tool: "the tool that came with the firmware on your FM-1",
    url: null,
    what: "Back up projects, presets, voices and samples before you continue.",
  };
}

// Check a package before it is offered for writing. expected: optional {sha256, product, size}
// from firmware.json. Returns {product, sha256, size, image}; throws with a readable message.
export async function checkPackage(bytes, expected = null) {
  if (!(bytes instanceof Uint8Array)) bytes = new Uint8Array(bytes);
  let product;
  try { product = productOf(bytes); } catch (e) { throw new Error("This is not an FM-1 firmware package (.fwsc)."); }
  if (product === "FM-1_015" || /^FM-1_0\d\d$/.test(product))
    throw new Error(`This is M-VAVE's own firmware (${product}). To return to it, use M-VAVE's Workbench or M-UPGRADE, not this flasher.`);
  if (!OUR_PRODUCT.test(product))
    throw new Error(`Unknown package identity "${product}". This flasher only writes DREADNOUGHT / Felucca-family packages (FM-1_9XY or FM-1_9XXYYZZ).`);
  const sha256 = await sha256hex(bytes);
  if (expected) {
    if (expected.size != null && expected.size !== bytes.length)
      throw new Error(`The download is damaged: ${bytes.length} bytes, expected ${expected.size}. Reload the page and try again.`);
    if (expected.sha256 && expected.sha256 !== sha256)
      throw new Error("The download is damaged: its SHA-256 does not match the release. Reload the page and try again.");
    if (expected.product && expected.product !== product)
      throw new Error(`The package says ${product}, the release says ${expected.product}.`);
  }
  return { product, sha256, size: bytes.length, image: logicalImage(bytes) };
}

// Progress 0..100 from the updater's steps. Step 1 (the running firmware checks the package) is
// short; step 2 (the loader writes) reads the whole image, so the address tells how far it is.
export function progressOf(step, arg, imageLength) {
  if (step === "start") return 2;
  if (step === "verify") return Math.min(10, 2 + arg / 10);
  if (step === "loader") return 12;
  if (step === "write") return 12 + Math.min(86, (arg / Math.max(1, imageLength)) * 86);
  if (step === "reboot") return 99;
  if (step === "done") return 100;
  return null;
}

// Plain-English messages for the updater's error codes.
export const ERRORS = {
  notfound: "FM-1 not found. Use a data USB cable straight into the computer, and close any other app or tab that uses MIDI.",
  model: "The connected device does not match this package.",
  stopped: "The FM-1 stopped answering. Press Install again: it resumes where it stopped.",
  lost: "The FM-1 was disconnected. Plug it back in and press Install again: it resumes where it stopped.",
  noloader: "The update loader did not appear. Unplug and replug the USB cable, then press Install again.",
  noreturn: "Written, but the FM-1 did not come back. Switch it off and on.",
  mismatch: "Written, but the FM-1 reports a different firmware than expected.",
  badreq: "The FM-1 asked for data outside the package. Nothing more was sent. Try again; if it repeats, report it.",
  foreign: "The FM-1 is in another firmware's update mode. Finish that update with its own tool first.",
};
