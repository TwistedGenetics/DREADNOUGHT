/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: the LFOs (see dn_lfo.h). Copyright (C) 2026 Jamie (TwistedGenetics). */
#include "dn_lfo.h"
#include "dn_dsp.h"

const char *const DN_LFO_SHAPE_NAME[DN_LS_N] = {"SINE", "TRI", "SAW UP", "SAW DN", "SQUARE", "S&H", "SMOOTH", "DRAW"};
const char *const DN_LFO_MODE_NAME[DN_LM_N] = {"FREE", "RETRIG", "1-SHOT"};
/* the note lengths, longest first: in beats, num / den */
const uint8_t DN_LFO_DIV[DN_LFO_NDIV][2] = {{32, 1}, {16, 1}, {8, 1}, {4, 1}, {2, 1}, {3, 2}, {4, 3}, {1, 1},
                                            {3, 4}, {2, 3}, {1, 2}, {3, 8}, {1, 3}, {1, 4}, {1, 6}, {1, 8}};
const char *const DN_LFO_DIV_NAME[DN_LFO_NDIV] = {"8 BARS", "4 BARS", "2 BARS", "1 BAR", "1/2", "1/4D", "1/2T", "1/4",
                                                  "1/8D", "1/4T", "1/8", "1/16D", "1/8T", "1/16", "1/16T", "1/32"};

void dn_lfo_draw_shape(dn_lfo_p_t *p, uint32_t shape)
{
    uint32_t i, n = p->npts == 32u ? 32u : 16u;
    int32_t seed = 0x2545F491;
    for (i = 0; i < DN_LFO_PTS; i++) {
        int32_t v;
        if (shape == DN_LS_SH || shape == DN_LS_SMOOTH)
            v = (int32_t)(dn_noise(&seed) % 201u) - 100;
        else {
            dn_lfo_p_t q = *p;
            q.shape = (uint8_t)shape;
            v = dn_lfo_at(&q, (i % n) << (n == 32u ? 27 : 28), 0, 0) * 100 / 32767;
        }
        p->pts[i] = (int8_t)dn_clamp(v, -100, 100);
    }
}

void dn_lfo_default(dn_lfo_p_t *p)
{
    uint32_t i;
    uint8_t *b = (uint8_t *)p;
    for (i = 0; i < sizeof *p; i++)
        b[i] = 0;
    p->shape = DN_LS_SINE;
    p->sync = 1;
    p->rate = 7;                                    /* 1/4 */
    p->smooth = 0;
    p->npts = 16;
    for (i = 0; i < DN_LFO_PTS; i++)                /* DRAW starts as a ramp down a beat's sixteenths */
        p->pts[i] = (int8_t)(100 - (int32_t)(i % 16u) * 200 / 15);
}

void dn_lfo_reset(dn_lfo_s_t *s, int32_t seed)
{
    s->acc = 0;
    s->ph = 0;
    s->done = 0;
    if (!s->rnd)
        s->rnd = seed | 1;
    s->sh_a = (int32_t)(dn_noise(&s->rnd) >> 16) - 32768;
    s->sh_b = (int32_t)(dn_noise(&s->rnd) >> 16) - 32768;
}

/* the shape at phase ph; a / b: S&H / SMOOTH's values for this cycle and the next */
int32_t dn_lfo_at(const dn_lfo_p_t *p, uint32_t ph, int32_t a, int32_t b)
{
    int32_t u = (int32_t)(ph >> 16);                /* 0..65535 */
    switch (p->shape) {
    case DN_LS_SINE: return dn_sine(ph);
    case DN_LS_TRI: return u < 32768 ? -32767 + 2 * u : 32767 - 2 * (u - 32768);
    case DN_LS_SAWUP: return dn_clamp(u - 32768, -32767, 32767);
    case DN_LS_SAWDN: return dn_clamp(32767 - u, -32767, 32767);
    case DN_LS_SQUARE: return u < 32768 ? 32767 : -32767;
    case DN_LS_SH: return a;
    case DN_LS_SMOOTH: {                            /* a smoothstep from this cycle's value to the next */
        int32_t t = u >> 1, s = (int32_t)(((int64_t)t * t >> 15) * (3 * 32768 - 2 * t) >> 15);   /* Q15 */
        return a + (int32_t)(((int64_t)(b - a) * s) >> 15);
    }
    default: {                                      /* DRAW: the points, stepped, sloped into the next by SMOOTH */
        uint32_t n = p->npts == 32u ? 32u : 16u, pos = (uint32_t)u * n, i = pos >> 16, f = pos & 0xFFFFu;
        int32_t va = p->pts[i] * 32767 / 100, vb = p->pts[(i + 1u) % n] * 32767 / 100;
        uint32_t w = (uint32_t)p->smooth * 65535u / 127u, edge = 65535u - w;
        if (!w || f <= edge)
            return va;
        return va + (int32_t)((float)(vb - va) * (float)(f - edge) / (float)w);
    }
    }
}

void dn_lfo_run(dn_lfo_s_t *s, const dn_lfo_p_t *p, uint32_t n, uint32_t bpm)
{
    uint32_t old = s->ph, ph;
    int32_t t, wrapped;
    if (p->sync) {                                  /* exact: samples x bpm x den against 2646000 x num */
        const uint8_t *d = DN_LFO_DIV[p->rate < DN_LFO_NDIV ? p->rate : 7u];
        uint32_t P = 2646000u * d[0], step = n * (bpm ? bpm : 120u) * d[1];
        s->acc += step;
        while (s->acc >= P)
            s->acc -= P;
        s->ph = (uint32_t)((float)s->acc / (float)P * 4294967040.0f);   /* (a hair under 2^32: never wraps) */
    } else {
        s->ph += (uint32_t)(((uint64_t)DN_LFO_INC[p->rate & 127u] * n) >> 5);
    }
    wrapped = s->ph < old;
    if (wrapped) {
        s->sh_a = s->sh_b;                          /* a new cycle: S&H's next value */
        s->sh_b = (int32_t)(dn_noise(&s->rnd) >> 16) - 32768;
        if (p->mode == DN_LM_ONESHOT)
            s->done = 1;
    }
    ph = s->done ? 0xFFFFFFFFu : s->ph;
    t = dn_lfo_at(p, ph + ((uint32_t)p->phase << 25), s->sh_a, s->sh_b);
    if (p->shape == DN_LS_SINE || p->shape == DN_LS_TRI || p->shape == DN_LS_SMOOTH)
        s->out = t;
    else                                            /* a jump: slewed over ~2 ms, no click */
        s->out += ((t - s->out) * 5) >> 4;
}
