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

/* ---- the loop's hits: an onset detector over 64-sample frames (~3 ms at 22 kHz) of the decoded loop ---------------
 * A frame whose level rises well above the two before it is a hit (a kick after a kick still counts: the rise is
 * against what came just before, not the loop's loudest); the strongest rise within ~6 ms wins; hits closer than
 * ~30 ms merge (the stronger stays). The slice then starts at the first sample that rises a quarter of the way from
 * the level before to the hit's peak, moved back to the zero crossing just before it (no click, no lost attack). */
#define DN_ON_HOP 64u
#define DN_ON_FRAMES (DN_BRK_CK * 256u / DN_ON_HOP)
static uint16_t on_e[DN_ON_FRAMES];                 /* (main loop only: dn_brk_load) */
static uint16_t on_d[DN_ON_FRAMES];

uint32_t dn_brk_zero_near(const dn_brk_t *b, uint32_t at, uint32_t range)
{
    int32_t pred, idx, prev = 0, s;
    uint32_t from = at > range ? at - range : 0u, to = at + range < b->loop.n ? at + range : b->loop.n, i,
             best = at, bestd = 0xFFFFFFFFu, c = b->ck[from >> 8];
    pred = (int16_t)(c & 0xFFFFu);
    idx = (int32_t)(c >> 16);
    for (i = from & ~255u; i < to; i++) {
        s = ima(b->loop.adpcm, i, &pred, &idx);
        if (i > from && ((prev < 0 && s >= 0) || (prev > 0 && s <= 0))) {
            uint32_t d = i > at ? i - at : at - i;
            if (d < bestd)
                best = i, bestd = d;
        }
        prev = s;
    }
    return best;
}

/* the zero crossing at or before `at` (within `range`) */
static uint32_t zero_before(const dn_brk_t *b, uint32_t at, uint32_t range)
{
    int32_t pred, idx, prev = 0, s;
    uint32_t from = at > range ? at - range : 0u, i, best = at, c = b->ck[from >> 8];
    pred = (int16_t)(c & 0xFFFFu);
    idx = (int32_t)(c >> 16);
    for (i = from & ~255u; i <= at && i < b->loop.n; i++) {
        s = ima(b->loop.adpcm, i, &pred, &idx);
        if (i > from && ((prev < 0 && s >= 0) || (prev > 0 && s <= 0)))
            best = i;
        prev = s;
    }
    return best;
}

