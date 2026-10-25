#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""DREADNOUGHT (0.32): wavetables made two ways must agree. Writes test WAVs (a factory table as Serum writes it,
a 24-bit stereo one of 80 frames to thin, a 32-bit float single cycle of 600 samples, a 16-bit one with no clm chunk)
and tools/wt_convert.py's tables of them into DIR; tests/host/wt_xcheck.mjs converts the same WAVs with the editor's
web/editor/wavetable.js and compares."""
import json
import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import wt_convert  # noqa: E402


def wav(x, ch, bits, flt=False, clm=None):
    if flt:
        pcm = np.asarray(x, dtype="<f4").tobytes()
        fmt = struct.pack("<HHIIHH", 3, ch, 44100, 44100 * ch * 4, ch * 4, 32)
    elif bits == 24:
        v = np.clip(np.round(np.asarray(x) * 8388607), -8388608, 8388607).astype(np.int32)
        b = v.astype("<i4").view(np.uint8).reshape(-1, 4)[:, :3]
        pcm = b.tobytes()
        fmt = struct.pack("<HHIIHH", 1, ch, 44100, 44100 * ch * 3, ch * 3, 24)
    else:
        pcm = np.clip(np.round(np.asarray(x) * 32767), -32768, 32767).astype("<i2").tobytes()
        fmt = struct.pack("<HHIIHH", 1, ch, 44100, 44100 * ch * 2, ch * 2, 16)
    body = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt
    if clm:
        c = clm.encode()
        body += b"clm " + struct.pack("<I", len(c)) + c + (b"\0" if len(c) & 1 else b"")
    body += b"data" + struct.pack("<I", len(pcm)) + pcm
    return b"RIFF" + struct.pack("<I", len(body)) + body


def main():
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(7)
    cases = []
    repo = Path(__file__).resolve().parents[2]
    cases.append(("reese", (repo / "wavetables" / "reese.wav").read_bytes(), {}))
    t = np.arange(80 * 1024) / 1024.0
    lft = np.sin(2 * np.pi * t * (1 + np.floor(t) / 20)) * 0.7 + rng.normal(0, 0.05, t.size)
    st = np.stack([lft, np.roll(lft, 3) * 0.9], axis=1).reshape(-1)
    cases.append(("stereo24", wav(st, 2, 24, clm="<!>1024 00000000 wavetable"), {}))
    one = np.sign(np.sin(2 * np.pi * np.arange(600) / 600)) * 0.5 + np.arange(600) / 600 * 0.3
    cases.append(("single", wav(one.astype(np.float32), 1, 32, flt=True), {}))
    many = rng.uniform(-1, 1, 2048 * 3)
    cases.append(("noclm16", wav(many, 1, 16), {"frames": 2}))
    meta = []
    for name, data, opt in cases:
        (out / f"{name}.wav").write_bytes(data)
        x, clm = wt_convert.read_wav(data)
        tb = wt_convert.convert(x, clm=clm, frames=opt.get("frames"), name=name)
        (out / f"{name}.dnwt").write_bytes(tb)
        meta.append({"name": name, **opt})
    (out / "cases.json").write_text(json.dumps(meta))


if __name__ == "__main__":
    main()
