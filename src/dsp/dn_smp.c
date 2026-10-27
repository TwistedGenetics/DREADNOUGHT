/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: SAMPLER (dn_smp.h says what it does). Copyright (C) 2026 Jamie (TwistedGenetics).
 * The IMA ADPCM step is dn_ima.h's (DREADNOUGHT's own, from the 1992 IMA recommendation): low nibble first. */
#include "dn_smp.h"
#include "dn_dsp.h"
#include "dn_ima.h"
#include "dsp/fastmath.h"

#define SM_FS 44100.f
#define SM_CTL 32u                                  /* the envelopes: once every 32 samples, the amp ramped between */
#define SM_FULL (1 << 24)

#define sm_dec dn_ima

uint32_t dn_smp_checkpoints(const uint8_t *adpcm, uint32_t n, uint32_t *ck, uint32_t max)
{
    int32_t pred = 0, idx = 0;
    uint32_t i, c = 0;
    for (i = 0; i < n; i++) {
        if (!(i & 255u)) {
            if (c >= max)
                break;
            ck[c++] = (uint32_t)(pred & 0xFFFF) | (uint32_t)idx << 16;
        }
        sm_dec(adpcm, i, &pred, &idx);
    }
    return c;
}

/* the decoder's state before sample at (from its checkpoint, at most 255 samples decoded) */
static void sm_seek(const dn_smp_src_t *s, uint32_t at, int32_t *pred, int32_t *idx)
{
    uint32_t b = at >> 8, i;
    if (b >= s->nck)
        b = s->nck ? s->nck - 1u : 0u;
    *pred = s->nck ? (int16_t)(s->ck[b] & 0xFFFFu) : 0;
    *idx = s->nck ? (int32_t)(s->ck[b] >> 16) : 0;
    for (i = b << 8; i < at; i++)
        sm_dec(s->adpcm, i, pred, idx);
}
static uint32_t sm_playable(const dn_smp_src_t *s)  /* the samples the checkpoints reach */
{
    uint32_t m = s->nck << 8;
    return s->n < m ? s->n : m;
}

/* ---- the values ------------------------------------------------------------------------------------------------- */
static const char *const PD_NAME[PD_N] = {"SAMPLE", "START", "END", "REVERSE", "PITCH", "FINE", "LEVEL", "PAN",
                                          "ATTACK", "HOLD", "DECAY", "CHOKE", "CUT", "RES", "DRIVE", "CRUSH"};
static const int16_t PD_MIN[PD_N] = {-1, 0, 0, 0, -24, -50, 0, -64, 0, 0, 0, 0, 0, 0, 0, 0};
static const int16_t PD_MAX[PD_N] = {126, 1000, 1000, 1, 24, 50, 127, 63, 127, 127, 127, 4, 127, 127, 127, 127};
static const char *const ZN_NAME[ZN_N] = {"SAMPLE", "ROOT", "LOW", "HIGH", "TUNE", "LEVEL", "START", "LOOP",
                                          "L.START", "L.END"};
static const int16_t ZN_MIN[ZN_N] = {-1, 0, 0, 0, -50, 0, 0, 0, 0, 0};
static const int16_t ZN_MAX[ZN_N] = {126, 127, 127, 127, 50, 127, 1000, 2, 1000, 1000};
static const char *const KY_NAME[KY_N] = {"ATTACK", "DECAY", "SUSTAIN", "RELEASE", "CUT", "RES", "DRIVE", "CRUSH",
                                          "VOICES"};
