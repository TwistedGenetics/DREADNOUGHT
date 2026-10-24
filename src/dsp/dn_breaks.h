/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: BREAKS, the Rex-style break chopper, standalone (no platform, no UI). Copyright (C) 2026 Jamie
 * (TwistedGenetics).
 *
 * A loop (IMA ADPCM, mono: the FM-1's sample-slot format, built-in or uploaded) is cut into 16 slices a bar. A step
 * plays one slice; the loop's speed follows the song's tempo, so a slice lasts one sixteenth and slices played in
 * order give the break back at the song's tempo (a 136 BPM break at 172 is sped up, pitch and all: the jungle way).
 * On top of the sync: PITCH in semitones, REVERSE per step, a release DECAY after the step's gate, CRUSH (sample
 * rate and bits down). One voice a track: a new slice cuts the last one (declicked), as a sampler's choke does.
 *
 * Loading a loop (dn_brk_load) decodes it once in the main loop: the decoder's state every 256 samples (so a slice
 * can start anywhere without decoding from the top) and a peak picture for the screen. Playing forward decodes as it
 * goes; a reversed slice is decoded into RAM first. dn_brk_tempo works out the speed (main loop, float); trigger,
 * release and render run in the audio interrupt. */
#pragma once
#include <stdint.h>

#define DN_BRK_CK 640u                              /* decoder states, one per 256 samples: loops up to ~7 s */
#define DN_BRK_RBUF 4096u                           /* a reversed slice, samples (~186 ms of a 22 kHz loop) */
#define DN_BRK_PEAKS 232u                           /* the screen's picture of the loop: one peak per column */
#define DN_BRK_MAXSL 64u                            /* slices a loop at most */
#define DN_BRK_GRAIN 1024u                          /* STRETCH: a grain, output samples (~23 ms), two overlapping */

typedef struct {
    const uint8_t *adpcm;
    uint32_t n;                                     /* samples */
    uint32_t rate;                                  /* Hz (only for the bar guess: playback follows the tempo) */
    uint8_t bars;                                   /* 1, 2 or 4 */
    char name[9];
} dn_loop_t;

typedef struct {
    /* the loop */
    dn_loop_t loop;
    uint8_t ok;                                     /* loaded: the audio may play it */
    uint8_t nslices;                                /* the slices in use (start[] has nslices + 1 entries: the end) */
    uint32_t start[DN_BRK_MAXSL + 1u];              /* where each slice starts, samples (the table in use) */
    uint8_t n_auto, n_man;
    uint32_t auto_start[DN_BRK_MAXSL];              /* AUTO: the loop's hits (found by dn_brk_load) */
    uint32_t man_start[DN_BRK_MAXSL];               /* MANUAL: set by hand (the app keeps them per loop) */
    uint32_t key;                                   /* the loop's identity for its manual slices: dn_brk_key */
    uint32_t ck[DN_BRK_CK];                         /* decoder state before sample 256 i: pred (16 bits), index */
    uint8_t peaks[DN_BRK_PEAKS];
    /* the sound */
    int8_t pitch;                                   /* -24..24 semitones on top of the tempo sync */
    uint8_t decay;                                  /* 0..126: release 1 ms .. 10 s after the gate; 127: none */
    uint8_t crush;                                  /* 0..127 */
    uint8_t level;                                  /* 0..127 (100: as 0.5) */
    uint8_t choke;                                  /* 0 off, 1..4: a hit cuts the other voices in its group (the app) */
    uint8_t smode;                                  /* slices: 0 GRID (16 a bar), 1 AUTO (the loop's hits), 2 MANUAL */
    uint8_t stretch;                                /* 0 REPITCH, 1 SLICE (Rex), 2 STRETCH (granular) */
    uint32_t inc;                                   /* loop samples a sample, Q16 (dn_brk_tempo): REPITCH, SLICE */
    uint32_t tinc;                                  /* STRETCH: the time moving on (the tempo's rate), Q16 */
    uint32_t pinc;                                  /* STRETCH: the grains' own speed (the pitch's rate), Q16 */
    /* the voice */
    uint8_t on, rev, rel, fast;                     /* fast: the release is a quick fade (the transport stopped) */
    int32_t mpitch;                         /* the LFOs (the app): pitch in 1/256 semitone (dn_brk_tempo applies it) */
    int16_t mdecay, mcrush, mlevel;         /* ... and steps on DECAY, CRUSH, LEVEL */
    uint32_t bpm;                           /* (the tempo last set) */
    uint8_t aud;                                    /* the whole loop playing round (the chop page's preview) */
    uint8_t stopend;                                /* this hit stops at its slice's end (auditioning a chop) */
    uint8_t slice;                                  /* the slice playing (the screen's playhead) */
    uint32_t p;                                     /* forward: the next sample to decode */
    int32_t pred, idx;
    int32_t a, b;                                   /* forward: the two samples either side of the position */
    uint32_t frac;                                  /* Q16 */
    int32_t rpos;                                   /* reverse: the position in rbuf, Q16 (counts down) */
    int32_t env, gain;                              /* Q15 */
    int32_t dc, last;                               /* the declick: what was sounding when a slice cut in */
    uint32_t hold_n;
    int32_t held;
    uint32_t played;                                /* samples of the loop played since the trigger (the playhead) */
    uint32_t endp;                                  /* SLICE: where the slice ends (forward) */
    uint32_t blen;                                  /* the slice in rbuf: samples (reverse, STRETCH) */
    int32_t tpos;                                   /* STRETCH: where the time is in rbuf, Q16 */
    int32_t tdir;                                   /* STRETCH: +1 forward, -1 reversed */
    uint32_t gpos[2], gage[2];                      /* STRETCH: each grain's read place (Q16) and age (samples) */
    int16_t rbuf[DN_BRK_RBUF];
} dn_brk_t;

