/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: the shared fixed-point DSP building blocks. Copyright (C) 2026 Jamie (TwistedGenetics).
 * All of it DREADNOUGHT's own. The polyBLEP saw, the soft clip and the trapezoidal SVF were written anew in 2026-10
 * from published designs (Valimaki & Huovilainen 2007; a rational tanh; Zavalishin's TPT SVF, as in A. Simper's
 * public-domain notes): each one's comment names its source. */
#pragma once
#include <stdint.h>
#include "dn_tables.h"

static inline int32_t dn_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int32_t dn_mulq15(int32_t a, int32_t b) { return (a * b) >> 15; }
/* a * k / 65536 without 64-bit: a up to 2^31, k Q16 */
static inline int32_t dn_mulq16(int32_t a, uint32_t k)
{
    return (int32_t)(((a >> 16) * (int32_t)k) + (int32_t)(((uint32_t)(a & 0xFFFF) * k) >> 16));
}

/* sine from the phase (2^32 a cycle), Q15, linearly interpolated */
static inline int32_t dn_sine(uint32_t ph)
{
    uint32_t i = ph >> 22;
    int32_t a = DN_SINE[i], b = DN_SINE[i + 1u];
    return a + (((b - a) * (int32_t)((ph >> 7) & 0x7FFFu)) >> 15);
}

/* polyBLEP residual (Q15) around a wrap of a phase accumulator (DREADNOUGHT's own, 2026-10, after V. Valimaki and
 * A. Huovilainen, "Antialiasing Oscillators in Subtractive Synthesis", IEEE Signal Processing Magazine, 2007): the
 * naive saw drops by 2 (65536) at the wrap; within one increment either side of it the two-sample polynomial step
 * takes its place. With d the distance to the wrap in increments (0..1), the residual is (1 - d)^2 just before the
 * wrap and -(1 - d)^2 just after: subtracted from the naive saw it meets the middle of the step from both sides.
 * Elsewhere 0 (two compares); within the window a float divide finds d. */
static inline int32_t dn_blep(uint32_t ph, uint32_t inc)
{
    uint32_t dist;
    int32_t m;
    int after;
    if (ph < inc)
        dist = ph, after = 1;                           /* just past the wrap */
    else if (0u - ph < inc)
        dist = 0u - ph, after = 0;                      /* just before it */
    else
        return 0;
    if (inc < 2u)
        return 0;
    m = 32768 - (int32_t)((float)(int32_t)(dist >> 1) / (float)(int32_t)(inc >> 1) * 32768.0f);
    m = m < 0 ? 0 : m;
    m = (m * m) >> 15;                                  /* (1 - d)^2, Q15 (m <= 32768: m^2 <= 2^30) */
    return after ? -m : m;
}
static inline int32_t dn_saw(uint32_t ph, uint32_t inc) { return ((int32_t)(ph >> 16) - 32768) - dn_blep(ph, inc); }
static inline int32_t dn_pulse(uint32_t ph, uint32_t inc, uint32_t pw)
{
    return (dn_saw(ph, inc) - dn_saw(ph + pw, inc)) >> 1;
}

/* the soft clip (DREADNOUGHT's own, 2026-10): ~tanh(x / 32768) x 32767 as a rational, v (27 + v^2) / (27 + 9 v^2) for
 * v = x / 32768, held at +-32767 from |v| = 3 where the curve meets it with zero slope (within 0.025 of tanh, a touch
 * firmer at the knee). 32-bit integers only: v^2 in Q12, the ratio in Q14, one 32-bit divide. */
static inline int32_t dn_softclip(int32_t x)
{
    int32_t a = x < -98304 ? -98304 : x > 98304 ? 98304 : x, q = a >> 3, v2, y;
    uint32_t r;
    v2 = (q * q) >> 12;                               /* v^2, Q12: <= 36864 */
    r = ((uint32_t)(110592 + v2) << 14) / (uint32_t)(110592 + 9 * v2);   /* (27 + v^2) / (27 + 9 v^2), Q14 */
    y = (a * (int32_t)r) >> 14;
    y -= y >> 15;                                     /* (x 32767 / 32768) */
    return y < -32767 ? -32767 : y > 32767 ? 32767 : y;
}

static inline uint32_t dn_noise(int32_t *st)
{
    uint32_t s = (uint32_t)*st;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    *st = (int32_t)s;
    return s;
}

/* the filters' g for a cutoff 0..127 << 8 (30 Hz .. 16 kHz, log), Q12 */
static inline int32_t dn_svf_g(int32_t cut)
{
    int32_t i, g;
    cut = dn_clamp(cut, 0, 127 << 8);
    i = cut >> 8;
    g = DN_SVF_G[i];
    if (i < 127)
        g += ((DN_SVF_G[i + 1] - g) * (cut & 255)) >> 8;
    return g;
}

