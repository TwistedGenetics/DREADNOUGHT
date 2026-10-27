/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: FM (0.25), the six-operator DX7-style synth. Copyright (C) 2026 Jamie (TwistedGenetics).
 *
 * Ported to C from Dexed's MSFA (env.cc, dx7note.cc, fm_core.cc, fm_op_kernel.cc, lfo.cc, pitchenv.cc, sin.h, exp2.h,
 * freqlut.cc): Copyright 2012 Google Inc., 2013-2017 Pascal Gauthier and the Dexed authors, Apache License 2.0. The
 * maths is theirs, kept as it was (the same integer steps, so the voices sound as they do in Dexed); what changed:
 * C, no tuning tables or MTS, no portamento, no pitch bend / mod wheel, tables made at build time, float in place of
 * double at note-on, a voice freed when its carriers have gone silent, last-note mono. */
#include "dn_fm.h"
#include "dn_tables.h"
#include "dn_math.h"

/* ---- the lookups (sin.h, exp2.h, freqlut.cc) ---------------------------------------------------------------- */
static inline int32_t fm_sin(int32_t phase)
{
    int32_t lowbits = phase & ((1 << 14) - 1);
    int32_t ix = (phase >> 13) & (1023 << 1);
    return DN_FM_SIN[ix + 1] + (int32_t)(((int64_t)DN_FM_SIN[ix] * (int64_t)lowbits) >> 14);
}
static inline int32_t fm_exp2(int32_t x)
{
    int32_t lowbits = x & ((1 << 14) - 1);
    int32_t ix = (x >> 13) & (1023 << 1);
    int32_t y = DN_FM_EXP2[ix + 1] + (int32_t)(((int64_t)DN_FM_EXP2[ix] * (int64_t)lowbits) >> 14);
    return y >> (6 - (x >> 24));
}
static inline int32_t fm_freq(int32_t logfreq)
{
    int32_t ix = (logfreq & 0xffffff) >> 14;
    int32_t y0 = DN_FM_FREQ[ix], y1 = DN_FM_FREQ[ix + 1];
    int32_t lowbits = logfreq & ((1 << 14) - 1);
    int32_t y = y0 + (int32_t)(((int64_t)(y1 - y0) * (int64_t)lowbits) >> 14);
    return y >> (20 - (logfreq >> 24));
}

/* ---- the envelope (env.cc, ACCURATE_ENVELOPE) --------------------------------------------------------------- */
static const uint8_t FM_LEVELLUT[20] = {0, 5, 9, 13, 17, 20, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 42, 43, 45, 46};
static const int32_t FM_STATICS[77] = {
    1764000, 1764000, 1411200, 1411200, 1190700, 1014300, 992250, 882000, 705600, 705600, 584325, 507150, 502740,
    441000, 418950, 352800, 308700, 286650, 253575, 220500, 220500, 176400, 145530, 145530, 125685, 110250, 110250,
    88200, 88200, 74970, 61740, 61740, 55125, 48510, 44100, 37485, 31311, 30870, 27562, 27562, 22050, 18522, 17640,
    15435, 14112, 13230, 11025, 9261, 9261, 7717, 6615, 6615, 5512, 5512, 4410, 3969, 3969, 3439, 2866, 2690, 2249,
    1984, 1896, 1808, 1411, 1367, 1234, 1146, 926, 837, 837, 705, 573, 573, 529, 441, 441};
static int32_t fm_scaleout(int32_t ol) { return ol >= 20 ? 28 + ol : FM_LEVELLUT[ol < 0 ? 0 : ol]; }
static void env_advance(dn_fm_env_t *e, uint32_t ix)
{
    e->ix = (uint8_t)ix;
    if (ix < 4u) {
        int32_t newlevel = e->levels[ix];
        int32_t actual = fm_scaleout(newlevel) >> 1;
        int32_t qrate;
        actual = (actual << 6) + e->outlevel - 4256;
        actual = actual < 16 ? 16 : actual;
        e->target = actual << 16;
        e->rising = e->target > e->level;
        qrate = (e->rates[ix] * 41) >> 6;
        qrate += e->rscale;
        qrate = qrate < 63 ? qrate : 63;
        if (e->target == e->level || (ix == 0u && newlevel == 0)) {
            int32_t sr = e->rates[ix] + e->rscale;
            sr = sr < 99 ? sr : 99;
            e->statics = sr < 77 ? FM_STATICS[sr] : 20 * (99 - sr);
            if (sr < 77 && ix == 0u && newlevel == 0)
                e->statics /= 20;                   /* (the attack scaled faster) */
        } else {
            e->statics = 0;
        }
        e->inc = (4 + (qrate & 3)) << (2 + 6 + (qrate >> 2));
    }
}
static void env_init(dn_fm_env_t *e, const uint8_t *r, const uint8_t *l, int32_t ol, int32_t rscale)
{
    uint32_t i;
    for (i = 0; i < 4u; i++)
        e->rates[i] = r[i], e->levels[i] = l[i];
    e->outlevel = (int16_t)ol;
    e->rscale = (uint8_t)rscale;
    e->level = 0;
    e->down = 1;
    env_advance(e, 0);
}
static int32_t env_sample(dn_fm_env_t *e)
{
    if (e->statics) {
        e->statics -= (int32_t)DN_FM_N;
        if (e->statics <= 0) {
            e->statics = 0;
            env_advance(e, e->ix + 1u);
        }
    }
    if (e->ix < 3u || (e->ix < 4u && !e->down)) {
        if (e->statics) {
            ;
        } else if (e->rising) {
            if (e->level < (1716 << 16))
                e->level = 1716 << 16;
            e->level += (((17 << 24) - e->level) >> 24) * e->inc;
            if (e->level >= e->target) {
                e->level = e->target;
                env_advance(e, e->ix + 1u);
            }
        } else {
            e->level -= e->inc;
            if (e->level <= e->target) {
                e->level = e->target;
                env_advance(e, e->ix + 1u);
            }
        }
    }
    return e->level;
}
static void env_key(dn_fm_env_t *e, uint32_t d)
{
    if (e->down != d) {
        e->down = (uint8_t)d;
        env_advance(e, d ? 0u : 3u);
    }
}

/* ---- the pitch envelope (pitchenv.cc) ---------------------------------------------------------------------- */
static const uint8_t FM_PENV_RATE[100] = {
    1, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 16, 16, 17, 18, 18, 19,
    20, 21, 22, 23, 24, 25, 26, 27, 28, 30, 31, 33, 34, 36, 37, 38, 39, 41, 42, 44, 46, 47, 49, 51, 53, 54, 56, 58, 60,
    62, 64, 66, 68, 70, 72, 74, 76, 79, 82, 85, 88, 91, 94, 98, 102, 106, 110, 115, 120, 125, 130, 135, 141, 147, 153,
    159, 165, 171, 178, 185, 193, 202, 211, 232, 243, 254, 255};
