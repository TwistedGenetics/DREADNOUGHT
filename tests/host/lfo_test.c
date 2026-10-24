/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT host test: the LFOs (src/dsp/dn_lfo.c) against the maths, the sequencer's clock and the 0.8 filter LFO
 * they replace; and the destinations really move (REESE's cutoff, pitch, level; a break's pitch).
 * Copyright (C) 2026 Jamie (TwistedGenetics). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dn_lfo.h"
#include "dn_seq.h"
#include "dn_dsp.h"
#include "reese.h"
#include "dn_breaks.h"

static int fails;
static void check(const char *what, int ok, double got, double want)
{
    printf("%s %s: %.3f (want %.3f)\n", ok ? "ok  " : "FAIL", what, got, want);
    fails += !ok;
}

/* the sequencer, to compare the synced LFO's cycle starts with its quarter notes */
static uint64_t now, quarters[4096];
static uint32_t nq;
static void on(uint32_t t, uint8_t n, uint8_t v, int s, uint8_t f) { (void)t, (void)n, (void)v, (void)s, (void)f; if (nq < 4096) quarters[nq++] = now; }
static void off(uint32_t t, uint8_t n) { (void)t, (void)n; }
static void clk(uint8_t st) { (void)st; }
static const dn_seq_cb_t CB = {on, off, clk, 0};

#define FS 44100u
static int32_t lr[2u * FS * 2u], lr2[2u * FS * 2u];

