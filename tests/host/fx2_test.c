/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT host test (0.16): every effect of the FX rack. For each: its memory (and that the pairs the app allows
 * fit the arena), its cost on the host, a golden render (the sums don't drift unseen), that a tail dies to silence,
 * and that a hot chord (8 loud saws, far over full scale) comes out no harsher than the same chord 12 dB down (a
 * loop clipping inside buzzes: Carnac's "digital buzz with several voices"). Then what each is for: TAPE / BBD /
 * QUADRA echoes on the beat, MASSIVE's echo modes on the beat, its TIME fitted to the memory it's given, its modes
 * switching without a click, SPRING's chirp, CELESTIAL's shimmer rising.
 * Copyright (C) 2026 Jamie (TwistedGenetics). */
#define _DEFAULT_SOURCE
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "dn_fx.h"

static int fails;
static void check(const char *what, int ok, double got, double want)
{
    printf("%s %s: %.3f (want %.3f)\n", ok ? "ok  " : "FAIL", what, got, want);
    fails += !ok;
}

#define FS 44100u
#define ARENA (176u * 1024u)
#define NMAX (FS * 70u)
static uint32_t mem[ARENA / 4u];
static int32_t in[2u * NMAX], out[2u * NMAX];

static void run(dn_fx_s_t *s, const dn_fx_p_t *p, uint32_t from, uint32_t n, uint32_t bpm)
{
    uint32_t i;
    memset(out + 2u * from, 0, 2u * n * sizeof out[0]);
    for (i = 0; i < n; i += 32u)
        dn_fx_run(s, p, in + 2u * (from + i), out + 2u * (from + i), n - i < 32u ? n - i : 32u, bpm, 100);
}
static double peak(uint32_t a, uint32_t b)
{
    double m = 0;
    for (uint32_t i = a; i < b; i++) {
        double v = fabs((double)out[2u * i]), w = fabs((double)out[2u * i + 1u]);
        if (v > m)
            m = v;
        if (w > m)
            m = w;
    }
    return m;
}
static double rms(uint32_t a, uint32_t b)
{
    double e = 0;
    for (uint32_t i = a; i < b; i++)
        e += (double)out[2u * i] * out[2u * i] + (double)out[2u * i + 1u] * out[2u * i + 1u];
    return sqrt(e / (2.0 * (b - a) + 1e-9));
}
/* brightness: the first difference's energy against the signal's (a clipped loop's harmonics raise it) */
static double bright(uint32_t a, uint32_t b)
{
    double e = 1e-9, d = 0;
    for (uint32_t i = a + 1u; i < b; i++)
        for (uint32_t c = 0; c < 2u; c++) {
            double x = out[2u * i + c], y = x - out[2u * (i - 1u) + c];
            e += x * x, d += y * y;
        }
    return d / e;
}
/* the energy at f in a..b (left), Goertzel */
static double goertzel(uint32_t a, uint32_t b, double f)
{
    double w = 2.0 * cos(2.0 * M_PI * f / FS), s1 = 0, s2 = 0;
    for (uint32_t i = a; i < b; i++) {
        double s0 = out[2u * i] + w * s1 - s2;
        s2 = s1, s1 = s0;
    }
    return s1 * s1 + s2 * s2 - w * s1 * s2 + 1e-9;
}
static void silence(uint32_t a, uint32_t b) { memset(in + 2u * a, 0, 2u * (b - a) * sizeof in[0]); }
/* a stab: n saws (a chord, detuned) at amp each, decaying, len samples, from a */
static void stab(uint32_t a, uint32_t len, int nv, double amp)
{
    static const double F[8] = {110.0, 138.6, 164.8, 220.0, 111.3, 139.9, 165.9, 221.5};
    for (uint32_t i = 0; i < len; i++) {
        double v = 0, env = exp(-3.0 * i / len);
        for (int k = 0; k < nv; k++) {
            double ph = fmod(F[k] * i / FS, 1.0);
            v += amp * (2.0 * ph - 1.0);
        }
        int32_t x = (int32_t)(v * env);
        in[2u * (a + i)] = x;
        in[2u * (a + i) + 1u] = (int32_t)(x * 0.9);
    }
}
static void wav(const char *dir, const char *name, uint32_t n)
{
    char path[512];
    FILE *f;
    uint32_t i, b = n * 4u;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (!(f = fopen(path, "wb")))
        return;
    uint32_t h[11] = {0x46464952u, 36u + b, 0x45564157u, 0x20746D66u, 16u, 0x00020001u, FS, FS * 4u, 0x00100004u,
                      0x61746164u, b};
    fwrite(h, 4, 11, f);
    for (i = 0; i < 2u * n; i++) {
        int32_t v = (in[i] >> 2) + out[i] / 2;      /* (the dry a little under the wet, as a send would sit) */
        int16_t s = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        fwrite(&s, 2, 1, f);
    }
    fclose(f);
}
static uint32_t fnv(uint32_t a, uint32_t b)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 2u * a; i < 2u * b; i++)
        for (int k = 0; k < 4; k++)
            h = (h ^ (((uint32_t)out[i] >> (8 * k)) & 255u)) * 16777619u;
    return h;
}
static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

