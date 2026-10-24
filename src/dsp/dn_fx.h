/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: the FX rack's effects (0.12). Copyright (C) 2026 Jamie (TwistedGenetics).
 *
 * Two send slots (A, B). Each holds one effect, fed the tracks' sends, its output added to the mix at its return
 * level. Their memory comes from one arena the app owns (dn_app.c), laid out whenever an effect changes: an effect
 * whose memory doesn't fit is refused. 0.12 has one effect, a tempo-synced ping-pong DELAY; Part 3 brings the
 * reverbs.
 *
 * DELAY: two lines (left, right) at half the sample rate (a darker, dub-like repeat, half the memory: 1.1 s), the
 * input into the left line, the left's output into the right, the right's back into the left at FEEDBACK (100 =
 * unity; above it builds up, soft-clipped so it howls rather than wraps), a low-pass in the loop (TONE). A new TIME
 * glides there (tape-like), never clicks. */
#pragma once
#include <stdint.h>

enum { DN_FX_OFF, DN_FX_DELAY, DN_FX_N };
#define DN_FX_NDIV 12u

typedef struct {
    uint8_t type;                                   /* DN_FX_* */
    uint8_t p[3];                                   /* DELAY: TIME (DN_FX_DIV index), FEEDBACK 0..127, TONE 0..127 */
} dn_fx_p_t;

typedef struct {
    int16_t *l, *r;                                 /* the lines (the arena's), len samples each, half rate */
    uint32_t len, w;
    uint32_t d_q8;                                  /* the delay now, half-rate samples << 8 (glides to the target) */
    int32_t lp_l, lp_r;                             /* the loop's low-pass */
    int32_t in_acc;                                 /* the first of each pair of input samples */
    int32_t y_l, y_r;                               /* the lines' outputs (held for the pair) */
    int32_t o_l, o_r;                               /* the output's smoothing */
    uint8_t ph, type;
} dn_fx_s_t;

extern const char *const DN_FX_NAME[DN_FX_N];
extern const char *const DN_FX_DIV_NAME[DN_FX_NDIV];
extern const uint8_t DN_FX_DIV[DN_FX_NDIV][2];      /* beats: num / den */

uint32_t dn_fx_ram(uint32_t type);                  /* bytes an effect needs from the arena */
void dn_fx_default(dn_fx_p_t *p, uint32_t type);
void dn_fx_init(dn_fx_s_t *s, uint32_t type, void *mem);   /* mem: dn_fx_ram(type) bytes, 4-aligned (cleared here) */
/* n frames: in the send (stereo, Q15-ish int32), out added to (stereo), at return level ret (100 = unity) */
void dn_fx_run(dn_fx_s_t *s, const dn_fx_p_t *p, const int32_t *in, int32_t *out, uint32_t n, uint32_t bpm,
               uint32_t ret);
uint32_t dn_fx_delay_samples(const dn_fx_p_t *p, uint32_t bpm);   /* DELAY's time, full-rate samples (clamped) */
