/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: REESE engine. Copyright (C) 2026 Jamie (TwistedGenetics).
 * The ladder (reese_ladder_coef / reese_ladder_tick) and the supersaw's detune offsets are Clipper's
 * (isod89/clipper-fm1 firmware/src/dsp.c ladder_*, eng_hybrid.c SUPER_OFF, Copyright (C) 2026 3dSam,
 * GPL-3.0-only), copied with their notes; the rest is new. Design notes and sources: docs/REESE.md. */
/* REESE: the drum & bass bass, built the way the research says a Reese is built (docs/REESE.md).
 *
 * Two (or more) oscillators detuned against each other: their sum is a comb filter that moves up the spectrum as
 * they drift in and out of phase, which is the Reese's movement before any effect (the Casio CZ's LINE 1+1', the
 * 1988 original). Then, in the order producers use: a crossover that takes the low end away from the oscillators
 * (they cancel each other there and the bottom pumps), drive on what's left (distortion only above the sub), a
 * Moog-style ladder, a swept notch comb, and a clean mono sine on the fundamental added last.
 *
 * EDIT (P_E0..P_E7):
 *   OSC   2SAW 3SAW 5SAW 7SAW SAWSQ 2SIN
 *         2SAW the classic pair; 3/5/7SAW a centre saw with detuned pairs (supersaw spacing); SAWSQ a saw against
 *         a square (techstep grit, the CZ's pulse); 2SIN two sines (the soft, wavy, original-style Reese: they are
 *         the low end themselves, so the crossover stays out of the way)
 *   DTN   0..127     the total spread in cents, 0..100, finer at the low end (64 = 25 cents, 90 = 50); SHAPE
 *                    (ENV / LFO -> SHP) adds to it. In cents the movement is faster on higher notes
 *   BEAT  OFF 1/1 1/2 1/4 1/8 1/16 1/4T 1/8T
 *                    OFF: DTN in cents. Otherwise the pair beats at that note value at the song tempo on every note
 *                    (a fixed detune in Hz worked out per note): the same movement across the whole line
 *   SUB   0..127     a clean sine on the fundamental, added after everything; above 0 the oscillators are
 *                    high-passed at ~120 Hz (24 dB) so the bottom is the sine alone, steady and mono
 *   CUT   cutoff     the ladder (ENV -> FLT and the LFO add to it); the Reese lives around 650 Hz - 2 kHz
 *   RES   0..127     its resonance (10..40 % is the usual range; from ~110 it sings)
 *   DRV   0..127     drive before the ladder, on the oscillators above the crossover only
 *   NTCH  0..127     a notch comb swept slowly (a short delay mixed against the signal): the extra hollow,
 *                    phasing movement; deeper and a little faster as it goes up; 0 off
 *
 * Every oscillator drifts a little on its own (about 1.5 cents, a slow random walk): two analog oscillators never
 * hold one beat pattern. Mono by design (engine poly 2: room for the release of the previous note while legato
 * slides). The per-voice state that does not fit in voice_t lives in the part's engine arena (engines.c eng_arena_of). */

#define RS_SAWS 7u
#define RS_COMB 256u                      /* the comb's line: up to 5.8 ms, a power of two */
#define RS_COMB_MIN 24                    /* its sweep in samples: 0.5 .. 4.5 ms */
#define RS_COMB_MAX 200
#define RS_XOVER_CUT (28 << 8)            /* the crossover: SVF_G index 28 = 118 Hz, 24 dB (two Butterworth stages) */
#define RS_XOVER_K 5798                   /* its damping, ~sqrt 2 (Butterworth), Q12 */
#define RS_DRIFT 30                       /* the drift walk's strength (sd ~1.5 cents) */
enum { RO_2SAW, RO_3SAW, RO_5SAW, RO_7SAW, RO_SAWSQ, RO_2SIN, RO_N };

typedef struct { int32_t G, H, G4, inv, k; } reese_ladder_t;   /* Clipper's ladder_t: Q16; k: Q12 */