/* the first sample past thr (L or R) in a..b */
static int32_t onset(uint32_t a, uint32_t b, int c, double thr)
{
    for (uint32_t i = a; i < b; i++)
        if (fabs((double)out[2u * i + (uint32_t)c]) > thr)
            return (int32_t)i;
    return -1;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    const char *gold = argc > 2 ? argv[2] : "tests/host/fx_golden.txt";
    static const uint32_t TAIL_S[DN_FX_N] = {0, 15, 25, 25, 30, 30, 40, 50, 30, 50, 40, 65};
    FILE *gf = fopen(gold, "r");
    char gline[DN_FX_N][64];
    int gwrite = !gf;
    dn_fx_s_t s;
    dn_fx_p_t p;
    uint32_t t, i, worst_pair = 0;
    memset(gline, 0, sizeof gline);
    if (gf) {
        char ln[128];
        while (fgets(ln, sizeof ln, gf))
            for (t = 1; t < DN_FX_N; t++) {
                size_t l = strlen(DN_FX_NAME[t]);
                if (!strncmp(ln, DN_FX_NAME[t], l) && ln[l] == ' ')
                    snprintf(gline[t], sizeof gline[t], "%s", ln + l + 1);
            }
        fclose(gf);
    }
    if (gwrite)
        gf = fopen(gold, "w");

    /* the memory: each, and the worst pair against the arena (MASSIVE takes the rest, so it needs its least) */
    printf("FX memory (bytes):");
    for (t = 1; t < DN_FX_N; t++)
        printf(" %s %u", DN_FX_NAME[t], dn_fx_ram(t));
    printf("\n");
    for (t = 1; t < DN_FX_N; t++)
        for (i = 1; i < DN_FX_N; i++)
            if (dn_fx_ram(t) + dn_fx_ram(i) > worst_pair && !(t == DN_FX_DELAY && i == DN_FX_DELAY))
                worst_pair = dn_fx_ram(t) + dn_fx_ram(i);
    check("DELAY with any other effect fits the 176 KB arena (the biggest pair but DELAY + DELAY)",
          worst_pair <= ARENA, worst_pair, ARENA);
    check("... and MASSIVE beside DELAY gets at least its 64 KB",
          ARENA - dn_fx_ram(DN_FX_DELAY) >= dn_fx_ram(DN_FX_MASSIVE), ARENA - dn_fx_ram(DN_FX_DELAY), 65536);

    for (t = 1; t < DN_FX_N; t++) {
        char what[160], nm[64], g[64];
        uint32_t bytes = dn_fx_flex(t) ? ARENA - dn_fx_ram(DN_FX_DELAY) : dn_fx_ram(t), n, end;
        double t0, ns, b_hot, b_soft, last;
        dn_fx_default(&p, t);
        /* 1: a golden render: three stabs, 2 s */
        dn_fx_init(&s, t, mem, bytes);
        silence(0, FS * 3u);
        stab(0, FS / 6u, 3, 6000.0);
        stab(FS * 7u / 10u, FS / 6u, 3, 6000.0);
        stab(FS * 14u / 10u, FS / 6u, 3, 6000.0);
        t0 = now_s();
        run(&s, &p, 0, FS * 3u, 172);
        ns = (now_s() - t0) * 1e9 / (FS * 3.0);
        snprintf(nm, sizeof nm, "fx-%s.wav", DN_FX_NAME[t]);
        for (i = 3; nm[i]; i++)
            nm[i] = (char)(nm[i] >= 'A' && nm[i] <= 'Z' ? nm[i] + 32 : nm[i]);
        wav(dir, nm, FS * 3u);
        snprintf(g, sizeof g, "%08x\n", fnv(0, FS * 3u));
        if (gwrite) {
            fprintf(gf, "%s %s", DN_FX_NAME[t], g);
        } else {
            snprintf(what, sizeof what, "%s: the golden render (tests/host/fx_golden.txt)", DN_FX_NAME[t]);
            check(what, !strcmp(g, gline[t]), strtoul(g, 0, 16), strtoul(gline[t], 0, 16));
        }
        printf("     %s: %u bytes, %.0f ns a sample on this host, wet peak %.0f, rms %.0f\n", DN_FX_NAME[t], bytes, ns,
               peak(0, FS * 3u), rms(0, FS * 3u));
        snprintf(what, sizeof what, "%s: it makes a sound (wet rms over the stabs)", DN_FX_NAME[t]);
        check(what, rms(0, FS * 3u) > 300.0, rms(0, FS * 3u), 300);
        /* 2: the tail dies: one stab, then silence, at the defaults */
        dn_fx_init(&s, t, mem, bytes);
        n = FS * TAIL_S[t];
        silence(0, n);
        stab(0, FS / 6u, 3, 6000.0);
        run(&s, &p, 0, n, 172);
        last = peak(n - FS, n);
        snprintf(what, sizeof what, "%s: silent %u s after one stab (the last second's peak, LSB)", DN_FX_NAME[t],
                 TAIL_S[t]);
        check(what, last <= 1.0, last, 0);
        /* 3: hot: 8 saws far over full scale against the same 12 dB down: the tail no brighter (+3 dB at most) */
        dn_fx_init(&s, t, mem, bytes);
        end = FS * 4u;
        silence(0, end);
        stab(0, FS * 2u, 8, 32000.0);
        run(&s, &p, 0, end, 172);
        b_hot = bright(FS * 2u + FS / 4u, FS * 4u);
        dn_fx_init(&s, t, mem, bytes);
        silence(0, end);
        stab(0, FS * 2u, 8, 8000.0 / 4.0);
        run(&s, &p, 0, end, 172);
        b_soft = bright(FS * 2u + FS / 4u, FS * 4u);
        snprintf(what, sizeof what, "%s: a hot chord's tail no harsher than a quiet one's (brightness dB)",
                 DN_FX_NAME[t]);
        check(what, 10.0 * log10(b_hot / b_soft) < 3.0, 10.0 * log10(b_hot / b_soft), 0);
    }
    if (gwrite) {
        fclose(gf);
        printf("     (wrote %s: the golden renders, from this run)\n", gold);
    }

    /* the echoes on the beat: an impulse, the first repeat at 1/8D of 172 BPM (11537 samples) */
    {
        static const uint8_t T[3] = {DN_FX_TAPE, DN_FX_BBD, DN_FX_QUADRA};
        for (i = 0; i < 3u; i++) {
            char what[120];
            int32_t at;
            dn_fx_default(&p, T[i]);
            if (T[i] == DN_FX_QUADRA)
                p.p[0] = 5, p.p[4] = 0;             /* DLY>REV, the plate off: the echo alone */
            else
                p.p[3] = 0;                         /* (ping-pong off: one head) */
            dn_fx_init(&s, T[i], mem, dn_fx_ram(T[i]));
            silence(0, FS);
            for (uint32_t k = 0; k < 300u; k++)
                in[2u * k] = in[2u * k + 1u] = 24000;
            run(&s, &p, 0, FS, 172);
            at = onset(2000, FS, 0, 1500.0);
            snprintf(what, sizeof what, "%s: the first echo of 1/8D at 172 BPM within 40 samples of 11537",
                     DN_FX_NAME[T[i]]);
            check(what, at > 0 && abs(at - 11537) <= 40, at, 11537);
        }
    }
    /* TAPE's ping-pong: the left first (at the delay), the right a delay later */
    {
        int32_t l, r;
        dn_fx_default(&p, DN_FX_TAPE);
        dn_fx_init(&s, DN_FX_TAPE, mem, dn_fx_ram(DN_FX_TAPE));
        silence(0, FS);
        for (i = 0; i < 300u; i++)
            in[2u * i] = in[2u * i + 1u] = 24000;
        run(&s, &p, 0, FS, 172);
        l = onset(2000, FS, 0, 1500.0), r = onset(2000, FS, 1, 1500.0);
        check("TAPE ping-pong: the left's first echo at 1/8D", abs(l - 11537) <= 40, l, 11537);
        check("... the right's a 1/8D later", abs(r - 2 * 11537) <= 60, r, 2 * 11537);
    }
    /* MASSIVE: ORBIT at 1/4, DENSITY 0: the repeat exactly on the beat; TIME fitted to its memory */
    {
        float ms;
        int32_t at;
        char txt[8];
        dn_fx_default(&p, DN_FX_MASSIVE);
        p.p[0] = 6, p.p[1] = 8, p.p[4] = 0, p.p[5] = 0, p.p[6] = 64;   /* ORBIT, 1/4, no density, no mod, open */
        dn_fx_init(&s, DN_FX_MASSIVE, mem, ARENA);
        silence(0, FS);
        for (i = 0; i < 64u; i++)
            in[2u * i] = in[2u * i + 1u] = 24000;
        run(&s, &p, 0, FS, 172);
        at = onset(2000, FS, 0, 1500.0);
        check("MASSIVE ORBIT 1/4 at 172 BPM (memory to spare): the repeat on the beat (15384 samples, +-24)",
              abs(at - 15384) <= 24, at, 15384);
        dn_fx_ptext(&s, &p, 1, 172, txt);
        check("... TIME reads 1/4", !strcmp(txt, "1/4"), 0, 0);
        /* beside DELAY (80 KB): HALL at 1/2 (698 ms) doesn't fit: halved, shown in ms */
        dn_fx_default(&p, DN_FX_MASSIVE);
        p.p[1] = 11;
        dn_fx_init(&s, DN_FX_MASSIVE, mem, ARENA - dn_fx_ram(DN_FX_DELAY));
        silence(0, 64);
        run(&s, &p, 0, 64, 172);
        ms = dn_fxm_time_ms(&s, &p, 172);
        dn_fx_ptext(&s, &p, 1, 172, txt);
        printf("     MASSIVE HALL beside DELAY: the lines hold %.0f ms; 1/2 at 172 BPM plays %.0f ms (%s)\n",
               s.u.m.tmax * 1000.0 / 22050.0, ms, txt);
        check("MASSIVE HALL 1/2 beside DELAY: halved to fit (349 ms or 174 ms), still on the beat",
              fabs(ms - 348.8) < 1.0 || fabs(ms - 174.4) < 1.0, ms, 348.8);
        check("... and TIME reads it in ms", txt[strlen(txt) - 1] == 's', 0, 0);
        /* a mode change while a tail rings: no click (no step over the tail's own) */
        {
            int32_t own = 0, step = 0;
            dn_fx_default(&p, DN_FX_MASSIVE);
            dn_fx_init(&s, DN_FX_MASSIVE, mem, ARENA);
            silence(0, FS * 3u);
            stab(0, FS, 4, 5000.0);
            for (i = 0; i < FS * 3u; i += 32u) {
                if (i == FS * 3u / 2u)
                    p.p[0] = 9;                     /* HALL -> DUB */
                memset(out + 2u * i, 0, 64u * sizeof out[0]);
                dn_fx_run(&s, &p, in + 2u * i, out + 2u * i, 32, 172, 100);
            }
            for (i = FS; i < FS * 3u / 2u; i++)
                if (abs(out[2u * i] - out[2u * i - 2u]) > own)
                    own = abs(out[2u * i] - out[2u * i - 2u]);
            for (i = FS * 3u / 2u; i < FS * 3u; i++)
                if (abs(out[2u * i] - out[2u * i - 2u]) > step)
                    step = abs(out[2u * i] - out[2u * i - 2u]);
            check("MASSIVE: HALL -> DUB while it rings: no step over the tail's own (largest step)", step <= own, step,
                  own);
        }
        /* FEEDBACK 127 on ABYSS (gain over 1 a pass): it holds, bounded */
        dn_fx_default(&p, DN_FX_MASSIVE);
        p.p[0] = 4, p.p[2] = 127;
        dn_fx_init(&s, DN_FX_MASSIVE, mem, ARENA);
        silence(0, FS * 20u);
        stab(0, FS / 2u, 4, 8000.0);
        run(&s, &p, 0, FS * 20u, 172);
        check("MASSIVE ABYSS at FEEDBACK 127: still there after 20 s (it holds)", rms(FS * 19u, FS * 20u) > 200.0,
              rms(FS * 19u, FS * 20u), 200);
        check("... and bounded (peak under 4 x full scale)", peak(0, FS * 20u) < 4.0 * 32768.0, peak(0, FS * 20u),
              4.0 * 32768.0);
    }
    /* CELESTIAL rises: a 220 Hz tone in; in its tail the octaves up (880 Hz) against 220, beside MOD's (the same tank
     * without the shifter) */
    {
        double rc, rm;
        dn_fx_default(&p, DN_FX_CELEST);
        dn_fx_init(&s, DN_FX_CELEST, mem, dn_fx_ram(DN_FX_CELEST));
        silence(0, FS * 4u);
        for (i = 0; i < FS / 2u; i++)
            in[2u * i] = in[2u * i + 1u] = (int32_t)(12000.0 * sin(2.0 * M_PI * 220.0 * i / FS));
        run(&s, &p, 0, FS * 4u, 172);
        rc = goertzel(FS * 2u, FS * 4u, 880.0) / goertzel(FS * 2u, FS * 4u, 220.0);
        p.type = DN_FX_MOD;
        dn_fx_init(&s, DN_FX_MOD, mem, dn_fx_ram(DN_FX_MOD));
        run(&s, &p, 0, FS * 4u, 172);
        rm = goertzel(FS * 2u, FS * 4u, 880.0) / goertzel(FS * 2u, FS * 4u, 220.0);
        check("CELESTIAL: two octaves up in its tail, against MOD's (dB)", 10.0 * log10(rc / rm) > 10.0,
              10.0 * log10(rc / rm), 10.0);
    }
    /* the values: every effect's names and tops make sense */
    for (t = 1; t < DN_FX_N; t++) {
        dn_fx_default(&p, t);
        for (i = 0; i < DN_FX_NP; i++) {
            char txt[8], what[120];
            const char *nm = dn_fx_pname(&p, i);
            int32_t mx = dn_fx_pmax(&p, i);
            dn_fx_ptext(0, &p, i, 172, txt);
            snprintf(what, sizeof what, "%s value %u (%s): its default within 0..%d, text '%s'", DN_FX_NAME[t], i, nm,
                     mx, txt);
            if (nm[0] != '-' || p.p[i])
                check(what, p.p[i] <= mx && txt[0], p.p[i], mx);
        }
    }
    printf(fails ? "FX (0.16): %d FAILED\n" : "FX (0.16): every effect ok\n", fails);
    return fails ? 1 : 0;
}
