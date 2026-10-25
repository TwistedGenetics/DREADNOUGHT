/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: WAVE (0.32; 0.33: WAVE 2), a wavetable synth, standalone (no platform, no UI). Copyright (C) 2026 Jamie
 * (TwistedGenetics).
 *
 * A table (tools/wt_convert.py makes them; the factory's are in the firmware, the player's in the sample slots) is a
 * 16-byte header - "DNWT", version 1, frames (1..64), log2 of the frame size (9), mips (7), its name (8 bytes) - then
 * each frame as 7 versions band-limited and 4x longer than their top harmonic (Carnac OS's layout): 512 samples
 * (harmonics 1-127), 256 (1-63), 128, 64, 32, 16, 8 (1), int16 little-endian, 2032 bytes a frame. A voice reads the
 * version that can't alias at its pitch (and its warp's speed), straight from flash (no RAM), and morphs between two
 * frames.
 *
 * A voice: oscillator A (its table, POS through it, a WARP of its phase: SYNC, BEND, MIRROR, PWM, FM by a sine,
 * CRUSH), up to 4 in unison (DETUNE, SPREAD), a sine an octave down (SUB) and NOISE. From 0.33 oscillator B too (OSC
 * B ON: its own table, POS, WARP, octave, semitones, cents), heard at B LEVEL and working on A by B MODE: FM (B moves
 * A's phase), RING (A x B), AM, SYNC (A restarts with B's cycle, A's pitch B's plus MOD AMT). B on doubles the
 * oscillators, so it halves the voices (8 oscillators in all). Then DRIVE (SOFT HARD FOLD RECT TUBE BITS RATE) into a
 * filter (LP12 LP24 HP BP NOTCH COMB, KEY tracking), SAT after it. Three envelopes (AMP, MOD, ENV 3), three LFOs (SINE
 * TRI SAW SQUARE S&H RANDOM, free or synced to the tempo, FREE or RETRIG), and a matrix of 8: a source to a
 * destination by an amount. Control every 32 samples, levels and positions ramped a sample at a time. */
#pragma once
#include <stdint.h>

#define DN_WT_NV 6u                                 /* voices */
#define DN_WT_NU 4u                                 /* oscillators a voice (unison) */
#define DN_WT_NOSC 8u                               /* oscillators in all: voices x unison (x2 with B) */
#define DN_WT_HDR 16u
#define DN_WT_FRAME_BYTES 2032u                     /* a frame: 512 + 256 + ... + 8 samples, int16 */
#define DN_WT_MAXF 64u
#define DN_WT_NLFO 3u
#define DN_WT_NMOD 8u                               /* the matrix's slots */
#define DN_WT_COMB 256u                             /* the comb's delay (samples, a power of 2) */

/* the values (the app's pages; ranges in dn_wt_vmin / dn_wt_vmax). 0..27 as 0.32 had them (saves keep their places);
 * 0.33's after. LRATE is LFO 1's rate, PLFO and CLFO LFO 1 on the position and the cutoff. */
enum { WP_TABLE, WP_POS, WP_WARP, WP_WAMT, WP_UNI, WP_DET, WP_SPREAD, WP_SUB, WP_CUT, WP_RES, WP_FENV, WP_DRIVE,
       WP_ATK, WP_DEC, WP_SUS, WP_REL, WP_MATK, WP_MDEC, WP_MSUS, WP_MREL, WP_PENV, WP_PLFO, WP_LRATE, WP_CLFO,
       WP_LEVEL, WP_VOICES, WP_GLIDE, WP_TRANS,
       WP_N032,                                     /* (0.32's count) */
       WP_OSCB = WP_N032, WP_BTAB, WP_BPOS, WP_BWARP, WP_BWAMT, WP_BOCT, WP_BSEMI, WP_BFINE, WP_BLEVEL, WP_BMODE,
       WP_BAMT, WP_NOISE, WP_FTYPE, WP_FKEY, WP_DTYPE, WP_SAT,
       WP_L1SHAPE, WP_L1SYNC, WP_L1MODE, WP_L2SHAPE, WP_L2RATE, WP_L2SYNC, WP_L2MODE, WP_L3SHAPE, WP_L3RATE, WP_L3SYNC,
       WP_L3MODE, WP_E3ATK, WP_E3DEC, WP_E3SUS, WP_E3REL,
       WP_M0,                                       /* the matrix: slot k's SRC, DST, AMT at WP_M0 + 3k */
       WP_N = WP_M0 + 3 * DN_WT_NMOD };
enum { WW_OFF, WW_SYNC, WW_BEND, WW_MIRROR, WW_PWM, WW_FM, WW_CRUSH, WW_N };
enum { WB_MIX, WB_FM, WB_RING, WB_AM, WB_SYNC, WB_N };   /* B MODE (MIX: B only heard) */
enum { WF_LP12, WF_LP24, WF_HP, WF_BP, WF_NOTCH, WF_COMB, WF_N };
enum { WD_SOFT, WD_HARD, WD_FOLD, WD_RECT, WD_TUBE, WD_BITS, WD_RATE, WD_N };
enum { WL_SINE, WL_TRI, WL_SAW, WL_SQUARE, WL_SH, WL_RANDOM, WL_N };
enum { WS_OFF, WS_LFO1, WS_LFO2, WS_LFO3, WS_MENV, WS_ENV3, WS_AENV, WS_VEL, WS_KEY, WS_RAND, WS_N };
enum { WT_OFF, WT_POSA, WT_POSB, WT_WARPA, WT_WARPB, WT_PITCH, WT_PITCHB, WT_BMOD, WT_BLEVEL, WT_CUT, WT_RES,
       WT_DRIVE, WT_LEVEL, WT_PAN, WT_SUB, WT_NOISE, WT_L1RATE, WT_L2RATE, WT_DETUNE, WT_N };
