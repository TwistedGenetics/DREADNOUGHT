/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: the 8-track step sequencer (dn_seq.h says what it does). Copyright (C) 2026 Jamie (TwistedGenetics). */
#include "dn_seq.h"

#define SPS_Q8_X_BPM 169344000                      /* 44100 x 15 x 256: a sixteenth at 1 BPM, in samples Q8 */

static int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

const uint8_t DN_CO_PCT[DN_CO_NPCT] = {1, 2, 4, 6, 9, 13, 19, 25, 33, 41, 50, 59, 67, 75, 81, 87, 91, 94, 96, 98, 99, 100};
const uint8_t DN_RTIM_DEN[DN_RTIM_N] = {1, 2, 3, 4, 5, 6, 8, 10, 12, 16, 20, 24, 32, 40, 48, 64, 80};

void dn_cond_ab(uint32_t c, uint32_t *a, uint32_t *b)
{
    uint32_t i = (c - DN_CO_AB) % DN_CO_NAB, B = 2;
    while (i >= B) {                                /* 1:2 2:2 | 1:3 2:3 3:3 | ... */
        i -= B;
        B++;
    }
    *a = i + 1u;
    *b = B;
}
static void put(char *o, const char *s)
{
    uint32_t i;
    for (i = 0; s[i] && i < 7u; i++)
        o[i] = s[i];
    o[i] = 0;
}
void dn_cond_text(uint32_t c, char out[8])
{
    static const char *const W[10] = {"FILL", "!FILL", "PRE", "!PRE", "NEI", "!NEI", "1ST", "!1ST", "LST", "!LST"};
    if (c == DN_CO_NONE) {
        put(out, "ALWAYS");
    } else if (c <= DN_CO_NPCT) {
        uint32_t p = DN_CO_PCT[c - 1u], n = 0;
        if (p >= 100u) out[n++] = '1';
        if (p >= 10u) out[n++] = (char)('0' + p / 10u % 10u);
        out[n++] = (char)('0' + p % 10u);
        out[n++] = '%';
        out[n] = 0;
    } else if (c < DN_CO_AB) {
        put(out, W[c - DN_CO_FILL]);
    } else if (c < DN_CO_N) {
        uint32_t a, b, n = 0;
        dn_cond_ab(c, &a, &b);
        if (c >= DN_CO_AB + DN_CO_NAB) out[n++] = '!';
        out[n++] = (char)('0' + a);
        out[n++] = ':';
        out[n++] = (char)('0' + b);
        out[n] = 0;
    } else {
        put(out, "?");
    }
}

void dn_step_clear(dn_step_t *st)
{
    st->note = 53;
    st->flags = 0;
    st->len = 3;                                    /* three quarters of a step */
    st->vel = 100;
    st->cond = 0;
    st->rtim = 12;                                  /* 1/32: two a step */
    st->rlen = 0;
    st->rvel = 0;
}

uint8_t dn_rt_vel(uint32_t vel, int32_t curve, uint32_t k, uint32_t n)
{
    int32_t d = 127 * (int32_t)(n - 1u), v;
    if (n <= 1u || !curve)
        return (uint8_t)vel;
    if (curve < 0)                                  /* fade out: vel .. down by |curve| / 127 of it at the last */
        v = (int32_t)vel * (d + curve * (int32_t)k) / d;
    else                                            /* fade in: from (1 - curve / 127) of vel up to vel */
        v = (int32_t)vel * (d - curve * (int32_t)(n - 1u - k)) / d;
    return (uint8_t)clampi(v, 1, 127);
}

int32_t dn_seq_step_q8(const dn_seq_t *s, uint32_t step)
{
    int32_t pair = 2 * SPS_Q8_X_BPM / clampi(s->bpm, 20, 300);     /* two sixteenths (< 2^25: x 75 fits) */
    int32_t first = pair * clampi(s->pat.swing, 50, 75) / 100;
    return (step & 1u) ? pair - first : first;      /* the pair adds up exactly: no drift against the tempo */
}

void dn_pattern_clear(dn_pattern_t *p)
{
    uint32_t t, i;
    uint8_t *b = (uint8_t *)p;
    for (i = 0; i < sizeof *p; i++)
        b[i] = 0;
    p->magic = DN_SEQ_MAGIC;
    p->swing = 50;
    for (t = 0; t < DN_SEQ_TRACKS; t++) {
        p->tr[t].len = 16;
        p->tr[t].chan = (uint8_t)t;
        p->tr[t].last_note = 53;                    /* F3, the lowest key: F1 on the reese presets (TRANS -24) */
        for (i = 0; i < DN_SEQ_STEPS; i++)
            dn_step_clear(&p->tr[t].st[i]);
    }
}

