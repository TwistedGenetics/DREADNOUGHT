/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT host test: the BREAKS engine against the loop itself. A slice played at the loop's own tempo must be
 * the loop's samples (resampled 22 -> 44 kHz); reversed, the same backwards; all slices in order at another tempo,
 * the whole loop sped up; the release must reach silence; a slice cutting another must not click.
 * Writes OUT/breaks-chop.wav: the built-in ROLLER chopped into a jungle pattern at 172 BPM, to listen to.
 * Copyright (C) 2026 Jamie (TwistedGenetics). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dn_breaks.h"
#include "dn_breaks_builtin.h"

static dn_brk_t B;
static int fails;
static int16_t ref[200000];
static int32_t out[2 * 400000];

static void check(const char *what, int ok, double got, double want)
{
    printf("%s %s: %.4f (want %.4f)\n", ok ? "ok  " : "FAIL", what, got, want);
    fails += !ok;
}

static void decode_all(const uint8_t *d, uint32_t n)   /* a reference decoder, written apart from the engine's */
{
    static const int st[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60,
        66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
        598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
        3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
        18500, 20350, 22385, 24623, 27086, 29794, 32767};
    static const int ix[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
    int pred = 0, idx = 0;
    for (uint32_t i = 0; i < n; i++) {
        int c = (i & 1) ? d[i >> 1] >> 4 : d[i >> 1] & 15, s = st[idx], df = s >> 3;
        if (c & 4) df += s;
        if (c & 2) df += s >> 1;
        if (c & 1) df += s >> 2;
        pred += (c & 8) ? -df : df;
        pred = pred < -32768 ? -32768 : pred > 32767 ? 32767 : pred;
        idx += ix[c & 7];
        idx = idx < 0 ? 0 : idx > 88 ? 88 : idx;
        ref[i] = (int16_t)pred;
    }
}

static double corr(const double *x, const double *y, int n)
{
    double sxy = 0, sxx = 0, syy = 0;
    for (int i = 0; i < n; i++)
        sxy += x[i] * y[i], sxx += x[i] * x[i], syy += y[i] * y[i];
    return sxy / sqrt(sxx * syy + 1e-9);
}

static void render(uint32_t n)
{
    memset(out, 0, sizeof out);
    dn_brk_render(&B, out, n);
}

static void wav(const char *path, const int32_t *x, uint32_t frames)
{
    FILE *f = fopen(path, "wb");
    uint32_t v, bytes = frames * 4;
    if (!f) { perror(path); exit(2); }
    fwrite("RIFF", 1, 4, f); v = 36 + bytes; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    v = 16; fwrite(&v, 4, 1, f); v = 1u | 2u << 16; fwrite(&v, 4, 1, f); v = 44100; fwrite(&v, 4, 1, f);
    v = 44100 * 4; fwrite(&v, 4, 1, f); v = 4u | 16u << 16; fwrite(&v, 4, 1, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    for (uint32_t i = 0; i < 2 * frames; i++) {
        int32_t s = x[i] > 32767 ? 32767 : x[i] < -32768 ? -32768 : x[i];
        int16_t h = (int16_t)s;
        fwrite(&h, 2, 1, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/host";
    static double x[100000], y[100000];
    dn_loop_t L = {DN_BRK_BUILTIN_LOOPS[0].adpcm, DN_BRK_BUILTIN_LOOPS[0].n, DN_BRK_BUILTIN_LOOPS[0].rate, 0, "ROLLER"};
    uint32_t slen, i;
    char path[512];

    L.bars = dn_brk_bars_of(L.n, L.rate);
    check("built-in loop: bars guessed", L.bars == 2, L.bars, 2);
    dn_brk_init(&B);
    dn_brk_load(&B, &L);
    check("slices (16 a bar)", B.nslices == 32, B.nslices, 32);
    dn_brk_tempo(&B, 136);
    check("at the loop's own 136 BPM: half a loop sample a sample (22 -> 44 kHz)", abs((int)B.inc - 32768) < 4,
          B.inc / 65536.0, 0.5);
    decode_all(L.adpcm, L.n);
    slen = L.n / 32;

    /* a slice at the loop's tempo = the loop's own samples */
    dn_brk_trigger(&B, 5, 127, 0);
    render(2 * slen);
    for (i = 0; i < 2 * slen; i++) {
        x[i] = out[2 * i];
        y[i] = (i & 1) ? (ref[5 * slen + i / 2] + ref[5 * slen + i / 2 + 1]) / 2.0 : ref[5 * slen + i / 2];
    }
    check("slice 5 = the loop's slice 5 (correlation)", corr(x, y, (int)(2 * slen)) > 0.995, corr(x, y, (int)(2 * slen)), 1);

    /* reversed */
    dn_brk_trigger(&B, 5, 127, 1);
    render(2 * slen - 4);
    for (i = 0; i < 2 * slen - 4; i++) {
        x[i] = out[2 * i];
        y[i] = (i & 1) ? (ref[5 * slen + slen - 1 - i / 2] + ref[5 * slen + slen - 2 - i / 2]) / 2.0
                       : ref[5 * slen + slen - 1 - i / 2];
    }
    check("slice 5 reversed = the slice backwards", corr(x + 64, y + 64, (int)(2 * slen - 68)) > 0.99,
          corr(x + 64, y + 64, (int)(2 * slen - 68)), 1);

    /* every slice in order at 172 BPM: the loop sped up to 172 */
    dn_brk_tempo(&B, 172);
    {
        uint32_t step = (uint32_t)(44100.0 * 15 / 172), total = 32 * step, s;
        static int32_t acc[2 * 400000];
        memset(acc, 0, sizeof acc);
        for (s = 0; s < 32; s++) {
            dn_brk_trigger(&B, s, 127, 0);
            memset(out, 0, 2 * step * sizeof(int32_t) + 64);
            dn_brk_render(&B, out, step);
            memcpy(acc + 2 * s * step, out, 2 * step * sizeof(int32_t));
        }
        for (i = 0; i < 90000 && i < total; i++) {
            double p = (double)i * L.n / total, f = p - floor(p);
            x[i] = acc[2 * i];
            y[i] = ref[(uint32_t)p] * (1 - f) + ref[(uint32_t)p + 1] * f;
        }
        check("32 slices in order at 172 BPM = the loop sped up (correlation)", corr(x, y, 90000) > 0.97,
              corr(x, y, 90000), 1);
    }

    /* the release: DECAY 40 (~20 ms .. 50 ms): silence soon after */
    B.decay = 40;
    dn_brk_trigger(&B, 0, 127, 0);
    render(400);
    dn_brk_release(&B);
    render(44100 / 2);
    {
        int32_t pk = 0;
        for (i = 22050 / 2; i < 22050; i++)
            pk = abs(out[2 * i]) > pk ? abs(out[2 * i]) : pk;
        check("release: silent a quarter second later (peak)", pk < 8, pk, 0);
        check("release: the voice is off", B.on == 0, B.on, 0);
    }
    B.decay = 127;

    /* a cut: slice 9 cutting into slice 2 mid-hit, no jump at the join */
    dn_brk_trigger(&B, 2, 127, 0);
    render(700);
    {
        int32_t before = out[2 * 699], jump, worst = 0;
        dn_brk_trigger(&B, 9, 127, 0);
        render(400);
        jump = abs(out[0] - before);
        for (i = 1; i < 400; i++)
            worst = abs(out[2 * i] - out[2 * i - 2]) > worst ? abs(out[2 * i] - out[2 * i - 2]) : worst;
        check("a cut: the step at the join is no bigger than the sound's own steps", jump <= worst + 200, jump, worst);
    }

    /* crush: bounded, and different */
    B.crush = 100;
    dn_brk_trigger(&B, 4, 127, 0);
    render(2000);
    {
        int32_t pk = 0;
        for (i = 0; i < 2000; i++)
            pk = abs(out[2 * i]) > pk ? abs(out[2 * i]) : pk;
        check("crush: bounded", pk < 32768 && pk > 1000, pk, 20000);
    }
    B.crush = 0;

    /* a chopped jungle pattern to listen to: 4 bars at 172 */
    {
        static const int8_t PAT[64] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                                       0, 1, 2, 3, 4, 5, 6, 7, 4, 5, 6, 7, 28, 29, 30, 31,
                                       0, 1, 2, 3, 12, 13, 12, 13, 8, 9, 10, 11, 12, 13, 14, 15,
                                       0, 1, -4, 3, 4, 5, 6, 7, 12, 12, 12, 12, 28, 28, 30, 30};
        uint32_t step = (uint32_t)(44100.0 * 15 / 172), s;
        static int32_t acc[2 * 400000];
        memset(acc, 0, sizeof acc);
        dn_brk_tempo(&B, 172);
        for (s = 0; s < 64; s++) {
            int p = PAT[s];
            dn_brk_trigger(&B, (uint32_t)(p < 0 ? -p : p), (s % 4) ? 100 : 127, p < 0);
            dn_brk_render(&B, acc + 2 * s * step, step);
        }
        snprintf(path, sizeof path, "%s/breaks-chop.wav", dir);
        wav(path, acc, 64 * step);
    }
    printf(fails ? "BREAKS: %d FAILED\n" : "BREAKS: slices, reverse, sync, release, cuts, crush all ok\n", fails);
    return fails ? 1 : 0;
}