/* the same, Q16 (finer at low cutoffs: a self-oscillating filter plays in tune) */
static inline int32_t dn_svf_g16(int32_t cut)
{
    int32_t i, f;
    uint32_t a, b;
    cut = dn_clamp(cut, 0, 127 << 8);
    i = cut >> 8, f = cut & 255;
    a = DN_SVF_G16[i], b = DN_SVF_G16[i < 127 ? i + 1 : 127];
    return (int32_t)(a + (uint32_t)(((uint64_t)(b - a) * (uint32_t)f) >> 8));
}

/* The state-variable filter (DREADNOUGHT's own, 2026-10), in Zavalishin's TPT form ("The Art of VA Filter Design",
 * ch. 4; the same trapezoidal SVF as A. Simper's public-domain Cytomic notes, SvfLinearTrapOptimised2): two
 * trapezoidal integrators g = tan(pi fc / fs), the damping k, the high-pass solved first with no delay,
 *   hp = (x - (g + k) s1 - s2) / (1 + g (g + k)),  bp = g hp + s1,  lp = g bp + s2,  s1' = bp + g hp,  s2' = lp + g bp.
 * Float inside (the FM-1's FPU), the engines' integers outside; the states are floats the caller keeps (zeroed to
 * start). A state that has decayed below 1e-20 is set to 0 (never a denormal). Outputs: 0 LP, 1 BP, 2 HP */
typedef struct { float g, gk, a; } dn_svf_t;
static inline void dn_svf_coef(dn_svf_t *c, int32_t cut, int32_t k)   /* k: damping Q12 (2.0 = 8192, sqrt2 = 5793) */
{
    float g = (float)dn_svf_g16(cut) * (1.0f / 65536.0f);
    c->g = g;
    c->gk = g + (float)k * (1.0f / 4096.0f);
    c->a = 1.0f / (1.0f + g * c->gk);
}
static inline int32_t dn_svf(const dn_svf_t *c, int32_t in, float *s1, float *s2, uint32_t out)
{
    float hp = ((float)in - c->gk * *s1 - *s2) * c->a, v = c->g * hp, bp = v + *s1, lp;
    *s1 = bp + v;
    v = c->g * bp;
    lp = v + *s2;
    *s2 = lp + v;
    if (*s1 < 1e-20f && *s1 > -1e-20f) *s1 = 0.0f;
    if (*s2 < 1e-20f && *s2 > -1e-20f) *s2 = 0.0f;
    return (int32_t)(out == 0u ? lp : out == 1u ? bp : hp);
}

/* a MIDI note plus a fraction (1/256 semitone) -> phase increment (the note table, interpolated in cents) */
static inline uint32_t dn_pitch_inc(int32_t note256)
{
    int32_t n = dn_clamp(note256, 0, 127 * 256 - 1), i = n >> 8, f = n & 255;
    uint32_t a = DN_NOTE_INC[i], b = DN_NOTE_INC[i + 1];
    return a + (uint32_t)(((uint64_t)(b - a) * (uint32_t)f) >> 8);
}

/* ---- envelope times locked to the tempo (0.13): a value 0..127 picks one of DN_ESYNC_N note lengths, and the
 * envelope covers ~99 % of its way in that long at the tempo (as DN_ENV_K does for its times; 10 s at most) */
#define DN_ESYNC_N 15
static const uint8_t DN_ESYNC[DN_ESYNC_N][2] = {{1, 16}, {1, 12}, {1, 8}, {1, 6}, {1, 4}, {1, 3}, {1, 2}, {2, 3}, {1, 1},
                                                {4, 3}, {2, 1}, {4, 1}, {8, 1}, {16, 1}, {32, 1}};   /* beats: num / den */
static inline uint32_t dn_esync_idx(int32_t v) { return (uint32_t)dn_clamp(v, 0, 127) * DN_ESYNC_N / 128u; }
static inline int32_t dn_esync_k(int32_t v, uint32_t bpm, int32_t lo)   /* lo: the quickest table index allowed */
{
    const uint8_t *d = DN_ESYNC[dn_esync_idx(v)];
    float ms = (float)d[0] * 60000.0f / ((float)(bpm ? bpm : 120u) * (float)d[1]), t = 1.0f;
    int32_t i = 0;
    while (i < 127 && t * 1.0369f < ms) {           /* the table's time nearest it (1.075254 ^ i ms; 1.0369: half a step) */
        t *= 1.075254f;
        i++;
    }
    return DN_ENV_K[i < lo ? lo : i];
}
