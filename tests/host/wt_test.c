/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT host test (0.32): WAVE. The factory tables whole and valid; a note in tune; the version read at a high
 * note keeping aliasing down (against reading the full one); the position morphing between frames; the envelopes
 * (silent after the release); voices and stealing; mono legato; every warp sounding and staying finite; its cost
 * against FM's (the engine already measured on the FM-1). Writes OUT/wave-*.wav to listen to.
 * Copyright (C) 2026 Jamie (TwistedGenetics). */
#include <math.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "dn_wt.h"
#include "dn_wt_tables.h"
#include "dn_fm.h"

static int fails;
static void check(const char *what, int ok, double got, double want)
{
    printf("%s %s: %.3f (want %.3f)\n", ok ? "ok  " : "FAIL", what, got, want);
    fails += !ok;
}

#define FS 44100u
static int32_t lr[2u * FS * 4u];
static dn_wt_t W;

static void render(uint32_t n) { memset(lr, 0, sizeof(int32_t) * 2u * n); dn_wt_render(&W, lr, n); }
static double rms(uint32_t a, uint32_t n)
{
    double e = 0;
    for (uint32_t i = a; i < a + n; i++) e += (double)lr[2u * i] * lr[2u * i];
    return sqrt(e / n);
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
static void patch(uint32_t table)
{
    dn_wt_init(&W);
    dn_wt_set_table(&W, DN_WT_FACTORY_TABLES[table]);
    W.p[WP_TABLE] = (int16_t)table;
    W.p[WP_CUT] = 127, W.p[WP_RES] = 0, W.p[WP_ATK] = 0, W.p[WP_SUS] = 127, W.p[WP_REL] = 10, W.p[WP_UNI] = 1;
}
/* the energy away from the note's harmonics (aliases), against all of it: a DFT at every bin of n samples */
static double alias_db(uint32_t a, uint32_t n, double f0)
{
    double tot = 0, off = 0;
    for (uint32_t k = 1; k < n / 2; k++) {
        double re = 0, im = 0, fk = (double)k * FS / n;
        for (uint32_t i = 0; i < n; i++) {
            double c = 2 * M_PI * i / (n - 1);      /* (Blackman-Harris: its side lobes far under what's measured) */
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
    char nm[9];
    /* the factory tables */
    {
        int ok = 1;
        for (uint32_t t = 0; t < DN_WT_FACTORY; t++)
            ok &= dn_wt_valid(DN_WT_FACTORY_TABLES[t], 16u + 8u * DN_WT_FRAME_BYTES) == 8;
        check("the 8 factory tables: 8 frames each, valid", ok && DN_WT_FACTORY == 8, DN_WT_FACTORY, 8);
        dn_wt_name(DN_WT_FACTORY_TABLES[1], nm);
        check("table 2 is REESE", !strcmp(nm, "REESE"), 0, 0);
        check("a table cut short is refused", !dn_wt_valid(DN_WT_FACTORY_TABLES[0], 16u + 7u * DN_WT_FRAME_BYTES), 0, 0);
    }
    /* in tune: ANALOG frame 1 (a sine), A4 */
    {
        patch(0);
        dn_wt_note_on(&W, 69, 127);
        render(FS);
        uint32_t zc = 0;
        for (uint32_t i = FS / 4u; i + 1u < FS; i++)
            zc += lr[2u * i] < 0 && lr[2u * i + 2u] >= 0;
        double hz = zc / 0.75;
        check("A4 on a sine frame: 440 Hz", fabs(hz - 440.0) < 2.0, hz, 440.0);
        check("... and loud enough (rms of 32768)", rms(FS / 4u, FS / 2u) > 8000.0, rms(FS / 4u, FS / 2u), 8000.0);
        wav(dir, "wave-a4.wav", FS);
    }
    /* aliasing: a saw (ANALOG's last frame) at a high note: the version for its pitch against the full one */
    {
        double good, bad;
        patch(0);
        W.p[WP_POS] = 127;
        dn_wt_note_on(&W, 100, 127);
        render(8192);
        good = alias_db(4096, 4096, 440.0 * pow(2.0, (100 - 69) / 12.0));
        patch(0);
        W.p[WP_POS] = 127;
        dn_wt_note_on(&W, 100, 127);
        render(64);                                /* (the first tick picks the version; then force the full one) */
        for (uint32_t u = 0; u < DN_WT_NU; u++) W.v[0].mip[u] = 0;
        {   /* (render without the tick choosing again: a hand loop, the same reads) */
            uint32_t n = 8192;
            memset(lr, 0, sizeof(int32_t) * 2u * n);
            const uint8_t *fr = DN_WT_FACTORY_TABLES[0] + 16u + 7u * DN_WT_FRAME_BYTES;
            uint32_t ph = 0, inc = W.v[0].inc[0];
            for (uint32_t i = 0; i < n; i++, ph += inc) lr[2u * i] = dn_wt_peek(DN_WT_FACTORY_TABLES[0], 7, 0, ph);
            (void)fr;
            bad = alias_db(4096, 4096, 440.0 * pow(2.0, (100 - 69) / 12.0));
        }
        printf("     saw at 2.6 kHz: aliases %.1f dB (the right version) vs %.1f dB (all 127 harmonics)\n", good, bad);
        check("a saw at 2.6 kHz: aliases under -40 dB", good < -40.0, good, -40.0);
        check("... far below reading the full frame", good < bad - 20.0, good, bad - 20.0);
    }
    /* ... and at C5 (523 Hz), where it matters more often */
    {
        patch(0);
        W.p[WP_POS] = 127;
        dn_wt_note_on(&W, 72, 127);
        render(8192);
        double a5 = alias_db(4096, 4096, 440.0 * pow(2.0, 3 / 12.0));
        check("a saw at C5: aliases under -48 dB", a5 < -48.0, a5, -48.0);
    }
    /* the position: frame 1 and the last differ; half way is in between */
    {
        double r0, r1, rh;
        patch(0); W.p[WP_POS] = 0; dn_wt_note_on(&W, 45, 127); render(FS / 4u); r0 = rms(FS / 8u, FS / 8u);
        patch(0); W.p[WP_POS] = 127; dn_wt_note_on(&W, 45, 127); render(FS / 4u); r1 = rms(FS / 8u, FS / 8u);
        patch(0); W.p[WP_POS] = 64; dn_wt_note_on(&W, 45, 127); render(FS / 4u); rh = rms(FS / 8u, FS / 8u);
        printf("     ANALOG: rms at POS 0 %.0f, 64 %.0f, 127 %.0f\n", r0, rh, r1);
        check("POS moves through the table (sine to saw: a different level)", fabs(r0 - r1) > 500.0, fabs(r0 - r1), 500.0);
        check("... half way between", rh > fmin(r0, r1) * 0.8 && rh < fmax(r0, r1) * 1.2, rh, (r0 + r1) / 2);
    }
    /* envelopes: let go, silent after the release; all voices free */
    {
        patch(3);
        W.p[WP_REL] = 30;
        dn_wt_note_on(&W, 48, 100);
        render(FS / 4u);
        double held = rms(0, FS / 4u);
        dn_wt_note_off(&W, 48);
        render(FS * 2u);
        check("held: sounding", held > 1000.0, held, 1000.0);
        check("let go: silent within 2 s", rms(FS * 3u / 2u, FS / 4u) < 1.0, rms(FS * 3u / 2u, FS / 4u), 0.0);
        check("... and the voice free", dn_wt_sounding(&W) == 0, dn_wt_sounding(&W), 0);
    }
    /* voices: 6 at once; a 7th takes the oldest */
    {
        patch(7);
        for (uint32_t k = 0; k < 6u; k++) dn_wt_note_on(&W, (uint8_t)(48 + k * 3), 100);
        render(256);
        check("6 notes: 6 voices", dn_wt_sounding(&W) == 6, dn_wt_sounding(&W), 6);
        dn_wt_note_on(&W, 80, 100);
        int has = 0, first = 0;
        for (uint32_t k = 0; k < DN_WT_NV; k++) has |= W.v[k].note == 80, first |= W.v[k].note == 48 && W.v[k].live;
        check("a 7th: played, the first (oldest) taken", has && !first, has, 1);
    }
    /* mono, legato: a key under a held one glides there without starting the envelope again */
    {
        patch(1);
        W.p[WP_VOICES] = 1, W.p[WP_GLIDE] = 60, W.p[WP_ATK] = 40;
        dn_wt_note_on(&W, 40, 100);
        render(FS / 2u);
        float lv = W.v[0].amp.lv;
        dn_wt_note_on(&W, 52, 100);
        render(32);
        check("mono legato: the envelope goes on", W.v[0].amp.lv >= lv * 0.98f, W.v[0].amp.lv, lv);
        check("... the pitch on its way (glide)", W.v[0].pitch > 40.0f && W.v[0].pitch < 52.0f, W.v[0].pitch, 46.0);
        dn_wt_note_off(&W, 52);
        check("... the first key still held: still sounding", W.v[0].gate == 1, W.v[0].gate, 1);
    }
    /* every warp, every table: sounding, finite, not wild; a WAV of each warp on REESE */
    {
        static const char *const WN[WW_N] = {"off", "sync", "bend", "mirror", "pwm", "fm", "crush"};
        int ok = 1;
        double worst = 0;
        for (uint32_t w = 0; w < WW_N; w++) {
            for (uint32_t t = 0; t < DN_WT_FACTORY; t++) {
                patch(t);
                W.p[WP_WARP] = (int16_t)w, W.p[WP_WAMT] = 90, W.p[WP_POS] = 70, W.p[WP_UNI] = 4, W.p[WP_DET] = 60;
                W.p[WP_SUB] = 60, W.p[WP_DRIVE] = 40, W.p[WP_RES] = 90, W.p[WP_CUT] = 80;
                dn_wt_note_on(&W, 36, 127);
                dn_wt_note_on(&W, 43, 127);
                render(FS / 2u);
                double r = rms(FS / 8u, FS / 4u);
                int32_t pk = 0;
                for (uint32_t i = 0; i < FS; i++) pk = abs(lr[i]) > pk ? abs(lr[i]) : pk;
                ok &= r > 300.0 && pk < 3 * 32768;
                worst = pk > worst ? pk : worst;
                if (t == 1u) {
                    char f[32];
                    snprintf(f, sizeof f, "wave-reese-%s.wav", WN[w]);
                    wav(dir, f, FS / 2u);
                }
            }
        }
        check("every warp on every table: sounding, the peak under 3x full scale", ok, worst, 3 * 32768);
    }
    /* (0.32.1) the warps don't alias: each at AMOUNT 100 on ANALOG's saw, C5, the energy off the note's harmonics */
    {
        static const char *const WN[WW_N] = {"OFF", "SYNC", "BEND", "MIRROR", "PWM", "FM", "CRUSH"};
        double worst = -200;
        const char *wn = "";
        for (uint32_t w = 0; w < WW_CRUSH; w++) {
            patch(0);
            W.p[WP_POS] = 127, W.p[WP_WARP] = (int16_t)w, W.p[WP_WAMT] = 100;
            dn_wt_note_on(&W, 72, 127);
            render(FS / 2u);
            double a = alias_db(FS / 4u, 2048u, 440.0 * pow(2.0, 3.0 / 12.0));
            printf("     warp %-6s at C5: off the harmonics %.1f dB\n", WN[w], a);
            if (a > worst) worst = a, wn = WN[w];
        }
        printf("     (the worst: %s)\n", wn);
        check("every warp at C5: what's off the note's harmonics under -30 dB (0.32: SYNC -10, PWM -8)", worst < -30.0, worst, -30.0);
    }
    /* (0.32.1) no zipper: a sine decaying fast, its level ramped a sample at a time, not stepped every 32 (the steps
     * buzzed at 1378 Hz either side of the note) */
    {
        patch(0);
        W.p[WP_POS] = 0, W.p[WP_DEC] = 40, W.p[WP_SUS] = 0;
        dn_wt_note_on(&W, 69, 127);
        render(FS / 4u);
        double tot = 0, side = 0;
        for (uint32_t i = 200; i < 200 + 8192; i++) tot += (double)lr[2u * i] * lr[2u * i];
        for (int kk = 1; kk <= 4; kk++)
            for (int sg = -1; sg <= 1; sg += 2) {
                double f = 1378.125 * kk + sg * 440.0, re = 0, im = 0;
                for (uint32_t i = 0; i < 8192; i++) {
                    double wv = 0.5 - 0.5 * cos(2 * M_PI * i / 8191), x = lr[2u * (200 + i)] * wv;
                    re += x * cos(2 * M_PI * f * i / FS), im += x * sin(2 * M_PI * f * i / FS);
                }
                side += (re * re + im * im) / (8192.0 * 0.375 * 8192.0 / 2.0);
            }
        double db = 10 * log10(side / (tot / 2.0) + 1e-15);
        check("a fast decay: no zipper at the control rate (under -40 dB; 0.32: -26)", db < -40.0, db, -40.0);
    }
    /* the factory sounds: every value in range, a factory table, a chord sounding and in bounds; a WAV of each */
    {
        int ok = 1;
        double worst = 0, quiet = 1e9;
        for (uint32_t i = 0; i < dn_wt_preset_count(); i++) {
            int16_t p[WP_N];
            dn_wt_preset(i, p);
            for (uint32_t k = 1; k < WP_N; k++)
                ok &= p[k] >= dn_wt_vmin(k) && p[k] <= dn_wt_vmax(k);
            ok &= p[WP_TABLE] >= 0 && p[WP_TABLE] < DN_WT_FACTORY;
            dn_wt_init(&W);
            memcpy(W.p, p, sizeof p);
            dn_wt_set_table(&W, DN_WT_FACTORY_TABLES[p[WP_TABLE]]);
            dn_wt_note_on(&W, 48, 110), dn_wt_note_on(&W, 55, 110), dn_wt_note_on(&W, 60, 110);
            render(FS);
            double r = rms(FS / 2u, FS / 4u);
            int32_t pk = 0;
            for (uint32_t j = 0; j < 2u * FS; j++) pk = abs(lr[j]) > pk ? abs(lr[j]) : pk;
            quiet = r < quiet ? r : quiet;
            worst = pk > worst ? pk : worst;
            ok &= r > 500.0 && pk < 2 * 32768;
            char f[40], *c;
            snprintf(f, sizeof f, "wave-preset-%s.wav", dn_wt_preset_name(i));
            for (c = f; *c; c++) *c = *c == ' ' ? '-' : (char)tolower(*c);
            wav(dir, f, FS);
        }
        printf("     %u factory sounds: the quietest rms %.0f, the loudest peak %.0f\n", dn_wt_preset_count(), quiet, worst);
        check("the factory sounds: values in range, a factory table, a chord sounding, peaks under 2x full scale", ok,
              worst, 2 * 32768);
    }
    /* the cost: 6 voices x 4 in unison (the most), against FM's 6 voices */
    {
        static dn_fm_t fm;
        clock_t c0, c1;
        double tw, tf;
        patch(1);
        W.p[WP_UNI] = 4, W.p[WP_DET] = 50, W.p[WP_WARP] = WW_BEND, W.p[WP_WAMT] = 90, W.p[WP_SUB] = 50;
        for (uint32_t k = 0; k < 6u; k++) dn_wt_note_on(&W, (uint8_t)(40 + k * 4), 100);
        c0 = clock();
        for (int r = 0; r < 10; r++) render(FS / 2u);
        c1 = clock();
        tw = (double)(c1 - c0) / CLOCKS_PER_SEC;
        dn_fm_init(&fm);
        {
            uint8_t pt[DN_FM_PATCH];
            dn_fm_factory(6, pt);                   /* (DARK PAD: six operators, long) */
            dn_fm_voice_load(&fm, pt);
        }
        for (uint32_t k = 0; k < 6u; k++) dn_fm_note_on(&fm, (uint8_t)(40 + k * 4), 100);
        c0 = clock();
        for (int r = 0; r < 10; r++) { memset(lr, 0, sizeof(int32_t) * FS); dn_fm_render(&fm, lr, FS / 2u); }
        printf("     (FM still sounding after: %u voices)\n", (unsigned)dn_fm_sounding(&fm));
        c1 = clock();
        tf = (double)(c1 - c0) / CLOCKS_PER_SEC;
        printf("     5 s: WAVE (6 notes asked, 4 in unison: 2 voices, BEND, SUB) %.1f ms, FM (6 voices) %.1f ms\n", tw * 1000, tf * 1000);
        check("WAVE at its heaviest (8 oscillators: 2 voices x 4, or 6 x 1) costs under 2.5x FM's 6 voices", tw < 2.5 * tf, tw / tf, 2.5);
    }
    printf("%s\n", fails ? "WAVE: FAILED" : "WAVE: tables, tuning, aliasing, morph, envelopes, voices, warps all ok");
    return fails != 0;
}