/* a rolling reese line in F for the first PLAY: a held root, a stab, a climb that glides and holds, a drop to the
 * lower octave on the last beat */
void dn_pattern_demo(dn_pattern_t *p)
{
    static const uint8_t S = DN_ST_SLIDE, A = DN_ST_ACC;
    static const uint8_t D[][4] = {      /* step, note, length (quarter steps), flags */
        {0, 53, 11, 0}, {3, 53, 2, A}, {6, 56, 4, 0}, {7, 58, 7, S}, {10, 53, 7, 0}, {12, 51, 7, 0}, {14, 41, 7, A},
    };
    uint32_t i;
    dn_pattern_clear(p);
    for (i = 0; i < sizeof D / sizeof D[0]; i++) {
        dn_step_t *st = &p->tr[0].st[D[i][0]];
        st->note = D[i][1];
        st->len = D[i][2];
        st->flags = (uint8_t)(DN_ST_ON | D[i][3]);
    }
    p->swing = 54;
}

/* 0.4 .. 0.10: the same tracks with 4-byte steps {note, flags, len, vel} (DNQ1: lengths in sixteenths of a step) */
int dn_pattern_from_old(dn_pattern_t *p, const uint8_t *b, uint32_t n)
{
    uint32_t magic, t, i, tb = DN_SEQ_STEPS * 4u + 4u;
    if (n < 8u + DN_SEQ_TRACKS * tb)
        return 0;
    magic = b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
    if (magic != DN_SEQ_MAGIC_V2 && magic != DN_SEQ_MAGIC_V1)
        return 0;
    dn_pattern_clear(p);
    p->swing = b[4];
    p->pad[0] = b[5], p->pad[1] = b[6], p->pad[2] = b[7];
    for (t = 0; t < DN_SEQ_TRACKS; t++) {
        const uint8_t *o = b + 8u + t * tb;
        dn_track_t *tr = &p->tr[t];
        for (i = 0; i < DN_SEQ_STEPS; i++) {
            dn_step_t *st = &tr->st[i];
            st->note = o[4u * i];
            st->flags = o[4u * i + 1u] & (DN_ST_ON | DN_ST_SLIDE | DN_ST_ACC);
            st->len = o[4u * i + 2u];
            st->vel = o[4u * i + 3u];
            if (magic == DN_SEQ_MAGIC_V1) {
                uint32_t l = clampi(st->len, 1, 16);
                st->len = (uint8_t)((l + 2u) / 4u ? (l + 2u) / 4u : 1u);
            }
        }
        o += DN_SEQ_STEPS * 4u;
        tr->len = o[0], tr->mute = o[1], tr->chan = o[2], tr->last_note = o[3];
    }
    return dn_pattern_valid(p);
}

int dn_pattern_valid(const dn_pattern_t *cp)
{
    dn_pattern_t *p = (dn_pattern_t *)cp;
    uint32_t t, i;
    if (p->magic != DN_SEQ_MAGIC)
        return 0;
    p->swing = (uint8_t)clampi(p->swing, 50, 75);
    for (t = 0; t < DN_SEQ_TRACKS; t++) {
        dn_track_t *tr = &p->tr[t];
        tr->len = (uint8_t)clampi(tr->len, 1, DN_SEQ_STEPS);
        tr->mute = tr->mute ? 1u : 0u;
        tr->chan &= 15u;
        tr->last_note &= 127u;
        for (i = 0; i < DN_SEQ_STEPS; i++) {
            dn_step_t *st = &tr->st[i];
            st->note &= 127u;
            st->flags &= DN_ST_ON | DN_ST_SLIDE | DN_ST_ACC | DN_ST_RTRG | DN_ST_CHORD;
            st->len = (uint8_t)clampi(st->len, 1, DN_LEN_MAX);
            st->vel = (uint8_t)clampi(st->vel, 1, 127);
            if (st->cond >= DN_CO_N)
                st->cond = 0;
            if (st->rtim >= DN_RTIM_N)
                st->rtim = 12;
            st->rvel = (int8_t)clampi(st->rvel, -127, 127);
        }
    }
    return 1;
}