static const int16_t KY_MIN[KY_N] = {0, 0, 0, 0, 0, 0, 0, 0, 1};
static const int16_t KY_MAX[KY_N] = {127, 127, 127, 127, 127, 127, 127, 127, 4};
const char *dn_smp_pd_name(uint32_t k) { return k < PD_N ? PD_NAME[k] : ""; }
int32_t dn_smp_pd_min(uint32_t k) { return k < PD_N ? PD_MIN[k] : 0; }
int32_t dn_smp_pd_max(uint32_t k) { return k < PD_N ? PD_MAX[k] : 0; }
const char *dn_smp_zn_name(uint32_t k) { return k < ZN_N ? ZN_NAME[k] : ""; }
int32_t dn_smp_zn_min(uint32_t k) { return k < ZN_N ? ZN_MIN[k] : 0; }
int32_t dn_smp_zn_max(uint32_t k) { return k < ZN_N ? ZN_MAX[k] : 0; }
const char *dn_smp_ky_name(uint32_t k) { return k < KY_N ? KY_NAME[k] : ""; }
int32_t dn_smp_ky_min(uint32_t k) { return k < KY_N ? KY_MIN[k] : 0; }
int32_t dn_smp_ky_max(uint32_t k) { return k < KY_N ? KY_MAX[k] : 0; }

static void sm_str(char out[8], const char *w)
{
    uint32_t i;
    for (i = 0; w[i] && i < 7u; i++)
        out[i] = w[i];
    out[i] = 0;
}
static void sm_num(char out[8], int32_t v, int plus, const char *unit)
{
    char t[12];
    uint32_t n = 0, i = 0, u = v < 0 ? (uint32_t)-v : (uint32_t)v;
    do
        t[n++] = (char)('0' + u % 10u), u /= 10u;
    while (u && n < 10u);
    if (v < 0)
        out[i++] = '-';
    else if (plus && v > 0)
        out[i++] = '+';
    while (n && i < 7u)
        out[i++] = t[--n];
    while (*unit && i < 7u)
        out[i++] = *unit++;
    out[i] = 0;
}
static void sm_note(char out[8], int32_t v)
{
    static const char *const N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    char o[8];
    sm_str(out, N[v % 12]);
    sm_num(o, v / 12 - 2, 0, "");                   /* (C3 = 60, as the FM-1's keys name it) */
    if (out[1] == '#')
        out[2] = o[0], out[3] = o[1], out[4] = 0;
    else
        out[1] = o[0], out[2] = o[1], out[3] = 0;
}
static void sm_permille(char out[8], int32_t v)     /* 0..1000 as a percentage with a tenth */
{
    char t[8];
    uint32_t i = 0, j = 0;
    sm_num(t, v / 10, 0, "");
    while (t[j] && i < 4u)
        out[i++] = t[j++];
    out[i++] = '.';
    out[i++] = (char)('0' + v % 10);
    out[i++] = '%';
    out[i] = 0;
}
void dn_smp_text(uint32_t kind, uint32_t k, int32_t v, char out[8])
{
    if (kind == 0u) {
        switch (k) {
        case PD_START: case PD_END: sm_permille(out, v); return;
        case PD_REV: sm_str(out, v ? "ON" : "OFF"); return;
        case PD_PITCH: sm_num(out, v, 1, ""); return;
        case PD_FINE: sm_num(out, v, 1, "c"); return;
        case PD_PAN:
            if (!v)
                sm_str(out, "C");
            else {
                char t[8];
                uint32_t j;
                sm_num(t, v < 0 ? -v : v, 0, "");
                out[0] = v < 0 ? 'L' : 'R';
                for (j = 0; t[j] && j < 6u; j++)
                    out[1u + j] = t[j];
                out[1u + j] = 0;
            }
            return;
        case PD_HOLD: if (v >= 127) { sm_str(out, "END"); return; } break;
        case PD_CHOKE: if (!v) { sm_str(out, "OFF"); return; } break;
        case PD_CUT: if (v >= 127) { sm_str(out, "OPEN"); return; } break;
        default: break;
        }
    } else if (kind == 1u) {
        switch (k) {
        case ZN_ROOT: case ZN_LOW: case ZN_HIGH: sm_note(out, v); return;
        case ZN_TUNE: sm_num(out, v, 1, "c"); return;
        case ZN_START: case ZN_LSTART: case ZN_LEND: sm_permille(out, v); return;
        case ZN_LOOP: sm_str(out, v == DN_SMP_LLOOP ? "LOOP" : v == DN_SMP_LSUST ? "SUST" : "OFF"); return;
        default: break;
        }
    } else if (k == KY_CUT && v >= 127) {
        sm_str(out, "OPEN");
        return;
    } else if (k == KY_VOICES && v == 1) {
        sm_str(out, "MONO");
        return;
    }
    sm_num(out, v, 0, "");
}

