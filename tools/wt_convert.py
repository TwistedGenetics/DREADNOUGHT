#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""DREADNOUGHT: wavetables, from Serum-style WAVs to the FM-1's format (0.32). Copyright (C) 2026 Jamie (TwistedGenetics).

    tools/wt_convert.py IN.wav [OUT.dnwt] [--frame N] [--frames N] [--name NAME]

A Serum wavetable is single cycles one after another in one WAV (2048 samples a cycle unless its `clm ` chunk says
otherwise: "<!>2048 ..."), 16 / 24 / 32-bit integer or 32-bit float, mono or stereo (folded to mono). Single-cycle
WAVs (AKWF) are a table of one frame; --frame sets the cycle length when the file doesn't say.

The FM-1's format (src/dsp/dn_wt.h): a 16-byte header ("DNWT", version 1, frames, log2 of the frame size (9), mips
(7), the name in 8 bytes), then each frame as 7 versions, each band-limited and four times longer than its top
harmonic (Carnac OS's layout): 512 samples (harmonics 1-127), 256 (1-63), 128, 64, 32, 16, 8 (1), int16 little-endian;
2032 bytes a frame. The engine reads the one that can't alias at the note's pitch, so the table needs no RAM.

Conversion: each cycle's DC removed, its harmonics 1-127 taken by a DFT (whatever its length), each version made from
them, the whole table scaled so its loudest point is 32000; more than 64 frames are thinned to 64, evenly. No
zero-crossing alignment (a table's frames are made to follow one another; turning each would break the morph).
The editor does the same in JavaScript (web/editor/wavetable.js); tests/host checks they agree to the last bit or one.
"""
import argparse
import struct
import sys
from pathlib import Path

import numpy as np

SIZE, MIPS, MAXF, PEAK = 512, 7, 64, 32000
HMAX = [(128 >> m) - 1 for m in range(MIPS)]   # (each version 4x its top harmonic: linear reads stay clean)
MAGIC = b"DNWT"


def read_wav(data: bytes):
    """-> (frames x cycle as float mono, the cycle length from 'clm ' or None)"""
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("not a WAV file")
    fmt = None
    pcm = None
    clm = None
    i = 12
    while i + 8 <= len(data):
        cid, n = data[i:i + 4], struct.unpack_from("<I", data, i + 4)[0]
        body = data[i + 8:i + 8 + n]
        if cid == b"fmt ":
            tag, ch, rate, _, _, bits = struct.unpack_from("<HHIIHH", body, 0)
            if tag == 0xFFFE and len(body) >= 26:
                tag = struct.unpack_from("<H", body, 24)[0]
            fmt = (tag, ch, bits)
        elif cid == b"data":
            pcm = body
        elif cid == b"clm ":
            t = body.decode("latin-1")
            if t.startswith("<!>"):
                try:
                    clm = int(t[3:].split()[0])
                except ValueError:
                    clm = None
        i += 8 + n + (n & 1)
    if not fmt or pcm is None:
        raise ValueError("no audio in that WAV")
    tag, ch, bits = fmt
    if tag == 3 and bits == 32:
        x = np.frombuffer(pcm[:len(pcm) // 4 * 4], dtype="<f4").astype(np.float64)
    elif tag == 1 and bits == 16:
        x = np.frombuffer(pcm[:len(pcm) // 2 * 2], dtype="<i2").astype(np.float64) / 32768.0
    elif tag == 1 and bits == 24:
        b = np.frombuffer(pcm[:len(pcm) // 3 * 3], dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        v = b[:, 0] | b[:, 1] << 8 | b[:, 2] << 16
        x = np.where(v >= 1 << 23, v - (1 << 24), v).astype(np.float64) / 8388608.0
    elif tag == 1 and bits == 32:
        x = np.frombuffer(pcm[:len(pcm) // 4 * 4], dtype="<i4").astype(np.float64) / 2147483648.0
    else:
        raise ValueError(f"a WAV of format {tag}, {bits} bits: not one of 16 / 24 / 32-bit or 32-bit float")
    x = x[:len(x) // ch * ch].reshape(-1, ch).mean(axis=1)   # (stereo: folded to mono)
    return x, clm


def harmonics(cycle):
    """the cycle's harmonics 1..127 (complex), by a DFT over its own length: the same sums as the editor's"""
    n = len(cycle)
    k = np.arange(1, HMAX[0] + 1)
    t = np.arange(n)
    ang = 2.0 * np.pi * np.outer(k, t) / n
    return (cycle @ np.cos(ang).T) - 1j * (cycle @ np.sin(ang).T)


def mip(h, m, n_in):
    """version m: a cycle of 512 >> m samples from harmonics 1 .. HMAX[m] (h from a cycle of n_in samples)"""
    size = SIZE >> m
    kk = np.arange(1, HMAX[m] + 1)
    t = np.arange(size)
    ang = 2.0 * np.pi * np.outer(t, kk) / size
    hh = h[:HMAX[m]]
    return (2.0 / n_in) * (np.cos(ang) @ hh.real - np.sin(ang) @ hh.imag)


def convert(x, frame=None, clm=None, frames=None, name="TABLE"):
    """samples (float mono) -> the FM-1's table bytes"""
    cyc = frame or clm or (len(x) if len(x) <= 4096 else 2048)
    nf = len(x) // cyc
    if nf < 1:
        raise ValueError(f"shorter than one cycle of {cyc} samples")
    pick = list(range(nf))
    want = min(frames or MAXF, MAXF, nf)
    if want < nf:                                   # (thinned evenly, the first and last kept)
        pick = [round(i * (nf - 1) / (want - 1)) if want > 1 else 0 for i in range(want)]
    out = []
    for f in pick:
        c = x[f * cyc:(f + 1) * cyc]
        c = c - c.mean()
        h = harmonics(c)
        out.append([mip(h, m, cyc) for m in range(MIPS)])
    peak = max(float(np.max(np.abs(fr[0]))) for fr in out) or 1.0
    scale = PEAK / peak
    nm = name.upper().encode("ascii", "replace")[:8].ljust(8, b"\0")
    b = bytearray(MAGIC + bytes([1, len(out), 9, MIPS]) + nm)
    for fr in out:
        for m in fr:
            v = np.floor(m * scale + 0.5).astype(np.int64)
            b += np.clip(v, -32768, 32767).astype("<i2").tobytes()
    return bytes(b)


def table_bytes(nframes):
    return 16 + nframes * 2 * sum(SIZE >> m for m in range(MIPS))


def main():
    ap = argparse.ArgumentParser(description="a Serum-style WAV -> a DREADNOUGHT wavetable")
    ap.add_argument("wav", type=Path)
    ap.add_argument("out", type=Path, nargs="?")
    ap.add_argument("--frame", type=int, choices=[256, 512, 1024, 2048, 4096], help="the cycle length (else clm, else 2048)")
    ap.add_argument("--frames", type=int, help=f"at most this many frames (1..{MAXF})")
    ap.add_argument("--name", help="its name (8 characters; else the file's)")
    a = ap.parse_args()
    x, clm = read_wav(a.wav.read_bytes())
    t = convert(x, a.frame, clm, a.frames, a.name or a.wav.stem)
    out = a.out or a.wav.with_suffix(".dnwt")
    out.write_bytes(t)
    print(f"{out}: {t[5]} frames, {len(t)} bytes ({len(t) / 1024:.1f} KB)")


if __name__ == "__main__":
    sys.exit(main())
