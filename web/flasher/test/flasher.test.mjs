// SPDX-License-Identifier: GPL-3.0-only
// node --test 'web/flasher/test/*.test.mjs'
import test from "node:test";
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { checkPackage, describeDevice, backupAdvice, progressOf } from "../flasher.js";
import { productOf } from "../vendor/fm1pkg.js";

// A minimal package carrying an identity in its 20 marker bytes (the rest is filler).
function fakePackage(identity, size = 4096) {
  const b = new Uint8Array(size);
  for (let i = 0; i < size; i++) b[i] = (i * 31) & 0xff;
  for (let i = 0; i < 20; i++)
    b[i * 0x30 + 0x2f] = i < identity.length ? (identity.charCodeAt(i) + i + 1) & 0xff : 0x7d;
  return b;
}
const sha = (b) => createHash("sha256").update(b).digest("hex");

test("identity round trip", () => {
  assert.equal(productOf(fakePackage("FM-1_900")), "FM-1_900");
});

test("accepts a Felucca-family package and strips the 20 marker bytes", async () => {
  const pkg = fakePackage("FM-1_901");
  const r = await checkPackage(pkg);
  assert.equal(r.product, "FM-1_901");
  assert.equal(r.image.length, pkg.length - 20);
  assert.equal(r.sha256, sha(pkg));
});

test("checks size, SHA-256 and identity against the release listing", async () => {
  const pkg = fakePackage("FM-1_902");
  await checkPackage(pkg, { size: pkg.length, sha256: sha(pkg), product: "FM-1_902" });
  await assert.rejects(checkPackage(pkg, { sha256: "0".repeat(64) }), /damaged/);
  await assert.rejects(checkPackage(pkg, { size: 1 }), /damaged/);
  await assert.rejects(checkPackage(pkg, { product: "FM-1_903" }), /release says/);
});

test("refuses the official firmware and unknown packages", async () => {
  await assert.rejects(checkPackage(fakePackage("FM-1_015")), /M-VAVE's own firmware/);
  await assert.rejects(checkPackage(fakePackage("XYZ_1")), /Unknown package identity/);
  await assert.rejects(checkPackage(new Uint8Array(10)), /not an FM-1 firmware/);
});

test("describes what is on the device and where to back it up", () => {
  assert.equal(describeDevice("FM-1_015").kind, "official");
  assert.equal(describeDevice("FM-1_909").kind, "felucca");
  assert.equal(describeDevice("ota-FM-1_909").kind, "loader");
  assert.match(backupAdvice("official").url, /m-vave/);
});

test("progress only goes forward", () => {
  const len = 600000;
  const seq = [["start"], ["verify", 30], ["loader"], ["write", 0], ["write", 300000], ["write", 599000], ["reboot"], ["done"]];
  let last = -1;
  for (const [s, a] of seq) {
    const p = progressOf(s, a, len);
    assert.ok(p >= last, `${s} went back`);
    last = p;
  }
  assert.equal(last, 100);
});