void dn_smp_pad_default(int16_t *pad)
{
    static const int16_t D[PD_N] = {-1, 0, 1000, 0, 0, 0, 100, 0, 0, 127, 64, 0, 127, 0, 0, 0};
    uint32_t k;
    for (k = 0; k < PD_N; k++)
        pad[k] = D[k];
}
void dn_smp_zone_default(int16_t *zone, uint32_t j)
{
    (void)j;
    zone[ZN_SAMPLE] = -1, zone[ZN_ROOT] = 60, zone[ZN_LOW] = 0, zone[ZN_HIGH] = 127, zone[ZN_TUNE] = 0;
    zone[ZN_LEVEL] = 100, zone[ZN_START] = 0, zone[ZN_LOOP] = DN_SMP_LOFF, zone[ZN_LSTART] = 0, zone[ZN_LEND] = 1000;
}

void dn_smp_init(dn_smp_t *s)
{
    uint32_t i;
    uint8_t *b = (uint8_t *)s;
    for (i = 0; i < sizeof *s; i++)
        b[i] = 0;
    for (i = 0; i < DN_SMP_PADS; i++)
        dn_smp_pad_default(s->p.pad[i]);
    for (i = 0; i < DN_SMP_ZONES; i++)
        dn_smp_zone_default(s->p.zone[i], i);
    s->p.key[KY_ATK] = 0, s->p.key[KY_DEC] = 64, s->p.key[KY_SUS] = 127, s->p.key[KY_REL] = 30;
    s->p.key[KY_CUT] = 127, s->p.key[KY_VOICES] = 4;
}

/* the rising zero crossing nearest `at` within +-range (at if none) */
static uint32_t sm_zero(const dn_smp_src_t *s, uint32_t at, uint32_t range, uint32_t lim)
{
    uint32_t from = at > range ? at - range : 0u, to = at + range < lim ? at + range : lim, i, best = at;
    uint32_t bd = 0xFFFFFFFFu;
    int32_t pred, idx, prev = 0, cur;
    sm_seek(s, from, &pred, &idx);
    for (i = from; i < to; i++) {
        cur = sm_dec(s->adpcm, i, &pred, &idx);
        if (i > from && prev < 0 && cur >= 0) {
            uint32_t d = i > at ? i - at : at - i;
            if (d < bd)
                bd = d, best = i;
        }
        prev = cur;
    }
    return best;
}
void dn_smp_zone_prepare(dn_smp_t *s, uint32_t j)
{
    dn_smp_src_t *r = &s->src[DN_SMP_PADS + j];
    const int16_t *z = s->p.zone[j];
    uint32_t n, a, b;
    if (!r->adpcm || !r->nck) {
        r->lst = r->lend = 0;
        return;
    }
    n = sm_playable(r);
    a = (uint32_t)dn_clamp(z[ZN_LSTART], 0, 1000) * (n / 1000u) + (uint32_t)dn_clamp(z[ZN_LSTART], 0, 1000) * (n % 1000u) / 1000u;
    b = (uint32_t)dn_clamp(z[ZN_LEND], 0, 1000) * (n / 1000u) + (uint32_t)dn_clamp(z[ZN_LEND], 0, 1000) * (n % 1000u) / 1000u;
    if (b > n - 1u)
        b = n - 1u;
    if (b < a + 64u)                                /* (a loop of at least 64 samples) */
        a = b > 64u ? b - 64u : 0u;
    a = sm_zero(r, a, 256u, n), b = sm_zero(r, b, 256u, n);
    if (b <= a + 32u)
        b = a + 64u < n ? a + 64u : n - 1u;
    r->lst = a, r->lend = b;
    sm_seek(r, a, &r->lpred, &r->lidx);
}

