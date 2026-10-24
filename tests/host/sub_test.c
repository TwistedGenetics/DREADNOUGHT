/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT host test (0.13): SUB, the SH-101-style bass, against its settings: the oscillator and the sub at their
 * pitches, the filter self-oscillating in tune and tracking the keys, the clean-sub split keeping the fundamental under
 * hard drive, accent, the gate VCA, portamento AUTO, every factory sound in range, and a render of each to listen to.
 * Copyright (C) 2026 Jamie (TwistedGenetics). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dn_sub.h"

static int fails;
static void check(const char *what, int ok, double got, double want)
{
    printf("%s %s: %.3f (want %.3f)\n", ok ? "ok  " : "FAIL", what, got, want);
    fails += !ok;
}
#define FS 44100u
static int32_t lr[2u * FS * 3u];

static void zero_all(dn_sub_t *s)
{
    dn_sub_init(s);
    for (int k = 0; k < SP_COUNT; k++) s->p[k] = 0;
    s->p[SP_CUT] = 127, s->p[SP_SUS] = 127, s->p[SP_LEVEL] = 100, s->p[SP_TONE] = 127;
}
static void render(dn_sub_t *s, uint32_t n)
{
    memset(lr, 0, 2u * n * sizeof lr[0]);
    for (uint32_t i = 0; i < n; i += 32u) dn_sub_render(s, lr + 2u * i, 32);
}
/* the frequency from rising zero crossings over [a, b) frames (interpolated) */
static double freq(uint32_t a, uint32_t b)
{
    double first = -1, last = -1;
    int count = 0;
    for (uint32_t i = a + 1; i < b; i++) {
        double x0 = lr[2u * (i - 1)], x1 = lr[2u * i];
        if (x0 < 0 && x1 >= 0) {
            double t = (i - 1) + (-x0) / (x1 - x0);
            if (first < 0) first = t; else count++;
            last = t;
        }
    }
    return count ? FS * count / (last - first) : 0;
}
static double amp_at(double f, uint32_t a, uint32_t b)   /* Goertzel: the level of one frequency */
{
    double w = 2 * M_PI * f / FS, c = 2 * cos(w), s0, s1 = 0, s2 = 0;
    for (uint32_t i = a; i < b; i++) { s0 = lr[2u * i] + c * s1 - s2; s2 = s1; s1 = s0; }
    return sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / (b - a) * 2;
}
static double peak(uint32_t a, uint32_t b)
{
    double m = 0;
    for (uint32_t i = a; i < b; i++) if (fabs((double)lr[2u * i]) > m) m = fabs((double)lr[2u * i]);
    return m;
}

