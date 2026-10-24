/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: the app's entry points (src/app/dn_app.c says who calls which). Copyright (C) 2026 Jamie
 * (TwistedGenetics). */
#pragma once
#include <stdint.h>
#include "reese.h"
#include "dn_dsp.h"
#include "dn_seq.h"
#include "dn_breaks.h"
#include "dn_lfo.h"
#include "dn_breaks_builtin.h"   /* (generated: tools/gen_dn_breaks.py) */

void dn_app_init(void);
void dn_app_render(int32_t *lr, uint32_t n);
void dn_app_midi(uint8_t st, uint8_t d1, uint8_t d2);
void dn_app_input(void);
void dn_app_draw(int force);
void dn_app_slots_changed(int done);     /* the sample slots: about to change (0), changed (1) */
uint32_t dn_app_slices_get(uint32_t key, uint32_t *starts);   /* a loop's manual slices (samples); 0: none */
int dn_app_slices_set(uint32_t key, uint32_t n, const uint32_t *starts);   /* 0: kept (and saved) */
/* the web editor's commands (src/app/dn_editor.c): 1 = ours, the reply's bytes given to put; 0 = not ours */
int dn_ed_cmd(uint32_t cmd, const uint8_t *a, uint32_t na, void (*put)(uint32_t));