static void find_hits(dn_brk_t *b)
{
    uint32_t nf = b->loop.n / DN_ON_HOP, f, i, n = 0, mx = 0;
    int32_t pred = 0, idx = 0;
    uint32_t pos[DN_BRK_MAXSL], str[DN_BRK_MAXSL];
    if (nf > DN_ON_FRAMES)
        nf = DN_ON_FRAMES;
    {
        int32_t last = 0;
        for (f = 0; f < nf; f++) {                  /* each frame's brightness: the mean of |s[n] - s[n-1]| (the
                                                     * attack's edge counts, a kick's slow swing hardly) */
            uint32_t e = 0;
            for (i = 0; i < DN_ON_HOP; i++) {
                int32_t v = ima(b->loop.adpcm, f * DN_ON_HOP + i, &pred, &idx), dv = v - last;
                e += (uint32_t)(dv < 0 ? -dv : dv);
                last = v;
            }
            on_e[f] = (uint16_t)(e / DN_ON_HOP > 32767u ? 32767u : e / DN_ON_HOP);
        }
    }
    for (f = nf; f-- > 1u;)                         /* two frames together (~6 ms) */
        on_e[f] = (uint16_t)(on_e[f] + on_e[f - 1u] > 65535u ? 65535u : on_e[f] + on_e[f - 1u]);
    for (f = 0; f < nf; f++) {                      /* the rise over the level just before (frames f-2, f-3) */
        uint32_t before = f > 1u ? on_e[f - 2u] : 0u;
        if (f > 2u && on_e[f - 3u] > before)
            before = on_e[f - 3u];
        on_d[f] = (uint16_t)(on_e[f] > before ? on_e[f] - before : 0u);
        mx = on_d[f] > mx ? on_d[f] : mx;
    }
    for (f = 0; f < nf; f++) {
        uint32_t j, local = 0, cnt = 0, thr, pk = 1;
        for (j = f > 16u ? f - 16u : 0u; j < f + 17u && j < nf; j++)
            local += on_d[j], cnt++;
        thr = local * 3u / (2u * (cnt ? cnt : 1u));
        if (thr < mx / 20u)
            thr = mx / 20u;                         /* (quiet frames: only real hits count) */
        for (j = f > 2u ? f - 2u : 0u; j <= f + 2u && j < nf; j++)
            if (j != f && (on_d[j] > on_d[f] || (on_d[j] == on_d[f] && j < f)))
                pk = 0;
        if (!pk || on_d[f] <= thr)
            continue;
        {
            uint32_t from = f ? (f - 1u) * DN_ON_HOP : 0u, end = (f + 2u) * DN_ON_HOP, top = 0, k, c, lvl, pre = 0;
            int32_t p2, i2, v;
            if (end > b->loop.n)
                end = b->loop.n;
            c = b->ck[from >> 8];
            p2 = (int16_t)(c & 0xFFFFu);
            i2 = (int32_t)(c >> 16);
            for (k = from & ~255u; k < from; k++)
                ima(b->loop.adpcm, k, &p2, &i2);
            {                                       /* the level before (the frame's first 16 samples) and the peak */
                int32_t pp = p2, ii = i2;
                for (k = from; k < end; k++) {
                    v = ima(b->loop.adpcm, k, &pp, &ii);
                    if (k < from + 16u)
                        pre = (uint32_t)(v < 0 ? -v : v) > pre ? (uint32_t)(v < 0 ? -v : v) : pre;
                    top = (uint32_t)(v < 0 ? -v : v) > top ? (uint32_t)(v < 0 ? -v : v) : top;
                }
                pre /= 2u;
            }
            lvl = 2u * pre + (top > 2u * pre ? (top - 2u * pre) / 4u : 0u);
            for (k = from; k < end; k++) {
                v = ima(b->loop.adpcm, k, &p2, &i2);
                if ((uint32_t)(v < 0 ? -v : v) > lvl)
                    break;
            }
            k = zero_before(b, k, 32u);
            if (n && k < pos[n - 1u] + 10u * DN_ON_HOP) {   /* within ~30 ms of the last: the stronger stays */
                if (on_d[f] > str[n - 1u])
                    pos[n - 1u] = k, str[n - 1u] = on_d[f];
                continue;
            }
            if (n < DN_BRK_MAXSL) {
                pos[n] = k;
                str[n++] = on_d[f];
            } else {                                /* full: the weakest goes */
                uint32_t w = 0;
                for (j = 1; j < n; j++)
                    if (str[j] < str[w])
                        w = j;
                if (str[w] < on_d[f]) {
                    for (j = w; j + 1u < n; j++)
                        pos[j] = pos[j + 1u], str[j] = str[j + 1u];
                    pos[n - 1u] = k;
                    str[n - 1u] = on_d[f];
                }
            }
        }
    }
    if (!n || pos[0] > 10u * DN_ON_HOP) {           /* the loop's start is always a slice */
        if (n == DN_BRK_MAXSL)
            n--;
        for (i = n; i > 0; i--)
            pos[i] = pos[i - 1u];
        n++;
    }
    pos[0] = 0;
    for (i = 0; i < n; i++)
        b->auto_start[i] = pos[i];
    b->n_auto = (uint8_t)n;
}

uint32_t dn_brk_key(const dn_loop_t *loop)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < 64u && i < loop->n / 2u; i++)
        h = (h ^ loop->adpcm[i]) * 16777619u;       /* FNV-1a */
    return h ^ loop->n;
}

