/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: the app's entry points (src/app/dn_app.c says who calls which). Copyright (C) 2026 Jamie
 * (TwistedGenetics). */
#pragma once
#include <stdint.h>
#include "reese.h"
#include "dn_dsp.h"
#include "dn_seq.h"

void dn_app_init(void);
void dn_app_render(int32_t *lr, uint32_t n);
void dn_app_midi(uint8_t st, uint8_t d1, uint8_t d2);
void dn_app_input(void);
void dn_app_draw(int force);