typedef struct {
    uint32_t ph[RS_SAWS];                 /* the oscillators */
    uint32_t sub;                         /* the sub's phase */
    int32_t drift[RS_SAWS];               /* each one's wander (Q16 of the frequency x 17) */
    int32_t dvel[RS_SAWS];                /* its slow velocity (the walk is smoothed twice: no steps) */
    int32_t lad[4];                       /* the ladder's stages (Q12) */
    int32_t xo[4];                        /* the crossover's two SVF stages */
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

/* --- the oscillators -------------------------------------------------------------------------------------- */
/* where each oscillator sits in the spread, Q12 of half the spread (+-4096 = +-DTN / 2). The stacks use the
 * supersaw's spacing (Clipper's SUPER_OFF, after Adam Szabo's JP-8000 analysis), scaled so the outer pair spans the
 * spread; one oscillator in the centre */
static const uint8_t RS_N[RO_N] = {2, 3, 5, 7, 2, 2};
static const int16_t RS_POS[RO_N][RS_SAWS] = {
    {-4096, 4096},
    {0, -4096, 4096},
    {0, 1297, -1271, 4049, -4096},
    {0, 741, -727, 2314, -2341, 4000, -4096},
    {-4096, 4096},                                    /* SAWSQ: the saw below, the square above */
    {-4096, 4096},
};
static const int16_t RS_GAIN[RO_N] = {23000, 19000, 13000, 9400, 23000, 26000};   /* each oscillator's level */
/* the beat lengths of BEAT 1..7 in quarter notes x 4: 1/1 1/2 1/4 1/8 1/16 1/4T 1/8T */
static const uint8_t RS_BEAT4[8] = {0, 1, 2, 4, 8, 16, 6, 12};

/* a pitch offset in 1/16 cent -> the frequency ratio - 1, Q16: 2^(c/1200) - 1 to second order (0.1 % at 100 cents) */
static inline int32_t rs_cents_q16(int32_t c16)
{
    return c16 * 37854 / 16000 + (int32_t)((int64_t)c16 * c16 * 11 / 256000);
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
        V->xo[0] = V->xo[1] = V->xo[2] = V->xo[3] = 0;
        memset(V->line, 0, sizeof V->line);
    }
}

