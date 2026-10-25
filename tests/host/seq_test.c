/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT host test: the step sequencer's timing and logic against what the tempo says.
 * Runs the sequencer the way the audio interrupt does (128-frame blocks cut at every event) and logs each event with
 * its sample. Copyright (C) 2026 Jamie (TwistedGenetics). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dn_seq.h"

typedef struct { uint64_t at; char kind; uint32_t track; uint8_t note, vel; int slide; } ev_t;
static ev_t ev[20000];
static uint32_t nev;
static uint64_t now;
static int fails;

static void on(uint32_t t, uint8_t n, uint8_t v, int s, uint8_t f) { (void)f; ev[nev++] = (ev_t){now, 'N', t, n, v, s}; }
static void off(uint32_t t, uint8_t n) { ev[nev++] = (ev_t){now, 'O', t, n, 0, 0}; }
static void clk(uint8_t st) { ev[nev++] = (ev_t){now, st == 0xF8 ? 'C' : st == 0xFA ? 'S' : 'P', 0, 0, 0, 0}; }
static const dn_seq_cb_t CB = {on, off, clk, 0, 0};

/* 0.11: for the conditions, a lighter log: which pass of its track each note-on came in */
static dn_seq_t *seen_seq;
static uint32_t ons[DN_SEQ_TRACKS][10240], vels[64], nvel;
static uint64_t vel_at[64];
static void on2(uint32_t t, uint8_t n, uint8_t v, int sl, uint8_t f)
{
    (void)n, (void)sl, (void)f;
    uint32_t p = seen_seq->pass[t];
    if (p < 10240u) ons[t][p]++;
    if (t == 0u && nvel < 64u) { vels[nvel] = v; vel_at[nvel++] = now; }
}
static void off2(uint32_t t, uint8_t n) { (void)t, (void)n; }
static void clk2(uint8_t st) { (void)st; }
static const dn_seq_cb_t CB2 = {on2, off2, clk2, 0, 0};
static void run2(dn_seq_t *s, uint64_t frames)
{
    while (frames) {
        uint32_t k = dn_seq_next(s);
        if (k > frames) k = (uint32_t)frames;
        now += k;
        dn_seq_run(s, k, &CB2);
        frames -= k;
    }
}
/* track 0, `len` steps, a note on step 0 with condition c; run `passes` passes; ons[0][p] says which played */
static void cond_run(dn_seq_t *s, uint32_t len, uint32_t c, uint32_t passes, double spb)
{
    dn_seq_init(s);
    seen_seq = s;
    memset(ons, 0, sizeof ons);
    nvel = 0;
    s->pat.tr[0].len = (uint8_t)len;
    s->pat.tr[0].st[0] = (dn_step_t){53, DN_ST_ON, 2, 100, (uint8_t)c, 0, 0, 0};
    now = 0;
    dn_seq_start(s, &CB2);
    run2(s, (uint64_t)(spb * len * passes) - 10u);
}
static uint32_t mask16(void) { uint32_t m = 0, p; for (p = 0; p < 16u; p++) if (ons[0][p]) m |= 1u << p; return m; }
static uint32_t find_ab(uint32_t a, uint32_t b, int neg)
{
    uint32_t c, A, B;
    for (c = DN_CO_AB; c < DN_CO_AB + DN_CO_NAB; c++) {
        dn_cond_ab(c, &A, &B);
        if (A == a && B == b) return c + (neg ? DN_CO_NAB : 0u);
    }
    return 0;
}

static void run(dn_seq_t *s, uint64_t frames)      /* as the audio interrupt does */
{
    while (frames) {
        uint32_t blk = frames > 128u ? 128u : (uint32_t)frames, done = 0;
        while (done < blk) {
            uint32_t k = dn_seq_next(s);
            if (k > blk - done)
                k = blk - done;
            now += k;
            dn_seq_run(s, k, &CB);
            done += k;
        }
        frames -= blk;
    }
}

static void check(const char *what, int ok, double got, double want)
{
    printf("%s %s: %.3f (want %.3f)\n", ok ? "ok  " : "FAIL", what, got, want);
    fails += !ok;
}

static uint32_t count(char kind, uint32_t track)
{
    uint32_t i, n = 0;
    for (i = 0; i < nev; i++)
        n += ev[i].kind == kind && (kind == 'C' || ev[i].track == track);
    return n;
}

