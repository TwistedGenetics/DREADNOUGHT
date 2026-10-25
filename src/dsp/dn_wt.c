/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: WAVE (0.32), the wavetable synth (dn_wt.h says what it does). Copyright (C) 2026 Jamie (TwistedGenetics).
 * Single-precision float for the filter and the envelopes (the FM-1's FPU), integers for the phases and the tables;
 * no 64-bit divides. */
#include "dn_wt.h"
#include "dn_tables.h"

#define WT_TICK 32u                                 /* control: every 32 samples */
static const uint16_t WT_MIPOFF[7] = {0, 512, 768, 896, 960, 992, 1008};   /* each version's first sample in a frame */

/* ---- tables ------------------------------------------------------------------------------------------------ */
uint32_t dn_wt_frames(const uint8_t *t) { return t ? t[5] : 0u; }
int dn_wt_valid(const uint8_t *t, uint32_t len)
{
    if (!t || len < DN_WT_HDR || t[0] != 'D' || t[1] != 'N' || t[2] != 'W' || t[3] != 'T' || t[4] != 1u || !t[5] ||
        t[5] > DN_WT_MAXF || t[6] != 9u || t[7] != 7u)
        return 0;
    return len >= DN_WT_HDR + (uint32_t)t[5] * DN_WT_FRAME_BYTES ? (int)t[5] : 0;
}
void dn_wt_name(const uint8_t *t, char out[9])
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        out[i] = t && t[8u + i] >= 32u && t[8u + i] < 127u ? (char)t[8u + i] : 0;
    out[8] = 0;
}
static inline int32_t wt_s16(const uint8_t *p) { return (int16_t)(uint16_t)(p[0] | p[1] << 8); }   /* (any alignment) */
/* a frame's version m at phase q, linear between its samples */
static inline int32_t wt_read(const uint8_t *fr, uint32_t m, uint32_t q)
{
    uint32_t sh = 23u + m, sz = 512u >> m, i = q >> sh, j = (i + 1u) & (sz - 1u), f = (q >> (sh - 15u)) & 0x7FFFu;
    const uint8_t *b = fr + 2u * WT_MIPOFF[m];
    int32_t a = wt_s16(b + 2u * i), c = wt_s16(b + 2u * j);
    return a + (((c - a) * (int32_t)f) >> 15);
}
int32_t dn_wt_peek(const uint8_t *t, uint32_t f, uint32_t m, uint32_t q)
{
    return wt_read(t + DN_WT_HDR + f * DN_WT_FRAME_BYTES, m, q);
}

/* ---- small maths ------------------------------------------------------------------------------------------- */
static float wt_exp2(float x)                       /* 2^x for |x| < 8, to ~1e-4 */
{
    int32_t i = (int32_t)(x < 0.0f ? x - 1.0f : x);
    float f = x - (float)i, p = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * 0.0096181f)));
    while (i > 0) p *= 2.0f, i--;
    while (i < 0) p *= 0.5f, i++;
    return p;
}
static uint32_t wt_inc(float semis)                 /* the phase step of a pitch in semitones (MIDI note numbers) */
{
    int32_t n = (int32_t)(semis < 0.0f ? semis - 1.0f : semis);
    float f;
    if (n < 0) n = 0;
    if (n > 126) n = 126;
    f = semis - (float)n;
    if (f > 2.0f) f = 2.0f;
    if (f < 0.0f) f = 0.0f;
    return (uint32_t)((float)DN_NOTE_INC[n] * wt_exp2(f * (1.0f / 12.0f)));
}
static inline int32_t wt_sine(uint32_t q)           /* Q15 */
{
    uint32_t i = q >> 22, f = (q >> 7) & 0x7FFFu;
    int32_t a = DN_SINE[i], b = DN_SINE[i + 1u];
    return a + (((b - a) * (int32_t)f) >> 15);
}
static inline float wt_clip(float x)                /* a soft clip (tanh-like) */
{
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}
static float wt_k(int32_t v) { return (float)DN_ENV_K[v < 0 ? 0 : v > 127 ? 127 : v] * (1.0f / 65536.0f); }

