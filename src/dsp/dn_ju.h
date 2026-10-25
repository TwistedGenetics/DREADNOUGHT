/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: JUNO (0.36), a Juno-60 / 106-style polysynth, standalone (no platform, no UI). Copyright (C) 2026 Jamie
 * (TwistedGenetics).
 *
 * The Juno's voice, as its panel lays it out: one DCO with a saw and a pulse (on / off each; the pulse's width moved by
 * the LFO, set by hand or by the envelope: PWM), a square sub-oscillator an octave down and noise, its RANGE 16' 8'
 * 4' and the LFO on its pitch; the HPF (BOOST, FLAT, 2, 3: on the voices together, as the 106's); a resonant 4-pole
 * low-pass (the 80017's: here a zero-delay-feedback ladder that self-oscillates in tune) with the envelope (+ or
 * inverted), the LFO, the keys and the velocity on its cutoff; ONE ADSR for the filter and the amp, or the amp on the
 * gate alone (VCA: GATE); one triangle LFO for all the voices, its DELAY fading it in after the first key (FREE, or
 * KEY: it starts with that key), free or synced to the tempo; and the CHORUS: I, II and I+II, two delay lines swept in
 * opposite directions, left and right (the Juno's stereo). Six voices (VOICES 1: mono).
 *
 * Control every 32 samples (the envelopes, the LFO, the pitches and the cutoffs), the levels and the filter ramped
 * a sample at a time. */
#pragma once
#include <stdint.h>

#define DN_JU_NV 6u                                 /* voices */
#define DN_JU_CB 256u                               /* the chorus's delay lines (a power of 2: 5.8 ms) */
#define DN_JU_NDIV 12u                              /* a synced LFO's lengths */

/* the values (the app's pages; ranges in dn_ju_vmin / dn_ju_vmax) */
enum { JP_RANGE, JP_SAW, JP_PULSE, JP_SUB, JP_PWM, JP_PWMSRC, JP_NOISE, JP_DLFO,
       JP_CUT, JP_RES, JP_FENV, JP_FKEY, JP_HPF, JP_FPOL, JP_FLFO, JP_FVEL,
       JP_ATK, JP_DEC, JP_SUS, JP_REL, JP_VCA, JP_VVEL,
       JP_LRATE, JP_LDELAY, JP_LSYNC, JP_LMODE,
       JP_CHORUS, JP_LEVEL, JP_VOICES, JP_TRANS, JP_N };
enum { JC_OFF, JC_I, JC_II, JC_I_II, JC_N };       /* CHORUS */

typedef struct {
    uint8_t live, gate, note, vel;
    uint8_t st;                                     /* the envelope: 0 attack, 1 decay / sustain, 2 release */
    uint32_t seq;
    uint32_t ph, inc, sph, sinc;                    /* the DCO and its sub (half the rate) */
    float env;                                      /* the envelope, 0..1 */
    float amp, G;                                   /* last tick's level and filter (G = g / (1 + g)): ramped from */
    float s[4];                                     /* the ladder's four stages */
} dn_ju_v_t;

typedef struct {
    int16_t p[JP_N];
    dn_ju_v_t v[DN_JU_NV];
    uint32_t seq, nz;
    uint32_t lph;                                   /* the LFO's phase */
    float lfo;                                      /* ... its value now, -1..1, faded in (DELAY) */
    uint32_t lt;                                    /* ticks since the first key (DELAY) */
    uint8_t nheld;
    uint16_t bpm;                                   /* (the app's tempo: a synced LFO) */
    float hlp;                                      /* the HPF's state */
    float cb[DN_JU_CB];                             /* the chorus: its delay line, where it writes, its LFO, the
                                                     * delayed sound's low-pass (a BBD's darkness), L and R */
    uint32_t cw, cph;
    float clp[2];
    uint32_t busy;                                  /* (voices rendered in the last tick) */
} dn_ju_t;

void dn_ju_init(dn_ju_t *s);
void dn_ju_defaults(int16_t *p);                    /* INIT */
uint32_t dn_ju_preset_count(void);                  /* the factory sounds: INIT first */
const char *dn_ju_preset_name(uint32_t i);
void dn_ju_preset(uint32_t i, int16_t *p);
uint32_t dn_ju_voices(const dn_ju_t *s);
void dn_ju_note_on(dn_ju_t *s, uint8_t note, uint8_t vel);
void dn_ju_note_off(dn_ju_t *s, uint8_t note);
void dn_ju_all_off(dn_ju_t *s);
void dn_ju_cut(dn_ju_t *s);
void dn_ju_render(dn_ju_t *s, int32_t *lr, uint32_t n);   /* adds, stereo */
uint32_t dn_ju_sounding(const dn_ju_t *s);

const char *dn_ju_vname(uint32_t k);
int32_t dn_ju_vmin(uint32_t k);
int32_t dn_ju_vmax(uint32_t k);
void dn_ju_vtext(uint32_t k, int32_t v, const int16_t *p, char out[8]);
