/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT host test (0.12): the FX rack's DELAY against its settings (the repeat lands on the beat it's set to,
 * each repeat FEEDBACK times the last, it never wraps however hard it's pushed, a new TIME glides without a click, it
 * dies away to nothing), and the shared voice pool's stealing rules.
 * Copyright (C) 2026 Jamie (TwistedGenetics). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dn_fx.h"
#include "dn_voice.h"

static int fails;
static void check(const char *what, int ok, double got, double want)
{
    printf("%s %s: %.3f (want %.3f)\n", ok ? "ok  " : "FAIL", what, got, want);
    fails += !ok;
}

#define FS 44100u
static uint32_t mem[200000 / 4];
static int32_t in[2u * FS * 12u], out[2u * FS * 12u];

/* the delay run over n frames of in, out cleared first, in blocks of 32 (as the app) */
static void run(dn_fx_s_t *s, const dn_fx_p_t *p, uint32_t n, uint32_t bpm)
{
    uint32_t i;
    memset(out, 0, 2u * n * sizeof out[0]);
    for (i = 0; i < n; i += 32u)
        dn_fx_run(s, p, in + 2u * i, out + 2u * i, n - i < 32u ? n - i : 32u, bpm, 100);
}
/* the loudest |sample| (left + right) in frames a..b, and where */
static double peak(uint32_t a, uint32_t b, uint32_t *at)
{
    double m = 0;
    for (uint32_t i = a; i < b; i++) {
        double v = fabs((double)out[2u * i]) + fabs((double)out[2u * i + 1u]);
        if (v > m)
            m = v, *at = i;
    }
    return m;
}

