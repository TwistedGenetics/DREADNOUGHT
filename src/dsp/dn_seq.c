/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: the 8-track step sequencer (dn_seq.h says what it does). Copyright (C) 2026 Jamie (TwistedGenetics). */
#include "dn_seq.h"

#define SPS_Q8_X_BPM 169344000                      /* 44100 x 15 x 256: a sixteenth at 1 BPM, in samples Q8 */

static int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

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
        for (i = 0; i < DN_SEQ_STEPS; i++) {
            p->tr[t].st[i].note = 53;
            p->tr[t].st[i].len = 3;                 /* three quarters of a step */
            p->tr[t].st[i].vel = 100;
        }
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

int dn_pattern_valid(const dn_pattern_t *cp)
{
    dn_pattern_t *p = (dn_pattern_t *)cp;
    uint32_t t, i;
    if (p->magic == DN_SEQ_MAGIC_V1) {              /* 0.4 / 0.5: a length was sixteenths of one step */
        for (t = 0; t < DN_SEQ_TRACKS; t++)
            for (i = 0; i < DN_SEQ_STEPS; i++) {
                uint32_t l = clampi(p->tr[t].st[i].len, 1, 16);
                p->tr[t].st[i].len = (uint8_t)((l + 2u) / 4u ? (l + 2u) / 4u : 1u);
            }
        p->magic = DN_SEQ_MAGIC;
    }
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
            st->flags &= DN_ST_ON | DN_ST_SLIDE | DN_ST_ACC;
            st->len = (uint8_t)clampi(st->len, 1, DN_LEN_MAX);
            st->vel = (uint8_t)clampi(st->vel, 1, 127);
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
    for (t = 0; t < DN_SEQ_TRACKS; t++)
        s->sounding[t] = 0xFF;
}

static void release(dn_seq_t *s, uint32_t t, const dn_seq_cb_t *cb)
{
    if (s->sounding[t] != 0xFF)
        cb->note_off(t, s->sounding[t]);
    s->sounding[t] = 0xFF;
    s->gate[t] = 0;
}

void dn_seq_start(dn_seq_t *s, const dn_seq_cb_t *cb)
{
    uint32_t t;
    for (t = 0; t < DN_SEQ_TRACKS; t++)
        release(s, t, cb);
    s->pos = 0;
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
    for (t = 0; t < DN_SEQ_TRACKS; t++)
        if (s->gate[t] > 0 && (uint32_t)s->gate[t] < n)
            n = (uint32_t)s->gate[t];
    return n;
}

static void fire_step(dn_seq_t *s, const dn_seq_cb_t *cb)
{
    uint32_t k = s->pos, t;
    int32_t q = dn_seq_step_q8(s, k);
    for (t = 0; t < DN_SEQ_TRACKS; t++) {
        const dn_track_t *tr = &s->pat.tr[t];
        uint32_t L = tr->len ? tr->len : 1u, i = k % L;
        const dn_step_t *st = &tr->st[i];
        int slide;
        s->cur[t] = (uint8_t)i;
        if (tr->mute) {
            release(s, t, cb);
            continue;
        }
        if (!(st->flags & DN_ST_ON))
            continue;                               /* a long note sounds on through steps that are off */
        slide = (st->flags & DN_ST_SLIDE) && s->sounding[t] != 0xFF;
        if (s->sounding[t] != 0xFF && !slide)
            release(s, t, cb);
        cb->note_on(t, st->note, (st->flags & DN_ST_ACC) ? 127u : st->vel, slide, st->flags);
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
                    break;
                }
            }
            s->gate[t] = g > 0 ? g : 1;
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
    for (t = 0; t < DN_SEQ_TRACKS; t++)
        if (s->gate[t] > 0) {
            s->gate[t] -= (int32_t)n;
            if (s->gate[t] <= 0)
                release(s, t, cb);
        }
    if (s->pulses < 6u && s->until_clock <= 0 && s->until_step > 0) {
        cb->clock(0xF8);
        s->pulses++;
        s->until_clock += dn_seq_step_q8(s, s->pos - 1u) / 6;
    }
    if (s->until_step <= 0)
        fire_step(s, cb);
}