static const int8_t FM_PENV_TAB[100] = {
    -128, -116, -104, -95, -85, -76, -68, -61, -56, -52, -49, -46, -43, -41, -39, -37, -35, -33, -32, -31, -30, -29,
    -28, -27, -26, -25, -24, -23, -22, -21, -20, -19, -18, -17, -16, -15, -14, -13, -12, -11, -10, -9, -8, -7, -6, -5,
    -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
    27, 28, 29, 30, 31, 32, 33, 34, 35, 38, 40, 43, 46, 49, 53, 58, 65, 73, 82, 92, 103, 115, 127};
static void penv_advance(dn_fm_penv_t *p, uint32_t ix)
{
    p->ix = (uint8_t)ix;
    if (ix < 4u) {
        p->target = FM_PENV_TAB[p->levels[ix]] * (1 << 19);
        p->rising = p->target > p->level;
        p->inc = FM_PENV_RATE[p->rates[ix]] * DN_FM_PENV_UNIT;
    }
}
static void penv_set(dn_fm_penv_t *p, const uint8_t *r, const uint8_t *l)
{
    uint32_t i;
    for (i = 0; i < 4u; i++)
        p->rates[i] = r[i], p->levels[i] = l[i];
    p->level = FM_PENV_TAB[l[3]] * (1 << 19);
    p->down = 1;
    penv_advance(p, 0);
}
static int32_t penv_sample(dn_fm_penv_t *p)
{
    if (p->ix < 3u || (p->ix < 4u && !p->down)) {
        if (p->rising) {
            p->level += p->inc;
            if (p->level >= p->target) {
                p->level = p->target;
                penv_advance(p, p->ix + 1u);
            }
        } else {
            p->level -= p->inc;
            if (p->level <= p->target) {
                p->level = p->target;
                penv_advance(p, p->ix + 1u);
            }
        }
    }
    return p->level;
}

/* ---- the algorithms (fm_core.cc): per operator (OP6 first) its in / out buses, carrier (+), feedback ---------- */
static const uint8_t FM_ALG[32][6] = {
    {0xc1, 0x11, 0x11, 0x14, 0x01, 0x14}, {0x01, 0x11, 0x11, 0x14, 0xc1, 0x14}, {0xc1, 0x11, 0x14, 0x01, 0x11, 0x14},
    {0xc1, 0x11, 0x94, 0x01, 0x11, 0x14}, {0xc1, 0x14, 0x01, 0x14, 0x01, 0x14}, {0xc1, 0x94, 0x01, 0x14, 0x01, 0x14},
    {0xc1, 0x11, 0x05, 0x14, 0x01, 0x14}, {0x01, 0x11, 0xc5, 0x14, 0x01, 0x14}, {0x01, 0x11, 0x05, 0x14, 0xc1, 0x14},
    {0x01, 0x05, 0x14, 0xc1, 0x11, 0x14}, {0xc1, 0x05, 0x14, 0x01, 0x11, 0x14}, {0x01, 0x05, 0x05, 0x14, 0xc1, 0x14},
    {0xc1, 0x05, 0x05, 0x14, 0x01, 0x14}, {0xc1, 0x05, 0x11, 0x14, 0x01, 0x14}, {0x01, 0x05, 0x11, 0x14, 0xc1, 0x14},
    {0xc1, 0x11, 0x02, 0x25, 0x05, 0x14}, {0x01, 0x11, 0x02, 0x25, 0xc5, 0x14}, {0x01, 0x11, 0x11, 0xc5, 0x05, 0x14},
    {0xc1, 0x14, 0x14, 0x01, 0x11, 0x14}, {0x01, 0x05, 0x14, 0xc1, 0x14, 0x14}, {0x01, 0x14, 0x14, 0xc1, 0x14, 0x14},
    {0xc1, 0x14, 0x14, 0x14, 0x01, 0x14}, {0xc1, 0x14, 0x14, 0x01, 0x14, 0x04}, {0xc1, 0x14, 0x14, 0x14, 0x04, 0x04},
    {0xc1, 0x14, 0x14, 0x04, 0x04, 0x04}, {0xc1, 0x05, 0x14, 0x01, 0x14, 0x04}, {0x01, 0x05, 0x14, 0xc1, 0x14, 0x04},
    {0x04, 0xc1, 0x11, 0x14, 0x01, 0x14}, {0xc1, 0x14, 0x01, 0x14, 0x04, 0x04}, {0x04, 0xc1, 0x11, 0x14, 0x04, 0x04},
    {0xc1, 0x14, 0x04, 0x04, 0x04, 0x04}, {0xc4, 0x04, 0x04, 0x04, 0x04, 0x04}};
uint32_t dn_fm_is_carrier(uint32_t alg, uint32_t op) { return (FM_ALG[alg & 31u][op % 6u] & 4u) != 0u; }

/* the operator kernels (fm_op_kernel.cc): in (a bus or none), feedback, into out (added or set) */
static void fm_kernel(int32_t *out, const int32_t *in, dn_fm_op_t *o, int32_t g1, int32_t g2, int add)
{
    int32_t dg = (g2 - g1 + (int32_t)(DN_FM_N >> 1)) >> 6, g = g1, ph = o->phase, f = o->freq;
    uint32_t i;
    for (i = 0; i < DN_FM_N; i++) {
        int32_t y;
        g += dg;
        y = (int32_t)(((int64_t)fm_sin(ph + (in ? in[i] : 0)) * (int64_t)g) >> 24);
        out[i] = add ? out[i] + y : y;
        ph += f;
    }
}
static void fm_kernel_fb(int32_t *out, dn_fm_op_t *o, int32_t g1, int32_t g2, int32_t *fb, int32_t shift, int add)
{
    int32_t dg = (g2 - g1 + (int32_t)(DN_FM_N >> 1)) >> 6, g = g1, ph = o->phase, f = o->freq;
    int32_t y0 = fb[0], y = fb[1];
    uint32_t i;
    for (i = 0; i < DN_FM_N; i++) {
        int32_t sfb;
        g += dg;
        sfb = (y0 + y) >> (shift + 1);
        y0 = y;
        y = fm_sin(ph + sfb);
        y = (int32_t)(((int64_t)y * (int64_t)g) >> 24);
        out[i] = add ? out[i] + y : y;
        ph += f;
    }
    fb[0] = y0;
    fb[1] = y;
}
static void fm_core(dn_fm_t *s, dn_fm_v_t *v, int32_t *output)
{
    const uint8_t *alg = FM_ALG[v->alg & 31u];
    int has[3] = {1, 0, 0};
    uint32_t op;
    for (op = 0; op < 6u; op++) {
        uint32_t fl = alg[op];
        int add = (fl & 4u) != 0u;
        dn_fm_op_t *o = &v->op[op];
        uint32_t inbus = (fl >> 4) & 3u, outbus = fl & 3u;
        int32_t *outp = outbus == 0u ? output : s->bus[outbus - 1u];
        int32_t g1 = o->gain_out, g2 = fm_exp2(o->level_in - (14 * (1 << 24)));
        o->gain_out = g2;
        if (g1 >= 1120 || g2 >= 1120) {
            if (!has[outbus])
                add = 0;
            if (inbus == 0u || !has[inbus]) {
                if ((fl & 0xc0u) == 0xc0u && v->fbshift < 16u)
                    fm_kernel_fb(outp, o, g1, g2, v->fb, v->fbshift, add);
                else
                    fm_kernel(outp, 0, o, g1, g2, add);
            } else {
                fm_kernel(outp, s->bus[inbus - 1u], o, g1, g2, add);
            }
            has[outbus] = 1;
        } else if (!add) {
            has[outbus] = 0;
        }
        o->phase += o->freq << 6;
    }
}

