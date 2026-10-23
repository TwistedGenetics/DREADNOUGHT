/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT's start-up screen: the name, a reese's beat (two saws a few cents apart: the sum's swell and fade),
 * the version. Copyright (C) 2026 Jamie (TwistedGenetics). A unity-build fragment after dn_app.c (its colours). */
static void dn_splash(const char *version)
{
    int32_t x, y, py = 0;
    uint32_t pa = 0, pb = 0;
    const uint32_t ia = 156180629u, ib = 156180629u + 29282618u;   /* 8 cycles across, against 9.5: 1.5 beats */
    lcd_fill(0, 0, 240, 240, DN_C_BAR);
    cv_begin(240, 140, DN_C_BAR);
    cv_text(120 - text_w(&FONT_M, "DREADNOUGHT") / 2, 12, &FONT_M, "DREADNOUGHT", DN_C_RED);   /* (FONT_L: digits) */
    cv_text(120 - text_w(&FONT_T, "DARK JUNGLE  /  DRUM & BASS") / 2, 42, &FONT_T, "DARK JUNGLE  /  DRUM & BASS",
            DN_C_LABEL);
    cv_rect(10, 64, 220, 50, DN_C_WELL);
    cv_rect(10, 89, 220, 1, DN_C_GRID);
    for (x = 0; x < 220; x++) {
        int32_t s = ((int32_t)(pa >> 17) - 16384) + ((int32_t)(pb >> 17) - 16384);   /* two saws, Q15 each half */
        y = 89 - (s * 22) / 32768;
        if (x)
            cv_line(9 + x, py, 10 + x, y, DN_C_BLUE);
        py = y;
        pa += ia;
        pb += ib;
    }
    cv_text(120 - text_w(&FONT_XS, version) / 2, 136 - 15, &FONT_XS, version, DN_C_DIMTXT);
    cv_blit(0, 50);
}
