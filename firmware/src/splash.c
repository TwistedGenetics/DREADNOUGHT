/* SPDX-License-Identifier: GPL-3.0-only */
/* DREADNOUGHT boot splash (replaces SLOOP's sail logo, tools/gen_logo.py): the wordmark drawn with the large font,
 * DREAD in white and NOUGHT in the modulation blue, over a Reese: two detuned saws beating, drawn as one trace that
 * swells and hollows. No bitmap in flash: about 200 bytes of code. */
#ifndef UI_SMALL                 /* (2.7) compiled for size: not the audio path (gfx.c UI_SMALL); the host tests: nothing */
#define UI_SMALL
#endif
#define DN_BLUE RGB(64, 150, 255)

/* the Reese under the wordmark: saw(f) + saw(f * 1.012) drawn as one connected trace: the two saws drift apart
 * across the screen, so the shape hollows and fills as a Reese does */
UI_SMALL static void dn_reese_draw(uint32_t y0)
{
    uint32_t x, y, h = 44u;
    int32_t prev = -1;
    cv_begin(240, h, C_BLACK);
    for (x = 8; x < 232u; x++) {
        uint32_t p1 = (x * 4096u * 9u / 224u) & 4095u;                 /* 9 cycles across */
        uint32_t p2 = (x * 4096u * 9u * 1012u / (224u * 1000u) + 1365u) & 4095u;
        int32_t s = ((int32_t)p1 - 2048) + ((int32_t)p2 - 2048);        /* -4096 .. 4094 */
        int32_t yy = (int32_t)(h / 2u) - s * (int32_t)(h / 2u - 2u) / 4096;
        int32_t a = prev < 0 ? yy : prev, b = yy, t;
        if (a > b) t = a, a = b, b = t;
        for (y = (uint32_t)a; y <= (uint32_t)b && y < h; y++)
            cv_px[y * 240u + x] = swap16(DN_BLUE);
        prev = yy;
    }
    cv_blit(0, y0);
}

/* the boot screen: the wordmark, the Reese, the version, where it comes from */
UI_SMALL static void sloop_splash(void)
{
    int32_t x;
    const char *v = FELUCCA_VERSION;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    cv_begin(240, 36, C_BLACK);
    x = 120 - (int32_t)text_w(&FONT_L, "DREADNOUGHT") / 2;
    x = cv_text(x, 0, &FONT_L, "DREAD", C_WHITE);
    cv_text(x, 0, &FONT_L, "NOUGHT", DN_BLUE);
    cv_blit(0, 70);
    dn_reese_draw(118);
    if (v[0] == 'D' && v[1] == 'R' && v[11] == ' ')
        v += 12;                                        /* "DREADNOUGHT 0.1" -> "0.1" under the wordmark */
    cv_begin(240, 36, C_BLACK);
    cv_text(120 - (int32_t)text_w(&FONT_S, v) / 2, 0, &FONT_S, v, RGB(196, 196, 204));
    cv_text(120 - (int32_t)text_w(&FONT_S, "based on sloop + felucca") / 2, 18, &FONT_S, "based on sloop + felucca",
            RGB(96, 96, 104));
    cv_blit(0, 202);
}