/* ---- a note (dx7note.cc) ----------------------------------------------------------------------------------- */
static const int32_t FM_COARSE[32] = {
    -16777216, 0, 16777216, 26591258, 33554432, 38955489, 43368474, 47099600, 50331648, 53182516, 55732705, 58039632,
    60145690, 62083076, 63876816, 65546747, 67108864, 68576247, 69959732, 71268397, 72509921, 73690858, 74816848,
    75892776, 76922906, 77910978, 78860292, 79773775, 80654032, 81503396, 82323963, 83117622};
static const uint8_t FM_VEL[64] = {
    0, 70, 86, 97, 106, 114, 121, 126, 132, 138, 142, 148, 152, 156, 160, 163, 166, 170, 173, 174, 178, 181, 184, 186,
    189, 190, 194, 196, 198, 200, 202, 205, 206, 209, 211, 214, 216, 218, 220, 222, 224, 225, 227, 229, 230, 232, 233,
    235, 237, 238, 240, 241, 242, 243, 244, 246, 246, 248, 249, 250, 251, 252, 253, 254};
static const uint8_t FM_EXPSCALE[33] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 14, 16, 19, 23, 27, 33, 39, 47, 56, 66,
                                        80, 94, 110, 126, 142, 158, 174, 190, 206, 222, 238, 250};
static const uint8_t FM_PMS[8] = {0, 10, 20, 33, 55, 92, 153, 255};
static const uint32_t FM_AMS[4] = {0, 4342338, 7171437, 16777216};

static int32_t fm_osc_freq(int32_t note, uint32_t mode, uint32_t coarse, uint32_t fine, int32_t detune)
{
    int32_t lf;
    if (mode == 0u) {
        lf = 50857777 + 1398101 * note;             /* (StandardTuning: (1 << 24) / 12 a semitone) */
        lf += DN_FM_DT[15 * (note & 127) + (detune < 0 ? 0 : detune > 14 ? 14 : detune)];   /* (MSFA's double maths,
                                                     * made at build time) */
        lf += FM_COARSE[coarse & 31u];
        if (fine)
            lf += DN_FM_FINE[fine < 100u ? fine : 99u];
    } else {
        lf = (4458616 * (int32_t)((coarse & 3u) * 100u + fine)) >> 3;
        lf += detune > 7 ? 13457 * (detune - 7) : 0;
    }
    return lf;
}
static int32_t fm_scale_vel(int32_t vel, int32_t sens)
{
    int32_t c = vel < 0 ? 0 : vel > 127 ? 127 : vel;
    int32_t vv = FM_VEL[c >> 1] - 239;
    return ((sens * vv + 7) >> 3) << 4;
}
static int32_t fm_scale_rate(int32_t note, int32_t sens)
{
    int32_t x = note / 3 - 7;
    x = x < 0 ? 0 : x > 31 ? 31 : x;
    return (sens * x) >> 3;
}
static int32_t fm_scale_curve(int32_t group, int32_t depth, int32_t curve)
{
    int32_t sc;
    if (curve == 0 || curve == 3)
        sc = (group * depth * 329) >> 12;
    else
        sc = (FM_EXPSCALE[group < 32 ? group : 32] * depth * 329) >> 15;
    return curve < 2 ? -sc : sc;
}
static int32_t fm_scale_level(int32_t note, int32_t bp, int32_t ld, int32_t rd, int32_t lc, int32_t rc)
{
    int32_t off = note - bp - 17;
    return off >= 0 ? fm_scale_curve((off + 1) / 3, rd, rc) : fm_scale_curve(-(off - 1) / 3, ld, lc);
}

