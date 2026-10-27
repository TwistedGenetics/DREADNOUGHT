/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: what the app asks of the machine under it (the panel, the lights, MIDI, the store, the sample slots,
 * the second core, the meters), for every target but the FM-1, whose platform declares the same calls itself.
 * Copyright (C) 2026 Jamie (TwistedGenetics); written 2026-10. Each target (tests/host/dn_host.c, plugin/core,
 * smk/) defines these. The panel is the FM-1's: its 14 buttons, 27 keys and 7 encoders; another machine maps its own
 * controls onto them. */
#pragma once
#include <stdint.h>

#ifndef DN_POOL
#define DN_POOL                         /* (where a target keeps the big buffers; nothing special by default) */
#endif

/* the buttons (bits of plat_buttons, plat_leds) */
enum { B_FX, B_SEL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC, B_OCTDN, B_OCTUP, NB };
/* the encoders */
enum { EN_SELECT, EN_ALGO, EN_PRESET, EN_K1, EN_K2, EN_K3, EN_K4, NE };
#define NKEYS 27                        /* the keys, F3 (bit 0) .. G5 */

/* time and the panel (main loop) */
uint32_t plat_ms(void);                 /* milliseconds since start */
uint32_t plat_buttons(void);            /* held now */
uint32_t plat_keys(void);               /* held now */
int32_t plat_enc(int role);             /* steps turned since the last call (clockwise +) */
uint32_t plat_master(void);             /* the volume, Q12 (4096 = unity) */
int plat_battery(void);                 /* 0..3 bars, 4 on USB power, -1 unknown */
void plat_leds(uint32_t buttons, uint32_t keys);
void plat_glow(int on);                 /* unlit buttons faintly lit (before plat_leds) */
void plat_keys_dim(uint32_t keys);      /* keys at the dim level (before plat_leds) */
void plat_play_red(int red);            /* PLAY's red light (before plat_leds) */

/* MIDI: packets are status | d1 << 8 | d2 << 16 as the app builds them (see dn_app.c) */
int plat_midi_in(uint32_t *pkt);        /* 1: one came (audio context) */
void plat_midi_out(uint32_t pkt);

/* the store: a few objects, each kept whole; the copy before the last one kept too */
#define PLAT_STORE_MAX 3840u
enum { OBJ_SET, OBJ_SOUND, OBJ_PAT0, OBJ_PAT1, OBJ_PANEL, OBJ_SONG, OBJ_MOTION, OBJ_NOBJ };
int plat_store_load(uint32_t obj, void *dst, uint32_t max);       /* bytes, or < 0: none */
int plat_store_save(uint32_t obj, const void *src, uint32_t len); /* 0: kept */
int plat_store_load_prev(uint32_t obj, void *dst, uint32_t max);

/* the sample slots (loops, DX7 voices, wavetables): read in place */
#define PLAT_NSLOTS 3
#define PLAT_SLOT_ZONES 16
int plat_slot_zones(int k);             /* how many in slot k (0: empty) */
const uint8_t *plat_slot(int k, int z, uint32_t *nsamples, uint32_t *rate, char name[9]);

/* the second core (a target with one says 0 / -1 everywhere) */
int plat_cpu2_set(int on);
int plat_cpu2_on(void);
int plat_cpu2_run(void (*fn)(uint32_t), uint32_t arg);  /* 1: taken */
int plat_cpu2_wait(void);                               /* 0: done; -1: do it here */
int plat_cpu2_gave_up(void);
uint32_t plat_cpu2_bad(void);
int plat_cpu2_barred(void);

/* the meters (PERF) */
uint32_t plat_cpu_pct(void);
uint32_t plat_cpu_peak_pct(void);
uint32_t plat_xruns(void);
int plat_vdc14_mv(void);
void plat_perf_init(void);
uint32_t plat_cycles(void);
int plat_cycles_cpu(void);
uint32_t plat_cycles_hz(void);
uint32_t plat_ticks24(void);
uint32_t plat_irq_stack_used(void);
uint32_t plat_irq_stack_size(void);
int plat_stalls(uint32_t s[3]);
void plat_stalls_enable(int on);
