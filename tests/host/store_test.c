/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT host test (0.29): X0X's storage.c as DREADNOUGHT builds it (tools/dn_patches.py): the two settings
 * objects EXT0 / EXT1 live in sample slot 3's top 16 KB (0xD8000..0xDBFFF), keep their A/B copies there, and never
 * touch slot 3's first 64 KB (to 0xD7FFF), the panel calibration (0xDC000) or X0X's own objects.
 * Copyright (C) 2026 Jamie (TwistedGenetics). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "app/plat.h"
static uint8_t flash[0x100000];
static uint32_t lo_touch = 0xFFFFFFFFu, hi_touch;
static void touch(uint32_t off, uint32_t n)
{
    if (off < lo_touch)
        lo_touch = off;
    if (off + n > hi_touch)
        hi_touch = off + n;
}
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, flash + off, n); return 0; }
static int st_erase(uint32_t off) { touch(off, 4096); memset(flash + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    touch(off, n);
    for (uint32_t i = 0; i < n; i++)
        flash[off + i] &= s[i];
    return 0;
}
#include "storage_dn.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    fails += !ok;
}
int main(void)
{
    uint8_t a[3840], b[3840], g[3840];
    memset(flash, 0xFF, sizeof flash);
    for (int i = 0; i < 3840; i++)
        a[i] = (uint8_t)(i * 7), b[i] = (uint8_t)(i * 13 + 1);
    memset(flash + 0xC8000, 0x5A, 0x10000);         /* slot 3's 64 KB of samples, the panel's calibration */
    memset(flash + 0xDC000, 0xA5, 0x1000);
    check("EXT0 saves", st_save(16, a, sizeof a) == 0);
    check("EXT1 saves", st_save(17, b, sizeof b) == 0);
    check("EXT0 saves again (its other copy)", st_save(16, b, 100) == 0);
    check("... and again", st_save(16, a, sizeof a) == 0);
    check("EXT0 loads what was saved last", st_load(16, g, sizeof g) == (int)sizeof a && !memcmp(g, a, sizeof a));
    check("EXT1 loads its own", st_load(17, g, sizeof g) == (int)sizeof b && !memcmp(g, b, sizeof b));
    check("EXT0's copy before: the 100 bytes", st_load_prev(16, g, sizeof g) == 100 && !memcmp(g, b, 100));
    printf("     written: 0x%05X..0x%05X\n", lo_touch, hi_touch - 1u);
    check("only 0xD8000..0xDBFFF written", lo_touch >= 0xD8000u && hi_touch <= 0xDC000u);
    {
        int same = 1;
        for (uint32_t i = 0xC8000; i < 0xD8000; i++)
            same &= flash[i] == 0x5A;
        check("slot 3's first 64 KB untouched", same);
        same = 1;
        for (uint32_t i = 0xDC000; i < 0xDD000; i++)
            same &= flash[i] == 0xA5;
        check("the panel's calibration (0xDC000) untouched", same);
    }
    check("X0X's objects where they were: SET 0xFC000, SOUND 0x97000, SONG 0x9D000, PANEL 0xDC000",
          st_sector(OBJ_SET, 0) == 0xFC000u && st_sector(OBJ_SOUND, 0) == 0x97000u && st_sector(OBJ_SONG, 1) == 0x9E000u &&
          st_sector(OBJ_PANEL, 0) == 0xDC000u && st_sector(OBJ_MOTION, 1) == 0xDF000u);
    printf("%s\n", fails ? "STORE: FAILED" : "STORE: slot 3's top 16 KB holds the two settings objects, nothing else moved");
    return fails != 0;
}