static void fm_note_init(dn_fm_v_t *v, const uint8_t *p, int32_t note, int32_t vel)
{
    uint32_t op;
    for (op = 0; op < 6u; op++) {
        const uint8_t *o = p + 21u * op;
        int32_t ol = fm_scaleout(o[DN_FMO_OL]);
        ol += fm_scale_level(note, o[DN_FMO_BP], o[DN_FMO_LD], o[DN_FMO_RD], o[DN_FMO_LC], o[DN_FMO_RC]);
        ol = ol < 127 ? ol : 127;
        ol <<= 5;
        ol += fm_scale_vel(vel, o[DN_FMO_KVS]);
        ol = ol > 0 ? ol : 0;
        env_init(&v->env[op], o + DN_FMO_R1, o + DN_FMO_L1, ol, fm_scale_rate(note, o[DN_FMO_RS]));
        v->base[op] = fm_osc_freq(note, o[DN_FMO_MODE], o[DN_FMO_COARSE], o[DN_FMO_FINE], o[DN_FMO_DET]);
        v->mode[op] = o[DN_FMO_MODE];
        v->amsens[op] = FM_AMS[o[DN_FMO_AMS] & 3u];
    }
    penv_set(&v->penv, p + DN_FMV_PR1, p + DN_FMV_PL1);
    v->alg = p[DN_FMV_ALG] & 31u;
    v->fbshift = (uint8_t)(p[DN_FMV_FB] ? 8u - p[DN_FMV_FB] : 16u);
    v->pmd = (p[DN_FMV_LPMD] * 165) >> 6;
    v->pms = FM_PMS[p[DN_FMV_LPMS] & 7u];
    v->amd = (p[DN_FMV_LAMD] * 165) >> 6;
    v->opsw = p[DN_FMV_OPSW];
}
/* the pitches of a held (mono, legato) note moved to another key, its envelopes going on */
static void fm_note_repitch(dn_fm_v_t *v, const uint8_t *p, int32_t note)
{
    uint32_t op;
    for (op = 0; op < 6u; op++) {
        const uint8_t *o = p + 21u * op;
        v->base[op] = fm_osc_freq(note, o[DN_FMO_MODE], o[DN_FMO_COARSE], o[DN_FMO_FINE], o[DN_FMO_DET]);
    }
}
static void fm_note_compute(dn_fm_t *s, dn_fm_v_t *v, int32_t *buf, int32_t lfo_val, int32_t lfo_delay)
{
    uint32_t pmd = (uint32_t)v->pmd * (uint32_t)lfo_delay, op;
    int32_t senslfo = v->pms * (lfo_val - (1 << 23));
    int32_t pmod = (int32_t)(((int64_t)pmd * (int64_t)senslfo) >> 39);
    uint32_t amod;
    pmod = pmod < 0 ? -pmod : pmod;
    pmod = penv_sample(&v->penv) + (senslfo < 0 ? -pmod : pmod);
    lfo_val = (1 << 24) - lfo_val;
    amod = (uint32_t)(((int64_t)v->amd * (int64_t)lfo_delay) >> 8);
    amod = (uint32_t)(((int64_t)amod * (int64_t)lfo_val) >> 24);
    for (op = 0; op < 6u; op++) {
        if (!(v->opsw & (1u << op))) {
            env_sample(&v->env[op]);
            v->op[op].level_in = 0;
        } else {
            int32_t level;
            v->op[op].freq = fm_freq(v->mode[op] ? v->base[op] : v->base[op] + pmod);
            level = env_sample(&v->env[op]);
            if (v->amsens[op]) {
                uint32_t sa = (uint32_t)(((uint64_t)amod * (uint64_t)v->amsens[op]) >> 24);
                uint32_t pt = (uint32_t)fm_expf((float)sa * (0.07f / 262144.f) + 12.2f);
                uint32_t ld = (uint32_t)(((uint64_t)(uint32_t)level * ((uint64_t)pt << 4)) >> 28);
                level -= (int32_t)ld;
            }
            v->op[op].level_in = level;
        }
    }
    fm_core(s, v, buf);
}
static int fm_playing(const dn_fm_v_t *v)
{
    uint32_t op;
    if (!v->live)
        return 0;
    for (op = 0; op < 6u; op++)
        if (dn_fm_is_carrier(v->alg, op) && (v->env[op].ix < 4u || v->env[op].levels[3] > 0u))
            return 1;
    return 0;
}
/* gone quiet: the key up, every carrier past its release and below the kernels' threshold */
static int fm_silent(const dn_fm_v_t *v)
{
    uint32_t op;
    if (v->key)
        return 0;
    for (op = 0; op < 6u; op++) {
        const dn_fm_env_t *e = &v->env[op];
        if (!dn_fm_is_carrier(v->alg, op) || !(v->opsw & (1u << op)))
            continue;
        if (e->ix < 3u || (e->ix == 3u && e->rising) || v->op[op].gain_out >= 1120)
            return 0;
    }
    return 1;
}

/* ---- the LFO (lfo.cc): one for all voices ------------------------------------------------------------------ */
static void fm_lfo_reset(dn_fm_t *s, const uint8_t *p)
{
    int32_t a = 99 - p[DN_FMV_LFD];
    s->lfo_delta = DN_FM_LFO[p[DN_FMV_LFS] < 100u ? p[DN_FMV_LFS] : 99u];
    if (a == 99) {
        s->lfo_dinc = s->lfo_dinc2 = ~0u;
    } else {
        a = (16 + (a & 15)) << (1 + (a >> 4));
        s->lfo_dinc = (uint32_t)DN_FM_LFO_UNIT * (uint32_t)a;
        a &= 0xff80;
        a = a > 0x80 ? a : 0x80;
        s->lfo_dinc2 = (uint32_t)DN_FM_LFO_UNIT * (uint32_t)a;
    }
    s->lfo_wave = p[DN_FMV_LFW];
    s->lfo_sync = p[DN_FMV_LFKS] != 0u;
}
static int32_t fm_lfo_sample(dn_fm_t *s)
{
    uint32_t ph = s->lfo_ph += s->lfo_delta;
    int32_t x;
    switch (s->lfo_wave) {
    case 0:
        x = (int32_t)(ph >> 7);
        x ^= -(int32_t)(ph >> 31);
        return x & ((1 << 24) - 1);
    case 1:
        return (int32_t)((~ph ^ (1u << 31)) >> 8);
    case 2:
        return (int32_t)((ph ^ (1u << 31)) >> 8);
    case 3:
        return (int32_t)(((~ph) >> 7) & (1u << 24));
    case 4:
        return (1 << 23) + (fm_sin((int32_t)(ph >> 8)) >> 1);
    case 5:
        if (ph < s->lfo_delta)
            s->lfo_rand = (uint8_t)((s->lfo_rand * 179u + 17u) & 0xffu);
        return ((s->lfo_rand ^ 0x80) + 1) << 16;
    default:
        return 1 << 23;
    }
}
static int32_t fm_lfo_delay(dn_fm_t *s)
{
    uint32_t delta = s->lfo_dstate < (1u << 31) ? s->lfo_dinc : s->lfo_dinc2;
    uint64_t d = (uint64_t)s->lfo_dstate + delta;
    if (d > 0xFFFFFFFFull)
        return 1 << 24;
    s->lfo_dstate = (uint32_t)d;
    return d < (1u << 31) ? 0 : (int32_t)((d >> 7) & ((1u << 24) - 1u));
}
static void fm_lfo_key(dn_fm_t *s)
{
    if (s->lfo_sync)
        s->lfo_ph = (1u << 31) - 1u;
    s->lfo_dstate = 0;
}

/* ---- the synth --------------------------------------------------------------------------------------------- */
void dn_fm_patch_init(uint8_t *p)
{
    uint32_t op, i;
    for (op = 0; op < 6u; op++) {
        uint8_t *o = p + 21u * op;
        for (i = 0; i < 4u; i++)
            o[DN_FMO_R1 + i] = 99, o[DN_FMO_L1 + i] = i < 3u ? 99u : 0u;
        o[DN_FMO_BP] = 39, o[DN_FMO_LD] = 0, o[DN_FMO_RD] = 0, o[DN_FMO_LC] = 0, o[DN_FMO_RC] = 0;
        o[DN_FMO_RS] = 0, o[DN_FMO_AMS] = 0, o[DN_FMO_KVS] = 0, o[DN_FMO_OL] = op == 5u ? 99u : 0u;
        o[DN_FMO_MODE] = 0, o[DN_FMO_COARSE] = 1, o[DN_FMO_FINE] = 0, o[DN_FMO_DET] = 7;
    }
    for (i = 0; i < 4u; i++)
        p[DN_FMV_PR1 + i] = 99, p[DN_FMV_PL1 + i] = 50;
    p[DN_FMV_ALG] = 0, p[DN_FMV_FB] = 0, p[DN_FMV_OKS] = 1, p[DN_FMV_LFS] = 35, p[DN_FMV_LFD] = 0;
    p[DN_FMV_LPMD] = 0, p[DN_FMV_LAMD] = 0, p[DN_FMV_LFKS] = 1, p[DN_FMV_LFW] = 0, p[DN_FMV_LPMS] = 3;
    p[DN_FMV_TRNSP] = 24;
    for (i = 0; i < 10u; i++)
        p[DN_FMV_NAME + i] = (uint8_t)"INIT VOICE"[i];
    p[DN_FMV_OPSW] = 0x3f;
}

