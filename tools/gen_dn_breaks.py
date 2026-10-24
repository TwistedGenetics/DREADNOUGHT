#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""DREADNOUGHT's built-in break loops, synthesised here at build time: no recording ships, the loops are this code's
output (a drum pattern is a rhythm; every sound below is made from sines and noise).

    tools/gen_dn_breaks.py OUT.h [--wav DIR]

Two loops of two bars at 136 BPM (a classic break's own tempo: the BREAKS engine speeds them up to the song's), mono,
22050 Hz, IMA ADPCM, low nibble first, from predictor 0 / index 0: the format of the FM-1's user sample slots (X0X's
tools/sampleio.py), so built-in and uploaded loops play through the same code.

    ROLLER   the "Amen"-style roll: kick on 1 and the "and" of 3, the snare's ghosted run, a ride on every eighth
    STEPPER  a tight "Think"-style groove: sixteenth hats with an open hat, a syncopated kick, a flammed snare

Copyright (C) 2026 Jamie (TwistedGenetics). Needs numpy.
"""
import sys
from pathlib import Path

import numpy as np

RATE = 22050
BPM = 136.0
BARS = 2
STEP = RATE * 60.0 / BPM / 4.0                   # a sixteenth, samples
N = int(round(STEP * 16 * BARS))
rng = np.random.default_rng(0x0DEAD)            # fixed: the same loops on every build


def env(n, decay_s, attack_s=0.0005):
    t = np.arange(n) / RATE
    e = np.exp(-t / decay_s)
    a = int(attack_s * RATE)
    if a > 1:
        e[:a] *= np.linspace(0, 1, a)
    return e


def onepole_lp(x, hz):
    a = np.exp(-2 * np.pi * hz / RATE)
    y = np.empty_like(x)
    s = 0.0
    for i, v in enumerate(x):
        s = (1 - a) * v + a * s
        y[i] = s
    return y


def hp(x, hz):
    return x - onepole_lp(x, hz)


def bp(x, lo, hi):
    return onepole_lp(hp(x, lo), hi)


def kick(n=int(0.30 * RATE)):
    t = np.arange(n) / RATE
    f = 48 + 120 * np.exp(-t / 0.035)            # the pitch drop
    ph = 2 * np.pi * np.cumsum(f) / RATE
    body = np.sin(ph) * env(n, 0.11)
    click = hp(rng.standard_normal(n), 1500) * env(n, 0.004) * 0.5
    return np.tanh(1.6 * (body + click))


def snare(n=int(0.30 * RATE), tone=185.0):
    t = np.arange(n) / RATE
    body = (np.sin(2 * np.pi * tone * t) + 0.5 * np.sin(2 * np.pi * tone * 1.62 * t)) * env(n, 0.06)
    noise = bp(rng.standard_normal(n), 900, 7000) * env(n, 0.075)
    return np.tanh(1.4 * (0.55 * body + 0.9 * noise))


def hat(n, decay):
    x = hp(rng.standard_normal(n), 6500)
    t = np.arange(n) / RATE                     # a little metal: square partials at inharmonic ratios
    m = sum(np.sign(np.sin(2 * np.pi * f * t)) for f in (3140, 4410, 5340, 6870)) * 0.12
    return (x + hp(m, 4000)) * env(n, decay) * 0.5


def ride(n=int(0.9 * RATE)):
    t = np.arange(n) / RATE
    m = sum(np.sin(2 * np.pi * f * t) * a for f, a in ((452, .3), (1012, .25), (2310, .3), (3407, .25), (5120, .2)))
    x = hp(rng.standard_normal(n), 5000) * 0.6
    return (x * env(n, 0.12) + m * env(n, 0.30)) * 0.22


SOUNDS = {"K": kick(), "S": snare(), "H": hat(int(0.06 * RATE), 0.022), "O": hat(int(0.35 * RATE), 0.16),
          "R": ride()}


def render(lanes, swing=0.0, flams=()):
    """lanes: {sound: 32-char string}: X accent, x hit, g ghost, . rest. swing: the off sixteenths' delay (of a step)"""
    out = np.zeros(N + RATE)
    hits = {}                                    # the true hit places: kick and snare, not ghosts (the onset test)
    for snd, pat in lanes.items():
        s = SOUNDS[snd]
        for i, c in enumerate(pat):
            if c == ".":
                continue
            g = {"X": 1.0, "x": 0.75, "g": 0.28}[c]
            at = int(round(i * STEP + (swing * STEP if i % 2 else 0)))
            if snd == "S" and i in flams:        # a flam: a soft hit just before
                pre = max(0, at - int(0.018 * RATE))
                out[pre:pre + len(s)] += s * g * 0.35
            out[at:at + len(s)] += s * g
            if snd in "KS" and g >= 0.75:
                hits[at] = max(hits.get(at, 0), g)
    tail = out[N:].copy()                        # the loop's tail wraps onto its start: it loops seamlessly
    out = out[:N]
    out[:len(tail)] += tail[:N]
    room = np.zeros_like(out)                    # a small room: two short echoes, darkened
    for d, g in ((int(0.021 * RATE), 0.10), (int(0.037 * RATE), 0.07)):
        room[d:] += out[:-d] * g
    out = out + onepole_lp(room, 5000)
    out = np.tanh(1.1 * out / np.max(np.abs(out))) # a little tape: the peaks rounded
    return out / np.max(np.abs(out)) * 0.95, sorted(hits)


