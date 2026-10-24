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
#define DN_SEQ_MAGIC 0x32514E44u                    /* "DNQ2": lengths in quarter steps (0.6) */
#define DN_SEQ_MAGIC_V1 0x31514E44u                 /* "DNQ1": 0.4 / 0.5, lengths in sixteenths of a step */
#define DN_LEN_STEP 4u                              /* length ticks a step */
#define DN_LEN_MAX 255u                             /* 63 3/4 steps */

enum { DN_ST_ON = 1u, DN_ST_SLIDE = 2u, DN_ST_ACC = 4u };

typedef struct {
    uint8_t note;                                   /* MIDI note */
    uint8_t flags;                                  /* DN_ST_* */
    uint8_t len;                                    /* how long the note lasts, in quarter steps: 1 (a 64th note)
                                                     * .. 255 (~64 steps); 4 = one step. It holds across the steps
                                                     * after it (and the pattern's end) until it runs out or a later
                                                     * step on the track cuts it (or slides from it) */
    uint8_t vel;                                    /* 1..127 */
} dn_step_t;

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
} dn_seq_t;

void dn_seq_init(dn_seq_t *s);
void dn_pattern_clear(dn_pattern_t *p);
void dn_pattern_demo(dn_pattern_t *p);              /* a first-boot pattern: a reese line on track 1 */
int dn_pattern_valid(const dn_pattern_t *p);        /* also clamps every value into range, and brings a 0.4 / 0.5
                                                     * pattern (DNQ1) up to date */
/* which steps of track t sound, for the screen: for each step, 0 nothing, 1 a note starts, 2 a note started earlier
 * still sounds (its length reaches here) */
void dn_pattern_coverage(const dn_pattern_t *p, uint32_t t, uint8_t out[DN_SEQ_STEPS]);
void dn_seq_start(dn_seq_t *s, const dn_seq_cb_t *cb);
void dn_seq_stop(dn_seq_t *s, const dn_seq_cb_t *cb);
uint32_t dn_seq_next(const dn_seq_t *s);            /* samples until the next event (at least 1; large when stopped) */
void dn_seq_run(dn_seq_t *s, uint32_t n, const dn_seq_cb_t *cb);
/* samples a step takes at this tempo, Q8 (the even and odd ones differ with swing) */
int32_t dn_seq_step_q8(const dn_seq_t *s, uint32_t step);
