/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * DREADNOUGHT on the SMK-37 Pro / Elite: the rest of the hooks of M-VAVE's instrument (its projects, its DX7 library,
 * its samples). DREADNOUGHT keeps its own project and samples (transfer.c), so these hold nothing. Copyright (C) 2026
 * Jamie (TwistedGenetics).
 *
 * keep(): the SDK's build checks that parts of M-VAVE's instrument are in the image; nothing of DREADNOUGHT's calls
 * them, so the linker would drop them. keep() names them behind a flag that is never set. */
#include "fm1.h"
#include "fm1_engine_control.h"
#include "instrument.h"
#include "dn_smk.h"
#include <string.h>

static struct groove_project scratch;
struct groove_project *groove_project_scratch(void) { return &scratch; }
void groove_project_init(void) { memset(&scratch, 0, sizeof scratch); }
void groove_project_name(char name[13]) { memcpy(name, "DREADNOUGHT ", 13); name[12] = 0; }
void groove_project_metadata(const struct groove_project *p) { (void)p; }
void groove_fm_library_init(void) {}
void groove_samples_init(void) {}
void groove_samples_poll(void) {}

volatile uint8_t dn_smk_keep_flag;
static __attribute__((noinline)) void keep(void)
{
    static struct groove_tuning t[GROOVE_TRACKS];
    static int32_t block[FM1_BLOCK_WORDS];
    (void)groove_tuning_capture(t);
    (void)groove_tuning_apply(t);
    (void)groove_project_apply_edit(&scratch, 0);
    (void)fm1_engine_render(block, FM1_BLOCK_FRAMES);
}
void groove_project_poll(void)
{
    if (dn_smk_keep_flag)
        keep();
}
