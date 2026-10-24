/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: the shared voice pool (0.12). Copyright (C) 2026 Jamie (TwistedGenetics).
 *
 * The polyphonic engines (FM, JUNO, WAVE, the SAMPLER's keys) share one pool of voices. A note asks for a voice; the
 * pool gives a free one, or takes one back (a steal), which the engine then fades out over 2-5 ms before it starts
 * the new note. Each track has a voice limit. Who gives a voice up:
 *   1. the track is at its limit: its own oldest released voice, else its own oldest held one;
 *   2. else a free voice;
 *   3. else the oldest released voice of any track, else the oldest held one.
 * Only bookkeeping: the engines own the sound. */
#pragma once
#include <stdint.h>

#define DN_VP_MAX 16u
#define DN_VP_NONE 0xFFu

typedef struct {
    uint8_t owner;                                  /* the track (DN_VP_NONE: free) */
    uint8_t note;
    uint8_t held;                                   /* 1 key down, 0 released (sounding its release) */
    uint8_t pad;
    uint32_t age;                                   /* when it started (the pool's clock) */
} dn_voice_t;

typedef struct {
    dn_voice_t v[DN_VP_MAX];
    uint32_t n;                                     /* voices in use by this pool (<= DN_VP_MAX) */
    uint32_t clock;
    uint8_t limit[8];                               /* per track */
} dn_vpool_t;

void dn_vp_init(dn_vpool_t *p, uint32_t n);
/* a voice for owner's note: its index; *stolen 1 when it was sounding (fade it first) */
uint32_t dn_vp_alloc(dn_vpool_t *p, uint32_t owner, uint32_t note, int *stolen);
uint32_t dn_vp_release(dn_vpool_t *p, uint32_t owner, uint32_t note);   /* the voice released (DN_VP_NONE: none) */
void dn_vp_free(dn_vpool_t *p, uint32_t i);         /* its release has finished */
uint32_t dn_vp_count(const dn_vpool_t *p, uint32_t owner);   /* voices the owner has (held or released) */