int main(void)
{
    dn_fx_s_t s;
    dn_fx_p_t p;
    uint32_t at = 0, d, i;
    printf("FX: the DELAY needs %u bytes of the arena\n", dn_fx_ram(DN_FX_DELAY));
    check("DELAY's memory: 96 KB (the 176 KB arena keeps 80 KB for the other slot)", dn_fx_ram(DN_FX_DELAY) <= 100u * 1024u,
          dn_fx_ram(DN_FX_DELAY), 98304);
    /* 1: an impulse; the first repeat at 1/8D of 172 BPM (0.75 beat = 11538 samples), on the right, then the left */
    dn_fx_default(&p, DN_FX_DELAY);
    p.p[1] = 60, p.p[2] = 127;
    dn_fx_init(&s, DN_FX_DELAY, mem, dn_fx_ram(DN_FX_DELAY));
    memset(in, 0, sizeof in);
    in[0] = in[1] = 20000;
    in[2] = in[3] = 20000;
    run(&s, &p, FS * 2u, 172);
    d = dn_fx_delay_samples(&p, 172);
    check("1/8D at 172 BPM: 11537 samples (0.75 x 15383.7)", d == 11537u, d, 11537);
    {
        double a1 = peak(d - 200u, d + 200u, &at);
        uint32_t at1 = at;
        double a2 = peak(2u * d - 200u, 2u * d + 200u, &at);
        uint32_t at2 = at;
        double a3 = peak(3u * d - 200u, 3u * d + 200u, &at);
        check("the first repeat lands within 4 samples of 1/8D", abs((int)at1 - (int)d) <= 4, at1, d);
        check("the second at 2 x 1/8D (the other side)", abs((int)at2 - (int)(2u * d)) <= 4, at2, 2.0 * d);
        check("FEEDBACK 60: the third repeat is 0.6 x the second (+-0.1)", fabs(a3 / a2 - 0.6) < 0.1, a3 / a2, 0.6);
        check("... and nothing before the first", peak(16, d - 200u, &at) < 50.0, peak(16, d - 200u, &at), 0);
        (void)a1;
    }
    /* 2: it dies away: FEEDBACK 60, 10 s on, nothing left */
    run(&s, &p, FS * 10u, 172);
    check("FEEDBACK 60: silent 12 s after the impulse (peak of the last second)", peak(FS * 9u, FS * 10u, &at) < 4.0,
          peak(FS * 9u, FS * 10u, &at), 0);
    /* 3: pushed: FEEDBACK 127 on loud noise for 10 s: it howls but never wraps */
    {
        int32_t seed = 1, mx = 0;
        dn_fx_init(&s, DN_FX_DELAY, mem, dn_fx_ram(DN_FX_DELAY));
        p.p[1] = 127;
        for (i = 0; i < FS * 10u; i++) {
            seed = seed * 1103515245 + 12345;
            in[2u * i] = in[2u * i + 1u] = (seed >> 16) % 30000;
        }
        run(&s, &p, FS * 10u, 172);
        for (i = 0; i < 2u * FS * 10u; i++)
            if (abs(out[i]) > mx)
                mx = abs(out[i]);
        check("FEEDBACK 127, loud noise in for 10 s: the output stays in range (peak)", mx <= 32767, mx, 32767);
        check("... and it does build up (it's alive)", mx > 20000, mx, 20000);
    }
    /* 4: a new TIME glides: a steady sine in, the time moved from 1/8D to 1/4: no step bigger than the sine's own */
    {
        int32_t step = 0, own = 0;
        dn_fx_init(&s, DN_FX_DELAY, mem, dn_fx_ram(DN_FX_DELAY));
        p.p[0] = 7, p.p[1] = 50;
        for (i = 0; i < FS * 4u; i++)
            in[2u * i] = in[2u * i + 1u] = (int32_t)(12000.0 * sin(2.0 * M_PI * 220.0 * i / FS));
        run(&s, &p, FS, 172);
        for (i = 0; i < FS; i++)
            in[2u * i] = in[2u * i + 1u] = in[2u * (i + FS)];
        memset(out, 0, sizeof out[0] * 2u * FS * 2u);
        for (i = 0; i < FS * 2u; i += 32u) {
            if (i == FS / 2u)
                p.p[0] = 8;                         /* (mid-run) */
            dn_fx_run(&s, &p, in + 2u * (i % FS), out + 2u * i, 32, 172, 100);
        }
        for (i = FS / 4u; i < FS / 2u; i++) {      /* the repeats' own steps, sample to sample, before the change */
            int32_t dd = abs(out[2u * i] - out[2u * i - 2u]);
            if (dd > own)
                own = dd;
        }
        for (i = FS / 2u; i < FS * 2u; i++) {
            int32_t dd = abs(out[2u * i] - out[2u * i - 2u]);
            if (dd > step)
                step = dd;
        }
        check("TIME 1/8D -> 1/4 while it plays: no step over 1.5 x the steady repeats' own (largest step)",
              step <= own * 3 / 2, step, own * 3 / 2);
    }
    /* 5: the voice pool */
    {
        dn_vpool_t vp;
        int st;
        uint32_t a, b, c, e;
        dn_vp_init(&vp, 4);
        a = dn_vp_alloc(&vp, 0, 60, &st);
        b = dn_vp_alloc(&vp, 0, 62, &st);
        c = dn_vp_alloc(&vp, 1, 64, &st);
        e = dn_vp_alloc(&vp, 1, 65, &st);
        check("4 voices, 4 notes: 4 different voices, none stolen", a != b && b != c && c != e && a != e && !st, st, 0);
        dn_vp_release(&vp, 0, 62);
        i = dn_vp_alloc(&vp, 2, 67, &st);
        check("full: a new note takes the oldest RELEASED voice (track 1's 62), stolen", i == b && st, i, b);
        i = dn_vp_alloc(&vp, 2, 69, &st);
        check("full, none released: the oldest held (track 1's 60)", i == a && st, i, a);
        vp.limit[1] = 2;
        i = dn_vp_alloc(&vp, 1, 70, &st);
        check("track 2 at its limit (2): its own oldest (64), not another's", i == c && st, i, c);
        dn_vp_free(&vp, e);
        i = dn_vp_alloc(&vp, 3, 72, &st);
        check("a voice freed (its release over): it's used, nothing stolen", i == e && !st, i, e);
        check("release of a note not playing: none", dn_vp_release(&vp, 5, 1) == DN_VP_NONE, 0, 0);
    }
    printf(fails ? "FX: %d FAILED\n" : "FX: the delay and the voice pool all ok\n", fails);
    return fails ? 1 : 0;
}