uint32_t dn_brk_grid_slices(const dn_brk_t *b) { return 16u * (b->loop.bars ? b->loop.bars : 1u); }

void dn_brk_slices_apply(dn_brk_t *b)
{
    uint32_t i, ns;
    if (b->smode == 2u && !b->n_man) {              /* the first time MANUAL: empty, one slice the whole loop */
        b->man_start[0] = 0;
        b->n_man = 1;
    }
    if (b->smode == 1u && b->n_auto) {
        ns = b->n_auto;
        for (i = 0; i < ns; i++)
            b->start[i] = b->auto_start[i];
    } else if (b->smode == 2u && b->n_man) {
        ns = b->n_man;
        for (i = 0; i < ns; i++)
            b->start[i] = b->man_start[i];
    } else {
        ns = dn_brk_grid_slices(b);
        for (i = 0; i < ns; i++)
            b->start[i] = i * (b->loop.n / ns);
    }
    b->start[ns] = b->loop.n;
    b->nslices = (uint8_t)ns;
}

uint32_t dn_brk_slice_at(const dn_brk_t *b, uint32_t num, uint32_t den)
{
    uint32_t dd = den ? den : 1u, at = (b->loop.n / dd) * num + (b->loop.n % dd) * num / dd, i, best = 0,
             bd = 0xFFFFFFFFu;                      /* (32-bit: n / den x num, no 64-bit divide) */
    for (i = 0; i < b->nslices; i++) {
        uint32_t d = b->start[i] > at ? b->start[i] - at : at - b->start[i];
        if (d < bd)
            bd = d, best = i;
    }
    return best;
}

void dn_brk_range_peaks(const dn_brk_t *b, uint32_t from, uint32_t n, uint8_t *out, uint32_t cols)
{
    uint32_t c = b->ck[(from < b->loop.n ? from : 0u) >> 8], i, col = 0, pk = 0;
    int32_t pred = (int16_t)(c & 0xFFFFu), idx = (int32_t)(c >> 16), v;
    for (i = 0; i < cols; i++)
        out[i] = 0;
    if (!b->ok || from >= b->loop.n || !n || !cols)
        return;
    for (i = from & ~255u; i < from; i++)
        ima(b->loop.adpcm, i, &pred, &idx);
    for (i = 0; i < n && from + i < b->loop.n; i++) {
        uint32_t cc = i * cols / n;
        v = ima(b->loop.adpcm, from + i, &pred, &idx);
        if (cc != col) {
            out[col] = (uint8_t)(pk >> 7);
            col = cc;
            pk = 0;
        }
        pk = (uint32_t)(v < 0 ? -v : v) > pk ? (uint32_t)(v < 0 ? -v : v) : pk;
    }
    if (col < cols)
        out[col] = (uint8_t)(pk >> 7);
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
    b->key = dn_brk_key(&b->loop);
    b->n_man = 0;                                   /* (the app gives back a loop's manual slices after this) */
    find_hits(b);
    dn_brk_slices_apply(b);
    __asm__ volatile("" ::: "memory");              /* the tables are written before the audio may read them */
    b->ok = 1;
}