void dn_pattern_coverage(const dn_pattern_t *p, uint32_t t, uint8_t out[DN_SEQ_STEPS])
{
    const dn_track_t *tr = &p->tr[t];
    uint32_t L = tr->len ? tr->len : 1u, i, j;
    for (i = 0; i < DN_SEQ_STEPS; i++)
        out[i] = 0;
    for (i = 0; i < L; i++) {
        const dn_step_t *st = &tr->st[i];
        uint32_t span;
        if (!(st->flags & DN_ST_ON))
            continue;
        out[i] = 1;
        span = (st->len + DN_LEN_STEP - 1u) / DN_LEN_STEP;      /* the steps it reaches into */
        for (j = 1; j < span && j < L; j++) {
            uint32_t k = (i + j) % L;
            if (tr->st[k].flags & DN_ST_ON)
                break;                              /* cut (or slid into) there */
            if (!out[k])
                out[k] = 2;
        }
    }
}

void dn_seq_init(dn_seq_t *s)
{
    uint32_t t;
    uint8_t *b = (uint8_t *)s;
    for (t = 0; t < sizeof *s; t++)
        b[t] = 0;
    dn_pattern_clear(&s->pat);
    s->bpm = 172;
    s->legato = 0xFF;
    s->rnd = 0x2545F491u;
    for (t = 0; t < DN_SEQ_TRACKS; t++)
        s->sounding[t] = 0xFF;
}

void dn_seq_track_new(dn_seq_t *s, uint32_t t)
{
    s->tpos[t] = 0;
    s->pass[t] = 0;
    s->had[t] = 0;
    s->rt_n[t] = 0;
}

void dn_seq_track_restart(dn_seq_t *s, uint32_t t)
{
    dn_seq_track_new(s, t);
    s->fresh |= (uint8_t)(1u << t);
}

static void release(dn_seq_t *s, uint32_t t, const dn_seq_cb_t *cb)
{
    if (s->sounding[t] != 0xFF)
        cb->note_off(t, s->sounding[t]);
    s->sounding[t] = 0xFF;
    s->gate[t] = 0;
    s->ovl &= (uint8_t)~(1u << t);
    s->rt_n[t] = 0;                                 /* (and any retrigs stop) */
}

void dn_seq_start(dn_seq_t *s, const dn_seq_cb_t *cb)
{
    uint32_t t;
    for (t = 0; t < DN_SEQ_TRACKS; t++)
        release(s, t, cb);
    s->pos = 0;
    s->fresh = 0;
    for (t = 0; t < DN_SEQ_TRACKS; t++)
        dn_seq_track_new(s, t);
    s->until_step = 0;                              /* the first step on the next sample */
    s->until_clock = 0;
    s->pulses = 6;                                  /* (the step sends its first pulse) */
    s->playing = 1;
    cb->clock(0xFA);
}

void dn_seq_stop(dn_seq_t *s, const dn_seq_cb_t *cb)
{
    uint32_t t;
    if (!s->playing)
        return;
    s->playing = 0;
    for (t = 0; t < DN_SEQ_TRACKS; t++)
        release(s, t, cb);
    cb->clock(0xFC);
}

static uint32_t ceil_q8(int32_t q) { return q <= 0 ? 1u : (uint32_t)((q + 255) >> 8); }

uint32_t dn_seq_next(const dn_seq_t *s)
{
    uint32_t n, t;
    if (!s->playing)
        return 1u << 30;
    n = ceil_q8(s->until_step);
    if (s->pulses < 6u && ceil_q8(s->until_clock) < n)
        n = ceil_q8(s->until_clock);
    for (t = 0; t < DN_SEQ_TRACKS; t++) {
        if (s->gate[t] > 0 && (uint32_t)s->gate[t] < n)
            n = (uint32_t)s->gate[t];
        if (s->rt_k[t] < s->rt_n[t] && ceil_q8(s->rt_left[t]) < n)
            n = ceil_q8(s->rt_left[t]);
    }
    return n;
}