/* ---- voices ---------------------------------------------------------------------------------------------------- */
static void sm_kill(dn_smp_t *s, dn_smp_v_t *v)     /* out now: its last value fades in the residue (no click) */
{
    if (!v->on)
        return;
    s->res_l += (float)v->lastl;
    s->res_r += (float)v->lastr;
    v->on = 0;
}
static dn_smp_v_t *sm_voice(dn_smp_t *s)            /* a free voice, or the oldest */
{
    dn_smp_v_t *o = &s->v[0];
    uint32_t i;
    for (i = 0; i < DN_SMP_VOICES; i++)
        if (!s->v[i].on)
            return &s->v[i];
    for (i = 1; i < DN_SMP_VOICES; i++)
        if (s->v[i].age < o->age)
            o = &s->v[i];
    sm_kill(s, o);
    return o;
}
static uint32_t sm_at(uint32_t n, int32_t pm)       /* a place in permille of n samples */
{
    uint32_t p = (uint32_t)dn_clamp(pm, 0, 1000);
    return p * (n / 1000u) + p * (n % 1000u) / 1000u;
}
static void sm_filter(dn_smp_v_t *v, int32_t cut, int32_t res, int32_t drive, int32_t crush)
{
    v->filt = cut < 127;
    if (v->filt) {
        float fc = 30.f * fm_powf(533.33f, (float)cut * (1.f / 127.f)), g, k;
        if (fc > 18000.f)
            fc = 18000.f;
        g = fm_tanf(3.14159265f * fc / SM_FS);
        k = 2.f - 1.95f * (float)dn_clamp(res, 0, 127) * (1.f / 127.f);
        v->fk = k;
        v->fa1 = 1.f / (1.f + g * (g + k));
        v->fa2 = g * v->fa1;
        v->fa3 = g * v->fa2;
    }
    v->ic1 = v->ic2 = 0.f;
    v->drv = drive > 0 ? 1.f + (float)drive * (1.f / 16.f) : 0.f;
    v->crush = (uint8_t)dn_clamp(crush, 0, 127);
    v->cph = 0, v->held = 0;
}
static void sm_pan(dn_smp_v_t *v, int32_t pan, float gain)
{
    float a = (float)(dn_clamp(pan, -64, 64) + 64) * (1.5707963f / 128.f);
    v->gl = fm_cosf(a) * 1.4142136f * gain;
    v->gr = fm_sinf(a) * 1.4142136f * gain;
}
/* a voice of src r from sample `from` (forward up to end), or reversed from end - 1 down to `from` */
static void sm_start(dn_smp_t *s, dn_smp_v_t *v, const dn_smp_src_t *r, uint32_t from, uint32_t end, int rev,
                     float semis)
{
    v->s = r;
    v->from = from, v->end = end, v->rev = (uint8_t)rev;
    v->inc = (uint32_t)((float)r->rate * (1.f / SM_FS) * fm_exp2f(semis * (1.f / 12.f)) * 65536.f);
    if (!v->inc)
        v->inc = 1;
    v->frac = 0;
    if (rev) {
        v->ipos = end - 1u;
        v->rbase = 0xFFFFFFFFu;                     /* (the window loads on the first read) */
    } else {
        sm_seek(r, from, &v->pred, &v->dix);
        v->pos = from;
        v->ipos = from;
        v->a = sm_dec(r->adpcm, v->pos++, &v->pred, &v->dix);
        v->b = v->pos < end ? sm_dec(r->adpcm, v->pos++, &v->pred, &v->dix) : 0;
    }
    v->env = 0, v->stage = 1, v->ctl = 0, v->amp_now = v->amp_next = 0;
    v->lastl = v->lastr = 0;
    v->hfx.type = DN_HFX_OFF, v->hfx.dr16 = 0, v->jd = 0;
    v->age = ++s->age;
    v->on = 1;
}

