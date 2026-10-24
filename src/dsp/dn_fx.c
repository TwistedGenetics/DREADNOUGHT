/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: the FX rack's effects (see dn_fx.h). Copyright (C) 2026 Jamie (TwistedGenetics). */
#include "dn_fx.h"
#include "dn_dsp.h"

#define DN_DLY_LEN 24576u                           /* half-rate samples a line: 1.11 s */

const char *const DN_FX_NAME[DN_FX_N] = {"OFF", "DELAY"};
const char *const DN_FX_DIV_NAME[DN_FX_NDIV] = {"1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4",
                                                "1/2T", "1/4D", "1/2"};
const uint8_t DN_FX_DIV[DN_FX_NDIV][2] = {{1, 8}, {1, 6}, {1, 4}, {1, 3}, {3, 8}, {1, 2}, {2, 3}, {3, 4}, {1, 1},
                                          {4, 3}, {3, 2}, {2, 1}};

uint32_t dn_fx_ram(uint32_t type)
{
    return type == DN_FX_DELAY ? 2u * DN_DLY_LEN * sizeof(int16_t) : 0u;
}

void dn_fx_default(dn_fx_p_t *p, uint32_t type)
{
    p->type = (uint8_t)type;
    p->p[0] = 7;                                    /* 1/8D: the jungle dub default */
    p->p[1] = 60;
    p->p[2] = 70;
}

void dn_fx_init(dn_fx_s_t *s, uint32_t type, void *mem)
{
    uint32_t i, n = dn_fx_ram(type) / 4u;
    uint32_t *m = (uint32_t *)mem;
    for (i = 0; i < n; i++)
        m[i] = 0;
    s->type = (uint8_t)type;
    s->l = (int16_t *)mem;
    s->r = s->l ? s->l + DN_DLY_LEN : 0;
    s->len = type == DN_FX_DELAY ? DN_DLY_LEN : 0u;
    s->w = 0;
    s->d_q8 = 0;
    s->lp_l = s->lp_r = s->in_acc = s->y_l = s->y_r = s->o_l = s->o_r = 0;
    s->ph = 0;
}

uint32_t dn_fx_delay_samples(const dn_fx_p_t *p, uint32_t bpm)
{
    const uint8_t *d = DN_FX_DIV[p->p[0] < DN_FX_NDIV ? p->p[0] : 8u];
    uint32_t n = 2646000u * d[0] / ((bpm ? bpm : 120u) * d[1]);   /* 44100 x 60 x beats / bpm */
    return n > 2u * (DN_DLY_LEN - 4u) ? 2u * (DN_DLY_LEN - 4u) : n;
}

/* the line at d (half-rate samples << 8) behind the write point, linearly interpolated */
static int32_t dn_tap(const int16_t *b, uint32_t len, uint32_t w, uint32_t d_q8)
{
    uint32_t di = d_q8 >> 8, f = d_q8 & 255u, i0 = w >= di ? w - di : w + len - di, i1 = i0 ? i0 - 1u : len - 1u;
    return b[i0] + (((b[i1] - b[i0]) * (int32_t)f) >> 8);
}

void dn_fx_run(dn_fx_s_t *s, const dn_fx_p_t *p, const int32_t *in, int32_t *out, uint32_t n, uint32_t bpm,
               uint32_t ret)
{
    uint32_t i, tgt;
    int32_t fb, a, g;
    if (s->type != DN_FX_DELAY || !s->l || p->type != DN_FX_DELAY)
        return;
    tgt = (dn_fx_delay_samples(p, bpm) / 2u) << 8;
    if (!s->d_q8)
        s->d_q8 = tgt;
    fb = (int32_t)p->p[1] * 32768 / 100;            /* Q15: 100 = unity */
    a = 150 + (int32_t)p->p[2] * 3946 / 127;        /* the loop's low-pass, Q12 */
    g = (int32_t)ret * 32768 / 100;
    for (i = 0; i < n; i++) {
        int32_t x = (in[2u * i] + in[2u * i + 1u]) >> 1;
        if (!s->ph) {
            s->in_acc = x;
        } else {                                    /* the pair: the lines move on one (half-rate) sample */
            int32_t xi = dn_clamp((s->in_acc + x) >> 1, -65536, 65536), fl, fr;
            if (s->d_q8 < tgt)                      /* TIME glides: a quarter sample a step, like tape */
                s->d_q8 = s->d_q8 + 64u > tgt ? tgt : s->d_q8 + 64u;
            else if (s->d_q8 > tgt)
                s->d_q8 = s->d_q8 < tgt + 64u ? tgt : s->d_q8 - 64u;
            s->y_l = dn_tap(s->l, s->len, s->w, s->d_q8);
            s->y_r = dn_tap(s->r, s->len, s->w, s->d_q8);
            fl = xi + ((s->y_r * fb) >> 15);        /* ping-pong: in and the right's repeat into the left */
            fr = s->y_l;                            /* the left's into the right */
            s->lp_l += ((fl - s->lp_l) * a) >> 12;
            s->lp_r += ((fr - s->lp_r) * a) >> 12;
            s->l[s->w] = (int16_t)dn_softclip(s->lp_l);
            s->r[s->w] = (int16_t)dn_clamp(s->lp_r, -32767, 32767);
            s->w = s->w + 1u >= s->len ? 0u : s->w + 1u;
        }
        s->ph ^= 1u;
        s->o_l += (s->y_l - s->o_l) >> 1;           /* (the held pair, softened) */
        s->o_r += (s->y_r - s->o_r) >> 1;
        out[2u * i] += (s->o_l * g) >> 15;
        out[2u * i + 1u] += (s->o_r * g) >> 15;
    }
}
