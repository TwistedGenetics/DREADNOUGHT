/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: single-precision maths without libm (the FM-1's and the SMK-37's FPUs are float-only and no libm is
 * linked). Copyright (C) 2026 Jamie (TwistedGenetics); written 2026-10. The polynomials were fitted for this file
 * (weighted least squares pushed toward minimax on Chebyshev points, tools/fit_dn_math.py); tests/host/math_test.c
 * checks every function against libm.
 *
 * Accuracy (math_test): exp2 / exp rel 2e-7, log2 abs 3e-7 near 1 and rel 1e-7 elsewhere, pow rel 2e-6 for
 * moderate arguments, tanh abs 3e-7, sin / cos abs 1e-6 on [-100, 100], tan rel 2e-6 on (-pi/2, pi/2), sqrt rel 2e-7.
 * Build the host with -ffp-contract=off so it computes what the device computes. None of these takes NaN; none
 * returns a denormal for an argument in range. */
#pragma once
#include <stdint.h>

#define FM_PI 3.14159265358979f
#define FM_TWO_PI 6.28318530717959f
#define FM_LN2 0.693147180559945f
#define FM_LOG2E 1.44269504088896f

typedef union {
    float f;
    uint32_t u;
    int32_t i;
} dn_f32_t;

static inline float fm_fabsf(float x) { dn_f32_t v; v.f = x; v.u &= 0x7FFFFFFFu; return v.f; }
static inline float fm_minf(float a, float b) { return a < b ? a : b; }
static inline float fm_maxf(float a, float b) { return a > b ? a : b; }
static inline float fm_clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static inline float fm_flush(float x) { return fm_fabsf(x) < 1e-20f ? 0.0f : x; }

/* the largest whole number not above x (|x| < 2^31) */
static inline float fm_floorf(float x)
{
    int32_t n = (int32_t)x;                     /* toward zero */
    return (float)(n - (x < (float)n));
}

/* 2^n * 2^f, n whole, f in [0, 1) */
static inline float fm_exp2_nf(int32_t n, float f)
{
    dn_f32_t s;
    float p = 1.8775759974e-03f;
    p = p * f + 8.9893422768e-03f;
    p = p * f + 5.5826315632e-02f;
    p = p * f + 2.4015361816e-01f;
    p = p * f + 6.9315307300e-01f;
    p = p * f + 9.9999992507e-01f;
    if (n < -126)
        return 0.0f;
    if (n > 127)
        n = 127;
    s.u = (uint32_t)(n + 127) << 23;
    return p * s.f;
}

static inline float fm_exp2f(float x)
{
    float n;
    if (x < -126.0f)
        return 0.0f;
    if (x > 128.0f)
        x = 128.0f;
    n = fm_floorf(x);
    return fm_exp2_nf((int32_t)n, x - n);
}

/* e^x: x log2 e split as (whole) + (fraction), the fraction made from x minus n ln 2 in two parts, so a large x
 * keeps its fraction's accuracy */
static inline float fm_expf(float x)
{
    float n, r;
    if (x < -87.5f)
        return 0.0f;
    if (x > 88.7f)
        x = 88.7f;
    n = fm_floorf(x * FM_LOG2E);
    r = (x - n * 0.693145751953125f) - n * 1.428606765330187e-6f;    /* ln 2 = hi + lo */
    return fm_exp2_nf((int32_t)n, r * FM_LOG2E);
}

/* log2 x for x > 0: the exponent, then the mantissa moved into [0.75, 1.5) and a polynomial in (m - 1) */
static inline float fm_log2f(float x)
{
    dn_f32_t v;
    int32_t e;
    float t, p;
    if (!(x > 0.0f))
        return -127.0f;
    v.f = x;
    e = (int32_t)((v.u >> 23) & 0xFFu) - 127;
    v.u = (v.u & 0x007FFFFFu) | 0x3F800000u;    /* m in [1, 2) */
    if (v.f >= 1.5f)
        v.f *= 0.5f, e++;
    t = v.f - 1.0f;                             /* [-0.25, 0.5) */
    p = -9.0785473785e-02f;
    p = p * t + 2.0150163837e-01f;
    p = p * t - 2.5222051924e-01f;
    p = p * t + 2.9084564965e-01f;
    p = p * t - 3.6022974200e-01f;
    p = p * t + 4.8079623512e-01f;
    p = p * t - 7.2135332851e-01f;
    p = p * t + 1.4426960446e+00f;
    return (float)e + p * t;
}
static inline float fm_logf(float x) { return fm_log2f(x) * FM_LN2; }

