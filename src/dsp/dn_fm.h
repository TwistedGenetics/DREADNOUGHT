/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: FM (0.25), a six-operator DX7-style synth, standalone (no platform, no UI). Copyright (C) 2026 Jamie
 * (TwistedGenetics).
 *
 * The engine is a C port of the core of Dexed's MSFA ("Music Synthesizer for Android", Copyright 2012 Google Inc.,
 * 2017 Pascal Gauthier and others, Apache License 2.0; see docs/UPSTREAM.md): its operators and 32 algorithms, the
 * DX7 envelopes (4 rates, 4 levels) and their timings, keyboard level and rate scaling, velocity, the LFO (with PMD /
 * AMD and its delay), the pitch envelope and operator feedback. Its tables are made at build time (tools/
 * gen_dn_tables.py), bit for bit as MSFA makes them at start-up; tests/host/fm_test.c renders voices through both and
 * compares them.
 *
 * A voice is the DX7's 155 values (the "unpacked" voice: OP6 first, 21 values an operator, then the pitch envelope,
 * ALGORITHM ... and the name), plus a 156th, the operators on (bit 0 OP6 .. bit 5 OP1). A DX7 bank keeps them packed
 * in 128 bytes (dn_fm_unpack / dn_fm_pack).
 *
 * Up to 6 voices (VOICES: fewer, or 1 for mono); the one free or quietest is taken, as Dexed does. A voice stops when
 * its carriers have gone silent after the key's up. Blocks of 64 samples: a note starts at the next block (up to
 * 1.5 ms later). */
#pragma once
#include <stdint.h>

#define DN_FM_NV 6u                                 /* voices */
#define DN_FM_N 64u                                 /* samples a block */
#define DN_FM_PATCH 156u                            /* an unpacked voice */
#define DN_FM_PACKED 128u

/* the unpacked voice's values: operator k (0 = OP6 ... 5 = OP1) at 21 k + DN_FMO_*, then the voice's */
enum { DN_FMO_R1, DN_FMO_R2, DN_FMO_R3, DN_FMO_R4, DN_FMO_L1, DN_FMO_L2, DN_FMO_L3, DN_FMO_L4, DN_FMO_BP, DN_FMO_LD,
       DN_FMO_RD, DN_FMO_LC, DN_FMO_RC, DN_FMO_RS, DN_FMO_AMS, DN_FMO_KVS, DN_FMO_OL, DN_FMO_MODE, DN_FMO_COARSE,
       DN_FMO_FINE, DN_FMO_DET, DN_FMO_N };
enum { DN_FMV_PR1 = 126, DN_FMV_PL1 = 130, DN_FMV_ALG = 134, DN_FMV_FB, DN_FMV_OKS, DN_FMV_LFS, DN_FMV_LFD,
       DN_FMV_LPMD, DN_FMV_LAMD, DN_FMV_LFKS, DN_FMV_LFW, DN_FMV_LPMS, DN_FMV_TRNSP, DN_FMV_NAME, DN_FMV_OPSW = 155 };

typedef struct {
    int32_t level, target, inc, statics;
    uint8_t rates[4], levels[4];
    int16_t outlevel;
    uint8_t rscale, ix, rising, down;
} dn_fm_env_t;

typedef struct {
    int32_t level, target, inc;
    uint8_t rates[4], levels[4];
    uint8_t ix, rising, down, pad;
} dn_fm_penv_t;

typedef struct {
    int32_t level_in, gain_out, freq, phase;
} dn_fm_op_t;

typedef struct {
    uint8_t live, key, note, vel;
    uint32_t seq;
    dn_fm_env_t env[6];
    dn_fm_op_t op[6];
    dn_fm_penv_t penv;
    int32_t base[6];
    int32_t fb[2];
    uint32_t amsens[6];
    uint8_t mode[6];
    uint8_t alg, fbshift, opsw, pms;
    int32_t pmd, amd;
} dn_fm_v_t;

typedef struct {
    uint8_t patch[DN_FM_PATCH];                     /* the voice the next notes play (the app's, macros applied) */
    uint8_t voices;                                 /* 1..6 (1: mono) */
    int16_t level;                                  /* 0..127 */
    dn_fm_v_t v[DN_FM_NV];
    uint32_t seq, lfo_ph, lfo_delta, lfo_dstate, lfo_dinc, lfo_dinc2;
    uint8_t lfo_wave, lfo_sync, lfo_rand, nheld;
    int32_t bus[2][DN_FM_N];
    int16_t out[DN_FM_N];                           /* the block made, being played out */
    uint32_t pos;
    uint32_t busy;                                  /* (the voices that rendered in the last block: the CPU figure) */
} dn_fm_t;

void dn_fm_init(dn_fm_t *s);
void dn_fm_patch_init(uint8_t *patch);              /* the DX7's INIT VOICE */
void dn_fm_unpack(const uint8_t *packed, uint8_t *patch);   /* 128 -> 156 (values clamped to their ranges) */
void dn_fm_pack(const uint8_t *patch, uint8_t *packed);     /* 156 -> 128 */
void dn_fm_voice_load(dn_fm_t *s, const uint8_t *patch);    /* a new voice: the LFO set, notes playing go on */
void dn_fm_note_on(dn_fm_t *s, uint32_t note, uint32_t vel);
void dn_fm_note_off(dn_fm_t *s, uint32_t note);
void dn_fm_all_off(dn_fm_t *s);                     /* every key up (released) */
void dn_fm_cut(dn_fm_t *s);                         /* every voice out at once */
void dn_fm_render(dn_fm_t *s, int32_t *lr, uint32_t n);   /* adds, stereo */
uint32_t dn_fm_sounding(const dn_fm_t *s);

/* the values: names, tops, text (k: 0..155, an operator's at 21 op + DN_FMO_*) */
const char *dn_fm_vname(uint32_t k);
int32_t dn_fm_vmax(uint32_t k);
void dn_fm_vtext(uint32_t k, int32_t v, char out[8]);
uint32_t dn_fm_is_carrier(uint32_t alg, uint32_t op);   /* op 0 = OP6 */

/* the factory voices (DREADNOUGHT's own, no Yamaha ROM sounds) */
uint32_t dn_fm_factory_count(void);
const char *dn_fm_factory_name(uint32_t i);
void dn_fm_factory(uint32_t i, uint8_t *patch);

/* the macros, from a voice into the voice played: BRIGHT moves the modulators' levels (-50..+50), TIME the envelope
 * rates (-50 slower .. +50 faster), ATTACK / RELEASE the carriers' R1 / R4 (-50..+50) */
typedef struct { int8_t bright, time, attack, release; } dn_fm_macro_t;
void dn_fm_apply(const uint8_t *base, const dn_fm_macro_t *m, uint8_t *out);
