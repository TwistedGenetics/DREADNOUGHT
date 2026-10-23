/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: the REESE engine, standalone (no platform, no UI). Copyright (C) 2026 Jamie (TwistedGenetics).
 * How it's built and why: docs/REESE.md. */
#pragma once
#include <stdint.h>

#define RS_CTL 32u                          /* control rate: envelopes, glide, drift once every 32 samples */
#define RS_NOSC 7u
#define RS_COMB 256u

enum { RP_OSC, RP_DTN, RP_BEAT, RP_SUB, RP_CUT, RP_RES, RP_DRV, RP_NTCH,     /* the eight EDIT values */
       RP_ATK, RP_DEC, RP_SUS, RP_REL, RP_FENV, RP_GLIDE, RP_LRATE, RP_LFLT, /* the rest of the patch */
       RP_LEVEL, RP_TRANS, RP_COUNT };
enum { RO_2SAW, RO_3SAW, RO_5SAW, RO_7SAW, RO_SAWSQ, RO_2SIN, RO_N };

typedef struct {
    const char *name;
    int8_t v[RP_COUNT];
} reese_preset_t;

typedef struct {
    int16_t p[RP_COUNT];                    /* the patch (see reese.c for each value's range) */
    uint16_t bpm;                           /* the song tempo (BEAT) */
    /* the voice (mono) */
    uint8_t gate, note, active;
    int32_t pitch256, target256;            /* the note now (gliding) and where it goes, 1/256 semitone */
    int32_t aenv, fenv;                     /* Q24 */
    uint8_t astage, fstage;                 /* 0 off, 1 attack, 2 decay / sustain, 3 release */
    uint32_t ph[RS_NOSC], sub, lph;
    int32_t drift[RS_NOSC], dvel[RS_NOSC];
    int32_t lad[4], xo[4];
    int32_t rnd;
    uint32_t cph, cw;
    int16_t line[RS_COMB];
    uint32_t ctl;                           /* samples left until the next control tick */
    int32_t amp_now, amp_next;              /* the amplitude ramp over the control tick, Q15 */
} reese_t;

void reese_init(reese_t *r);
uint32_t reese_preset_count(void);
const char *reese_preset_name(uint32_t i);
void reese_load_preset(reese_t *r, uint32_t i);
void reese_note_on(reese_t *r, uint8_t note, uint8_t vel, int legato);
void reese_note_off(reese_t *r);
/* adds n frames of the voice to lr (interleaved stereo, Q15 scale; the same signal on both sides: mono below
 * the effects) */
void reese_render(reese_t *r, int32_t *lr, uint32_t n);
/* each value's name, its range and how it reads on the screen */
const char *reese_param_name(uint32_t k);
int32_t reese_param_min(uint32_t k);
int32_t reese_param_max(uint32_t k);
void reese_param_text(uint32_t k, int32_t v, char out[8]);