#define DN_WT_NDIV 16u                              /* a synced LFO's lengths */

typedef struct {
    float lv;                                       /* 0..1 */
    uint8_t st;                                     /* 0 attack, 1 decay / sustain, 2 release, 3 done */
} dn_wt_env_t;

typedef struct {
    uint8_t live, gate, note, vel;
    uint32_t seq;
    float pitch, target;                            /* semitones (the note + TRANS), gliding */
    uint32_t ph[DN_WT_NU], inc[DN_WT_NU], phb[DN_WT_NU], incb[DN_WT_NU], sph, sinc;
    uint8_t mip[DN_WT_NU], mipb[DN_WT_NU];
    dn_wt_env_t amp, mod, e3;
    uint32_t lph[DN_WT_NLFO];                       /* the LFOs (RETRIG: each voice its own) */
    float lsh[DN_WT_NLFO], lsh0[DN_WT_NLFO];        /* (S&H, RANDOM: the value now, the one before) */
    float rnd;                                      /* (RANDOM: one a note, -1..1) */
    float lrm[2];                                   /* (LFO 1, 2 RETRIG: their rates moved by the matrix) */
    uint32_t nz;                                    /* (the noise's state) */
    float ic1[2], ic2[2], jc1[2], jc2[2];           /* the filter's state, L and R (LP24: a second stage) */
    float volp, posp, bposp, f1p, f2p, f3p;         /* last tick's level, positions, filter: ramped from */
    float glp, grp;                                 /* (the pan, last tick) */
    float hold[2], hph;                             /* (DRIVE RATE: the sample held, how far to the next) */
    float dx[2];                                    /* (DRIVE: the last input, for 2x oversampling) */
    float dcx[2], dcy[2];                           /* (RECT, TUBE: the DC they make, taken out) */
    float sblep;                                    /* (SYNC: the step to finish on the next sample) */
    uint8_t cw;                                     /* (COMB: where the delay writes) */
    int16_t comb[2][DN_WT_COMB];
} dn_wt_v_t;

typedef struct {
    int16_t p[WP_N];
    const uint8_t *tab, *tabb;                      /* A's and B's tables (checked: dn_wt_valid) */
    dn_wt_v_t v[DN_WT_NV];
    uint32_t seq, tick;
    uint32_t lph[DN_WT_NLFO];                       /* the FREE LFOs' phases (all the voices') */
    float lsh[DN_WT_NLFO], lsh0[DN_WT_NLFO];
    uint32_t rng;
    uint16_t bpm;                                   /* (the app's tempo: the synced LFOs) */
    uint8_t nheld;
    uint32_t busy;                                  /* (voices rendered in the last tick: the CPU figure) */
} dn_wt_t;

void dn_wt_init(dn_wt_t *s);
void dn_wt_defaults(int16_t *p);                    /* INIT */
void dn_wt_upgrade(int16_t *p, uint32_t n);         /* a sound of n values (0.32: 28): the rest as INIT has them */
uint32_t dn_wt_preset_count(void);                  /* the factory sounds: INIT first */
const char *dn_wt_preset_name(uint32_t i);
void dn_wt_preset(uint32_t i, int16_t *p);          /* (TABLE, B TABLE: factory tables' numbers) */
int dn_wt_valid(const uint8_t *t, uint32_t len);    /* a table, whole: its frames; 0 not one */
uint32_t dn_wt_frames(const uint8_t *t);
void dn_wt_name(const uint8_t *t, char out[9]);
void dn_wt_set_table(dn_wt_t *s, const uint8_t *t); /* (main loop, inside the app's lock) */
void dn_wt_set_table_b(dn_wt_t *s, const uint8_t *t);
uint32_t dn_wt_voices(const dn_wt_t *s);           /* the voices notes can take now (VOICES, within the CPU's budget) */
uint32_t dn_wt_cost(const int16_t *p);             /* a voice's cost (the budget: 156) */
void dn_wt_note_on(dn_wt_t *s, uint8_t note, uint8_t vel);
void dn_wt_note_off(dn_wt_t *s, uint8_t note);
void dn_wt_all_off(dn_wt_t *s);                     /* release every voice */
void dn_wt_cut(dn_wt_t *s);                         /* silence now */
void dn_wt_render(dn_wt_t *s, int32_t *lr, uint32_t n);   /* adds, stereo */
uint32_t dn_wt_sounding(const dn_wt_t *s);

/* the values: name, range, text (TABLE, B TABLE: the app's count and names) */
const char *dn_wt_vname(uint32_t k);
int32_t dn_wt_vmin(uint32_t k);
int32_t dn_wt_vmax(uint32_t k);
void dn_wt_vtext(uint32_t k, int32_t v, const int16_t *p, char out[8]);   /* (p: the sound, for an LFO's RATE: synced or not) */
int dn_wt_is_rate(uint32_t k);                      /* an LFO's RATE: 0 no, 1..3 which LFO */

/* one sample of a frame's version, as the engine reads it (tests): frame f, mip m, position q (0..2^32) */
int32_t dn_wt_peek(const uint8_t *t, uint32_t f, uint32_t m, uint32_t q);
