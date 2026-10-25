/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: the 8-track step sequencer, standalone (no platform, no UI). Copyright (C) 2026 Jamie (TwistedGenetics).
 *
 * Eight tracks of up to 64 sixteenth-note steps, each track its own length (polymeter: a 12-step line against a 16-step
 * one). A step: on / off, a note, a velocity (accent), a gate length in sixteenths of the step, and a slide: the
 * note glides from the one before without a new attack (the 303's tie; on REESE, a legato note with its GLIDE).
 * Swing delays every second sixteenth. MIDI clock (24 a quarter) comes out with the steps.
 *
 * It runs in the audio interrupt, sample-accurate: the render asks how far the next event is (dn_seq_next), renders
 * that many samples, then lets the sequencer move on (dn_seq_run), which calls back for each note and clock pulse.
 * The panel edits the pattern and the transport from the main loop, under the app's lock. */
#pragma once
#include <stdint.h>

#define DN_SEQ_TRACKS 8u
#define DN_SEQ_STEPS 64u
#define DN_SEQ_MAGIC 0x33514E44u                    /* "DNQ3": 8-byte steps: conditions, retrigs (0.11) */
#define DN_SEQ_MAGIC_V2 0x32514E44u                 /* "DNQ2": 4-byte steps, lengths in quarter steps (0.6 .. 0.10) */
#define DN_SEQ_MAGIC_V1 0x31514E44u                 /* "DNQ1": 0.4 / 0.5, lengths in sixteenths of a step */
#define DN_LEN_STEP 4u                              /* length ticks a step */
#define DN_LEN_MAX 255u                             /* 63 3/4 steps */

enum { DN_ST_ON = 1u, DN_ST_SLIDE = 2u, DN_ST_ACC = 4u, DN_ST_RTRG = 8u };
/* (0.27) a chord: flags' top 4 bits, one of 15 chords (0: none; dn_app.c DN_CHORD_*: MIN, MAJ, MIN7 ...), built on the
 * step's note, so moving the note moves the chord */
#define DN_ST_CHORD 0xF0u
#define DN_ST_CHORD_OF(f) ((uint32_t)(f) >> 4)
#define DN_CHORDS 15u
#define DN_CHORD_N 5u

typedef struct {
    uint8_t note;                                   /* MIDI note */
    uint8_t flags;                                  /* DN_ST_* */
    uint8_t len;                                    /* how long the note lasts, in quarter steps: 1 (a 64th note)
                                                     * .. 255 (~64 steps); 4 = one step. It holds across the steps
                                                     * after it (and the pattern's end) until it runs out or a later
                                                     * step on the track cuts it (or slides from it) */
    uint8_t vel;                                    /* 1..127 */
    uint8_t cond;                                   /* the trig condition (Digitakt's): DN_CO_* (0: always) */
    uint8_t rtim;                                   /* retrig rate: DN_RTIM_DEN[rtim] a whole note (9: 1/16, a step) */
    uint8_t rlen;                                   /* retrigs last this long, quarter steps (0: the note's length) */
    int8_t rvel;                                    /* their velocity curve: -127 fades out .. 0 flat .. +127 fades in */
} dn_step_t;

/* trig conditions (Digitakt's, its manual 10.10.3): 0 none; 1..22 a chance (DN_CO_PCT, %); then FILL, !FILL, PRE,
 * !PRE, NEI, !NEI, 1ST, !1ST, LST, !LST; then A:B (1:2 2:2 1:3 2:3 3:3 ... 8:8: on the A-th of every B passes), then
 * !A:B in the same order */
#define DN_CO_NPCT 22u
enum { DN_CO_NONE = 0, DN_CO_FILL = DN_CO_NPCT + 1u, DN_CO_NFILL, DN_CO_PRE, DN_CO_NPRE, DN_CO_NEI, DN_CO_NNEI,
       DN_CO_1ST, DN_CO_N1ST, DN_CO_LST, DN_CO_NLST, DN_CO_AB };
#define DN_CO_NAB 35u                               /* A:B with 1 <= A <= B, 2 <= B <= 8 */
#define DN_CO_N (DN_CO_AB + 2u * DN_CO_NAB)         /* (then !A:B) */
extern const uint8_t DN_CO_PCT[DN_CO_NPCT];
void dn_cond_ab(uint32_t c, uint32_t *a, uint32_t *b);   /* an A:B / !A:B condition's A and B */
void dn_cond_text(uint32_t c, char out[8]);
#define DN_RTIM_N 17u
extern const uint8_t DN_RTIM_DEN[DN_RTIM_N];        /* 1/1 .. 1/80 of a whole note */

typedef struct {
    dn_step_t st[DN_SEQ_STEPS];
    uint8_t len;                                    /* 1..64 */
    uint8_t mute;
    uint8_t chan;                                   /* MIDI channel 0..15 (tracks without an engine play it) */
    uint8_t last_note;                              /* the note a new step gets */
} dn_track_t;

typedef struct {
    uint32_t magic;
    uint8_t swing;                                  /* 50..75: the second sixteenth's share of the pair, % */
    uint8_t pad[3];
    dn_track_t tr[DN_SEQ_TRACKS];
} dn_pattern_t;

/* what the sequencer asks of the app (all called in the audio interrupt) */
typedef struct {
    void (*note_on)(uint32_t track, uint8_t note, uint8_t vel, int slide, uint8_t flags);   /* flags: the step's */
    void (*note_off)(uint32_t track, uint8_t note);
    void (*clock)(uint8_t status);                  /* 0xF8 a pulse, 0xFA start, 0xFC stop */
    void (*wrap)(uint32_t track);                   /* a track's pattern comes round again (before its step 1 plays):
                                                     * the app may switch its pattern here (dn_seq_track_new). May be 0 */
} dn_seq_cb_t;

typedef struct {
    dn_pattern_t pat;
    uint16_t bpm;
    uint8_t playing;
    uint32_t pos;                                   /* steps played since start (the step now: pos - 1) */
    int32_t until_step;                             /* samples to the next step, Q8 */
    int32_t until_clock;                            /* samples to the next clock pulse, Q8 */
    uint8_t pulses;                                 /* clock pulses sent in this step (6 a step) */
    int32_t gate[DN_SEQ_TRACKS];                    /* samples the note sounds on (0: none) */
    uint8_t sounding[DN_SEQ_TRACKS];                /* the note each track holds (0xFF: none) */
    uint8_t cur[DN_SEQ_TRACKS];                     /* each track's step now */
    uint8_t legato;                                 /* the tracks whose overlapping notes may slide (the app's:
                                                     * synth and MIDI tracks; not the drum tracks) */
    uint8_t ovl;                                    /* the tracks whose next note slides (the one before overlaps) */
    uint8_t tpos[DN_SEQ_TRACKS];                    /* each track's step to play next (its own length) */
    uint16_t pass[DN_SEQ_TRACKS];                   /* each track's passes through its pattern since it began */
    uint8_t pre[DN_SEQ_TRACKS];                     /* the last condition evaluated on each track: 1 true (PRE / NEI) */
    uint8_t had[DN_SEQ_TRACKS];                     /* ... and whether there was one */
    uint8_t fill;                                   /* FILL held (the app) */
    uint8_t lst;                                    /* the tracks on their last pass: a pattern change is queued */
    uint32_t rnd;                                   /* the chances' dice */
    /* retrigs, per track: Q8 samples to the next, its index and count, the note and its velocity curve */
    int32_t rt_left[DN_SEQ_TRACKS], rt_int[DN_SEQ_TRACKS];
    uint8_t rt_k[DN_SEQ_TRACKS], rt_n[DN_SEQ_TRACKS], rt_note[DN_SEQ_TRACKS], rt_vel[DN_SEQ_TRACKS];
    int8_t rt_curve[DN_SEQ_TRACKS];
    uint8_t rt_flags[DN_SEQ_TRACKS];
} dn_seq_t;

/* the pattern's pad[0]: OVERLAP CUT, one bit a track (a note reaching past the next one retriggers it, not slides) */
#define DN_PAT_CUT(p) ((p)->pad[0])

void dn_seq_init(dn_seq_t *s);
void dn_pattern_clear(dn_pattern_t *p);
void dn_pattern_demo(dn_pattern_t *p);              /* a first-boot pattern: a reese line on track 1 */
int dn_pattern_valid(const dn_pattern_t *p);        /* also clamps every value into range */
/* a pattern saved by 0.4 .. 0.10 (DNQ1 / DNQ2: 4-byte steps, as bytes) into the current form; 0: not one */
int dn_pattern_from_old(dn_pattern_t *p, const uint8_t *b, uint32_t n);
void dn_step_clear(dn_step_t *st);                  /* an off step with the defaults (note 53, 3/4 step, vel 100) */
/* a track starts its pattern again from step 1 (a new pattern switched in): its passes from 0 (1ST), no retrigs */
void dn_seq_track_new(dn_seq_t *s, uint32_t t);
/* the velocity of retrig k of n (k = 0: the step itself) along the curve */
uint8_t dn_rt_vel(uint32_t vel, int32_t curve, uint32_t k, uint32_t n);
/* which steps of track t sound, for the screen: for each step, 0 nothing, 1 a note starts, 2 a note started earlier
 * still sounds (its length reaches here) */
void dn_pattern_coverage(const dn_pattern_t *p, uint32_t t, uint8_t out[DN_SEQ_STEPS]);
void dn_seq_start(dn_seq_t *s, const dn_seq_cb_t *cb);
void dn_seq_stop(dn_seq_t *s, const dn_seq_cb_t *cb);
uint32_t dn_seq_next(const dn_seq_t *s);            /* samples until the next event (at least 1; large when stopped) */
void dn_seq_run(dn_seq_t *s, uint32_t n, const dn_seq_cb_t *cb);
/* samples a step takes at this tempo, Q8 (the even and odd ones differ with swing) */
int32_t dn_seq_step_q8(const dn_seq_t *s, uint32_t step);