/* the trig condition on track t's step: plays or not. PRE / !PRE don't count as evaluated (the manual's rule) */
static uint32_t dice(dn_seq_t *s)
{
    uint32_t x = s->rnd;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s->rnd = x;
    return x;
}
static int cond_ok(dn_seq_t *s, uint32_t t, uint32_t c)
{
    int r;
    uint32_t a, b;
    if (c == DN_CO_NONE)
        return 1;
    if (c == DN_CO_PRE || c == DN_CO_NPRE)
        return (s->had[t] && s->pre[t]) == (c == DN_CO_PRE);
    if (c <= DN_CO_NPCT)
        r = (dice(s) >> 8) % 100u < DN_CO_PCT[c - 1u];
    else if (c == DN_CO_FILL || c == DN_CO_NFILL)
        r = (s->fill != 0) == (c == DN_CO_FILL);
    else if (c == DN_CO_NEI || c == DN_CO_NNEI) {
        uint32_t nb = t ? t - 1u : DN_SEQ_TRACKS - 1u;   /* (the track before; track 1's: track 8) */
        r = (s->had[nb] && s->pre[nb]) == (c == DN_CO_NEI);
    } else if (c == DN_CO_1ST || c == DN_CO_N1ST)
        r = (s->pass[t] == 0u) == (c == DN_CO_1ST);
    else if (c == DN_CO_LST || c == DN_CO_NLST)
        r = ((s->lst >> t & 1u) != 0u) == (c == DN_CO_LST);
    else if (c < DN_CO_N) {
        dn_cond_ab(c, &a, &b);
        r = (s->pass[t] % b == a - 1u) == (c < DN_CO_AB + DN_CO_NAB);
    } else
        r = 1;
    s->pre[t] = (uint8_t)r;
    s->had[t] = 1;
    return r;
}

static void fire_step(dn_seq_t *s, const dn_seq_cb_t *cb)
{
    uint32_t k = s->pos, t;
    int32_t q = dn_seq_step_q8(s, k);
    if (cb->bar && k % 16u == 0u)
        cb->bar(k);                                 /* (0.38) a bar begins: the song's turn */
    for (t = 0; t < DN_SEQ_TRACKS; t++) {
        const dn_track_t *tr = &s->pat.tr[t];
        uint32_t L, i;
        const dn_step_t *st;
        int slide;
        if (s->tpos[t] >= (tr->len ? tr->len : 1u))
            s->tpos[t] = 0;                         /* (the track was made shorter) */
        if (s->fresh >> t & 1u) {                   /* (0.38) restarted by the song: from the top, as at PLAY */
            s->fresh &= (uint8_t)~(1u << t);
        } else if (s->tpos[t] == 0u && k > 0u) {    /* round again: a pass more; the app may switch the pattern */
            if (s->pass[t] < 0xFFFFu)
                s->pass[t]++;
            if (cb->wrap)
                cb->wrap(t);
            tr = &s->pat.tr[t];
        }
        L = tr->len ? tr->len : 1u;
        i = s->tpos[t];
        s->tpos[t] = (uint8_t)((i + 1u) % L);
        st = &tr->st[i];
        s->cur[t] = (uint8_t)i;
        if (tr->mute) {
            release(s, t, cb);
            continue;
        }
        if (!(st->flags & DN_ST_ON))
            continue;                               /* a long note sounds on through steps that are off */
        if (!cond_ok(s, t, st->cond))
            continue;                               /* its condition says no: as if it were off */
        slide = ((st->flags & DN_ST_SLIDE) || (s->ovl >> t & 1u)) && s->sounding[t] != 0xFF;
        s->ovl &= (uint8_t)~(1u << t);
        if (s->sounding[t] != 0xFF && !slide)
            release(s, t, cb);
        {
            uint32_t v = (st->flags & DN_ST_ACC) ? 127u : st->vel;
            if (st->flags & DN_ST_RTRG) {           /* the first of the retrigs: the curve's start */
                int32_t whole = 8 * (dn_seq_step_q8(s, k) + dn_seq_step_q8(s, k + 1u)), iv;
                uint32_t pr = (uint32_t)(dn_seq_step_q8(s, k) + dn_seq_step_q8(s, k + 1u)) >> 8;
                int32_t tot = (int32_t)(pr << 8) * (st->rlen ? st->rlen : st->len) / (int32_t)(2u * DN_LEN_STEP);
                iv = st->rtim < DN_RTIM_N ? whole / DN_RTIM_DEN[st->rtim] : 0;
                if (iv > 0)
                    v = dn_rt_vel(v, st->rvel, 0, (uint32_t)clampi((tot + iv - 1) / iv, 1, 255));
            }
            cb->note_on(t, st->note, (uint8_t)v, slide, st->flags);
        }
        if (slide && s->sounding[t] != st->note)
            cb->note_off(t, s->sounding[t]);        /* (MIDI: the old note ends after the new one starts) */
        s->sounding[t] = st->note;
        {
            /* the length: quarter steps at the pair's average (swing evens out over two steps) */
            uint32_t pair = (uint32_t)(dn_seq_step_q8(s, k) + dn_seq_step_q8(s, k + 1u)) >> 8, j, span, dist = 0;
            int32_t g = (int32_t)(pair * st->len / (2u * DN_LEN_STEP));
            span = (st->len + DN_LEN_STEP - 1u) / DN_LEN_STEP;
            for (j = 1; j <= span && L > 1u; j++) {            /* the next step that plays, inside the length */
                const dn_step_t *nx = &tr->st[(i + j) % L];
                dist += (uint32_t)dn_seq_step_q8(s, k + j - 1u) >> 8;
                if (nx->flags & DN_ST_ON) {
                    if (nx->flags & DN_ST_SLIDE)
                        g = (int32_t)dist + 64;     /* it slides from this note: hold on into it (just past it) */
                    else if (st->len > j * DN_LEN_STEP && (s->legato & ~DN_PAT_CUT(&s->pat)) >> t & 1u) {
                        g = (int32_t)dist + 64;     /* this note reaches past the next one: it slides there too */
                        s->ovl |= (uint8_t)(1u << t);
                    }
                    break;
                }
            }
            s->gate[t] = g > 0 ? g : 1;
            s->rt_n[t] = 0;
            if ((st->flags & DN_ST_RTRG) && st->rtim < DN_RTIM_N) {   /* retrigs: at the rate, for RLEN (or the
                                                     * note's length), cut by the next note */
                int32_t whole = 8 * (dn_seq_step_q8(s, k) + dn_seq_step_q8(s, k + 1u));   /* Q8, 16 steps */
                int32_t iv = whole / DN_RTIM_DEN[st->rtim], tot = (int32_t)(pair << 8) * (st->rlen ? st->rlen : st->len)
                             / (int32_t)(2u * DN_LEN_STEP);
                int32_t n = iv > 0 ? (tot + iv - 1) / iv : 1;
                n = clampi(n, 1, 255);
                if (n > 1) {
                    s->rt_n[t] = (uint8_t)n;
                    s->rt_k[t] = 1;
                    s->rt_int[t] = iv;
                    s->rt_left[t] = iv;
                    s->rt_note[t] = st->note;
                    s->rt_vel[t] = (st->flags & DN_ST_ACC) ? 127u : st->vel;
                    s->rt_curve[t] = st->rvel;
                    s->rt_flags[t] = (uint8_t)(st->flags & ~DN_ST_SLIDE);
                    if (s->gate[t] < (tot >> 8))
                        s->gate[t] = tot >> 8;      /* (the note sounds through its retrigs) */
                }
            }
        }
    }
    s->pos++;
    s->until_step += q;
    cb->clock(0xF8);                                /* six pulses a sixteenth: 24 a quarter */
    s->pulses = 1;
    s->until_clock = q / 6;
}

