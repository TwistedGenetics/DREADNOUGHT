/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT host test (0.25): FM against Dexed. The tables bit for bit as MSFA makes them; 400 voices made at random
 * over all 32 algorithms (every value in its range: rates, levels, scaling, curves, velocity, detune, fixed
 * frequencies, feedback, the LFO and its delay, the pitch envelope) played as notes and chords through DREADNOUGHT's
 * port and through Dexed's own MSFA (tests/host/ref), sample for sample; packed voices round trip; the pitch, an
 * envelope's timing, velocity; voices freed after their release; the factory voices (golden: peak, RMS, length);
 * the CPU it takes on this computer against REESE.
 * Copyright (C) 2026 Jamie (TwistedGenetics). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "dn_fm.h"
#include "dn_tables.h"
#include "reese.h"

void ref_fm_init(void);
const int32_t *ref_fm_sintab(void);
const int32_t *ref_fm_exp2tab(void);
const int32_t *ref_fm_freqlut(void);
void ref_fm_play(const uint8_t *patch, int nnotes, const int *notes, const int *vels, const int *on, const int *off,
                 int n, int16_t *out);

static int fails;
static void check(const char *what, int ok, double got, double want)
{
    printf("%s %s: %.3f (want %.3f)\n", ok ? "ok  " : "FAIL", what, got, want);
    fails += !ok;
}

#define FS 44100
#define NB 360                                      /* blocks a render: 0.52 s */
static dn_fm_t S;
static int16_t A[NB * 64], B[NB * 64];
static int32_t lr[2 * NB * 64];

/* DREADNOUGHT's FM, played as the reference plays: events at block starts, LEVEL 128 (unity: Dexed's own sum) */
static void ours(const uint8_t *patch, int nn, const int *notes, const int *vels, const int *on, const int *off,
                 int n, int16_t *out, int voices)
{
    int b, i;
    dn_fm_init(&S);
    S.level = 128;
    S.voices = (uint8_t)voices;
    dn_fm_voice_load(&S, patch);
    memset(lr, 0, sizeof lr);
    for (b = 0; b < n / 64; b++) {
        for (i = 0; i < nn; i++) {
            if (off[i] == b)
                dn_fm_note_off(&S, (uint32_t)notes[i]);
            if (on[i] == b)
                dn_fm_note_on(&S, (uint32_t)notes[i], (uint32_t)vels[i]);
        }
        dn_fm_render(&S, lr + 2 * 64 * b, 64);
    }
    for (i = 0; i < n; i++)
        out[i] = (int16_t)lr[2 * i];
}

static uint32_t rs = 12345;
static uint32_t rnd(uint32_t n) { rs = rs * 1103515245u + 12345u; return (rs >> 8) % n; }
/* a voice at random: every value anywhere in its range, but most of them playable (carriers up, attack not stuck) */
static void random_patch(uint8_t *p, uint32_t alg)
{
    uint32_t op, k;
    dn_fm_patch_init(p);
    for (k = 0; k < 155u; k++)
        if (k < DN_FMV_NAME)
            p[k] = (uint8_t)rnd((uint32_t)dn_fm_vmax(k) + 1u);
    p[DN_FMV_ALG] = (uint8_t)alg;
    p[DN_FMV_TRNSP] = (uint8_t)(18u + rnd(13));
    for (op = 0; op < 6u; op++) {
        uint8_t *o = p + 21 * op;
        if (dn_fm_is_carrier(alg, op))
            o[DN_FMO_OL] = (uint8_t)(70u + rnd(30));
        o[DN_FMO_R1] = (uint8_t)(40u + rnd(60));
        o[DN_FMO_L1] = (uint8_t)(60u + rnd(40));
        if (rnd(4) == 0u)
            o[DN_FMO_MODE] = 1;
        else
            o[DN_FMO_MODE] = 0;
        if (o[DN_FMO_MODE])                         /* (fixed: a few hundred Hz, not 9 kHz) */
            o[DN_FMO_COARSE] = (uint8_t)rnd(3);
    }
    p[DN_FMV_OPSW] = rnd(8) == 0u ? (uint8_t)(0x3fu & ~(1u << rnd(6))) : 0x3fu;
}
static double snr(const int16_t *a, const int16_t *b, int n, int *maxd)
{
    double s = 0, e = 0;
    int i;
    *maxd = 0;
    for (i = 0; i < n; i++) {
        int d = a[i] - b[i];
        s += (double)b[i] * b[i];
        e += (double)d * d;
        if (abs(d) > *maxd)
            *maxd = abs(d);
    }
    return e == 0 ? 999 : s == 0 ? -999 : 10 * log10(s / e);
}
static double rms(const int16_t *x, int n)
{
    double s = 0;
    int i;
    for (i = 0; i < n; i++)
        s += (double)x[i] * x[i];
    return sqrt(s / (n ? n : 1));
}
static double zc_hz(const int16_t *x, int from, int n)   /* rising zero crossings, interpolated */
{
    double t0 = -1, t1 = 0;
    int i, c = 0;
    for (i = from + 1; i < from + n; i++)
        if (x[i - 1] < 0 && x[i] >= 0) {
            double t = i - 1 + (double)-x[i - 1] / (x[i] - x[i - 1]);
            if (t0 < 0)
                t0 = t;
            t1 = t;
            c++;
        }
    return c > 1 ? (c - 1) * FS / (t1 - t0) : 0;
}

