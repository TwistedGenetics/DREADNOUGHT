#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the GitHub Pages site: the web flasher plus every released firmware package.

    tools/make_site.py RELEASES_DIR SITE_DIR

RELEASES_DIR holds releases.json (from `gh release list --json tagName,isPrerelease,publishedAt`)
and one folder per tag with its .fwsc files. Each package is copied to SITE_DIR/fw/<tag>/ and listed
in SITE_DIR/flasher/firmware.json with its size, SHA-256 and package identity, so the flasher
can show it and check it before writing.
"""
import hashlib
import json
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F


def product_of(data: bytes) -> str:
    """The package identity ("FM-1_9XY"): one marker byte after each of the first 20 blocks."""
    if len(data) < BLOCKS * BLK:
        raise ValueError("not an FM-1 package (too short)")
    out = ""
    for i in range(BLOCKS):
        m = data[i * BLK + KEEP]
        if m != 0x7D:
            out += chr((m - i - 1) & 0xFF)
    return out


def main(rel_dir: Path, site: Path) -> None:
    if site.exists():
        shutil.rmtree(site)
    shutil.copytree(ROOT / "web", site, ignore=shutil.ignore_patterns("test", "*.mjs.map"))
    listing = []
    meta = rel_dir / "releases.json"
    releases = json.loads(meta.read_text()) if meta.exists() else []
    for r in releases:
        tag = r["tagName"]
        for pkg in sorted((rel_dir / tag).glob("*.fwsc")):
            data = pkg.read_bytes()
            try:
                product = product_of(data)
            except ValueError as e:
                print(f"skip {pkg}: {e}")
                continue
            if not product.startswith("FM-1_9"):
                print(f"skip {pkg}: identity {product} is not a DREADNOUGHT package")
                continue
            dest = site / "fw" / tag / pkg.name
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(pkg, dest)
            listing.append({
                "tag": tag,
                "name": pkg.name,
                "url": f"../fw/{tag}/{pkg.name}",
                "size": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
                "product": product,
                "prerelease": bool(r.get("isPrerelease")),
                "published": r.get("publishedAt", ""),
            })
    (site / "flasher").mkdir(parents=True, exist_ok=True)
    (site / "flasher" / "firmware.json").write_text(json.dumps({"releases": listing}, indent=1))
    print(f"site: {len(listing)} package(s) in {site}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(Path(sys.argv[1]), Path(sys.argv[2]))
