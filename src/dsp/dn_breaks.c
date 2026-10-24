/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: BREAKS (dn_breaks.h says what it does). Copyright (C) 2026 Jamie (TwistedGenetics).
 * The IMA ADPCM step follows Felucca's eng_sample.c (Leo Kuroshita, Hügelton Instruments, GPL-3.0-only), the
 * decoder X0X's dsp/breaks.c uses: the standard IMA tables, low nibble first. */
#include "dn_breaks.h"
#include "dn_dsp.h"

static const int16_t IMA_STEP[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
    118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
    6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767};
static const int8_t IMA_IDX[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

/* the sample at nibble i, from the state before it; the state moves on */
static inline int32_t ima(const uint8_t *d, uint32_t i, int32_t *pred, int32_t *idx)
{
    uint32_t code = (i & 1u) ? (d[i >> 1] >> 4) : (d[i >> 1] & 15u);
    int32_t step = IMA_STEP[*idx], diff = step >> 3;
    if (code & 4u) diff += step;
    if (code & 2u) diff += step >> 1;
    if (code & 1u) diff += step >> 2;
    *pred = dn_clamp((code & 8u) ? *pred - diff : *pred + diff, -32768, 32767);
    *idx = dn_clamp(*idx + IMA_IDX[code & 7u], 0, 88);
    return *pred;
}

void dn_brk_init(dn_brk_t *b)
{
    uint32_t i;
    uint8_t *p = (uint8_t *)b;
    for (i = 0; i < sizeof *b; i++)
        p[i] = 0;
    b->decay = 127;
    b->level = 100;
}

uint8_t dn_brk_bars_of(uint32_t n, uint32_t rate)
{
    uint32_t bars, best = 1;
    float bestd = 1e9f;
    for (bars = 1; bars <= 4u; bars *= 2u) {        /* BPM = bars x 240 / seconds (main loop: float, no 64-bit) */
        float bpm = (float)bars * 240.0f * (float)rate / (float)(n ? n : 1u), d = bpm > 120.0f ? bpm - 120.0f : 120.0f - bpm;
        if (bpm >= 80.0f && bpm <= 180.0f && d < bestd)
            best = bars, bestd = d;
    }
    return (uint8_t)best;
}

void dn_brk_load(dn_brk_t *b, const dn_loop_t *loop)
{
    uint32_t i, col = 0, pk = 0, n;
    int32_t pred = 0, idx = 0;
    b->ok = 0;
    b->on = 0;
    b->loop = *loop;
    n = loop->n;
    if (!loop->adpcm || n < 256u)
        return;
    if (n > DN_BRK_CK * 256u)                       /* longer than the table: the rest is not played */
        n = b->loop.n = DN_BRK_CK * 256u;
    b->nslices = (uint8_t)(16u * (loop->bars ? loop->bars : 1u));
    for (i = 0; i < n; i++) {
        int32_t s;
        uint32_t c = i * DN_BRK_PEAKS / n;          /* (i < 2^18: fits) */
        if (!(i & 255u))
            b->ck[i >> 8] = (uint32_t)(pred & 0xFFFF) | (uint32_t)idx << 16;
        s = ima(loop->adpcm, i, &pred, &idx);
        if (c != col) {
            b->peaks[col] = (uint8_t)(pk >> 7);
            col = c;
            pk = 0;
        }
        if ((uint32_t)(s < 0 ? -s : s) > pk)
            pk = (uint32_t)(s < 0 ? -s : s);
    }
    b->peaks[col] = (uint8_t)(pk >> 7);
    __asm__ volatile("" ::: "memory");              /* the tables are written before the audio may read them */
    b->ok = 1;
}

void dn_brk_tempo(dn_brk_t *b, uint32_t bpm)
{
    float f;
    if (!b->loop.n || !b->loop.bars)
        return;
    f = (float)bpm * (float)b->loop.n / ((float)b->loop.bars * 10584000.0f);   /* 240 s x 44100 a bar */
    b->inc = (uint32_t)(f * (float)DN_SEMI_Q16[dn_clamp(b->pitch, -24, 24) + 24]);
}

/* the decoder at sample `at` (from the state saved before it) */
static void seek(dn_brk_t *b, uint32_t at)
{
    uint32_t c = b->ck[at >> 8];
    b->pred = (int16_t)(c & 0xFFFFu);
    b->idx = (int32_t)(c >> 16);
    for (b->p = at & ~255u; b->p < at; b->p++)
        ima(b->loop.adpcm, b->p, &b->pred, &b->idx);
}
static inline int32_t next(dn_brk_t *b)              /* the next sample forward; past the end: silence */
{
    if (b->p >= b->loop.n) {
        b->p++;
        return 0;
    }
    return ima(b->loop.adpcm, b->p++, &b->pred, &b->idx);
}

void dn_brk_trigger(dn_brk_t *b, uint32_t slice, uint32_t vel, int reverse)
{
    uint32_t start;
    if (!b->ok)
        return;
    slice %= b->nslices;
    start = slice * (b->loop.n / b->nslices);
    b->dc = b->on ? b->last : b->dc;                /* the cut: what was sounding fades in ~1 ms, not a click */
    b->slice = (uint8_t)slice;
    b->rev = (uint8_t)(reverse != 0);
    b->rel = b->fast = 0;
    b->env = 32767;
    b->gain = (int32_t)dn_clamp((int32_t)vel, 1, 127) * 258;
    b->played = 0;
    b->hold_n = 0;
    seek(b, start);
    if (b->rev) {                                   /* the slice into RAM, then played backwards */
        uint32_t len = b->loop.n / b->nslices, i;
        if (len > DN_BRK_RBUF)
            len = DN_BRK_RBUF;
        for (i = 0; i < len; i++)
            b->rbuf[i] = (int16_t)next(b);
        b->rpos = (int32_t)((len - 1u) << 16);
    } else {
        b->a = next(b);
        b->b = next(b);
        b->frac = 0;
    }
    b->on = 1;
}

void dn_brk_choke(dn_brk_t *b)
{
    if (b->on) {
        b->dc = b->last;                            /* the declick carries what was sounding down to zero */
        b->on = 0;
    }
}

void dn_brk_cut(dn_brk_t *b)
{
    b->rel = 1;
    b->fast = 1;
}

void dn_brk_release(dn_brk_t *b)
{
    if (b->decay < 127u)
        b->rel = 1;
}

void dn_brk_render(dn_brk_t *b, int32_t *lr, uint32_t n)
{
    uint32_t i, k = DN_ENV_K[b->fast ? 30 : dn_clamp(b->decay, 0, 126)] >> 5;   /* per sample (the table: per 32) */
    uint32_t hold = 1u + b->crush / 9u, shift = b->crush / 11u;
    if (!b->ok)
        return;
    for (i = 0; i < n; i++) {
        int32_t v = 0, o;
        uint8_t was = b->on;
        if (b->on) {
            if (b->rev) {
                int32_t j = b->rpos >> 16;
                int32_t s0 = b->rbuf[j], s1 = j + 1 < (int32_t)DN_BRK_RBUF ? b->rbuf[j + 1] : s0;   /* at j + frac */
                v = s0 + (((s1 - s0) * ((b->rpos & 0xFFFF) >> 1)) >> 15);
                b->rpos -= (int32_t)b->inc;
                if (b->rpos < 0)
                    b->on = 0;
            } else {
                v = b->a + (((b->b - b->a) * (int32_t)(b->frac >> 1)) >> 15);
                b->frac += b->inc;
                while (b->frac >= 65536u) {
                    b->frac -= 65536u;
                    b->a = b->b;
                    b->b = next(b);
                }
                if (b->p >= b->loop.n + 2u)
                    b->on = 0;                      /* the end of the loop */
            }
            b->played += b->inc >> 8;               /* (Q8: the screen's playhead) */
            if (b->crush) {                         /* sample rate down (hold), then bits down */
                if (b->hold_n == 0u)
                    b->held = v;
                b->hold_n = (b->hold_n + 1u) % hold;
                v = (b->held >> shift) << shift;
            }
            if (b->rel) {
                b->env -= (int32_t)(((uint32_t)b->env * k) >> 16) + 1;
                if (b->env <= 0)
                    b->env = 0, b->on = 0;
            }
            v = (((v * b->env) >> 15) * b->gain) >> 15;
        }
        o = v + b->dc;
        b->dc -= b->dc >= 0 ? (b->dc + 31) >> 5 : -((31 - b->dc) >> 5);   /* to zero, from either side */
        if (was && !b->on)
            b->dc = o;                              /* stopped mid-wave: it fades from there, no click */
        b->last = o;
        o = (o * 21000) >> 15;                      /* the mix level: under REESE's, a full break stays clear */
        o = (o * (int32_t)b->level) / 100;
        lr[2u * i] += o;
        lr[2u * i + 1u] += o;
    }
}
