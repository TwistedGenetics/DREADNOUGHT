/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: the REESE engine. Copyright (C) 2026 Jamie (TwistedGenetics).
 * The ladder (rs_ladder_coef / rs_ladder_tick) and the supersaw spacing are Clipper's (isod89/clipper-fm1
 * firmware/src/dsp.c ladder_*, eng_hybrid.c SUPER_OFF, Copyright (C) 2026 3dSam, GPL-3.0-only), copied with their
 * notes; the rest is DREADNOUGHT's. Why it is built this way, with sources: docs/REESE.md.
 *
 * Two or more oscillators detuned against each other: their sum is a comb filter that moves up the spectrum as
 * they drift in and out of phase (the Casio CZ's LINE 1+1', the 1988 original). Then, in the order producers use:
 * a crossover that takes the low end away from the oscillators (they cancel each other there and the bottom
 * pumps), drive on what's left, a Moog-style ladder, a swept notch comb, and a clean mono sine on the fundamental
 * added last. Mono, legato with glide; every oscillator drifts a little on its own.
 *
 * The patch (p[]):
 *   OSC   0..5   2SAW 3SAW 5SAW 7SAW SAWSQ 2SIN
 *   DTN   0..127 the total spread in cents, 0..100, x^2 (64 = 25 cents, 90 = 50, 127 = 100)
 *   BEAT  0..7   OFF, or the pair beats at 1/1 1/2 1/4 1/8 1/16 1/4T 1/8T of the tempo on every note
 *   SUB   0..127 a clean sine on the fundamental; above 0 the oscillators are high-passed at ~120 Hz (24 dB)
 *   CUT   0..127 the ladder's cutoff, 30 Hz .. 16 kHz (62 = 650 Hz, 71 = 1 kHz, 79 = 1.5 kHz, 85 = 2 kHz)
 *   RES   0..127 its resonance
 *   DRV   0..127 drive before the ladder, above the crossover only
 *   NTCH  0..127 a notch comb swept slowly; 0 off
 *   ATK DEC SUS REL 0..127  the amplitude envelope (times 1 ms .. 10 s)
 *   FENV  -64..63 the envelope on the cutoff (an attack-decay with the amp's attack and decay)
 *   GLIDE 0..127 the slide between legato notes (0 none)
 *   LRATE 0..127 the filter LFO's rate (0.05 .. 20 Hz); LFLT -64..63 its depth on the cutoff (0 off)
 *   LEVEL 0..127 (100 = unity); TRANS -36..36 semitones (the presets play an octave or two down) */
#include "reese.h"
#include "dn_dsp.h"

#define RS_COMB_MIN 24                    /* the comb sweep in samples: 0.5 .. 4.5 ms */
#define RS_COMB_MAX 200
#define RS_XOVER_CUT (28 << 8)            /* 118 Hz */
#define RS_XOVER_K 5798                   /* ~sqrt 2: Butterworth */
#define RS_DRIFT 30                       /* the drift walk (sd ~1.5 cents) */
#define RS_FULL (1 << 24)                 /* envelopes, Q24 */

/* ---------------------------------------------------------------------------------------------- the patch --- */
static const char *const RP_NAME[RP_COUNT] = {"OSC", "DTN", "BEAT", "SUB", "CUT", "RES", "DRV", "NTCH",
                                              "ATK", "DEC", "SUS", "REL", "FENV", "GLIDE", "LRATE", "LFLT",
                                              "LEVEL", "TRANS"};
static const int8_t RP_MIN[RP_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -64, 0, 0, -64, 0, -36};
static const int8_t RP_MAX[RP_COUNT] = {RO_N - 1, 127, 7, 127, 127, 127, 127, 127, 127, 127, 127, 127, 63, 127,
                                        127, 63, 127, 36};
static const char *const RO_NAME[RO_N] = {"2SAW", "3SAW", "5SAW", "7SAW", "SAWSQ", "2SIN"};
static const char *const RB_NAME[8] = {"OFF", "1/1", "1/2", "1/4", "1/8", "1/16", "1/4T", "1/8T"};

const char *reese_param_name(uint32_t k) { return k < RP_COUNT ? RP_NAME[k] : ""; }
int32_t reese_param_min(uint32_t k) { return k < RP_COUNT ? RP_MIN[k] : 0; }
int32_t reese_param_max(uint32_t k) { return k < RP_COUNT ? RP_MAX[k] : 0; }

static void put_int(char *o, int32_t v)
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
static void put_str(char *o, const char *s)
{
    uint32_t i;
    for (i = 0; s[i] && i < 7u; i++)
        o[i] = s[i];
    o[i] = 0;
}
void reese_param_text(uint32_t k, int32_t v, char out[8])
{
    switch (k) {
    case RP_OSC: put_str(out, RO_NAME[dn_clamp(v, 0, RO_N - 1)]); break;
    case RP_BEAT: put_str(out, RB_NAME[dn_clamp(v, 0, 7)]); break;
    case RP_DTN: {                                       /* in cents */
        int32_t c = v * v * 100 / (127 * 127), i = 0;
        put_int(out, c);
        while (out[i]) i++;
        out[i] = 'c', out[i + 1] = 0;
        break;
    }
    default: put_int(out, v); break;
    }
}

/* ---------------------------------------------------------------------------------------------- presets --- */
#define RS_P(n, osc, dtn, beat, sub, cut, res, drv, ntch, a, d, s, r, fenv, gl, lr, lf, tr) \
    {n, {osc, dtn, beat, sub, cut, res, drv, ntch, a, d, s, r, fenv, gl, lr, lf, 100, tr}}
static const reese_preset_t PRESETS[] = {
    /* the jungle classic: two saws ~30 cents apart, low-passed ~900 Hz, a clean sub, a little drive */
    RS_P("CLASSIC RS", RO_2SAW, 70, 0, 80, 68, 18, 24, 0, 0, 70, 120, 30, 0, 36, 0, 0, -24),
    /* the 1988 original's idea: two detuned sines (+-27 cents), saturated for harmonics, no separate sub */
    RS_P("OG REESE", RO_2SIN, 93, 0, 0, 76, 8, 46, 0, 0, 70, 124, 28, 0, 40, 0, 0, -24),
    /* the classic with a slow notch sweep: the hollow, phasing vowel */
    RS_P("HOLLOW RS", RO_2SAW, 80, 0, 80, 70, 20, 30, 80, 0, 70, 120, 30, 0, 36, 0, 0, -24),
    /* techstep: a saw against a square, driven hard, a notch sweep and a slow filter LFO */
    RS_P("TECHSTEP", RO_SAWSQ, 90, 0, 76, 79, 30, 96, 50, 0, 80, 116, 28, 12, 30, 40, 14, -24),
    /* neuro: five saws spread 75 cents, the ladder resonating, the drive folding, a faster filter wobble */
    RS_P("NEURO RS", RO_5SAW, 110, 0, 80, 74, 52, 120, 70, 0, 80, 110, 26, 24, 0, 80, 34, -24),
    /* the beat locked to quarter notes at the tempo on every note */
    RS_P("SYNC RS", RO_2SAW, 70, 3, 80, 68, 20, 30, 40, 0, 70, 120, 30, 0, 36, 0, 0, -24),
    /* dark and slow: three saws only 15 cents apart, low-passed low, a heavy sub */
    RS_P("DARK RS", RO_3SAW, 50, 0, 96, 52, 24, 40, 30, 0, 70, 124, 34, 0, 40, 0, 0, -24),
    /* mostly sub: a narrow pair (10 cents) colouring a big sine */
    RS_P("SUB REESE", RO_2SAW, 40, 0, 110, 56, 10, 20, 0, 0, 70, 124, 28, 0, 36, 0, 0, -24),
    /* a bright seven-saw spread a whole semitone wide and a long glide: the hoover side of the family */
    RS_P("HOOVER RS", RO_7SAW, 127, 0, 30, 96, 10, 60, 40, 0, 80, 110, 38, 0, 60, 0, 0, -12),
};
#define NPRESETS (sizeof PRESETS / sizeof PRESETS[0])
uint32_t reese_preset_count(void) { return NPRESETS; }
const char *reese_preset_name(uint32_t i) { return PRESETS[i % NPRESETS].name; }
void reese_load_preset(reese_t *r, uint32_t i)
{
    uint32_t k;
    for (k = 0; k < RP_COUNT; k++)
        r->p[k] = PRESETS[i % NPRESETS].v[k];
}

void reese_init(reese_t *r)
{
    uint32_t i;
    uint8_t *b = (uint8_t *)r;
    for (i = 0; i < sizeof *r; i++)
        b[i] = 0;
    r->bpm = 172;
    r->rnd = 0x5EE5E;
    reese_load_preset(r, 0);
}

/* ------------------------------------------------------------------------ the ladder, from Clipper --- */
/* Zavalishin's zero-delay-feedback ladder: four one-pole TPT stages, the feedback solved, the input through a tanh
 * (the transistor ladder's differential pair): a hot signal drives it warm, the resonance saturates softly. Signals
 * Q12, coefficients Q16, products and sums in 64 bits; G <= 0.75 keeps the states bounded. */
typedef struct { int32_t G, H, G4, inv, k; } rs_ladder_t;
static inline int32_t rs_mul_sh(int32_t a, int32_t b, int sh) { return (int32_t)(((int64_t)a * b) >> sh); }
static void rs_ladder_coef(rs_ladder_t *c, int32_t cut, int32_t res)
{
    int32_t g = dn_clamp(dn_svf_g(cut), 1, 3 * 4096), G, G2;
    G = (int32_t)(((uint32_t)g << 16) / (uint32_t)(4096 + g));   /* g / (1 + g) */
    G2 = rs_mul_sh(G, G, 16);
    c->G = G;
    c->H = 65536 - G;
    c->G4 = rs_mul_sh(G2, G2, 16);
    c->k = dn_clamp(res, 0, 127) * 16900 / 127;                  /* 0 .. 4.13 */
    c->inv = (int32_t)(0xFFFFFFFFu / (uint32_t)(65536 + rs_mul_sh(c->k, c->G4, 12)));   /* (32-bit divide only) */
}
static inline int32_t rs_ladder_tick(const rs_ladder_t *c, int32_t x, int32_t *s)
{
    int64_t in = (int64_t)dn_clamp(x, -131072, 131072) << 12, S, y, u;
    int32_t G = c->G, uq, v, j;
    in += (in * c->k) >> 13;                                     /* x (1 + k / 2): the passband kept at high RES */
    S = ((int64_t)G * s[0] >> 16) + s[1];
    S = ((G * S) >> 16) + s[2];
    S = ((G * S) >> 16) + s[3];
    S = (c->H * S) >> 16;
    y = ((((int64_t)c->G4 * in) >> 16) + S) * c->inv >> 16;    /* the ladder's output, solved */
    u = in - ((c->k * y) >> 12);                                 /* the ladder's input (Q12) */
    {
        int64_t h = u >> 13;
        if (h <= -65536 || h >= 65536)
            uq = dn_softclip(h < 0 ? -65536 : 65536) << 13;
        else
            uq = (dn_softclip((int32_t)h) << 13) + (int32_t)(u - (h << 13));
    }
    for (j = 0; j < 4; j++) {
        v = (int32_t)(((int64_t)G * (uq - s[j]) + 32768) >> 16);
        uq = v + s[j];
        s[j] = uq + v;
    }
    return (uq + 2048) >> 12;
}

/* ---------------------------------------------------------------------------------------- the oscillators --- */
/* where each oscillator sits in the spread, Q12 of half the spread; the stacks use the supersaw's spacing (Clipper's
 * SUPER_OFF, after Adam Szabo's JP-8000 analysis), scaled so the outer pair spans the spread */
static const uint8_t RS_N[RO_N] = {2, 3, 5, 7, 2, 2};
static const int16_t RS_POS[RO_N][RS_NOSC] = {
    {-4096, 4096},
    {0, -4096, 4096},
    {0, 1297, -1271, 4049, -4096},
    {0, 741, -727, 2314, -2341, 4000, -4096},
    {-4096, 4096},
    {-4096, 4096},
};
static const int16_t RS_GAIN[RO_N] = {23000, 19000, 13000, 9400, 23000, 26000};
static const uint8_t RS_BEAT4[8] = {0, 1, 2, 4, 8, 16, 6, 12};   /* the beat in quarter notes x 4 */

/* a pitch offset in 1/16 cent -> the frequency ratio - 1, Q16 (2^(c/1200) - 1 to second order) */
static inline int32_t rs_cents_q16(int32_t c16)
{
    c16 = dn_clamp(c16, -16000, 16000);                         /* +-1000 cents; 32-bit only (no 64-bit divide) */
    return c16 * 37854 / 16000 + (c16 * c16 / 256) * 11 / 1000;
}

void reese_note_on(reese_t *r, uint8_t note, uint8_t vel, int legato)
{
    uint32_t j;
    int32_t t = ((int32_t)note + r->p[RP_TRANS]) * 256;
    (void)vel;
    r->note = note;
    r->target256 = t;
    if (legato && r->gate && r->active && r->p[RP_GLIDE]) {      /* a slide: no new attack */
        r->gate = 1;
        return;
    }
    r->pitch256 = t;
    if (!r->active || r->astage == 0) {                       /* a fresh note: spread phases, the sub from zero */
        for (j = 0; j < RS_NOSC; j++)
            r->ph[j] = dn_noise(&r->rnd);
        r->sub = 0;
        for (j = 0; j < 4; j++)
            r->lad[j] = r->xo[j] = 0;
        for (j = 0; j < RS_COMB; j++)
            r->line[j] = 0;
    }
    r->gate = 1;
    r->active = 1;
    r->astage = 1;
    r->fstage = 1;
}

void reese_note_off(reese_t *r)
{
    r->gate = 0;
    if (r->astage)
        r->astage = 3;
    if (r->fstage)
        r->fstage = 3;
}

/* the envelopes and the glide, once a control tick */
static void rs_control(reese_t *r)
{
    const int16_t *p = r->p;
    int32_t ka = DN_ENV_K[dn_clamp(p[RP_ATK], 0, 127)], kd = DN_ENV_K[dn_clamp(p[RP_DEC], 0, 127)];
    int32_t kr = DN_ENV_K[dn_clamp(p[RP_REL], 0, 127)], sus = dn_clamp(p[RP_SUS], 0, 127) * (RS_FULL / 127);
    /* amplitude: attack to an overshoot target (an analog curve), decay to sustain, release to zero */
    switch (r->astage) {
    case 1:
        r->aenv += dn_mulq16((RS_FULL + RS_FULL / 4) - r->aenv, (uint32_t)ka);
        if (r->aenv >= RS_FULL) r->aenv = RS_FULL, r->astage = 2;
        break;
    case 2: r->aenv += dn_mulq16(sus - r->aenv, (uint32_t)kd); break;
    case 3:
        r->aenv -= dn_mulq16(r->aenv, (uint32_t)kr);
        if (r->aenv < RS_FULL / 4096) r->aenv = 0, r->astage = 0, r->active = 0;
        break;
    default: break;
    }
    /* the filter envelope: an attack-decay to zero with the amp's times, then its release */
    switch (r->fstage) {
    case 1:
        r->fenv += dn_mulq16((RS_FULL + RS_FULL / 4) - r->fenv, (uint32_t)ka);
        if (r->fenv >= RS_FULL) r->fenv = RS_FULL, r->fstage = 2;
        break;
    case 2: r->fenv -= dn_mulq16(r->fenv, (uint32_t)kd); break;
    case 3: r->fenv -= dn_mulq16(r->fenv, (uint32_t)kr); break;
    default: break;
    }
    /* the glide */
    if (r->pitch256 != r->target256) {
        int32_t d = r->target256 - r->pitch256, step = dn_mulq16(d, DN_ENV_K[dn_clamp(p[RP_GLIDE], 0, 127)]);
        if (!p[RP_GLIDE] || (d > -2 && d < 2)) r->pitch256 = r->target256;
        else r->pitch256 += step ? step : (d > 0 ? 1 : -1);
    }
    r->amp_now = r->amp_next;
    r->amp_next = (int32_t)(((int64_t)(r->aenv >> 9) * dn_clamp(p[RP_LEVEL], 0, 127) * 258) >> 15);   /* Q15 */
}

void reese_render(reese_t *r, int32_t *lr, uint32_t n)
{
    const int16_t *p = r->p;
    while (n) {
        uint32_t no, mode, beat, i, j, len;
        int32_t sub, cut, drv, hol, xover, g, gain, x0, x1, x2, x3, lfo;
        uint32_t inc, sinc[RS_NOSC], cph, cw, crate;
        rs_ladder_t lc;
        dn_svf_t xc;
        int32_t buf[RS_CTL];
        if (!r->ctl) {
            rs_control(r);
            r->ctl = RS_CTL;
        }
        len = n < r->ctl ? n : r->ctl;
        if (!r->active && !r->amp_now && !r->amp_next) {     /* silent: keep time only */
            r->ctl -= len, n -= len, lr += 2u * len;
            continue;
        }
        mode = (uint32_t)dn_clamp(p[RP_OSC], 0, RO_N - 1);
        no = RS_N[mode];
        beat = (uint32_t)dn_clamp(p[RP_BEAT], 0, 7);
        sub = p[RP_SUB] * 258;
        drv = p[RP_DRV];
        hol = p[RP_NTCH] * 258;
        xover = sub && mode != RO_2SIN;
        g = RS_GAIN[mode];
        inc = dn_pitch_inc(r->pitch256);
        /* the LFO on the cutoff (a sine), advanced once a block of this length */
        r->lph += (uint32_t)(((uint64_t)DN_LFO_INC[dn_clamp(p[RP_LRATE], 0, 127)] * len) >> 5);
        lfo = p[RP_LFLT] ? (dn_sine(r->lph) * p[RP_LFLT]) >> 6 : 0;          /* +-32 steps of cutoff at full depth */
        cut = (p[RP_CUT] << 8) + (int32_t)(((int64_t)(r->fenv >> 8) * p[RP_FENV]) >> 14) + lfo;   /* FENV 63: +63 steps */

        /* the drift: a twice-smoothed random walk per oscillator, pulled back to centre */
        if (len == r->ctl)
            for (j = 0; j < no; j++) {
                int32_t rr = (int32_t)(dn_noise(&r->rnd) >> 20) - 2048;
                r->dvel[j] += (rr * RS_DRIFT >> 7) - (r->dvel[j] >> 4);
                r->drift[j] += (r->dvel[j] >> 5) - (r->drift[j] >> 7);
            }
        if (beat) {
            /* the pair beats at the note value at the tempo on every note: a fixed offset in Hz, half each way
             * (beat Hz = BPM / 60 x quarters; 1 Hz is an increment of 2^32 / 44100 = 97391) */
            uint32_t bpm = (uint32_t)dn_clamp(r->bpm, 20, 300);
            int32_t half = (int32_t)(bpm * RS_BEAT4[beat] * 97391u / 480u);
            /* one drift for all of them: the pitch still breathes, the beat stays locked to the tempo */
            uint32_t dinc = inc + (uint32_t)(int32_t)(((int64_t)(int32_t)(inc >> 4) * (r->drift[0] / 17)) >> 12);
            for (j = 0; j < no; j++)
                sinc[j] = dinc + (uint32_t)((half * RS_POS[mode][j]) >> 12);
        } else {
            int32_t x = dn_clamp(p[RP_DTN] << 8, 0, 127 << 8);
            /* 1/16 cent, x^2 (x up to 32512: x^2 / 1024 * 1600 stays below 2^32) */
            int32_t s16 = (int32_t)((((uint32_t)x * (uint32_t)x) >> 10) * 1600u / 1032256u);
            for (j = 0; j < no; j++) {
                int32_t off = rs_cents_q16((s16 / 2) * RS_POS[mode][j] / 4096) + r->drift[j] / 17;
                sinc[j] = inc + (uint32_t)(int32_t)(((int64_t)(int32_t)(inc >> 4) * off) >> 12);
            }
        }
        rs_ladder_coef(&lc, cut, p[RP_RES]);
        if (xover)
            dn_svf_coef(&xc, RS_XOVER_CUT, RS_XOVER_K);

        for (i = 0; i < len; i++)
            buf[i] = 0;
        for (j = 0; j < no; j++) {
            uint32_t q = r->ph[j], qi = sinc[j];
            if (mode == RO_2SIN)
                for (i = 0; i < len; i++, q += qi) buf[i] += dn_mulq15(dn_sine(q), g);
            else if (mode == RO_SAWSQ && j == 1)
                for (i = 0; i < len; i++, q += qi) buf[i] += dn_mulq15(dn_pulse(q, qi, 0x80000000u), g);
            else
                for (i = 0; i < len; i++, q += qi) buf[i] += dn_mulq15(dn_saw(q, qi), g);
            r->ph[j] = q;
        }
        for (j = no; j < RS_NOSC; j++)
            r->ph[j] += inc * len;

        gain = 4096 + drv * 96;
        x0 = r->xo[0], x1 = r->xo[1], x2 = r->xo[2], x3 = r->xo[3];
        cph = r->cph, cw = r->cw;
        crate = (uint32_t)(8000u + (uint32_t)p[RP_NTCH] * 520u);
        for (i = 0; i < len; i++) {
            int32_t x = buf[i] >> 1, y;
            if (xover) {
                x = dn_svf(&xc, x, &x0, &x1, 2u);
                x = dn_svf(&xc, x, &x2, &x3, 2u);
            }
            x = x * gain >> 12;
            if (drv > 64)
                x = dn_softclip(x) + ((x - dn_softclip(x)) >> 2);
            y = rs_ladder_tick(&lc, x, r->lad);
            if (hol) {
                uint32_t tri = cph >> 15, d16;
                int32_t a, b, fr, dl;
                tri = tri < 65536u ? tri : 131071u - tri;
                d16 = (uint32_t)RS_COMB_MIN * 256u + ((tri * (uint32_t)(RS_COMB_MAX - RS_COMB_MIN)) >> 8);
                a = r->line[(cw - (d16 >> 8)) & (RS_COMB - 1u)];
                b = r->line[(cw - (d16 >> 8) - 1u) & (RS_COMB - 1u)];
                fr = (int32_t)(d16 & 255u);
                dl = a + (((b - a) * fr) >> 8);
                r->line[cw & (RS_COMB - 1u)] = (int16_t)dn_clamp(y, -32768, 32767);
                cw++;
                y -= dn_mulq15(dl, hol);
                cph += crate;
            }
            buf[i] = y;
        }
        r->xo[0] = x0, r->xo[1] = x1, r->xo[2] = x2, r->xo[3] = x3;
        r->cph = cph, r->cw = cw;

        {   /* the sub, clean and mono, after everything; then the amplitude ramp over the tick */
            uint32_t sp = r->sub, pos = RS_CTL - r->ctl;
            int32_t a0 = r->amp_now, da = r->amp_next - r->amp_now;
            for (i = 0; i < len; i++, sp += inc) {
                int32_t s = buf[i], amp = a0 + ((da * (int32_t)(pos + i)) >> 5), y;
                if (sub)
                    s += dn_mulq15(dn_sine(sp), sub);
                s = dn_clamp(s, -60000, 60000);
                y = dn_mulq15(dn_mulq15(s, amp), 16000);    /* ~Q15 full scale at the loudest */
                lr[2u * i] += y;
                lr[2u * i + 1u] += y;
            }
            r->sub = sp;
        }
        r->ctl -= len;
        n -= len;
        lr += 2u * len;
    }
}