void dn_smp_hfx(dn_smp_t *s, uint32_t type, uint32_t bpm) { dn_hfx_setup(&s->nfx, type, bpm, 44100u); }

void dn_smp_pad(dn_smp_t *s, uint32_t pad, uint32_t vel)
{
    const int16_t *p;
    const dn_smp_src_t *r;
    dn_smp_v_t *v;
    uint32_t i, n, a, b, choke;
    dn_hfx_t fx = s->nfx;                           /* (0.39) its own FX: this hit's, not the next's */
    s->nfx.type = DN_HFX_OFF, s->nfx.dr16 = 0;
    if (pad >= DN_SMP_PADS)
        return;
    s->last_pad = (uint8_t)pad;
    p = s->p.pad[pad];
    r = &s->src[pad];
    if (p[PD_SAMPLE] < 0 || !r->adpcm || !r->nck)
        return;
    choke = (uint32_t)dn_clamp(p[PD_CHOKE], 0, 4);
    for (i = 0; i < DN_SMP_VOICES; i++) {           /* the pad's own last hit, and its choke group */
        dn_smp_v_t *o = &s->v[i];
        if (o->on && !o->kind && (o->idx == pad || (choke && s->p.pad[o->idx][PD_CHOKE] == (int16_t)choke)))
            sm_kill(s, o);
    }
    n = sm_playable(r);
    a = sm_at(n, p[PD_START]), b = sm_at(n, p[PD_END]);
    if (b > n)
        b = n;
    if (b < a + 2u)
        return;
    v = sm_voice(s);
    v->kind = 0, v->idx = (uint8_t)pad, v->note = 0, v->gate = 1, v->loop = 0;
    sm_start(s, v, r, a, b, p[PD_REV] != 0, (float)p[PD_PITCH] + (float)p[PD_FINE] * 0.01f);
    v->hfx = fx;
    v->hfx.start = a;
    v->ka = p[PD_ATK] ? DN_ENV_K[dn_clamp(p[PD_ATK], 0, 127)] : 0u;
    v->kd = DN_ENV_K[dn_clamp(p[PD_DEC], 0, 127)];
    v->hold = p[PD_HOLD] >= 127 ? 0xFFFFFFFFu
                                : (uint32_t)((float)(p[PD_HOLD] * p[PD_HOLD]) * (2.f * SM_FS / (126.f * 126.f)));
    v->sus = 0, v->kr = v->kd;
    sm_filter(v, p[PD_CUT], p[PD_RES], p[PD_DRIVE], p[PD_CRUSH]);
    sm_pan(v, p[PD_PAN], (float)dn_clamp(p[PD_LEVEL], 0, 127) * 0.01f * (float)(vel ? vel : 1u) * (1.f / 127.f));
}

int dn_smp_zone_of(const dn_smp_t *s, uint32_t note)
{
    uint32_t j;
    for (j = 0; j < DN_SMP_ZONES; j++) {
        const int16_t *z = s->p.zone[j];
        if (z[ZN_SAMPLE] >= 0 && s->src[DN_SMP_PADS + j].adpcm && s->src[DN_SMP_PADS + j].nck &&
            (int32_t)note >= z[ZN_LOW] && (int32_t)note <= z[ZN_HIGH])
            return (int)j;
    }
    return -1;
}