/* each value's top (and so its range: all start at 0) */
static uint8_t fm_top(uint32_t k)
{
    static const uint8_t OT[DN_FMO_N] = {99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 3, 3, 7, 3, 7, 99, 1, 31, 99, 14};
    static const uint8_t VT[30] = {99, 99, 99, 99, 99, 99, 99, 99, 31, 7, 1, 99, 99, 99, 99, 1, 5, 7, 48,
                                   127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 63};
    return k < 126u ? OT[k % 21u] : k < DN_FM_PATCH ? VT[k - 126u] : 0u;
}
int32_t dn_fm_vmax(uint32_t k) { return fm_top(k); }

void dn_fm_unpack(const uint8_t *b, uint8_t *r)
{
    uint32_t op, i;
    for (op = 0; op < 6u; op++) {
        const uint8_t *s = b + 17u * op;
        uint8_t *o = r + 21u * op;
        for (i = 0; i < 11u; i++)
            o[i] = s[i] & 0x7fu;
        o[DN_FMO_LC] = s[11] & 3u;
        o[DN_FMO_RC] = (s[11] >> 2) & 3u;
        o[DN_FMO_RS] = s[12] & 7u;
        o[DN_FMO_AMS] = s[13] & 3u;
        o[DN_FMO_KVS] = (s[13] >> 2) & 7u;
        o[DN_FMO_OL] = s[14] & 0x7fu;
        o[DN_FMO_MODE] = s[15] & 1u;
        o[DN_FMO_COARSE] = (s[15] >> 1) & 0x1fu;
        o[DN_FMO_FINE] = s[16] & 0x7fu;
        o[DN_FMO_DET] = (s[12] >> 3) & 0x0fu;
    }
    for (i = 0; i < 8u; i++)
        r[126u + i] = b[102u + i] & 0x7fu;
    r[DN_FMV_ALG] = b[110] & 0x1fu;
    r[DN_FMV_FB] = b[111] & 7u;
    r[DN_FMV_OKS] = (b[111] >> 3) & 1u;
    r[DN_FMV_LFS] = b[112] & 0x7fu;
    r[DN_FMV_LFD] = b[113] & 0x7fu;
    r[DN_FMV_LPMD] = b[114] & 0x7fu;
    r[DN_FMV_LAMD] = b[115] & 0x7fu;
    r[DN_FMV_LFKS] = b[116] & 1u;
    r[DN_FMV_LFW] = (b[116] >> 1) & 7u;
    r[DN_FMV_LPMS] = (b[116] >> 4) & 7u;
    r[DN_FMV_TRNSP] = b[117] & 0x7fu;
    for (i = 0; i < 10u; i++)
        r[DN_FMV_NAME + i] = b[118u + i] & 0x7fu;
    r[DN_FMV_OPSW] = 0x3f;
    for (i = 0; i < DN_FM_PATCH; i++)               /* (out of range in a bank: brought in, as Dexed's normparm) */
        if (i < DN_FMV_NAME || i == DN_FMV_OPSW)
            r[i] = r[i] > fm_top(i) ? fm_top(i) : r[i];
}
void dn_fm_pack(const uint8_t *r, uint8_t *b)
{
    uint32_t op, i;
    for (op = 0; op < 6u; op++) {
        const uint8_t *o = r + 21u * op;
        uint8_t *s = b + 17u * op;
        for (i = 0; i < 11u; i++)
            s[i] = o[i];
        s[11] = (uint8_t)((o[DN_FMO_LC] & 3u) | (o[DN_FMO_RC] & 3u) << 2);
        s[12] = (uint8_t)((o[DN_FMO_RS] & 7u) | (o[DN_FMO_DET] & 15u) << 3);
        s[13] = (uint8_t)((o[DN_FMO_AMS] & 3u) | (o[DN_FMO_KVS] & 7u) << 2);
        s[14] = o[DN_FMO_OL];
        s[15] = (uint8_t)((o[DN_FMO_MODE] & 1u) | (o[DN_FMO_COARSE] & 31u) << 1);
        s[16] = o[DN_FMO_FINE];
    }
    for (i = 0; i < 8u; i++)
        b[102u + i] = r[126u + i];
    b[110] = r[DN_FMV_ALG] & 31u;
    b[111] = (uint8_t)((r[DN_FMV_FB] & 7u) | (r[DN_FMV_OKS] & 1u) << 3);
    b[112] = r[DN_FMV_LFS], b[113] = r[DN_FMV_LFD], b[114] = r[DN_FMV_LPMD], b[115] = r[DN_FMV_LAMD];
    b[116] = (uint8_t)((r[DN_FMV_LFKS] & 1u) | (r[DN_FMV_LFW] & 7u) << 1 | (r[DN_FMV_LPMS] & 7u) << 4);
    b[117] = r[DN_FMV_TRNSP];
    for (i = 0; i < 10u; i++)
        b[118u + i] = r[DN_FMV_NAME + i] & 0x7fu;
}