/* ---- values ------------------------------------------------------------------------------------------------ */
static const int8_t WT_MIN[WP_N] = {0, 0, 0, 0, 1, 0, 0, 0, 0, 0, -64, 0, 0, 0, 0, 0, 0, 0, 0, 0, -64, 0, 0, 0, 0, 1, 0,
                                    -24};
static const int8_t WT_MAX[WP_N] = {0, 127, WW_N - 1, 127, 4, 127, 127, 127, 127, 127, 63, 127, 127, 127, 127, 127,
                                    127, 127, 127, 127, 63, 127, 127, 127, 127, DN_WT_NV, 127, 24};
static const char *const WT_NAME[WP_N] = {"TABLE", "POS", "WARP", "AMOUNT", "UNISON", "DETUNE", "SPREAD", "SUB",
                                          "CUT", "RES", "ENV", "DRIVE", "ATK", "DEC", "SUS", "REL", "M.ATK", "M.DEC",
                                          "M.SUS", "M.REL", "POS ENV", "POS LFO", "LFO RATE", "CUT LFO", "LEVEL",
                                          "VOICES", "GLIDE", "TRANS"};
const char *dn_wt_vname(uint32_t k) { return k < WP_N ? WT_NAME[k] : ""; }
int32_t dn_wt_vmin(uint32_t k) { return k < WP_N ? WT_MIN[k] : 0; }
int32_t dn_wt_vmax(uint32_t k) { return k < WP_N ? WT_MAX[k] : 0; }
static void wt_num(char *o, int32_t v)
{
    char t[8];
    int32_t n = 0, i = 0;
    uint32_t u = v < 0 ? (uint32_t)-v : (uint32_t)v;
    if (v < 0) o[i++] = '-';
    do t[n++] = (char)('0' + u % 10u), u /= 10u; while (u);
    while (n) o[i++] = t[--n];
    o[i] = 0;
}
void dn_wt_vtext(uint32_t k, int32_t v, char out[8])
{
    static const char *const W[WW_N] = {"OFF", "SYNC", "BEND", "MIRROR", "PWM", "FM", "CRUSH"};
    uint32_t i;
    if (k == WP_WARP) {
        const char *w = W[v < 0 ? 0 : v >= WW_N ? WW_N - 1 : v];
        for (i = 0; w[i]; i++) out[i] = w[i];
        out[i] = 0;
        return;
    }
    if (k == WP_VOICES && v == 1) {
        out[0] = 'M', out[1] = 'O', out[2] = 'N', out[3] = 'O', out[4] = 0;
        return;
    }
    if ((k == WP_FENV || k == WP_PENV || k == WP_TRANS) && v > 0) {
        out[0] = '+';
        wt_num(out + 1, v);
        return;
    }
    wt_num(out, k == WP_TABLE ? v + 1 : v);
}
void dn_wt_defaults(int16_t *p)
{
    static const int8_t D[WP_N] = {0, 0, 0, 0, 1, 20, 64, 0, 100, 20, 0, 0, 2, 60, 110, 40, 2, 60, 0, 40, 0, 0, 40, 0,
                                   100, DN_WT_NV, 0, 0};
    uint32_t k;
    for (k = 0; k < WP_N; k++)
        p[k] = D[k];
}

