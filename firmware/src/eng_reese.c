/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: REESE engine. Copyright (C) 2026 Jamie (TwistedGenetics).
 * The ladder (reese_ladder_coef / reese_ladder_tick) and the supersaw's detune offsets and level curves are
 * Clipper's (isod89/clipper-fm1 firmware/src/dsp.c ladder_*, eng_hybrid.c SUPER_OFF / super_detune, Copyright (C)
 * 2026 3dSam, GPL-3.0-only), copied with their notes; the rest is new. */
/* REESE: the drum & bass bass. Up to seven band-limited saws detuned against each other (their beating is the
 * Reese's movement), each drifting on its own, through a Moog-style ladder with drive, then a moving comb that
 * hollows the sound out; a clean sine on the fundamental is added after all of that, so the low end stays solid
 * however hard the top is pushed (the presets play an octave or two down: TRANS).
 *
 * EDIT (P_E0..P_E7):
 *   SAWS  1..7       how many saws (1: one saw, 2: the classic two-saw Reese, 7: a full stack)
 *   DTN   0..127     the detune spread (finer at the low end: the supersaw's curve); SHAPE (ENV / LFO -> SHP) adds to it
 *   SUB   0..127     a clean sine on the fundamental, after the filter and the drive
 *   DRFT  0..127     each saw's pitch wanders on its own (slow random walk): an analog stack never sits still
 *   CUT   cutoff     the ladder's cutoff (ENV -> FLT and the LFO add to it)
 *   RES   0..127     its resonance (from about 110 it starts to sing)
 *   DRV   0..127     drive into the ladder: warm low, a growl high
 *   HOLW  0..127     the moving comb: a short delay swept slowly, mixed against the signal (the hollow, phasing
 *                    vowel of a Reese); 0 off
 *
 * Mono by design (engine poly 2: room for the release of the previous note while legato slides). The per-voice state
 * that does not fit in voice_t lives in the part's engine arena (engines.c eng_arena_of): six more saw phases, their
 * drift, the ladder's four stages and the comb's line. */

#define RS_SAWS 7u
#define RS_COMB 256u                      /* the comb's line: up to 5.8 ms, a power of two */
#define RS_COMB_MIN 24                    /* its sweep in samples: 0.5 .. 4.5 ms */
#define RS_COMB_MAX 200

typedef struct { int32_t G, H, G4, inv, k; } reese_ladder_t;   /* Clipper's ladder_t: Q16; k: Q12 */

typedef struct {
    uint32_t ph[RS_SAWS];                 /* the saws (0 is the centre) */
    uint32_t sub;                         /* the sub's phase */
    int32_t drift[RS_SAWS];               /* each saw's wander, 1/4096 semitone */
    int32_t dvel[RS_SAWS];                /* its slow velocity (the walk is smoothed twice: no steps) */
    int32_t lad[4];                       /* the ladder's stages (Q12) */
    int32_t rnd;                          /* noise32 state */
    uint32_t cph;                         /* the comb sweep's phase */
    uint32_t cw;                          /* the comb line's write index */
    int16_t line[RS_COMB];
} reese_vc_t;
typedef struct { reese_vc_t v[NVOICE]; } reese_part_t;

static reese_part_t *reese_of(const track_t *t) { return (reese_part_t *)eng_arena_of(t, ENGI_REESE); }

/* --- the ladder, from Clipper (see the header) --------------------------------------------------------------- */
/* Zavalishin's zero-delay-feedback ladder: four one-pole TPT stages, the feedback solved, the input through a tanh
 * (the transistor ladder's differential pair): a hot signal drives it warm, the resonance saturates softly. Signals
 * Q12, coefficients Q16, products and sums in 64 bits; G <= 0.75 keeps the states bounded. */
static inline int32_t rs_mul_sh(int32_t a, int32_t b, int sh) { return (int32_t)(((int64_t)a * b) >> sh); }
static inline void reese_ladder_coef(reese_ladder_t *c, int32_t cut, int32_t res)   /* cut 0..127 << 8, res 0..127 */
{
    int32_t i, g, G, G2;
    cut = clamp(cut, 0, 127 << 8);
    i = cut >> 8;
    g = SVF_G[i];
    if (i < 127)
        g += ((SVF_G[i + 1] - g) * (cut & 255)) >> 8;
    g = clamp(g, 1, 3 * 4096);
    G = (int32_t)(((uint32_t)g << 16) / (uint32_t)(4096 + g));   /* g / (1 + g) */
    G2 = rs_mul_sh(G, G, 16);
    c->G = G;
    c->H = 65536 - G;
    c->G4 = rs_mul_sh(G2, G2, 16);
    c->k = clamp(res, 0, 127) * 16900 / 127;                     /* 0 .. 4.13 */
    c->inv = (int32_t)(0xFFFFFFFFu / (uint32_t)(65536 + rs_mul_sh(c->k, c->G4, 12)));   /* (32-bit divide only) */
}
static inline int32_t reese_ladder_tick(const reese_ladder_t *c, int32_t x, int32_t *s)
{
    int64_t in = (int64_t)clamp(x, -131072, 131072) << 12, S, y, u;
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
            uq = softclip(h < 0 ? -65536 : 65536) << 13;
        else
            uq = (softclip((int32_t)h) << 13) + (int32_t)(u - (h << 13));
    }
    for (j = 0; j < 4; j++) {
        v = (int32_t)(((int64_t)G * (uq - s[j]) + 32768) >> 16);
        uq = v + s[j];
        s[j] = uq + v;
    }
    return (uq + 2048) >> 12;
}