void dn_fm_init(dn_fm_t *s)
{
    uint8_t *z = (uint8_t *)s;
    uint32_t i;
    for (i = 0; i < sizeof *s; i++)
        z[i] = 0;
    s->voices = DN_FM_NV;
    s->level = 100;
    s->pos = DN_FM_N;
    dn_fm_patch_init(s->patch);
    fm_lfo_reset(s, s->patch);
}
void dn_fm_voice_load(dn_fm_t *s, const uint8_t *patch)
{
    uint32_t i;
    for (i = 0; i < DN_FM_PATCH; i++)
        s->patch[i] = patch[i];
    fm_lfo_reset(s, s->patch);
}
uint32_t dn_fm_sounding(const dn_fm_t *s)
{
    uint32_t i, n = 0;
    for (i = 0; i < DN_FM_NV; i++)
        n += s->v[i].live != 0u;
    return n;
}
static void fm_osc_sync(dn_fm_v_t *v)
{
    uint32_t op;
    for (op = 0; op < 6u; op++)
        v->op[op].gain_out = 0, v->op[op].phase = 0;
}
void dn_fm_note_on(dn_fm_t *s, uint32_t note, uint32_t vel)
{
    uint32_t i, best = 0, nv = s->voices ? (s->voices < DN_FM_NV ? s->voices : DN_FM_NV) : 1u, held = 0;
    int32_t bs = -1, pitch = (int32_t)note + (int32_t)s->patch[DN_FMV_TRNSP] - 24;
    dn_fm_v_t *v;
    int steal;
    if (!vel) {
        dn_fm_note_off(s, note);
        return;
    }
    pitch = pitch < 0 ? 0 : pitch > 127 ? 127 : pitch;
    for (i = 0; i < DN_FM_NV; i++)
        held |= s->v[i].key;
    if (!held)
        fm_lfo_key(s);
    if (nv == 1u) {                                 /* mono: one voice, last note; a key still down: legato */
        v = &s->v[0];
        for (i = 1; i < DN_FM_NV; i++)
            s->v[i].live = s->v[i].key = 0;
        if (v->live && v->key) {
            fm_note_repitch(v, s->patch, pitch);
            v->note = (uint8_t)note;
            return;
        }
        steal = fm_playing(v);
        fm_note_init(v, s->patch, pitch, (int32_t)vel);
        if (s->patch[DN_FMV_OKS] && !steal)
            fm_osc_sync(v);
        v->live = 1, v->key = 1, v->note = (uint8_t)note, v->vel = (uint8_t)vel, v->seq = ++s->seq;
        return;
    }
    for (i = 0; i < nv; i++) {                      /* Dexed's choice: free (4), key up (2), same note (1), oldest */
        int32_t sc = (!fm_playing(&s->v[i]) ? 4 : 0) + (!s->v[i].key ? 2 : 0) + (s->v[i].note == note ? 1 : 0);
        if (sc > bs || (sc == bs && s->v[i].seq < s->v[best].seq))
            best = i, bs = sc;
    }
    for (i = nv; i < DN_FM_NV; i++)                 /* (VOICES lowered: the voices above it go) */
        s->v[i].live = s->v[i].key = 0;
    v = &s->v[best];
    steal = fm_playing(v);
    fm_note_init(v, s->patch, pitch, (int32_t)vel);
    if (s->patch[DN_FMV_OKS] && !steal) {
        fm_osc_sync(v);
    } else if (!s->patch[DN_FMV_OKS]) {
        for (i = 0; i < nv; i++)                    /* (the same pitch playing: its phases, no cancelling) */
            if (i != best && fm_playing(&s->v[i]) && s->v[i].note == note) {
                uint32_t op;
                for (op = 0; op < 6u; op++)
                    v->op[op].phase = s->v[i].op[op].phase;
                break;
            }
    }
    v->live = 1, v->key = 1, v->note = (uint8_t)note, v->vel = (uint8_t)vel, v->seq = ++s->seq;
}
static void fm_keyup(dn_fm_v_t *v)
{
    uint32_t op;
    for (op = 0; op < 6u; op++)
        env_key(&v->env[op], 0);
    if (v->penv.down) {
        v->penv.down = 0;
        penv_advance(&v->penv, 3);
    }
    v->key = 0;
}
void dn_fm_note_off(dn_fm_t *s, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < DN_FM_NV; i++)                  /* (one voice a note-off, as Dexed: the same note twice, two offs) */
        if (s->v[i].key && s->v[i].note == note) {
            fm_keyup(&s->v[i]);
            return;
        }
}
void dn_fm_all_off(dn_fm_t *s)
{
    uint32_t i;
    for (i = 0; i < DN_FM_NV; i++)
        if (s->v[i].key)
            fm_keyup(&s->v[i]);
}
void dn_fm_cut(dn_fm_t *s)
{
    uint32_t i;
    for (i = 0; i < DN_FM_NV; i++)
        s->v[i].live = s->v[i].key = 0;
}

/* a block: every voice, each clipped as Dexed clips it, then the level */
static void fm_block(dn_fm_t *s)
{
    static int32_t acc[DN_FM_N], vb[DN_FM_N];
    int32_t lv, ld, g = s->level >= 128 ? 32768 : (int32_t)s->level * (int32_t)s->level;   /* (LEVEL's square:
                                                     * 100 -> about -3.7 dB; 128 as Dexed, the tests') */
    uint32_t i, j, any = 0;
    lv = fm_lfo_sample(s);
    ld = fm_lfo_delay(s);
    for (j = 0; j < DN_FM_N; j++)
        acc[j] = 0;
    s->busy = 0;
    for (i = 0; i < DN_FM_NV; i++) {
        dn_fm_v_t *v = &s->v[i];
        if (!v->live)
            continue;
        for (j = 0; j < DN_FM_N; j++)
            vb[j] = 0;
        fm_note_compute(s, v, vb, lv, ld);
        for (j = 0; j < DN_FM_N; j++) {
            int32_t x = vb[j] >> 4;
            acc[j] += x < -(1 << 24) ? -32768 : x >= (1 << 24) ? 32767 : x >> 9;
        }
        any = 1;
        s->busy++;
        if (fm_silent(v))
            v->live = 0;
    }
    for (j = 0; j < DN_FM_N; j++)
        s->out[j] = 0;
    if (any)
        for (j = 0; j < DN_FM_N; j++) {
            int32_t y = (acc[j] * g) >> 15;         /* (127 x 127 / 32768: about half; voices add) */
            s->out[j] = (int16_t)(y < -32768 ? -32768 : y > 32767 ? 32767 : y);
        }
}
void dn_fm_render(dn_fm_t *s, int32_t *lr, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t y;
        if (s->pos >= DN_FM_N) {
            uint32_t k, live = 0;
            for (k = 0; k < DN_FM_NV; k++)
                live |= s->v[k].live;
            if (!live) {                            /* (nothing playing: nothing to make) */
                s->pos = DN_FM_N;
                fm_lfo_sample(s);
                return;
            }
            fm_block(s);
            s->pos = 0;
        }
        y = s->out[s->pos++];
        lr[2u * i] += y;
        lr[2u * i + 1u] += y;
    }
}