void dn_smp_note_on(dn_smp_t *s, uint32_t note, uint32_t vel)
{
    int zj = dn_smp_zone_of(s, note);
    const int16_t *z, *k = s->p.key;
    const dn_smp_src_t *r;
    dn_smp_v_t *v = 0;
    uint32_t i, n, a, b, nv = 0, lim = (uint32_t)dn_clamp(k[KY_VOICES], 1, (int32_t)DN_SMP_VOICES);
    if (zj < 0)
        return;
    s->last_zone = (uint8_t)zj;
    z = s->p.zone[zj];
    r = &s->src[DN_SMP_PADS + (uint32_t)zj];
    for (i = 0; i < DN_SMP_VOICES; i++)             /* the keys' voices: at the limit, the oldest goes */
        if (s->v[i].on && s->v[i].kind)
            nv++;
    while (nv >= lim) {
        dn_smp_v_t *o = 0;
        for (i = 0; i < DN_SMP_VOICES; i++)
            if (s->v[i].on && s->v[i].kind && (!o || s->v[i].age < o->age))
                o = &s->v[i];
        if (!o)
            break;
        sm_kill(s, o);
        nv--;
    }
    n = sm_playable(r);
    a = sm_at(n, z[ZN_START]), b = n;
    if (b < a + 2u)
        return;
    v = sm_voice(s);
    v->kind = 1, v->idx = (uint8_t)zj, v->note = (uint8_t)note, v->gate = 1;
    v->loop = (uint8_t)(z[ZN_LOOP] && r->lend > r->lst && r->lst >= a ? z[ZN_LOOP] : DN_SMP_LOFF);
    sm_start(s, v, r, a, b, 0, (float)((int32_t)note - z[ZN_ROOT]) + (float)z[ZN_TUNE] * 0.01f);
    v->ka = k[KY_ATK] ? DN_ENV_K[dn_clamp(k[KY_ATK], 0, 127)] : 0u;
    v->kd = DN_ENV_K[dn_clamp(k[KY_DEC], 0, 127)];
    v->kr = DN_ENV_K[dn_clamp(k[KY_REL], 0, 127)];
    v->sus = dn_clamp(k[KY_SUS], 0, 127) * (SM_FULL / 127);
    v->hold = 0;
    sm_filter(v, k[KY_CUT], k[KY_RES], k[KY_DRIVE], k[KY_CRUSH]);
    sm_pan(v, 0, (float)dn_clamp(z[ZN_LEVEL], 0, 127) * 0.01f * (float)(vel ? vel : 1u) * (1.f / 127.f));
}
void dn_smp_note_off(dn_smp_t *s, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < DN_SMP_VOICES; i++) {
        dn_smp_v_t *v = &s->v[i];
        if (v->on && v->kind && v->note == note && v->gate) {
            v->gate = 0;
            v->stage = 4;
        }
    }
}
void dn_smp_cut(dn_smp_t *s)
{
    uint32_t i;
    for (i = 0; i < DN_SMP_VOICES; i++)
        sm_kill(s, &s->v[i]);
}

/* the envelope, once a control tick */
static void sm_control(dn_smp_t *s, dn_smp_v_t *v)
{
    switch (v->stage) {
    case 1:
        if (!v->ka)
            v->env = SM_FULL;
        else
            v->env += dn_mulq16((SM_FULL + SM_FULL / 4) - v->env, v->ka);
        if (v->env >= SM_FULL)
            v->env = SM_FULL, v->stage = v->kind ? 3 : 2;
        break;
    case 2:                                         /* (a pad's HOLD) */
        if (v->hold != 0xFFFFFFFFu) {
            if (v->hold <= SM_CTL)
                v->stage = 3;
            else
                v->hold -= SM_CTL;
        }
        break;
    case 3:
        if (v->kind)
            v->env += dn_mulq16(v->sus - v->env, v->kd);
        else
            v->env -= dn_mulq16(v->env, v->kd);
        break;
    case 4:
        v->env -= dn_mulq16(v->env, v->kr);
        break;
    default:
        break;
    }
    v->amp_now = v->amp_next;
    v->amp_next = v->env >> 9;                      /* Q15 */
    if ((v->stage == 4 || (v->stage == 3 && !v->kind)) && v->env < SM_FULL / 4096) {
        v->amp_next = 0;
        if (v->amp_now == 0)
            sm_kill(s, v);
    }
}

