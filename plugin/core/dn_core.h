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

/* ---- (0.45) the GUI's API (plugin/core/dnc_api.c) ---- */
enum { DNC_SEL_NONE, DNC_SEL_PAD, DNC_SEL_ZONE, DNC_SEL_OP, DNC_SEL_LFO, DNC_SEL_MSLOT, DNC_SEL_TRACK };
#define DNC_SEC_MAX 8
typedef struct {
    const char *name;
    uint8_t sel;                                     /* DNC_SEL_*: the values belong to a pad, zone, operator ... */
    uint8_t n;
    uint16_t id[DNC_SEC_MAX];
} dnc_section_t;
typedef struct { uint8_t note, flags, len, vel, cond, rtim, rlen; int8_t rvel; } dnc_step_t;
typedef struct { const uint8_t *adpcm; uint32_t bytes, n, rate; } dnc_part_t;
#define DNC_ST_ON 1u
#define DNC_ST_SLIDE 2u
#define DNC_ST_ACC 4u
#define DNC_ST_RTRG 8u
#define DNC_CO_FXONLY 0x80u

int dnc_engine_count(void);
const char *dnc_engine_name(int e);
int dnc_track_engine(int t);
const char *dnc_track_name(int t);
int dnc_track(void);
void dnc_track_select(int t);
void dnc_track_set_engine(int t, int e);
int dnc_track_mute(int t);
void dnc_track_set_mute(int t, int m);
uint32_t dnc_track_colour(int t);
void dnc_audition(int t, int note, int vel, int on);

int32_t dnc_get(uint32_t id, int sel, int idx);
int32_t dnc_min(uint32_t id, int sel, int idx);
int32_t dnc_max(uint32_t id, int sel, int idx);
const char *dnc_name(uint32_t id, int sel, int idx);
void dnc_text(uint32_t id, int sel, int idx, int32_t v, char out[16]);
void dnc_set(uint32_t id, int sel, int idx, int32_t v);
uint32_t dnc_choices(uint32_t id, int sel, int idx, int32_t *out, uint32_t max);
uint32_t dnc_sections(int t, dnc_section_t *out, uint32_t max);
uint32_t dnc_fx_sections(dnc_section_t *out, uint32_t max);
uint32_t dnc_id_bpm(void);
uint32_t dnc_id_send(int b);                         /* SEND A / B (DNC_SEL_TRACK, the track) */
uint32_t dnc_id_special(int which);                  /* SAMPLER: 0 pad SAMPLE, 1 zone SAMPLE, 2-4 ROOT LOW HIGH, 5 MODE */
int dnc_smp_mode(int t);                             /* 0 ONE-SHOT, 1 KEYS, -1 not a SAMPLER */
uint32_t dnc_id_lfo_shape(void);

uint32_t dnc_preset_count(void);
int dnc_preset_current(void);
const char *dnc_preset_name(uint32_t i);
int dnc_preset_used(uint32_t i);
void dnc_preset_load(uint32_t i);
int dnc_user_store(uint32_t u, const char *name);

int dnc_pat_current(int t);
int dnc_pat_queued(int t);
uint32_t dnc_pat_used(int t);
void dnc_pat_select(int t, int n);
int dnc_track_len(int t);
void dnc_track_set_len(int t, int len);
int dnc_swing(void);
void dnc_set_swing(int s);
void dnc_step_get(int t, int i, dnc_step_t *out);
void dnc_step_set(int t, int i, const dnc_step_t *in);
void dnc_step_default(int t, dnc_step_t *out);
int dnc_playing_step(int t);
int dnc_is_pads(int t);
uint32_t dnc_pads(int t);
void dnc_cond_text(int c, char out[8]);
int dnc_cond_count(void);
int dnc_rtim_den(int i);
int dnc_rtim_count(void);
const char *dnc_chord_name(int c);
int dnc_chord_count(void);
const char *dnc_hitfx_name(int c);
int dnc_hitfx_count(void);
int dnc_track_chords(int t);
int dnc_track_hitfx(int t);
int dnc_transport_toggle(void);

int dnc_brk_info(int b, const uint8_t **adpcm, uint32_t *n, uint32_t *rate, uint32_t *starts, uint32_t *ns, int *manual);
int dnc_brk_slices_set(int b, const uint32_t *starts, uint32_t n);
int dnc_brk_of_track(int t);

uint32_t dnc_sample_count(void);
const uint8_t *dnc_sample(uint32_t i, uint32_t *n, uint32_t *rate, int *kind);
const char *dnc_sample_name(uint32_t i);
void dnc_sample_set_name(uint32_t i, const char *s);
int dnc_loop_of_sample(uint32_t i);
int dnc_samples_store(const dnc_part_t *p, uint32_t np);
uint32_t dnc_samples_room(void);
void dnc_ima_encode(const int16_t *s, uint32_t n, uint8_t *out);
void dnc_ima_decode(const uint8_t *b, uint32_t n, int16_t *out);
const char *dnc_message(void);
int dnc_dirty(void);
void dnc_save(void);
uint32_t dnc_auto_ids(int e, uint16_t *out);         /* the values instrument e offers the DAW (up to 8) */
int32_t dnc_auto_get(int e, uint32_t id);
void dnc_auto_set(int e, uint32_t id, int32_t v);
int32_t dnc_auto_min(int e, uint32_t id);
int32_t dnc_auto_max(int e, uint32_t id);
int dnc_skin(void);                                  /* the FM-1's skin: 0 BLOCKS, 1 LIGHT, 2 DARK */
void dnc_set_skin(int s);
void dnc_set_draw(int on);                           /* the FM-1's screen drawn too (off: the plugin's GUI only) */
uint32_t dnc_convert(const float *x, uint32_t n, uint32_t src_rate, uint32_t rate, int trim, uint8_t *out,
                     uint32_t out_max);

#ifdef __cplusplus
}
#endif