int main(int argc, char **argv)
{
    dn_sub_t s;
    double f;
    const char *dir = argc > 1 ? argv[1] : "build/host";
    /* 1: the saw at A2 (110 Hz); the sub an octave, two octaves down */
    zero_all(&s);
    s.p[SP_SAW] = 100;
    dn_sub_note_on(&s, 45, 100, 0, 0);
    render(&s, FS);
    f = freq(FS / 4u, FS);
    check("SAW at A2: 110 Hz", fabs(f - 110.0) < 0.3, f, 110);
    for (int m = 0; m < 2; m++) {
        zero_all(&s);
        s.p[SP_SUB] = 120, s.p[SP_SUBM] = (int16_t)m;
        dn_sub_note_on(&s, 45, 100, 0, 0);
        render(&s, FS);
        f = freq(FS / 4u, FS);
        check(m ? "SUB -2 OCT: 27.5 Hz" : "SUB -1 OCT: 55 Hz", fabs(f - (m ? 27.5 : 55.0)) < 0.3, f, m ? 27.5 : 55.0);
    }
    /* 2: the filter alone, self-oscillating (RES 127, nothing in the mixer), KEY 127: it plays the keys in tune */
    {
        double fr[3];
        int notes[3] = {48, 60, 72};
        for (int j = 0; j < 3; j++) {
            zero_all(&s);
            s.p[SP_RES] = 127, s.p[SP_KEY] = 127, s.p[SP_CUT] = 70;
            dn_sub_note_on(&s, (uint8_t)notes[j], 100, 0, 0);
            render(&s, FS * 2u);
            fr[j] = freq(FS, FS * 2u);
        }
        printf("     self-oscillation at C3 C4 C5: %.1f %.1f %.1f Hz\n", fr[0], fr[1], fr[2]);
        check("self-oscillating: it rings (C4)", fr[1] > 50, fr[1], 1);
        check("... an octave up the keys: 2x (in cents off)", fabs(1200 * log2(fr[1] / fr[0]) - 1200) < 5,
              1200 * log2(fr[1] / fr[0]) - 1200, 0);
        check("... and the next: 2x (cents off)", fabs(1200 * log2(fr[2] / fr[1]) - 1200) < 5,
              1200 * log2(fr[2] / fr[1]) - 1200, 0);
    }
    /* 3: the clean-sub split: F1 (43.7 Hz) square sub, DRIVE 127 HARD: with SPLIT the fundamental stays within 2 dB
     * of the undriven sound; without, it moves further */
    {
        double clean, split, whole;
        zero_all(&s);
        s.p[SP_SUB] = 100, s.p[SP_CUT] = 60;
        dn_sub_note_on(&s, 41, 100, 0, 0);
        render(&s, FS);
        clean = amp_at(43.65 / 2, FS / 2u, FS);
        s.p[SP_DRIVE] = 127, s.p[SP_DTYPE] = 1, s.p[SP_SPLIT] = 90;
        render(&s, FS);
        split = amp_at(43.65 / 2, FS / 2u, FS);
        s.p[SP_SPLIT] = 0;
        render(&s, FS);
        whole = amp_at(43.65 / 2, FS / 2u, FS);
        check("SPLIT under HARD drive: the sub's fundamental within 2 dB of clean (dB)", fabs(20 * log10(split / clean)) < 2.0,
              20 * log10(split / clean), 0);
        check("... without SPLIT it changes more (dB, absolute)", fabs(20 * log10(whole / clean)) > fabs(20 * log10(split / clean)),
              fabs(20 * log10(whole / clean)), 2);
    }
    /* 4: accent: louder */
    {
        double a, b;
        zero_all(&s);
        s.p[SP_SAW] = 100, s.p[SP_ACC] = 127;
        dn_sub_note_on(&s, 45, 100, 0, 0);
        render(&s, FS / 2u);
        a = peak(FS / 4u, FS / 2u);
        dn_sub_note_on(&s, 45, 100, 0, 1);
        render(&s, FS / 2u);
        b = peak(FS / 4u, FS / 2u);
        check("ACCENT 127: louder (ratio)", b / a > 1.3, b / a, 1.5);
    }
    /* 5: VCA GATE: full level from the first ms, silent soon after the key is up even with a long release */
    {
        zero_all(&s);
        s.p[SP_SAW] = 100, s.p[SP_VCA] = 1, s.p[SP_ATK] = 127, s.p[SP_REL] = 127;
        dn_sub_note_on(&s, 45, 100, 0, 0);
        render(&s, FS / 10u);
        check("GATE: loud within 10 ms despite a 10 s attack (peak)", peak(441, 2000) > 8000, peak(441, 2000), 8000);
        dn_sub_note_off(&s);
        render(&s, FS / 10u);
        check("GATE: silent 50 ms after the key is up (peak)", peak(2205, 4410) < 50, peak(2205, 4410), 0);
    }
    /* 6: portamento AUTO: a legato note glides, a fresh one jumps */
    {
        zero_all(&s);
        s.p[SP_SAW] = 100, s.p[SP_GLIDE] = 60, s.p[SP_GMODE] = 1;
        dn_sub_note_on(&s, 45, 100, 0, 0);
        render(&s, 4410);
        dn_sub_note_on(&s, 57, 100, 1, 0);
        render(&s, 64);
        check("AUTO: a legato note glides (not there yet, 1/256 semitone)", s.pitch256 < 57 * 256 - 256, s.pitch256, 57 * 256);
        dn_sub_note_off(&s);
        render(&s, FS);
        dn_sub_note_on(&s, 45, 100, 0, 0);
        render(&s, 64);
        check("AUTO: a fresh note jumps", s.pitch256 == 45 * 256, s.pitch256, 45 * 256);
    }
    /* 6a: envelope times locked to the tempo (0.13): ATK on 1/4 takes twice as long at 60 BPM as at 120, and as long as
     * a quarter at 120 does free (the table's 500 ms) */
    {
        uint32_t tt[3];
        for (int m = 0; m < 3; m++) {
            zero_all(&s);
            s.p[SP_SAW] = 100, s.p[SP_ATK] = m < 2 ? 69 : 85;   /* 69: the 9th length (1/4); free 85: ~500 ms */
            s.esync = m < 2, s.bpm = m == 1 ? 60 : 120;
            dn_sub_note_on(&s, 45, 100, 0, 0);
            for (tt[m] = 0; tt[m] < 3u * FS && s.stage == 1; tt[m] += 32u)
                dn_sub_render(&s, lr, 32);
        }
        check("ENV SYNC: ATK 1/4 at 60 BPM takes 2x as long as at 120", fabs((double)tt[1] / tt[0] - 2.0) < 0.15,
              (double)tt[1] / tt[0], 2);
        check("... and as long as ~500 ms free (ratio)", fabs((double)tt[0] / tt[2] - 1.0) < 0.1, (double)tt[0] / tt[2], 1);
    }
    /* 6b: every value an LFO can reach on SUB changes the sound (its modulation, as the app sets it) */
    {
        static const int K[] = {SP_CUT, SP_RES, SP_DRIVE, SP_LEVEL, SP_PW, SP_ENV, SP_SUB, SP_CRUSH, SP_TONE, SP_PULSE, SP_NOISE};
        static int32_t ref[2u * 22050u];
        for (unsigned j = 0; j < sizeof K / sizeof K[0]; j++) {
            double d = 0, e = 0;
            char t[80];
            for (int m = 0; m < 2; m++) {
                zero_all(&s);
                s.p[SP_SAW] = 60, s.p[SP_PULSE] = 60, s.p[SP_SUB] = 60, s.p[SP_NOISE] = 0, s.p[SP_PW] = 30;
                s.p[SP_CUT] = 70, s.p[SP_RES] = 40, s.p[SP_ENV] = 20, s.p[SP_DRIVE] = 30, s.p[SP_TONE] = 60;
                s.p[SP_SUS] = 80, s.p[SP_DEC] = 60, s.p[SP_LEVEL] = 80;
                if (m) s.mod[K[j]] = 40 * 256;
                dn_sub_note_on(&s, 45, 100, 0, 0);
                render(&s, 22050);
                if (!m) memcpy(ref, lr, sizeof ref);
            }
            for (uint32_t i = 2205; i < 22050; i++) {
                double a = ref[2u * i], b = lr[2u * i];
                d += (a - b) * (a - b), e += a * a;
            }
            snprintf(t, sizeof t, "LFO on SUB's %s: the sound changes (difference / signal energy)", dn_sub_param_name((uint32_t)K[j]));
            check(t, d / e > (K[j] == SP_NOISE || K[j] == SP_CRUSH ? 0.003 : 0.01), d / e, 1);
        }
        zero_all(&s);
        s.p[SP_SAW] = 100;
        s.mod_pitch = 12 * 256;
        dn_sub_note_on(&s, 45, 100, 0, 0);
        render(&s, FS);
        check("LFO on SUB's PITCH: +12 is an octave up (Hz)", fabs(freq(FS / 4u, FS) - 220.0) < 0.5, freq(FS / 4u, FS), 220);
    }
    /* 7: no click as a note starts: the onset's click energy (the second difference, the first 5 ms) against the held
     * sound's (5 ms at 50 ms), every factory sound without noise in it (a noise hit is meant to bite) */
    for (uint32_t k = 0; k < dn_sub_preset_count(); k++) {
        double a = 0, b = 0;
        char t[80];
        dn_sub_init(&s);
        dn_sub_load_preset(&s, k);
        if (s.p[SP_NOISE])
            continue;
        s.p[SP_ENV] = 0;                            /* (the amp's start: the filter's snap is a sound, not a click) */
        dn_sub_note_on(&s, 53, 100, 0, 0);
        render(&s, 4410);
        for (int i = 2; i < 220; i++) { double d = lr[2*i] - 2.0*lr[2*i-2] + lr[2*i-4]; a += d * d; }
        for (int i = 2205; i < 2425; i++) { double d = lr[2*i] - 2.0*lr[2*i-2] + lr[2*i-4]; b += d * d; }
        snprintf(t, sizeof t, "%s: no click as the amp starts (onset / held click energy, ENV 0)", dn_sub_preset_name(k));
        check(t, a / (b + 1) < 5.0, a / (b + 1), 1);
    }
    /* 8: every factory sound: never wraps, never silent, and a WAV of each to listen to */
    {
        char path[512];
        FILE *w;
        snprintf(path, sizeof path, "%s/sub_presets.wav", dir);
        w = fopen(path, "wb");
        for (uint32_t k = 0; k < dn_sub_preset_count(); k++) {
            double pk = 0;
            dn_sub_init(&s);
            dn_sub_load_preset(&s, k);
            for (int st = 0; st < 8; st++) {               /* a bar of eighths at 172: a jungle bass line */
                static const uint8_t L[8] = {41, 41, 44, 41, 39, 41, 46, 44};
                dn_sub_note_on(&s, (uint8_t)(L[st] + 12), 100, st % 2, st == 3);
                render(&s, 5000);
                dn_sub_note_off(&s);
                render(&s, 2692);
                pk = fmax(pk, peak(0, 2692));
                (void)pk;
                if (w)
                    for (uint32_t i = 0; i < 2692u; i++) { int16_t h = (int16_t)(lr[2u * i] > 32767 ? 32767 : lr[2u * i] < -32768 ? -32768 : lr[2u * i]); fwrite(&h, 2, 1, w); }
            }
            render(&s, 2000);
            {
                double m = 0;
                dn_sub_note_on(&s, 41, 100, 0, 0);
                render(&s, FS / 2u);
                m = peak(0, FS / 2u);
                char t[64];
                snprintf(t, sizeof t, "%s: plays, in range (peak)", dn_sub_preset_name(k));
                check(t, m > 3000 && m <= 32767, m, 20000);
            }
        }
        if (w) fclose(w);
    }
    /* 0.19: the filter envelope on its own: the amp held (SUS 127), the filter's decaying to F.SUS 0. A low cutoff opened
     * by ENV: bright at the note's start, dark later, the level held; with F.SUS 127 it stays open */
    {
        dn_sub_t s;
        double h0, h1, a0, a1;
        for (int sus = 0; sus < 2; sus++) {
            zero_all(&s);
            s.p[SP_SAW] = 100, s.p[SP_CUT] = 50, s.p[SP_ENV] = 63, s.p[SP_DEC] = 0;
            s.fe[0] = 14, s.fe[1] = 85, s.fe[2] = sus ? 127 : 0, s.fe[3] = 40;
            dn_sub_note_on(&s, 48, 100, 0, 0);
            render(&s, FS);
            h0 = amp_at(130.81 * 8, 1000, 3000), h1 = amp_at(130.81 * 8, 40000, 44000);   /* the 8th harmonic */
            a0 = amp_at(130.81, 1000, 3000), a1 = amp_at(130.81, 40000, 44000);           /* the fundamental */
            if (!sus) {
                check("filter env F.SUS 0: the 8th harmonic falls >20 dB over the note", 20 * log10(h0 / h1) > 20,
                      20 * log10(h0 / h1), 20);
                check("... while the amp holds the fundamental (within 3 dB)", fabs(20 * log10(a0 / a1)) < 3,
                      20 * log10(a0 / a1), 0);
            } else {
                check("filter env F.SUS 127: the 8th harmonic stays (within 3 dB)", fabs(20 * log10(h0 / h1)) < 3,
                      20 * log10(h0 / h1), 0);
            }
        }
        dn_sub_load_preset(&s, 0);
        check("a factory sound: the filter envelope is the amp's (as the one 101 envelope)",
              s.fe[0] == s.p[SP_ATK] && s.fe[1] == s.p[SP_DEC] && s.fe[2] == s.p[SP_SUS] && s.fe[3] == s.p[SP_REL], 0, 0);
    }
    printf(fails ? "SUB: %d FAILED\n" : "SUB: pitches, the filter in tune, the split, accent, gate, glide, presets all ok\n", fails);
    return fails ? 1 : 0;
}