int main(void)
{
    static dn_seq_t s;
    uint32_t i, k, prev;
    double spb = 44100.0 * 15.0 / 172.0;            /* a sixteenth at 172 BPM, samples */

    /* 1: four on-beat steps, no swing: the notes land on the sixteenth grid, 64 sixteenths of clock = 384 pulses */
    dn_seq_init(&s);
    for (i = 0; i < 16; i += 4)
        s.pat.tr[0].st[i].flags = DN_ST_ON;
    dn_seq_start(&s, &CB);
    run(&s, (uint64_t)(spb * 64.0) - 10);
    for (i = k = 0; i < nev; i++)
        if (ev[i].kind == 'N') {
            double want = 1.0 + 4.0 * spb * k;
            if (k < 16 && (ev[i].at < want - 1.5 || ev[i].at > want + 1.5)) {
                check("note time (samples)", 0, (double)ev[i].at, want);
                break;
            }
            k++;
        }
    check("16 notes in 4 bars of quarters", k == 16, k, 16);
    check("clock pulses in 4 bars (24 a quarter)", count('C', 0) == 384, count('C', 0), 384);
    for (i = 0; i < nev && ev[i].kind != 'O'; i++)
        ;
    check("gate 12/16 of a step (samples)", i < nev && ev[i].at > 0.75 * spb - 2 && ev[i].at < 0.75 * spb + 3,
          (double)ev[i].at, 0.75 * spb + 1);
    dn_seq_stop(&s, &CB);
    check("stop: every note off", count('N', 0) == count('O', 0), count('O', 0), count('N', 0));

    /* 2: swing 66: the off-beat sixteenth lands 2/3 of the way through the pair */
    nev = 0; now = 0;
    dn_seq_init(&s);
    s.pat.swing = 66;
    s.pat.tr[0].st[0].flags = s.pat.tr[0].st[1].flags = DN_ST_ON;
    dn_seq_start(&s, &CB);
    run(&s, (uint64_t)(spb * 2));
    for (i = k = prev = 0; i < nev; i++)
        if (ev[i].kind == 'N') {
            if (k == 1)
                check("swing 66: second sixteenth's place in the pair", ev[i].at - prev > 1.32 * spb - 2 &&
                      ev[i].at - prev < 1.32 * spb + 2, (double)(ev[i].at - prev) / (2 * spb), 0.66);
            prev = (uint32_t)ev[i].at;
            k++;
        }

    /* 3: a slide: the second note starts without the first ending first (legato), then the first ends */
    nev = 0; now = 0;
    dn_seq_init(&s);
    s.pat.tr[0].st[0] = (dn_step_t){53, DN_ST_ON, 3, 100, 0, 0, 0, 0};
    s.pat.tr[0].st[1] = (dn_step_t){56, DN_ST_ON | DN_ST_SLIDE, 2, 100, 0, 0, 0, 0};
    dn_seq_start(&s, &CB);
    run(&s, (uint64_t)(spb * 3));
    {
        int ok = 0;
        for (i = 0; i + 1 < nev; i++)
            if (ev[i].kind == 'N' && ev[i].note == 56)
                ok = ev[i].slide && ev[i + 1].kind == 'O' && ev[i + 1].note == 53;
        for (i = 0; i < nev; i++)
            if (ev[i].kind == 'O' && ev[i].note == 53 && ev[i].at < spb - 2)
                ok = 0;                             /* the first note was held into the slide, not cut at 12/16 */
        check("slide: tied, glides, old note off after the new one", ok, ok, 1);
    }

    /* 4: polymeter and mutes: track 2 (12 steps) against track 1 (16); track 3 muted */
    nev = 0; now = 0;
    dn_seq_init(&s);
    s.pat.tr[0].st[0].flags = DN_ST_ON;
    s.pat.tr[1].len = 12;
    s.pat.tr[1].st[0].flags = DN_ST_ON;
    s.pat.tr[2].st[0].flags = DN_ST_ON;
    s.pat.tr[2].mute = 1;
    dn_seq_start(&s, &CB);
    run(&s, (uint64_t)(spb * 48) - 10);
    check("polymeter: 16-step track, 3 notes in 48 steps", count('N', 0) == 3, count('N', 0), 3);
    check("polymeter: 12-step track, 4 notes in 48 steps", count('N', 1) == 4, count('N', 1), 4);
    check("muted track: silent", count('N', 2) == 0, count('N', 2), 0);

    /* 5: the tempo follows a change at once (the next step) */
    nev = 0; now = 0;
    dn_seq_init(&s);
    for (i = 0; i < 16; i++)
        s.pat.tr[0].st[i].flags = DN_ST_ON;
    s.bpm = 120;
    dn_seq_start(&s, &CB);
    run(&s, (uint64_t)(44100.0 * 15 / 120 * 8) - 10);
    check("120 BPM: 8 sixteenths in 1 s", count('N', 0) == 8, count('N', 0), 8);

    /* 6: long notes (0.6): a 4-step note holds 4 steps, across steps that are off */
    {
        uint64_t on_at = 0, off_at = 0;
        nev = 0; now = 0;
        dn_seq_init(&s);
        s.pat.tr[0].st[0] = (dn_step_t){53, DN_ST_ON, 16, 100, 0, 0, 0, 0};
        dn_seq_start(&s, &CB);
        run(&s, (uint64_t)(spb * 8));
        for (i = 0; i < nev; i++) {
            if (ev[i].kind == 'N' && !on_at) on_at = ev[i].at;
            if (ev[i].kind == 'O' && !off_at) off_at = ev[i].at;
        }
        check("a 4-step note lasts 4 steps (samples)", off_at && fabs((double)(off_at - on_at) - 4 * spb) < 3,
              (double)(off_at - on_at), 4 * spb);
    }
    /* across the pattern's end: a 4-step track, a 2-step note on its last step */
    {
        uint64_t on_at = 0, off_at = 0;
        nev = 0; now = 0;
        dn_seq_init(&s);
        s.pat.tr[0].len = 4;
        s.pat.tr[0].st[3] = (dn_step_t){53, DN_ST_ON, 8, 100, 0, 0, 0, 0};
        dn_seq_start(&s, &CB);
        run(&s, (uint64_t)(spb * 6));
        for (i = 0; i < nev; i++) {
            if (ev[i].kind == 'N' && !on_at) on_at = ev[i].at;
            if (ev[i].kind == 'O' && !off_at) off_at = ev[i].at;
        }
        check("a note across the pattern's end holds (samples)", off_at && fabs((double)(off_at - on_at) - 2 * spb) < 3,
              (double)(off_at - on_at), 2 * spb);
    }
    /* a later step cuts a long note; a later step with slide glides from it (no gap) */
    {
        uint64_t off_at = 0, on2 = 0;
        nev = 0; now = 0;
        dn_seq_init(&s);
        s.pat.tr[0].st[0] = (dn_step_t){53, DN_ST_ON, 32, 100, 0, 0, 0, 0};
        s.pat.tr[0].st[2] = (dn_step_t){56, DN_ST_ON, 4, 100, 0, 0, 0, 0};
        DN_PAT_CUT(&s.pat) = 1;                     /* OVERLAP CUT on track 1 */
        dn_seq_start(&s, &CB);
        run(&s, (uint64_t)(spb * 3));
        for (i = 0; i < nev; i++) {
            if (ev[i].kind == 'O' && ev[i].note == 53 && !off_at) off_at = ev[i].at;
            if (ev[i].kind == 'N' && ev[i].note == 56 && !on2) on2 = ev[i].at;
        }
        check("OVERLAP CUT: a later step cuts the long note at its own time", off_at && on2 && off_at <= on2 && on2 - off_at < 2,
              (double)off_at, (double)on2);
        nev = 0; now = 0;
        dn_seq_init(&s);
        s.pat.tr[0].st[0] = (dn_step_t){53, DN_ST_ON, 8, 100, 0, 0, 0, 0};
        s.pat.tr[0].st[2] = (dn_step_t){56, DN_ST_ON | DN_ST_SLIDE, 4, 100, 0, 0, 0, 0};
        dn_seq_start(&s, &CB);
        run(&s, (uint64_t)(spb * 3));
        {
            int ok = 0;
            for (i = 0; i + 1 < nev; i++)
                if (ev[i].kind == 'N' && ev[i].note == 56)
                    ok = ev[i].slide && ev[i + 1].kind == 'O' && ev[i + 1].note == 53;
            check("a slide two steps on glides from the long note (no gap)", ok, ok, 1);
        }
    }
    /* 0.8: a note reaching past the next one slides into it (OVERLAP SLIDE, the default); one that ends on time, an
     * OVERLAP CUT track and a drum track (not legato) retrigger */
    {
        static const struct { uint8_t len, cut, legato; int slide; const char *what; } C[] = {
            {9, 0, 0xFF, 1, "overlap (2 1/4 steps, the next note at 2): one note-on with slide, old off just after"},
            {32, 0, 0xFF, 1, "overlap (8 steps): slides too"},
            {8, 0, 0xFF, 0, "a note that ends where the next starts: retriggers"},
            {9, 1, 0xFF, 0, "overlap on an OVERLAP CUT track: retriggers"},
            {9, 0, 0xFE, 0, "overlap on a track that isn't legato (a drum track): retriggers"},
        };
        uint32_t c;
        for (c = 0; c < sizeof C / sizeof C[0]; c++) {
            int ok = 0, offs_before = 0, seen = 0;
            nev = 0; now = 0;
            dn_seq_init(&s);
            s.legato = C[c].legato;
            DN_PAT_CUT(&s.pat) = C[c].cut;
            s.pat.tr[0].st[0] = (dn_step_t){53, DN_ST_ON, C[c].len, 100, 0, 0, 0, 0};
            s.pat.tr[0].st[2] = (dn_step_t){58, DN_ST_ON, 4, 100, 0, 0, 0, 0};
            dn_seq_start(&s, &CB);
            run(&s, (uint64_t)(spb * 3));
            for (i = 0; i < nev; i++) {
                if (ev[i].kind == 'O' && ev[i].note == 53 && !seen)
                    offs_before++;
                if (ev[i].kind == 'N' && ev[i].note == 58 && !seen) {
                    seen = 1;
                    ok = C[c].slide ? ev[i].slide && !offs_before && i + 1 < nev && ev[i + 1].kind == 'O' &&
                                      ev[i + 1].note == 53
                                    : !ev[i].slide && offs_before == 1;
                }
            }
            check(C[c].what, ok, ok, 1);
        }
        /* and the slide holds on the second time round (the pattern loops: the overlap is found again) */
        {
            int slides = 0;
            nev = 0; now = 0;
            dn_seq_init(&s);
            s.pat.tr[0].len = 4;
            s.pat.tr[0].st[0] = (dn_step_t){53, DN_ST_ON, 12, 100, 0, 0, 0, 0};
            s.pat.tr[0].st[2] = (dn_step_t){58, DN_ST_ON, 4, 100, 0, 0, 0, 0};
            dn_seq_start(&s, &CB);
            run(&s, (uint64_t)(spb * 8));
            for (i = 0; i < nev; i++)
                slides += ev[i].kind == 'N' && ev[i].note == 58 && ev[i].slide;
            check("overlap slides every time round", slides == 2, slides, 2);
        }
    }
    /* a 0.5 pattern (DNQ1, lengths in sixteenths of a step) comes up to date */
    {
        static uint8_t old[8 + 8 * (64 * 4 + 4)];
        dn_pattern_t p;
        memset(old, 0, sizeof old);
        old[0] = 0x44, old[1] = 0x4E, old[2] = 0x51, old[3] = 0x31;
        for (i = 0; i < 8u; i++) old[8 + i * 260 + 256] = 16;
        old[8 + 2] = 16, old[8 + 6] = 12, old[8 + 10] = 1;
        check("0.5 pattern migrates", dn_pattern_from_old(&p, old, sizeof old) && p.tr[0].st[0].len == 4 &&
              p.tr[0].st[1].len == 3 && p.tr[0].st[2].len == 1, p.tr[0].st[0].len, 4);
    }
    /* the coverage the screen draws: a note's length reaches into the steps after it, up to the next note */
    {
        dn_pattern_t p;
        uint8_t cov[DN_SEQ_STEPS];
        dn_pattern_clear(&p);
        p.tr[0].st[0] = (dn_step_t){53, DN_ST_ON, 16, 100, 0, 0, 0, 0};
        p.tr[0].st[2] = (dn_step_t){53, DN_ST_ON, 4, 100, 0, 0, 0, 0};
        p.tr[0].st[14] = (dn_step_t){53, DN_ST_ON, 12, 100, 0, 0, 0, 0};
        dn_pattern_coverage(&p, 0, cov);
        check("coverage: start, held, cut by the next note, wraps", cov[0] == 1 && cov[1] == 2 && cov[2] == 1 &&
              cov[3] == 0 && cov[15] == 2 && cov[0] == 1 && cov[14] == 1, cov[1], 2);
    }

    /* 7: the demo pattern is valid and plays */
    {
        dn_pattern_t p;
        dn_pattern_demo(&p);
        check("demo pattern valid", dn_pattern_valid(&p), 1, 1);
        p.magic = 0;
        check("garbage refused", !dn_pattern_valid(&p), 0, 0);
    }
    /* 0.11: trig conditions, as the Digitakt's manual has them */
    {
        double spb = 44100.0 * 15.0 / 172.0;
        uint32_t n = 0, p, want;
        char tx[8];
        cond_run(&s, 1, 11, 10000, spb);            /* 11: 50 % */
        for (p = 0; p < 10000u; p++) n += ons[0][p] != 0u;
        check("50 %: plays half the time (10 000 passes, %)", n > 4800u && n < 5200u, n / 100.0, 50);
        dn_cond_text(11, tx);
        check("its name reads 50%", !strcmp(tx, "50%"), 0, 0);
        cond_run(&s, 1, 22, 1000, spb);
        for (n = 0, p = 0; p < 1000u; p++) n += ons[0][p] != 0u;
        check("100 %: always", n == 1000u, n, 1000);
        {
            static const struct { uint32_t a, b; int neg; uint32_t mask; const char *name; } AB[] = {
                {1, 2, 0, 0x5555u, "1:2: passes 1, 3, 5 ..."}, {2, 4, 0, 0x2222u, "2:4: passes 2, 6, 10, 14"},
                {3, 4, 0, 0x4444u, "3:4: passes 3, 7, 11, 15"}, {1, 2, 1, 0xAAAAu, "!1:2: passes 2, 4, 6 ..."},
                {8, 8, 0, 0x8080u, "8:8: passes 8, 16"},
            };
            for (p = 0; p < sizeof AB / sizeof AB[0]; p++) {
                uint32_t c = find_ab(AB[p].a, AB[p].b, AB[p].neg);
                cond_run(&s, 4, c, 16, spb);
                check(AB[p].name, mask16() == AB[p].mask, mask16(), AB[p].mask);
            }
            dn_cond_text(find_ab(2, 4, 1), tx);
            check("!2:4 reads !2:4", !strcmp(tx, "!2:4"), 0, 0);
        }
        cond_run(&s, 4, DN_CO_1ST, 16, spb);
        check("1ST: the first pass only", mask16() == 1u, mask16(), 1);
        cond_run(&s, 4, DN_CO_N1ST, 16, spb);
        check("!1ST: all but the first", mask16() == 0xFFFEu, mask16(), 0xFFFE);
        cond_run(&s, 4, DN_CO_FILL, 4, spb);
        check("FILL, not held: never", mask16() == 0u, mask16(), 0);
        dn_seq_init(&s);
        {   /* FILL held; LST with a change queued; and their opposites */
            static const struct { uint32_t c; uint8_t fill, lst; uint32_t mask; const char *name; } F[] = {
                {DN_CO_FILL, 1, 0, 0xF, "FILL held: always"}, {DN_CO_NFILL, 1, 0, 0, "!FILL held: never"},
                {DN_CO_LST, 0, 1, 0xF, "LST, a change queued: plays"}, {DN_CO_LST, 0, 0, 0, "LST, none queued: not"},
                {DN_CO_NLST, 0, 1, 0, "!LST, a change queued: not"},
            };
            for (p = 0; p < sizeof F / sizeof F[0]; p++) {
                dn_seq_init(&s);
                seen_seq = &s;
                memset(ons, 0, sizeof ons);
                s.fill = F[p].fill;
                s.lst = F[p].lst;
                s.pat.tr[0].len = 4;
                s.pat.tr[0].st[0] = (dn_step_t){53, DN_ST_ON, 2, 100, (uint8_t)F[p].c, 0, 0, 0};
                now = 0;
                dn_seq_start(&s, &CB2);
                s.fill = F[p].fill;
                s.lst = F[p].lst;
                run2(&s, (uint64_t)(spb * 16) - 10u);
                check(F[p].name, mask16() == F[p].mask, mask16(), F[p].mask);
            }
        }
        /* PRE: the manual's example: a 50 % trig, then PRE, PRE, !PRE (steps 1-4 of a 4-step track) */
        {
            uint32_t bad = 0;
            dn_seq_init(&s);
            seen_seq = &s;
            s.pat.tr[0].len = 4;
            s.pat.tr[0].st[0] = (dn_step_t){53, DN_ST_ON, 1, 100, 11, 0, 0, 0};
            s.pat.tr[0].st[1] = (dn_step_t){54, DN_ST_ON, 1, 100, DN_CO_PRE, 0, 0, 0};
            s.pat.tr[0].st[2] = (dn_step_t){55, DN_ST_ON, 1, 100, DN_CO_PRE, 0, 0, 0};
            s.pat.tr[0].st[3] = (dn_step_t){56, DN_ST_ON, 1, 100, DN_CO_NPRE, 0, 0, 0};
            /* (the log counts per pass: 4 = all three with the 50 %; 1 = only !PRE) */
            memset(ons, 0, sizeof ons);
            now = 0;
            dn_seq_start(&s, &CB2);
            run2(&s, (uint64_t)(spb * 4 * 400) - 10u);
            for (p = 0; p < 400u; p++) bad += ons[0][p] != 3u && ons[0][p] != 1u;
            check("PRE / !PRE follow the 50 % trig before them (400 passes: 3 notes or 1, never else)", bad == 0u, bad, 0);
        }
        /* NEI: track 2 plays exactly when track 1's 50 % did */
        {
            uint32_t bad = 0, both = 0;
            dn_seq_init(&s);
            seen_seq = &s;
            s.pat.tr[0].len = 1;
            s.pat.tr[1].len = 1;
            s.pat.tr[0].st[0] = (dn_step_t){53, DN_ST_ON, 1, 100, 11, 0, 0, 0};
            s.pat.tr[1].st[0] = (dn_step_t){53, DN_ST_ON, 1, 100, DN_CO_NEI, 0, 0, 0};
            memset(ons, 0, sizeof ons);
            now = 0;
            dn_seq_start(&s, &CB2);
            run2(&s, (uint64_t)(spb * 2000) - 10u);
            for (p = 0; p < 2000u; p++) { bad += (ons[0][p] != 0u) != (ons[1][p] != 0u); both += ons[1][p] != 0u; }
            check("NEI: the track plays when the track before's 50 % did (2000 steps)", bad == 0u && both > 800u, bad, 0);
        }
        /* a 12-step track counts its own passes: 1:2 on it plays at steps 0, 24, 48 of the song */
        {
            dn_seq_init(&s);
            seen_seq = &s;
            s.pat.tr[0].len = 12;
            s.pat.tr[0].st[0] = (dn_step_t){53, DN_ST_ON, 1, 100, (uint8_t)find_ab(1, 2, 0), 0, 0, 0};
            memset(ons, 0, sizeof ons);
            nvel = 0;
            now = 0;
            dn_seq_start(&s, &CB2);
            run2(&s, (uint64_t)(spb * 60) - 10u);
            want = nvel == 3u && fabs((double)vel_at[1] - 24 * spb) < 3 && fabs((double)vel_at[2] - 48 * spb) < 3;
            check("a 12-step track's 1:2: at steps 1, 25, 49 of the song", want, nvel, 3);
        }
    }
    /* 0.11: retrigs: when they land, their velocity curve, cut by the next note */
    {
        double spb = 44100.0 * 15.0 / 172.0, whole = 16 * spb;
        static const struct { uint8_t rtim, rlen; uint32_t n; double iv; const char *name; } RT[] = {
            {12, 4, 2, 0.5, "1/32 over a step: 2 hits, half a step apart"},
            {2, 64, 3, 16.0 / 3, "1/3 over a bar: 3 hits, a third of a bar apart"},
            {16, 4, 5, 0.2, "1/80 over a step: 5 hits, a fifth of a step apart"},
        };
        uint32_t r, i2;
        for (r = 0; r < sizeof RT / sizeof RT[0]; r++) {
            int ok = 1;
            dn_seq_init(&s);
            seen_seq = &s;
            s.pat.tr[0].len = 32;
            s.pat.tr[0].st[0] = (dn_step_t){53, DN_ST_ON | DN_ST_RTRG, 4, 100, 0, RT[r].rtim, RT[r].rlen, 0};
            nvel = 0;
            now = 0;
            dn_seq_start(&s, &CB2);
            run2(&s, (uint64_t)(spb * 20));
            ok = nvel == RT[r].n;
            for (i2 = 1; ok && i2 < nvel; i2++)
                ok = fabs((double)(vel_at[i2] - vel_at[0]) - i2 * RT[r].iv * spb) <= 1.5;
            check(RT[r].name, ok, nvel, RT[r].n);
        }
        (void)whole;
        {   /* the curves: 5 hits at vel 100 */
            static const struct { int8_t c; uint8_t v[5]; const char *name; } CV[] = {
                {-127, {100, 75, 50, 25, 1}, "RVEL -127: fades out (100 75 50 25 ~0)"},
                {0, {100, 100, 100, 100, 100}, "RVEL 0: flat"},
                {127, {1, 25, 50, 75, 100}, "RVEL +127: fades in (~0 25 50 75 100)"},
            };
            for (r = 0; r < 3u; r++) {
                int ok;
                dn_seq_init(&s);
                seen_seq = &s;
                s.pat.tr[0].len = 32;
                s.pat.tr[0].st[0] = (dn_step_t){53, DN_ST_ON | DN_ST_RTRG, 4, 100, 0, 16, 4, CV[r].c};
                nvel = 0;
                now = 0;
                dn_seq_start(&s, &CB2);
                run2(&s, (uint64_t)(spb * 2));
                ok = nvel == 5u;
                for (i2 = 0; ok && i2 < 5u; i2++) ok = vels[i2] == CV[r].v[i2];
                check(CV[r].name, ok, nvel ? vels[nvel - 1] : 0, CV[r].v[4]);
            }
        }
        {   /* a roll over 2 steps (1/32: 4 hits) cut by a note on step 2 */
            dn_seq_init(&s);
            seen_seq = &s;
            s.pat.tr[0].len = 32;
            s.pat.tr[0].st[0] = (dn_step_t){53, DN_ST_ON | DN_ST_RTRG, 4, 100, 0, 12, 8, 0};
            s.pat.tr[0].st[1] = (dn_step_t){60, DN_ST_ON, 2, 100, 0, 0, 0, 0};
            nvel = 0;
            now = 0;
            dn_seq_start(&s, &CB2);
            run2(&s, (uint64_t)(spb * 3));
            check("a retrig roll is cut by the next note (2 hits, then the note)", nvel == 3u, nvel, 3);
        }
    }
    /* 0.11: a 0.10 pattern (DNQ2, 4-byte steps) opens as it was */
    {
        static uint8_t old[8 + 8 * (64 * 4 + 4)];
        dn_pattern_t p;
        memset(old, 0, sizeof old);
        old[0] = 0x44, old[1] = 0x4E, old[2] = 0x51, old[3] = 0x32;
        old[4] = 58;
        for (i = 0; i < 8u; i++) old[8 + i * 260 + 256] = 16;
        old[8 + 0] = 60, old[9] = DN_ST_ON | DN_ST_ACC, old[10] = 8, old[11] = 90;
        old[8 + 260 + 256] = 12;
        check("0.10 pattern opens: step 1 as it was, no condition, no retrig",
              dn_pattern_from_old(&p, old, sizeof old) && p.tr[0].st[0].note == 60 && p.tr[0].st[0].len == 8 &&
              p.tr[0].st[0].vel == 90 && p.tr[0].st[0].flags == (DN_ST_ON | DN_ST_ACC) && p.tr[0].st[0].cond == 0 &&
              p.tr[1].len == 12 && p.swing == 58 && p.magic == DN_SEQ_MAGIC, p.tr[0].st[0].note, 60);
    }
    printf(fails ? "SEQ: %d FAILED\n" : "SEQ: timing, swing, slide, polymeter, mutes, conditions, retrigs all ok\n", fails);
    return fails ? 1 : 0;
}