static void reese_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    reese_vc_t *V = &reese_of(t)->v[(uint32_t)(v - t->v) % NVOICE];
    uint32_t mode = (uint32_t)clamp(p[P_E0], 0, RO_N - 1), no = RS_N[mode], i, j;
    uint32_t beat = (uint32_t)clamp(p[P_E2], 0, 7);
    int32_t sub = p[P_E3] * 258;
    int32_t cut = (p[P_E4] << 8) + m->cutoff;
    int32_t drv = p[P_E6], hol = p[P_E7] * 258;
    int32_t xover = sub && mode != RO_2SIN;
    uint32_t inc = m->inc, sinc[RS_SAWS];
    int32_t g = RS_GAIN[mode];
    reese_ladder_t lc;
    tsvf_t xc;
    int32_t buf[CTL];

    /* each oscillator's drift: a twice-smoothed random walk, once a control tick, pulled back to centre */
    for (j = 0; j < no; j++) {
        int32_t r = (int32_t)(noise32(&V->rnd) >> 20) - 2048;          /* -2048..2047 */
        V->dvel[j] += (r * RS_DRIFT >> 7) - (V->dvel[j] >> 4);
        V->drift[j] += (V->dvel[j] >> 5) - (V->drift[j] >> 7);
    }
    if (beat) {
        /* the pair beats at the note value at the tempo whatever the note: a fixed offset in Hz, half each way
         * (beat Hz = BPM / 60 x quarters; the increment of 1 Hz is 2^32 / 44100 = 97391) */
        uint32_t bpm = (uint32_t)clamp(song.g[G_BPM], 20, 300);
        int32_t half = (int32_t)(bpm * RS_BEAT4[beat] * 97391u / 480u);   /* half the beat's increment */
        for (j = 0; j < no; j++)
            sinc[j] = inc + (uint32_t)((half * RS_POS[mode][j]) >> 12) +
                      (uint32_t)(int32_t)(((int64_t)(int32_t)(inc >> 4) * (V->drift[j] / 17)) >> 12);
    } else {
        /* DTN: the spread in cents, 0..100, x^2 (finer at the low end), SHAPE adds to it */
        int32_t x = clamp((p[P_E1] << 8) + m->shape - (64 << 8), 0, 127 << 8);
        int32_t s16 = (int32_t)((int64_t)x * x * 1600 / ((int64_t)(127 << 8) * (127 << 8)));   /* 1/16 cent */
        for (j = 0; j < no; j++) {
            int32_t off = rs_cents_q16((s16 / 2) * RS_POS[mode][j] / 4096) + V->drift[j] / 17;
            sinc[j] = inc + (uint32_t)(int32_t)(((int64_t)(int32_t)(inc >> 4) * off) >> 12);
        }
    }
    reese_ladder_coef(&lc, cut, p[P_E5]);
    if (xover)
        tsvf_coef(&xc, RS_XOVER_CUT, 40);                 /* (RES 40: the damping RS_XOVER_K) */

    /* the oscillators, one at a time (the phase stays in a register) */
    for (i = 0; i < n; i++)
        buf[i] = 0;
    for (j = 0; j < no; j++) {
        uint32_t q = V->ph[j], qi = sinc[j];
        if (mode == RO_2SIN)
            for (i = 0; i < n; i++, q += qi)
                buf[i] += mulq15(osc_sine(q), g);
        else if (mode == RO_SAWSQ && j == 1)
            for (i = 0; i < n; i++, q += qi)
                buf[i] += mulq15(osc_pulse(q, qi, 0x80000000u), g);
        else
            for (i = 0; i < n; i++, q += qi)
                buf[i] += mulq15(osc_saw(q, qi), g);
        V->ph[j] = q;
    }
    for (j = no; j < RS_SAWS; j++)                    /* the silent ones keep running: no jump when OSC turns */
        V->ph[j] += inc * n;

    /* the crossover, the drive, the ladder, the comb */
    {
        int32_t gain = 4096 + drv * 96;                       /* 1x .. ~4x, Q12 */
        int32_t x0 = V->xo[0], x1 = V->xo[1], x2 = V->xo[2], x3 = V->xo[3];
        uint32_t cph = V->cph, cw = V->cw;
        /* the comb's sweep: a slow triangle, 0.08 .. 0.7 Hz with NTCH (deeper, a little faster) */
        uint32_t crate = (uint32_t)(8000u + (uint32_t)p[P_E7] * 520u);
        for (i = 0; i < n; i++) {
            int32_t x = buf[i] >> 1, y;
            if (xover) {                                      /* the low end away (24 dB): the sine sub takes it */
                x = analog_tap(&xc, RS_XOVER_K, x, &x0, &x1, 2);
                x = analog_tap(&xc, RS_XOVER_K, x, &x2, &x3, 2);
            }
            x = x * gain >> 12;
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
        V->xo[0] = x0, V->xo[1] = x1, V->xo[2] = x2, V->xo[3] = x3;
        V->cph = cph;
        V->cw = cw;
    }

    /* the sub, clean and mono, after everything; then the amplitude */
    {
        uint32_t sp = V->sub;
        for (i = 0; i < n; i++, sp += inc) {
            int32_t s = buf[i];
            if (sub)
                s += mulq15(osc_sine(sp), sub);
            s = clamp(s, -60000, 60000);
            out[i] += mulq15(mulq15(s, amp_at(m, i)), VOICE_FS) << 1;
        }
        V->sub = sp;
    }
}

