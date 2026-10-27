#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# DREADNOUGHT: fits the polynomials in src/dsp/dn_math.h (numpy). Copyright (C) 2026 Jamie (TwistedGenetics).
import numpy as np
from numpy.polynomial import chebyshev as C
def fit(f, a, b, deg, odd=False, rel=False):
    # least squares on dense Chebyshev-distributed points, then a few weighted (Lawson) passes toward minimax
    x = (a+b)/2 + (b-a)/2*np.cos(np.linspace(0, np.pi, 4000))
    y = f(x); w = np.ones_like(x)
    if odd: pw = np.arange(1, deg+1, 2)
    else: pw = np.arange(deg+1)
    A = np.stack([x**p for p in pw], 1)
    s = 1/np.abs(y) if rel else np.ones_like(y)
    for it in range(60):
        W = np.sqrt(w)*s
        c, *_ = np.linalg.lstsq(A*W[:,None], y*W, rcond=None)
        e = np.abs(A@c - y)*s
        w = w*e; w /= w.sum()
    return pw, c, (np.abs(A@c-y)*s).max()
print("----")
np.set_printoptions(precision=12)
for name,args in [("exp2",(lambda x: 2**x,0,1,5,False,True)),("log2",(lambda x: np.log2(1+x),-0.25,0.5,8,False,False)),("sin",(lambda t: np.sin(2*np.pi*t),-0.25,0.25,9,True,False)),("tanh",(np.tanh,-0.55,0.55,11,True,False))]:
    pw,c,e=fit(*args); print(name, list(pw), ["%.10e"%v for v in c], e)