/* the factory sounds (PRESETS): each every value, its table one of the factory's (TABLE: 0 ANALOG .. 7 HOLLOW) */
static const struct { char name[10]; int8_t p[WP_N]; } WT_PRESET[] = {
    /*            TAB POS WRP AMT UNI DET SPR SUB CUT RES ENV DRV ATK DEC SUS REL MA  MD  MS  MR  PEN PLF LRT CLF LVL VC GLD TRN */
    {"INIT",      {0,  0,  0,  0,  1,  20, 64, 0,  100, 20, 0,  0,  2,  60, 110, 40, 2,  60, 0,  40, 0,  0,  40, 0,  100, 6, 0, 0}},
    {"SUPERSAW",  {0,  127, 0, 0,  4,  45, 110, 0, 112, 10, 0,  0,  2,  60, 115, 55, 2,  60, 0,  40, 0,  0,  40, 0,  90,  6, 0, 0}},
    {"REESE",     {1,  40, 0,  0,  2,  25, 60, 60, 62, 30, 18, 40, 0,  60, 127, 22, 0,  70, 0,  30, 0,  30, 18, 0,  100, 1, 18, 0}},
    {"PPG BELL",  {2,  0,  0,  0,  1,  0,  64, 0,  120, 10, 0,  0,  0,  82, 40, 70, 0,  72, 0,  60, 50, 0,  40, 0,  100, 6, 0, 0}},
    {"VOX PAD",   {3,  30, 0,  0,  2,  15, 90, 0,  92, 15, 0,  0,  62, 80, 110, 82, 2,  60, 0,  40, 0,  48, 14, 0,  100, 4, 0, 0}},
    {"SYNC LEAD", {4,  10, 1,  50, 1,  0,  64, 30, 100, 30, 10, 20, 0,  60, 110, 30, 0,  62, 20, 40, 40, 0,  40, 0,  100, 1, 14, 0}},
    {"PWM STRNG", {5,  40, 0,  0,  2,  20, 90, 0,  86, 10, 0,  0,  52, 70, 110, 70, 2,  60, 0,  40, 0,  40, 24, 0,  100, 4, 0, 0}},
    {"GRIT BASS", {6,  20, 0,  0,  1,  0,  64, 50, 70, 40, 30, 70, 0,  64, 100, 28, 0,  52, 0,  30, 60, 0,  40, 0,  100, 1, 6, -12}},
    {"ORGAN",     {7,  60, 0,  0,  1,  0,  64, 20, 104, 10, 0,  10, 2,  70, 100, 40, 2,  60, 0,  40, 0,  0,  40, 0,  100, 6, 0, 0}},
    {"FM GROWL",  {0,  0,  5,  60, 1,  0,  64, 40, 80, 50, 20, 50, 0,  60, 120, 30, 0,  60, 0,  40, 0,  0,  40, 30, 100, 1, 10, -12}},
    {"BENT KEYS", {7,  30, 2,  70, 2,  12, 70, 0,  96, 20, 15, 0,  0,  72, 70, 50, 0,  64, 0,  40, 0,  0,  40, 0,  100, 6, 0, 0}},
    {"CRUSHER",   {2,  50, 6,  64, 1,  0,  64, 30, 90, 30, 0,  40, 0,  60, 110, 30, 0,  60, 0,  40, 0,  40, 50, 0,  95,  1, 0, 0}},
};
#define WT_NPRESET (sizeof WT_PRESET / sizeof WT_PRESET[0])
uint32_t dn_wt_preset_count(void) { return WT_NPRESET; }
const char *dn_wt_preset_name(uint32_t i) { return WT_PRESET[i % WT_NPRESET].name; }
void dn_wt_preset(uint32_t i, int16_t *p)
{
    uint32_t k;
    for (k = 0; k < WP_N; k++)
        p[k] = WT_PRESET[i % WT_NPRESET].p[k];
}

/* ---- voices ------------------------------------------------------------------------------------------------ */
void dn_wt_init(dn_wt_t *s)
{
    uint8_t *b = (uint8_t *)s;
    uint32_t i;
    for (i = 0; i < sizeof *s; i++)
        b[i] = 0;
    dn_wt_defaults(s->p);
}
void dn_wt_set_table(dn_wt_t *s, const uint8_t *t) { s->tab = t; }

static void wt_start(dn_wt_t *s, dn_wt_v_t *v, uint8_t note, uint8_t vel, int legato)
{
    uint32_t u;
    v->note = note, v->vel = vel, v->gate = 1, v->seq = ++s->seq;
    v->target = (float)note + (float)s->p[WP_TRANS];
    if (!legato || !v->live || s->p[WP_GLIDE] == 0)
        v->pitch = v->target;
    if (!legato || !v->live) {
        v->amp.st = 0, v->amp.lv = 0.0f;
        v->mod.st = 0, v->mod.lv = 0.0f;
        for (u = 0; u < DN_WT_NU; u++)              /* (unison: each its own start, so they beat) */
            v->ph[u] = u ? (s->seq * 0x9E3779B9u) ^ (u * 0x85EBCA6Bu) : 0u;
        v->sph = 0;
        v->ic1[0] = v->ic1[1] = v->ic2[0] = v->ic2[1] = 0.0f;
        v->volp = 0.0f, v->posp = -1.0f, v->f1p = -1.0f;
    } else {
        v->amp.st = v->amp.st > 1u ? 1u : v->amp.st;   /* (legato: the envelopes go on) */
        v->mod.st = v->mod.st > 1u ? 1u : v->mod.st;
    }
    v->live = 1;
}
/* the voices a note can take: VOICES, but no more than DN_WT_NOSC oscillators in all (UNISON 2: 4 voices, 3 or 4:
 * 2), the most the FM-1 can play beside the rest */