static const char *const N_REESE_OSC[] = {"2SAW", "3SAW", "5SAW", "7SAW", "SAWSQ", "2SIN"};
static const char *const N_REESE_BEAT[] = {"OFF", "1/1", "1/2", "1/4", "1/8", "1/16", "1/4T", "1/8T"};
static const preset_t REESE_PRESETS[] = {
    /* {OSC, DTN, BEAT, SUB, CUT, RES, DRV, NTCH}, {A D S R}, fenv, mono. CUT: 62 = 650 Hz, 71 = 1 kHz, 79 = 1.5 kHz,
     * 85 = 2 kHz; DTN: 64 = 25 cents, 82 = 42, 90 = 50, 110 = 75, 127 = 100. Glide on the slides (legato) */
    /* the jungle classic: two saws ~30 cents apart, low-passed around 900 Hz, a clean sub, a little drive */
    {"CLASSIC RS", {0, 70, 0, 80, 68, 18, 24, 0}, {0, 70, 120, 26}, 0, 1, FX(0, 0, 0, 6),
     XP(P_GLIDE + 1, 36, P_GLMODE + 1, 1, P_TRANS + 1, -24)},
    /* the 1988 original's idea: two detuned sines (+-27 cents), saturated for harmonics, no separate sub */
    {"OG REESE", {RO_2SIN, 93, 0, 0, 76, 8, 46, 0}, {0, 70, 124, 24}, 0, 1, FX(10, 0, 0, 6),
     XP(P_GLIDE + 1, 40, P_GLMODE + 1, 1, P_TRANS + 1, -24)},
    /* the classic with a slow notch sweep: the hollow, phasing vowel */
    {"HOLLOW RS", {0, 80, 0, 80, 70, 20, 30, 80}, {0, 70, 120, 26}, 0, 1, FX(0, 0, 0, 8),
     XP(P_GLIDE + 1, 36, P_GLMODE + 1, 1, P_TRANS + 1, -24)},
    /* techstep: a saw against a square, driven hard, brighter, a notch sweep and a slow filter LFO */
    {"TECHSTEP", {RO_SAWSQ, 90, 0, 76, 79, 30, 96, 50}, {0, 80, 116, 24}, 12, 1, FX(14, 0, 0, 4),
     XP(P_LRATE + 1, 20, P_LD_FLT + 1, 14, P_GLIDE + 1, 30, P_TRANS + 1, -24)},
    /* neuro: five saws spread 75 cents, the ladder resonating, the drive folding, a 1/8 filter wobble */
    {"NEURO RS", {RO_5SAW, 110, 0, 80, 74, 52, 120, 70}, {0, 80, 110, 22}, 24, 1, FX(18, 0, 0, 4),
     XP(P_LRATE + 1, 70, P_LD_FLT + 1, 34, P_TRANS + 1, -24)},
    /* the beat locked to quarter notes at the tempo on every note */
    {"SYNC RS", {0, 70, 3, 80, 68, 20, 30, 40}, {0, 70, 120, 26}, 0, 1, FX(0, 0, 0, 6),
     XP(P_GLIDE + 1, 36, P_GLMODE + 1, 1, P_TRANS + 1, -24)},
    /* dark and slow: three saws only 15 cents apart, low-passed low, a heavy sub */
    {"DARK RS", {RO_3SAW, 50, 0, 96, 52, 24, 40, 30}, {0, 70, 124, 30}, 0, 1, FX(0, 0, 0, 10),
     XP(P_GLIDE + 1, 40, P_GLMODE + 1, 1, P_TRANS + 1, -24)},
    /* a bright seven-saw spread a whole semitone wide, a pitch dive and glide: the hoover side of the family */
    {"HOOVER RS", {RO_7SAW, 127, 0, 30, 96, 10, 60, 40}, {0, 80, 110, 34}, 0, 1, FX(10, 30, 10, 18),
     XP(P_ED_PIT + 1, 20, P_GLIDE + 1, 50, P_TRANS + 1, -12)},
    /* mostly sub: a narrow pair (10 cents) just colouring a big sine */
    {"SUB REESE", {0, 40, 0, 110, 56, 10, 20, 0}, {0, 70, 124, 24}, 0, 1, FX(0, 0, 0, 4),
     XP(P_GLIDE + 1, 36, P_GLMODE + 1, 1, P_TRANS + 1, -24)},
};

static const engine_t ENG_REESE = {
    "REESE", {"OSC", "FLT"},
    {
        {"OSC", F_ENUM, 0, RO_N - 1, 0, N_REESE_OSC, 0},
        {"DTN", F_PCT, 0, 127, 70, 0, 0},
        {"BEAT", F_ENUM, 0, 7, 0, N_REESE_BEAT, 0},
        {"SUB", F_PCT, 0, 127, 80, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 68, 0, 0},
        {"RES", F_PCT, 0, 127, 18, 0, 0},
        {"DRV", F_PCT, 0, 127, 24, 0, 0},
        {"NTCH", F_PCT, 0, 127, 0, 0, 0},
    },
    REESE_PRESETS, sizeof(REESE_PRESETS) / sizeof(REESE_PRESETS[0]), 1, reese_note_on, reese_render,
    0x4C9F, {P_E1, P_E4, P_E7, P_E6}, 2,
};
