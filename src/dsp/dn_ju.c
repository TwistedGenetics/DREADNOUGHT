/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: JUNO (0.36), a Juno-60 / 106-style polysynth (dn_ju.h says what it is). Copyright (C) 2026 Jamie
 * (TwistedGenetics). The ladder is the topology-preserving 4-pole (Zavalishin's "The Art of VA Filter Design"),
 * written here in float; the chorus's rates and sweeps follow published measurements of the Juno-60's. */
#include "dn_ju.h"
#include "dn_dsp.h"
#include "dsp/fastmath.h"

#define JU_TICK 32u
#define JU_FS 44100.0f

static const char *const JU_NAME[JP_N] = {"RANGE", "SAW", "PULSE", "SUB", "PWM", "PWM SRC", "NOISE", "LFO",
    "CUT", "RES", "ENV", "KEY", "HPF", "ENV POL", "LFO", "VEL", "ATK", "DEC", "SUS", "REL", "VCA", "VEL",
    "RATE", "DELAY", "SYNC", "MODE", "CHORUS", "LEVEL", "VOICES", "TRANS"};
static const int8_t JU_MIN[JP_N] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                    0, 0, 1, -24};
static const int8_t JU_MAX[JP_N] = {2, 1, 1, 127, 127, 2, 127, 127, 127, 127, 127, 127, 3, 1, 127, 127, 127, 127, 127,
                                    127, 1, 127, 127, 127, DN_JU_NDIV, 1, JC_N - 1, 127, DN_JU_NV, 24};
static const int16_t JU_INIT[JP_N] = {1, 1, 0, 0, 0, 0, 0, 0, 100, 0, 0, 64, 1, 0, 0, 0, 0, 40, 127, 30, 0, 30,
                                      60, 0, 0, 0, 0, 100, DN_JU_NV, 0};
/* a synced LFO: a cycle this many sixteenths long */
static const uint8_t JU_DIV[DN_JU_NDIV] = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64};
static const char *const JU_DIVN[DN_JU_NDIV] = {"1/16", "1/8", "1/8D", "1/4", "1/4D", "1/2", "1/2D", "1 BAR", "1.5 BR",
                                                "2 BAR", "3 BAR", "4 BAR"};

const char *dn_ju_vname(uint32_t k) { return k < JP_N ? JU_NAME[k] : ""; }
int32_t dn_ju_vmin(uint32_t k) { return k < JP_N ? JU_MIN[k] : 0; }
int32_t dn_ju_vmax(uint32_t k) { return k < JP_N ? JU_MAX[k] : 0; }
void dn_ju_defaults(int16_t *p)
{
    uint32_t k;
    for (k = 0; k < JP_N; k++)
        p[k] = JU_INIT[k];
}

static void ju_str(char *o, const char *s)
{
    uint32_t i;
    for (i = 0; s[i] && i < 7u; i++)
        o[i] = s[i];
    o[i] = 0;
}
static void ju_int(char *o, int32_t v)
{
    char t[8];
    uint32_t n = 0, i = 0, u = (uint32_t)(v < 0 ? -v : v);
    do t[n++] = (char)('0' + u % 10u); while ((u /= 10u) && n < 6u);
    if (v < 0)
        o[i++] = '-';
    while (n)
        o[i++] = t[--n];
    o[i] = 0;
}
void dn_ju_vtext(uint32_t k, int32_t v, const int16_t *p, char out[8])
{
    static const char *const RANGE[3] = {"16'", "8'", "4'"}, *const SRC[3] = {"LFO", "MAN", "ENV"};
    static const char *const HPF[4] = {"BOOST", "FLAT", "2", "3"}, *const CH[JC_N] = {"OFF", "I", "II", "I+II"};
    (void)p;
    v = v < dn_ju_vmin(k) ? dn_ju_vmin(k) : v > dn_ju_vmax(k) ? dn_ju_vmax(k) : v;
    switch (k) {
    case JP_RANGE: ju_str(out, RANGE[v]); return;
    case JP_SAW: case JP_PULSE: ju_str(out, v ? "ON" : "OFF"); return;
    case JP_PWMSRC: ju_str(out, SRC[v]); return;
    case JP_HPF: ju_str(out, HPF[v]); return;
    case JP_FPOL: ju_str(out, v ? "INV" : "NORM"); return;
    case JP_VCA: ju_str(out, v ? "GATE" : "ENV"); return;
    case JP_LSYNC: ju_str(out, v ? JU_DIVN[v - 1] : "OFF"); return;
    case JP_LMODE: ju_str(out, v ? "KEY" : "FREE"); return;
    case JP_CHORUS: ju_str(out, CH[v]); return;
    case JP_TRANS:
        if (v > 0) {
            out[0] = '+';
            ju_int(out + 1, v);
        } else
            ju_int(out, v);
        return;
    default: ju_int(out, v); return;
    }
}