void dn_brk_tempo(dn_brk_t *b, uint32_t bpm)
{
    float f;
    if (!b->loop.n || !b->loop.bars)
        return;
    b->bpm = bpm;
    f = (float)bpm * (float)b->loop.n / ((float)b->loop.bars * 10584000.0f);   /* 240 s x 44100 a bar */
    {
        int32_t e = dn_clamp((int32_t)b->pitch * 256 + b->mpitch, -24 * 256, 24 * 256 - 1) + 24 * 256;   /* + LFOs */
        float s0 = (float)DN_SEMI_Q16[e >> 8], s1 = (float)DN_SEMI_Q16[(e >> 8) + 1];
        float semi = s0 + (s1 - s0) * (float)(e & 255) / 256.0f;
        float own = (float)(b->loop.rate ? b->loop.rate : 22050u) / 44100.0f;      /* the loop's own speed */
        b->tinc = (uint32_t)(f * 65536.0f);         /* STRETCH: time at the tempo; grains at the pitch */
        b->pinc = (uint32_t)(own * semi);
        b->inc = b->stretch == 1u ? b->pinc          /* SLICE: each slice at its own speed (+ PITCH), placed in time */
                                  : (uint32_t)(f * semi);   /* REPITCH: the speed follows the tempo, pitch and all */
    }
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
    start = b->start[slice];
    b->dc = b->on ? b->last : b->dc;                /* the cut: what was sounding fades in ~1 ms, not a click */
    b->slice = (uint8_t)slice;
    b->rev = (uint8_t)(reverse != 0);
    b->rel = b->fast = b->aud = b->stopend = 0;
    b->env = 32767;
    b->gain = (int32_t)dn_clamp((int32_t)vel, 1, 127) * 258;
    b->played = 0;
    b->hold_n = 0;
    seek(b, start);
    b->endp = b->start[slice + 1u];
    if (b->rev || b->stretch == 2u) {               /* the slice into RAM: played backwards, or by grains */
        uint32_t len = b->start[slice + 1u] - start, i;
        if (len > DN_BRK_RBUF)
            len = DN_BRK_RBUF;
        for (i = 0; i < len; i++)
            b->rbuf[i] = (int16_t)next(b);
        b->blen = len;
        b->rpos = (int32_t)((len - 1u) << 16);
        if (b->stretch == 2u) {                     /* two grains: the first already at its window's top, so the
                                                     * slice's attack plays whole; the second rising from nothing */
            b->tdir = b->rev ? -1 : 1;
            b->tpos = b->rev ? (int32_t)((len - 1u) << 16) : 0;
            b->gpos[0] = b->gpos[1] = (uint32_t)b->tpos;
            b->gage[0] = DN_BRK_GRAIN / 2u;
            b->gage[1] = 0;
        }
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

void dn_brk_audition(dn_brk_t *b, uint32_t at)
{
    uint32_t i;
    if (!b->ok)
        return;
    b->dc = b->on ? b->last : b->dc;
    for (i = 0; i + 1u < b->nslices && b->start[i + 1u] <= at; i++)
        ;
    b->slice = (uint8_t)i;
    b->rev = 0;
    b->rel = b->fast = 0;
    b->aud = 1;
    b->env = 32767;
    b->gain = 127 * 258;
    b->played = 0;
    b->hold_n = 0;
    b->endp = b->loop.n;
    seek(b, at < b->loop.n ? at : 0u);
    b->a = next(b);
    b->b = next(b);
    b->frac = 0;
    b->on = 1;
}

uint32_t dn_brk_pos(const dn_brk_t *b) { return b->p > 2u ? b->p - 2u : 0u; }

void dn_brk_release(dn_brk_t *b)
{
    if (b->decay < 127u)
        b->rel = 1;
}

/* rbuf at a Q16 place (0 outside it) */
static inline int32_t rb_at(const dn_brk_t *b, uint32_t q)
{
    uint32_t j = q >> 16;
    int32_t s0, s1;
    if (j + 1u >= b->blen)
        return j < b->blen ? b->rbuf[j] : 0;
    s0 = b->rbuf[j];
    s1 = b->rbuf[j + 1u];
    return s0 + (((s1 - s0) * (int32_t)((q & 0xFFFFu) >> 1)) >> 15);
}

/* STRETCH: where a new grain starts. Near the time's place, the offset (+-96 samples, steps of 8) whose next 32
 * samples best match what the other grain plays next (WSOLA): the grains join in phase, drums don't flam or phase */
static uint32_t grain_start(const dn_brk_t *b, uint32_t other)
{
    int32_t base = b->tpos >> 16, o, best_o = 0, oth = (int32_t)(other >> 16), j, best = INT32_MIN;
    for (o = -96; o <= 96; o += 8) {
        int32_t c = base + o, acc = 0;              /* (16 products of 14-bit values: fits 32 bits) */
        if (c < 0 || c + 32 >= (int32_t)b->blen || oth + 32 >= (int32_t)b->blen)
            continue;
        for (j = 0; j < 32; j += 2)
            acc += (b->rbuf[c + j] >> 2) * (b->rbuf[oth + j] >> 2);
        if (acc > best)
            best = acc, best_o = o;
    }
    base += best_o;
    return (uint32_t)(base < 0 ? 0 : base) << 16;
}

/* STRETCH: one output sample from the two grains (Hann windows half a grain apart: they add up to one) */
static inline int32_t stretch_tick(dn_brk_t *b)
{
    int32_t v = 0;
    uint32_t g;
    for (g = 0; g < 2u; g++) {
        int32_t w = dn_sine(b->gage[g] << 21);      /* sin(pi age / GRAIN): the age in 2^10 -> half a cycle */
        w = (w * w) >> 15;                          /* sin^2: the Hann window */
        v += (rb_at(b, b->gpos[g]) * w) >> 15;
        b->gpos[g] += b->pinc;
        if (++b->gage[g] >= DN_BRK_GRAIN) {         /* this grain is done: a new one where the time is now */
            b->gage[g] = 0;
            b->gpos[g] = grain_start(b, b->gpos[g ^ 1u]);
        }
    }
    b->tpos += b->tdir * (int32_t)b->tinc;
    if (b->tpos < 0 || (uint32_t)(b->tpos >> 16) >= b->blen) {   /* the slice's time is used up: fade */
        b->tpos = b->tpos < 0 ? 0 : (int32_t)((b->blen - 1u) << 16);
        if (!b->rel)
            b->rel = b->fast = 1;
    }
    return v;
}

void dn_brk_render(dn_brk_t *b, int32_t *lr, uint32_t n)
{
    uint32_t i, k;
    int32_t cr = dn_clamp((int32_t)b->crush + b->mcrush, 0, 127), dec = dn_clamp((int32_t)b->decay + b->mdecay, 0, 126);
    int32_t lv = dn_clamp((int32_t)b->level + b->mlevel, 0, 127);   /* (each + the LFOs) */
    uint32_t hold = 1u + (uint32_t)cr / 9u, shift = (uint32_t)cr / 11u;
    if (!b->ok)
        return;
    for (i = 0; i < n; i++) {
        int32_t v = 0, o;
        uint8_t was = b->on;
        k = DN_ENV_K[b->fast ? 30 : dec] >> 5;   /* per sample (the table: per 32) */
        if (b->on) {
            if (b->aud) {                           /* the loop round and round, at its own speed */
                v = b->a + (((b->b - b->a) * (int32_t)(b->frac >> 1)) >> 15);
                b->frac += b->pinc;
                while (b->frac >= 65536u) {
                    b->frac -= 65536u;
                    b->a = b->b;
                    if (b->p >= b->loop.n)
                        seek(b, 0);
                    b->b = next(b);
                }
                while (b->slice + 1u < b->nslices && b->p >= b->start[b->slice + 1u] + 2u)
                    b->slice++;
                if (b->p < 4u)
                    b->slice = 0;
            } else if (b->stretch == 2u) {
                v = stretch_tick(b);
                b->played += b->tinc >> 8;
            } else if (b->rev) {
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
                if ((b->stretch == 1u || b->stopend) && b->p >= b->endp && !b->rel)
                    b->rel = b->fast = 1;           /* SLICE: the slice ends (a slower tempo leaves a gap): fade */
            }
            if (b->stretch != 2u)
                b->played += b->inc >> 8;           /* (Q8: the screen's playhead) */
            if (cr) {                               /* sample rate down (hold), then bits down */
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
        o = (o * lv) / 100;
        lr[2u * i] += o;
        lr[2u * i + 1u] += o;
    }
}