uint32_t dn_wt_voices(const dn_wt_t *s)
{
    uint32_t nv = s->p[WP_VOICES] < 1 ? 1u : (uint32_t)s->p[WP_VOICES], nu = s->p[WP_UNI] < 1 ? 1u : (uint32_t)s->p[WP_UNI];
    if (nv > DN_WT_NV)
        nv = DN_WT_NV;
    if (nu > DN_WT_NU)
        nu = DN_WT_NU;
    if (nv * nu > DN_WT_NOSC)
        nv = DN_WT_NOSC / nu;
    return nv ? nv : 1u;
}
void dn_wt_note_on(dn_wt_t *s, uint8_t note, uint8_t vel)
{
    uint32_t k, best = 0, nv = s->p[WP_VOICES] <= 1 ? 1u : dn_wt_voices(s);
    if (nv == 1u) {                                 /* mono: the one voice, legato under a held key */
        wt_start(s, &s->v[0], note, vel, s->nheld > 0u && s->v[0].gate);
        s->nheld++;
        return;
    }
    for (k = nv; k < DN_WT_NV; k++)                 /* (fewer now than before: those over the count let go) */
        if (s->v[k].live && s->v[k].gate)
            s->v[k].gate = 0, s->v[k].amp.st = 2, s->v[k].mod.st = 2;
    for (k = 0; k < nv; k++)                        /* a free one, else the one released longest, else the oldest */
        if (!s->v[k].live) {
            best = k;
            goto found;
        }
    for (k = 0, best = 0; k < nv; k++)
        if ((!s->v[k].gate && (s->v[best].gate || s->v[k].seq < s->v[best].seq)) ||
            (s->v[k].gate && s->v[best].gate && s->v[k].seq < s->v[best].seq))
            best = k;
found:
    wt_start(s, &s->v[best], note, vel, 0);
    s->nheld++;
}
void dn_wt_note_off(dn_wt_t *s, uint8_t note)
{
    uint32_t k;
    if (s->nheld)
        s->nheld--;
    for (k = 0; k < DN_WT_NV; k++)
        if (s->v[k].live && s->v[k].gate && s->v[k].note == note) {
            if (k == 0u && s->p[WP_VOICES] <= 1 && s->nheld)
                continue;                           /* (mono: another key still held) */
            s->v[k].gate = 0;
            s->v[k].amp.st = 2, s->v[k].mod.st = 2;
        }
}
void dn_wt_all_off(dn_wt_t *s)
{
    uint32_t k;
    s->nheld = 0;
    for (k = 0; k < DN_WT_NV; k++)
        if (s->v[k].live)
            s->v[k].gate = 0, s->v[k].amp.st = 2, s->v[k].mod.st = 2;
}
void dn_wt_cut(dn_wt_t *s)
{
    uint32_t k;
    s->nheld = 0;
    for (k = 0; k < DN_WT_NV; k++)
        s->v[k].live = 0, s->v[k].gate = 0;
}
uint32_t dn_wt_sounding(const dn_wt_t *s)
{
    uint32_t k, n = 0;
    for (k = 0; k < DN_WT_NV; k++)
        n += s->v[k].live;
    return n;
}

