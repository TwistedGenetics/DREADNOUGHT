/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT host test: the BREAKS engine against the loop itself. A slice played at the loop's own tempo must be
 * the loop's samples (resampled 22 -> 44 kHz); reversed, the same backwards; all slices in order at another tempo,
 * the whole loop sped up; the release must reach silence; a slice cutting another must not click.
 * Writes OUT/breaks-chop.wav: the built-in ROLLER chopped into a jungle pattern at 172 BPM, to listen to.
 * Copyright (C) 2026 Jamie (TwistedGenetics). */
#include <math.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dn_breaks.h"
#define DN_BRK_TRUTH 1
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

/* IMA ADPCM, low nibble first, from predictor 0 / index 0 (X0X's sampleio.ima_encode) */
static void ima_encode(const int16_t *x, uint32_t n, uint8_t *out)
{
    static const int st[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60,
        66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
        598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
        3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
        18500, 20350, 22385, 24623, 27086, 29794, 32767};
    static const int ix[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
    int pred = 0, idx = 0;
    for (uint32_t i = 0; i < n; i++) {
        int s = st[idx], d = x[i] - pred, code = d < 0 ? 8 : 0, diff = s >> 3;
        d = abs(d);
        if (d >= s) code |= 4, d -= s, diff += s;
        if (d >= s >> 1) code |= 2, d -= s >> 1, diff += s >> 1;
        if (d >= s >> 2) code |= 1, diff += s >> 2;
        pred += (code & 8) ? -diff : diff;
        pred = pred < -32768 ? -32768 : pred > 32767 ? 32767 : pred;
        idx += ix[code & 7];
        idx = idx < 0 ? 0 : idx > 88 ? 88 : idx;
        if (i & 1) out[i >> 1] |= (uint8_t)(code << 4);
        else out[i >> 1] = (uint8_t)code;
    }
}

/* the frequency from the rising zero crossings of the left channel between frames a and b */
static double zc_freq(const int32_t *x, uint32_t a, uint32_t b)
{
    double first = -1, last = -1;
    int cnt = 0;
    for (uint32_t i = a + 1; i < b; i++)
        if (x[2 * i - 2] < 0 && x[2 * i] >= 0) {
            double t = i - 1 + (double)-x[2 * i - 2] / (x[2 * i] - x[2 * i - 2]);
            if (first < 0) first = t;
            last = t;
            cnt++;
        }
    return cnt > 1 ? (cnt - 1) * 44100.0 / (last - first) : 0;
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
    /* AUTO slices: the onset detector finds the generator's own kicks and snares (within 5 ms) on both built-in loops */
    for (uint32_t li = 0; li < DN_BRK_BUILTIN; li++) {
        dn_loop_t M = {DN_BRK_BUILTIN_LOOPS[li].adpcm, DN_BRK_BUILTIN_LOOPS[li].n, DN_BRK_BUILTIN_LOOPS[li].rate, 2, ""};
        uint32_t found = 0, h, j, tol = M.rate * 5u / 1000u, extra = 0;
        dn_brk_load(&B, &M);
        for (h = 0; h < DN_BRK_NHITS[li]; h++) {
            int ok = 0;
            for (j = 0; j < B.n_auto; j++)
                ok |= (B.auto_start[j] + tol >= DN_BRK_HITS[li][h]) && (B.auto_start[j] <= DN_BRK_HITS[li][h] + tol);
            found += (uint32_t)ok;
        }
        for (j = 0; j < B.n_auto; j++) {            /* (slices that are no kick or snare: hats, rides, ghosts) */
            int near = 0;
            for (h = 0; h < DN_BRK_NHITS[li]; h++)
                near |= B.auto_start[j] + tol >= DN_BRK_HITS[li][h] && B.auto_start[j] <= DN_BRK_HITS[li][h] + tol;
            extra += (uint32_t)!near;
        }
        printf("     %s: %u slices found, %u of %u kicks / snares, %u others\n", DN_BRK_BUILTIN_LOOPS[li].name,
               B.n_auto, found, (unsigned)DN_BRK_NHITS[li], extra);
        check("AUTO slices find >= 95 % of the kicks and snares", found * 100u >= 95u * DN_BRK_NHITS[li],
              100.0 * found / DN_BRK_NHITS[li], 95);
        B.smode = 1;
        dn_brk_slices_apply(&B);
        check("AUTO in use: the slices are the hits", B.nslices == B.n_auto && B.start[1] == B.auto_start[1], B.nslices,
              B.n_auto);
        /* a slice moved by hand plays from its new place */
        B.smode = 2;
        B.n_man = 0;
        dn_brk_slices_apply(&B);
        check("MANUAL the first time: empty, one slice the whole loop", B.nslices == 1u && B.start[0] == 0u, B.nslices, 1);
        memcpy(B.man_start, B.auto_start, sizeof B.auto_start);   /* "copy AUTO" (an explicit action now) */
        B.n_man = B.n_auto;
        dn_brk_slices_apply(&B);
        B.man_start[3] += 500;
        dn_brk_slices_apply(&B);
        dn_brk_tempo(&B, 136);
        B.ok = 1;
        dn_brk_trigger(&B, 3, 127, 0);
        check("MANUAL: slice 3 starts where it was moved to", B.p == B.man_start[3] + 2u, B.p, B.man_start[3] + 2u);
        B.smode = 0;
        dn_brk_slices_apply(&B);
        check("GRID: 16 a bar again", B.nslices == 32u && B.start[1] == M.n / 32u, B.nslices, 32);
    }
    /* ---- STRETCH modes (0.6): a test loop of sustained 330 Hz notes, one a sixteenth, 2 bars at 136 BPM ---- */
    {
        static uint8_t tl[60000];
        static int16_t pcm[120000];
        uint32_t tn = 77824, step = tn / 32, i2;
        double ph = 0;
        for (i2 = 0; i2 < tn; i2++) {                /* each note: a 2 ms attack, then steady */
            uint32_t at = i2 % step;
            double a = at < 44 ? at / 44.0 : 1.0;
            ph += 2 * 3.14159265358979 * 330.0 / 22050.0;
            pcm[i2] = (int16_t)(16000 * a * sin(ph));
        }
        ima_encode(pcm, tn, tl);
        dn_loop_t T = {tl, tn, 22050, 2, "TONE"};
        double f[3][2];
        int32_t step_max[3] = {0, 0, 0};
        uint32_t last_loud[3] = {0, 0, 0};
        for (int mode = 0; mode < 3; mode++)
            for (int ti = 0; ti < 2; ti++) {
                uint32_t bpm = ti ? 172 : 136, j;
                dn_brk_init(&B);
                dn_brk_load(&B, &T);
                B.stretch = (uint8_t)mode;
                dn_brk_tempo(&B, bpm);
                dn_brk_trigger(&B, 3, 127, 0);
                render(8000);
                f[mode][ti] = zc_freq(out, 300, 2500);
                if (ti) {
                    for (j = 400; j < 2400; j++) {
                        int32_t d = abs(out[2 * j] - out[2 * j - 2]);
                        step_max[mode] = d > step_max[mode] ? d : step_max[mode];
                    }
                    for (j = 0; j < 8000; j++)
                        if (abs(out[2 * j]) > 2000)
                            last_loud[mode] = j;
                }
            }
        printf("     pitch at 136 / 172 BPM: REPITCH %.1f / %.1f Hz, SLICE %.1f / %.1f, STRETCH %.1f / %.1f\n",
               f[0][0], f[0][1], f[1][0], f[1][1], f[2][0], f[2][1]);
        check("REPITCH: the pitch follows the tempo (172 / 136)", fabs(f[0][1] / f[0][0] - 172.0 / 136) < 0.01,
              f[0][1] / f[0][0], 172.0 / 136);
        for (int mode = 1; mode < 3; mode++)
            for (int ti = 0; ti < 2; ti++)
                check(mode == 1 ? "SLICE keeps the pitch (Hz, within 10 cents)" : "STRETCH keeps the pitch (Hz, within 10 cents)",
                      fabs(1200 * log2(f[mode][ti] / 330.0)) < 10, f[mode][ti], 330);
        {
            double want = 44100.0 * 15 / 172;       /* STRETCH: a slice lasts one sixteenth at the new tempo */
            check("STRETCH: the slice lasts a sixteenth at 172 (samples, within 3 %)",
                  fabs(last_loud[2] - want) < 0.03 * want + 400, last_loud[2], want);
            check("SLICE: the slice keeps its own length (samples)",
                  fabs(last_loud[1] - 2.0 * step) < 0.03 * 2 * step + 400, last_loud[1], 2.0 * step);
        }
        check("STRETCH: no clicks at the grain joins (largest step vs plain playback)",
              step_max[2] <= step_max[1] * 13 / 10 + 50, step_max[2], step_max[1]);
    }
    /* transients: a kick's attack through STRETCH as strong as through REPITCH (the first 5 ms) */
    {
        dn_loop_t R = {DN_BRK_BUILTIN_LOOPS[0].adpcm, DN_BRK_BUILTIN_LOOPS[0].n, DN_BRK_BUILTIN_LOOPS[0].rate, 2, ""};
        int32_t pk[2] = {0, 0};
        for (int mode = 0; mode < 2; mode++) {
            dn_brk_init(&B);
            dn_brk_load(&B, &R);
            B.stretch = mode ? 2 : 0;
            dn_brk_tempo(&B, 172);
            dn_brk_trigger(&B, 0, 127, 0);
            render(400);
            for (uint32_t j = 0; j < 220; j++)
                pk[mode] = abs(out[2 * j]) > pk[mode] ? abs(out[2 * j]) : pk[mode];
        }
        check("STRETCH keeps the kick's attack (peak in 5 ms vs REPITCH)", pk[1] * 10 >= pk[0] * 8, pk[1], pk[0]);
    }
    /* the cost on this machine, a voice a second of audio (the FM-1's own figure: the CPU % on the BRK2 page) */
    {
        double t[3];
        dn_loop_t R = {DN_BRK_BUILTIN_LOOPS[0].adpcm, DN_BRK_BUILTIN_LOOPS[0].n, DN_BRK_BUILTIN_LOOPS[0].rate, 2, ""};
        for (int mode = 0; mode < 3; mode++) {
            clock_t c0;
            dn_brk_init(&B);
            dn_brk_load(&B, &R);
            B.stretch = (uint8_t)mode;
            dn_brk_tempo(&B, 172);
            c0 = clock();
            for (int rep = 0; rep < 20; rep++)
                for (uint32_t s2 = 0; s2 < 11; s2++) {
                    dn_brk_trigger(&B, s2, 127, 0);
                    render(3846);
                }
            t[mode] = (double)(clock() - c0) / CLOCKS_PER_SEC / 20;
        }
        printf("     cost a voice, a second of audio here: REPITCH %.2f ms, SLICE %.2f ms, STRETCH %.2f ms\n",
               t[0] * 1000, t[1] * 1000, t[2] * 1000);
        check("STRETCH costs under 4x REPITCH", t[2] < 4 * t[0] + 0.0005, t[2] / (t[0] + 1e-9), 4);
    }
    printf(fails ? "BREAKS: %d FAILED\n" : "BREAKS: slices, reverse, sync, release, cuts, crush all ok\n", fails);
    return fails ? 1 : 0;
}
