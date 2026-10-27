/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: the 4-pole ladder low-pass of REESE and SUB. Copyright (C) 2026 Jamie (TwistedGenetics).
 * DREADNOUGHT's own, written 2026-10 from the published design: V. Zavalishin, "The Art of VA Filter Design"
 * (rev. 2.1.0, 2018), ch. 3 (the trapezoidal "TPT" one-pole), ch. 5 (the ladder: four of them in a loop, the loop's
 * zero-delay equation solved instead of a unit delay put in it) and ch. 6 (a saturator in the loop).
 *
 * Four identical TPT one-poles, each  v = G (x - s), y = v + s, s' = y + v  with G = g / (1 + g) and g = tan(pi fc/fs)
 * (the cutoff prewarped: the analog ladder's -180 degree point, where it rings, lands exactly on fc). Written out, the
 * fourth output is  y4 = G^4 u + S  where S collects the four states (S = (1-G)(((s1 G + s2) G + s3) G + s4)), so with
 * the feedback  u = x - k y4  the loop solves to  y4 = (G^4 x + S) / (1 + k G^4)  with no delay. The saturator then
 * goes where the transistor ladder's input pair is: u = sat(x - k y4), using that solved y4 (Zavalishin's cheap
 * nonlinear ladder: the loop stays tuned, a hot input or the resonance compresses softly, and above k = 4 the
 * oscillation grows until the saturator holds it). sat(v) = v (27 + v^2) / (27 + 9 v^2), held at +-1 from |v| = 3: a
 * rational tanh (same slope at 0, meets 1 at 3 with zero slope, within 0.025 of tanh).
 *
 * Float (the FM-1's FPU is single precision); no libm, dn_math.h for the coefficients. Signals: the engines'
 * integers, 65536 = the saturator's 1.0. A constant 1e-20 rides on the input: the states settle on it, never on a
 * denormal. Stable for every cutoff and k (the solved loop's denominator is >= 1). */
#pragma once
#include <stdint.h>
#include "dn_math.h"

typedef struct {
    float G, H, G4;      /* a stage's G, 1 - G, the cascade's G^4 */
    float k, den;        /* the feedback; 1 / (1 + k G^4) */
    float in;            /* the input scale: 1/65536 x the passband make-up */
} dn_ladder_t;

#define DN_LADDER_LOG2_SPAN 9.0588937f                 /* log2(16000 / 30): CUT 0..127 is 30 Hz .. 16 kHz, log */

/* cut: 0..127 << 8 (clamped); k: the feedback (4: the edge of self-oscillation); makeup: the share of k the input is
 * raised by (the passband falls as 1 / (1 + k): 0.5 gives some of it back, as the panel's level expects) */
static inline void dn_ladder_coef(dn_ladder_t *c, int32_t cut, float k, float makeup)
{
    float f, g;
    cut = cut < 0 ? 0 : cut > 127 * 256 ? 127 * 256 : cut;
    f = 30.0f * fm_exp2f((float)cut * (DN_LADDER_LOG2_SPAN / (127.0f * 256.0f)));
    g = fm_tanf(f * (3.14159265f / 44100.0f));
    c->G = g / (1.0f + g);
    c->H = 1.0f - c->G;
    c->G4 = (c->G * c->G) * (c->G * c->G);
    c->k = k;
    c->den = 1.0f / (1.0f + k * c->G4);
    c->in = (1.0f + makeup * k) * (1.0f / 65536.0f);
}

static inline float dn_ladder_sat(float v)
{
    float v2;
    if (v > 3.0f) return 1.0f;
    if (v < -3.0f) return -1.0f;
    v2 = v * v;
    return v * (27.0f + v2) / (27.0f + 9.0f * v2);
}

/* one sample: x in, the 4-pole low-pass out (both the engines' integer scale); s: the four states */
static inline int32_t dn_ladder_tick(const dn_ladder_t *c, int32_t x, float *s)
{
    const float G = c->G;
    float u = (float)x * c->in + 1e-20f, y4, v;
    y4 = (c->G4 * u + c->H * (((s[0] * G + s[1]) * G + s[2]) * G + s[3])) * c->den;
    u = dn_ladder_sat(u - c->k * y4);
    v = G * (u - s[0]), u = v + s[0], s[0] = u + v;
    v = G * (u - s[1]), u = v + s[1], s[1] = u + v;
    v = G * (u - s[2]), u = v + s[2], s[2] = u + v;
    v = G * (u - s[3]), u = v + s[3], s[3] = u + v;
    return (int32_t)(u * 65536.0f);
}