/* ---- the factory sounds: INIT, then each as (value, setting) pairs over INIT's, -1 ending them ---------------- */
typedef struct { const char *name; int16_t kv[40]; } ju_preset_t;
static const ju_preset_t JU_PRESET[] = {
    {"INIT", {-1}},
    {"JUNO PAD", {JP_PULSE, 1, JP_PWM, 70, JP_CUT, 70, JP_RES, 20, JP_FENV, 30, JP_ATK, 70, JP_DEC, 80, JP_SUS, 100,
                  JP_REL, 85, JP_LRATE, 45, JP_LDELAY, 30, JP_CHORUS, JC_II, -1}},
    {"BRASS", {JP_CUT, 45, JP_RES, 15, JP_FENV, 75, JP_FKEY, 80, JP_ATK, 25, JP_DEC, 60, JP_SUS, 80, JP_REL, 40,
               JP_CHORUS, JC_I, -1}},
    {"STRINGS", {JP_SAW, 0, JP_PULSE, 1, JP_PWM, 90, JP_LRATE, 55, JP_CUT, 85, JP_HPF, 2, JP_ATK, 75, JP_DEC, 60,
                 JP_SUS, 120, JP_REL, 85, JP_CHORUS, JC_II, -1}},
    {"106 BASS", {JP_RANGE, 0, JP_SAW, 0, JP_PULSE, 1, JP_PWM, 40, JP_PWMSRC, 1, JP_SUB, 127, JP_CUT, 35, JP_RES, 40,
                  JP_FENV, 60, JP_DEC, 55, JP_SUS, 0, JP_REL, 20, JP_HPF, 0, JP_VOICES, 1, -1}},
    {"ORGAN", {JP_SAW, 0, JP_PULSE, 1, JP_SUB, 100, JP_CUT, 110, JP_VCA, 1, JP_REL, 10, JP_LEVEL, 75, JP_CHORUS, JC_I, -1}},
    {"RAVE STAB", {JP_PULSE, 1, JP_PWM, 60, JP_PWMSRC, 1, JP_CUT, 60, JP_RES, 70, JP_FENV, 70, JP_DEC, 50,
                   JP_SUS, 20, JP_REL, 35, JP_CHORUS, JC_I_II, -1}},
    {"PLUCK", {JP_CUT, 40, JP_RES, 30, JP_FENV, 80, JP_FVEL, 40, JP_DEC, 45, JP_SUS, 0, JP_REL, 45, JP_CHORUS, JC_II,
               -1}},
    {"PIANO", {JP_PULSE, 1, JP_PWM, 30, JP_PWMSRC, 1, JP_CUT, 55, JP_FENV, 60, JP_FKEY, 100, JP_DEC, 85, JP_SUS, 0,
               JP_REL, 60, JP_VVEL, 80, JP_CHORUS, JC_I, -1}},
    {"SUB BASS", {JP_RANGE, 0, JP_SAW, 0, JP_SUB, 127, JP_CUT, 40, JP_DEC, 60, JP_REL, 15, JP_HPF, 0, JP_VOICES, 1,
                  -1}},
    {"ACID POLY", {JP_CUT, 30, JP_RES, 95, JP_FENV, 85, JP_DEC, 40, JP_SUS, 10, JP_REL, 25, JP_VOICES, 3, -1}},
    {"GLASS", {JP_RANGE, 2, JP_SAW, 0, JP_PULSE, 1, JP_PWM, 100, JP_PWMSRC, 2, JP_HPF, 3, JP_CUT, 95, JP_RES, 60,
               JP_ATK, 5, JP_DEC, 90, JP_SUS, 30, JP_REL, 90, JP_CHORUS, JC_I_II, -1}},
    {"DARK PAD", {JP_SUB, 60, JP_CUT, 35, JP_RES, 55, JP_FLFO, 40, JP_LRATE, 20, JP_LDELAY, 60, JP_ATK, 90,
                  JP_REL, 95, JP_CHORUS, JC_II, -1}},
    {"NOISE SWEEP", {JP_SAW, 0, JP_NOISE, 127, JP_CUT, 50, JP_RES, 90, JP_FLFO, 90, JP_LRATE, 30, JP_ATK, 60,
                     JP_REL, 80, JP_CHORUS, JC_II, -1}},
    {"WHISTLE", {JP_SAW, 0, JP_CUT, 60, JP_RES, 127, JP_FKEY, 127, JP_ATK, 40, JP_REL, 50, JP_CHORUS, JC_I, -1}},
    {"HOOVERISH", {JP_RANGE, 0, JP_PULSE, 1, JP_PWM, 110, JP_LRATE, 80, JP_DLFO, 30, JP_LDELAY, 40, JP_CUT, 80,
                   JP_RES, 30, JP_FENV, 20, JP_VOICES, 2, JP_CHORUS, JC_I_II, -1}},
    {"JUNGLE STAB", {JP_PULSE, 1, JP_PWM, 50, JP_PWMSRC, 1, JP_SUB, 80, JP_CUT, 70, JP_RES, 35, JP_FENV, 55,
                     JP_DEC, 45, JP_SUS, 0, JP_REL, 30, JP_HPF, 2, JP_CHORUS, JC_I, -1}},
};
#define JU_NPRESET (sizeof JU_PRESET / sizeof JU_PRESET[0])
uint32_t dn_ju_preset_count(void) { return JU_NPRESET; }
const char *dn_ju_preset_name(uint32_t i) { return JU_PRESET[i % JU_NPRESET].name; }
void dn_ju_preset(uint32_t i, int16_t *p)
{
    const int16_t *kv = JU_PRESET[i % JU_NPRESET].kv;
    uint32_t j;
    dn_ju_defaults(p);
    for (j = 0; j + 1u < 40u && kv[j] >= 0; j += 2u)
        if (kv[j] < (int16_t)JP_N)
            p[kv[j]] = kv[j + 1u];
}