void dn_seq_run(dn_seq_t *s, uint32_t n, const dn_seq_cb_t *cb)
{
    uint32_t t;
    if (!s->playing)
        return;
    s->until_step -= (int32_t)(n << 8);
    if (s->pulses < 6u)
        s->until_clock -= (int32_t)(n << 8);
    for (t = 0; t < DN_SEQ_TRACKS; t++) {
        if (s->rt_k[t] < s->rt_n[t]) {             /* a retrig due: the note again, along the curve */
            s->rt_left[t] -= (int32_t)(n << 8);
            if (s->rt_left[t] <= 0 && s->sounding[t] != 0xFF) {
                uint8_t v = dn_rt_vel(s->rt_vel[t], s->rt_curve[t], s->rt_k[t], s->rt_n[t]);
                cb->note_off(t, s->sounding[t]);
                cb->note_on(t, s->rt_note[t], v, 0, s->rt_flags[t]);
                s->sounding[t] = s->rt_note[t];
                s->rt_k[t]++;
                s->rt_left[t] += s->rt_int[t];
            }
        }
        if (s->gate[t] > 0) {
            s->gate[t] -= (int32_t)n;
            if (s->gate[t] <= 0)
                release(s, t, cb);
        }
    }
    if (s->pulses < 6u && s->until_clock <= 0 && s->until_step > 0) {
        cb->clock(0xF8);
        s->pulses++;
        s->until_clock += dn_seq_step_q8(s, s->pos - 1u) / 6;
    }
    if (s->until_step <= 0)
        fire_step(s, cb);
}
