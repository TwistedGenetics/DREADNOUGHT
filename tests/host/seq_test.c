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
static const dn_seq_cb_t CB = {on, off, clk};

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
    s.pat.tr[0].st[0] = (dn_step_t){53, DN_ST_ON, 3, 100};
    s.pat.tr[0].st[1] = (dn_step_t){56, DN_ST_ON | DN_ST_SLIDE, 2, 100};
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
        s.pat.tr[0].st[0] = (dn_step_t){53, DN_ST_ON, 16, 100};
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
        s.pat.tr[0].st[3] = (dn_step_t){53, DN_ST_ON, 8, 100};
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
        s.pat.tr[0].st[0] = (dn_step_t){53, DN_ST_ON, 32, 100};
        s.pat.tr[0].st[2] = (dn_step_t){56, DN_ST_ON, 4, 100};
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
        s.pat.tr[0].st[0] = (dn_step_t){53, DN_ST_ON, 8, 100};
        s.pat.tr[0].st[2] = (dn_step_t){56, DN_ST_ON | DN_ST_SLIDE, 4, 100};
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
            s.pat.tr[0].st[0] = (dn_step_t){53, DN_ST_ON, C[c].len, 100};
            s.pat.tr[0].st[2] = (dn_step_t){58, DN_ST_ON, 4, 100};
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
            s.pat.tr[0].st[0] = (dn_step_t){53, DN_ST_ON, 12, 100};
            s.pat.tr[0].st[2] = (dn_step_t){58, DN_ST_ON, 4, 100};
            dn_seq_start(&s, &CB);
            run(&s, (uint64_t)(spb * 8));
            for (i = 0; i < nev; i++)
                slides += ev[i].kind == 'N' && ev[i].note == 58 && ev[i].slide;
            check("overlap slides every time round", slides == 2, slides, 2);
        }
    }
    /* a 0.5 pattern (DNQ1, lengths in sixteenths of a step) comes up to date */
    {
        dn_pattern_t p;
        dn_pattern_clear(&p);
        p.magic = DN_SEQ_MAGIC_V1;
        p.tr[0].st[0].len = 16;
        p.tr[0].st[1].len = 12;
        p.tr[0].st[2].len = 1;
        check("0.5 pattern migrates", dn_pattern_valid(&p) && p.magic == DN_SEQ_MAGIC && p.tr[0].st[0].len == 4 &&
              p.tr[0].st[1].len == 3 && p.tr[0].st[2].len == 1, p.tr[0].st[0].len, 4);
    }
    /* the coverage the screen draws: a note's length reaches into the steps after it, up to the next note */
    {
        dn_pattern_t p;
        uint8_t cov[DN_SEQ_STEPS];
        dn_pattern_clear(&p);
        p.tr[0].st[0] = (dn_step_t){53, DN_ST_ON, 16, 100};
        p.tr[0].st[2] = (dn_step_t){53, DN_ST_ON, 4, 100};
        p.tr[0].st[14] = (dn_step_t){53, DN_ST_ON, 12, 100};
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
    printf(fails ? "SEQ: %d FAILED\n" : "SEQ: timing, swing, slide, polymeter, mutes all ok\n", fails);
    return fails ? 1 : 0;
}