/* ---- the values' names and text ---------------------------------------------------------------------------- */
const char *dn_fm_vname(uint32_t k)
{
    static const char *const ON[DN_FMO_N] = {"R1", "R2", "R3", "R4", "L1", "L2", "L3", "L4", "BREAK", "L.DEPTH",
                                             "R.DEPTH", "L.CURVE", "R.CURVE", "RATE KS", "AMS", "VEL", "LEVEL",
                                             "MODE", "COARSE", "FINE", "DETUNE"};
    static const char *const VN[19] = {"P.R1", "P.R2", "P.R3", "P.R4", "P.L1", "P.L2", "P.L3", "P.L4", "ALGO",
                                       "FEEDBACK", "KEY SYNC", "LFO SPD", "LFO DLY", "LFO PMD", "LFO AMD",
                                       "LFO SYNC", "LFO WAVE", "PMS", "TRANSP"};
    if (k < 126u)
        return ON[k % 21u];
    if (k < DN_FMV_NAME)
        return VN[k - 126u];
    return k == DN_FMV_OPSW ? "OPS ON" : "NAME";
}
static void fm_str(char out[8], const char *w)
{
    uint32_t i;
    for (i = 0; w[i] && i < 7u; i++)
        out[i] = w[i];
    out[i] = 0;
}
static void fm_num(char out[8], int32_t v)
{
    char t[12];
    uint32_t n = 0, i = 0, u = v < 0 ? (uint32_t)-v : (uint32_t)v;
    do
        t[n++] = (char)('0' + u % 10u), u /= 10u;
    while (u && n < 10u);
    if (v < 0)
        out[i++] = '-';
    while (n && i < 7u)
        out[i++] = t[--n];
    out[i] = 0;
}
void dn_fm_vtext(uint32_t k, int32_t v, char out[8])
{
    static const char *const CURVE[4] = {"-LIN", "-EXP", "+EXP", "+LIN"};
    static const char *const WAVE[6] = {"TRI", "SAW DN", "SAW UP", "SQUARE", "SINE", "S&H"};
    static const char *const NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    if (k < 126u) {
        uint32_t f = k % 21u;
        if (f == DN_FMO_LC || f == DN_FMO_RC) {
            fm_str(out, CURVE[v & 3]);
            return;
        }
        if (f == DN_FMO_MODE) {
            fm_str(out, v ? "FIXED" : "RATIO");
            return;
        }
        if (f == DN_FMO_DET) {
            fm_num(out, v - 7);
            if (v > 7) {
                char t[8];
                uint32_t i;
                fm_str(t, out);
                out[0] = '+';
                for (i = 0; t[i] && i < 6u; i++)
                    out[1u + i] = t[i];
                out[1u + i] = 0;
            }
            return;
        }
        if (f == DN_FMO_BP) {                       /* (break point 0 = A-1 .. 99 = C8; C3 = 39) */
            int32_t n = v + 21;
            char o[8];
            uint32_t i = 0, j = 0;
            fm_str(out, NN[n % 12]);
            fm_num(o, n / 12 - 2);
            while (out[i])
                i++;
            while (o[j] && i < 7u)
                out[i++] = o[j++];
            out[i] = 0;
            return;
        }
    } else if (k == DN_FMV_ALG) {
        fm_num(out, v + 1);
        return;
    } else if (k == DN_FMV_FB) {
        fm_num(out, v);
        return;
    } else if (k == DN_FMV_OKS || k == DN_FMV_LFKS) {
        fm_str(out, v ? "ON" : "OFF");
        return;
    } else if (k == DN_FMV_LFW) {
        fm_str(out, WAVE[v < 6 ? v : 5]);
        return;
    } else if (k == DN_FMV_TRNSP) {
        int32_t n = v + 36;                         /* (24 = C3: the key plays as itself) */
        char o[8];
        uint32_t i = 0, j = 0;
        fm_str(out, NN[n % 12]);
        fm_num(o, n / 12 - 2);
        while (out[i])
            i++;
        while (o[j] && i < 7u)
            out[i++] = o[j++];
        out[i] = 0;
        return;
    }
    fm_num(out, v);
}

