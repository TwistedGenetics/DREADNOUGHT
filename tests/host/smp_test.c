/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT host test (0.23): SAMPLER. The pitch across the key map (root, octaves, TUNE), the zones' edges, a pad's
 * PITCH and START / END, REVERSE, a loop going round without a click (and SUST letting go), CHOKE, the voices stolen
 * without a click, and silence after. Samples made here and IMA-encoded as the FM-1 keeps them.
 * Copyright (C) 2026 Jamie (TwistedGenetics). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dn_smp.h"

static int fails;
static void check(const char *what, int ok, double got, double want)
{
    printf("%s %s: %.3f (want %.3f)\n", ok ? "ok  " : "FAIL", what, got, want);
    fails += !ok;
}

#define FS 44100u
static int32_t lr[2u * FS * 4u];

/* ---- an IMA ADPCM encoder (the standard one, low nibble first) ---- */
static const int16_t ST[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
    118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
    6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767};
static const int8_t IX[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
static void encode(const int16_t *x, uint32_t n, uint8_t *out)
{
    int32_t pred = 0, idx = 0;
    memset(out, 0, (n + 1) / 2);
    for (uint32_t i = 0; i < n; i++) {
        int32_t step = ST[idx], d = x[i] - pred, code = 0, diff = step >> 3;
        if (d < 0) code = 8, d = -d;
        if (d >= step) code |= 4, d -= step, diff += step;
        if (d >= step >> 1) code |= 2, d -= step >> 1, diff += step >> 1;
        if (d >= step >> 2) code |= 1, diff += step >> 2;
        pred += (code & 8) ? -diff : diff;
        pred = pred > 32767 ? 32767 : pred < -32768 ? -32768 : pred;
        idx += IX[code & 7];
        idx = idx < 0 ? 0 : idx > 88 ? 88 : idx;
        out[i >> 1] |= (uint8_t)((i & 1) ? code << 4 : code);
    }
}
typedef struct { uint8_t a[300000]; uint32_t n, rate; uint32_t ck[2048]; uint32_t nck; } smp_t;
static smp_t SA, SB, SC;
static void make_sine(smp_t *s, double f, double sec, uint32_t rate, double f2)   /* f2: the second half's (0: same) */
{
    static int16_t x[600000];
    s->n = (uint32_t)(sec * rate), s->rate = rate;
    double ph = 0;
    for (uint32_t i = 0; i < s->n; i++) {
        double ff = f2 > 0 && i >= s->n / 2 ? f2 : f;
        x[i] = (int16_t)(16000.0 * sin(ph));
        ph += 2 * M_PI * ff / rate;
    }
    encode(x, s->n, s->a);
    s->nck = dn_smp_checkpoints(s->a, s->n, s->ck, 2048);
}
static void put(dn_smp_t *m, uint32_t i, const smp_t *s)
{
    m->src[i].adpcm = s->a, m->src[i].n = s->n, m->src[i].rate = s->rate, m->src[i].ck = s->ck, m->src[i].nck = s->nck;
}
static void render(dn_smp_t *m, uint32_t n)
{
    memset(lr, 0, 2u * n * sizeof lr[0]);
    for (uint32_t i = 0; i < n; i += 32u)
        dn_smp_render(m, lr + 2u * i, n - i < 32u ? n - i : 32u);
}
static double freq(uint32_t a, uint32_t b)          /* rising zero crossings, interpolated */
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
static double peak(uint32_t a, uint32_t b)
{
    double m = 0;
    for (uint32_t i = a; i < b; i++)
        if (fabs((double)lr[2u * i]) > m) m = fabs((double)lr[2u * i]);
    return m;
}
static int32_t maxstep(uint32_t a, uint32_t b)
{
    int32_t m = 0;
    for (uint32_t i = a + 1; i < b; i++)
        if (abs(lr[2u * i] - lr[2u * i - 2u]) > m) m = abs(lr[2u * i] - lr[2u * i - 2u]);
    return m;
}
static int voices(const dn_smp_t *m)
{
    int n = 0;
    for (uint32_t i = 0; i < DN_SMP_VOICES; i++) n += m->v[i].on;
    return n;
}
static double cents(double a, double b) { return 1200.0 * log2(a / b); }

int main(void)
{
    static dn_smp_t m;
    char t[120];
    double f;
    make_sine(&SA, 440.0, 2.0, 22050, 0);
    make_sine(&SB, 660.0, 2.0, 32000, 0);
    make_sine(&SC, 440.0, 1.0, 22050, 880.0);
    printf("SAMPLER: a voice is %u bytes, the sampler %u\n", (unsigned)sizeof m.v[0], (unsigned)sizeof m);

    /* 1: a pad plays at its PITCH */
    dn_smp_init(&m);
    put(&m, 0, &SA);
    m.p.pad[0][PD_SAMPLE] = 0;
    dn_smp_pad(&m, 0, 127);
    render(&m, FS / 2u);
    f = freq(2000, FS / 2u);
    check("pad 1, PITCH 0: 440 Hz (a 22 kHz sample), cents off", fabs(cents(f, 440.0)) < 2, cents(f, 440.0), 0);
    check("... at its level (LEVEL 100, velocity 127: about the sample's)", peak(2000, FS / 2u) > 14000 &&
          peak(2000, FS / 2u) < 18000, peak(2000, FS / 2u), 16000);
    m.p.pad[0][PD_PITCH] = 12, m.p.pad[0][PD_FINE] = -50;
    dn_smp_pad(&m, 0, 127);
    render(&m, FS / 2u);
    f = freq(2000, FS / 2u);
    check("PITCH +12, FINE -50: 880 Hz less half a semitone, cents off", fabs(cents(f, 880.0 * pow(2, -0.5 / 12))) < 2,
          cents(f, 880.0 * pow(2, -0.5 / 12)), 0);
    /* START / END: from the middle, half as long */
    m.p.pad[0][PD_PITCH] = 0, m.p.pad[0][PD_FINE] = 0, m.p.pad[0][PD_START] = 500;
    dn_smp_cut(&m);
    dn_smp_pad(&m, 0, 127);
    render(&m, FS * 3u / 2u);
    {
        uint32_t last = 0;
        for (uint32_t i = 0; i < FS * 3u / 2u; i++) if (abs(lr[2u * i]) > 100) last = i;
        check("START 50 %: a 2 s sample plays 1 s (seconds)", fabs(last / 44100.0 - 1.0) < 0.01, last / 44100.0, 1.0);
        check("... then silence (and its voice free)", peak(last + 200, FS * 3u / 2u) < 1 && !voices(&m),
              peak(last + 200, FS * 3u / 2u), 0);
    }
    m.p.pad[0][PD_START] = 0, m.p.pad[0][PD_END] = 250;
    dn_smp_pad(&m, 0, 127);
    render(&m, FS);
    {
        uint32_t last = 0;
        for (uint32_t i = 0; i < FS; i++) if (abs(lr[2u * i]) > 100) last = i;
        check("END 25 %: 0.5 s (seconds)", fabs(last / 44100.0 - 0.5) < 0.01, last / 44100.0, 0.5);
        check("... its end cut without a click (largest step, of a 440 Hz sine's ~1000)", maxstep(last - 50, last + 300) < 1200,
              maxstep(last - 50, last + 300), 1000);
    }
    /* REVERSE: a sample of 440 Hz then 880 Hz plays 880 then 440 */
    put(&m, 1, &SC);
    m.p.pad[1][PD_SAMPLE] = 1, m.p.pad[1][PD_REV] = 1;
    dn_smp_pad(&m, 1, 127);
    render(&m, FS);
    {
        double f1 = freq(1000, FS / 2u - 1000), f2 = freq(FS / 2u + 1000, FS - 1000);
        check("REVERSE: the second half first (880 Hz, cents)", fabs(cents(f1, 880.0)) < 3, cents(f1, 880.0), 0);
        check("... then the first (440 Hz, cents)", fabs(cents(f2, 440.0)) < 3, cents(f2, 440.0), 0);
        check("... no clicks along it (largest step)", maxstep(10, FS - 1000) < 2600, maxstep(10, FS - 1000), 2500);
    }
    /* CHOKE: pads 3 and 4 in group 1: 4 cuts 3 */
    put(&m, 2, &SA), put(&m, 3, &SB);
    m.p.pad[2][PD_SAMPLE] = 0, m.p.pad[2][PD_CHOKE] = 1;
    m.p.pad[3][PD_SAMPLE] = 1, m.p.pad[3][PD_CHOKE] = 1;
    dn_smp_cut(&m);
    render(&m, 64);
    dn_smp_pad(&m, 2, 127);
    render(&m, 4410);
    check("CHOKE: pad 3 playing", voices(&m) == 1, voices(&m), 1);
    dn_smp_pad(&m, 3, 127);
    render(&m, FS / 4u);
    check("... pad 4 (the same group) cuts it: one voice", voices(&m) == 1 && m.v[0].idx == 3 ? 1 : voices(&m) == 1,
          voices(&m), 1);
    f = freq(2000, FS / 4u);
    check("... and it's pad 4's sample (660 Hz)", fabs(cents(f, 660.0)) < 3, cents(f, 660.0), 0);
    check("... no click where it cut (largest step, of a 660 Hz sine's ~1500)", maxstep(0, 400) < 3000, maxstep(0, 400),
          1500);
    m.p.pad[3][PD_CHOKE] = 0;
    dn_smp_pad(&m, 2, 127);
    dn_smp_pad(&m, 3, 127);
    render(&m, 100);
    check("... no group: both play", voices(&m) == 2, voices(&m), 2);

    /* 2: KEYS: the map. Zone 1 (A, 440 Hz, root A3 = 69) on keys 0..59; zone 2 (B, 660 Hz, root 60) on 60..127 */
    dn_smp_init(&m);
    m.p.mode = DN_SMP_KEYS;
    put(&m, DN_SMP_PADS + 0u, &SA), put(&m, DN_SMP_PADS + 1u, &SB);
    m.p.zone[0][ZN_SAMPLE] = 0, m.p.zone[0][ZN_ROOT] = 69, m.p.zone[0][ZN_LOW] = 0, m.p.zone[0][ZN_HIGH] = 59;
    m.p.zone[1][ZN_SAMPLE] = 1, m.p.zone[1][ZN_ROOT] = 60, m.p.zone[1][ZN_LOW] = 60, m.p.zone[1][ZN_HIGH] = 127;
    {
        static const int notes[6] = {33, 45, 57, 59, 60, 84};
        double worst = 0;
        for (int i = 0; i < 6; i++) {
            int n = notes[i];
            double want = n < 60 ? 440.0 * pow(2, (n - 69) / 12.0) : 660.0 * pow(2, (n - 60) / 12.0);
            dn_smp_cut(&m);
            dn_smp_note_on(&m, (uint32_t)n, 127);
            render(&m, FS / 4u);
            f = freq(2000, FS / 4u);
            if (fabs(cents(f, want)) > worst) worst = fabs(cents(f, want));
            snprintf(t, sizeof t, "KEYS: note %d plays %.1f Hz (zone %d), cents off", n, want, n < 60 ? 1 : 2);
            check(t, fabs(cents(f, want)) < 3, cents(f, want), 0);
            dn_smp_note_off(&m, (uint32_t)n);
        }
        check("... zone edges: 59 in zone 1, 60 in zone 2", dn_smp_zone_of(&m, 59) == 0 && dn_smp_zone_of(&m, 60) == 1,
              dn_smp_zone_of(&m, 60), 1);
    }
    m.p.zone[1][ZN_TUNE] = 50;
    dn_smp_cut(&m);
    dn_smp_note_on(&m, 60, 127);
    render(&m, FS / 4u);
    f = freq(2000, FS / 4u);
    check("TUNE +50: half a semitone up (cents off)", fabs(cents(f, 660.0 * pow(2, 0.5 / 12))) < 3,
          cents(f, 660.0 * pow(2, 0.5 / 12)), 0);
    m.p.zone[1][ZN_TUNE] = 0;
    /* the release: the note let go fades (RELEASE), then silence */
    dn_smp_note_off(&m, 60);
    render(&m, FS);
    check("note off: it fades, then silence and no voice", peak(FS / 2u, FS) < 1 && !voices(&m), peak(FS / 2u, FS), 0);
    /* 4 voices: a 5th note steals the oldest, without a click */
    for (int i = 0; i < 4; i++) dn_smp_note_on(&m, (uint32_t)(60 + 3 * i), 100);
    render(&m, 2000);
    check("4 notes: 4 voices", voices(&m) == 4, voices(&m), 4);
    dn_smp_note_on(&m, 75, 100);
    render(&m, 2000);
    check("a 5th: still 4 (the oldest stolen)", voices(&m) == 4, voices(&m), 4);
    m.p.key[KY_VOICES] = 1;
    dn_smp_note_on(&m, 72, 100);
    render(&m, 100);
    check("VOICES 1: a new note takes the keys' voices (mono)", voices(&m) == 1, voices(&m), 1);
    m.p.key[KY_VOICES] = 4;

    /* 3: LOOP: a 2 s sample looped from 30 % to 31.3 % (not whole cycles): no click where it goes round */
    dn_smp_cut(&m);
    render(&m, 64);
    m.p.zone[0][ZN_LOW] = 0, m.p.zone[0][ZN_HIGH] = 127, m.p.zone[0][ZN_ROOT] = 69;
    m.p.zone[0][ZN_LOOP] = DN_SMP_LLOOP, m.p.zone[0][ZN_LSTART] = 300, m.p.zone[0][ZN_LEND] = 313;
    dn_smp_zone_prepare(&m, 0);
    dn_smp_note_on(&m, 69, 127);
    render(&m, FS * 3u);
    {
        int32_t own = maxstep(FS / 10u, FS / 5u), all = maxstep(FS / 5u, FS * 3u);
        check("LOOP: still sounding after 3 s of a 2 s sample", peak(FS * 3u - 4410, FS * 3u) > 8000,
              peak(FS * 3u - 4410, FS * 3u), 16000);
        check("... no click where it goes round (largest step against the sine's own, x)", all <= own * 13 / 10,
              (double)all / own, 1.0);
        f = freq(FS, FS * 3u);
        check("... and in tune round and round (cents)", fabs(cents(f, 440.0)) < 15, cents(f, 440.0), 0);
    }
    /* SUST: round while held, let go: on to the end, then silence */
    dn_smp_cut(&m);
    render(&m, 64);
    m.p.zone[0][ZN_LOOP] = DN_SMP_LSUST;
    m.p.key[KY_REL] = 127;
    dn_smp_note_on(&m, 69, 127);
    render(&m, FS * 3u);
    check("SUST: held 3 s, still sounding", peak(FS * 3u - 4410, FS * 3u) > 8000, peak(FS * 3u - 4410, FS * 3u), 16000);
    dn_smp_note_off(&m, 69);
    render(&m, FS * 2u);
    {
        uint32_t last = 0;
        for (uint32_t i = 0; i < FS * 2u; i++) if (abs(lr[2u * i]) > 100) last = i;
        check("... let go: plays on from the loop to the end (70 % of 2 s, s)", fabs(last / 44100.0 - 1.4) < 0.05,
              last / 44100.0, 1.4);
    }
    /* (0.39) a pad's hit FX, at 120 BPM (a beat: 22050 samples; a cycle: a 32nd, 2756): a 440 Hz pad of 0.5 s */
    {
        static dn_smp_t h;
        const uint32_t g = 22050u / 8u;
        uint32_t last;
        int32_t d = 0;
        dn_smp_init(&h);
        put(&h, 0, &SA);
        h.p.pad[0][PD_SAMPLE] = 0, h.p.pad[0][PD_END] = 250;
#define LAST(n) do { last = 0; for (uint32_t i = 0; i < (n); i++) if (abs(lr[2u * i]) > 100) last = i; } while (0)
        dn_smp_hfx(&h, DN_HFX_STR2, 120);
        dn_smp_pad(&h, 0, 127);
        render(&h, FS * 2u);
        LAST(FS * 2u);
        check("HIT FX STR 1/2: 0.5 s of pad lasts 1 s (less the last cycle's reach: s)",
              last > FS * 92u / 100u && last <= FS, last / 44100.0, 1.0);
        f = freq(3u * g + 100u, 4u * g - 100u);
        check("... at its own pitch (440 Hz inside a cycle, cents)", fabs(cents(f, 440.0)) < 5, cents(f, 440.0), 0);
        check("... the cycles' joins declicked (largest step: the sine's own ~1000, a join's fade adds under 700)",
              maxstep(200, last - 10) < 1700, maxstep(200, last - 10), 1000);
        dn_smp_hfx(&h, DN_HFX_STR4, 120);
        dn_smp_pad(&h, 0, 127);
        render(&h, FS * 3u);
        LAST(FS * 3u);
        check("HIT FX STR 1/4: lasts 2 s (less the last cycle's reach: s)", last > FS * 18u / 10u && last <= 2u * FS,
              last / 44100.0, 2.0);
        dn_smp_hfx(&h, DN_HFX_STUT32, 120);
        dn_smp_pad(&h, 0, 127);
        render(&h, FS);
        for (uint32_t i = 2u * g + 200u; i < 3u * g - 200u; i++)
            if (abs(lr[2u * i] - lr[2u * (i + g)]) > d) d = abs(lr[2u * i] - lr[2u * (i + g)]);
        check("HIT FX STUT32: the first 32nd over and over (a cycle vs the next: largest difference)", d < 300, d, 0);
        LAST(FS);
        check("... for a beat, then the pad plays on (it ends past 0.6 s, s)", last > FS * 6u / 10u, last / 44100.0, 0.75);
        dn_smp_hfx(&h, DN_HFX_TAPE, 120);
        dn_smp_pad(&h, 0, 127);
        render(&h, FS);
        f = freq(FS / 4u - 1500u, FS / 4u + 1500u);
        check("HIT FX TAPESTOP: half speed half a beat in (Hz)", fabs(f - 220.0) < 25.0, f, 220.0);
        check("... stopped by the beat's end (voices)", voices(&h) == 0 && peak(23000u, FS) < 1, voices(&h), 0);
        dn_smp_hfx(&h, DN_HFX_OCTUP, 120);
        dn_smp_pad(&h, 0, 127);
        render(&h, FS);
        f = freq(1000, 10000);
        LAST(FS);
        check("HIT FX OCT+: 880 Hz (cents)", fabs(cents(f, 880.0)) < 3, cents(f, 880.0), 0);
        check("... a quarter second (s)", fabs(last / 44100.0 - 0.25) < 0.01, last / 44100.0, 0.25);
        dn_smp_hfx(&h, DN_HFX_OCTDN, 120);
        dn_smp_pad(&h, 0, 127);
        render(&h, FS);
        f = freq(1000, 30000);
        check("HIT FX OCT-: 220 Hz (cents)", fabs(cents(f, 220.0)) < 3, cents(f, 220.0), 0);
        dn_smp_pad(&h, 0, 127);
        render(&h, FS);
        f = freq(1000, 20000);
        LAST(FS);
        check("the next hit without one: as it was (440 Hz, 0.5 s)", fabs(cents(f, 440.0)) < 3 &&
              fabs(last / 44100.0 - 0.5) < 0.01, last / 44100.0, 0.5);
        h.p.pad[0][PD_REV] = 1;
        dn_smp_hfx(&h, DN_HFX_STR2, 120);
        dn_smp_pad(&h, 0, 127);
        render(&h, FS * 2u);
        LAST(FS * 2u);
        check("HIT FX STR 1/2 on a reversed pad: 1 s too (s)", last > FS * 92u / 100u && last <= FS, last / 44100.0, 1.0);
        check("... declicked (largest step; not its last cycle: the test encoder's own start, reversed)",
              maxstep(200, last - 1500) < 1700, maxstep(200, last - 1500), 1000);
    }
    printf(fails ? "SAMPLER: %d FAILED\n" : "SAMPLER: pitch, zones, START / END, REVERSE, CHOKE, loops, voices all ok\n",
           fails);
    return fails ? 1 : 0;
}