static void wt_env(dn_wt_env_t *e, int32_t a, int32_t d, int32_t sus, int32_t r)
{
    float s = (float)sus * (1.0f / 127.0f);
    if (e->st == 0u) {                              /* (towards a bit over the top, so it gets there) */
        e->lv += (1.25f - e->lv) * (a ? wt_k(a) : 1.0f);
        if (e->lv >= 1.0f)
            e->lv = 1.0f, e->st = 1;
    } else if (e->st == 1u) {
        e->lv += (s - e->lv) * wt_k(d);
    } else if (e->st == 2u) {
        e->lv -= e->lv * wt_k(r);
        if (e->lv < 0.0003f)
            e->lv = 0.0f, e->st = 3;
    }
}

/* the phase, warped (w: the mode; a: its amount, 0..127, turned into a1 / a2 at control rate) */
static inline uint32_t wt_warp(uint32_t q, uint32_t w, uint32_t a1, uint32_t a2, uint32_t bp)
{
    switch (w) {
    case WW_SYNC:                                   /* (the cycle read 1x .. 8x, restarting with the note's) */
        return (uint32_t)(((uint64_t)q * a1) >> 8);
    case WW_BEND:                                   /* (the middle of the cycle moved: one half squeezed) */
    case WW_PWM:
        if (q < bp)
            return (uint32_t)(((uint64_t)q * a1) >> 16);   /* a1 = 2^31 / bp, Q16 */
        return w == WW_PWM ? 0u : 0x80000000u + (uint32_t)(((uint64_t)(q - bp) * a2) >> 16);
    case WW_MIRROR:                                 /* (forward, then back) */
        return q < 0x80000000u ? q << 1 : (~q) << 1;
    case WW_FM:                                     /* (a sine at the note, its depth up to half a cycle) */
        return q + (uint32_t)(wt_sine(q) * (int32_t)a1);
    case WW_CRUSH:                                  /* (fewer places in the cycle) */
        return q & a1;
    default:
        return q;
    }
}

