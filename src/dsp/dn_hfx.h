/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: a hit's own effect (0.39), for the breaks and SAMPLER's pads: a step can carry one (its flags' top 4
 * bits on those tracks, where no chord goes). Copyright (C) 2026 Jamie (TwistedGenetics).
 *
 *   STR 1/2, STR 1/4   the Akai way of stretching (the S950 / S1000's cyclic time-stretch): the hit is played in
 *                      short cycles (a 32nd note each), each from where the time has got to, the time moving at half /
 *                      a quarter speed: the same pitch, longer, with the cycles' grainy, metallic warble
 *   STUT32, STUT64     the first 32nd / 64th of the hit over and over for a beat (the time standing still), then the
 *                      hit plays on
 *   TAPESTOP           the hit slowing to a stop over a beat, pitch and all
 *   OCT-, OCT+         an octave down / up (half / double speed)
 *
 * The engine reads its sample forward at `inc` (Q16 source samples an output sample): it asks dn_hfx_inc for the rate
 * to read at and, every output sample, dn_hfx_tick whether a cycle begins (then it jumps to dn_hfx_at and declicks). */
#pragma once
#include <stdint.h>

enum { DN_HFX_OFF, DN_HFX_STR2, DN_HFX_STR4, DN_HFX_STUT32, DN_HFX_STUT64, DN_HFX_TAPE, DN_HFX_OCTDN, DN_HFX_OCTUP,
       DN_HFX_N };
static const char *const DN_HFX_NAME[DN_HFX_N] = {"OFF", "STR 1/2", "STR 1/4", "STUT32", "STUT64", "TAPESTP", "OCT-",
                                                  "OCT+"};

typedef struct {
    uint8_t type;
    uint32_t g, gn;                                 /* a cycle's length, output samples; how far into this one */
    uint32_t t16;                                   /* the time: source samples since the hit began, Q16 */
    uint32_t s16;                                   /* how fast the time moves, Q16 (65536: as played) */
    uint32_t r16, dr16;                             /* the rate (Q16, 65536: as played), and its fall a sample */
    uint32_t start;                                 /* the source sample the hit began on */
    uint16_t left;                                  /* cycles still to go (0: no end) */
} dn_hfx_t;

/* (the app, before the hit) type at the tempo (bpm), for an engine at fs Hz */
static inline void dn_hfx_setup(dn_hfx_t *h, uint32_t type, uint32_t bpm, uint32_t fs)
{
    uint32_t beat = fs * 60u / (bpm ? bpm : 120u);  /* output samples a beat */
    h->type = (uint8_t)(type < DN_HFX_N ? type : 0u);
    h->gn = 0, h->t16 = 0, h->dr16 = 0, h->left = 0;
    h->g = beat / 8u;                               /* (a 32nd note) */
    h->s16 = 65536u, h->r16 = 65536u;
    switch (h->type) {
    case DN_HFX_STR2: h->s16 = 32768u; break;
    case DN_HFX_STR4: h->s16 = 16384u; break;
    case DN_HFX_STUT32: h->s16 = 0u, h->left = 8u; break;
    case DN_HFX_STUT64: h->s16 = 0u, h->g = beat / 16u, h->left = 16u; break;
    case DN_HFX_TAPE: h->dr16 = 65536u / (beat ? beat : 1u) + 1u; break;
    case DN_HFX_OCTDN: h->r16 = 32768u; break;
    case DN_HFX_OCTUP: h->r16 = 131072u; break;
    default: break;
    }
    if (h->g < 64u)
        h->g = 64u;
}
static inline int dn_hfx_cycles(const dn_hfx_t *h) { return h->type >= DN_HFX_STR2 && h->type <= DN_HFX_STUT64; }
/* the rate to read at now (Q16 source samples an output sample) */
static inline uint32_t dn_hfx_inc(const dn_hfx_t *h, uint32_t inc)
{
    return h->type == DN_HFX_OFF || h->r16 == 65536u ? inc : (uint32_t)(((uint64_t)inc * h->r16) >> 16);
}
/* an output sample on: 1 when a new cycle begins (jump to dn_hfx_at); TAPESTOP: 2 when it has stopped */
static inline int dn_hfx_tick(dn_hfx_t *h, uint32_t inc)
{
    if (h->dr16) {
        h->r16 = h->r16 > h->dr16 ? h->r16 - h->dr16 : 0u;
        return h->r16 ? 0 : 2;
    }
    if (!dn_hfx_cycles(h))
        return 0;
    h->t16 += (uint32_t)(((uint64_t)inc * h->s16) >> 16);
    if (++h->gn >= h->g) {
        h->gn = 0;
        if (h->left && !--h->left) {
            h->type = DN_HFX_OFF;                   /* (the stutter's beat is up: the hit plays on from here) */
            return 0;
        }
        return 1;
    }
    return 0;
}
static inline uint32_t dn_hfx_at(const dn_hfx_t *h) { return h->start + (h->t16 >> 16); }