/* the reversed voice's sample i, from its window (decoded from the checkpoint below it when i leaves it) */
static inline int32_t sm_rev_at(dn_smp_v_t *v, uint32_t i)
{
    if (v->rbase == 0xFFFFFFFFu || i < v->rbase || i >= v->rbase + DN_SMP_RB) {
        const dn_smp_src_t *r = v->s;
        uint32_t base = i + 1u > DN_SMP_RB ? ((i + 1u - DN_SMP_RB + 255u) & ~255u) : 0u, k, lim = sm_playable(r);
        int32_t pred, idx;
        sm_seek(r, base, &pred, &idx);
        for (k = 0; k < DN_SMP_RB; k++)
            v->rb[k] = (int16_t)(base + k < lim ? sm_dec(r->adpcm, base + k, &pred, &idx) : 0);
        v->rbase = base;
    }
    return v->rb[i - v->rbase];
}

/* (0.39) a pad's hit FX, an output sample on (x0: the sample just played): a cycle begins where the time has got to;
 * 1: the voice has ended */
static int sm_hfx(dn_smp_t *s, dn_smp_v_t *v, int32_t x0)
{
    const dn_smp_src_t *r = v->s;
    int k = dn_hfx_tick(&v->hfx, v->inc);
    uint32_t d;
    if (k == 2) {                                   /* (TAPESTOP: stopped) */
        sm_kill(s, v);
        return 1;
    }
    if (k != 1)
        return 0;
    d = v->hfx.t16 >> 16;
    if (v->rev) {
        if (v->end < v->from + 2u + d) {
            sm_kill(s, v);
            return 1;
        }
        v->ipos = v->end - 1u - d;
        v->frac = 0;
        v->jd += x0 - sm_rev_at(v, v->ipos);
        return 0;
    }
    d += v->from;
    if (d + 2u >= v->end) {
        sm_kill(s, v);
        return 1;
    }
    sm_seek(r, d, &v->pred, &v->dix);
    v->pos = d;
    v->ipos = d;
    v->a = sm_dec(r->adpcm, v->pos++, &v->pred, &v->dix);
    v->b = sm_dec(r->adpcm, v->pos++, &v->pred, &v->dix);
    v->frac = 0;
    v->jd += x0 - v->a;
    return 0;
}

