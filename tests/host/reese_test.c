/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT host test: the REESE engine on its own. Every preset: a held note, a legato slide, a release.
 * Checks the output is bounded, has no DC, decays to silence after the release, and writes one WAV per preset
 * (OUT/reese-NN-NAME.wav) for listening and for tests/host/reese_measure.py.
 *   cc -O2 -Ibuild/gen -Isrc/dsp tests/host/reese_test.c src/dsp/reese.c -o build/host/reese_test
 *   build/host/reese_test OUT_DIR */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "reese.h"

#define FS 44100u
static int32_t lr[2u * FS * 4u];
static reese_t R;

static void wav(const char *path, const int32_t *x, uint32_t frames)
{
    FILE *f = fopen(path, "wb");
    uint32_t i, bytes = frames * 4u, v;
    if (!f) { perror(path); exit(1); }
    fwrite("RIFF", 1, 4, f); v = 36u + bytes; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    v = 16; fwrite(&v, 4, 1, f); v = 1u | 2u << 16; fwrite(&v, 4, 1, f); v = FS; fwrite(&v, 4, 1, f);
    v = FS * 4u; fwrite(&v, 4, 1, f); v = 4u | 16u << 16; fwrite(&v, 4, 1, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    for (i = 0; i < 2u * frames; i++) {
        int32_t s = x[i] > 32767 ? 32767 : x[i] < -32768 ? -32768 : x[i];
        int16_t h = (int16_t)s;
        fwrite(&h, 2, 1, f);
    }
    fclose(f);
}

/* reese_test --one OUT.wav NOTE SECONDS [K=V ...]: preset 0 with parameter K (an RP_* index) set to V, one held note */
static int one(int argc, char **argv)
{
    uint32_t secs = (uint32_t)atoi(argv[4]), frames, k;
    reese_init(&R);
    R.bpm = 120;
    for (k = 5; k < (uint32_t)argc; k++) {
        int id, v;
        if (sscanf(argv[k], "%d=%d", &id, &v) == 2 && id >= 0 && id < RP_COUNT)
            R.p[id] = (int16_t)v;
        else if (sscanf(argv[k], "bpm=%d", &v) == 1)
            R.bpm = (uint16_t)v;
    }
    frames = FS * (secs > 4u ? 4u : secs);
    memset(lr, 0, sizeof lr);
    reese_note_on(&R, (uint8_t)atoi(argv[3]), 100, 0);
    reese_render(&R, lr, frames);
    wav(argv[2], lr, frames);
    return 0;
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "build/host";
    if (argc >= 5 && !strcmp(argv[1], "--one"))
        return one(argc, argv);
    uint32_t pi, fails = 0;
    for (pi = 0; pi < reese_preset_count(); pi++) {
        uint32_t frames = FS * 4u, i, peak = 0, tail = 0;
        int64_t sum = 0;
        char path[256], name[16];
        memset(lr, 0, sizeof lr);
        reese_init(&R);
        reese_load_preset(&R, pi);
        R.bpm = 172;
        reese_note_on(&R, 43, 100, 0);                   /* G2 -> G0/G1 with TRANS */
        reese_render(&R, lr, FS * 3u / 2u);
        reese_note_on(&R, 46, 100, 1);                   /* a legato slide up a minor third */
        reese_render(&R, lr + 2u * (FS * 3u / 2u), FS);
        reese_note_off(&R);
        reese_render(&R, lr + 2u * (FS * 5u / 2u), FS * 3u / 2u);
        for (i = 0; i < 2u * frames; i++) {
            uint32_t a = (uint32_t)(lr[i] < 0 ? -lr[i] : lr[i]);
            if (a > peak) peak = a;
            sum += lr[i];
        }
        for (i = 2u * (frames - FS / 4u); i < 2u * frames; i++)      /* the last quarter second */
            if ((uint32_t)(lr[i] < 0 ? -lr[i] : lr[i]) > tail) tail = (uint32_t)(lr[i] < 0 ? -lr[i] : lr[i]);
        {
            int32_t dc = (int32_t)(sum / (int64_t)(2u * frames));
            int ok = peak > 3000u && peak <= 32767u && dc > -200 && dc < 200 && tail < 30u;
            printf("reese %-10s peak %5u  dc %4d  tail %3u  %s\n", reese_preset_name(pi), peak, dc, tail,
                   ok ? "ok" : "FAIL");
            fails += !ok;
        }
        for (i = 0; reese_preset_name(pi)[i] && i < 15u; i++)
            name[i] = reese_preset_name(pi)[i] == ' ' ? '_' : reese_preset_name(pi)[i];
        name[i] = 0;
        snprintf(path, sizeof path, "%s/reese-%02u-%s.wav", out, pi, name);
        wav(path, lr, frames);
    }
    printf(fails ? "REESE: %u FAILED\n" : "REESE: all presets bounded, no DC, silent after release\n", fails);
    return fails ? 1 : 0;
}