void dn_brk_init(dn_brk_t *b);
/* main loop, with the voice not playing (b->ok = 0 under the app's lock first) */
void dn_brk_load(dn_brk_t *b, const dn_loop_t *loop);
uint8_t dn_brk_bars_of(uint32_t n, uint32_t rate);  /* 1, 2 or 4: the tempo nearest 120 inside 80..180 (X0X's guess) */
void dn_brk_tempo(dn_brk_t *b, uint32_t bpm);        /* main loop: after a tempo, pitch or loop change */
uint32_t dn_brk_key(const dn_loop_t *loop);         /* a loop's identity: its length and a hash of its first bytes */
/* main loop: put the table smode asks for in use (GRID: 16 a bar; AUTO: the hits found; MANUAL: man_start, or if there
 * is none yet, a copy of AUTO). The app holds its lock around it (the audio reads start[]) */
void dn_brk_slices_apply(dn_brk_t *b);
uint32_t dn_brk_grid_slices(const dn_brk_t *b);     /* 16 a bar */
/* the slice nearest a place in the loop given as num / den of its length (the sequencer's default for a step) */
uint32_t dn_brk_slice_at(const dn_brk_t *b, uint32_t num, uint32_t den);
/* the peaks of samples from .. from + n into cols columns (0..255): the slice editor's zoom (main loop) */
void dn_brk_range_peaks(const dn_brk_t *b, uint32_t from, uint32_t n, uint8_t *out, uint32_t cols);
/* the sample nearest `at` where the wave crosses zero, within +-range */
uint32_t dn_brk_zero_near(const dn_brk_t *b, uint32_t at, uint32_t range);
/* audio interrupt */
void dn_brk_trigger(dn_brk_t *b, uint32_t slice, uint32_t vel, int reverse);
void dn_brk_release(dn_brk_t *b);
void dn_brk_cut(dn_brk_t *b);                       /* a quick fade whatever DECAY says (stop) */
void dn_brk_choke(dn_brk_t *b);                     /* stop now (another voice in its choke group hit): declicked */
/* the whole loop from sample `at`, at its own speed, round and round until dn_brk_cut (chopping by ear) */
void dn_brk_audition(dn_brk_t *b, uint32_t at);
uint32_t dn_brk_pos(const dn_brk_t *b);              /* where it is playing now, samples into the loop */
void dn_brk_render(dn_brk_t *b, int32_t *lr, uint32_t n);   /* adds, stereo, Q15 scale */
