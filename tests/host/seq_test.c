/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT host test: the step sequencer's timing and logic against what the tempo says.
 * Runs the sequencer the way the audio interrupt does (128-frame blocks cut at every event) and logs each event with
 * its sample. Copyright (C) 2026 Jamie (TwistedGenetics). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dn_seq.h"

typedef struct { uint64_t at; char kind; uint32_t track; uint8_t note, vel; int slide; } ev_t;
static ev_t ev[20000];
static uint32_t nev;
static uint64_t now;
static int fails;

static void on(uint32_t t, uint8_t n, uint8_t v, int s) { ev[nev++] = (ev_t){now, 'N', t, n, v, s}; }
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
    s.pat.tr[0].st[0] = (dn_step_t){53, DN_ST_ON, 12, 100};
    s.pat.tr[0].st[1] = (dn_step_t){56, DN_ST_ON | DN_ST_SLIDE, 8, 100};
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

    /* 6: the demo pattern is valid and plays */
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