LOOPS = [
    ("ROLLER", render({
        "K": "X.x.......xx....X.x.......x.....",
        "S": "....X..g.g..X..g....X..g.g..X.g.",
        "R": "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.",
    }, swing=0.06)),
    ("STEPPER", render({
        "K": "X.....x..x......X.....x..x.x....",
        "S": "....X.......X..g....X..g....X.gX",
        "H": "xgxgxgxgxgx.xgxgxgxgxgxgxgx.xgxg",
        "O": "...........x...............x....",
    }, swing=0.10, flams=(4, 20))),
]

# ---- IMA ADPCM (X0X's sampleio.ima_encode: low nibble first, from predictor 0 / index 0) ------------------------
STEPS = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
         107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
         876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
         4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
         22385, 24623, 27086, 29794, 32767]
IDX = [-1, -1, -1, -1, 2, 4, 6, 8]


def ima(samples):
    pred, idx, nib = 0, 0, []
    for s in samples:
        step = STEPS[idx]
        d = s - pred
        code = 8 if d < 0 else 0
        d = abs(d)
        diff = step >> 3
        if d >= step:
            code |= 4; d -= step; diff += step
        if d >= step >> 1:
            code |= 2; d -= step >> 1; diff += step >> 1
        if d >= step >> 2:
            code |= 1; diff += step >> 2
        pred = max(-32768, min(32767, pred - diff if code & 8 else pred + diff))
        idx = max(0, min(88, idx + IDX[code & 7]))
        nib.append(code)
    if len(nib) & 1:
        nib.append(0)
    return bytes(a | (b << 4) for a, b in zip(nib[0::2], nib[1::2]))


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    out = Path(sys.argv[1])
    wav_dir = Path(sys.argv[sys.argv.index("--wav") + 1]) if "--wav" in sys.argv else None
    h = ["/* generated by tools/gen_dn_breaks.py: do not edit. DREADNOUGHT's built-in loops, synthesised (no recordings):",
         f" * {BARS} bars at {BPM:g} BPM, {RATE} Hz, IMA ADPCM low nibble first from predictor 0 / index 0. */",
         "#pragma once", "#include <stdint.h>", f"#define DN_BRK_BUILTIN {len(LOOPS)}"]
    total = 0
    for k, (name, (x, hits)) in enumerate(LOOPS):
        pcm = [int(v) for v in np.round(x * 32767)]
        b = ima(pcm)
        total += len(b)
        h.append(f"static const uint8_t DN_BRK_DATA{k}[{len(b)}] = {{")
        for i in range(0, len(b), 24):
            h.append("    " + ", ".join(str(v) for v in b[i:i + 24]) + ",")
        h.append("};")
        h.append(f"#ifdef DN_BRK_TRUTH  /* the host test only: where the generator put each kick and snare */")
        h.append(f"static const uint32_t DN_BRK_HITS{k}[{len(hits)}] = {{{', '.join(str(v) for v in hits)}}};")
        h.append("#endif")
        if wav_dir:
            import wave
            wav_dir.mkdir(parents=True, exist_ok=True)
            with wave.open(str(wav_dir / f"builtin-{name.lower()}.wav"), "wb") as w:
                w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE)
                w.writeframes(np.array(pcm, dtype="<i2").tobytes())
    h.append("static const struct { const char *name; const uint8_t *adpcm; uint32_t n, rate; uint8_t bars; } "
             "DN_BRK_BUILTIN_LOOPS[DN_BRK_BUILTIN] = {")
    for k, (name, (x, hits)) in enumerate(LOOPS):
        h.append(f'    {{"{name}", DN_BRK_DATA{k}, {len(x)}u, {RATE}u, {BARS}}},')
    h.append("};")
    h.append("#ifdef DN_BRK_TRUTH")
    h.append("static const uint32_t *const DN_BRK_HITS[DN_BRK_BUILTIN] = {" +
             ", ".join(f"DN_BRK_HITS{k}" for k in range(len(LOOPS))) + "};")
    h.append("static const uint32_t DN_BRK_NHITS[DN_BRK_BUILTIN] = {" +
             ", ".join(f"sizeof DN_BRK_HITS{k} / 4" for k in range(len(LOOPS))) + "};")
    h.append("#endif")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(h) + "\n")
    print(f"breaks: {len(LOOPS)} loops, {total} B of flash")


if __name__ == "__main__":
    main()