static void sm_voice_render(dn_smp_t *s, dn_smp_v_t *v, int32_t *lr, uint32_t n)
{
    const dn_smp_src_t *r = v->s;
    uint32_t i, done = 0;
    for (i = 0; i < n && v->on; i++) {
        int32_t x, x0, amp;
        float y;
        if (!v->ctl) {
            sm_control(s, v);
            v->ctl = SM_CTL;
            if (!v->on)
                break;
        }
        amp = v->amp_now + (((v->amp_next - v->amp_now) * (int32_t)(SM_CTL - v->ctl)) >> 5);
        v->ctl--;
        /* the sample, read between two */
        if (v->rev) {
            int32_t a = sm_rev_at(v, v->ipos), b = v->ipos > v->from ? sm_rev_at(v, v->ipos - 1u) : 0;
            x = a + (int32_t)(((int64_t)(b - a) * v->frac) >> 16);
        } else {
            x = v->a + (int32_t)(((int64_t)(v->b - v->a) * v->frac) >> 16);
        }
        x0 = x;
        if (v->jd) {                                /* (0.39) a hit FX's jump: the step it made, fading (~1.5 ms) */
            x += v->jd;
            v->jd -= v->jd >= 0 ? (v->jd + 63) >> 6 : -((63 - v->jd) >> 6);
        }
        y = (float)x;
        if (v->filt) {                              /* the low-pass (a TPT state-variable filter) */
            float v3 = y - v->ic2, v1 = v->fa1 * v->ic1 + v->fa2 * v3, v2 = v->ic2 + v->fa2 * v->ic1 + v->fa3 * v3;
            v->ic1 = 2.f * v1 - v->ic1;
            v->ic2 = 2.f * v2 - v->ic2;
            y = v2;
        }
        if (v->drv > 0.f)
            y = fm_tanhf(y * (1.f / 32768.f) * v->drv) * 32768.f;
        x = (int32_t)y;
        if (v->crush) {                             /* the rate down (hold), then the bits */
            v->cph += 65536u / (1u + v->crush / 8u);
            if (v->cph >= 65536u)
                v->cph -= 65536u, v->held = x;
            x = v->held & ~((1 << (v->crush * 11 / 127)) - 1);
        }
        x = (int32_t)(((int64_t)x * amp) >> 15);
        v->lastl = (int32_t)((float)x * v->gl);
        v->lastr = (int32_t)((float)x * v->gr);
        lr[2u * i] += v->lastl;
        lr[2u * i + 1u] += v->lastr;
        done = i + 1u;
        /* on */
        v->frac += dn_hfx_inc(&v->hfx, v->inc);
        if (v->hfx.type && sm_hfx(s, v, x0))
            break;
        while (v->frac >= 65536u && v->on) {
            v->frac -= 65536u;
            if (v->rev) {
                if (v->ipos <= v->from + 1u) {
                    sm_kill(s, v);
                    break;
                }
                v->ipos--;
                continue;
            }
            v->ipos++;
            v->a = v->b;
            if (v->loop && (v->loop == DN_SMP_LLOOP || v->gate) && v->ipos >= r->lend) {   /* round */
                int32_t pred = r->lpred, idx = r->lidx;
                v->ipos = r->lst;
                v->pos = r->lst;
                v->a = sm_dec(r->adpcm, v->pos++, &pred, &idx);
                v->b = sm_dec(r->adpcm, v->pos++, &pred, &idx);
                v->pred = pred, v->dix = idx;
                continue;
            }
            if (v->ipos + 1u >= v->end) {           /* at its END */
                sm_kill(s, v);
                break;
            }
            v->b = v->pos < v->end ? sm_dec(r->adpcm, v->pos++, &v->pred, &v->dix) : 0;
        }
    }
    if (!v->on && done < n) {                       /* (0.39) it ended inside the block: what it left fades from
                                                     * there, not from the block's start (sm_kill put it in res) */
        float l = (float)v->lastl, rr = (float)v->lastr;
        s->res_l -= l, s->res_r -= rr;
        for (i = done; i < n; i++) {
            lr[2u * i] += (int32_t)l;
            lr[2u * i + 1u] += (int32_t)rr;
            l *= 0.96f, rr *= 0.96f;
        }
        s->resn_l += l, s->resn_r += rr;
    }
    if (v->on) {                                    /* (no denormals) */
        if (v->ic1 < 1e-6f && v->ic1 > -1e-6f)
            v->ic1 = 0.f;
        if (v->ic2 < 1e-6f && v->ic2 > -1e-6f)
            v->ic2 = 0.f;
    }
}

void dn_smp_render(dn_smp_t *s, int32_t *lr, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < DN_SMP_VOICES; i++)
        if (s->v[i].on)
            sm_voice_render(s, &s->v[i], lr, n);
    if (s->res_l != 0.f || s->res_r != 0.f) {       /* what cut voices left, fading over ~1 ms */
        float l = s->res_l, r = s->res_r;
        for (i = 0; i < n; i++) {
            lr[2u * i] += (int32_t)l;
            lr[2u * i + 1u] += (int32_t)r;
            l *= 0.96f, r *= 0.96f;
        }
        s->res_l = l < 0.5f && l > -0.5f ? 0.f : l;
        s->res_r = r < 0.5f && r > -0.5f ? 0.f : r;
    }
    s->res_l += s->resn_l, s->res_r += s->resn_r;
    s->resn_l = s->resn_r = 0.f;
}
