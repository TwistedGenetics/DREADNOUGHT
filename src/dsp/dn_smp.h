/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: SAMPLER (0.23), one-shots and key-mapped samples, standalone (no platform, no UI). Copyright (C) 2026
 * Jamie (TwistedGenetics).
 *
 * The samples are the project's: the same store BREAKS plays from (the FM-1's sample slots, IMA ADPCM, mono, each at
 * its own rate, and the built-in loops). The app resolves which sample each pad and zone plays (src[]) and gives each
 * sample its decoder checkpoints (one every 256 samples, dn_smp_checkpoints, main loop), so a voice can start anywhere.
 *
 * ONE-SHOT: 16 pads (DRUM's rows, the keys' first 16): each its sample, START and END, REVERSE, PITCH and FINE, LEVEL
 * and PAN, an AHD envelope (HOLD 127: to the end), a CHOKE group (a hit cuts the others in its group; a pad also cuts
 * itself), a low-pass (CUT, RES), DRIVE and CRUSH. A hit plays out whatever the gate.
 * KEYS: a key map of 8 zones, each a sample with its ROOT, its LOW..HIGH range (several zones make a multisample),
 * TUNE, LEVEL, START, and a LOOP (OFF; LOOP: round while it sounds; SUST: round while the key is held, then on to the
 * end), its points snapped to rising zero crossings (no click where it goes round). One ADSR, filter, drive and crush
 * for the keys; up to 4 voices (VOICES 1: mono).
 * Four voices in all, the oldest stolen. A voice cut (stolen, choked, at its END) leaves its last value fading out over
 * about a millisecond, so nothing clicks. */
#pragma once
#include <stdint.h>

#define DN_SMP_PADS 16u
#define DN_SMP_ZONES 8u
#define DN_SMP_VOICES 4u
#define DN_SMP_NSRC (DN_SMP_PADS + DN_SMP_ZONES)   /* src[]: pad i at i, zone j at 16 + j */
#define DN_SMP_RB 512u                              /* a reversed voice's window, samples */

enum { DN_SMP_ONESHOT, DN_SMP_KEYS };
enum { PD_SAMPLE, PD_START, PD_END, PD_REV, PD_PITCH, PD_FINE, PD_LEVEL, PD_PAN, PD_ATK, PD_HOLD, PD_DEC, PD_CHOKE,
       PD_CUT, PD_RES, PD_DRIVE, PD_CRUSH, PD_N };
enum { ZN_SAMPLE, ZN_ROOT, ZN_LOW, ZN_HIGH, ZN_TUNE, ZN_LEVEL, ZN_START, ZN_LOOP, ZN_LSTART, ZN_LEND, ZN_N };
enum { KY_ATK, KY_DEC, KY_SUS, KY_REL, KY_CUT, KY_RES, KY_DRIVE, KY_CRUSH, KY_VOICES, KY_N };
enum { DN_SMP_LOFF, DN_SMP_LLOOP, DN_SMP_LSUST };

typedef struct {                                    /* the settings (the app keeps them) */
    uint8_t mode;                                   /* DN_SMP_ONESHOT / KEYS */
    int16_t pad[DN_SMP_PADS][PD_N];                 /* SAMPLE -1: none */
    int16_t zone[DN_SMP_ZONES][ZN_N];
    int16_t key[KY_N];
} dn_smp_p_t;

typedef struct {                                    /* a sample as a pad or zone plays it (the app fills these) */
    const uint8_t *adpcm;                           /* 0: nothing */
    uint32_t n, rate;
    const uint32_t *ck;                             /* the decoder's state before sample 256 i: pred (16 bits), index */
    uint32_t nck;
    uint32_t lst, lend;                             /* a zone's loop, samples (dn_smp_zone_prepare: snapped) */
    int32_t lpred, lidx;                            /* the decoder's state before lst */
} dn_smp_src_t;

typedef struct {
    uint8_t on, kind, idx, note;                    /* kind 0 a pad, 1 a zone */
    uint8_t rev, loop, gate, stage;                 /* stage 1 attack, 2 hold, 3 decay / sustain, 4 release */
    const dn_smp_src_t *s;
    uint32_t ipos, pos, frac, inc, from, end;       /* ipos: the sample under a (Q0), pos: the next to decode */
    int32_t pred, dix, a, b;                        /* the decoder (forward) */
    int32_t env, sus;                               /* Q24 */
    uint32_t ka, kd, kr, hold, ctl;
    int32_t amp_now, amp_next;
    float gl, gr, fa1, fa2, fa3, fk, ic1, ic2, drv;
    uint8_t filt, crush;
    int32_t held;
    uint32_t cph;
    int32_t lastl, lastr;
    uint32_t age, rbase;
    int16_t rb[DN_SMP_RB];
} dn_smp_v_t;

typedef struct {
    dn_smp_p_t p;
    dn_smp_src_t src[DN_SMP_NSRC];
    dn_smp_v_t v[DN_SMP_VOICES];
    float res_l, res_r;                             /* what cut voices left, fading */
    uint32_t age;
    uint8_t last_pad, last_zone;                    /* (the screen: what played last) */
} dn_smp_t;

void dn_smp_init(dn_smp_t *s);
void dn_smp_pad_default(int16_t *pad);
void dn_smp_zone_default(int16_t *zone, uint32_t j);
/* main loop: a sample's decoder checkpoints into ck (max of them); returns how many */
uint32_t dn_smp_checkpoints(const uint8_t *adpcm, uint32_t n, uint32_t *ck, uint32_t max);
/* main loop (the app's lock held): zone j's loop points from its values, snapped to rising zero crossings */
void dn_smp_zone_prepare(dn_smp_t *s, uint32_t j);
/* audio */
void dn_smp_pad(dn_smp_t *s, uint32_t pad, uint32_t vel);
void dn_smp_note_on(dn_smp_t *s, uint32_t note, uint32_t vel);
void dn_smp_note_off(dn_smp_t *s, uint32_t note);
void dn_smp_cut(dn_smp_t *s);                       /* every voice out (declicked): the transport stopped */
void dn_smp_render(dn_smp_t *s, int32_t *lr, uint32_t n);   /* adds, stereo */
int dn_smp_zone_of(const dn_smp_t *s, uint32_t note);   /* the zone a key plays (-1 none) */
/* the values: names, ranges, text */
const char *dn_smp_pd_name(uint32_t k);
int32_t dn_smp_pd_min(uint32_t k);
int32_t dn_smp_pd_max(uint32_t k);
const char *dn_smp_zn_name(uint32_t k);
int32_t dn_smp_zn_min(uint32_t k);
int32_t dn_smp_zn_max(uint32_t k);
const char *dn_smp_ky_name(uint32_t k);
int32_t dn_smp_ky_min(uint32_t k);
int32_t dn_smp_ky_max(uint32_t k);
/* text for a value; kind 0 pad, 1 zone, 2 keys */
void dn_smp_text(uint32_t kind, uint32_t k, int32_t v, char out[8]);