/* --- the saws ------------------------------------------------------------------------------------------------ */
/* the supersaw's offsets at full detune (Q16 of the frequency, after Adam Szabo's analysis of the JP-8000, as in
 * Clipper), in the order the saws come in as SAWS grows: the pair closest to the centre first, so SAWS 2 is the
 * two-saw Reese (centre + one) and 3 its symmetric version */
static const int32_t RS_OFF[RS_SAWS] = {0, 1305, -1279, 4074, -4121, 7042, -7210};
static int32_t reese_detune(int32_t x)            /* 0..127 << 8 -> 0..65536, finer at the low end */
{
    int32_t q = clamp(x, 0, 127 << 8) * 2;            /* 0..65024 */
    return (int32_t)(((int64_t)q * q >> 16) * 3 + q) >> 2;
}

static void reese_note_on(track_t *t, voice_t *v)
{
    reese_vc_t *V = &reese_of(t)->v[(uint32_t)(v - t->v) % NVOICE];
    uint32_t j;
    if (!v->env && !v->env_out) {                     /* a fresh note (not a legato slide): free-running phases spread
                                                       * so the stack does not start as one big click */
        if (!V->rnd)
            V->rnd = 0x5EE5E + (int32_t)v->age;
        for (j = 0; j < RS_SAWS; j++)
            V->ph[j] = noise32(&V->rnd);
        V->sub = 0;                                   /* the sub from zero: the same thump every time */
        memset(V->lad, 0, sizeof V->lad);
        memset(V->line, 0, sizeof V->line);
    }
}

static void reese_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    reese_vc_t *V = &reese_of(t)->v[(uint32_t)(v - t->v) % NVOICE];
    uint32_t ns = (uint32_t)clamp(p[P_E0], 1, (int32_t)RS_SAWS), i, j;
    int32_t det = reese_detune((p[P_E1] << 8) + m->shape - (64 << 8));
    int32_t sub = p[P_E2] * 258, drft = p[P_E3];
    int32_t cut = (p[P_E4] << 8) + m->cutoff;
    int32_t drv = p[P_E6], hol = p[P_E7] * 258;
    uint32_t inc = m->inc, sinc[RS_SAWS], sub_inc = inc;
    int32_t cg, sg;
    reese_ladder_t lc;
    int32_t buf[CTL];

    /* the centre saw falls and the others rise as the detune opens (Szabo's level fits, simplified as in Clipper);
     * one saw alone stays at full level */
    {
        int32_t dq = det >> 1;
        cg = ns > 1u ? 32767 - mulq15(dq, 18000) : 32767;
        sg = ns > 1u ? 2000 + mulq15(dq, 16000) : 0;
        if (ns == 2u)                                 /* the two-saw Reese: both at the same level */
            cg = sg = 23000;
    }
    /* drift: a twice-smoothed random walk per saw, updated once a control tick */
    for (j = 0; j < RS_SAWS; j++) {
        if (drft) {
            int32_t r = (int32_t)(noise32(&V->rnd) >> 20) - 2048;         /* -2048..2047 */
            V->dvel[j] += (r * drft >> 7) - (V->dvel[j] >> 4);
            V->drift[j] += (V->dvel[j] >> 5) - (V->drift[j] >> 7);       /* pulled back to centre: it wanders, never leaves */
        } else {
            V->drift[j] -= V->drift[j] >> 3;
            V->dvel[j] = 0;
        }
        /* inc * (1 + off * det + drift): the drift in 1/4096 semitone ~ 1/70000 of the frequency each */
        sinc[j] = inc + (uint32_t)(int32_t)(((int64_t)(int32_t)(inc >> 4) *
                                             (mulq16(RS_OFF[j], (uint32_t)det) + V->drift[j] / 17)) >> 12);
    }
    reese_ladder_coef(&lc, cut, p[P_E5]);

    /* the saws, one at a time (the phase stays in a register) */
    for (i = 0; i < n; i++)
        buf[i] = 0;
    for (j = 0; j < ns; j++) {
        uint32_t q = V->ph[j], qi = sinc[j];
        int32_t g = j ? sg : cg;
        for (i = 0; i < n; i++, q += qi)
            buf[i] += mulq15(osc_saw(q, qi), g);
        V->ph[j] = q;
    }
    for (j = ns; j < RS_SAWS; j++)                    /* the silent ones keep running: no jump when SAWS turns */
        V->ph[j] += sinc[j] * n;

    /* drive, the ladder, the comb */
    {
        int32_t gain = 4096 + drv * 96;                       /* 1x .. ~4x, Q12 */
        uint32_t cph = V->cph, cw = V->cw;
        /* the comb's sweep: a slow triangle, 0.08 .. 0.7 Hz with HOLW (more hollow, a little faster) */
        uint32_t crate = (uint32_t)(8000u + (uint32_t)p[P_E7] * 520u);
        for (i = 0; i < n; i++) {
            int32_t x = (buf[i] >> 1) * gain >> 12, y;
            if (drv > 64)                                     /* the growl: the hottest settings fold back a little */
                x = softclip(x) + ((x - softclip(x)) >> 2);
            y = reese_ladder_tick(&lc, x, V->lad);
            if (hol) {
                uint32_t tri = cph >> 15, d16;
                int32_t a, b, fr, dl;
                tri = tri < 65536u ? tri : 131071u - tri;    /* 0..65535 */
                d16 = (uint32_t)RS_COMB_MIN * 256u + ((tri * (uint32_t)(RS_COMB_MAX - RS_COMB_MIN)) >> 8);  /* Q8 */
                a = V->line[(cw - (d16 >> 8)) & (RS_COMB - 1u)];
                b = V->line[(cw - (d16 >> 8) - 1u) & (RS_COMB - 1u)];
                fr = (int32_t)(d16 & 255u);
                dl = a + (((b - a) * fr) >> 8);
                V->line[cw & (RS_COMB - 1u)] = (int16_t)clamp(y, -32768, 32767);
                cw++;
                y -= mulq15(dl, hol);                         /* against itself: notches that sweep */
                cph += crate;
            }
            buf[i] = y;
        }
        V->cph = cph;
        V->cw = cw;
    }

    /* the sub, clean, after everything; then the amplitude */
    {
        uint32_t sp = V->sub;
        for (i = 0; i < n; i++, sp += sub_inc) {
            int32_t s = buf[i];
            if (sub)
                s += mulq15(osc_sine(sp), sub);
            s = clamp(s, -60000, 60000);
            out[i] += mulq15(mulq15(s, amp_at(m, i)), VOICE_FS) << 1;
        }
        V->sub = sp;
    }
}