/* how much faster than the note a warp reads the table at most (the version read must allow for it, or it aliases) */
static float wt_speed(uint32_t w, uint32_t a1, uint32_t bp)
{
    switch (w) {
    case WW_SYNC: return (float)a1 * (1.0f / 256.0f);
    case WW_BEND: {
        float x = 2147483648.0f / (float)bp, y = 2147483648.0f / (float)(0u - bp);
        return x > y ? x : y;
    }
    case WW_PWM: return 4294967296.0f / (float)bp;
    case WW_MIRROR: return 2.0f;
    case WW_FM: return 1.0f + 6.2831853f * 32767.0f * (float)a1 * (1.0f / 4294967296.0f);
    default: return 1.0f;
    }
}
static void wt_tick(dn_wt_t *s, dn_wt_v_t *v, float *gl, float *gr, uint32_t *bp, uint32_t *a1, uint32_t *a2,
                    float *g, float *kres, float *pos)
{
    const int16_t *p = s->p;
    uint32_t u, nu = p[WP_UNI] < 1 ? 1u : p[WP_UNI] > (int16_t)DN_WT_NU ? DN_WT_NU : (uint32_t)p[WP_UNI];
    float det = (float)p[WP_DET] * (0.5f / 127.0f), spr = (float)p[WP_SPREAD] * (1.0f / 127.0f), pp, cut;
    int32_t ci;
    wt_env(&v->amp, p[WP_ATK], p[WP_DEC], p[WP_SUS], p[WP_REL]);
    wt_env(&v->mod, p[WP_MATK], p[WP_MDEC], p[WP_MSUS], p[WP_MREL]);
    if (v->amp.st == 3u) {
        v->live = 0;
        return;
    }
    if (p[WP_GLIDE])
        v->pitch += (v->target - v->pitch) * wt_k(p[WP_GLIDE]);
    else
        v->pitch = v->target;
    v->sinc = wt_inc(v->pitch - 12.0f);
    pp = (float)p[WP_POS] * (1.0f / 127.0f) + (float)p[WP_PENV] * (1.0f / 64.0f) * v->mod.lv +
         (float)p[WP_PLFO] * (1.0f / 127.0f) * s->lfo;
    *pos = pp < 0.0f ? 0.0f : pp > 1.0f ? 1.0f : pp;
    cut = (float)p[WP_CUT] + (float)p[WP_FENV] * 2.0f * v->mod.lv + (float)p[WP_CLFO] * 0.5f * s->lfo;
    ci = (int32_t)cut;
    ci = ci < 0 ? 0 : ci > 127 ? 127 : ci;
    *g = (float)DN_SVF_G[ci] * (1.0f / 4096.0f);
    *kres = 2.0f - 1.94f * (float)p[WP_RES] * (1.0f / 127.0f);
    switch (p[WP_WARP]) {
    case WW_SYNC: *a1 = 256u + (uint32_t)p[WP_WAMT] * 1792u / 127u; break;
    case WW_BEND:
    case WW_PWM: {
        uint32_t b = p[WP_WARP] == WW_PWM ? 0x80000000u - (uint32_t)p[WP_WAMT] * 0x0F80000u   /* (50 % .. 3 %) */
                                          : 0x80000000u + (uint32_t)((int32_t)p[WP_WAMT] - 64) * 0x00E00000u;
        *bp = b;                                    /* (the scales as Q16, by a float divide: no 64-bit one) */
        *a1 = (uint32_t)((p[WP_WARP] == WW_PWM ? 4294967296.0f : 2147483648.0f) / (float)b * 65536.0f);
        *a2 = (uint32_t)(2147483648.0f / (float)(0u - b) * 65536.0f);
        break;
    }
    case WW_FM: *a1 = (uint32_t)p[WP_WAMT] * 516u; break;   /* (Q15 sine x this: up to half a cycle) */
    case WW_CRUSH: *a1 = ~((1u << (24u + (uint32_t)p[WP_WAMT] * 6u / 127u)) - 1u); break;
    default: break;
    }
    {                                               /* each oscillator's pitch, and the version that can't alias at it
                                                     * (the warp's speed-up allowed for) */
        float sp = wt_speed((uint32_t)p[WP_WARP], *a1, *bp);
    for (u = 0; u < nu; u++) {
            float o = nu > 1u ? det * (2.0f * (float)u / (float)(nu - 1u) - 1.0f) : 0.0f;
            float pan = nu > 1u ? spr * (2.0f * (float)u / (float)(nu - 1u) - 1.0f) : 0.0f;   /* -1 L .. +1 R */
            uint32_t inc = wt_inc(v->pitch + o), allow = inc ? (uint32_t)(2147483648.0f / ((float)inc * sp)) : 128u, m = 0;
            while (m < 6u && (128u >> m) - 1u > allow)   /* (the version with no harmonic past Nyquist) */
                m++;
            v->inc[u] = inc, v->mip[u] = (uint8_t)m;
            gl[u] = (1.0f - pan) * 0.5f, gr[u] = (1.0f + pan) * 0.5f;
        }
    }
}

/* a frame pair's version at phase q, read 4-point (Hermite), mixed tf of the way from a to b */
static inline float wt_rd(const int16_t *pa, const int16_t *pb, float tf, uint32_t sh, uint32_t msk, uint32_t q)
{
    uint32_t j = q >> sh, j0 = (j - 1u) & msk, j2 = (j + 1u) & msk, j3 = (j + 2u) & msk;
    float x = (float)(q & ((1u << sh) - 1u)) * (1.0f / (float)(1u << sh)), ya, yb;
    {
        float x0 = pa[j0], x1 = pa[j], x2 = pa[j2], x3 = pa[j3];
        float c1 = 0.5f * (x2 - x0), c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3, c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
        ya = ((c3 * x + c2) * x + c1) * x + x1;
    }
    if (pb == pa)
        return ya;
    {
        float x0 = pb[j0], x1 = pb[j], x2 = pb[j2], x3 = pb[j3];
        float c1 = 0.5f * (x2 - x0), c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3, c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
        yb = ((c3 * x + c2) * x + c1) * x + x1;
    }
    return ya + (yb - ya) * tf;
}
/* (0.32.1) a step of height j in the signal at phase e, smoothed (PolyBLEP: the two samples either side of it), so
 * SYNC's restart and PWM's edges don't alias: what to add to the sample at phase ph (inv: 1 / the phase step) */