/* ---- the factory voices: DREADNOUGHT's own (jungle FM basses, stabs, bells), no Yamaha sounds ---------------- */
typedef struct {
    uint8_t r[4], l[4], ol, coarse, fine, det, kvs, ams;
} fm_opdef_t;
typedef struct {
    const char *name;
    uint8_t alg, fb, lfs, lfd, pmd, amd, lfw, pms;
    fm_opdef_t op[6];                               /* OP1 first */
} fm_def_t;
#define OP(r1, r2, r3, r4, l1, l2, l3, l4, ol, c, f, d, v) {{r1, r2, r3, r4}, {l1, l2, l3, l4}, ol, c, f, d, v, 0}
#define OFF OP(99, 99, 99, 99, 99, 99, 99, 0, 0, 1, 0, 7, 0)
static const fm_def_t FM_FACTORY[] = {
    /* a deep FM bass: a sine carrier, a ratio-1 modulator snapping shut fast (the "dub" knock), a little feedback */
    {"DUB FM BS", 1, 5, 30, 0, 0, 0, 0, 0,
     {OP(99, 60, 40, 70, 99, 90, 85, 0, 99, 1, 0, 7, 2), OP(99, 70, 50, 70, 99, 60, 40, 0, 80, 1, 0, 7, 3),
      OP(99, 50, 40, 70, 99, 80, 0, 0, 72, 1, 0, 8, 1), OP(99, 55, 40, 70, 99, 70, 30, 0, 70, 2, 0, 7, 2), OFF, OFF}},
    /* reese-ish: two carriers a hair apart, each with its own detuned modulator: the beating of a reese, FM's way */
    {"FM REESE", 4, 6, 20, 0, 6, 0, 4, 2,
     {OP(80, 40, 40, 70, 99, 95, 95, 0, 99, 1, 0, 3, 1), OP(80, 40, 40, 70, 99, 90, 88, 0, 76, 1, 0, 2, 2),
      OP(80, 40, 40, 70, 99, 95, 95, 0, 99, 1, 0, 11, 1), OP(80, 40, 40, 70, 99, 90, 88, 0, 76, 1, 0, 12, 2), OFF,
      OFF}},
    /* a hollow stab: 3 stacks of 2, short */
    {"HOLLOW STB", 4, 3, 35, 0, 0, 0, 0, 0,
     {OP(99, 50, 30, 60, 99, 75, 0, 0, 99, 1, 0, 7, 2), OP(99, 60, 40, 60, 99, 60, 0, 0, 82, 3, 0, 7, 4),
      OP(99, 50, 30, 60, 99, 75, 0, 0, 95, 2, 0, 8, 2), OP(99, 60, 40, 60, 99, 60, 0, 0, 78, 5, 0, 7, 4),
      OP(99, 50, 30, 60, 99, 75, 0, 0, 90, 1, 0, 6, 2), OP(99, 65, 40, 60, 99, 50, 0, 0, 80, 7, 0, 7, 4)}},
    /* a dark bell: an inharmonic modulator (3.50), a long ring */
    {"DARK BELL", 4, 0, 35, 0, 0, 0, 0, 0,
     {OP(99, 30, 25, 30, 99, 80, 0, 0, 99, 1, 0, 7, 2), OP(99, 45, 30, 35, 99, 60, 0, 0, 74, 3, 50, 7, 3),
      OP(99, 28, 25, 30, 99, 80, 0, 0, 90, 1, 0, 9, 2), OP(99, 40, 30, 35, 99, 55, 0, 0, 68, 4, 20, 7, 3), OFF, OFF}},
    /* a metal hit: fixed and odd ratios, fast, for percussion */
    {"METAL HIT", 6, 7, 35, 0, 0, 0, 0, 0,
     {OP(99, 55, 35, 50, 99, 0, 0, 0, 99, 1, 0, 7, 3), OP(99, 60, 40, 50, 99, 0, 0, 0, 88, 3, 41, 7, 4),
      OP(99, 70, 40, 50, 99, 0, 0, 0, 82, 7, 33, 7, 4), OP(99, 50, 35, 50, 99, 0, 0, 0, 92, 2, 70, 7, 3),
      OP(99, 70, 40, 50, 99, 0, 0, 0, 80, 9, 12, 7, 4), OP(99, 75, 40, 50, 99, 0, 0, 0, 78, 13, 0, 7, 4)}},
    /* a pluck: a bright modulator dying faster than the carrier */
    {"FM PLUCK", 4, 4, 35, 0, 0, 0, 0, 0,
     {OP(99, 45, 30, 55, 99, 60, 0, 0, 99, 1, 0, 7, 2), OP(99, 65, 40, 55, 99, 30, 0, 0, 84, 2, 0, 7, 4),
      OP(99, 45, 30, 55, 99, 60, 0, 0, 80, 2, 0, 9, 2), OP(99, 68, 40, 55, 99, 30, 0, 0, 74, 4, 0, 7, 4), OFF, OFF}},
    /* a pad: slow, two detuned carriers, the LFO in the pitch */
    {"DARK PAD", 4, 2, 28, 40, 5, 0, 4, 2,
     {OP(62, 30, 30, 35, 99, 95, 90, 0, 99, 1, 0, 4, 0), OP(55, 30, 30, 35, 90, 85, 80, 0, 72, 1, 0, 7, 1),
      OP(62, 30, 30, 35, 99, 95, 90, 0, 97, 1, 0, 10, 0), OP(55, 30, 30, 35, 90, 85, 80, 0, 70, 2, 0, 7, 1), OFF,
      OFF}},
    /* a growl: a modulator with the LFO in its level (AMS), feedback on top */
    {"GROWL BASS", 0, 7, 50, 0, 0, 60, 0, 0,
     {OP(99, 40, 40, 70, 99, 95, 95, 0, 99, 1, 0, 7, 1), OP(99, 40, 40, 70, 99, 95, 95, 0, 82, 1, 0, 7, 2),
      OP(99, 40, 40, 70, 99, 95, 95, 0, 88, 1, 0, 8, 1), OP(99, 40, 40, 70, 99, 90, 90, 0, 78, 2, 0, 7, 2),
      OP(99, 40, 40, 70, 99, 90, 90, 0, 70, 1, 0, 7, 2), OP(99, 40, 40, 70, 99, 90, 90, 0, 76, 1, 0, 8, 2)}},
    /* a clean sine sub, for under breaks */
    {"SINE SUB", 31, 0, 35, 0, 0, 0, 0, 0,
     {OP(99, 50, 40, 75, 99, 95, 95, 0, 99, 1, 0, 7, 1), OFF, OFF, OFF, OFF, OFF}},
    {"INIT VOICE", 0, 0, 35, 0, 0, 0, 0, 3, {OP(99, 99, 99, 99, 99, 99, 99, 0, 99, 1, 0, 7, 0), OFF, OFF, OFF, OFF, OFF}},
};
#undef OP
#undef OFF
#define FM_NFACT (sizeof FM_FACTORY / sizeof FM_FACTORY[0])
uint32_t dn_fm_factory_count(void) { return FM_NFACT; }
const char *dn_fm_factory_name(uint32_t i) { return FM_FACTORY[i < FM_NFACT ? i : FM_NFACT - 1u].name; }
void dn_fm_factory(uint32_t i, uint8_t *p)
{
    const fm_def_t *d = &FM_FACTORY[i < FM_NFACT ? i : FM_NFACT - 1u];
    uint32_t k, j;
    dn_fm_patch_init(p);
    for (k = 0; k < 6u; k++) {
        const fm_opdef_t *o = &d->op[k];
        uint8_t *q = p + 21u * (5u - k);            /* (OP1 is the last in the voice) */
        for (j = 0; j < 4u; j++)
            q[DN_FMO_R1 + j] = o->r[j], q[DN_FMO_L1 + j] = o->l[j];
        q[DN_FMO_OL] = o->ol;
        q[DN_FMO_COARSE] = o->coarse;
        q[DN_FMO_FINE] = o->fine;
        q[DN_FMO_DET] = o->det;
        q[DN_FMO_KVS] = o->kvs;
        q[DN_FMO_RS] = 2;                           /* (higher notes a little faster, as an acoustic sound) */
        if (d->amd && k > 0u)
            q[DN_FMO_AMS] = 2;
    }
    p[DN_FMV_ALG] = d->alg;
    p[DN_FMV_FB] = d->fb;
    p[DN_FMV_LFS] = d->lfs, p[DN_FMV_LFD] = d->lfd, p[DN_FMV_LPMD] = d->pmd, p[DN_FMV_LAMD] = d->amd;
    p[DN_FMV_LFW] = d->lfw, p[DN_FMV_LPMS] = d->pms;
    for (j = 0; j < 10u; j++)
        p[DN_FMV_NAME + j] = 32;
    for (j = 0; d->name[j] && j < 10u; j++)
        p[DN_FMV_NAME + j] = (uint8_t)d->name[j];
}

/* ---- the macros ------------------------------------------------------------------------------------------- */
static uint8_t fm_clamp99(int32_t v) { return (uint8_t)(v < 0 ? 0 : v > 99 ? 99 : v); }
void dn_fm_apply(const uint8_t *base, const dn_fm_macro_t *m, uint8_t *out)
{
    uint32_t i, op, alg = base[DN_FMV_ALG] & 31u;
    for (i = 0; i < DN_FM_PATCH; i++)
        out[i] = base[i];
    for (op = 0; op < 6u; op++) {
        uint8_t *o = out + 21u * op;
        int car = dn_fm_is_carrier(alg, op) != 0u;
        if (!car && m->bright && o[DN_FMO_OL])      /* BRIGHT: the modulators (an operator off stays off) */
            o[DN_FMO_OL] = fm_clamp99(o[DN_FMO_OL] + m->bright / 2);
        if (m->time)
            for (i = 0; i < 4u; i++)
                o[DN_FMO_R1 + i] = fm_clamp99(o[DN_FMO_R1 + i] + m->time / 2);
        if (car && m->attack)                       /* (+ slower: a longer attack) */
            o[DN_FMO_R1] = fm_clamp99(o[DN_FMO_R1] - m->attack);
        if (car && m->release)
            o[DN_FMO_R4] = fm_clamp99(o[DN_FMO_R4] - m->release);
    }
}
