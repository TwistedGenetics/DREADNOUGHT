#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# DREADNOUGHT: the SMK-37 flasher page for one model, with the firmware (and M-VAVE's V16, to go back) in it.
# Copyright (C) 2026 Jamie (TwistedGenetics).
#   smk_flasher.py KIT MODEL OUT.html      KIT: the kit after build-dn.ps1 built MODEL (pro | elite)
# The packages are described as M-VAVE's own updater (the kit's sdk/WebUpdate.js) takes them.
import base64
import hashlib
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
STOCK = {"pro": ("Pro", "2d98ce72530e71d617384963423820536f094501299e2cfec0a8d3c3fb6bcfb0"),
         "elite": ("Elite", "b2ee38abfef3ae922e4eecdb8a551e14345300ae6f270265feacd33b6ef99cb6")}
STOCK_LOADER = "ef1f2db89b011fa53e71df363f0122de0ea896618c53fb53e8da676322004541"


def sha(b):
    return hashlib.sha256(b).hexdigest()


def main():
    kit, model, out = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
    name, stock_hash = STOCK[model]
    build = kit / "build" / model
    fw = (build / "generated" / f"SMK37_{model.upper()}_OPEN.fwsc").read_bytes()
    ident = json.loads((build / "build-identity.json").read_text(encoding="utf-8-sig"))
    vh = (kit / "app" / "version.h").read_text(encoding="utf-8")
    rel = re.search(r'FM1_RELEASE_VERSION "(V\d+)"', vh).group(1)
    product = re.search(r'FM1_UPDATE_PRODUCT_%s "([^"]+)"' % model.upper(), vh).group(1)
    ver = (REPO / "VERSION").read_text(encoding="utf-8").strip()
    family = 8 if model == "pro" else 9
    stock = (kit / "sdk" / "firmware" / f"SMK37-{name}-V16.fwsc").read_bytes()
    if sha(stock) != stock_hash:
        raise SystemExit("smk_flasher: the kit's V16 is not M-VAVE's")
    pkg = {"schema": 1, "product": product, "profile": model, "family": family, "release_version": rel,
           "build_id": ident["build_id"], "sha256": sha(fw), "bytes": len(fw),
           "loader_sha256": sha((kit / "sdk" / "tools" / "pack" / "ota.bin").read_bytes()),
           "data": base64.b64encode(fw).decode(), "model": model}
    st = {"schema": 1, "kind": "stock", "product": f"SMK-37 {name}_016", "model": model,
          "profile": f"SMK37 {name} Original V16", "family": family, "release_version": "V16", "build_id": stock_hash,
          "sha256": stock_hash, "bytes": len(stock), "loader_sha256": STOCK_LOADER,
          "data": base64.b64encode(stock).decode()}
    page = (REPO / "web" / "smk" / "flasher.html").read_text(encoding="utf-8")
    for k, v in [("/*DN_WEBUPDATE*/", (kit / "sdk" / "WebUpdate.js").read_text(encoding="utf-8-sig")),
                 ("/*DN_PACKAGE*/", json.dumps(pkg)), ("/*DN_STOCK*/", json.dumps(st)),
                 ("/*DN_MODEL_NAME*/", name), ("/*DN_MODEL*/", model), ("/*DN_VERSION*/", ver)]:
        if k not in page:
            raise SystemExit(f"smk_flasher: {k} not in the page")
        page = page.replace(k, v)
    out.write_text(page, encoding="utf-8")
    print(f"smk_flasher: {out} ({len(page) // 1024} KB): {product}, build {ident['build_id'][:12]}")


if __name__ == "__main__":
    main()