/* x^y for x > 0 (0 for x <= 0) */
static inline float fm_powf(float x, float y) { return x > 0.0f ? fm_exp2f(y * fm_log2f(x)) : 0.0f; }
static inline float fm_db2lin(float db) { return fm_exp2f(db * 0.166096405f); }   /* 10^(db / 20) */

/* sin(2 pi t) for t in [-0.25, 0.25]: odd, degree 9 */
static inline float dn_sin_quarter(float t)
{
    float t2 = t * t, p = 3.9536696711e+01f;
    p = p * t2 - 7.6549781117e+01f;
    p = p * t2 + 8.1601004025e+01f;
    p = p * t2 - 4.1341655031e+01f;
    p = p * t2 + 6.2831851601e+00f;
    return p * t;
}
/* sine of a phase in turns (one cycle a turn), any phase within |ph| < 2^22 */
static inline float fm_sin_turns(float ph)
{
    float t = ph - fm_floorf(ph + 0.5f);       /* [-0.5, 0.5) */
    if (t > 0.25f)
        t = 0.5f - t;                           /* sin is mirrored about a quarter turn */
    else if (t < -0.25f)
        t = -0.5f - t;
    return dn_sin_quarter(t);
}
/* x to [-pi, pi) */
static inline float fm_wrap_pi(float x)
{
    float k = fm_floorf(x * (1.0f / FM_TWO_PI) + 0.5f);
    return ((x - k * 6.28125f) - k * 1.9353071795864769e-3f);     /* 2 pi = hi + lo: the hi part exact for k < 2^12 */
}
static inline float fm_sin_pi(float x) { return fm_sin_turns(x * (1.0f / FM_TWO_PI)); }
static inline float fm_sinf(float x) { return fm_sin_pi(fm_wrap_pi(x)); }
static inline float fm_cosf(float x) { return fm_sin_turns(fm_wrap_pi(x) * (1.0f / FM_TWO_PI) + 0.25f); }

/* tan on (-pi/2, pi/2): sin over cos, cos taken as the sine of the distance to the pole so it stays exact there */
static inline float fm_tanf(float x)
{
    float ax = fm_fabsf(x), a = ax * (1.0f / FM_TWO_PI), s = dn_sin_quarter(a > 0.25f ? 0.5f - a : a), c;
    c = dn_sin_quarter(((1.5703125f - ax) + 4.838267948966e-4f) * (1.0f / FM_TWO_PI));   /* pi/2 = hi + lo */
    if (c < 1e-30f)
        c = 1e-30f;
    return x < 0.0f ? -s / c : s / c;
}

/* tanh: odd polynomial near 0; further out 1 - 2 / (e^2|x| + 1) */
static inline float fm_tanhf(float x)
{
    float a = fm_fabsf(x), y;
    if (a < 0.55f) {
        float x2 = x * x, p = -6.0635775020e-03f;
        p = p * x2 + 2.0896367249e-02f;
        p = p * x2 - 5.3799264952e-02f;
        p = p * x2 + 1.3331884246e-01f;
        p = p * x2 - 3.3333279702e-01f;
        p = p * x2 + 9.9999999431e-01f;
        return p * x;
    }
    if (a > 9.0f)
        return x < 0.0f ? -1.0f : 1.0f;
    y = 1.0f - 2.0f / (fm_exp2f(a * (2.0f * FM_LOG2E)) + 1.0f);
    return x < 0.0f ? -y : y;
}

/* 1 / sqrt x, x > 0: a guess from the bits, two Newton steps */
static inline float fm_rsqrtf(float x)
{
    dn_f32_t v;
    float h = 0.5f * x, y;
    v.f = x;
    v.u = 0x5F375A86u - (v.u >> 1);
    y = v.f;
    y = y * (1.5f - h * y * y);
    y = y * (1.5f - h * y * y);
    return y;
}
/* sqrt x (0 for x <= 0): x / sqrt x, one more Newton step on the root itself */
static inline float fm_sqrtf(float x)
{
    float r, y;
    if (!(x > 0.0f))
        return 0.0f;
    r = fm_rsqrtf(x);
    y = x * r;
    return y + 0.5f * r * (x - y * y);
}