int main(int argc, char **argv)
{
    static uint8_t p[DN_FM_PATCH], q[DN_FM_PATCH], pk[DN_FM_PACKED], pk2[DN_FM_PACKED];
    int i, k, n = NB * 64, exact = 0, worst_i = -1, worst_md = 0;
    double worst = 999, hz, env_dev = 0;
    FILE *g = argc > 2 ? fopen(argv[2], "r") : 0, *gw = 0;
    ref_fm_init();
    /* ---- the tables ---- */
    check("FM tables: the sine as MSFA makes it", !memcmp(DN_FM_SIN, ref_fm_sintab(), sizeof DN_FM_SIN), 0, 0);
    check("FM tables: 2^x as MSFA makes it", !memcmp(DN_FM_EXP2, ref_fm_exp2tab(), sizeof DN_FM_EXP2), 0, 0);
    check("FM tables: the frequencies as MSFA makes them", !memcmp(DN_FM_FREQ, ref_fm_freqlut(), sizeof DN_FM_FREQ), 0, 0);
    /* ---- 400 random voices, all algorithms: a note, then a chord of 3, the same in both ---- */
    for (i = 0; i < 400; i++) {
        int notes[4], vels[4], on[4], off[4], nn, md;
        double sn;
        random_patch(p, (uint32_t)i % 32u);
        if (i % 2 == 0) {
            nn = 1;
            notes[0] = 36 + (int)rnd(48), vels[0] = 1 + (int)rnd(127), on[0] = 1, off[0] = 200;
        } else {
            nn = 3;
            for (k = 0; k < 3; k++)
                notes[k] = 40 + (int)rnd(40), vels[k] = 30 + (int)rnd(98), on[k] = 2 + 20 * k, off[k] = 150 + 30 * k;
        }
        ref_fm_play(p, nn, notes, vels, on, off, n, A);
        ours(p, nn, notes, vels, on, off, n, B, 6);
        sn = snr(B, A, n, &md);
        {   /* the loudness over time, 1024-sample windows above -60 dB: within a fraction of a dB */
            int w;
            for (w = 0; w + 1024 <= n; w += 1024) {
                double ra = rms(A + w, 1024), rb = rms(B + w, 1024);
                if (ra > 33 && fabs(20 * log10((rb + 1e-9) / ra)) > env_dev)
                    env_dev = fabs(20 * log10((rb + 1e-9) / ra));
            }
        }
        if (sn >= 999)
            exact++;
        if (sn < worst)
            worst = sn, worst_i = i, worst_md = md;
    }
    printf("     FM vs Dexed: %d of 400 sample for sample; the worst (voice %d) %.1f dB, max diff %d\n", exact, worst_i,
           worst, worst_md);
    check("FM vs Dexed: 400 random voices, most identical to the sample", exact >= 320, exact, 320);
    check("FM vs Dexed: every one's loudness over time within 0.5 dB of Dexed's (the worst, dB)", env_dev < 0.5, env_dev, 0.5);
    /* without amp mod (AMS: Dexed's level change there is a double exp(); here a float one, a few LSB apart, which
     * FB 7's chaos then grows), every voice exact: detune and all */
    exact = 0;
    for (i = 0; i < 64; i++) {
        int notes[1] = {48 + (int)rnd(30)}, vels[1] = {100}, on[1] = {1}, off[1] = {250}, md;
        uint32_t op;
        random_patch(p, (uint32_t)i % 32u);
        for (op = 0; op < 6u; op++)
            p[21 * op + DN_FMO_AMS] = 0;
        ref_fm_play(p, 1, notes, vels, on, off, n, A);
        ours(p, 1, notes, vels, on, off, n, B, 6);
        exact += snr(B, A, n, &md) >= 999;
    }
    check("FM vs Dexed: without amp mod, every voice exact (64)", exact == 64, exact, 64);
    /* ---- the packed voice: unpack -> pack round trips every field ---- */
    for (k = 0, i = 0; i < 200; i++) {
        uint32_t j;
        random_patch(p, (uint32_t)i % 32u);
        for (j = 0; j < 10; j++)
            p[DN_FMV_NAME + j] = (uint8_t)(32 + rnd(95));
        dn_fm_pack(p, pk);
        dn_fm_unpack(pk, q);
        dn_fm_pack(q, pk2);
        k += !memcmp(p, q, 155) && !memcmp(pk, pk2, sizeof pk);
    }
    check("DX7 voice: packed and unpacked, every value comes back (200 voices)", k == 200, k, 200);
    {   /* a field by hand: OP1's coarse 5 + fixed in byte 15 of the last op block, ALG 22, feedback 6 + key sync */
        uint8_t b[128] = {0};
        b[5 * 17 + 15] = (uint8_t)(5 << 1 | 1);
        b[110] = 21, b[111] = 6 | 8, b[116] = 1 | 4 << 1 | 5 << 4;
        dn_fm_unpack(b, q);
        check("DX7 voice: OP1 COARSE 5 FIXED, ALGO 22, FB 6, KEY SYNC, LFO SYNC / SINE / PMS 5 read from the bytes",
              q[105 + DN_FMO_COARSE] == 5 && q[105 + DN_FMO_MODE] == 1 && q[DN_FMV_ALG] == 21 && q[DN_FMV_FB] == 6 &&
              q[DN_FMV_OKS] == 1 && q[DN_FMV_LFKS] == 1 && q[DN_FMV_LFW] == 4 && q[DN_FMV_LPMS] == 5, 1, 1);
        b[110] = 0x7f;                              /* (out of range in a bank: brought in) */
        b[0] = 0x7f;
        dn_fm_unpack(b, q);
        check("DX7 voice: values out of range brought in (ALGO 127 -> 32, R1 127 -> 99)", q[DN_FMV_ALG] == 31 && q[0] == 99,
              q[DN_FMV_ALG] + 1, 32);
    }
    /* ---- music: INIT VOICE's pitch, velocity, an envelope's time, release ---- */
    {
        int notes[1] = {69}, vels[1] = {100}, on[1] = {0}, off[1] = {300};
        dn_fm_patch_init(p);
        ours(p, 1, notes, vels, on, off, n, B, 6);
        hz = zc_hz(B, 4096, 8192);
        check("INIT VOICE: A3 (note 69) at 440 Hz", fabs(hz - 440) < 0.5, hz, 440);
        notes[0] = 81;
        ours(p, 1, notes, vels, on, off, n, B, 6);
        hz = zc_hz(B, 4096, 8192);
        check("INIT VOICE: an octave up, 880 Hz", fabs(hz - 880) < 1, hz, 880);
        p[105 + DN_FMO_COARSE] = 2;                 /* OP1 at ratio 2 */
        ours(p, 1, notes, vels, on, off, n, B, 6);
        hz = zc_hz(B, 4096, 8192);
        check("INIT VOICE, OP1 COARSE 2: 1760 Hz", fabs(hz - 1760) < 2, hz, 1760);
        p[105 + DN_FMO_COARSE] = 1;
        p[105 + DN_FMO_KVS] = 7;                    /* velocity sensitivity: soft is quieter */
        vels[0] = 127;
        ours(p, 1, notes, vels, on, off, n, B, 6);
        hz = rms(B + 4096, 4096);
        vels[0] = 40;
        ours(p, 1, notes, vels, on, off, n, B, 6);
        check("VEL 7: velocity 40 well below 127 (dB)", 20 * log10(rms(B + 4096, 4096) / hz) < -12,
              20 * log10(rms(B + 4096, 4096) / hz), -12);
        p[105 + DN_FMO_KVS] = 0;
        p[105 + DN_FMO_R1] = 50;                    /* an attack: R1 50 takes a while to rise */
        vels[0] = 100;
        ours(p, 1, notes, vels, on, off, n, B, 6);
        {
            double full = rms(B + 15000, 2000);
            int t = 0;
            for (i = 0; i < 15000; i += 64)
                if (rms(B + i, 64) > 0.5 * full) { t = i; break; }
            ref_fm_play(p, 1, notes, vels, on, off, n, A);
            for (i = 0, k = 0; i < 15000; i += 64)
                if (rms(A + i, 64) > 0.5 * full) { k = i; break; }
            check("R1 50: the attack reaches -6 dB when Dexed's does (ms)", t == k && t > 441, t * 1000.0 / FS,
                  k * 1000.0 / FS);
        }
        p[105 + DN_FMO_R1] = 99;
        off[0] = 40;                                /* release: R4 99 -> the voice freed soon after */
        ours(p, 1, notes, vels, on, off, n, B, 6);
        check("released, R4 99: the voice is freed", dn_fm_sounding(&S) == 0, dn_fm_sounding(&S), 0);
        check("released: silent at the end", rms(B + n - 4096, 4096) < 1, rms(B + n - 4096, 4096), 0);
    }
    /* ---- voices: 7 notes on 6 voices, mono legato ---- */
    {
        int notes[7], vels[7], on[7], off[7];
        for (k = 0; k < 7; k++)
            notes[k] = 50 + 3 * k, vels[k] = 100, on[k] = 1 + k, off[k] = 999;
        dn_fm_patch_init(p);
        ours(p, 7, notes, vels, on, off, n, B, 6);
        check("7 notes held on 6 voices: 6 sounding", dn_fm_sounding(&S) == 6, dn_fm_sounding(&S), 6);
        ours(p, 7, notes, vels, on, off, n, B, 1);
        check("VOICES 1 (mono): one voice", dn_fm_sounding(&S) == 1, dn_fm_sounding(&S), 1);
        hz = zc_hz(B, 8000, 8192);
        check("mono: the last note sounds (note 68, 415.3 Hz)", fabs(hz - 415.3) < 1, hz, 415.3);
    }
    /* ---- the factory voices: each sounds, doesn't clip, is freed after; golden peak / RMS / length ---- */
    if (argc > 1) {
        char path[512];
        snprintf(path, sizeof path, "%s/fm-factory.txt", argv[1]);
        gw = fopen(path, "w");
    }
    for (i = 0; i < (int)dn_fm_factory_count(); i++) {
        int notes[2] = {36, 48}, vels[2] = {110, 100}, on[2] = {1, 120}, off[2] = {100, 200}, pk_ = 0, last = 0;
        char line[160], want[160];
        dn_fm_factory((uint32_t)i, p);
        ours(p, 2, notes, vels, on, off, n, B, 6);
        S.level = 100;
        for (k = 0; k < n; k++) {
            if (abs(B[k]) > pk_)
                pk_ = abs(B[k]);
            if (abs(B[k]) > 30)
                last = k;
        }
        snprintf(line, sizeof line, "%-10s peak %5d rms %7.1f last %6d", dn_fm_factory_name((uint32_t)i), pk_,
                 rms(B, n), last);
        printf("     %s\n", line);
        if (gw)
            fprintf(gw, "%s\n", line);
        check("factory voice sounds and stays under full scale", pk_ > 3000 && pk_ < 32767, pk_, 3000);
        if (g && fgets(want, sizeof want, g)) {
            want[strcspn(want, "\n")] = 0;
            if (strcmp(want, line))
                printf("FAIL golden: %s\n          was %s\n", line, want), fails++;
        }
    }
    if (gw)
        fclose(gw);
    if (g)
        fclose(g);
    /* ---- CPU on this computer: 6 voices of the busiest factory voice against REESE ---- */
    {
        static reese_t R;
        static int32_t buf[2 * 4096];
        clock_t t0;
        double fm_t, rs_t;
        dn_fm_factory(7, p);                        /* (GROWL BASS: all six operators, held) */
        dn_fm_init(&S);
        dn_fm_voice_load(&S, p);
        for (k = 0; k < 6; k++)
            dn_fm_note_on(&S, (uint32_t)(48 + 5 * k), 100);
        t0 = clock();
        for (i = 0; i < 400 * 6; i++)
            dn_fm_render(&S, buf, 682);
        fm_t = (double)(clock() - t0) / CLOCKS_PER_SEC;
        reese_init(&R);
        reese_note_on(&R, 40, 100, 0);
        t0 = clock();
        for (i = 0; i < 400 * 6; i++)
            reese_render(&R, buf, 682);
        rs_t = (double)(clock() - t0) / CLOCKS_PER_SEC;
        printf("     CPU here: 6 FM voices %.2f x REESE's (%.3f s of %.1f s of sound)\n", fm_t / rs_t, fm_t,
               400.0 * 6 * 682 / FS);
    }
    printf(fails ? "FM: %d FAILED\n" : "FM: all ok\n", fails);
    return fails ? 1 : 0;
}
