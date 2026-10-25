/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: WAVE (0.32), a wavetable synth, standalone (no platform, no UI). Copyright (C) 2026 Jamie
 * (TwistedGenetics).
 *
 * A table (tools/wt_convert.py makes them; the factory's are in the firmware, the player's in the sample slots) is a
 * 16-byte header - "DNWT", version 1, frames (1..64), log2 of the frame size (9), mips (7), its name (8 bytes) - then
 * each frame as 7 versions band-limited and 4x longer than their top harmonic (Carnac OS's layout): 512 samples
 * (harmonics 1-127), 256 (1-63), 128, 64, 32, 16, 8 (1), int16 little-endian, 2032 bytes a frame. A voice reads the version that can't alias at its pitch, straight from
 * flash (no RAM), and morphs between two frames.
 *
 * Up to 6 voices (VOICES: 1 = mono, legato with GLIDE), each up to 4 oscillators in unison (DETUNE, SPREAD across the
 * stereo field) and a sine an octave down (SUB). The phase can be warped before the table is read: SYNC, BEND,
 * MIRROR, PWM, FM (by a sine at the note), CRUSH (fewer samples and bits). Then a 12 dB state-variable low-pass with
 * DRIVE before it, an amp envelope (ADSR) and a mod envelope (ADSR) for the cutoff (FENV) and the position (PENV);
 * an LFO (sine, RATE) on the position (PLFO) and the cutoff (CLFO). Control every 32 samples. */
#pragma once
#include <stdint.h>

#define DN_WT_NV 6u                                 /* voices */
#define DN_WT_NU 4u                                 /* oscillators a voice (unison) */
#define DN_WT_NOSC 8u                               /* oscillators in all: voices x unison (the CPU's budget) */
#define DN_WT_HDR 16u
#define DN_WT_FRAME_BYTES 2032u                     /* a frame: 512 + 256 + ... + 8 samples, int16 */
#define DN_WT_MAXF 64u

/* the values (the app's pages; ranges in dn_wt_vmin / dn_wt_vmax) */
enum { WP_TABLE, WP_POS, WP_WARP, WP_WAMT, WP_UNI, WP_DET, WP_SPREAD, WP_SUB, WP_CUT, WP_RES, WP_FENV, WP_DRIVE,
       WP_ATK, WP_DEC, WP_SUS, WP_REL, WP_MATK, WP_MDEC, WP_MSUS, WP_MREL, WP_PENV, WP_PLFO, WP_LRATE, WP_CLFO,
       WP_LEVEL, WP_VOICES, WP_GLIDE, WP_TRANS, WP_N };
enum { WW_OFF, WW_SYNC, WW_BEND, WW_MIRROR, WW_PWM, WW_FM, WW_CRUSH, WW_N };

typedef struct {
    float lv;                                       /* 0..1 */
    uint8_t st;                                     /* 0 attack, 1 decay / sustain, 2 release, 3 done */
} dn_wt_env_t;

typedef struct {
    uint8_t live, gate, note, vel;
    uint32_t seq;
    float pitch, target;                            /* semitones (the note + TRANS), gliding */
    uint32_t ph[DN_WT_NU], inc[DN_WT_NU], sph, sinc;
    uint8_t mip[DN_WT_NU];
    dn_wt_env_t amp, mod;
    float ic1[2], ic2[2];                           /* the filter's state, L and R */
    float volp, posp, f1p, f2p, f3p;                /* (0.32.1) last tick's level, position, filter: ramped from, a sample
                                                     * at a time (stepping them every 32 samples buzzed) */
} dn_wt_v_t;

typedef struct {
    int16_t p[WP_N];
    const uint8_t *tab;                             /* the table playing (checked: dn_wt_valid) */
    dn_wt_v_t v[DN_WT_NV];
    uint32_t seq, lfo_ph, tick;
    float lfo;                                      /* -1..1 */
    uint8_t nheld;
    uint32_t busy;                                  /* (voices rendered in the last tick: the CPU figure) */
} dn_wt_t;

void dn_wt_init(dn_wt_t *s);
void dn_wt_defaults(int16_t *p);                    /* INIT */
uint32_t dn_wt_preset_count(void);                  /* the factory sounds: INIT first */
const char *dn_wt_preset_name(uint32_t i);
void dn_wt_preset(uint32_t i, int16_t *p);          /* (TABLE: a factory table's number) */
int dn_wt_valid(const uint8_t *t, uint32_t len);    /* a table, whole: its frames; 0 not one */
uint32_t dn_wt_frames(const uint8_t *t);
void dn_wt_name(const uint8_t *t, char out[9]);
void dn_wt_set_table(dn_wt_t *s, const uint8_t *t); /* (main loop, inside the app's lock) */
uint32_t dn_wt_voices(const dn_wt_t *s);           /* the voices notes can take now (VOICES, within DN_WT_NOSC) */
void dn_wt_note_on(dn_wt_t *s, uint8_t note, uint8_t vel);
void dn_wt_note_off(dn_wt_t *s, uint8_t note);
void dn_wt_all_off(dn_wt_t *s);                     /* release every voice */
void dn_wt_cut(dn_wt_t *s);                         /* silence now */
void dn_wt_render(dn_wt_t *s, int32_t *lr, uint32_t n);   /* adds, stereo */
uint32_t dn_wt_sounding(const dn_wt_t *s);

/* the values: name, range, text */
const char *dn_wt_vname(uint32_t k);
int32_t dn_wt_vmin(uint32_t k);
int32_t dn_wt_vmax(uint32_t k);                     /* (TABLE: the app's count) */
void dn_wt_vtext(uint32_t k, int32_t v, char out[8]);

/* one sample of a frame's version, as the engine reads it (tests): frame f, mip m, position q (0..2^32) */
int32_t dn_wt_peek(const uint8_t *t, uint32_t f, uint32_t m, uint32_t q);
