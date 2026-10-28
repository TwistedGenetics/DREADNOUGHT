/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * DREADNOUGHT on the SMK-37 Pro / Elite: what the APP's parts tell each other. Copyright (C) 2026 Jamie
 * (TwistedGenetics). The parts:
 *   screen.c       the app itself (src/app over src/core), the panel, the screen, the lights; the control task's hooks
 *   transfer.c     the store and the sample slots in the user flash; the editor's SysEx (F0 7D 46 4C ...)
 *   midi_router.c  MIDI in and out; the hooks of M-VAVE's instrument, which DREADNOUGHT does not use
 *   project.c      the rest of those hooks (projects, the DX7 library, samples), empty
 *   board/audio.c  M-VAVE's audio interrupt, rendering DREADNOUGHT instead of M-VAVE's instrument */
#pragma once
#include <stddef.h>
#include <stdint.h>

/* screen.c */
void dn_smk_render(int32_t *q23, unsigned frames);   /* the audio interrupt: one block, stereo, Q23 */
extern volatile uint8_t dn_smk_ready;
extern volatile uint8_t dn_smk_volume;               /* 0..127: the output's level (the 8th encoder; audio_output.c) */

/* transfer.c (control task) */
void dn_smk_store_init(void);
int dn_smk_store_load(uint32_t obj, void *dst, uint32_t max);
int dn_smk_store_save(uint32_t obj, const void *src, uint32_t len);
int dn_smk_slot_zones(int k);
const uint8_t *dn_smk_slot(int k, int z, uint32_t *nsamples, uint32_t *rate, char name[9]);
void dn_smk_editor_poll(void);

/* midi_router.c */
int dn_smk_midi_in(uint32_t *pkt);                   /* the audio interrupt: one channel message (USB-MIDI packet) */
void dn_smk_midi_out(uint32_t pkt);                  /* any context */
void dn_smk_midi_poll(void);                         /* the control task: what is queued goes out */
int dn_smk_send(unsigned port, const uint8_t *m, size_t n);   /* control task: a whole message, any length */