void dn_ju_init(dn_ju_t *s)
{
    uint32_t k;
    uint8_t *b = (uint8_t *)s;
    for (k = 0; k < sizeof *s; k++)
        b[k] = 0;
    dn_ju_defaults(s->p);
    s->nz = 0x2468ACEu;
    s->bpm = 172;
    s->lfo = 0.0f;
}

uint32_t dn_ju_voices(const dn_ju_t *s)
{
    int32_t n = s->p[JP_VOICES];
    return n < 1 ? 1u : n > (int32_t)DN_JU_NV ? DN_JU_NV : (uint32_t)n;
}
uint32_t dn_ju_sounding(const dn_ju_t *s)
{
    uint32_t k, n = 0;
    for (k = 0; k < DN_JU_NV; k++)
        n += s->v[k].live;
    return n;
}

void dn_ju_note_on(dn_ju_t *s, uint8_t note, uint8_t vel)
{
    uint32_t k, nv = dn_ju_voices(s), best = 0, bs = 0xFFFFFFFFu;
    dn_ju_v_t *v;
    if (!s->nheld) {                                /* the first key: the LFO's DELAY from now (KEY: its cycle too) */
        s->lt = 0;
        if (s->p[JP_LMODE])
            s->lph = 0xC0000000u;                   /* (the triangle at 0, rising) */
    }
    s->nheld++;
    for (k = 0; k < nv; k++)                        /* the voice: one on this note, a free one, the oldest let go, the
                                                     * oldest */
        if (s->v[k].live && s->v[k].note == note) {
            best = k, bs = 0;
            break;
        }
    if (bs) {
        for (k = 0; k < nv; k++) {
            const dn_ju_v_t *w = &s->v[k];
            uint32_t sc = !w->live ? 0u : !w->gate ? 1u + (w->seq & 0x7FFFFFFu) : 0x8000000u + (w->seq & 0x7FFFFFFu);
            if (sc < bs)
                bs = sc, best = k;
        }
    }
    v = &s->v[best];
    if (!v->live) {
        s->nz = s->nz * 1664525u + 1013904223u;
        v->ph = s->nz;                              /* (the DCOs run free: no two voices in phase) */
        v->sph = v->ph >> 1;
        v->env = 0.0f, v->amp = 0.0f, v->G = -1.0f;
        v->s[0] = 1e-3f, v->s[1] = v->s[2] = v->s[3] = 0.0f;   /* (a breath in it: a ringing filter starts alone) */
    }
    v->live = 1, v->gate = 1, v->note = note, v->vel = vel ? vel : 1u, v->st = 0;
    v->seq = ++s->seq;
}
void dn_ju_note_off(dn_ju_t *s, uint8_t note)
{
    uint32_t k;
    for (k = 0; k < DN_JU_NV; k++)
        if (s->v[k].live && s->v[k].gate && s->v[k].note == note)
            s->v[k].gate = 0, s->v[k].st = 2;
    if (s->nheld)
        s->nheld--;
}
void dn_ju_all_off(dn_ju_t *s)
{
    uint32_t k;
    for (k = 0; k < DN_JU_NV; k++)
        if (s->v[k].live)
            s->v[k].gate = 0, s->v[k].st = 2;
    s->nheld = 0;
}
void dn_ju_cut(dn_ju_t *s)
{
    uint32_t k;
    for (k = 0; k < DN_JU_NV; k++)
        s->v[k].live = 0, s->v[k].gate = 0;
    s->nheld = 0;
}

