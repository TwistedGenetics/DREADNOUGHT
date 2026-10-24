/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: the LFOs (0.9). Copyright (C) 2026 Jamie (TwistedGenetics).
 *
 * Four LFOs, each a shape at a rate to one destination at a depth. Synced, the rate is a note length (8 bars ..
 * 1/32, dotted and triplet) and the phase is an exact fraction (a 32-bit counter: samples x BPM x den, wrapping at
 * 2646000 x num), so it never drifts from the sequencer's clock; free, it's 0.05 .. 20 Hz (the old LFO's table).
 * DRAW is a shape of 16 or 32 points, SMOOTH from steps to slopes. The app runs them every 32 samples (dn_app.c) and
 * adds them to REESE's and the breaks' values. */
#pragma once
#include <stdint.h>

#define DN_NLFO 4u
#define DN_LFO_PTS 32u
#define DN_LFO_NDIV 16u
enum { DN_LS_SINE, DN_LS_TRI, DN_LS_SAWUP, DN_LS_SAWDN, DN_LS_SQUARE, DN_LS_SH, DN_LS_SMOOTH, DN_LS_DRAW, DN_LS_N };
enum { DN_LM_FREE, DN_LM_RETRIG, DN_LM_ONESHOT, DN_LM_N };

typedef struct {
    uint8_t shape;                                  /* DN_LS_* */
    uint8_t sync;                                   /* 1: rate is a note length (DN_LFO_DIV); 0: Hz (0..127) */
    uint8_t rate;
    uint8_t dest;                                   /* the app's (dn_app.c DN_LD_*); 0 none */
    int8_t depth;                                   /* -64..63 */
    uint8_t phase;                                  /* 0..127: where a cycle starts (and RETRIG restarts) */
    uint8_t mode;                                   /* DN_LM_* */
    uint8_t smooth;                                 /* DRAW: 0 steps .. 127 slopes all the way */
    uint8_t npts;                                   /* DRAW: 16 or 32 points a cycle */
    uint8_t pad[3];
    int8_t pts[DN_LFO_PTS];                         /* -100..100 */
} dn_lfo_p_t;

typedef struct {
    uint32_t acc;                                   /* synced: the exact phase counter */
    uint32_t ph;                                    /* the phase, 2^32 a cycle */
    int32_t out;                                    /* the value now, -32767..32767 */
    int32_t sh_a, sh_b;                             /* S&H / SMOOTH: this cycle's value, the next one's */
    int32_t rnd;
    uint8_t done;                                   /* ONE-SHOT: the cycle has run */
} dn_lfo_s_t;

extern const char *const DN_LFO_SHAPE_NAME[DN_LS_N];
extern const char *const DN_LFO_MODE_NAME[DN_LM_N];
extern const char *const DN_LFO_DIV_NAME[DN_LFO_NDIV];
extern const uint8_t DN_LFO_DIV[DN_LFO_NDIV][2];   /* beats: num / den */

void dn_lfo_default(dn_lfo_p_t *p);
void dn_lfo_reset(dn_lfo_s_t *s, int32_t seed);   /* the cycle from its start (RETRIG, the sequencer starting) */
void dn_lfo_run(dn_lfo_s_t *s, const dn_lfo_p_t *p, uint32_t n, uint32_t bpm);   /* n samples on; s->out the value */
int32_t dn_lfo_at(const dn_lfo_p_t *p, uint32_t ph, int32_t a, int32_t b);       /* the shape at a phase */
void dn_lfo_draw_shape(dn_lfo_p_t *p, uint32_t shape);   /* fills DRAW's points with a shape (DN_LS_*) */
