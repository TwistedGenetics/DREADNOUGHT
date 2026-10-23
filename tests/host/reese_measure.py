#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""DREADNOUGHT: measure the REESE engine against the maths (needs numpy and build/host/reese_test).
   - DTN: the pair's beat rate equals f0 (2^(cents/1200) - 1), within 6 % (each oscillator drifts on its own,
     ~1.5 cents, on purpose: that is the analog movement)
   - BEAT: the beat rate is the note value at the tempo, the same on every note, within 3 %
   - SUB: with the crossover, the low band stays steady (min/max of its level >= 0.6)"""
import subprocess, sys, wave
import numpy as np
from numpy.fft import rfft, irfft, rfftfreq
BIN = "build/host/reese_test"
OUT = "build/host/m.wav"
FS = 44100
fails = 0


def render(note, *kv):
    subprocess.run([BIN, "--one", OUT, str(note), "4", *kv], check=True)
    w = wave.open(OUT)
    return np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, 2).mean(1) / 32768


def beat(d, f0):
    s = d[FS // 2:FS * 4]
    S = rfft(s); fr = rfftfreq(len(s), 1 / FS)
    S[(fr < f0 * 0.7) | (fr > f0 * 1.3)] = 0
    env = np.abs(irfft(S, len(s)))
    env = np.convolve(env, np.ones(441) / 441, "same"); env -= env.mean()
    E = np.abs(rfft(env * np.hanning(len(env)), n=1 << 21)); fe = rfftfreq(1 << 21, 1 / FS)
    m = (fe > 0.2) & (fe < 15)
    return fe[m][np.argmax(E[m])]


def check(label, got, want, tol=0.03):
    global fails
    ok = abs(got - want) <= tol * want
    fails += not ok
    print(f"{'ok  ' if ok else 'FAIL'} {label}: {got:.2f} (want {want:.2f})")


# open filter, no sub, no drive, no notch, no transpose, no LFO, sustain full
BASE = ["3=0", "4=127", "5=0", "6=0", "7=0", "17=0", "15=0", "10=127", "12=0"]
for note in (36, 43):
    f0 = 440 * 2 ** ((note - 69) / 12)
    for dtn in (64, 90, 127):
        c = 100 * (dtn / 127) ** 2
        check(f"DTN {dtn} ({c:.0f} cents) on {f0:.0f} Hz: beat Hz", beat(render(note, *BASE, "0=0", "2=0", f"1={dtn}"), f0),
              f0 * (2 ** (c / 1200) - 1), 0.06)
for note in (31, 43, 55):
    f0 = 440 * 2 ** ((note - 69) / 12)
    check(f"BEAT 1/4 at 120 BPM on {f0:.0f} Hz: beat Hz", beat(render(note, *BASE, "0=0", "2=3", "1=70", "bpm=120"), f0), 2.0)
# the low end with the sub and its crossover (a drive and a filter as in CLASSIC RS)
note = 36; f0 = 440 * 2 ** ((note - 69) / 12)
for sub, want in ((80, 0.6),):
    d = render(note, "0=0", "1=90", "2=0", f"3={sub}", "4=68", "5=18", "6=24", "7=0", "17=0", "15=0", "10=127", "12=0")
    s = d[FS // 2:FS * 4]; S = rfft(s); fr = rfftfreq(len(s), 1 / FS); S[fr > f0 * 1.4] = 0
    low = irfft(S, len(s)); e = np.sqrt(np.convolve(low ** 2, np.ones(4410) / 4410, "valid"))
    st = e.min() / e.max(); ok = st >= want; fails += not ok
    print(f"{'ok  ' if ok else 'FAIL'} SUB {sub}: low-band steadiness {st:.2f} (want >= {want})")
print("REESE MEASURE: " + ("ALL OK" if not fails else f"{fails} FAILED"))
sys.exit(1 if fails else 0)