static uint32_t ju_inc(float semis)                 /* the phase step of a pitch in semitones (MIDI note numbers) */
{
    int32_t n = (int32_t)(semis < 0.0f ? semis - 1.0f : semis);
    float f;
    if (n < 0) n = 0, semis = 0.0f;
    if (n > 126) n = 126, semis = 126.0f;
    f = semis - (float)n;
    return (uint32_t)((float)DN_NOTE_INC[n] * fm_exp2f(f * (1.0f / 12.0f)));
}
static inline float ju_k(int32_t v)                 /* an envelope time's step a tick (1 ms .. 10 s), 0..1 */
{
    return (float)DN_ENV_K[v < 0 ? 0 : v > 127 ? 127 : v] * (1.0f / 65536.0f);
}
/* polyBLEP residual around a wrap: ph and inc as the accumulator's, ri = 1 / inc */
static inline float ju_blep(uint32_t ph, uint32_t inc, float ri)
{
    float x;
    if (ph < inc) {
        x = (float)ph * ri;
        return x + x - x * x - 1.0f;
    }
    if (ph > ~inc) {
        x = -(float)(~ph + 1u) * ri;
        return x * x + x + x + 1.0f;
    }
    return 0.0f;
}
static inline float ju_sat(float u)                 /* a soft clip (the ladder's input): u - 4u^3/27, flat past 1.5 */
{
    if (u > 1.5f) return 1.0f;
    if (u < -1.5f) return -1.0f;
    return u - 0.148148f * u * u * u;
}

