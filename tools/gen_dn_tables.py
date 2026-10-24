#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""DREADNOUGHT's DSP tables, as a C header.

    tools/gen_dn_tables.py OUT.h

DN_SINE    1025 x int16   one cycle of a sine, Q15, with the first value repeated at the end (interpolation)
DN_TANH    257 x int16    tanh(x) for x = 0 .. 2 in 256 steps, Q15 (the soft clipper)
DN_SVF_G   128 x uint16   tan(pi f / FS), Q12, for f = 30 Hz .. 16 kHz in 128 log steps (the filters' cutoff)
DN_SVF_G16 128 x uint32   the same, Q16 (SUB's ladder: in tune when it self-oscillates)
DN_NOTE_INC 128 x uint32  the phase increment of each MIDI note at 44.1 kHz (A4 = 440 Hz), 2^32 per cycle
DN_ENV_K   128 x uint16   one-pole step per control tick (32 samples), Q16, for times 1 ms .. 10 s (log): the
                          envelopes and the glide cover ~99 % of the way in that time
DN_LFO_INC 128 x uint32   the LFO's phase increment per control tick for 0.05 .. 20 Hz (log)
DN_SEMI_Q16 49 x uint32   2^(k/12) for k = -24 .. +24 semitones, Q16 (the breaks' pitch)
FM (0.25), as Dexed's MSFA (Apache-2.0) computes them at start-up (sin.cc, exp2.cc, freqlut.cc, lfo.cc), for 44.1 kHz
and blocks of 64:
DN_FM_SIN   2048 x int32  the sine, delta-interleaved (Sin::lookup)
DN_FM_EXP2  2048 x int32  2^x, delta-interleaved (Exp2::lookup)
DN_FM_FREQ  1025 x int32  log frequency -> phase increment (Freqlut::lookup)
DN_FM_LFO   100 x uint32  the LFO's phase step a block for SPEED 0..99
DN_FM_FINE  100 x int32   FINE 0..99 in log frequency (osc_freq)
DN_FM_DT  128 x 15 int32  DETUNE 0..14 on note n, in log frequency, exactly as osc_freq's double maths gives it
"""
import math
import sys
from pathlib import Path

FS = 44100


def arr(name, ctype, vals, per=12):
    lines = [f"static const {ctype} {name}[{len(vals)}] = {{"]
    for i in range(0, len(vals), per):
        lines.append("    " + ", ".join(str(v) for v in vals[i:i + per]) + ",")
    lines.append("};")
    return lines


def fm_tables():
    """MSFA's tables, bit for bit (the same double maths and int64 recurrence as sin.cc / exp2.cc / freqlut.cc)."""
    L = []
    n = 1024
    dphase = 2 * math.pi / n
    c = int(math.floor(math.cos(dphase) * (1 << 30) + 0.5))
    s = int(math.floor(math.sin(dphase) * (1 << 30) + 0.5))
    R = 1 << 29
    u, v = 1 << 30, 0
    sint = [0] * (2 * n)

    def i32(x):
        x &= 0xFFFFFFFF
        return x - (1 << 32) if x & 0x80000000 else x
    for i in range(n // 2):
        sint[(i << 1) + 1] = (v + 32) >> 6
        sint[((i + n // 2) << 1) + 1] = -((v + 32) >> 6)
        t = i32((u * s + v * c + R) >> 30)
        u = i32((u * c - v * s + R) >> 30)
        v = t
    for i in range(n - 1):
        sint[i << 1] = sint[(i << 1) + 3] - sint[(i << 1) + 1]
    sint[(n << 1) - 2] = -sint[(n << 1) - 1]
    L += arr("DN_FM_SIN", "int32_t", sint, 8)
    inc = math.exp2(1.0 / n)
    y = float(1 << 30)
    ex = [0] * (2 * n)
    for i in range(n):
        ex[(i << 1) + 1] = i32(int(math.floor(y + 0.5)))
        y *= inc
    for i in range(n - 1):
        ex[i << 1] = i32(ex[(i << 1) + 3] - ex[(i << 1) + 1])
    ex[(n << 1) - 2] = i32((1 << 31) - ex[(n << 1) - 1])
    L += arr("DN_FM_EXP2", "int32_t", ex, 8)
    y = float(1 << 44) / FS
    inc = math.pow(2, 1.0 / n)
    fr = []
    for i in range(n + 1):
        fr.append(i32(int(math.floor(y + 0.5))))
        y *= inc
    L += arr("DN_FM_FREQ", "int32_t", fr, 8)
    src = [0.062541, 0.125031, 0.312393, 0.437120, 0.624610, 0.750694, 0.936330, 1.125302, 1.249609, 1.436782,
           1.560915, 1.752081, 1.875117, 2.062494, 2.247191, 2.374451, 2.560492, 2.686728, 2.873976, 2.998950,
           3.188013, 3.369840, 3.500175, 3.682224, 3.812065, 4.000800, 4.186202, 4.310716, 4.501260, 4.623209,
           4.814636, 4.930480, 5.121901, 5.315191, 5.434783, 5.617346, 5.750431, 5.946717, 6.062811, 6.248438,
           6.431695, 6.564264, 6.749460, 6.868132, 7.052186, 7.250580, 7.375719, 7.556294, 7.687577, 7.877738,
           7.993605, 8.181967, 8.372405, 8.504848, 8.685079, 8.810573, 8.986341, 9.122423, 9.300595, 9.500285,
           9.607994, 9.798158, 9.950249, 10.117361, 11.251125, 11.384335, 12.562814, 13.676149, 13.904338,
           15.092062, 16.366612, 16.638935, 17.869907, 19.193858, 19.425019, 20.833333, 21.034918, 22.502250,
           24.003841, 24.260068, 25.746653, 27.173913, 27.578599, 29.052876, 30.693677, 31.191516, 32.658393,
           34.317090, 34.674064, 36.416606, 38.197097, 38.550501, 40.387722, 40.749796, 42.625746, 44.326241,
           44.883303, 46.772685, 48.590865, 49.261084]
    ratio = int((4437500000.0 * 64) / FS) & 0xFFFFFFFF
    L += arr("DN_FM_LFO", "uint32_t", [int(x * ratio) & 0xFFFFFFFF for x in src], 8)
    L += arr("DN_FM_FINE", "int32_t", [int(math.floor(24204406.323123 * math.log(1 + 0.01 * f) + 0.5)) for f in range(100)], 8)
    import struct
    dt = []
    for note in range(128):
        lf = 50857777 + ((1 << 24) // 12) * note
        f32 = struct.unpack("f", struct.pack("f", float(lf)))[0]   # ((float)logfreq)
        x = struct.unpack("f", struct.pack("f", f32 / (1 << 24)))[0]   # / (1 << 24), in float
        ratio = 0.0209 * math.exp(-0.396 * x) / 7
        for det in range(15):
            v = lf + ratio * lf * (det - 7)
            dt.append(int(v) - lf)                 # (int += double: truncated toward zero)
    L += arr("DN_FM_DT", "int32_t", dt, 15)
    L.append(f"#define DN_FM_LFO_UNIT {int(64 * 25190424 / FS + 0.5)}")
    L.append(f"#define DN_FM_PENV_UNIT {int(64 * (1 << 24) / (21.3 * FS) + 0.5)}")
    return L


def main(out):
    L = ["/* generated by tools/gen_dn_tables.py: do not edit */", "#pragma once", "#include <stdint.h>",
         f"#define DN_FS {FS}"]
    sine = [int(round(32767 * math.sin(2 * math.pi * i / 1024))) for i in range(1024)]
    L += arr("DN_SINE", "int16_t", sine + [sine[0]])
    L += arr("DN_TANH", "int16_t", [int(32767 * math.tanh(i / 256 * 2)) for i in range(257)])
    fc = [30.0 * ((16000 / 30) ** (v / 127)) for v in range(128)]
    L += arr("DN_SVF_G", "uint16_t", [int(4096 * math.tan(math.pi * min(f, 0.45 * FS) / FS)) for f in fc])
    L += arr("DN_SVF_G16", "uint32_t", [int(round(65536 * math.tan(math.pi * min(f, 0.45 * FS) / FS))) for f in fc], 8)
    L += arr("DN_NOTE_INC", "uint32_t",
             [min(0xFFFFFFFF, int(round(440.0 * 2 ** ((n - 69) / 12) / FS * 2 ** 32))) for n in range(128)], per=6)
    tick = 32 / FS
    ms = [1.0 * (10000 ** (v / 127)) for v in range(128)]
    L += arr("DN_ENV_K", "uint16_t", [min(65535, max(1, int(round(65536 * (1 - math.exp(-tick / (m / 1000 / 4.6))))))) for m in ms])
    hz = [0.05 * (400 ** (v / 127)) for v in range(128)]
    L += arr("DN_LFO_INC", "uint32_t", [int(round(h * tick * 2 ** 32)) for h in hz], per=8)
    L += arr("DN_SEMI_Q16", "uint32_t", [int(round(65536 * 2 ** ((k - 24) / 12))) for k in range(49)], per=8)
    L += fm_tables()
    Path(out).parent.mkdir(parents=True, exist_ok=True)
    Path(out).write_text("\n".join(L) + "\n")
    print(f"tables: {out}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
