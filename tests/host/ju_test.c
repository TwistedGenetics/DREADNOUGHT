/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT host test (0.36): JUNO. In tune at each RANGE; the sub an octave down; the pulse's width (PWM by hand);
 * the saw's aliasing at a high note; the filter closing, its resonance ringing on its own (no oscillator), the keys
 * tracking it; the HPF taking the bass; the envelope silent after the release, the gate VCA; six voices and the
 * oldest stolen, VOICES 1; the LFO's DELAY; the chorus: mono off, stereo on; every factory sound finite and under
 * clipping with six notes; its cost against FM's 6 voices (measured on the FM-1). Writes OUT/juno-*.wav.
 * Copyright (C) 2026 Jamie (TwistedGenetics). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "dn_ju.h"
#include "dn_fm.h"

static int fails;
static void check(const char *what, int ok, double got, double want)
{
    printf("%s %s: %.3f (want %.3f)\n", ok ? "ok  " : "FAIL", what, got, want);
    fails += !ok;
}

#define FS 44100u
static int32_t lr[2u * FS * 4u];
static dn_ju_t J;

static void render(uint32_t n) { memset(lr, 0, sizeof(int32_t) * 2u * n); dn_ju_render(&J, lr, n); }
static double rms(uint32_t a, uint32_t n, int ch)
{
    double e = 0;
    for (uint32_t i = a; i < a + n; i++) e += (double)lr[2u * i + ch] * lr[2u * i + ch];
    return sqrt(e / n);
}
static double freq(uint32_t a, uint32_t n)          /* rising zero crossings a second */
{
    uint32_t zc = 0, first = 0, last = 0;
    for (uint32_t i = a; i + 1u < a + n; i++)
        if (lr[2u * i] < 0 && lr[2u * i + 2u] >= 0) {
            if (!zc) first = i;
            last = i, zc++;
        }
    return zc > 1u ? (double)(zc - 1u) * FS / (double)(last - first) : 0.0;
}
static void wav(const char *dir, const char *name, uint32_t n)
{
    char p[512];
    FILE *f;
    snprintf(p, sizeof p, "%s/%s", dir, name);
    if (!(f = fopen(p, "wb"))) return;
    uint32_t bytes = n * 4u, v;
    fwrite("RIFF", 1, 4, f); v = 36u + bytes; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    v = 16; fwrite(&v, 4, 1, f);
    uint16_t h[4] = {1, 2}; fwrite(h, 2, 2, f); v = FS; fwrite(&v, 4, 1, f); v = FS * 4u; fwrite(&v, 4, 1, f);
    h[0] = 4, h[1] = 16; fwrite(h, 2, 2, f); fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    for (uint32_t i = 0; i < 2u * n; i++) {
        int32_t x = lr[i] > 32767 ? 32767 : lr[i] < -32768 ? -32768 : lr[i];
        int16_t s = (int16_t)x;
        fwrite(&s, 2, 1, f);
    }
    fclose(f);
}
static void open_patch(void)                        /* a plain saw, the filter open, no envelope on it */
{
    dn_ju_init(&J);
    J.p[JP_CUT] = 127, J.p[JP_FKEY] = 0, J.p[JP_SUS] = 127, J.p[JP_ATK] = 0, J.p[JP_REL] = 10, J.p[JP_VVEL] = 0;
}
static double alias_db(uint32_t a, uint32_t n, double f0)
{
    double tot = 0, off = 0;
    for (uint32_t k = 1; k < n / 2; k++) {
        double re = 0, im = 0, fk = (double)k * FS / n;
        for (uint32_t i = 0; i < n; i++) {
            double c = 2 * M_PI * i / (n - 1);
            double w = 0.35875 - 0.48829 * cos(c) + 0.14128 * cos(2 * c) - 0.01168 * cos(3 * c), x = lr[2u * (a + i)] * w;
            re += x * cos(2 * M_PI * k * i / n), im -= x * sin(2 * M_PI * k * i / n);
        }
        double p = re * re + im * im, h = fk / f0, d = fabs(h - floor(h + 0.5)) * f0;
        tot += p;
        if (d > 5.0 * FS / n)
            off += p;
    }
    return 10 * log10(off / tot + 1e-12);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    /* in tune, each RANGE */
    {
        static const double WANT[3] = {220.0, 440.0, 880.0};
        for (int r = 0; r < 3; r++) {
            open_patch();
            J.p[JP_RANGE] = (int16_t)r;
            dn_ju_note_on(&J, 69, 127);
            render(FS / 2u);
            double f = freq(FS / 8u, FS / 4u);
            char w[64];
            snprintf(w, sizeof w, "A4 at RANGE %s in tune (Hz)", r == 0 ? "16'" : r == 1 ? "8'" : "4'");
            check(w, fabs(f - WANT[r]) < WANT[r] * 0.003, f, WANT[r]);
        }
    }
    /* the sub alone: an octave under the note */
    {
        open_patch();
        J.p[JP_SAW] = 0, J.p[JP_SUB] = 127;
        dn_ju_note_on(&J, 69, 127);
        render(FS / 2u);
        double f = freq(FS / 8u, FS / 4u);
        check("the sub alone: A3 under A4 (Hz)", fabs(f - 220.0) < 1.0, f, 220.0);
    }
    /* the pulse's width: PWM by hand from 50 % to ~4 % */
    {
        for (int pwm = 0; pwm <= 127; pwm += 127) {
            open_patch();
            J.p[JP_SAW] = 0, J.p[JP_PULSE] = 1, J.p[JP_PWMSRC] = 1, J.p[JP_PWM] = (int16_t)pwm, J.p[JP_CUT] = 127;
            dn_ju_note_on(&J, 45, 127);
            render(FS / 2u);
            uint32_t hi = 0, nn = FS / 4u;
            int32_t mx = 0;
            for (uint32_t i = FS / 8u; i < FS / 8u + nn; i++) mx = lr[2u * i] > mx ? lr[2u * i] : mx;
            for (uint32_t i = FS / 8u; i < FS / 8u + nn; i++) hi += lr[2u * i] > mx / 4;
            double duty = (double)hi / nn;
            check(pwm ? "PWM 127 by hand: the pulse ~4 % high" : "PWM 0: a square, ~50 % high", pwm ? duty < 0.09 : fabs(duty - 0.5) < 0.06,
                  duty, pwm ? 0.04 : 0.5);
        }
    }
    /* the saw's aliasing at C7 (polyBLEP) */
    {
        open_patch();
        dn_ju_note_on(&J, 96, 127);
        render(FS / 2u);
        double a = alias_db(FS / 8u, 4096u, 440.0 * pow(2.0, (96 - 69) / 12.0));
        check("the saw at C7: aliases (dB under the whole)", a < -30.0, a, -30.0);
    }
    /* the filter: CUT down takes the highs; RES at the top rings with no oscillator; KEY tracks it */
    {
        double open_r, shut_r;
        open_patch();
        dn_ju_note_on(&J, 45, 127);
        render(FS / 4u);
        open_r = rms(FS / 8u, FS / 8u, 0);
        open_patch();
        J.p[JP_CUT] = 20;
        dn_ju_note_on(&J, 45, 127);
        render(FS / 4u);
        shut_r = rms(FS / 8u, FS / 8u, 0);
        check("CUT 20 against open: quieter (a saw's highs gone)", shut_r < open_r * 0.5, shut_r / open_r, 0.5);
        open_patch();
        J.p[JP_SAW] = 0, J.p[JP_RES] = 127, J.p[JP_CUT] = 70, J.p[JP_FKEY] = 127;
        dn_ju_note_on(&J, 60, 127);
        J.v[0].s[0] = 0.01f;                        /* (a nudge: a real one has the DCO's leak) */
        render(FS);
        double r1 = rms(FS / 2u, FS / 4u, 0), f1 = freq(FS / 2u, FS / 4u);
        check("RES 127, no oscillator: the filter rings on its own", r1 > 1500.0, r1, 1500.0);
        wav(dir, "juno-whistle.wav", FS);
        dn_ju_note_off(&J, 60);
        J.p[JP_SAW] = 0;
        open_patch();
        J.p[JP_SAW] = 0, J.p[JP_RES] = 127, J.p[JP_CUT] = 70, J.p[JP_FKEY] = 127;
        dn_ju_note_on(&J, 72, 127);
        J.v[0].s[0] = 0.01f;
        render(FS);
        double f2 = freq(FS / 2u, FS / 4u);
        check("KEY 127: an octave up the keys, the ringing an octave up (ratio)", fabs(f2 / f1 - 2.0) < 0.12, f2 / f1, 2.0);
    }
    /* the HPF: 3 against FLAT on a low note */
    {
        double flat, h3;
        open_patch();
        dn_ju_note_on(&J, 33, 127);
        render(FS / 2u);
        flat = rms(FS / 4u, FS / 4u, 0);
        open_patch();
        J.p[JP_HPF] = 3;
        dn_ju_note_on(&J, 33, 127);
        render(FS / 2u);
        h3 = rms(FS / 4u, FS / 4u, 0);
        check("HPF 3 on A1: the bass taken (against FLAT)", h3 < flat * 0.75, h3 / flat, 0.75);
    }
    /* the envelope: silent after the release; the gate VCA */
    {
        for (int g = 0; g < 2; g++) {
            open_patch();
            J.p[JP_VCA] = (int16_t)g, J.p[JP_REL] = 40;
            dn_ju_note_on(&J, 60, 100);
            render(FS / 4u);
            dn_ju_note_off(&J, 60);
            render(FS);
            check(g ? "VCA GATE: silent and free after the key" : "VCA ENV: silent and free after the release",
                  dn_ju_sounding(&J) == 0u && rms(FS / 2u, FS / 4u, 0) < 1.0, dn_ju_sounding(&J), 0);
        }
    }
    /* six voices, the oldest stolen; VOICES 1 */
    {
        open_patch();
        for (uint32_t k = 0; k < 7u; k++) dn_ju_note_on(&J, (uint8_t)(48 + k), 100);
        int has48 = 0, has54 = 0;
        for (uint32_t k = 0; k < DN_JU_NV; k++) has48 |= J.v[k].note == 48, has54 |= J.v[k].note == 54;
        check("seven keys: six voices, the first stolen for the seventh", dn_ju_sounding(&J) == 6u && !has48 && has54, dn_ju_sounding(&J), 6);
        open_patch();
        J.p[JP_VOICES] = 1;
        dn_ju_note_on(&J, 48, 100);
        dn_ju_note_on(&J, 52, 100);
        render(256);
        check("VOICES 1: one voice, the last key", dn_ju_sounding(&J) == 1u && J.v[0].note == 52, dn_ju_sounding(&J), 1);
    }
    /* the LFO's DELAY: still at first, then moving */
    {
        open_patch();
        J.p[JP_LDELAY] = 80, J.p[JP_LRATE] = 90;
        dn_ju_note_on(&J, 60, 100);
        render(4410);
        float early = J.lfo;
        double mx = 0;
        for (int t = 0; t < 100; t++) { render(2205); if (fabs(J.lfo) > mx) mx = fabs(J.lfo); }
        check("LFO DELAY: still at first, moving after", fabs(early) < 0.01 && mx > 0.5, mx, 0.5);
    }
    /* the chorus: OFF mono, I / II / I+II stereo */
    {
        for (int c = 0; c < JC_N; c++) {
            open_patch();
            J.p[JP_CHORUS] = (int16_t)c;
            for (uint32_t k = 0; k < 3u; k++) dn_ju_note_on(&J, (uint8_t)(48 + 4 * k), 100);
            render(FS);
            double d = 0, e = 0;
            for (uint32_t i = FS / 2u; i < FS; i++) {
                double x = lr[2u * i] - lr[2u * i + 1u];
                d += x * x, e += (double)lr[2u * i] * lr[2u * i];
            }
            double side = sqrt(d / (e + 1)) ;
            char w[64];
            snprintf(w, sizeof w, "CHORUS %s: %s (side / mid)", c == 0 ? "OFF" : c == 1 ? "I" : c == 2 ? "II" : "I+II",
                     c ? "stereo" : "mono");
            check(w, c ? side > 0.05 : side < 1e-6, side, c ? 0.05 : 0.0);
            if (c == 2) wav(dir, "juno-chorus2.wav", FS);
        }
    }
    /* every factory sound: six notes, finite, under clipping; a chord of each to listen to */
    {
        int ok = 1;
        int32_t worst = 0;
        const char *wn = "";
        for (uint32_t pi = 0; pi < dn_ju_preset_count(); pi++) {
            dn_ju_init(&J);
            dn_ju_preset(pi, J.p);
            for (uint32_t k = 0; k < 6u; k++) dn_ju_note_on(&J, (uint8_t)(48 + (k * 7u) % 24u), 127);
            render(FS);
            int32_t pk = 0;
            double e = 0;
            for (uint32_t i = 0; i < 2u * FS; i++) { int32_t a = abs(lr[i]); pk = a > pk ? a : pk; e += (double)lr[i] * lr[i]; }
            if (pk > worst) worst = pk, wn = dn_ju_preset_name(pi);
            if (e < 1e6 || pk > 32767) {
                printf("     %s: peak %d, energy %.0f\n", dn_ju_preset_name(pi), pk, e);
                ok = 0;
            }
            if (pi == 1u) wav(dir, "juno-pad.wav", FS);
            if (pi == 16u) wav(dir, "juno-stab.wav", FS);
        }
        printf("     (the loudest: %s)\n", wn);
        check("every factory sound: six notes sound, none clips (peak)", ok, worst, 32767);
    }
    /* its cost against FM's 6 voices (the FM-1 runs those): everything on, six voices, chorus */
    {
        static dn_fm_t fm;
        clock_t c0, c1;
        double tf, tj = 1e9;
        dn_fm_init(&fm);
        {
            uint8_t pt[DN_FM_PATCH];
            dn_fm_factory(6, pt);
            dn_fm_voice_load(&fm, pt);
        }
        for (uint32_t k = 0; k < 6u; k++) dn_fm_note_on(&fm, (uint8_t)(40 + k * 4), 100);
        c0 = clock();
        for (int r = 0; r < 10; r++) { memset(lr, 0, sizeof(int32_t) * FS); dn_fm_render(&fm, lr, FS / 2u); }
        c1 = clock();
        tf = (double)(c1 - c0) / CLOCKS_PER_SEC;
        open_patch();
        J.p[JP_PULSE] = 1, J.p[JP_SUB] = 100, J.p[JP_NOISE] = 30, J.p[JP_RES] = 80, J.p[JP_HPF] = 2, J.p[JP_CHORUS] = JC_I_II;
        J.p[JP_CUT] = 60, J.p[JP_FENV] = 50;
        for (uint32_t k = 0; k < 6u; k++) dn_ju_note_on(&J, (uint8_t)(40 + k * 4), 100);
        for (int rep = 0; rep < 3; rep++) {
            c0 = clock();
            for (int r = 0; r < 10; r++) render(FS / 2u);
            c1 = clock();
            if ((double)(c1 - c0) / CLOCKS_PER_SEC < tj) tj = (double)(c1 - c0) / CLOCKS_PER_SEC;
        }
        printf("     5 s: JUNO (everything on, 6 voices) %.1f ms = %.2f x FM (6 voices, %.1f ms)\n", tj * 1000, tj / tf, tf * 1000);
        check("JUNO at its heaviest under WAVE's heaviest (~3.3 x FM's 6 voices, run on the FM-1 in 0.33; a loose "
              "bound: the host's timing wanders)", tj < 2.9 * tf, tj / tf, 2.9);
    }
    printf(fails ? "JUNO: %d FAILED\n" : "JUNO: all ok\n", fails);
    return fails != 0;
}