static const preset_t REESE_PRESETS[] = {
    /* {SAWS, DTN, SUB, DRFT, CUT, RES, DRV, HOLW}, {A D S R}, fenv, mono */
    {"CLASSIC RS", {2, 34, 70, 30, 60, 18, 30, 0}, {0, 70, 120, 26}, 0, 1, FX(0, 0, 0, 6), XP(P_TRANS + 1, -24)},
    {"HOLLOW RS", {2, 40, 64, 40, 66, 22, 36, 70}, {0, 70, 120, 26}, 0, 1, FX(0, 0, 0, 8), XP(P_TRANS + 1, -24)},
    {"STACK RS", {7, 30, 60, 24, 58, 16, 44, 30}, {0, 70, 120, 28}, 0, 1, FX(0, 10, 0, 8), XP(P_TRANS + 1, -24)},
    {"NEURO RS", {5, 48, 72, 30, 44, 60, 110, 56}, {0, 80, 110, 22}, 24, 1, FX(18, 0, 0, 4),
     XP(P_LRATE + 1, 70, P_LD_FLT + 1, 34, P_TRANS + 1, -24)},
    {"DARK RS", {3, 22, 84, 36, 36, 26, 48, 40}, {0, 70, 124, 30}, 0, 1, FX(0, 0, 0, 10), XP(P_TRANS + 1, -24)},
    {"MOVING RS", {3, 30, 70, 50, 54, 30, 40, 90}, {0, 70, 120, 26}, 0, 1, FX(0, 0, 0, 8),
     XP(P_LRATE + 1, 18, P_LD_FLT + 1, 18, P_LD_SHP + 1, 20, P_TRANS + 1, -24)},
    {"HOOVER RS", {7, 64, 30, 20, 96, 10, 60, 40}, {0, 80, 110, 34}, 0, 1, FX(10, 30, 10, 18),
     XP(P_ED_PIT + 1, 20, P_GLIDE + 1, 50, P_TRANS + 1, -12)},
    {"SUB REESE", {2, 26, 110, 20, 40, 10, 24, 20}, {0, 70, 124, 24}, 0, 1, FX(0, 0, 0, 4), XP(P_TRANS + 1, -24)},
};

static const engine_t ENG_REESE = {
    "REESE", {"OSC", "FLT"},
    {
        {"SAWS", F_INT, 1, 7, 2, 0, 0},
        {"DTN", F_PCT, 0, 127, 34, 0, 0},
        {"SUB", F_PCT, 0, 127, 70, 0, 0},
        {"DRFT", F_PCT, 0, 127, 30, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 60, 0, 0},
        {"RES", F_PCT, 0, 127, 18, 0, 0},
        {"DRV", F_PCT, 0, 127, 30, 0, 0},
        {"HOLW", F_PCT, 0, 127, 0, 0, 0},
    },
    REESE_PRESETS, sizeof(REESE_PRESETS) / sizeof(REESE_PRESETS[0]), 1, reese_note_on, reese_render,
    0x4C9F, {P_E1, P_E4, P_E7, P_E6}, 2,
};
