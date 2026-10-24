/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: SUB, an SH-101-style mono bass for jungle (0.13). Copyright (C) 2026 Jamie (TwistedGenetics).
 * The ladder's zero-delay-feedback core is Clipper's (isod89/clipper-fm1 firmware/src/dsp.c, Copyright (C) 2026
 * 3dSam, GPL-3.0-only), as in reese.c; the rest is DREADNOUGHT's.
 *
 * The SH-101's voice, as its panel lays it out: one oscillator with a saw, a pulse (its width set by hand and moved by
 * its own triangle LFO: PWM) and a square sub-oscillator an octave or two down (or the 25 % pulse two down), plus
 * noise, each with its own level on a mixer; a resonant 4-pole low-pass (the IR3109 is an OTA cascade: here the ZDF
 * ladder, which sounds the same family and self-oscillates in tune) with envelope amount and keyboard tracking; ONE
 * ADSR for both the filter and the amp (the 101's), or the amp on the gate alone (VCA: GATE); portamento OFF / AUTO
 * (legato notes only, the 101's switch) / ON; accent from the sequencer's ACC steps opening the filter and lifting the
 * level.
 *
 * Then the jungle part, after the amp, in order: DRIVE (SOFT, HARD, FOLD, RECT), CRUSH (bits and rate down), TONE (a
 * low-pass to tame it), and the clean-sub SPLIT: below the split frequency the signal goes round the dirt untouched,
 * so the fundamental stays whole under the heaviest drive.
 *
 * The patch (p[]):
 *   SAW PULSE SUB NOISE 0..127   the mixer
 *   PW 0..127      the pulse's width, 50 % .. 5 %;  PWM 0..127 how far the triangle moves it;  PWMR 0..127 its rate
 *                  (0.05 .. 20 Hz);  SUBM 0..2 the sub: -1 OCT, -2 OCT, -2 25%
 *   CUT 0..127     30 Hz .. 16 kHz (log);  RES 0..127 (self-oscillates near the top);  ENV -64..63;  KEY 0..127
 *                  (127: the cutoff follows the keys a semitone a semitone)
 *   DRIVE 0..127;  DTYPE 0..3 SOFT HARD FOLD RECT;  CRUSH 0..127 (0 off);  TONE 0..127 (127 open)
 *   ATK DEC SUS REL 0..127 (1 ms .. 10 s; the attack never quicker than ~3 ms: a sharper start clicks);  GLIDE 0..127;  GMODE 0..2 OFF AUTO ON;  ACC 0..127;  VCA 0..1 ENV GATE
 *   LEVEL 0..127 (100 = unity);  TRANS -36..36;  SPLIT 0..127 (0 off; else 40 .. 300 Hz) */
#include "dn_sub.h"
#include "dn_dsp.h"

#define SB_FULL (1 << 24)
#define SB_SEMI 299                                 /* cutoff steps (1/256) a semitone: 127 steps span 9.06 octaves */

static const char *const SP_NAME[SP_COUNT] = {"SAW", "PULSE", "SUB", "NOISE", "PW", "PWM", "PWM RATE", "SUB MODE",
    "CUT", "RES", "ENV", "KEY", "DRIVE", "TYPE", "CRUSH", "TONE", "ATK", "DEC", "SUS", "REL", "GLIDE", "PORTA",
    "ACCENT", "VCA", "LEVEL", "TRANS", "SPLIT"};
static const int8_t SP_MIN[SP_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                        -36, 0};
static const int8_t SP_MAX[SP_COUNT] = {127, 127, 127, 127, 127, 127, 127, SB_SUBN - 1, 127, 127, 63, 127, 127,
                                        SB_DN - 1, 127, 127, 127, 127, 127, 127, 127, 2, 127, 1, 127, 36, 127};

const char *dn_sub_param_name(uint32_t k) { return k < SP_COUNT ? SP_NAME[k] : ""; }
int32_t dn_sub_param_min(uint32_t k) { return k < SP_COUNT ? SP_MIN[k] : 0; }
int32_t dn_sub_param_max(uint32_t k) { return k < SP_COUNT ? SP_MAX[k] : 0; }

static void sb_str(char *o, const char *s)
{
    uint32_t i;
    for (i = 0; s[i] && i < 7u; i++)
        o[i] = s[i];
    o[i] = 0;
}
static void sb_int(char *o, int32_t v)
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
int32_t dn_sub_cut_hz(int32_t c)                    /* 30 Hz x 533.3 ^ (c / 127 / 256) */
{
    float f = 30.0f, step = 1.0506f;                /* 533.33 ^ (1 / 127) */
    int32_t i, n = dn_clamp(c, 0, 127 << 8);
    for (i = 0; i < (n >> 8); i++)
        f *= step;
    f *= 1.0f + 0.0506f * (float)(n & 255) / 256.0f;
    return (int32_t)(f + 0.5f);
}
void dn_sub_param_text(uint32_t k, int32_t v, char out[8])
{
    static const char *const SM[SB_SUBN] = {"-1 OCT", "-2 OCT", "-2 25%"};
    static const char *const DT[SB_DN] = {"SOFT", "HARD", "FOLD", "RECT"};
    static const char *const GM[3] = {"OFF", "AUTO", "ON"};
    switch (k) {
    case SP_SUBM: sb_str(out, SM[dn_clamp(v, 0, SB_SUBN - 1)]); return;
    case SP_DTYPE: sb_str(out, DT[dn_clamp(v, 0, SB_DN - 1)]); return;
    case SP_GMODE: sb_str(out, GM[dn_clamp(v, 0, 2)]); return;
    case SP_VCA: sb_str(out, v ? "GATE" : "ENV"); return;
    case SP_PW: sb_int(out, 50 - v * 45 / 127); { uint32_t i = 0; while (out[i]) i++; out[i] = '%', out[i + 1] = 0; } return;
    case SP_CRUSH: if (!v) { sb_str(out, "OFF"); return; } break;
    case SP_SPLIT:
        if (!v) { sb_str(out, "OFF"); return; }
        sb_int(out, 40 + v * 260 / 127);
        { uint32_t i = 0; while (out[i]) i++; out[i] = 'H', out[i + 1] = 'z', out[i + 2] = 0; }
        return;
    case SP_CUT: {
        int32_t hz = dn_sub_cut_hz(v << 8);
        uint32_t i = 0;
        if (hz >= 1000) {
            sb_int(out, hz / 1000);
            while (out[i]) i++;
            out[i] = '.', out[i + 1] = (char)('0' + hz / 100 % 10), out[i + 2] = 'k', out[i + 3] = 0;
        } else {
            sb_int(out, hz);
            while (out[i]) i++;
            out[i] = 'H', out[i + 1] = 'z', out[i + 2] = 0;
        }
        return;
    }
    case SP_GLIDE:
        if (!v) { sb_str(out, "OFF"); return; }
        break;
    case SP_TRANS: case SP_ENV:
        if (v > 0) { out[0] = '+'; sb_int(out + 1, v); return; }
        break;
    default: break;
    }
    sb_int(out, v);
}

/* ---- the factory sounds -------------------------------------------------------------------------------------- */
typedef struct { const char *name; int8_t v[SP_COUNT]; } dn_sub_preset_t;
/*        SAW PUL SUB NOI  PW PWM PWR SBM  CUT RES ENV KEY  DRV TYP CRU TON  ATK DEC SUS REL  GLI GMO ACC VCA LEV TRN SPL */
static const dn_sub_preset_t SB_PRESETS[] = {
    /* dread: a pulse and the sub, the filter low and round, driven soft, the clean low end split off */
    {"DREAD", {40, 90, 110, 0, 30, 0, 0, 0, 48, 30, 6, 40, 70, 0, 0, 90, 0, 70, 110, 30, 40, 1, 60, 0, 100, -24, 70}},
    /* donk: a short, plucky, resonant square bounce (the hardcore donk), quick decay */
    {"DONK", {0, 120, 60, 0, 0, 0, 0, 0, 56, 90, 30, 64, 40, 1, 0, 110, 0, 40, 30, 20, 0, 0, 90, 0, 127, -12, 60}},
    /* rolling sub: mostly sub and a little saw, the filter closed, a sine-ish rolling bass for steppers */
    {"ROLLER", {30, 0, 127, 0, 0, 0, 0, 0, 38, 10, 4, 30, 30, 0, 0, 70, 2, 80, 120, 34, 30, 1, 40, 0, 127, -24, 90}},
    /* wobble: resonant saw with the PWM; put an LFO on CUTOFF (LFO page) for the wobble */
    {"WOBBLE", {110, 60, 90, 0, 40, 60, 50, 0, 50, 70, 6, 50, 80, 0, 0, 100, 0, 70, 120, 30, 20, 1, 50, 0, 100, -24, 70}},
    /* acid: the 101 / 303 squelch: saw, high resonance, big envelope, accent and auto glide */
    {"ACID 101", {127, 0, 30, 0, 0, 0, 0, 0, 44, 104, 40, 64, 50, 0, 0, 120, 0, 50, 20, 30, 50, 1, 110, 0, 127, -12, 0}},
    /* an 808-style sine sub: the filter shut on the sub alone, folded for harmonics above the split */
    {"808 SUB", {0, 0, 127, 0, 0, 0, 0, 0, 20, 0, 8, 40, 90, 2, 0, 90, 0, 90, 0, 60, 45, 2, 30, 0, 100, -24, 100}},
    /* growl: hard-driven saw and pulse, mid resonance, a slow PWM: the techstep growl */
    {"GROWL", {120, 100, 80, 10, 60, 70, 30, 0, 62, 60, 8, 50, 120, 1, 0, 80, 0, 70, 120, 30, 20, 1, 60, 0, 60, -24, 80}},
    /* square sub: the 25 % pulse two octaves down under a hollow square: the 101 sub sound */
    {"SQUARE SB", {0, 100, 127, 0, 0, 0, 0, 2, 52, 20, 4, 40, 20, 0, 0, 100, 0, 70, 120, 30, 0, 0, 40, 1, 120, -12, 0}},
    /* punch: a fast filter snap on saw and sub, gated amp: the 101's tight bass */
    {"PUNCH", {100, 0, 100, 0, 0, 0, 0, 0, 40, 50, 28, 64, 50, 0, 0, 110, 0, 30, 0, 10, 0, 0, 80, 1, 100, -24, 60}},
    /* tech sub: rectified for an octave-up rasp over a clean sub */
    {"TECH SUB", {80, 80, 110, 0, 50, 20, 40, 0, 58, 40, 8, 50, 70, 3, 0, 90, 0, 70, 120, 30, 20, 1, 50, 0, 70, -24, 110}},
    /* crushed: bits and rate down on a resonant pulse, the low end kept clean: lo-fi jungle */
    {"CRUSHED", {60, 110, 100, 0, 20, 30, 20, 0, 60, 50, 8, 50, 40, 0, 70, 90, 0, 70, 120, 30, 20, 1, 50, 0, 100, -24, 80}},
    /* rave stab: bright saw + pulse, noise, a short env: the old-skool bleep-bass hit */
    {"RAVE STAB", {127, 90, 40, 30, 30, 40, 70, 0, 70, 60, 28, 64, 60, 0, 0, 127, 0, 50, 20, 40, 0, 0, 70, 0, 120, 0, 0}},
    /* INIT: a plain starting point (the saw, the filter open, nothing else) */
    {"INIT", {100, 0, 0, 0, 0, 0, 0, 0, 127, 0, 0, 0, 0, 0, 0, 127, 0, 64, 127, 20, 0, 0, 0, 0, 100, -24, 0}},
};
#define SB_NPRESETS (sizeof SB_PRESETS / sizeof SB_PRESETS[0])
uint32_t dn_sub_preset_count(void) { return SB_NPRESETS; }
const char *dn_sub_preset_name(uint32_t i) { return SB_PRESETS[i % SB_NPRESETS].name; }
void dn_sub_load_preset(dn_sub_t *s, uint32_t i)
{
    uint32_t k;
    for (k = 0; k < SP_COUNT; k++)
        s->p[k] = SB_PRESETS[i % SB_NPRESETS].v[k];
}

void dn_sub_init(dn_sub_t *s)
{
    uint32_t i;
    uint8_t *b = (uint8_t *)s;
    for (i = 0; i < sizeof *s; i++)
        b[i] = 0;
    s->rnd = 0x101101;
    s->bpm = 172;
    dn_sub_load_preset(s, 0);
}

/* ---- the filter: the ZDF ladder (Clipper's), as in reese.c ------------------------------------------------- */
typedef struct { int32_t G, H, G4, inv, k; } sb_ladder_t;
static inline int32_t sb_mul_sh(int32_t a, int32_t b, int sh) { return (int32_t)(((int64_t)a * b) >> sh); }
static void sb_ladder_coef(sb_ladder_t *c, int32_t cut, int32_t res)
{
    int32_t g = dn_clamp(dn_svf_g16(cut), 16, 3 * 65536), G, G2;
    G = (int32_t)((float)g * 65536.0f / (65536.0f + (float)g));   /* g / (1 + g), Q16 (single float: no 64-bit divide) */
    G2 = sb_mul_sh(G, G, 16);
    c->G = G;
    c->H = 65536 - G;
    c->G4 = sb_mul_sh(G2, G2, 16);
    c->k = dn_clamp(res, 0, 127) * 17400 / 127;      /* 0 .. 4.25: past 4 it rings on its own */
    c->inv = (int32_t)(0xFFFFFFFFu / (uint32_t)(65536 + sb_mul_sh(c->k, c->G4, 12)));
}
static inline int32_t sb_ladder_tick(const sb_ladder_t *c, int32_t x, int32_t *s)
{
    int64_t in = (int64_t)dn_clamp(x, -131072, 131072) << 12, S, y, u;
    int32_t G = c->G, uq, v, j;
    in += (in * c->k) >> 14;                        /* (a little of the passband kept at high RES) */
    S = ((int64_t)G * s[0] >> 16) + s[1];
    S = ((G * S) >> 16) + s[2];
    S = ((G * S) >> 16) + s[3];
    S = (c->H * S) >> 16;
    y = ((((int64_t)c->G4 * in) >> 16) + S) * c->inv >> 16;
    u = in - ((c->k * y) >> 12);
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

/* ---- notes ----------------------------------------------------------------------------------------------- */
void dn_sub_note_on(dn_sub_t *s, uint8_t note, uint8_t vel, int legato, int accent)
{
    int32_t t = ((int32_t)note + s->p[SP_TRANS]) * 256;
    int glide = s->p[SP_GLIDE] && (s->p[SP_GMODE] == SB_GON || (s->p[SP_GMODE] == SB_GAUTO && legato && s->gate));
    s->vel = vel ? vel : 1;
    s->acc = accent ? 1u : 0u;
    s->note = note;
    s->target256 = t;
    if (!glide || !s->active)
        s->pitch256 = t;
    if (legato && s->gate && s->active && s->stage) {   /* legato: no new attack (the 101 retriggers only on a new
                                                         * key after all are up) */
        s->gate = 1;
        return;
    }
    if (!s->active) {
        uint32_t j;
        s->ph = 0x80000000u;                        /* the saw from its zero crossing: no step at the start */
        s->sph = 0;
        for (j = 0; j < 4; j++)
            s->lad[j] = 0;
        s->lad[0] = 64;                             /* (a seed: a self-oscillating filter starts on its own) */
    }
    s->gate = 1;
    s->active = 1;
    s->stage = 1;
}
void dn_sub_note_off(dn_sub_t *s)
{
    s->gate = 0;
    if (s->stage)
        s->stage = 3;
}

#define SB_M(s, k, lo, hi) dn_clamp((s)->p[k] + ((s)->mod[k] >> 8), lo, hi)

static void sb_control(dn_sub_t *s)
{
    const int16_t *p = s->p;
    int32_t ka, kd, kr, sus = dn_clamp(p[SP_SUS], 0, 127) * (SB_FULL / 127), a;
    if (s->esync) {
        ka = s->stage == 1 ? dn_esync_k(p[SP_ATK], s->bpm, 14) : 0;
        kd = s->stage == 2 ? dn_esync_k(p[SP_DEC], s->bpm, 0) : 0;
        kr = s->stage == 3 ? dn_esync_k(p[SP_REL], s->bpm, 0) : 0;
    } else {
        ka = DN_ENV_K[dn_clamp(p[SP_ATK], 14, 127)], kd = DN_ENV_K[dn_clamp(p[SP_DEC], 0, 127)];
        kr = DN_ENV_K[dn_clamp(p[SP_REL], 0, 127)];
    }
    switch (s->stage) {
    case 1:
        s->env += dn_mulq16((SB_FULL + SB_FULL / 4) - s->env, (uint32_t)ka);
        if (s->env >= SB_FULL) s->env = SB_FULL, s->stage = 2;
        break;
    case 2: s->env += dn_mulq16(sus - s->env, (uint32_t)kd); break;
    case 3:
        s->env -= dn_mulq16(s->env, (uint32_t)kr);
        if (s->env < SB_FULL / 4096) s->env = 0, s->stage = 0;
        break;
    default: break;
    }
    /* GATE: the amp follows the gate, declicked (~2 ms); ENV: the envelope */
    s->gamp += dn_mulq16((s->gate ? SB_FULL : 0) - s->gamp, DN_ENV_K[14]);   /* (~3 ms: a declick, no click) */
    a = p[SP_VCA] ? s->gamp : s->env;
    if (!s->stage && (!p[SP_VCA] || s->gamp < SB_FULL / 4096))
        s->active = 0;
    if (s->pitch256 != s->target256) {
        int32_t d = s->target256 - s->pitch256, step = dn_mulq16(d, DN_ENV_K[dn_clamp(p[SP_GLIDE], 0, 127)]);
        if (!p[SP_GLIDE] || (d > -2 && d < 2)) s->pitch256 = s->target256;
        else s->pitch256 += step ? step : (d > 0 ? 1 : -1);
    }
    s->pwph += DN_LFO_INC[dn_clamp(p[SP_PWMR], 0, 127)];
    s->amp_now = s->amp_next;
    s->amp_next = (int32_t)(((int64_t)(a >> 9) * 100 * 258) >> 15);   /* Q15 (LEVEL comes after the dirt) */
    if (s->acc)                                     /* accent: louder by up to half again */
        s->amp_next += s->amp_next * dn_clamp(p[SP_ACC], 0, 127) / 254;
    if (s->vsens)
        s->amp_next = s->amp_next * (127 * 127 - (int32_t)s->vsens * (127 - (int32_t)s->vel)) / (127 * 127);
    if (!s->active)
        s->amp_next = 0;
}

/* the dirt on one sample of the band above the split */
static inline int32_t sb_drive(int32_t x, int32_t gain, uint32_t type, int32_t *dc)
{
    int32_t d = (int32_t)(((int64_t)x * gain) >> 12), y;
    switch (type) {
    case SB_HARD: y = dn_clamp(d, -22000, 22000) * 3 / 2; break;
    case SB_FOLD: {                                 /* a triangle fold: past +-T it turns back, as often as needed */
        const int32_t T = 20000;
        int32_t u = d % (4 * T);
        if (u < 0) u += 4 * T;
        y = u < T ? u : u < 3 * T ? 2 * T - u : u - 4 * T;
        y = y * 3 / 2;
        break;
    }
    case SB_RECT: {                                 /* full-wave: an octave up and rasp, its DC taken out */
        int32_t r = dn_softclip(d);
        r = r < 0 ? -r : r;
        *dc += (r - *dc) >> 10;
        y = (r - *dc) * 2;
        break;
    }
    default: y = dn_softclip(d); break;
    }
    return y;
}

void dn_sub_render(dn_sub_t *s, int32_t *lr, uint32_t n)
{
    const int16_t *p = s->p;
    while (n) {
        uint32_t len, i, inc, sinc, pw, type;
        int32_t buf[SB_CTL], lsaw, lpul, lsub, lnoi, cut, drive, gain, crush, tone, split, level;
        sb_ladder_t lc;
        if (!s->ctl) {
            sb_control(s);
            s->ctl = SB_CTL;
        }
        len = n < s->ctl ? n : s->ctl;
        if (!s->active && !s->amp_now && !s->amp_next) {
            s->ctl -= len, n -= len, lr += 2u * len;
            continue;
        }
        inc = dn_pitch_inc(s->pitch256 + s->mod_pitch);
        sinc = inc >> (p[SP_SUBM] == SB_SUB1 ? 1 : 2);
        {   /* the pulse's width: PW by hand, the triangle (PWM) on it; 3 % .. 97 % */
            uint32_t tri = s->pwph >> 15;
            int32_t t = (int32_t)(tri < 65536u ? tri : 131071u - tri) - 32768, w;
            w = 32768 - SB_M(s, SP_PW, 0, 127) * 29491 / 127 + ((t * SB_M(s, SP_PWM, 0, 127)) >> 8);
            w = dn_clamp(w, 2000, 63536);
            pw = (uint32_t)w << 16;
        }
        lsaw = SB_M(s, SP_SAW, 0, 127) * 100;
        lpul = SB_M(s, SP_PULSE, 0, 127) * 100;
        lsub = SB_M(s, SP_SUB, 0, 127) * 110;
        lnoi = SB_M(s, SP_NOISE, 0, 127) * 90;
        for (i = 0; i < len; i++) {
            int32_t x = 0;
            uint32_t q = s->ph, sq = s->sph;
            if (lsaw) x += (dn_saw(q, inc) * lsaw) >> 14;
            if (lpul) x += (dn_pulse(q, inc, pw) * lpul) >> 14;
            if (lsub) x += ((p[SP_SUBM] == SB_SUB2P ? dn_pulse(sq, sinc, 0x40000000u) : dn_pulse(sq, sinc, 0x80000000u)) * lsub) >> 14;
            if (lnoi) x += (((int32_t)(dn_noise(&s->rnd) >> 16) - 32768) * lnoi) >> 14;
            else x += ((int32_t)(dn_noise(&s->rnd) >> 16) - 32768) >> 11;   /* (a -78 dB floor, as the circuit has:
                                                                              * the filter rings up from it) */
            buf[i] = x;
            s->ph = q + inc;
            s->sph = sq + sinc;
        }
        cut = (p[SP_CUT] << 8) + s->mod[SP_CUT]
              + (int32_t)(((int64_t)(s->env >> 8) * (SB_M(s, SP_ENV, -64, 63) + (s->acc ? p[SP_ACC] / 6 : 0))) >> 8)   /* ENV 63:
                                                     * +63 steps (4.5 octaves) at the envelope's top */
              + ((s->note - 60) * SB_SEMI * SB_M(s, SP_KEY, 0, 127)) / 127 + (s->acc ? p[SP_ACC] * 8 : 0);
        sb_ladder_coef(&lc, cut, SB_M(s, SP_RES, 0, 127));
        drive = SB_M(s, SP_DRIVE, 0, 127);
        type = (uint32_t)dn_clamp(p[SP_DTYPE], 0, SB_DN - 1);
        gain = 4096 + drive * drive * 2;            /* x1 .. ~9 */
        crush = SB_M(s, SP_CRUSH, 0, 127);
        tone = SB_M(s, SP_TONE, 0, 127);
        split = p[SP_SPLIT] ? (40 + p[SP_SPLIT] * 260 / 127) * 467 / 100 : 0;   /* a one-pole's Q15 coefficient */
        level = SB_M(s, SP_LEVEL, 0, 127) * 17000 / 100;   /* the output, after the dirt (100 = unity) */
        {
            uint32_t pos = SB_CTL - s->ctl;
            int32_t a0 = s->amp_now, da = s->amp_next - s->amp_now;
            int32_t ta = 32768;                     /* TONE: a one-pole from 120 Hz (0) up to open (127) */
            if (tone < 127) {
                float g = (float)dn_svf_g16(24 * 256 + tone * 103 * 256 / 127) / 65536.0f;
                ta = (int32_t)(2.0f * g / (1.0f + g) * 32768.0f);
                if (ta > 32768) ta = 32768;
            }
            for (i = 0; i < len; i++) {
                int32_t amp = a0 + ((da * (int32_t)(pos + i)) >> 5), x, lo = 0, hi, y;
                x = sb_ladder_tick(&lc, buf[i], s->lad);
                x = (int32_t)(((int64_t)dn_clamp(x, -80000, 80000) * amp) >> 15);
                if (split) {                        /* the clean low end, round the dirt */
                    s->lo1 += ((x - s->lo1) * split) >> 15;
                    s->lo2 += ((s->lo1 - s->lo2) * split) >> 15;
                    lo = s->lo2;
                }
                hi = x - lo;
                if (drive || type == SB_RECT)
                    hi = sb_drive(hi, gain, type, &s->dc);
                if (crush) {                        /* rate down (hold), then bits down */
                    s->cr_ph += 65536 / (1 + crush / 8);
                    if (s->cr_ph >= 65536) {
                        s->cr_ph -= 65536;
                        s->hold = hi;
                    }
                    hi = s->hold & ~((1 << (crush * 11 / 127)) - 1);
                }
                if (ta < 32768)
                    s->tone += (int32_t)(((int64_t)(hi - s->tone) * ta) >> 15), hi = s->tone;
                y = dn_clamp(hi + lo, -45000, 45000);
                y = dn_clamp(dn_mulq15(y, level), -32767, 32767);   /* (LEVEL 100: 45000 -> 23300) */
                lr[2u * i] += y;
                lr[2u * i + 1u] += y;
            }
        }
        s->ctl -= len;
        n -= len;
        lr += 2u * len;
    }
}
