/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT as a plugin: the C core (the app and the engines, as the FM-1 runs them, on a simulated FM-1).
 * Copyright (C) 2026 Jamie (TwistedGenetics).
 *
 * Every call but dnc_screen_copy() and the state calls comes from one thread (the plugin's audio thread); the
 * plugin keeps the others apart with its own lock. One instance per process: the app keeps its state in globals. */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define DNC_RATE 44100u          /* the engines' rate: the plugin resamples to the host's */
#define DNC_W 240u
#define DNC_H 240u
#define DNC_NBTN 14              /* FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+ */
#define DNC_NENC 7               /* SELECT ALGO PRESET K1 K2 K3 K4 */
#define DNC_NKEYS 27             /* F3 .. G5 */

void dnc_init(void);                                 /* the FM-1 switched on (from what dnc_state_load gave) */
/* n frames at 44.1 kHz, stereo interleaved, as floats (-1..1); the panel is read every millisecond and the screen
 * drawn every 16, as the FM-1 does */
void dnc_render(float *lr, uint32_t n);
void dnc_idle(uint32_t ms);                          /* the panel and the screen without the sound (host not playing) */

void dnc_button(int b, int down);
void dnc_key(int k, int down);
void dnc_turn(int enc, int detents);
void dnc_all_up(void);

/* MIDI from the host: channels 1-8 play tracks 1-8, channels 9-16 the track the screen shows (the keys' track).
 * A BREAKS track (or SAMPLER's pads) takes note 36 (C1) as its first slice. */
void dnc_midi(uint8_t st, uint8_t d1, uint8_t d2);
int dnc_midi_out(uint8_t msg[3]);                    /* MIDI tracks' notes for the host: 1 = got one */
/* the host's transport: its tempo (BPM) and whether it plays (the sequencer starts and stops with it) */
void dnc_transport(int playing, double bpm);
int dnc_playing(void);
uint32_t dnc_bpm(void);

uint32_t dnc_screen_serial(void);                    /* changes when the screen does */
void dnc_screen_copy(uint32_t *argb);                /* 240 x 240, 0xFFRRGGBB */
uint32_t dnc_leds_buttons(void);                     /* lit: bit = button */
uint32_t dnc_leds_keys(void);
uint32_t dnc_leds_keys_dim(void);

/* the project (what SAVE keeps, as the FM-1's flash does): SAVE first, then the bytes; load, then dnc_init */
uint32_t dnc_state_max(void);
uint32_t dnc_state_save(uint8_t *out, uint32_t max);
int dnc_state_load(const uint8_t *in, uint32_t n);    /* 0 = ok */

/* (0.45) the web editor's frames (F0 7D 46 4C cmd args F7: the FM-1's), from the plugin's MIDI port: 1 = one of
 * them (answered); the answers, one frame at a time (its length; 0: none) */
int dnc_sysex(const uint8_t *msg, uint32_t n);
uint32_t dnc_sysex_out(uint8_t *out, uint32_t max);
uint32_t dnc_slots_used(void);                       /* KB the sample slots hold */

const char *dnc_version(void);

#ifdef __cplusplus
}
#endif