typedef struct { float G, dG, inv, dinv, a, da; dn_ju_v_t *v; } ju_f_t;   /* a voice's filter and level, a tick */
void dn_ju_render(dn_ju_t *s, int32_t *lr, uint32_t n)
{
    static float mb[JU_TICK], jx[DN_JU_NV + 1u][JU_TICK];
    static ju_f_t jf[DN_JU_NV + 1u];
    uint32_t i0 = 0;
    const int16_t *p = s->p;
    while (i0 < n) {
        uint32_t len = n - i0 < JU_TICK ? n - i0 : JU_TICK, k, i, busy = 0;
        float rl = 1.0f / (float)len, lfo, tri, pwm_m, kres = (float)p[JP_RES] * (4.6f / 127.0f);   /* (past 4 it
                                                     * rings on its own) */
        uint32_t nf = 0;
        for (i = 0; i < len; i++)
            mb[i] = 0.0f;
        {   /* the LFO: its rate (free, or the tempo's), the triangle, faded in after the first key (DELAY) */
            float hz, d = (float)p[JP_LDELAY] * (1.0f / 127.0f), amt = 1.0f, t;
            if (p[JP_LSYNC] > 0)
                hz = (float)(s->bpm ? s->bpm : 120u) * (4.0f / 60.0f) / (float)JU_DIV[(p[JP_LSYNC] - 1) % DN_JU_NDIV];
            else
                hz = 0.1f * fm_exp2f((float)p[JP_LRATE] * (8.229f / 127.0f));   /* 0.1 .. 30 Hz */
            s->lph += (uint32_t)(hz * ((float)JU_TICK / JU_FS) * 4294967296.0f);
            t = (float)(s->lph >> 8) * (1.0f / 16777216.0f);
            tri = 4.0f * fm_fabsf(t - 0.5f) - 1.0f;
            if (d > 0.0f) {                         /* DELAY: up to 3 s still, then as long fading in */
                float sec = (float)s->lt * ((float)JU_TICK / JU_FS), dl = 3.0f * d * d;
                amt = (sec - dl) / (dl > 0.05f ? dl : 0.05f);
                amt = amt < 0.0f ? 0.0f : amt > 1.0f ? 1.0f : amt;
            }
            if (s->lt < 0x7FFFFFFFu)
                s->lt++;
            lfo = s->lfo = tri * amt;
        }
        pwm_m = (float)p[JP_PWM] * (0.46f / 127.0f);
        for (k = 0; k < DN_JU_NV; k++) {
            dn_ju_v_t *v = &s->v[k];
            float pitch, a1, da, G1, dG, c, f, g, ri, rs, pw, sawl, pul, subl, nzl, comp, G, a, inv0, inv1, inv, dinv;
            uint32_t pwph;
            if (!v->live)
                continue;
            busy++;
            /* the envelope (ONE, the Juno's), a tick on */
            if (v->st == 0) {
                v->env += (1.3f - v->env) * ju_k(p[JP_ATK]);
                if (v->env >= 1.0f)
                    v->env = 1.0f, v->st = 1;
            } else if (v->st == 1) {
                v->env += ((float)p[JP_SUS] * (1.0f / 127.0f) - v->env) * ju_k(p[JP_DEC]);
            } else {
                v->env -= v->env * ju_k(p[JP_REL]);
            }
            {   /* the level: the envelope, or the gate (VCA GATE: quick either way), less for a soft note (VEL) */
                float vv = (float)p[JP_VVEL] * (1.0f / 127.0f), vf = 1.0f - vv * (1.0f - (float)v->vel * (1.0f / 127.0f));
                a1 = (p[JP_VCA] ? (v->gate ? 1.0f : 0.0f) : v->env) * vf;
                if (p[JP_VCA] && !v->gate)
                    a1 = v->amp * 0.5f;             /* (GATE: let go, gone in a few ticks) */
                if (!v->gate && (p[JP_VCA] ? v->amp < 1e-4f : v->env < 1e-4f)) {
                    v->live = 0;                    /* (silent: free) */
                    continue;
                }
            }
            /* the pitch: the note, TRANS, RANGE, the LFO (DCO LFO) */
            pitch = (float)v->note + (float)p[JP_TRANS] + 12.0f * (float)(p[JP_RANGE] - 1);
            {
                float dl = (float)p[JP_DLFO] * (1.0f / 127.0f);
                pitch += lfo * dl * dl * 7.0f;
            }
            v->inc = ju_inc(pitch);
            v->sinc = v->inc >> 1;
            ri = 1.0f / (float)(v->inc ? v->inc : 1u);
            rs = 1.0f / (float)(v->sinc ? v->sinc : 1u);
            /* the cutoff: CUT (30 Hz .. 16 kHz), the envelope (+ or inverted), the LFO, the keys, the velocity */
            c = (float)p[JP_CUT] * (108.7f / 127.0f)
              + (float)p[JP_FENV] * (96.0f / 127.0f) * v->env * (p[JP_FPOL] ? -1.0f : 1.0f)
              + (float)p[JP_FLFO] * (36.0f / 127.0f) * lfo
              + ((float)v->note - 60.0f) * (float)p[JP_FKEY] * (1.0f / 127.0f)
              + (float)p[JP_FVEL] * (24.0f / 127.0f) * ((float)v->vel - 64.0f) * (1.0f / 63.0f);
            f = 30.0f * fm_exp2f(c * (1.0f / 12.0f));
            f = f < 20.0f ? 20.0f : f > 18000.0f ? 18000.0f : f;
            g = fm_tanf(3.14159265f * f / JU_FS);
            G1 = g / (1.0f + g);
            if (v->G < 0.0f)
                v->G = G1;
            comp = 1.0f + 0.4f * kres;              /* (a little of the bass the resonance takes, back) */
            {
                float G4a = v->G * v->G * v->G * v->G, G4b = G1 * G1 * G1 * G1;
                inv0 = 1.0f / (1.0f + kres * G4a), inv1 = 1.0f / (1.0f + kres * G4b);
            }
            G = v->G, dG = (G1 - v->G) * rl, inv = inv0, dinv = (inv1 - inv0) * rl;
            a = v->amp, da = (a1 - v->amp) * rl;
            v->G = G1, v->amp = a1;
            /* the DCO: the pulse's width (PWM by the LFO, by hand, or by the envelope), the mixer */
            pw = 0.5f - pwm_m * (p[JP_PWMSRC] == 0 ? (lfo + 1.0f) * 0.5f : p[JP_PWMSRC] == 1 ? 1.0f : v->env);
            pwph = (uint32_t)(pw * 4294967295.0f);
            sawl = p[JP_SAW] ? 0.5f : 0.0f;
            pul = p[JP_PULSE] ? 0.5f : 0.0f;
            subl = (float)p[JP_SUB] * (0.5f / 127.0f);
            nzl = (float)p[JP_NOISE] * (0.4f / 127.0f);
            {   /* the DCO's mix for the tick (the filter after, all the voices together) */
                uint32_t ph = v->ph, sph = v->sph, inc = v->inc, sinc = v->sinc, nz = s->nz;
                float *xb = jx[nf];
                for (i = 0; i < len; i++) {
                    float x = 0.0f;
                    ph += inc, sph += sinc;
                    if (sawl > 0.0f)                /* saw: a rising ramp */
                        x += sawl * ((float)(ph >> 8) * (2.0f / 16777216.0f) - 1.0f - ju_blep(ph, inc, ri));
                    if (pul > 0.0f) {               /* pulse: high for the first pw of the cycle */
                        uint32_t q = ph - pwph;
                        x += pul * ((ph < pwph ? 1.0f : -1.0f) + ju_blep(ph, inc, ri) - ju_blep(q, inc, ri));
                    }
                    if (subl > 0.0f) {              /* the sub: a square an octave down */
                        uint32_t q = sph + 0x80000000u;
                        x += subl * ((sph < 0x80000000u ? 1.0f : -1.0f) + ju_blep(sph, sinc, rs) - ju_blep(q, sinc, rs));
                    }
                    if (nzl > 0.0f) {
                        nz = nz * 1664525u + 1013904223u;
                        x += nzl * (float)(int32_t)nz * (1.0f / 2147483648.0f);
                    }
                    xb[i] = x * comp;
                }
                v->ph = ph, v->sph = sph, s->nz = nz;
                jf[nf].G = G, jf[nf].dG = dG, jf[nf].inv = inv, jf[nf].dinv = dinv, jf[nf].a = a, jf[nf].da = da;
                jf[nf].v = v;
                nf++;
            }
        }
        if (nf & 1u) {                              /* (the filters go in pairs: a silent one beside an odd one) */
            static dn_ju_v_t pad;
            for (i = 0; i < len; i++)
                jx[nf][i] = 0.0f;
            pad.s[0] = pad.s[1] = pad.s[2] = pad.s[3] = 0.0f;
            jf[nf].G = 0.5f, jf[nf].dG = 0.0f, jf[nf].inv = 1.0f, jf[nf].dinv = 0.0f, jf[nf].a = 0.0f, jf[nf].da = 0.0f;
            jf[nf].v = &pad;
            nf++;
        }
        for (k = 0; k < nf; k += 2u) {              /* the ladders, two voices at once (their sums interleave: a core
                                                     * that can't reorder still has two chains to work on) */
            ju_f_t *A = &jf[k], *B = &jf[k + 1u];
            float as0 = A->v->s[0], as1 = A->v->s[1], as2 = A->v->s[2], as3 = A->v->s[3];
            float bs0 = B->v->s[0], bs1 = B->v->s[1], bs2 = B->v->s[2], bs3 = B->v->s[3];
            float aG = A->G, aI = A->inv, aA = A->a, bG = B->G, bI = B->inv, bA = B->a;
            const float *ax = jx[k], *bx = jx[k + 1u];
            for (i = 0; i < len; i++) {
                float ab = 1.0f - aG, aG2 = aG * aG, aG3 = aG2 * aG, aG4 = aG2 * aG2;
                float bb = 1.0f - bG, bG2 = bG * bG, bG3 = bG2 * bG, bG4 = bG2 * bG2;
                float aS = ab * (aG3 * as0 + aG2 * as1 + aG * as2 + as3), bS = bb * (bG3 * bs0 + bG2 * bs1 + bG * bs2 + bs3);
                float ay = (aG4 * ax[i] + aS) * aI, by = (bG4 * bx[i] + bS) * bI;
                float au = ju_sat(ax[i] - kres * ay), bu = ju_sat(bx[i] - kres * by);
                float a0 = aG * au + ab * as0, b0 = bG * bu + bb * bs0, a1_, b1_, a2, b2, a3, b3;
                as0 = a0 + a0 - as0, bs0 = b0 + b0 - bs0;
                a1_ = aG * a0 + ab * as1, b1_ = bG * b0 + bb * bs1;
                as1 = a1_ + a1_ - as1, bs1 = b1_ + b1_ - bs1;
                a2 = aG * a1_ + ab * as2, b2 = bG * b1_ + bb * bs2;
                as2 = a2 + a2 - as2, bs2 = b2 + b2 - bs2;
                a3 = aG * a2 + ab * as3, b3 = bG * b2 + bb * bs3;
                as3 = a3 + a3 - as3, bs3 = b3 + b3 - bs3;
                mb[i] += a3 * aA + b3 * bA;
                aG += A->dG, aI += A->dinv, aA += A->da, bG += B->dG, bI += B->dinv, bA += B->da;
            }
            A->v->s[0] = fm_flush(as0), A->v->s[1] = fm_flush(as1), A->v->s[2] = fm_flush(as2), A->v->s[3] = fm_flush(as3);
            B->v->s[0] = fm_flush(bs0), B->v->s[1] = fm_flush(bs1), B->v->s[2] = fm_flush(bs2), B->v->s[3] = fm_flush(bs3);
        }
        s->busy = busy;
        {   /* the HPF (on the voices together, as the 106's), the chorus, out */
            static const float HA[4] = {0.01415f, 0.0f, 0.0322f, 0.0975f};   /* (one-poles at 100, -, 230, 720 Hz) */
            uint32_t hp = (uint32_t)p[JP_HPF] & 3u, ch = (uint32_t)p[JP_CHORUS] % JC_N;
            float og = (float)p[JP_LEVEL] * (0.3f / 100.0f) * 32767.0f, hl = s->hlp, ha = HA[hp];
            static const float CR[JC_N] = {0.0f, 0.513f, 0.863f, 9.75f}, CD[JC_N] = {0.0f, 80.0f, 80.0f, 9.0f};
            uint32_t cinc = (uint32_t)(CR[ch] * (1.0f / JU_FS) * 4294967296.0f);
            float cdep = CD[ch], cl = s->clp[0], cr = s->clp[1];
            for (i = 0; i < len; i++) {
                float x = mb[i], L, R;
                if (hp != 1u) {
                    hl += ha * (x - hl);
                    x = hp == 0u ? x + 0.6f * hl : x - hl;
                }
                if (ch) {
                    float t, tri2, dl, dr, fl, fr;
                    uint32_t il, ir, w = s->cw;
                    s->cb[w & (DN_JU_CB - 1u)] = (int16_t)(x > 1.99f ? 32600 : x < -1.99f ? -32600 : (int32_t)(x * 16384.0f));
                    s->cph += cinc;
                    t = (float)(s->cph >> 8) * (1.0f / 16777216.0f);
                    tri2 = 4.0f * fm_fabsf(t - 0.5f) - 1.0f;
                    dl = 155.0f + cdep * tri2, dr = 155.0f - cdep * tri2;
                    il = (uint32_t)dl, ir = (uint32_t)dr;
                    fl = dl - (float)il, fr = dr - (float)ir;
                    L = ((float)s->cb[(w - il) & (DN_JU_CB - 1u)] * (1.0f - fl) + (float)s->cb[(w - il - 1u) & (DN_JU_CB - 1u)] * fl)
                      * (1.0f / 16384.0f);
                    R = ((float)s->cb[(w - ir) & (DN_JU_CB - 1u)] * (1.0f - fr) + (float)s->cb[(w - ir - 1u) & (DN_JU_CB - 1u)] * fr)
                      * (1.0f / 16384.0f);
                    cl += 0.63f * (L - cl), cr += 0.63f * (R - cr);   /* (the BBD's darkness: ~7 kHz) */
                    s->cw = w + 1u;
                    L = 0.72f * (x + cl), R = 0.72f * (x + cr);
                } else {
                    L = R = x;
                }
                lr[2u * (i0 + i)] += (int32_t)(L * og);
                lr[2u * (i0 + i) + 1u] += (int32_t)(R * og);
            }
            s->hlp = fm_flush(hl), s->clp[0] = fm_flush(cl), s->clp[1] = fm_flush(cr);
        }
        i0 += len;
    }
}