int main(void)
{
    dn_lfo_p_t p;
    dn_lfo_s_t s;
    uint32_t i;
    /* 1: the shapes against the maths */
    {
        double worst[5] = {0};
        dn_lfo_default(&p);
        for (i = 0; i < 1024u; i++) {
            uint32_t ph = i << 22;
            double x = i / 1024.0, w[5];
            w[0] = sin(2 * M_PI * x);
            w[1] = x < 0.5 ? -1 + 4 * x : 3 - 4 * x;
            w[2] = 2 * x - 1;
            w[3] = 1 - 2 * x;
            w[4] = x < 0.5 ? 1 : -1;
            for (uint32_t k = 0; k < 5u; k++) {
                double e;
                p.shape = (uint8_t)k;
                e = fabs(dn_lfo_at(&p, ph, 0, 0) / 32767.0 - w[k]);
                if (e > worst[k]) worst[k] = e;
            }
        }
        for (i = 0; i < 5u; i++) {
            char t[64];
            snprintf(t, sizeof t, "%s: the shape over a cycle (worst error, of 1)", DN_LFO_SHAPE_NAME[i]);
            check(t, worst[i] < 0.002, worst[i], 0);
        }
    }
    /* 2: DRAW: SMOOTH 0 holds each point; full SMOOTH slopes all the way (halfway: the two points' mean) */
    {
        dn_lfo_default(&p);
        p.shape = DN_LS_DRAW;
        p.npts = 16;
        for (i = 0; i < 32u; i++) p.pts[i] = (int8_t)(i % 2u ? -100 : 100);
        p.smooth = 0;
        check("DRAW, SMOOTH 0: point 1 all through its step", dn_lfo_at(&p, (1u << 28) - (1u << 20), 0, 0) == 32767,
              dn_lfo_at(&p, (1u << 28) - (1u << 20), 0, 0), 32767);
        p.smooth = 127;
        check("DRAW, SMOOTH 127: halfway between +100 and -100 is 0", abs(dn_lfo_at(&p, 1u << 27, 0, 0)) < 200,
              dn_lfo_at(&p, 1u << 27, 0, 0), 0);
        p.smooth = 64;
        check("DRAW, SMOOTH 64: the first half of the step held", dn_lfo_at(&p, 1u << 26, 0, 0) == 32767,
              dn_lfo_at(&p, 1u << 26, 0, 0), 32767);
    }
    /* 3: synced, a 1/4 at 172 BPM against the sequencer's quarter notes for 64 bars: every cycle starts within a
     * sample of a quarter note (the LFO run sample by sample here; the app's 32-sample ticks round to the tick) */
    {
        static dn_seq_t q;
        uint64_t t = 0;
        uint32_t k = 0, starts = 0;
        double worst = 0;
        uint32_t prev;
        dn_seq_init(&q);
        q.bpm = 172;
        q.pat.tr[0].len = 16;
        for (i = 0; i < 16u; i += 4u) q.pat.tr[0].st[i] = (dn_step_t){53, DN_ST_ON, 2, 100};
        dn_lfo_default(&p);                         /* SINE, synced, 1/4 */
        dn_lfo_reset(&s, 1);
        dn_seq_start(&q, &CB);
        now = 0;
        prev = 0;
        while (t < (uint64_t)FS * 60u * 64u * 4u / 172u) {   /* 64 bars */
            now = ++t;                              /* (as the app: the sample counted, then the events at its end) */
            dn_seq_run(&q, 1, &CB);
            dn_lfo_run(&s, &p, 1, 172);
            if (s.ph < prev) {                      /* a cycle began on this sample: the quarter note nearest */
                while (k + 1u < nq && quarters[k + 1u] <= t) k++;
                double d = fabs((double)t - (double)quarters[k]);
                if (k + 1u < nq && fabs((double)quarters[k + 1u] - (double)t) < d) d = fabs((double)quarters[k + 1u] - (double)t);
                if (d > worst) worst = d;
                starts++;
            }
            prev = s.ph;
        }
        check("synced 1/4 at 172 BPM, 64 bars: cycles", starts >= 255u && starts <= 256u, starts, 256);
        check("... each within a sample of the sequencer's quarter note (worst, samples)", worst <= 1.0, worst, 1);
    }
    /* 4: ONE-SHOT: one cycle, then it holds where the cycle ends */
    {
        dn_lfo_default(&p);
        p.shape = DN_LS_SAWUP;
        p.mode = DN_LM_ONESHOT;
        dn_lfo_reset(&s, 1);
        for (i = 0; i < 20000u; i++) dn_lfo_run(&s, &p, 1, 172);   /* past a beat (15384) */
        check("1-SHOT saw up: holds at its top after the cycle", s.done && s.out > 32000, s.out, 32767);
    }
    /* 5: a 0.8 sound's filter LFO (LRATE 40, LFLT 14) and LFO 1 (free, rate 40, sine to CUTOFF, depth 14) sound
     * the same: rendered in the app's 32-sample ticks, the difference is nothing */
    {
        reese_t a, b;
        int32_t d = 0;
        reese_init(&a);
        reese_load_preset(&a, 3);                   /* TECHSTEP */
        a.bpm = 172;
        b = a;
        b.p[RP_LFLT] = 0;
        dn_lfo_default(&p);
        p.sync = 0, p.rate = (uint8_t)a.p[RP_LRATE], p.dest = 1, p.depth = (int8_t)a.p[RP_LFLT];
        dn_lfo_reset(&s, 1);
        memset(lr, 0, sizeof lr);
        memset(lr2, 0, sizeof lr2);
        reese_note_on(&a, 41, 100, 0);
        reese_note_on(&b, 41, 100, 0);
        for (i = 0; i < FS; i += 32u) {
            reese_render(&a, lr + 2u * i, 32);
            dn_lfo_run(&s, &p, 32, 172);
            b.mod[RP_CUT] = (s.out * p.depth) >> 6;
            reese_render(&b, lr2 + 2u * i, 32);
        }
        for (i = 0; i < 2u * FS; i++) {
            int32_t e = abs(lr[i] - lr2[i]);
            if (e > d) d = e;
        }
        check("TECHSTEP's 0.8 filter LFO vs LFO 1 after the move: the biggest difference (of 32767)", d <= 2, d, 0);
    }
    /* 6: the destinations move: REESE's pitch an octave (zero crossings x2), its level down, the cutoff (brightness:
     * the first difference's energy), a break's pitch (its speed) */
    {
        reese_t r;
        uint32_t z[2] = {0, 0};
        double e[2][2] = {{0, 0}, {0, 0}};
        for (int m = 0; m < 2; m++) {
            reese_init(&r);
            reese_load_preset(&r, 1);               /* OG REESE: two sines */
            r.p[RP_DTN] = 0;
            r.mod_pitch = m ? 12 * 256 : 0;
            memset(lr, 0, sizeof lr);
            reese_note_on(&r, 45, 100, 0);
            reese_render(&r, lr, FS);
            for (i = FS / 2u; i < FS - 1u; i++)
                z[m] += (lr[2u * i] < 0) != (lr[2u * i + 2u] < 0);
        }
        check("PITCH +12 semitones: zero crossings x2", fabs((double)z[1] / z[0] - 2.0) < 0.05, (double)z[1] / z[0], 2);
        for (int m = 0; m < 2; m++) {
            reese_init(&r);
            reese_load_preset(&r, 0);
            r.p[RP_CUT] = 40;
            r.mod[RP_CUT] = m ? 60 * 256 : 0;
            r.mod[RP_LEVEL] = m ? -40 * 256 : 0;
            memset(lr, 0, sizeof lr);
            reese_note_on(&r, 41, 100, 0);
            reese_render(&r, lr, FS);
            for (i = FS / 2u; i < FS - 1u; i++) {
                double x = lr[2u * i], dx = lr[2u * i + 2u] - lr[2u * i];
                e[m][0] += x * x, e[m][1] += dx * dx;
            }
        }
        check("CUTOFF +60: brighter (difference energy / energy, x)", (e[1][1] / e[1][0]) / (e[0][1] / e[0][0]) > 2.0,
              (e[1][1] / e[1][0]) / (e[0][1] / e[0][0]), 2);
        {
            static dn_brk_t bk;
            uint32_t i0, i1;
            dn_brk_init(&bk);
            bk.loop.n = 88200, bk.loop.bars = 2, bk.loop.rate = 22050;
            dn_brk_tempo(&bk, 172);
            i0 = bk.inc;
            bk.mpitch = 12 * 256;
            dn_brk_tempo(&bk, 172);
            i1 = bk.inc;
            check("a break's PITCH +12: twice the speed", fabs((double)i1 / i0 - 2.0) < 0.002, (double)i1 / i0, 2);
        }
    }
    printf(fails ? "LFO: %d FAILED\n" : "LFO: shapes, DRAW, sync, 1-SHOT, the 0.8 LFO, destinations all ok\n", fails);
    return fails ? 1 : 0;
}