static inline float wt_blep(uint32_t ph, uint32_t e, uint32_t inc, float inv, float j)
{
    uint32_t d = ph - e;
    if (d < inc) {
        float t = (float)d * inv;
        return j * 0.5f * (t + t - t * t - 1.0f);
    }
    d = e - ph;
    if (d && d <= inc) {
        float t = -(float)d * inv;
        return j * 0.5f * (t * t + t + t + 1.0f);
    }
    return 0.0f;
}

void dn_wt_render(dn_wt_t *s, int32_t *lr, uint32_t n)
{
    uint32_t i0 = 0;
    const uint8_t *t = s->tab;
    uint32_t nf = dn_wt_frames(t);
    while (i0 < n) {
        uint32_t len = n - i0 < WT_TICK ? n - i0 : WT_TICK, k, busy = 0;
        float rl = 1.0f / (float)len;
        s->lfo_ph += DN_LFO_INC[s->p[WP_LRATE] < 0 ? 0 : s->p[WP_LRATE] > 127 ? 127 : s->p[WP_LRATE]];
        s->lfo = (float)wt_sine(s->lfo_ph) * (1.0f / 32768.0f);
        for (k = 0; k < DN_WT_NV; k++) {
            dn_wt_v_t *v = &s->v[k];
            float gl[DN_WT_NU], gr[DN_WT_NU], g = 0.5f, kr = 2.0f, pos = 0.0f, a1f, a2f, a3f, vol, drive, sub;
            float fp, tf0, tf1, f1, f2, f3, d1, d2, d3, vc, dv;
            uint32_t bp = 0x80000000u, a1 = 0, a2 = 0, u, i, w = (uint32_t)s->p[WP_WARP], nu, fa, fb;
            const uint8_t *fra, *frb;
            if (!v->live || !t || !nf)
                continue;
            wt_tick(s, v, gl, gr, &bp, &a1, &a2, &g, &kr, &pos);
            if (!v->live)
                continue;
            busy++;
            nu = s->p[WP_UNI] < 1 ? 1u : s->p[WP_UNI] > (int16_t)DN_WT_NU ? DN_WT_NU : (uint32_t)s->p[WP_UNI];
            /* the two frames either side of where the position was at the tick's start, and the way between them
             * ramped to where it is now (across a frame: held at the next one, which the next tick starts from) */
            if (v->posp < 0.0f)
                v->posp = pos;
            fp = v->posp * (float)(nf - 1u);
            fa = (uint32_t)fp;
            if (fa >= nf - 1u)
                fa = nf > 1u ? nf - 2u : 0u;
            fb = nf > 1u ? fa + 1u : fa;
            tf0 = fp - (float)fa;
            tf1 = pos * (float)(nf - 1u) - (float)fa;
            tf0 = tf0 < 0.0f ? 0.0f : tf0 > 1.0f ? 1.0f : tf0;
            tf1 = tf1 < 0.0f ? 0.0f : tf1 > 1.0f ? 1.0f : tf1;
            v->posp = pos;
            fra = t + DN_WT_HDR + fa * DN_WT_FRAME_BYTES, frb = t + DN_WT_HDR + fb * DN_WT_FRAME_BYTES;
            a1f = 1.0f / (1.0f + g * (g + kr)), a2f = g * a1f, a3f = g * a2f;
            if (v->f1p < 0.0f)
                v->f1p = a1f, v->f2p = a2f, v->f3p = a3f;
            f1 = v->f1p, f2 = v->f2p, f3 = v->f3p;      /* (the filter ramped too) */
            d1 = (a1f - f1) * rl, d2 = (a2f - f2) * rl, d3 = (a3f - f3) * rl;
            v->f1p = a1f, v->f2p = a2f, v->f3p = a3f;
            vol = v->amp.lv * (float)v->vel * (1.0f / 127.0f) * (float)s->p[WP_LEVEL] * (1.0f / 127.0f);
            vc = v->volp, dv = (vol - vc) * rl;          /* (the level ramped: the envelope moved in steps) */
            v->volp = vol;
            drive = 1.0f + (float)s->p[WP_DRIVE] * (6.0f / 127.0f);
            sub = (float)s->p[WP_SUB] * (1.0f / 127.0f);
            {
                float bl[WT_TICK], br[WT_TICK], dtf = (tf1 - tf0) * rl;
                int stereo = nu > 1u && s->p[WP_SPREAD] > 0;
                float osc = (1.0f / 32768.0f) / (float)nu * 1.1f;   /* (the oscillators' sum to about +-1) */
                int32_t cm = w == WW_CRUSH ? ~((1 << (4 + s->p[WP_WAMT] * 8 / 127)) - 1) : -1;
                for (i = 0; i < len; i++)
                    bl[i] = br[i] = 0.0f;
                for (u = 0; u < nu; u++) {          /* each oscillator over the tick */
                    uint32_t m = v->mip[u], sh = 23u + m, msk = (512u >> m) - 1u, ph = v->ph[u], inc = v->inc[u];
                    const int16_t *pa = (const int16_t *)(const void *)(fra + 2u * WT_MIPOFF[m]);
                    const int16_t *pb = (const int16_t *)(const void *)(frb + 2u * WT_MIPOFF[m]);
                    float cl = gl[u] * osc, cr = gr[u] * osc, tf = tf0, inv = inc ? 1.0f / (float)inc : 0.0f, j0 = 0.0f, j1 = 0.0f;
                    if (w == WW_SYNC) {             /* the restart's step: from where the cycle got to, back to its start */
                        float tm = 0.5f * (tf0 + tf1);
                        j0 = wt_rd(pa, pb, tm, sh, msk, 0u) - wt_rd(pa, pb, tm, sh, msk, (a1 & 255u) << 24);
                    } else if (w == WW_PWM) {       /* the gate: up at the cycle's start, down at the pulse's end */
                        j0 = wt_rd(pa, pb, 0.5f * (tf0 + tf1), sh, msk, 0u);
                        j1 = -j0;
                    }
                    for (i = 0; i < len; i++, ph += inc, tf += dtf) {
                        uint32_t q = w ? wt_warp(ph, w, a1, a2, bp) : ph;
                        float y = wt_rd(pa, pb, tf, sh, msk, q);
                        if (w == WW_PWM && ph >= bp)
                            y = 0.0f;
                        else if (w == WW_CRUSH)
                            y = (float)((int32_t)y & cm);
                        if (j0 != 0.0f) {
                            y += wt_blep(ph, 0u, inc, inv, j0);
                            if (j1 != 0.0f)
                                y += wt_blep(ph, bp, inc, inv, j1);
                        }
                        bl[i] += y * cl, br[i] += y * cr;
                    }
                    v->ph[u] = ph;
                }
                for (i = 0; i < len; i++, vc += dv, f1 += d1, f2 += d2, f3 += d3) {
                    float l, r, x = (float)wt_sine(v->sph) * (1.0f / 32768.0f) * sub * 0.7f;   /* the sub */
                    v->sph += v->sinc;
                    l = (bl[i] + x) * vc, r = (br[i] + x) * vc;
                    if (drive > 1.0f) {
                        float dg = 1.0f / (0.8f + 0.2f * drive);
                        l = wt_clip(l * drive) * dg, r = wt_clip(r * drive) * dg;
                    }
                    {                               /* the filter (a TPT state-variable low-pass): each side, or one for
                                                     * both when there's no spread */
                        float v3 = l - v->ic2[0], v1 = f1 * v->ic1[0] + f2 * v3, v2 = v->ic2[0] + f2 * v->ic1[0] + f3 * v3;
                        v->ic1[0] = 2.0f * v1 - v->ic1[0], v->ic2[0] = 2.0f * v2 - v->ic2[0], l = v2;
                        if (stereo) {
                            v3 = r - v->ic2[1], v1 = f1 * v->ic1[1] + f2 * v3, v2 = v->ic2[1] + f2 * v->ic1[1] + f3 * v3;
                            v->ic1[1] = 2.0f * v1 - v->ic1[1], v->ic2[1] = 2.0f * v2 - v->ic2[1], r = v2;
                        } else {
                            r = l;
                        }
                    }
                    lr[2u * (i0 + i)] += (int32_t)(l * 32767.0f);
                    lr[2u * (i0 + i) + 1u] += (int32_t)(r * 32767.0f);
                }
            }
        }
        s->busy = busy;
        i0 += len;
    }
}
