/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: the FX rack's echoes TAPE and BBD and its reverbs SPRING, TOMB, MOD, CELESTIAL, DOLMEN, TUMULUS.
 * Copyright (C) 2026 Jamie (TwistedGenetics).
 *
 * DREADNOUGHT's own, written 2026-10 from published designs, replacing 0.16's port of Carnac OS's fx.c (its function
 * bodies were not read for this: it was only measured from the outside, as a black box):
 *   - J. Dattorro, "Effect Design, Part 1: Reverberator and Other Filters", J. Audio Eng. Soc. 45 (9), 1997: the
 *     figure-eight tank (input diffusers, then two halves of allpass, delay, damping, allpass, delay, each half fed
 *     the other's end, the outputs tapped from inside the lines) for MOD, CELESTIAL, DOLMEN and TUMULUS. The line
 *     lengths, taps, coefficients, modulation and the four characters are our own.
 *   - M. R. Schroeder, "Natural Sounding Artificial Reverberation", J. Audio Eng. Soc. 10 (3), 1962, and the damped
 *     comb of Jezar's Freeverb (public domain), for TOMB: low-passed feedback combs in parallel, then allpasses. Our
 *     own lengths and stereo (a second tap in each comb).
 *   - V. Valimaki, J. Parker, J. S. Abel, "Parametric Spring Reverberation Effect", J. Audio Eng. Soc. 58 (7/8),
 *     2010: a cascade of stretched first-order allpasses, (a + z^-K) / (1 + a z^-K), in a delay loop, for SPRING's
 *     dispersion (each pass smears the low end later: the falling chirp, the "drip").
 *   - the tape loop (a varispeed record head averaging what it records into cells, play heads reading between them,
 *     wow and flutter swinging the heads, head loss, bass loss, saturation) and the bucket brigade (a clocked ring of
 *     held samples between fixed low-passes and a 2:1 compander: the device as A. Holters and U. Zolzer describe it,
 *     "Physical Modelling of a Bucket-Brigade Device Based Delay Line", DAFx 2011, reduced to a simple held-bucket
 *     model) are our own.
 *
 * The reverbs run at half rate (22.05 kHz, as DELAY and MASSIVE do): the send in pairs, LOW CUT (two one-poles)
 * before the predelay, the reverb, then back to full rate by linear interpolation. Every line holds int16. A float
 * stored is held at full scale and rounded to the nearest above 16 LSB, toward zero below; every loop's gain is
 * under 1 below FEEDBACK 100, so a tail ends in exact silence. Float states are flushed each block (no denormals).
 * Coefficients are made again only when a value (or the tempo) changes. No 64-bit arithmetic; no libm (X0X's
 * fastmath). */
#include "dn_fx.h"
#include "dn_dsp.h"
#include "dsp/fastmath.h"

#define FXC_FS 44100.f
#define FXC_FH 22050.f                                  /* the reverbs' rate */

/* the memory */
#define TP_CELLS 16384u                                 /* TAPE: the tape's cells */
#define BB_N 8192u                                      /* BBD: buckets */
#define PRE_N 2224u                                     /* the reverbs' predelay, half rate (127 x 35 / 2 + 1) */
#define SP_L0 811u                                      /* SPRING: the two springs' round trips, half rate */
#define SP_L1 1031u
#define TB_RING 4096u                                   /* TOMB: its ring (six lines, 2858 cells) */
#define TK_RING 32768u                                  /* the tanks' ring (DOLMEN: half) */
#define SH_N 2048u                                      /* CELESTIAL: the shifter's line */

uint32_t dn_fxc_ram(uint32_t type)
{
    switch (type) {
    case DN_FX_TAPE:
        return TP_CELLS * 2u;
    case DN_FX_BBD:
        return BB_N * 2u;
    case DN_FX_SPRING:
        return 2u * DN_SP_M * 2u * 4u + ((PRE_N + SP_L0 + SP_L1) * 2u + 3u) / 4u * 4u;
    case DN_FX_TOMB:
        return (PRE_N + TB_RING) * 2u;
    case DN_FX_MOD:
    case DN_FX_TUMULUS:
        return PRE_N * 2u + TK_RING * 2u;
    case DN_FX_CELEST:
        return PRE_N * 2u + TK_RING * 2u + SH_N * 2u;
    case DN_FX_DOLMEN:
        return PRE_N * 2u + TK_RING;
    default:
        return 0u;
    }
}

/* ---- shared pieces ------------------------------------------------------------------------------------------- */
static inline float fxc_tiny(float v) { return v < 1e-12f && v > -1e-12f ? 0.f : v; }
/* to a cell: held at full scale; rounded to the nearest above 16 LSB (a long tail keeps its decay), toward zero
 * below (no loop can hold a small value up: the tail ends in silence). No branch. */
static inline int16_t fxc_cell(float v)
{
    fm_bits_t h;
    uint32_t sg, big;
    v = v > 32767.f ? 32767.f : v < -32767.f ? -32767.f : v;
    h.f = v;
    sg = h.u & 0x80000000u;
    big = 0u - (uint32_t)((h.u & 0x7FFFFFFFu) > 0x41800000u);   /* |v| > 16 */
    h.u = (0x3F000000u & big) | sg;                                /* +-0.5 or 0 */
    return (int16_t)(int32_t)(v + h.f);
}
static float fxc_k(float fc, float fs) { return 1.f - fm_expf(-FM_TWO_PI * fc / fs); }   /* a one-pole's step */
/* a soft clip, full scale 32768, without a divide: the quintic (15 u - 10 u^3 + 3 u^5) / 8 of u = x / 1.875 (the
 * integral of (1 - u^2)^2: slope 1 at 0, at full scale from 1.875 x full scale with its first two derivatives 0) */
static inline float fxc_sat(float x)
{
    float u = x * (1.f / (32768.f * 1.875f)), u2;
    if (u >= 1.f)
        return 32767.f;
    if (u <= -1.f)
        return -32767.f;
    u2 = u * u;
    return (32767.f / 8.f) * u * (15.f + u2 * (3.f * u2 - 10.f));
}
static inline float fxc_noise(uint32_t *r)              /* -1 .. 1 */
{
    uint32_t s = *r;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    *r = s;
    return (float)(int32_t)s * (1.f / 2147483648.f);
}
static float fxc_wrap(float ph) { return ph >= 1.f ? ph - fm_floorf(ph) : ph; }
/* the delays' loop: TIME at the tempo (twice it for ping-pong: two echoes a lap), halved while too long for the
 * line, doubled while too short, so it stays on the beat */
static float fxc_loop(const dn_fx_p_t *p, uint32_t bpm, uint32_t lo, uint32_t hi)
{
    uint32_t d = dn_fx_sync_samples(p->p[0], bpm), l = (p->p[3] & 1u) ? 2u * d : d;
    while (l > hi)
        l >>= 1;
    while (l < lo)
        l <<= 1;
    return (float)l;
}
/* FEEDBACK 0..127 -> a lap's gain: (v / 100)^1.6 up to 100 (70: -5 dB), then on to 1.5 (the runaway) */
static float fxc_fb(uint32_t v)
{
    float u = (float)v * 0.01f;
    return v <= 100u ? (u > 0.f ? fm_exp2f(1.6f * fm_log2f(u)) : 0.f) : 1.f + 1.85f * (u - 1.f);
}
/* the delays' coefficients are stale (a value or the tempo moved) */
static int fxc_stale(dn_fxt_t *t, const dn_fx_p_t *p, uint32_t bpm)
{
    uint32_t i;
    int st = !t->started || t->bpm != (uint16_t)bpm;
    for (i = 0; i < 4u; i++)
        st |= t->last[i] != p->p[i], t->last[i] = p->p[i];
    t->bpm = (uint16_t)bpm;
    return st;
}

/* ---- TAPE -----------------------------------------------------------------------------------------------------
 * A loop of TP_CELLS cells. The record head runs at the tape speed (cells a sample, at most 1): each cell it passes
 * gets the average of what it recorded on the way (so a slow tape is a low-passed one). Up to TP_GAPMAX cells the
 * tape runs at full speed and the head gap is the delay; longer, the gap stays and the tape slows (down to 1/6), as a
 * varispeed echo does, darker. A new TIME moves gap and speed there smoothly: the echoes bend. The play heads read
 * between cells (linear) at the gap, swung by wow (0.55 Hz, and a slower drift) and flutter (6.7 Hz); what they play
 * loses top (head loss) and bottom, and goes back to the record head at FEEDBACK through a soft saturation (DEGRADE
 * drives it; past 100 the loop holds a howl, darker, from a hiss). PING PONG: a second play head at half the gap
 * feeds the left, the far head the right and the loop (a lap is then two echoes: the right's at the square root of
 * the lap's gain, so they fall evenly). */
#define TP_MASK (TP_CELLS - 1u)
#define TP_GAPMAX 16000.f
#define TP_MAXD 96000u

static inline float fxc_tp_rd(const int16_t *line, float pos)   /* between cells; pos >= 0 */
{
    int32_t i = (int32_t)pos;
    float fr = pos - (float)i, a = line[(uint32_t)i & TP_MASK], b = line[(uint32_t)(i + 1) & TP_MASK];
    return a + fr * (b - a);
}

static void fxc_tape(dn_fxt_t *t, const dn_fx_p_t *p, const int32_t *in, int32_t *wl, int32_t *wr, uint32_t n,
                     uint32_t bpm)
{
    float deg = (float)p->p[2] * (1.f / 127.f), m1, dm, m, ph, acc, rate, gap, pos;
    float fb, kl, kh, rgain, drive, undrive, seed, rate_t, gap_t;
    float lb = t->f[0], hb = t->f[1], la = t->f[2];
    uint32_t pp = p->p[3] & 1u, i, w = t->w;
    int16_t *line = t->line;
    if (fxc_stale(t, p, bpm)) {
        float len = fxc_loop(p, bpm, 1u, TP_MAXD);
        fb = fxc_fb(p->p[1]);
        t->c[0] = fb;
        t->c[1] = fxc_k(11000.f * fm_exp2f(-2.6f * deg) * (fb > 1.f ? 1.f / (1.f + 5.f * (fb - 1.f)) : 1.f), FXC_FS);
        t->c[2] = fxc_k(35.f + 150.f * deg, FXC_FS);
        t->c[3] = pp ? (fb < 1.f ? fm_sqrtf(fb) : 1.f) : 1.f;
        t->c[4] = len > TP_GAPMAX ? TP_GAPMAX / len : 1.f;
        t->c[5] = len * t->c[4];
        if (!t->started)                                /* (the heads start where the first TIME puts them) */
            t->rate = t->c[4], t->gap = t->c[5], t->started = 1u, t->rng = 0x2545F491u;
    }
    fb = t->c[0], kl = t->c[1], kh = t->c[2], rgain = t->c[3], rate_t = t->c[4], gap_t = t->c[5];
    drive = 1.f + 1.5f * deg, undrive = 1.f / drive, seed = fb > 1.f ? 24.f * (fb - 1.f) : 0.f;
    if (t->gap != gap_t || t->rate != rate_t) {         /* a new TIME: speed and gap move there (the echoes bend) */
        float dg = (gap_t - t->gap) * 0.08f, lim = 0.5f * (float)n;
        t->gap += dg > lim ? lim : dg < -lim ? -lim : dg;
        t->rate += (rate_t - t->rate) * 0.08f;
        if (t->gap - gap_t < 0.01f && gap_t - t->gap < 0.01f)
            t->gap = gap_t;
        if (t->rate - rate_t < 1e-6f && rate_t - t->rate < 1e-6f)
            t->rate = rate_t;
    }
    t->lfo[0] = fxc_wrap(t->lfo[0] + (float)n * (0.55f / FXC_FS));
    t->lfo[1] = fxc_wrap(t->lfo[1] + (float)n * (6.7f / FXC_FS));
    m1 = ((4.f + 60.f * deg) * (fm_sin_turns(t->lfo[0]) + 0.35f * fm_sin_turns(0.237f * t->lfo[0] + 0.1f)) +
          (0.3f + 3.f * deg) * fm_sin_turns(t->lfo[1])) * t->rate;
    dm = (m1 - t->mod) / (float)(n ? n : 1u);
    m = t->mod, ph = t->ph, acc = t->acc, rate = t->rate, gap = t->gap;
    pos = (float)(w + TP_CELLS) + ph;                   /* the record head, a lap on (the play heads stay >= 0) */
    for (i = 0; i < n; i++) {
        float y, eb, ea = 0.f, rec, at;
        m += dm;
        at = pos - m;
        y = fxc_tp_rd(line, at - gap);
        lb += kl * (y - lb);
        hb += kh * (lb - hb);
        eb = lb - hb;
        if (pp) {                                       /* (the left's head: its bass loss is the far head's) */
            y = fxc_tp_rd(line, at - 0.5f * gap);
            la += kl * (y - la);
            ea = la - hb;
        }
        rec = (float)in[i] + fb * eb;
        if (seed > 0.f)
            rec += seed * fxc_noise(&t->rng);
        rec = fxc_sat(rec * drive) * undrive;
        ph += rate;
        pos += rate;
        acc += rec * rate;
        if (ph >= 1.f) {                                /* a cell passed the record head */
            ph -= 1.f;
            line[w] = fxc_cell(acc - rec * ph);
            w = (w + 1u) & TP_MASK;
            acc = rec * ph;
            if (!w)
                pos -= (float)TP_CELLS;
        }
        wl[i] = (int32_t)(pp ? ea : eb);
        wr[i] = (int32_t)(rgain * eb);
    }
    t->w = w, t->ph = ph, t->acc = fxc_tiny(acc), t->mod = m1;
    t->f[0] = fxc_tiny(lb), t->f[1] = fxc_tiny(hb), t->f[2] = fxc_tiny(la);
}

/* ---- BBD ------------------------------------------------------------------------------------------------------
 * BB_N buckets in a ring, clocked BB_N times a delay (from 8 times a sample down to 0.15). Each tick the oldest
 * bucket leaves (held until the next: the steps, and the clock's images, are the crumble) and the input as it is
 * then takes its place. Around it the fixed parts of a BBD echo: two-pole low-passes before and after (~4 kHz, lower
 * as it wears), a 2:1 compander (the compressor before, sqrt(ref / level), the expander after, level / ref, each
 * with its own peak detector, looking at the buckets as they are clocked: they never quite agree, the breath; below
 * its floor the expander closes, so the bucket hiss only rides on a signal), and a hiss in the buckets. FEEDBACK
 * past 100 runs away into the buckets' clip. DEGRADE: darker filters, more hiss, a wobbling clock, a slower
 * expander. PING PONG as TAPE's, the half-way bucket the left. */
#define BB_MASK (BB_N - 1u)
#define BB_REF 32767.f                                  /* the compander's unity: full scale in, full scale out */
#define BB_GMAX 12.f
#define BB_MAXD 54000u
#define BB_MIND 1024u

static void fxc_bbd(dn_fxt_t *t, const dn_fx_p_t *p, const int32_t *in, int32_t *wl, int32_t *wr, uint32_t n,
                    uint32_t bpm)
{
    float deg = (float)p->p[2] * (1.f / 127.f), fb, kpre, kpost, khp, rgain, clk, rc, re, hiss = 2.f + 30.f * deg;
    float p0 = t->f[0], p1 = t->f[1], qb0 = t->f[2], qb1 = t->f[3], qa0 = t->f[4], qa1 = t->f[5], hp = t->f[6];
    float e0 = t->env[0], e1 = t->env[1], e2 = t->env[2], yb = t->hold[1], ya = t->hold[0], ph = t->ph;
    float efb = t->f[7];
    uint32_t pp = p->p[3] & 1u, i, w = t->w;
    int16_t *line = t->line;
    if (fxc_stale(t, p, bpm)) {
        float fc = 4200.f * fm_exp2f(-1.2f * deg);
        fb = fxc_fb(p->p[1]);
        t->c[0] = fb;
        t->c[1] = fxc_k(fc, FXC_FS);
        t->c[2] = fb > 1.f ? fxc_k(fc / (1.f + 4.f * (fb - 1.f)), FXC_FS) : t->c[1];
        t->c[3] = pp ? (fb < 1.f ? fm_sqrtf(fb) : 1.f) : 1.f;
        t->c[4] = (float)BB_N / fxc_loop(p, bpm, BB_MIND, BB_MAXD);
        t->c[5] = fxc_k(40.f, FXC_FS);
        if (!t->started)
            t->rate = t->c[4], t->started = 1u, t->rng = 0x9E3779B9u, t->lfo[0] = 0.5f;
    }
    fb = t->c[0], kpre = t->c[1], kpost = t->c[2], rgain = t->c[3], khp = t->c[5];
    if (t->rate != t->c[4]) {                           /* a new TIME: the clock glides (the echoes bend) */
        t->rate += (t->c[4] - t->rate) * 0.08f;
        if (t->rate - t->c[4] < 1e-6f && t->c[4] - t->rate < 1e-6f)
            t->rate = t->c[4];
    }
    t->lfo[0] = fxc_wrap(t->lfo[0] + (float)n * (0.43f / FXC_FS));
    clk = t->rate * (1.f + (0.0005f + 0.006f * deg) * fm_sin_turns(t->lfo[0]));
    rc = 1.f - 0.0006f / clk;                           /* the detectors' fall a tick: ~40 ms, the expander's slower */
    re = 1.f - 0.0006f * (1.f - 0.6f * deg) / clk;
    rc = rc < 0.5f ? 0.5f : rc, re = re < 0.5f ? 0.5f : re;
    for (i = 0; i < n; i++) {
        float u = (float)in[i] + fb * efb;
        p0 += kpre * (u - p0);
        p1 += kpre * (p0 - p1);
        ph += clk;
        if (ph >= 1.f) {                                /* the clock: the compander where a bucket is filled */
            float g, hb = 0.f, ha = 0.f;
            e0 = fm_maxf(fm_fabsf(p1), e0 * rc);
            g = e0 > BB_REF / (BB_GMAX * BB_GMAX) ? fm_rsqrtf(e0 * (1.f / BB_REF)) : BB_GMAX;
            u = fxc_sat(p1 * g);
            do {                                        /* a tick: the oldest bucket out, the input in */
                ph -= 1.f;
                hb = line[w];
                ha = line[(w + BB_N / 2u) & BB_MASK];
                line[w] = fxc_cell(u + hiss * fxc_noise(&t->rng));
                w = (w + 1u) & BB_MASK;
            } while (ph >= 1.f);
            e1 = fm_maxf(fm_fabsf(hb), e1 * re);
            yb = hb * e1 * (1.f / BB_REF);
            e2 = fm_maxf(fm_fabsf(ha), e2 * re);
            ya = ha * e2 * (1.f / BB_REF);
        }
        qb0 += kpost * (yb - qb0);
        qb1 += kpost * (qb0 - qb1);
        hp += khp * (qb1 - hp);
        efb = qb1 - hp;
        if (pp) {
            qa0 += kpost * (ya - qa0);
            qa1 += kpost * (qa0 - qa1);
        }
        wl[i] = (int32_t)(pp ? qa1 : efb);
        wr[i] = (int32_t)(rgain * efb);
    }
    t->w = w, t->ph = ph, t->hold[0] = ya, t->hold[1] = yb;
    t->env[0] = fxc_tiny(e0), t->env[1] = fxc_tiny(e1), t->env[2] = fxc_tiny(e2);
    t->f[0] = fxc_tiny(p0), t->f[1] = fxc_tiny(p1), t->f[2] = fxc_tiny(qb0), t->f[3] = fxc_tiny(qb1);
    t->f[4] = fxc_tiny(qa0), t->f[5] = fxc_tiny(qa1), t->f[6] = fxc_tiny(hp), t->f[7] = fxc_tiny(efb);
}

/* ---- the reverbs' front and back ---------------------------------------------------------------------------- */
#define FXC_HMAX 17u                                    /* half-rate samples a block, at most */

/* TIME 0..127 -> the RT60 asked of the loops (s): a curve through five points (TIME 0, 32, 64, 96, 127), log between */
static float fxc_rt(uint32_t v, const float *pt)
{
    uint32_t k = v >= 127u ? 3u : v * 4u / 127u;
    float f = (float)v * (4.f / 127.f) - (float)k, a = fm_log2f(pt[k]);
    return fm_exp2f(a + f * (fm_log2f(pt[k + 1u]) - a));
}
/* the gain a pass of len half-rate samples gets for an RT60 of rt: 10^(-3 len / (rt fh)) */
static float fxc_g(float len, float rt) { return fm_exp2f(-9.965784f * len / (rt * FXC_FH)); }

/* the send to half rate (pairs), LOW CUT (off, 30 Hz .. 800 Hz: two one-pole high-passes), the predelay */
static uint32_t fxc_front(dn_fxf_t *f, const dn_fx_p_t *p, const int32_t *in, float *xh, uint32_t n)
{
    uint32_t i, m = 0, d = ((uint32_t)p->p[2] * 35u + 1u) / 2u, w = f->pre_w;
    float k = f->c[0], c0 = f->lc[0], c1 = f->lc[1], x0 = f->x0;
    uint8_t ph = f->ph;
    for (i = 0; i < n; i++) {
        float x;
        if (!ph) {
            x0 = (float)in[i];
            ph = 1u;
            continue;
        }
        ph = 0u;
        x = 0.5f * (x0 + (float)in[i]);
        if (k > 0.f) {
            c0 += k * (x - c0);
            x -= c0;
            c1 += k * (x - c1);
            x -= c1;
        }
        f->pre[w] = fxc_cell(x);
        if (d)
            x = f->pre[w >= d ? w - d : w + PRE_N - d];
        w = w + 1u >= PRE_N ? 0u : w + 1u;
        xh[m++] = x;
    }
    f->pre_w = w, f->ph = ph, f->x0 = x0, f->lc[0] = fxc_tiny(c0), f->lc[1] = fxc_tiny(c1);
    return m;
}
/* back to full rate: each new half-rate output, then the midpoint to the next */
static void fxc_back(dn_fxf_t *f, uint8_t ph, const float *yl, const float *yr, int32_t *wl, int32_t *wr, uint32_t n)
{
    uint32_t i, j = 0;
    float ol = f->yl, or_ = f->yr;
    for (i = 0; i < n; i++) {
        if (!ph) {
            wl[i] = (int32_t)ol;
            wr[i] = (int32_t)or_;
            ph = 1u;
        } else {
            wl[i] = (int32_t)(0.5f * (ol + yl[j]));
            wr[i] = (int32_t)(0.5f * (or_ + yr[j]));
            ol = yl[j], or_ = yr[j];
            j++;
            ph = 0u;
        }
    }
    f->yl = ol, f->yr = or_;
}

/* ---- SPRING ---------------------------------------------------------------------------------------------------
 * Two springs (37 and 47 ms round trips). Each is a loop: the round trip's end, damped (DAMP) and with its bass taken
 * out (a spring carries none), back in at the gain TIME asks, through DN_SP_M stretched allpasses (K = 2, a < 0: the
 * lows delayed more than the highs, ~8 ms more each pass), into the line. Every echo is chirped again, falling. */
static const float SP_A[2] = {-0.70f, -0.64f};
static const uint32_t SP_L[2] = {SP_L0, SP_L1};
static const float SP_RT[5] = {0.31f, 0.46f, 0.74f, 1.55f, 7.f};
#define SP_IN 0.35f
#define SP_OUT 1.3f

static void fxc_spring_coef(dn_fxf_t *f, const dn_fx_p_t *p)
{
    float rt = fxc_rt(p->p[0], SP_RT);
    f->c[1] = fxc_g((float)SP_L0, rt), f->c[2] = fxc_g((float)SP_L1, rt);
    f->c[3] = fxc_k(7000.f * fm_exp2f(-2.f * (float)p->p[1] * (1.f / 127.f)), FXC_FH);
    f->c[4] = fxc_k(90.f, FXC_FH);
}
static void fxc_spring(dn_fxs_t *sp, const float *x, float *yl, float *yr, uint32_t m)
{
    float kd = sp->f.c[3], kh = sp->f.c[4];
    uint32_t i, j, s, k = sp->k;
    for (j = 0; j < 2u; j++) {
        int16_t *line = sp->line[j];
        float *ap = sp->ap + j * DN_SP_M * 2u, a = SP_A[j], g = sp->f.c[1u + j], lp = sp->lp[j], hp = sp->hp[j];
        uint32_t w = sp->w[j], kk = k, len = SP_L[j];
        float *y = j ? yr : yl;
        for (i = 0; i < m; i++) {
            float *z = ap + kk, u, e, v = line[w];
            lp += kd * (v - lp);
            hp += kh * (lp - hp);
            e = lp - hp;
            u = x[i] * SP_IN + g * e;
            for (s = 0; s < DN_SP_M; s++) {             /* (a + z^-2) / (1 + a z^-2) */
                float zs = z[2u * s];
                v = u - a * zs;
                u = a * v + zs;
                z[2u * s] = v;
            }
            line[w] = fxc_cell(u);
            w = w + 1u >= len ? 0u : w + 1u;
            kk ^= 1u;
            y[i] = e;
        }
        sp->w[j] = w, sp->lp[j] = fxc_tiny(lp), sp->hp[j] = fxc_tiny(hp);
    }
    sp->k = k ^ (m & 1u);
    for (i = 0; i < m; i++) {
        float l = yl[i], r = yr[i];
        yl[i] = SP_OUT * (l + 0.3f * r);
        yr[i] = SP_OUT * (r + 0.3f * l);
    }
    for (j = 0; j < 2u * DN_SP_M * 2u; j++)
        sp->ap[j] = fxc_tiny(sp->ap[j]);
}

/* ---- TOMB -----------------------------------------------------------------------------------------------------
 * A small dark stone room: four low-passed feedback combs (25 .. 34 ms) in parallel, each pass of each comb losing
 * what TIME says and its top (DAMP); the left their sum through one allpass, the right the same combs tapped 19
 * samples younger through another (the two sides decorrelate, the low end stays shared). All six lines share one
 * ring and one pointer moving down (a line's cell d samples old is d past its start). */
static const uint16_t TB_LEN[6] = {557, 617, 683, 751, 113, 131};
static const uint16_t TB_OFF[6] = {0, 558, 1176, 1860, 2612, 2726};   /* (each line its length and one) */
static const float TB_RT[5] = {0.7f, 0.89f, 1.16f, 1.91f, 3.8f};
#define TB_M (TB_RING - 1u)
#define TB_SPREAD 19u
#define TB_IN 0.3f
#define TB_OUT 1.7f

static void fxc_tomb_coef(dn_fxf_t *f, const dn_fx_p_t *p)
{
    float rt = fxc_rt(p->p[0], TB_RT);
    uint32_t j;
    for (j = 0; j < 4u; j++)
        f->c[1u + j] = fxc_g((float)TB_LEN[j], rt);
    f->c[5] = 0.05f + 0.6f * (float)p->p[1] * (1.f / 127.f);
}
static inline float fxc_tb_ap(int16_t *c, uint32_t at, uint32_t len, float x, float g)   /* v = x + g s, y = s - g v */
{
    float s = c[(at + len) & TB_M], v = x + g * s;
    c[at & TB_M] = fxc_cell(v);
    return s - g * v;
}
static void fxc_tomb(dn_fxr_t *r, const float *x, float *yl, float *yr, uint32_t m)
{
    float g0 = r->f.c[1], g1 = r->f.c[2], g2 = r->f.c[3], g3 = r->f.c[4], d = r->f.c[5];
    float lp0 = r->lp[0], lp1 = r->lp[1], lp2 = r->lp[2], lp3 = r->lp[3];
    int16_t *c = r->cell;
    uint32_t i, q = r->p;
    for (i = 0; i < m; i++) {
        float in = x[i] * TB_IN, y0, y1, y2, y3, sr;
        sr = (float)(c[(q + TB_OFF[0] + TB_LEN[0] - TB_SPREAD) & TB_M] + c[(q + TB_OFF[1] + TB_LEN[1] - TB_SPREAD) & TB_M] +
                     c[(q + TB_OFF[2] + TB_LEN[2] - TB_SPREAD) & TB_M] + c[(q + TB_OFF[3] + TB_LEN[3] - TB_SPREAD) & TB_M]);
#define TB_COMB(k, y, lp, g)                                                                                           \
    y = c[(q + TB_OFF[k] + TB_LEN[k]) & TB_M];                                                                         \
    lp = y + d * (lp - y);                                                                                             \
    c[(q + TB_OFF[k]) & TB_M] = fxc_cell(in + g * lp)
        TB_COMB(0, y0, lp0, g0);
        TB_COMB(1, y1, lp1, g1);
        TB_COMB(2, y2, lp2, g2);
        TB_COMB(3, y3, lp3, g3);
#undef TB_COMB
        yl[i] = TB_OUT * fxc_tb_ap(c, q + TB_OFF[4], TB_LEN[4], 0.5f * (y0 + y1 + y2 + y3), 0.5f);
        yr[i] = TB_OUT * fxc_tb_ap(c, q + TB_OFF[5], TB_LEN[5], 0.5f * sr, 0.5f);
        q = (q - 1u) & TB_M;
    }
    r->p = q;
    r->lp[0] = fxc_tiny(lp0), r->lp[1] = fxc_tiny(lp1), r->lp[2] = fxc_tiny(lp2), r->lp[3] = fxc_tiny(lp3);
}

/* ---- the tanks: MOD, CELESTIAL, DOLMEN, TUMULUS ----------------------------------------------------------------
 * Dattorro's figure eight at half rate in one ring (every line a stretch of it, one pointer moving down). In: a
 * low-pass (the character's bandwidth), four allpasses (diffusion). Then two halves, each: the input plus the other
 * half's end (x decay), an allpass (swung by an LFO where the character moves), a delay, a damping low-pass (DAMP), x
 * decay, an allpass, a delay. Each side's output is seven taps inside the other half's lines and its own (signs mixed).
 * decay from TIME: the loop is the eight lines, four decays a lap. CELESTIAL: the tank's output band-passed, shifted
 * an octave up (two read heads crossfading, each moving at twice the speed) and fed back to its input: each lap it
 * climbs. */
typedef struct {
    float size, rt[5];                                  /* the lines' scale; TIME's RT60 curve */
    float bw, dhi, dlo;                                 /* the input's low-pass; DAMP's range (Hz) */
    float ex, r0, r1;                                   /* the LFO's swing (cells) and rates (Hz) */
    float gd[2], g1, g2;                                /* the diffusers' and the tank allpasses' coefficients */
    float gin, gout, shim;
} fxc_tank_t;
static const fxc_tank_t TK[4] = {
    /* MOD: a big moving hall */
    {1.25f, {0.63f, 1.04f, 1.98f, 4.6f, 34.f}, 10000.f, 10500.f, 4500.f, 9.f, 0.73f, 1.07f, {0.72f, 0.62f}, -0.62f,
     0.5f, 0.3f, 1.26f, 0.f},
    /* CELESTIAL: bigger, bright, gently moving, shimmering */
    {1.3f, {0.57f, 0.93f, 1.74f, 3.6f, 21.f}, 10500.f, 10500.f, 4500.f, 5.f, 0.41f, 0.59f, {0.72f, 0.62f}, -0.62f,
     0.5f, 0.3f, 1.08f, 0.45f},
    /* DOLMEN: small, dense, still, bright */
    {0.6f, {0.41f, 0.56f, 0.85f, 1.25f, 1.9f}, 10500.f, 10500.f, 3500.f, 0.f, 0.f, 0.f, {0.76f, 0.68f}, -0.7f, 0.6f,
     0.3f, 1.54f, 0.f},
    /* TUMULUS: a cavern, long and dark, barely moving */
    {1.95f, {1.13f, 1.69f, 2.93f, 6.8f, 36.f}, 6000.f, 6000.f, 1500.f, 3.f, 0.19f, 0.27f, {0.7f, 0.6f}, -0.6f, 0.5f,
     0.3f, 1.55f, 0.f},
};
/* the lines at size 1 (half-rate samples): four diffusers; left: allpass, delay, allpass, delay; right: the same */
static const uint16_t TK_LEN[DN_FXK_LINES] = {97, 67, 251, 181, 531, 3173, 1405, 2631, 732, 2979, 1865, 2247};
/* the taps: {line, place (/256)}, the left's then the right's, signed as TK_SG */
static const uint8_t TK_TAP[2][7][2] = {
    {{9, 28}, {9, 136}, {10, 74}, {11, 182}, {5, 59}, {6, 156}, {7, 20}},
    {{5, 31}, {5, 131}, {6, 80}, {7, 176}, {9, 54}, {10, 150}, {11, 25}},
};
static const float TK_SG[7] = {1.f, 1.f, -1.f, 1.f, -1.f, -1.f, -1.f};
#define TK_SHW 2000u                                    /* CELESTIAL: the shifter's grain, cells */

static const fxc_tank_t *fxc_tk(uint32_t type) { return &TK[type - DN_FX_MOD]; }

static void fxc_tank_init(dn_fxk_t *k, uint32_t type, int16_t *ring, uint32_t cells)
{
    const fxc_tank_t *c = fxc_tk(type);
    uint32_t j, t, at = 0, ex = (uint32_t)c->ex + 1u;
    k->ring = ring, k->mask = cells - 1u;
    for (j = 0; j < DN_FXK_LINES; j++) {
        float s = j < 4u ? (c->size < 1.3f ? c->size : 1.3f) : c->size;
        k->len[j] = ((uint32_t)((float)TK_LEN[j] * s)) | 1u;
        k->off[j] = at;
        at += k->len[j] + 2u + ((j == 4u || j == 8u) ? 2u * ex : 0u);
    }
    for (j = 0; j < 2u; j++)
        for (t = 0; t < 7u; t++) {
            uint32_t l = TK_TAP[j][t][0];
            k->tap[j][t] = k->off[l] + 1u + k->len[l] * TK_TAP[j][t][1] / 256u;
        }
}
static void fxc_tank_coef(dn_fxk_t *k, uint32_t type, const dn_fx_p_t *p)
{
    const fxc_tank_t *c = fxc_tk(type);
    float lap = 0.f, decay;
    uint32_t j;
    for (j = 4; j < DN_FXK_LINES; j++)
        lap += (float)k->len[j];
    decay = fm_exp2f(-9.965784f * lap / (4.f * fxc_rt(p->p[0], c->rt) * FXC_FH));
    k->f.c[1] = decay > 0.985f ? 0.985f : decay;
    k->f.c[2] = fxc_k(c->dhi * fm_exp2f(fm_log2f(c->dlo / c->dhi) * (float)p->p[1] * (1.f / 127.f)), FXC_FH);
    k->f.c[3] = fxc_k(c->bw, FXC_FH);
    k->f.c[4] = fxc_k(4000.f, FXC_FH);
    k->f.c[5] = fxc_k(120.f, FXC_FH);
}

#define TK_RD(at) ((float)rg[(at) & mask])
#define TK_WR(at, v) (rg[(at) & mask] = fxc_cell(v))
/* an allpass on line j: v = x + g s, y = s - g v (s: the line's end) */
#define TK_AP(j, x, g)                                                                                                 \
    do {                                                                                                               \
        float s_ = TK_RD(q + k->off[j] + k->len[j]), v_ = (x) + (g) * s_;                                             \
        TK_WR(q + k->off[j], v_);                                                                                      \
        (x) = s_ - (g) * v_;                                                                                           \
    } while (0)
/* the same, its end swung: read len + e cells back (e >= 0, between cells) */
#define TK_APM(j, x, g, e)                                                                                             \
    do {                                                                                                               \
        uint32_t at_ = q + k->off[j] + k->len[j] + (uint32_t)(e);                                                      \
        float fr_ = (e) - (float)(uint32_t)(e), a_ = TK_RD(at_), s_ = a_ + fr_ * (TK_RD(at_ + 1u) - a_);              \
        float v_ = (x) + (g) * s_;                                                                                     \
        TK_WR(q + k->off[j], v_);                                                                                      \
        (x) = s_ - (g) * v_;                                                                                           \
    } while (0)

static void fxc_tank(dn_fxk_t *k, uint32_t type, const float *x, float *yl, float *yr, uint32_t m)
{
    const fxc_tank_t *c = fxc_tk(type);
    float decay = k->f.c[1], kd = k->f.c[2], kb = k->f.c[3], ks = k->f.c[4], kh = k->f.c[5];
    float m0 = k->m[0], m1 = k->m[1], d0 = 0.f, d1 = 0.f, bw = k->bw, dp0 = k->damp[0], dp1 = k->damp[1];
    float z0 = k->z[0], z1 = k->z[1], shout = k->shl[3];
    float gd0 = c->gd[0], gd1 = c->gd[1], g1 = c->g1, g2 = c->g2, gin = c->gin, gout = c->gout, shim = c->shim;
    int16_t *rg = k->ring;
    uint32_t i, t, mask = k->mask, q = k->p, moving = c->ex > 0.f;
    if (moving && m) {                                  /* the LFOs at the block's end; swung linearly between */
        float e0, e1;
        k->lph[0] = fxc_wrap(k->lph[0] + (float)m * c->r0 / FXC_FH);
        k->lph[1] = fxc_wrap(k->lph[1] + (float)m * c->r1 / FXC_FH);
        e0 = c->ex * (1.f + fm_sin_turns(k->lph[0]));
        e1 = c->ex * (1.f + fm_sin_turns(k->lph[1]));
        d0 = (e0 - m0) / (float)m, d1 = (e1 - m1) / (float)m;
    }
    for (i = 0; i < m; i++) {
        float in = x[i] * gin + shim * shout, a, b, l = 0.f, r = 0.f;
        bw += kb * (in - bw);
        in = bw;
        TK_AP(0, in, gd0);
        TK_AP(1, in, gd0);
        TK_AP(2, in, gd1);
        TK_AP(3, in, gd1);
        /* the left half */
        a = in + decay * z1;
        if (moving) {
            m0 += d0;
            TK_APM(4, a, g1, m0);
        } else {
            TK_AP(4, a, g1);
        }
        TK_WR(q + k->off[5], a);
        b = TK_RD(q + k->off[5] + k->len[5]);
        dp0 += kd * (b - dp0);
        b = decay * dp0;
        TK_AP(6, b, g2);
        TK_WR(q + k->off[7], b);
        z0 = TK_RD(q + k->off[7] + k->len[7]);
        /* the right half */
        a = in + decay * z0;
        if (moving) {
            m1 += d1;
            TK_APM(8, a, g1, m1);
        } else {
            TK_AP(8, a, g1);
        }
        TK_WR(q + k->off[9], a);
        b = TK_RD(q + k->off[9] + k->len[9]);
        dp1 += kd * (b - dp1);
        b = decay * dp1;
        TK_AP(10, b, g2);
        TK_WR(q + k->off[11], b);
        z1 = TK_RD(q + k->off[11] + k->len[11]);
        /* the taps */
        for (t = 0; t < 7u; t++) {
            l += TK_SG[t] * TK_RD(q + k->tap[0][t]);
            r += TK_SG[t] * TK_RD(q + k->tap[1][t]);
        }
        yl[i] = gout * l;
        yr[i] = gout * r;
        if (shim > 0.f) {                               /* CELESTIAL: the octave up, for the next sample's input */
            uint32_t c1 = k->sh_c, c2 = c1 + TK_SHW / 2u, sw = k->sh_w;
            float w1;
            if (c2 >= TK_SHW)
                c2 -= TK_SHW;
            k->shl[0] += ks * (0.5f * (l + r) - k->shl[0]);
            k->shl[1] += ks * (k->shl[0] - k->shl[1]);
            k->shl[2] += kh * (k->shl[1] - k->shl[2]);
            k->sh[sw] = fxc_cell(k->shl[1] - k->shl[2]);
            w1 = (float)c1 * (2.f / TK_SHW);
            w1 = w1 > 1.f ? 2.f - w1 : w1;              /* the heads' crossfade: each silent where it jumps */
            shout = w1 * (float)k->sh[(sw - (TK_SHW - c1)) & (SH_N - 1u)] +
                    (1.f - w1) * (float)k->sh[(sw - (TK_SHW - c2)) & (SH_N - 1u)];
            k->sh_w = (sw + 1u) & (SH_N - 1u);
            k->sh_c = c1 + 1u >= TK_SHW ? 0u : c1 + 1u;
        }
        q = (q - 1u) & mask;
    }
    k->p = q, k->m[0] = m0, k->m[1] = m1, k->z[0] = z0, k->z[1] = z1;
    k->bw = fxc_tiny(bw), k->damp[0] = fxc_tiny(dp0), k->damp[1] = fxc_tiny(dp1), k->shl[3] = fxc_tiny(shout);
    for (t = 0; t < 3u; t++)
        k->shl[t] = fxc_tiny(k->shl[t]);
}
#undef TK_RD
#undef TK_WR
#undef TK_AP
#undef TK_APM

/* ---- the entry points ------------------------------------------------------------------------------------------ */
void dn_fxc_init(dn_fx_s_t *s)
{
    uint8_t *m = (uint8_t *)s->mem;
    uint32_t t = s->type;
    if (t == DN_FX_TAPE || t == DN_FX_BBD) {
        s->u.t.line = (int16_t *)(void *)m;
    } else if (t == DN_FX_SPRING) {
        dn_fxs_t *sp = &s->u.s;
        sp->ap = (float *)(void *)m;
        m += 2u * DN_SP_M * 2u * 4u;
        sp->f.pre = (int16_t *)(void *)m;
        sp->line[0] = sp->f.pre + PRE_N;
        sp->line[1] = sp->line[0] + SP_L0;
    } else if (t == DN_FX_TOMB) {
        s->u.r.f.pre = (int16_t *)(void *)m;
        s->u.r.cell = s->u.r.f.pre + PRE_N;
    } else if (t >= DN_FX_MOD && t <= DN_FX_TUMULUS) {
        dn_fxk_t *k = &s->u.k;
        uint32_t cells = t == DN_FX_DOLMEN ? TK_RING / 2u : TK_RING;
        k->f.pre = (int16_t *)(void *)m;
        fxc_tank_init(k, t, k->f.pre + PRE_N, cells);
        if (t == DN_FX_CELEST)
            k->sh = k->ring + cells;
    }
}

void dn_fxc_run(dn_fx_s_t *s, const dn_fx_p_t *p, const int32_t *mono, int32_t *wl, int32_t *wr, uint32_t n,
                uint32_t bpm)
{
    float xh[FXC_HMAX], yl[FXC_HMAX], yr[FXC_HMAX];
    dn_fxf_t *f;
    uint32_t t = s->type, m, i;
    uint8_t ph, st;
    if (t == DN_FX_TAPE) {
        fxc_tape(&s->u.t, p, mono, wl, wr, n, bpm);
        return;
    }
    if (t == DN_FX_BBD) {
        fxc_bbd(&s->u.t, p, mono, wl, wr, n, bpm);
        return;
    }
    if (t < DN_FX_SPRING || t > DN_FX_TUMULUS || n > 2u * (FXC_HMAX - 1u))
        return;
    f = t == DN_FX_SPRING ? &s->u.s.f : t == DN_FX_TOMB ? &s->u.r.f : &s->u.k.f;
    for (i = 0, st = !f->made; i < 4u; i++)             /* a value moved: the coefficients again */
        st |= f->last[i] != p->p[i], f->last[i] = p->p[i];
    if (st) {
        f->made = 1u;
        f->c[0] = p->p[3] ? fxc_k(30.f + (float)(p->p[3] * p->p[3]) * (770.f / 16129.f), FXC_FH) : 0.f;
        if (t == DN_FX_SPRING)
            fxc_spring_coef(f, p);
        else if (t == DN_FX_TOMB)
            fxc_tomb_coef(f, p);
        else
            fxc_tank_coef(&s->u.k, t, p);
    }
    ph = f->ph;
    m = fxc_front(f, p, mono, xh, n);
    if (t == DN_FX_SPRING)
        fxc_spring(&s->u.s, xh, yl, yr, m);
    else if (t == DN_FX_TOMB)
        fxc_tomb(&s->u.r, xh, yl, yr, m);
    else
        fxc_tank(&s->u.k, t, xh, yl, yr, m);
    fxc_back(f, ph, yl, yr, wl, wr, n);
}
