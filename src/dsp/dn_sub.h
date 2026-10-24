/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: SUB, an SH-101-style mono bass with a jungle "dirt" stage (0.13). Copyright (C) 2026 Jamie
 * (TwistedGenetics). How it's built: the comment at the top of dn_sub.c. */
#pragma once
#include <stdint.h>

#define SB_CTL 32u                                  /* control rate: the envelope, glide, PWM once every 32 samples */

enum { SP_SAW, SP_PULSE, SP_SUB, SP_NOISE,          /* OSC: the 101's mixer */
       SP_PW, SP_PWM, SP_PWMR, SP_SUBM,             /* OSC (FX): pulse width, its modulation, the sub's kind */
       SP_CUT, SP_RES, SP_ENV, SP_KEY,              /* FILTER */
       SP_DRIVE, SP_DTYPE, SP_CRUSH, SP_TONE,       /* DIRT (EDIT again) */
       SP_ATK, SP_DEC, SP_SUS, SP_REL,              /* AMP */
       SP_GLIDE, SP_GMODE, SP_ACC, SP_VCA,          /* MOD */
       SP_LEVEL, SP_TRANS, SP_SPLIT,                /* OUT */
       SP_COUNT };
enum { SB_SUB1, SB_SUB2, SB_SUB2P, SB_SUBN };       /* the sub: square an octave down, two, a 25 % pulse two down */
enum { SB_SOFT, SB_HARD, SB_FOLD, SB_RECT, SB_DN }; /* the drive's kinds */
enum { SB_GOFF, SB_GAUTO, SB_GON };                 /* portamento: off, legato notes only, always */

typedef struct {
    int16_t p[SP_COUNT];
    uint8_t gate, note, active, acc, vel, vsens;
    uint8_t esync;                                  /* 1: ATK DEC REL are note lengths at the tempo (0.13) */
    uint16_t bpm;
    int32_t pitch256, target256;
    int32_t env;                                    /* Q24: one envelope, for the filter and the amp (the 101's) */
    uint8_t stage;                                  /* 0 off, 1 attack, 2 decay / sustain, 3 release */
    int16_t fe[4];                                  /* the filter envelope (0.19): ATK DEC SUS REL, 0..127 (a preset:
                                                     * the amp's, so it moves as the one 101 envelope did) */
    int32_t fenv;                                   /* Q24 */
    uint8_t fstage;
    int32_t gamp;                                   /* GATE mode's amp, Q24 (declicked) */
    uint32_t ph, sph, pwph;                         /* the oscillator, the sub (half its rate), the PWM triangle */
    int32_t lad[4];
    int32_t rnd;
    uint32_t ctl;
    int32_t amp_now, amp_next;
    int32_t lo1, lo2;                               /* the clean-sub split's low band (two one-poles) */
    int32_t tone, hold, cr_ph;                      /* the TONE low-pass; CRUSH's sample-and-hold */
    int32_t dc;                                     /* (RECT: its DC taken out) */
    int32_t mod[SP_COUNT];                          /* the LFOs (the app), 1/256 of a step */
    int32_t mod_pitch;                              /* 1/256 semitone */
} dn_sub_t;

void dn_sub_init(dn_sub_t *s);
uint32_t dn_sub_preset_count(void);
const char *dn_sub_preset_name(uint32_t i);
void dn_sub_load_preset(dn_sub_t *s, uint32_t i);
void dn_sub_fe_default(dn_sub_t *s);               /* (0.19) the filter envelope = the amp's */
void dn_sub_note_on(dn_sub_t *s, uint8_t note, uint8_t vel, int legato, int accent);
void dn_sub_note_off(dn_sub_t *s);
void dn_sub_render(dn_sub_t *s, int32_t *lr, uint32_t n);   /* adds n stereo frames (mono on both sides) */
const char *dn_sub_param_name(uint32_t k);
int32_t dn_sub_param_min(uint32_t k);
int32_t dn_sub_param_max(uint32_t k);
void dn_sub_param_text(uint32_t k, int32_t v, char out[8]);
int32_t dn_sub_cut_hz(int32_t cut256);              /* (the tests: a cutoff value in Hz) */
